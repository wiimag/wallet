/*
 * Copyright 2022-2023 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#include "service.h"

#include <framework/common.h>
#include <framework/profiler.h>

#include <foundation/array.h>

#include <algorithm>

#define HASH_SERVICE_TABS (static_hash_string("service_tabs", 12, 0xeee279126075ccf8ULL))
#define HASH_SERVICE_MENU (static_hash_string("service_menu", 12, 0x597ea6b5d910db56ULL))
#define HASH_SERVICE_WINDOW (static_hash_string("service_window", 14, 0x576d11d2f45d4892ULL))
#define HASH_SERVICE_MENU_STATUS (static_hash_string("service_menu_status", 19, 0x200f262941438cb4ULL))

struct service_handler_t
{
    hash_t key;
    service_invoke_handler_t fn;
};

struct service_t
{
    hash_t key;
    char name[64];
    int priority;

    // Creation handlers
    service_initialize_handler_t initialize;
    service_shutdown_handler_t shutdown;

    // Runtime handlers
    service_handler_t* handlers{ nullptr };
};

static service_t _services[64];
static size_t _service_count = 0;
static bool _services_initialize = false;

Service::Service(const char* FOUNDATION_RESTRICT name, hash_t service_hash,
    service_initialize_handler_t initialize_handler,
    service_shutdown_handler_t shutdown_handler,
    int priority)
{
    const size_t MAX_SERVICE_COUNT = ARRAY_COUNT(_services);
    FOUNDATION_ASSERT(_services_initialize == false);
    FOUNDATION_ASSERT_MSGFORMAT(_service_count < MAX_SERVICE_COUNT, "Too many services (%zu)", MAX_SERVICE_COUNT);

    service_t s{ service_hash };
    string_copy(STRING_CONST_CAPACITY(s.name), name, string_length(name));
    s.priority = priority;
    s.initialize = initialize_handler;
    s.shutdown = shutdown_handler;

    _services[_service_count++] = s;
    std::sort(&_services[0], &_services[0] + _service_count, [](const auto& a, const auto& b) { return a.priority < b.priority; });
}

FOUNDATION_STATIC service_t* service_find(hash_t key)
{
    for (size_t i = 0, end = _service_count; i != end; ++i)
    {
        service_t& s = _services[i];
        if (s.key == key)
            return &s;
    }

    return nullptr;
}

FOUNDATION_STATIC service_handler_t* service_find_handler(service_t* service, hash_t handler_key)
{
    for (size_t i = 0, end = array_size(service->handlers); i != end; ++i)
    {
        service_handler_t& h = service->handlers[i];
        if (h.key == handler_key)
            return &h;
    }

    return nullptr;
}

FOUNDATION_STATIC service_handler_t* service_get_or_create_handler(service_t* service, hash_t handler_key)
{
    service_handler_t* h = service_find_handler(service, handler_key);
    if (h)
        return h;

    array_push(service->handlers, service_handler_t{ handler_key });
    return array_last(service->handlers);
}

void service_initialize()
{
    for (size_t i = 0, end = _service_count; i != end; ++i)
    {
        service_t& s = _services[i];
        {
            PERFORMANCE_TRACKER_FORMAT("service::%s", s.name);
            s.initialize();
        }
        log_infof(s.key, STRING_CONST("Service %s initialized"), s.name);
    }

    _services_initialize = true;
}

void service_shutdown()
{
    for (int i = (int)_service_count - 1; i >= 0; --i)
    {
        service_t& s = _services[i];
        memory_context_push(s.key);

        if (s.shutdown)
        {
            s.shutdown();
            log_infof(s.key, STRING_CONST("Service %s shutdown"), s.name);
        }

        array_deallocate(s.handlers);
        memory_context_pop();
    }
}

void service_register_handler(hash_t service_key, hash_t handler_key, const service_invoke_handler_t& handler)
{
    service_t* s = service_find(service_key);
    FOUNDATION_ASSERT(s);

    memory_context_push(service_key);
    if (service_handler_t* h = service_get_or_create_handler(s, handler_key))
        h->fn = handler;
    memory_context_pop();
}

void service_register_menu(hash_t service_key, const service_invoke_handler_t& menu_handler)
{
    service_register_handler(service_key, HASH_SERVICE_MENU, menu_handler);
}

void service_register_tabs(hash_t service_key, const service_invoke_handler_t& tabs_handler)
{
    service_register_handler(service_key, HASH_SERVICE_TABS, tabs_handler);
}

void service_register_window(hash_t service_key, const service_invoke_handler_t& window_handler)
{
    service_register_handler(service_key, HASH_SERVICE_WINDOW, window_handler);
}

void service_register_menu_status(hash_t service_key, const service_invoke_handler_t& menu_status_handler)
{
    service_register_handler(service_key, HASH_SERVICE_MENU_STATUS, menu_status_handler);
}

void service_foreach(hash_t handler_key)
{
    for (size_t i = 0, end = _service_count; i != end; ++i)
    {
        service_t& s = _services[i];
        service_handler_t* handler = service_find_handler(&s, handler_key);
        if (handler && handler->fn)
        {
            memory_context_push(s.key);
            handler->fn();
            memory_context_pop();
        }
    }
}

void service_foreach_menu()
{
    service_foreach(HASH_SERVICE_MENU);
}

void service_foreach_menu_status()
{
    service_foreach(HASH_SERVICE_MENU_STATUS);
}

void service_foreach_tabs()
{
    service_foreach(HASH_SERVICE_TABS);
}

void service_foreach_window()
{
    service_foreach(HASH_SERVICE_WINDOW);
}

