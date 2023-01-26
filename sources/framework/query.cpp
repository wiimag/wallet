/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#include "query.h"

#include "common.h"
#include "config.h"
#include "session.h"
#include "scoped_string.h"
#include "progress.h"
#include "generics.h"
#include "concurrent_queue.h"
#include "profiler.h"

#include <foundation/log.h>
#include <foundation/hashstrings.h>
#include <foundation/array.h>
#include <foundation/thread.h>
#include <foundation/hash.h>
#include <foundation/time.h>
#include <foundation/fs.h>
#include <foundation/stream.h>
#include <foundation/environment.h>

#if FOUNDATION_PLATFORM_WINDOWS
    #undef APIENTRY
    #include "foundation/windows.h"
    #undef THREAD_PRIORITY_NORMAL
    #undef ERROR_ACCESS_DENIED

    #pragma comment( lib, "glfw3.lib" )
    #pragma comment( lib, "Ws2_32.lib" )
    #pragma comment( lib, "Wldap32.lib" )
    #pragma comment( lib, "Crypt32.lib" )
    #pragma comment( lib, "Normaliz.lib" )

    /* Notes to build libcurl
     *
     * 1. Open VS x64 console
     * 2. Goto curl dev folder
     * 3. > set RTLIBCFG=static
     * 4. > buildconf.bat
     * 5. > cd winbuild
     * 6. > nmake /f MakeFile.vc mode=static DEBUG=no
     * 7. > nmake /f MakeFile.vc mode=static DEBUG=yes
     */
    #if BUILD_RELEASE
        #pragma comment( lib, "libcurl_a.lib" )
    #else
        #pragma comment( lib, "libcurl_a_debug.lib" )
    #endif
#endif

#include <curl/curl.h>

#define HASH_QUERY static_hash_string("query", 5, 0x3e5d5820a8a43840)

#ifndef MAX_QUERY_THREADS
#define MAX_QUERY_THREADS 8
#endif

static bool _initialized = false;
static thread_t* _fetcher_threads[MAX_QUERY_THREADS] { nullptr };
static thread_local CURL* _req = nullptr;
static thread_local struct curl_slist* _req_json_header_chunk = nullptr;
static thread_t* _main_thread = nullptr;

struct json_query_request_t
{
    tick_t tick{};
    string_t query{};
    string_t body{};
    query_format_t format{};
    query_callback_t callback{};
    uint64_t invalid_cache_query_after_seconds{ 15ULL * 60ULL };

    json_query_request_t()
        : tick(time_current())
    {
    }

    bool operator<(const json_query_request_t& other) const
    {
        return tick < other.tick;
    }
};

static concurrent_queue<json_query_request_t> _fetcher_requests{};

FOUNDATION_STATIC void query_curl_cleanup()
{
    if (_req)
    {
        curl_easy_cleanup(_req);
        curl_slist_free_all(_req_json_header_chunk);
        _req = nullptr;
        _req_json_header_chunk = nullptr;
    }
}

