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
    
} *_backend_module;

//
// ## PRIVATE
//

FOUNDATION_STATIC void backend_establish_connection()
{
    string_const_t url{};
    if (environment_argument("backend", &url, false))
        _backend_module->url = string_clone(STRING_ARGS(url));
    else
        _backend_module->url = string_clone(STRING_CONST("https://wallet.wiimag.com/"));

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
}

FOUNDATION_STATIC void backend_shutdown()
{
    string_deallocate(_backend_module->url);

    MEM_DELETE(_backend_module);
}

DEFINE_MODULE(BACKEND, backend_initialize, backend_shutdown, MODULE_PRIORITY_BASE-1);
