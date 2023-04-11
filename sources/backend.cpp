/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "backend.h"

#include "stock.h"

#include <framework/module.h>
#include <framework/session.h>
#include <framework/console.h>

#include <foundation/stream.h>

#define HASH_BACKEND static_hash_string("backend", 7, 0x22e7c7ffddbc5debULL)

static struct BACKEND_MODULE {

    string_t url{};
    
    bool connected{ false };

    char GOOGLE_SEARCH_API_KEY[64] = { 0 };
    
} *_backend_module;

//
// ## PRIVATE
//

FOUNDATION_STATIC void backend_load_google_search_api_key()
{
    string_const_t google_apis_key{};
    if (environment_argument("google-apis-key", &google_apis_key))
    {
        string_copy(STRING_BUFFER(_backend_module->GOOGLE_SEARCH_API_KEY), STRING_ARGS(google_apis_key));
        console_add_secret_key_token(STRING_ARGS(google_apis_key));
    }
    else
    {
        string_const_t key_file_path = session_get_user_file_path(STRING_CONST("google.key"));
        stream_t* key_stream = fs_open_file(STRING_ARGS(key_file_path), STREAM_IN);
        if (key_stream)
        {
            string_t key = stream_read_string(key_stream);
            string_copy(STRING_BUFFER(_backend_module->GOOGLE_SEARCH_API_KEY), STRING_ARGS(key));
            string_deallocate(key.str);
            stream_deallocate(key_stream);
        }
        else
        {
            // Clear key
            _backend_module->GOOGLE_SEARCH_API_KEY[0] = 0;
        }
    }

    if (_backend_module->GOOGLE_SEARCH_API_KEY[0])
        console_add_secret_key_token(STRING_LENGTH(_backend_module->GOOGLE_SEARCH_API_KEY));
}

FOUNDATION_STATIC void backend_establish_connection()
{
    string_const_t url{};
    if (environment_argument("backend", &url, false))
        _backend_module->url = string_clone(STRING_ARGS(url));
    else
        _backend_module->url = string_clone(STRING_CONST("http://localhost:8080"));

    const char* connect_status_query = string_format_static_const("%.*s/status", STRING_FORMAT(_backend_module->url));
    query_execute_async_json(connect_status_query, FORMAT_JSON_WITH_ERROR, [](const json_object_t& res)
    {
        if (!res.resolved())
        {
            _backend_module->connected = false;
            log_warnf(HASH_BACKEND, WARNING_NETWORK, STRING_CONST("Failed to connect to backend"));
            return;
        }

        _backend_module->connected = true;
        log_infof(HASH_BACKEND, STRING_CONST("Connected to backend"));
    });
}

//
// ## PUBLIC
//

bool backend_is_connected()
{
    return _backend_module && _backend_module->connected;
}

string_const_t backend_url()
{
    FOUNDATION_ASSERT(_backend_module);

    return string_to_const(_backend_module->url);
}

string_t backend_google_search_api_key()
{
    return {STRING_BUFFER(_backend_module->GOOGLE_SEARCH_API_KEY)};
}

string_t backend_set_google_search_api_key(const char* apikey)
{
    string_t k = string_copy(STRING_BUFFER(_backend_module->GOOGLE_SEARCH_API_KEY), apikey, string_length(apikey));
    if (_backend_module->GOOGLE_SEARCH_API_KEY[0])
        console_add_secret_key_token(STRING_LENGTH(_backend_module->GOOGLE_SEARCH_API_KEY));

    string_const_t key_file_path = session_get_user_file_path(STRING_CONST("google.key"));
    stream_t* key_stream = fs_open_file(STRING_ARGS(key_file_path), STREAM_CREATE | STREAM_OUT | STREAM_TRUNCATE);
    if (key_stream == nullptr)
        return k;

    log_infof(0, STRING_CONST("Writing key file %.*s"), STRING_FORMAT(key_file_path));
    stream_write_string(key_stream, STRING_ARGS(k));
    stream_deallocate(key_stream);

    return k;
}

bool backend_execute_news_search_query(const char* symbol, size_t symbol_length, const query_callback_t& callback)
{
    string_const_t google_apis_key = string_to_const(_backend_module->GOOGLE_SEARCH_API_KEY);
    if (!backend_is_connected() && string_is_null(google_apis_key))
        return false;

    string_t google_search_query = {};
    char google_search_query_buffer[2048];    
    string_const_t name = stock_get_short_name(symbol, symbol_length);

    // Check if we can resolve the query directly through the Google Search API if we have valid key.
    if (!string_is_null(google_apis_key))
    {
        google_search_query = string_format(STRING_BUFFER(google_search_query_buffer), 
            STRING_CONST("https://www.googleapis.com/customsearch/v1?key=%.*s&cx=7363b4123b9a84885&dateRestrict=d30&q=%.*s"),
            STRING_FORMAT(google_apis_key), STRING_FORMAT(name));
    }
    else
    {
        google_search_query = string_format(STRING_BUFFER(google_search_query_buffer), 
            STRING_CONST("%.*s/customsearch/v1?dateRestrict=d30&q=%.*s"),
            STRING_FORMAT(_backend_module->url), STRING_FORMAT(name));
    }

    char google_search_query_escaped[2048];
    string_escape_url(STRING_BUFFER(google_search_query_escaped), STRING_ARGS(google_search_query));
    return query_execute_async_json(google_search_query_escaped, FORMAT_JSON, callback);
}


//
// ## MODULE
//

FOUNDATION_STATIC void backend_initialize()
{
    _backend_module = MEM_NEW(HASH_BACKEND, BACKEND_MODULE);

    backend_establish_connection();
    backend_load_google_search_api_key();
}

FOUNDATION_STATIC void backend_shutdown()
{
    string_deallocate(_backend_module->url);

    MEM_DELETE(_backend_module);
}

DEFINE_MODULE(BACKEND, backend_initialize, backend_shutdown, MODULE_PRIORITY_BASE-1);