FOUNDATION_STATIC CURL* query_create_curl_request()
{
    CURL* req = curl_easy_init();

    if (req)
    {
        curl_easy_setopt(req, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(req, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);

        if (environment_command_line_arg("verbose"))
            curl_easy_setopt(req, CURLOPT_VERBOSE, 1L);

        #if !BUILD_DEPLOY
        curl_easy_setopt(req, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(req, CURLOPT_SSL_VERIFYHOST, 0L);
        #endif

        if (_req_json_header_chunk == nullptr)
        {
            static thread_local char user_agent_header[256];
            const application_t* app = environment_application();
            string_format(STRING_CONST_CAPACITY(user_agent_header), STRING_CONST("user-agent: Infineis/%hu.%hu"), app->version.sub.major, app->version.sub.minor);
            _req_json_header_chunk = curl_slist_append(_req_json_header_chunk, user_agent_header);
            _req_json_header_chunk = curl_slist_append(_req_json_header_chunk, "Content-Type: application/json");
        }

        static thread_local bool register_cleanup = true;
        if (register_cleanup)
        {
            on_thread_exit(query_curl_cleanup);
            register_cleanup = false;
        }
    }

    return req;
}

FOUNDATION_STATIC CURL* query_get_or_create_curl_request()
{
    if (_req == nullptr)
        _req = query_create_curl_request();
    return _req;
}

struct CURLRequest
{
    CURLRequest()
        : req(query_get_or_create_curl_request())
        , status(CURLE_OK)
        , response_code(0)
    { 
        status = req != nullptr ? CURLE_OK : CURLE_FAILED_INIT;
    }

    virtual ~CURLRequest()
    {
        if (req && req != _req)
            curl_easy_cleanup(req);
    }

    virtual bool execute(const char* query)
    {
        curl_easy_setopt(req, CURLOPT_URL, query);
        curl_easy_setopt(req, CURLOPT_HTTPGET, 1L);

        status = curl_easy_perform(req);
        curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &response_code);
        if (status != CURLE_OK)
        {
            log_errorf(HASH_QUERY, ERROR_EXCEPTION,
                STRING_CONST("CURL %s (%d): %s"), curl_easy_strerror(status), status, query);
            return false;
        }

        return response_code < 400;
    }

    operator CURL*()
    {
        return req;
    }

    operator const CURL*()
    {
        return req;
    }

    operator bool()
    {
        return req != nullptr;
    }

protected:

    CURL* req;

public:

    CURLcode status;
    long response_code;
    
};

struct JSONRequest : public CURLRequest
{
    JSONRequest()
    {
        curl_easy_setopt(req, CURLOPT_WRITEDATA, &json);
        curl_easy_setopt(req, CURLOPT_HTTPHEADER, _req_json_header_chunk);
        curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, read_http_json_callback_func);
    }

    ~JSONRequest() override
    {
        if (json.str)
            string_deallocate(json.str);

        curl_easy_setopt(req, CURLOPT_WRITEDATA, nullptr);
        curl_easy_setopt(req, CURLOPT_HTTPHEADER, nullptr);
        curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, nullptr);
    }

    bool execute(const char* query) override
    {
        #if ENABLE_QUERY_MOCKING
        bool query_mock_success = false;
        if (query_mock_is_enabled(query, &query_mock_success, &json))
        {
            status = CURLE_OK;
            return query_mock_success;
        }
        #endif
        return CURLRequest::execute(query) && json.str != nullptr;
    }

    bool post(const char* query, string_t body)
    {
        curl_easy_setopt(req, CURLOPT_URL, query);
        curl_easy_setopt(req, CURLOPT_POSTFIELDSIZE, body.length);
        curl_easy_setopt(req, CURLOPT_POSTFIELDS, (const char*)body.str);

        status = curl_easy_perform(req);
        curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &response_code);

        curl_easy_setopt(req, CURLOPT_POSTFIELDSIZE, 0);
        curl_easy_setopt(req, CURLOPT_POSTFIELDS, nullptr);

        if (status != CURLE_OK)
        {
            log_errorf(HASH_QUERY, ERROR_EXCEPTION,
                STRING_CONST("CURL %s (%d): %s"), curl_easy_strerror(status), status, query);
            return false;
        }

        return response_code < 400;
    }

    bool post(const char* query, const config_handle_t& body)
    {
        config_sjson_const_t json_post_body = config_sjson(body, CONFIG_OPTION_WRITE_JSON);
        const size_t json_length = array_size(json_post_body);
        curl_easy_setopt(req, CURLOPT_URL, query);
        curl_easy_setopt(req, CURLOPT_POSTFIELDSIZE, json_length-1);
        curl_easy_setopt(req, CURLOPT_POSTFIELDS, (const char*)json_post_body);

        status = curl_easy_perform(req);
        curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &response_code);
        if (status != CURLE_OK)
        {
            log_errorf(HASH_QUERY, ERROR_EXCEPTION,
                STRING_CONST("CURL %s (%d): %s"), curl_easy_strerror(status), status, query);
        }

        curl_easy_setopt(req, CURLOPT_POSTFIELDSIZE, 0);
        curl_easy_setopt(req, CURLOPT_POSTFIELDS, nullptr);
        config_sjson_deallocate(json_post_body);

        return status == CURLE_OK && response_code < 400;
    }

    string_t json{};

private:

    static size_t read_http_json_callback_func(void* ptr, size_t size, size_t count, void* stream)
    {
        string_t* json = (string_t*)stream;
        if (json->str == nullptr)
        {
            *json = string_clone((const char*)ptr, size * count);
        }
        else
        {
            string_t newJson = string_allocate_concat(json->str, json->length, (const char*)ptr, size * count);
            string_deallocate(json->str);
            *json = newJson;
        }
        
        return count;
    }
};

bool query_execute_json(const char* query, query_format_t format, void(*json_callback)(const char* json, const json_token_t* tokens), uint64_t invalid_cache_query_after_seconds)
{
    return query_execute_json(query, format, [json_callback](const json_object_t& data)
    {
        json_callback(data.buffer, data.tokens);
    }, invalid_cache_query_after_seconds);
}

