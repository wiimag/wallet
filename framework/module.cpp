/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "module.h"

#include <framework/array.h>
#include <framework/common.h>
#include <framework/profiler.h>

#define HASH_SERVICE_TABS (static_hash_string("service_tabs", 12, 0xeee279126075ccf8ULL))
#define HASH_SERVICE_MENU (static_hash_string("service_menu", 12, 0x597ea6b5d910db56ULL))
#define HASH_SERVICE_WINDOW (static_hash_string("service_window", 14, 0x576d11d2f45d4892ULL))
#define HASH_SERVICE_UPDATE (static_hash_string("service_update", 14, 0xbaa2a5e8e56e258aULL))
#define HASH_SERVICE_MENU_STATUS (static_hash_string("service_menu_status", 19, 0x200f262941438cb4ULL))

struct module_handler_t
{
    hash_t key;
    module_invoke_handler_t fn;
};

struct module_t
{
    hash_t key;
    char name[64];
    int priority;

    // Creation handlers
    module_initialize_handler_t initialize;
    module_shutdown_handler_t shutdown;

    // Runtime handlers
    module_handler_t* handlers{ nullptr };
};

/*! Modules are usually registered before main() is called,
 *  that is why we provide a fixed set of them.
 */
static module_t _modules[64];

/*! Number of registered modules. */
static size_t _module_count = 0;

/*! Flag to check if static modules have been initialized. */
static bool _modules_initialize = false;

//
// PRIVATE
// 

Module::Module(const char* FOUNDATION_RESTRICT name, hash_t service_hash,
    module_initialize_handler_t initialize_handler,
    module_shutdown_handler_t shutdown_handler,
    int priority)
{
    const size_t MAX_SERVICE_COUNT = ARRAY_COUNT(_modules);
    FOUNDATION_ASSERT(_modules_initialize == false);
    FOUNDATION_ASSERT_MSGFORMAT(_module_count < MAX_SERVICE_COUNT, "Too many services (%zu)", MAX_SERVICE_COUNT);

    module_t s{ service_hash };
    string_copy(STRING_BUFFER(s.name), name, string_length(name));
    s.priority = priority;
    s.initialize = initialize_handler;
    s.shutdown = shutdown_handler;

    _modules[_module_count++] = s;
    array_sort(_modules, _module_count, [](const auto& a, const auto& b) { return a.priority - b.priority; });
}

FOUNDATION_STATIC module_t* service_find(hash_t key)
{
    for (size_t i = 0, end = _module_count; i != end; ++i)
    {
        module_t& s = _modules[i];
        if (s.key == key)
            return &s;
    }

    return nullptr;
}

FOUNDATION_STATIC module_handler_t* service_find_handler(module_t* service, hash_t handler_key)
{
    for (size_t i = 0, end = array_size(service->handlers); i != end; ++i)
    {
        module_handler_t& h = service->handlers[i];
        if (h.key == handler_key)
            return &h;
    }

    return nullptr;
}

FOUNDATION_STATIC module_handler_t* service_get_or_create_handler(module_t* service, hash_t handler_key)
{
    module_handler_t* h = service_find_handler(service, handler_key);
    if (h)
        return h;

    array_push(service->handlers, module_handler_t{ handler_key });
    return array_last(service->handlers);
}

//
// # PUBLIC API
// 

void module_initialize()
{
    for (size_t i = 0, end = _module_count; i != end; ++i)
    {
        module_t& s = _modules[i];

        if (s.priority >= MODULE_PRIORITY_UI && main_is_batch_mode())
        {
            s.shutdown = nullptr;
            log_debugf(s.key, STRING_CONST("Service %s skipped (batch mode)"), s.name);
            continue;
        }
        
        log_debugf(s.key, STRING_CONST("Service %s initialization"), s.name);

        {
            PERFORMANCE_TRACKER_FORMAT("Service::%s", s.name);
            s.initialize();
        }
    }

    _modules_initialize = true;
}

void module_shutdown()
{
    for (int i = (int)_module_count - 1; i >= 0; --i)
    {
        module_t& s = _modules[i];
        memory_context_push(s.key);

        if (s.shutdown)
        {
            s.shutdown();
            log_debugf(s.key, STRING_CONST("Service %s shutdown"), s.name);
        }

        array_deallocate(s.handlers);
        memory_context_pop();
    }
}

void module_register_handler(hash_t service_key, hash_t handler_key, const module_invoke_handler_t& handler)
{
    module_t* s = service_find(service_key);
    FOUNDATION_ASSERT(s);

    memory_context_push(service_key);
    if (module_handler_t* h = service_get_or_create_handler(s, handler_key))
        h->fn = handler;
    memory_context_pop();
}

void module_register_menu(hash_t service_key, const module_invoke_handler_t& menu_handler)
{
    if (main_is_batch_mode())
        return;
        
    module_register_handler(service_key, HASH_SERVICE_MENU, menu_handler);
}

void module_register_tabs(hash_t service_key, const module_invoke_handler_t& tabs_handler)
{
    if (main_is_batch_mode())
        return;

    module_register_handler(service_key, HASH_SERVICE_TABS, tabs_handler);
}

void module_register_window(hash_t service_key, const module_invoke_handler_t& window_handler)
{
    if (main_is_batch_mode())
        return;

    module_register_handler(service_key, HASH_SERVICE_WINDOW, window_handler);
}

void module_register_update(hash_t service_key, const module_invoke_handler_t& update_handler)
{
    module_register_handler(service_key, HASH_SERVICE_UPDATE, update_handler);
}

void module_register_menu_status(hash_t service_key, const module_invoke_handler_t& menu_status_handler)
{
    if (main_is_batch_mode())
        return;
    module_register_handler(service_key, HASH_SERVICE_MENU_STATUS, menu_status_handler);
}

void module_foreach(hash_t handler_key)
{
    for (size_t i = 0, end = _module_count; i != end; ++i)
    {
        module_t& s = _modules[i];
        module_handler_t* handler = service_find_handler(&s, handler_key);
        if (handler && handler->fn)
        {
            memory_context_push(s.key);
            handler->fn();
            memory_context_pop();
        }
    }
}

void module_foreach_menu()
{
    module_foreach(HASH_SERVICE_MENU);
}

void module_foreach_menu_status()
{
    module_foreach(HASH_SERVICE_MENU_STATUS);
}

void module_foreach_tabs()
{
    module_foreach(HASH_SERVICE_TABS);
}

void module_foreach_window()
{
    module_foreach(HASH_SERVICE_WINDOW);
}

void module_update()
{
    module_foreach(HASH_SERVICE_UPDATE);
}

