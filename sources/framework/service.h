/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 * 
 * Application service management
 */

#pragma once

#include "function.h"

#include <foundation/hash.h>

struct service_t;

typedef void (*service_initialize_handler_t)(void);
typedef void (*service_shutdown_handler_t)(void);

typedef function<void()> service_invoke_handler_t;

#define SERVICE_PRIORITY_UI 1
#define SERVICE_PRIORITY_TESTS 10

#define DEFINE_SERVICE(NAME, initialize_fn, shutdown_fn, ...) \
    static Service __##NAME##_service(#NAME, HASH_##NAME, initialize_fn, shutdown_fn, __VA_ARGS__)

class Service
{
public:
    Service(const char* FOUNDATION_RESTRICT name, hash_t service_hash,
        service_initialize_handler_t initialize_handler,
        service_shutdown_handler_t shutdown_handler,
        int priority);

    Service(const char* FOUNDATION_RESTRICT name, hash_t service_hash,
        service_initialize_handler_t initialize_handler,
        service_shutdown_handler_t shutdown_handler)
        : Service(name, service_hash, initialize_handler, shutdown_handler, INT_MAX)
    {
    }
};

void service_initialize();

void service_shutdown();

void service_register_handler(hash_t service_key, hash_t handler_key, const service_invoke_handler_t& handler);
void service_register_menu(hash_t service_key, const service_invoke_handler_t& menu_handler);
void service_register_tabs(hash_t service_key, const service_invoke_handler_t& tabs_handler);

void service_foreach(hash_t handler_key);
void service_foreach_menu();
void service_foreach_tabs();