FOUNDATION_STATIC bool query_is_cache_file_valid(const char* query, query_format_t format, uint64_t invalid_cache_query_after_seconds, string_const_t& cache_file_path)
{
    cache_file_path = string_const_t{ nullptr, 0 };
    if (format != FORMAT_JSON_CACHE)
        return false;

    char query_hash_string_buffer[32] = { 0 };
    hash_t query_hash = hash(query, strlen(query));
    string_t query_hash_string = string_format(query_hash_string_buffer, sizeof(query_hash_string_buffer), STRING_CONST("%llx"), query_hash);
    cache_file_path = session_get_user_file_path(STRING_ARGS(query_hash_string), STRING_CONST("cache"), STRING_CONST("json"));
    if (!fs_is_file(STRING_ARGS(cache_file_path)))
        return false;

    // Check if we have a cache file for the given query that is not older than N hours
    const tick_t last_modified = fs_last_modified(STRING_ARGS(cache_file_path));
    const tick_t system_time = time_system();
    const uint64_t elapsed_seconds = (uint64_t)((system_time - last_modified) / 1000.0);
    return elapsed_seconds <= invalid_cache_query_after_seconds;
}

FOUNDATION_STATIC bool query_is_cache_file_valid(const char* query, query_format_t format, uint64_t invalid_cache_query_after_seconds)
{
    string_const_t cache_file_path{ nullptr, 0 };
    return query_is_cache_file_valid(query, format, invalid_cache_query_after_seconds, cache_file_path);
}

FOUNDATION_STATIC size_t query_upload_file_stream(char* buffer, size_t size, size_t nmemb, void* userdata)
{
    stream_t* fstream = (stream_t*)userdata;
    return (size_t)stream_read(fstream, buffer, size * nmemb);
}

bool query_execute_send_file(const char* query, query_format_t format, string_t file_path, const query_callback_t& callback)
{
    if (!fs_is_file(STRING_ARGS(file_path)))
    {
        log_errorf(HASH_QUERY, ERROR_UNKNOWN_RESOURCE, STRING_CONST("Cannot post file %.*s"), STRING_FORMAT(file_path));
        return false;
    }

    stream_t* fstream = fs_open_file(STRING_ARGS(file_path), STREAM_IN | STREAM_BINARY);
    if (fstream == nullptr)
        return false;

    JSONRequest req;
    if (!req)
        return false;

    curl_easy_setopt(req, CURLOPT_URL, query);

    curl_httppost *formpost = nullptr, *lastptr = nullptr;
    curl_formadd(&formpost, &lastptr,
        CURLFORM_COPYNAME, "file",
        CURLFORM_FILE, file_path.str,
        CURLFORM_CONTENTTYPE, "application/octet-stream",
        CURLFORM_END);

    char user_agent_header[256];
    const application_t* app = environment_application();
    string_format(STRING_CONST_CAPACITY(user_agent_header), STRING_CONST("user-agent: Infineis/%hu.%hu"), app->version.sub.major, app->version.sub.minor);
    curl_slist* headerlist = curl_slist_append(NULL, user_agent_header);
    headerlist = curl_slist_append(headerlist, "Expect:");

    curl_easy_setopt(req, CURLOPT_HTTPHEADER, headerlist);
    curl_easy_setopt(req, CURLOPT_HTTPPOST, formpost);

    req.status = curl_easy_perform(req);
    if (req.status != CURLE_OK)
    {
        log_errorf(HASH_QUERY, ERROR_EXCEPTION,
            STRING_CONST("CURL %s (%d): %s"), curl_easy_strerror(req.status), req.status, query);
    }
    else
    {
        curl_off_t speed_upload, total_time;
        curl_easy_getinfo(req, CURLINFO_SPEED_UPLOAD_T, &speed_upload);
        curl_easy_getinfo(req, CURLINFO_TOTAL_TIME_T, &total_time);

        log_infof(HASH_QUERY, STRING_CONST("File %.*s was uploaded (Speed: %lu bytes/sec during %lu.%06lu seconds)"),
            STRING_FORMAT(file_path),
            (unsigned long)speed_upload,
            (unsigned long)(total_time / 1000000),
            (unsigned long)(total_time % 1000000));
    }

    curl_formfree(formpost);
    curl_slist_free_all(headerlist);

    stream_deallocate(fstream);
    curl_easy_setopt(req, CURLOPT_READDATA, nullptr);
    curl_easy_setopt(req, CURLOPT_READFUNCTION, nullptr);
    curl_easy_setopt(req, CURLOPT_HTTPHEADER, nullptr);
    curl_easy_setopt(req, CURLOPT_HTTPPOST, nullptr);

    curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &req.response_code);
    if (callback)
    {
        json_object_t json = json_parse(req.json);
        json.status_code = req.response_code;
        json.error_code = req.response_code < 400 ? req.status : CURL_LAST;
        callback(json);
        signal_thread();
    }
    return req.status == CURLE_OK && req.response_code < 400;
}

