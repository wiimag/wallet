/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#pragma once

#include "function.h"
#include "string_table.h"

#include <foundation/array.h>

#include <imgui/imgui_internal.h>

typedef enum : unsigned int {
    COLUMN_OPTIONS_NONE = 0,
    COLUMN_SORTABLE = 1 << 0,
    COLUMN_LEFT_ALIGN = 1 << 1,
    COLUMN_RIGHT_ALIGN = 1 << 2,
    COLUMN_MIDDLE_ALIGN = 1 << 3,
    COLUMN_CENTER_ALIGN = COLUMN_MIDDLE_ALIGN,
    COLUMN_ZERO_USE_DASH = 1 << 4,
    COLUMN_FREEZE = 1 << 5,
    COLUMN_HIDE_DEFAULT = 1 << 6,
    COLUMN_STRETCH = 1 << 7,
    COLUMN_DYNAMIC_VALUE = 1 << 8,
    COLUMN_ROUND_NUMBER = 1 << 9,
    COLUMN_TEXT_WRAPPING = 1 << 10,
    COLUMN_HIDE_HEADER_TEXT = 1 << 11,
    COLUMN_CUSTOM_DRAWING = 1 << 12,
    COLUMN_NOCLIP_CONTENT = 1 << 13,
    COLUMN_NUMBER_ABBREVIATION = 1 << 14,
    COLUMN_SUMMARY_AVERAGE = 1 << 15,
    COLUMN_SEARCHABLE = 1 << 16,
    COLUMN_COMPUTE_SUMMARY = 1 << 17,
    COLUMN_RENDER_ELEMENT = 1 << 18,
    COLUMN_DEFAULT_SORT = 1 << 19,

    COLUMN_ALIGNMENT_MASK = COLUMN_LEFT_ALIGN | COLUMN_RIGHT_ALIGN | COLUMN_MIDDLE_ALIGN,
} column_flag_t;
typedef unsigned int column_flags_t;

typedef enum : size_t {
    TABLE_NONE = 0,
    TABLE_SUMMARY = 1ULL << 32,
    TABLE_HIGHLIGHT_HOVERED_ROW = 1ULL << 33
} table_flag_t;
typedef size_t table_flags_t;

typedef enum : unsigned int {
    COLUMN_FORMAT_UNDEFINED = 0,
    COLUMN_FORMAT_TEXT = 2,
    COLUMN_FORMAT_SYMBOL,
    COLUMN_FORMAT_NUMBER,
    COLUMN_FORMAT_CURRENCY,
    COLUMN_FORMAT_PERCENTAGE,
    COLUMN_FORMAT_DATE,
    COLUMN_FORMAT_LINK
} column_format_t;

typedef enum : unsigned int {
    COLUMN_COLOR_NONE       = 0,
    COLUMN_COLOR_TEXT       = 1 << 0,
    COLUMN_COLOR_BACKGROUND = 1 << 1
} column_style_type_t;
typedef unsigned int column_styles_t;

typedef void* table_element_ptr_t;
typedef const void* table_element_ptr_const_t;

struct cell_style_t
{
    column_styles_t types;

    struct {
        float x, y, width, height;
    } rect;

    unsigned int text_color;
    unsigned int background_color;
};

struct cell_t
{
    cell_t()
        : format(COLUMN_FORMAT_UNDEFINED)
        , text(nullptr)
        , length(0)
    {

    }

    FOUNDATION_FORCEINLINE cell_t(std::nullptr_t n)
        : cell_t()
    {
    }

    cell_t(const char* text, size_t text_length, column_format_t format = COLUMN_FORMAT_TEXT)
        : format(format)
        , text(text)
        , length(text_length)
    {

    }

    cell_t(string_const_t text, column_format_t format = COLUMN_FORMAT_TEXT)
        : cell_t(text.str, text.length, format)
    {
    }

    cell_t(const char* text, column_format_t format = COLUMN_FORMAT_TEXT)
        : cell_t(text, string_length(text), format)
    {
    }

    cell_t(string_table_symbol_t symbol)
        : format(COLUMN_FORMAT_TEXT)
    {
        string_const_t s = string_table_decode_const(symbol);
        text = s.str;
        length = s.length;
    }

    cell_t(double value, column_format_t format = COLUMN_FORMAT_NUMBER)
        : format(format)
        , number(value)
        , length(sizeof(value))
    {
    }

    cell_t(time_t time)
        : format(COLUMN_FORMAT_DATE)
        , time(time)
        , length(sizeof(time))
    {
    }

    column_format_t format{ COLUMN_FORMAT_UNDEFINED };

    union {
        const char* text{ nullptr };
        string_table_symbol_t symbol;
        double number;
        time_t time;
    };

    size_t length{0};
    cell_style_t style{};
};

