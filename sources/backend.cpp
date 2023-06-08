/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */

#include "backend.h"

#include "stock.h"

#include <framework/app.h>
#include <framework/about.h>
#include <framework/module.h>
#include <framework/session.h>
#include <framework/console.h>
#include <framework/dispatcher.h>
#include <framework/query.h>
#include <framework/system.h>
#include <framework/glfw.h>

#include <foundation/stream.h>
#include <foundation/environment.h>
#include <foundation/version.h>
#include <foundation/path.h>

#define HASH_BACKEND static_hash_string("backend", 7, 0x22e7c7ffddbc5debULL)

#if BUILD_BACKEND
#   pragma message("Backend module is enabled")
#endif

static struct BACKEND_MODULE {

    string_t url{};
    
    bool connected{ false };
    
} *_backend_module;

//
// ## PRIVATE
//

FOUNDATION_STATIC const char* backend_platform_name_for_package()
{
    #if FOUNDATION_PLATFORM_WINDOWS
        return "windows";
    #elif FOUNDATION_PLATFORM_MACOS
        return "osx";
    #elif FOUNDATION_PLATFORM_LINUX
        return "linux";
    #else
        #error Unknown platform
    #endif
}

FOUNDATION_STATIC void backend_fetch_versions_callback(const json_object_t& res, bool use_notif)
{
    if (!res.resolved())
    {
        log_warnf(HASH_BACKEND, WARNING_NETWORK, STRING_CONST("Failed to get product versions at %.*s"), STRING_FORMAT(res.query));
        return;
    }

    string_const_t proto = path_protocol(STRING_ARGS(res.query));
    string_const_t host = path_strip_protocol(STRING_ARGS(res.query));
    if (host.length && host.str[0] == '/')
        host = string_const(host.str + 1, host.length - 1);
    const size_t spos = string_find(STRING_ARGS(host), '/', 0);
    if (spos == STRING_NPOS)
    {
        log_warnf(HASH_BACKEND, WARNING_NETWORK, STRING_CONST("Failed to get host from URL %.*s"), STRING_FORMAT(res.query));
        return;
    }

    host = string_const(host.str, spos);

    // Get current application version
    const application_t* app = environment_application();

    char current_version_string_buffer[32];
    string_t myversionstr = string_from_version(STRING_BUFFER(current_version_string_buffer), app->version);

    bool skip_no_update_check = false;
    
    // Scan versions for the most recent one
    auto versions = res["versions"];
    for (const auto e : versions)
    {
        string_const_t versionstr = e["version"].as_string();
        version_t version = string_to_version_short(STRING_ARGS(versionstr));

        if (app->version.sub.major > version.sub.major)
            continue;

        if (app->version.sub.major == version.sub.major && app->version.sub.minor > version.sub.minor)
            continue;

        if (app->version.sub.major == version.sub.major && app->version.sub.minor == version.sub.minor && app->version.sub.revision >= version.sub.revision)
            continue;

        const char* package_platform = backend_platform_name_for_package();

        // We got a new version
        string_const_t versiontag = e["version"].as_string();
        string_const_t url = e["package"][package_platform]["url"].as_string();
        if (url.length == 0)
            continue;

        string_const_t description = e["description"].as_string();

        char download_url_buffer[1024];
        string_t download_url = string_format(STRING_BUFFER(download_url_buffer), STRING_CONST("%.*s://%.*s/releases/%.*s"), 
            STRING_FORMAT(proto), STRING_FORMAT(host), STRING_FORMAT(versiontag));
        
        string_const_t titletr = tr(STRING_CONST("A new version is available"), true);

        char msg_buffer[1024];
        string_t msgtr = tr_format(STRING_BUFFER(msg_buffer), 
            "Currently you are using version {1}\n\n"
            "{3}\n\n"
            "Do you want to download version {0} and install it?\n\n"
            "This will close the application to launch the installer.", 
            versionstr, myversionstr, download_url, description);

        bool download_new_version = false;
        if (use_notif)
        {
            if (skip_no_update_check)
                break;

            download_new_version = false;

            titletr = tr_format_static("New version {0} available", versionstr);
            system_notification_push(STRING_ARGS(titletr), STRING_ARGS(description));
        }
        else
        {
            download_new_version = system_message_box(STRING_ARGS(titletr), STRING_ARGS(msgtr), true);
        }

        skip_no_update_check = true;

        if (download_new_version)
        {
            stream_t* new_version_download = query_execute_download_file(download_url.str);
            if (new_version_download)
            {
                string_const_t new_version_path = stream_path(new_version_download);

                char package_path_buffer[BUILD_MAX_PATHLEN];
                string_t package_path = string_copy(STRING_BUFFER(package_path_buffer), STRING_ARGS(new_version_path));
                log_debugf(HASH_BACKEND, STRING_CONST("Downloaded new version to %.*s"), STRING_FORMAT(new_version_path));
                stream_deallocate(new_version_download);

                // Rename file to .EXE
                string_t package_msi_path = string_concat(STRING_BUFFER(package_path_buffer), STRING_ARGS(package_path), STRING_CONST(".msi"));
                if (fs_move_file(STRING_ARGS(package_path), STRING_ARGS(package_msi_path)))
                {
                    log_debugf(HASH_BACKEND, STRING_CONST("Renamed new version to %.*s"), STRING_FORMAT(package_msi_path));
                    package_path = package_msi_path;

                    system_execute_command(STRING_ARGS(package_path));

                    dispatch([]() 
                    {
                        auto* main_window = glfw_main_window();
                        glfw_request_close_window(main_window);
                    });
                }
            }

            return;
        }
    }

    if (!use_notif && !skip_no_update_check)
    {
        char msg_buffer[1024];
        string_const_t titletr = tr(STRING_CONST("No update available"), true);
        string_t msgtr = tr_format(STRING_BUFFER(msg_buffer), "You are using the latest version {0}", myversionstr);
        system_message_box(STRING_ARGS(titletr), STRING_ARGS(msgtr), false);
        log_infof(HASH_BACKEND, STRING_CONST("Current version is %.*s is up-to-date."), STRING_FORMAT(myversionstr));
    }
}