bool query_execute_json(const char* query, query_format_t format, string_t body, const query_callback_t& callback, uint64_t invalid_cache_query_after_seconds /*= 0*/)
{
    MEMORY_TRACKER(HASH_QUERY);

    bool warning_logged = false;
    const bool has_body_content = !string_is_null(body);
    string_const_t cache_file_path{ nullptr, 0 };
    if (format == FORMAT_JSON_CACHE && !has_body_content)
    {
        if (query_is_cache_file_valid(query, format, invalid_cache_query_after_seconds, cache_file_path))
        {
            stream_t* cache_file_stream = fs_open_file(STRING_ARGS(cache_file_path), STREAM_IN | STREAM_BINARY);
            if (cache_file_stream != nullptr)
            {
                const size_t json_buffer_size = stream_size(cache_file_stream);
                log_debugf(HASH_QUERY, STRING_CONST("Fetch from cache query %s at %.*s"), query, STRING_FORMAT(cache_file_path));

                string_t json_buffer = string_allocate(json_buffer_size + 1, json_buffer_size + 2);
                scoped_string_t json_string = stream_read_string_buffer(cache_file_stream, json_buffer.str, json_buffer.length);

                const json_object_t& json = json_parse(json_string);
                stream_deallocate(cache_file_stream);

                if (json.root != nullptr)
                {
                    callback(json);
                    signal_thread();
                    return true;
                }
                else
                {
                    log_warnf(0, WARNING_PERFORMANCE, STRING_CONST("Failed to parse JSON from cache file for %s at %.*s"), query, STRING_FORMAT(cache_file_path));
                    warning_logged = true;
                }
            }
            else
            {
                log_warnf(0, WARNING_PERFORMANCE, STRING_CONST("Failed to open cache file for %s at %.*s"), query, STRING_FORMAT(cache_file_path));
                warning_logged = true;
            }
        }
        else
        {
            log_warnf(0, WARNING_PERFORMANCE, STRING_CONST("Updating query %s"), query);
            warning_logged = true;
        }
    }

    JSONRequest req;
    if (!req)
        return false;

    if (!warning_logged)
        log_debugf(HASH_QUERY, STRING_CONST("Execute query %s"), query);
    if ((has_body_content ? req.post(query, body) : req.execute(query)) || format == FORMAT_JSON_WITH_ERROR)
    {
        json_object_t json = json_parse(req.json);
        json.status_code = req.response_code;
        json.error_code = req.status > 0 ? req.status : (json.status_code >= 400 ? CURL_LAST : CURLE_OK);

        if (cache_file_path.length > 0 && format == FORMAT_JSON_CACHE && req.status == CURLE_OK && json.token_count > 0)
        {
            stream_t* cache_file_stream = fs_open_file(STRING_ARGS(cache_file_path), STREAM_CREATE | STREAM_OUT | STREAM_TRUNCATE);
            if (cache_file_stream == nullptr)
                return false;

            log_debugf(0, STRING_CONST("Writing query file %.*s"), STRING_FORMAT(cache_file_path));
            stream_write_string(cache_file_stream, json.buffer, strlen(json.buffer));
            stream_deallocate(cache_file_stream);
        }

        if (callback)
        {
            try
            {
                callback(json);
                signal_thread();
            }
            catch (...)
            {
                log_errorf(HASH_QUERY, ERROR_EXCEPTION, STRING_CONST("Failed to execute JSON callback for %s [%.*s...]"), query, 64, json.buffer);
                return false;
            }
        }
    }
    else if (format == FORMAT_JSON_WITH_ERROR)
    {
        json_object_t json{};
        json.status_code = req.response_code;
        json.error_code = req.status;
    }

    return req.status == CURLE_OK && req.response_code < 400;
}

bool query_execute_json(const char* query, query_format_t format, const query_callback_t& callback, uint64_t invalid_cache_query_after_seconds)
{
    return query_execute_json(query, format, {}, callback, invalid_cache_query_after_seconds);
}