struct table_t;
struct column_t;
struct row_t;

typedef function<cell_t(table_element_ptr_t element, const column_t* column)> cell_fetch_value_handler_t;
typedef function<void(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)> cell_callback_handler_t;
typedef function<void(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)> cell_style_handler_t;
typedef function<bool(table_element_ptr_t element)> table_update_cell_handler_t;
typedef function<bool(table_element_ptr_const_t element, const char*, size_t)> table_search_handler_t;
typedef function<bool(table_t* table, column_t*, int sort_direction)> table_sort_handler_t;
typedef function<void(table_t* table)> table_context_menu_handler_t;
typedef function<bool(table_t* table, row_t* row, table_element_ptr_t element)> table_row_handler_t;

struct column_t
{
    bool used{ false };

    string_table_symbol_t name;
    string_table_symbol_t alias;

    float width{ 0.0f };
    column_flags_t flags{ COLUMN_OPTIONS_NONE };
    column_format_t format{ COLUMN_FORMAT_TEXT };

    cell_fetch_value_handler_t fetch_value;
    cell_callback_handler_t context_menu;
    cell_callback_handler_t tooltip;
    cell_callback_handler_t selected;
    cell_style_handler_t style_formatter;

    hash_t hovered_cell{ 0 };
    tick_t hovered_time{ 0 };

    string_const_t get_name() const
    {
        return string_table_decode_const(name);
    }

    column_t& set_style_formatter(const cell_style_handler_t& handler)
    {
        style_formatter = handler;
        return *this;
    }

    column_t& set_context_menu_callback(const cell_callback_handler_t& handler)
    {
        context_menu = handler;
        return *this;
    }

    column_t& set_selected_callback(const cell_callback_handler_t& handler)
    {
        selected = handler;
        return *this;
    }

    column_t& set_tooltip_callback(const cell_callback_handler_t& handler)
    {
        tooltip = handler;
        return *this;
    }

    column_t& set_width(float _width)
    {
        this->width = _width;
        return *this;
    }
};

struct row_t
{
    table_element_ptr_t element;
    float height { 0 };
    bool fetched{ false };

    ImRect rect;
    ImU32 background_color{ 0 };
    bool hovered{ false };
};

struct table_t
{
    string_t name { nullptr, 0 };
    table_flags_t flags;

    column_t columns[64];

    table_element_ptr_const_t elements{ nullptr };
    int element_count{ 0 };
    size_t element_size{ 0 };

    row_t* rows{ nullptr };
    int rows_visible_count{ 0 };
    hash_t ordered_hash { 0 };

    int column_freeze{ 0 };
    bool needs_sorting { false };
    float max_row_height { 0 };
    tick_t last_sort_time{ 0 }; // Used to throttle sorting at startup
    string_const_t search_filter{ nullptr, 0 };

    table_search_handler_t search;
    table_search_handler_t filter;
    table_update_cell_handler_t update;
    table_sort_handler_t sort;
    cell_callback_handler_t context_menu;
    cell_callback_handler_t selected;
    table_row_handler_t row_begin;
    table_row_handler_t row_end;
};

struct table_sorting_context_t
{
    table_t* table;
    column_t* sorting_column;
    int sort_direction;
    bool completly_sorted{ true };
    string_const_t search_filter;
};

table_t* table_allocate(const char* name);
void table_deallocate(table_t* table);

size_t table_column_count(table_t* table);

void table_render(table_t* table, table_element_ptr_const_t elements, const int element_count, size_t element_size, float outer_size_x, float outer_size_y);

template<typename T>
void table_render(table_t* table, const T* elements, float outer_size_x = 0.0F, float outer_size_y = 0.0F)
{
    table_render(table, elements, array_size(elements), sizeof(T), outer_size_x, outer_size_y);
}

column_t& table_add_column(table_t* table, 
    const char* name, size_t name_length, 
    const cell_fetch_value_handler_t& fetch_value_handler,
    column_format_t format = COLUMN_FORMAT_TEXT,
    column_flags_t flags = COLUMN_OPTIONS_NONE);

template <size_t N> FOUNDATION_FORCEINLINE
column_t& table_add_column(table_t* table,
    const char(&name)[N],
    const cell_fetch_value_handler_t& fetch_value_handler,
    column_format_t format = COLUMN_FORMAT_TEXT,
    column_flags_t flags = COLUMN_OPTIONS_NONE)
{
    return table_add_column(table, name, N, fetch_value_handler, format, flags);
}

void table_cell_right_aligned_column_label(const char* label, void* payload);

void table_cell_right_aligned_label(const char* label, size_t label_length, const char* url = nullptr, size_t url_length = 0, float offset = 0.0f);

void table_cell_middle_aligned_column_label(const char* label, void* payload);

void table_cell_middle_aligned_label(const char* label, size_t label_length);