FOUNDATION_STATIC bool backend_check_new_version_event(const dispatcher_event_args_t& args)
{
    backend_check_new_version(args.size > 0 ? true : false);
    return true;
}

FOUNDATION_STATIC void backend_establish_connection()
{
    string_const_t url{};
    if (environment_argument("backend", &url, false))
        _backend_module->url = string_clone(STRING_ARGS(url));
    else
        _backend_module->url = string_clone(STRING_CONST("https://wallet.wiimag.com"));

    const char* connect_status_query = string_format_static_const("%.*s/api/status", STRING_FORMAT(_backend_module->url));
    query_execute_async_json(connect_status_query, FORMAT_JSON_WITH_ERROR, [](const json_object_t& res)
    {
        if (!res.resolved())
        {
            _backend_module->connected = false;
            log_warnf(HASH_BACKEND, WARNING_NETWORK, STRING_CONST("Failed to connect to backend"));
            return;
        }

        _backend_module->connected = true;

        dispatcher_post_event(EVENT_BACKEND_CONNECTED);
        dispatcher_register_event_listener(EVENT_CHECK_NEW_VERSIONS, backend_check_new_version_event);

        bool use_notif = true;
        dispatcher_post_event(EVENT_CHECK_NEW_VERSIONS, &use_notif, sizeof(use_notif));

        tr_info(HASH_BACKEND, "Connected to backend");
    });
}

FOUNDATION_STATIC void backend_open_feedback_page(void* user_data)
{
    system_execute_command("https://wallet.wiimag.com/feedback");
}

FOUNDATION_STATIC bool backend_open_web_site(const dispatcher_event_args_t& args)
{
    return system_execute_command("https://wallet.wiimag.com");
}

//
// ## PUBLIC
//