bool query_execute_async_json(const char* query, const config_handle_t& body, const query_callback_t& callback)
{
    FOUNDATION_ASSERT(string_equal(query, 4, STRING_CONST("http")));
    const size_t query_length = string_length(query);
    log_debugf(HASH_QUERY, STRING_CONST("Queueing POST query [%zu] %.*s"), _fetcher_requests.size(), (int)query_length, query);
    json_query_request_t request{};
    request.query = string_clone(query, query_length);
    request.format = FORMAT_JSON_WITH_ERROR;
    request.callback = callback;

    if (body)
    {
        config_sjson_const_t json_body = config_sjson(body, CONFIG_OPTION_WRITE_JSON);
        request.body = string_clone(json_body, array_size(json_body) - 1);
        config_sjson_deallocate(json_body);
    }    

    request.invalid_cache_query_after_seconds = 0;
    _fetcher_requests.push(request);
    signal_thread();

    progress_set(min(_fetcher_requests.size(), ARRAY_COUNT(_fetcher_threads)), ARRAY_COUNT(_fetcher_threads));

    return true;
}

bool query_execute_async_json(const char* query, query_format_t format, const query_callback_t& json_callback, int ignore_if_queue_more_than /*= 0*/, uint64_t invalid_cache_query_after_seconds /*= 0*/)
{
    if (_fetcher_requests.size() > ignore_if_queue_more_than)
        thread_try_wait(60);

    if (!query_is_cache_file_valid(query, format, invalid_cache_query_after_seconds) && ignore_if_queue_more_than != 0 && _fetcher_requests.size() >= ignore_if_queue_more_than)
        return false;

    FOUNDATION_ASSERT(string_equal(query, 4, STRING_CONST("http")));
    const size_t query_length = string_length(query);
    log_debugf(HASH_QUERY, STRING_CONST("Queueing GET query [%zu] %.*s"), _fetcher_requests.size(), (int)query_length, query);
    json_query_request_t request;    
    request.query = string_clone(query, query_length);
    request.format = format;
    request.callback = json_callback;
    request.invalid_cache_query_after_seconds = invalid_cache_query_after_seconds;
    _fetcher_requests.push(request);

    progress_set(min(_fetcher_requests.size(), ARRAY_COUNT(_fetcher_threads)), ARRAY_COUNT(_fetcher_threads));

    return true;
}

bool query_execute_async_send_file(const char* query, string_t file_path, const query_callback_t& callback)
{
    FOUNDATION_ASSERT(string_equal(query, 4, STRING_CONST("http")));
    const size_t query_length = string_length(query);
    json_query_request_t request;
    request.query = string_clone(query, query_length);
    request.callback = callback;
    request.invalid_cache_query_after_seconds = 0;

    if (fs_is_file(STRING_ARGS(file_path)))
    {
        request.format = FORMAT_IN_FILE_OUT_JSON;
        request.body = string_clone(STRING_ARGS(file_path));
    }
    else
    {
        request.format = FORMAT_UNDEFINED;
    }

    _fetcher_requests.push(request);

    progress_set(min(_fetcher_requests.size(), ARRAY_COUNT(_fetcher_threads)), ARRAY_COUNT(_fetcher_threads));

    return true;
}

FOUNDATION_STATIC void* fetcher_thread_fn(void* arg)
{
    _req = query_create_curl_request();

    json_query_request_t req;
    while (!thread_try_wait(1))
    {
        if (_fetcher_requests.try_pop(req, 16))
        {
            if (req.format == FORMAT_IN_FILE_OUT_JSON)
            {
                query_execute_send_file(req.query.str, req.format, req.body, req.callback);
            }
            else if (!query_execute_json(req.query.str, req.format, req.body, req.callback, req.invalid_cache_query_after_seconds))
            {
                if (req.format != FORMAT_JSON_WITH_ERROR)
                {
                    log_errorf(HASH_QUERY, ERROR_EXCEPTION,
                        STRING_CONST("Failed to execute query %.*s"), STRING_FORMAT(req.query));
                }
            }

            string_deallocate(req.body.str);
            string_deallocate(req.query.str);
            if (_main_thread)
                thread_signal(_main_thread);
            
            signal_thread();
            progress_set(min(_fetcher_requests.size(), ARRAY_COUNT(_fetcher_threads)), ARRAY_COUNT(_fetcher_threads));
        }        
    }

    // Empty jobs before exiting thread (prevent memory leaks)
    while (_fetcher_requests.try_pop(req))
    {
        string_deallocate(req.body.str);
        string_deallocate(req.query.str);
    }
    return 0;
}

