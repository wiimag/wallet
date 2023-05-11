/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 *
 * The watches module is responsible for managing watch points.
 * A watch point is an expression that is evaluated to provide additional
 * information to the user based on a context. The context can be a report, 
 * a pattern, a stock, a transaction, etc.
 */

#pragma once

#include <framework/config.h>

struct table_t;

typedef enum {

    WATCH_POINT_UNDEFINED,
    WATCH_POINT_VALUE,
    WATCH_POINT_DATE,
    WATCH_POINT_INTEGER,
    WATCH_POINT_PLOT,
    WATCH_POINT_TABLE,

} watch_point_type_t;

typedef enum {

    WATCH_VALUE_UNDEFINED,
    WATCH_VALUE_NULL,
    WATCH_VALUE_TEXT,
    WATCH_VALUE_NUMBER,
    WATCH_VALUE_BOOLEAN,
    WATCH_VALUE_DATE,

} watch_value_type_t;

struct watch_value_t
{
    watch_value_type_t type;
    union {
        string_t       text;
        double         number;
    };
};

struct watch_context_t;
struct watch_point_t
{
    string_t             name;
    watch_point_type_t   type;
    string_t             expression;
    watch_value_t        record;

    watch_context_t*     context;
    char*                expression_edit_buffer;
    size_t               expression_edit_buffer_size;

    char                 name_buffer[64];
};

struct watch_variable_t
{
    string_t           name;
    watch_value_t      value;
};

struct watch_context_t
{
    string_t          name;
    watch_point_t*    points;
    watch_variable_t* variables;

    table_t*          table;

    char              name_buffer[64];
};

void watches_init();

void watches_shutdown();

watch_context_t* watch_create(const char* name, size_t length, config_handle_t data = nullptr);

void watch_save(watch_context_t* context, config_handle_t data);

void watch_destroy(watch_context_t*& context);

void watches_render(watch_context_t* context);

void watch_open_dialog(watch_context_t* context);

void watches_render_open_window(watch_context_t* context);

void watch_point_add(watch_context_t* context, 
    const char* name, size_t name_length, 
    const char* expression = nullptr, size_t expression_length = 0, 
    bool evaluate = true, bool edit = true);

void watch_set_variable(watch_context_t* context, const char* name, size_t name_length, time_t date);

void watch_set_variable(watch_context_t* context, const char* name, size_t name_length, double number);

void watch_set_variable(watch_context_t* context, const char* name, size_t name_length, const char* value, size_t value_length);