bool backend_open_url(const char* url, size_t url_length, ...)
{
    // Remove leading slash
    if (url_length > 0 && url[0] == '/')
    {
        ++url;
        --url_length;
    }

    va_list args;
    va_start(args, url_length);
    string_t uri = string_vformat(SHARED_BUFFER(256), url, url_length, args);
    va_end(args);

    string_t urlstr = string_format(SHARED_BUFFER(2048), 
        STRING_CONST("%.*s/%.*s"), STRING_FORMAT(_backend_module->url), STRING_FORMAT(uri));

    return system_execute_command(STRING_ARGS(urlstr));
}

string_t backend_translate_text(const char* id, size_t id_length, const char* text, size_t text_length, const char* lang, size_t lang_length)
{
    if (!backend_is_connected())
        return {};

    const char* translate_url = string_format_static_const("%.*s/v2/translate?id=%.*s", STRING_FORMAT(_backend_module->url), (int)id_length, id);

    char text_buffer[8096];
    string_t formatted_text = string_copy(STRING_BUFFER(text_buffer), text, text_length);
    formatted_text = string_replace(STRING_ARGS(formatted_text), sizeof(text_buffer), STRING_CONST("\""), STRING_CONST("\\\""), true);

    char post_buffer[8096];
    string_t post_body = string_format(STRING_BUFFER(post_buffer), 
           STRING_CONST("{\"text\":[\"%.*s\"],\"target_lang\":\"%.*s\"}"), 
           min((int)formatted_text.length, 8000), formatted_text.str, (int)lang_length, lang);

    string_t translation{};
    query_execute_json(translate_url, FORMAT_JSON_WITH_ERROR, post_body, [id, id_length, &translation](const json_object_t& res)
    {
        if (!res.resolved())
        {
            log_warnf(HASH_BACKEND, WARNING_NETWORK, STRING_CONST("Failed to translate text for %.*s"), (int)id_length, id);
            return;
        }

        translation = res["translations"].get(0ULL)["text"].as_string_clone();
        translation = string_replace(STRING_ARGS_CAPACITY(translation), STRING_CONST("\\\""), STRING_CONST("\""), true);
    });

    if (translation.length == 0)
        translation = string_clone(text, text_length);
    else
    {
        log_infof(HASH_BACKEND, STRING_CONST("Translated text for %.*s"), (int)id_length, id);
    }
    return translation;
}

void backend_check_new_version(bool use_notif)
{
    // Use PRODUCT_VERSIONS_URL to check if a new version is available.
    query_execute_async_json(PRODUCT_VERSIONS_URL, FORMAT_JSON_WITH_ERROR, LC1(backend_fetch_versions_callback(_1, use_notif)));
}

bool backend_is_connected()
{
    return _backend_module && _backend_module->connected;
}

string_const_t backend_url()
{
    FOUNDATION_ASSERT(_backend_module);

    return string_to_const(_backend_module->url);
}

bool backend_execute_news_search_query(const char* symbol, size_t symbol_length, const query_callback_t& callback)
{
    if (!backend_is_connected())
        return false;

    char google_search_query_buffer[2048];    
    string_const_t name = stock_get_short_name(symbol, symbol_length);
    string_t google_search_query = string_format(STRING_BUFFER(google_search_query_buffer), 
        STRING_CONST("%.*s/customsearch/v1?dateRestrict=d30&q=%.*s"),
        STRING_FORMAT(_backend_module->url), STRING_FORMAT(name));

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

    dispatcher_register_event_listener(EVENT_ABOUT_OPEN_WEBSITE, backend_open_web_site);

    app_register_menu(HASH_BACKEND, 
        STRING_CONST("Help/Feedback"), 
        nullptr, 0, AppMenuFlags::Append, backend_open_feedback_page);
}

FOUNDATION_STATIC void backend_shutdown()
{
    string_deallocate(_backend_module->url);

    MEM_DELETE(_backend_module);
}

DEFINE_MODULE(BACKEND, backend_initialize, backend_shutdown, MODULE_PRIORITY_BASE-1);