bool query_post_json(const char* url, const config_handle_t& post_data, const query_callback_t& callback)
{
    JSONRequest req;
    if (!req)
        return false;

    if (req.post(url, post_data))
    {
        json_object_t response = json_parse(req.json);
        response.status_code = req.response_code;
        response.error_code = req.status > 0 ? req.status : (response.status_code >= 400 ? CURL_LAST : CURLE_OK);

        if (callback)
        {
            try
            {
                callback(response);
                signal_thread();
            }
            catch (...)
            {
                log_errorf(HASH_QUERY, ERROR_EXCEPTION, STRING_CONST("Failed to post JSON or %s"), url);
                return false;
            }
        }
    }

    return req.status == CURLE_OK && req.response_code < 400;
}

FOUNDATION_STATIC int query_download_file_write_function(void* buffer, size_t size, size_t count, void* stream)
{
    stream_t* download_stream = (stream_t*)stream;
    return (int)stream_write(download_stream, (const void*)buffer, size * count);
}

stream_t* query_execute_download_file(const char* query)
{
    CURL* req = query_get_or_create_curl_request();

    stream_t* download_stream = fs_temporary_file();
    if (download_stream == nullptr)
        return nullptr;

    curl_easy_setopt(req, CURLOPT_URL, query);
    curl_easy_setopt(req, CURLOPT_WRITEDATA, download_stream);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, query_download_file_write_function);

    CURLcode status = curl_easy_perform(req);
    if (status != CURLE_OK)
    {
        log_errorf(HASH_QUERY, ERROR_EXCEPTION,
            STRING_CONST("CURL %s (%d): %s"), curl_easy_strerror(status), status, query);
        stream_deallocate(download_stream);
        return nullptr;
    }

    long response_code = 0;
    curl_easy_getinfo(req, CURLINFO_RESPONSE_CODE, &response_code);

    curl_easy_setopt(req, CURLOPT_WRITEDATA, nullptr);
    curl_easy_setopt(req, CURLOPT_WRITEFUNCTION, nullptr);

    if (response_code >= 400)
    {
        log_errorf(HASH_QUERY, ERROR_EXCEPTION,
            STRING_CONST("Failed to download file %s (%d)"), query, (int)response_code);

        stream_deallocate(download_stream);
        return nullptr;
    }

    return download_stream;
}

//
// # SYSTEM
//

void query_initialize()
{
    if (_initialized)
        return;

    _main_thread = thread_self();

    CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (res != CURLE_OK)
    {
        log_errorf(HASH_QUERY, ERROR_EXCEPTION, STRING_CONST("curl_global_init() failed(%d) : %s"), res, curl_easy_strerror(res));
        return;
    }

    _initialized = true;
    _req = query_create_curl_request();

    string_const_t query_cache_path = session_get_user_file_path(STRING_CONST("cache"));
    fs_make_directory(STRING_ARGS(query_cache_path));

    const size_t thread_count = sizeof(_fetcher_threads) / sizeof(_fetcher_threads[0]);
    _fetcher_requests.create();

    for (int i = 0; i < thread_count; ++i)
        _fetcher_threads[i] = thread_allocate(fetcher_thread_fn, nullptr, STRING_CONST("CURL HTTP Fetcher"), THREAD_PRIORITY_NORMAL, 0);

    for (int i = 0; i < thread_count; ++i)
        thread_start(_fetcher_threads[i]);

    #if ENABLE_QUERY_MOCKING
        query_mock_initialize();
    #endif
}

void query_shutdown()
{
    if (!_initialized)
        return;

    #if ENABLE_QUERY_MOCKING
        query_mock_shutdown();
    #endif

    const size_t thread_count = sizeof(_fetcher_threads) / sizeof(_fetcher_threads[0]);
    for (size_t i = 0; i < thread_count; ++i)
    {
        while (thread_is_running(_fetcher_threads[i]))
        {
            _fetcher_requests.signal();
            thread_signal(_fetcher_threads[i]);
        }
        thread_join(_fetcher_threads[i]);
    }

    _fetcher_requests.destroy();

    for (int i = 0; i < thread_count; ++i)
    {
        thread_deallocate(_fetcher_threads[i]);
        _fetcher_threads[i] = nullptr;
    }

    query_curl_cleanup();
    curl_global_cleanup();

    _main_thread = nullptr;
}
