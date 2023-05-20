/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * This module helps managing tables and columns to display reports.
 */

#pragma once

#include <framework/imgui.h>
#include <framework/function.h>
#include <framework/string_table.h>

#include <foundation/array.h>

struct table_row_t;
struct table_t;
struct table_column_t;

/*! Table flags that can define how table are displayed and what behavior they have. */
typedef enum : size_t {
    TABLE_DEFAULT_OPTIONS = 0,
    TABLE_SUMMARY = 1ULL << 32,
    TABLE_HIGHLIGHT_HOVERED_ROW = 1ULL << 33,
    TABLE_LOCALIZATION_CONTENT = 1ULL << 34
} table_flag_t;
typedef size_t table_flags_t;

/*! Column flags that can define how column are displayed and what behavior they have. */
typedef enum : unsigned int {

    /*! No flags. */
    COLUMN_OPTIONS_NONE = 0,

    /*! Column is sortable. */
    COLUMN_SORTABLE = 1 << 0,

    /*! Column is left aligned. */
    COLUMN_LEFT_ALIGN = 1 << 1,

    /*! Column is right aligned. */
    COLUMN_RIGHT_ALIGN = 1 << 2,

    /*! Column is middle aligned. */
    COLUMN_MIDDLE_ALIGN = 1 << 3,

    /*! Column is (middle) center aligned. */
    COLUMN_CENTER_ALIGN = COLUMN_MIDDLE_ALIGN,

    /*! Numeric column show a value of zero as a dash. */
    COLUMN_ZERO_USE_DASH = 1 << 4,

    /*! Column is frozen, meaning it will always be visible. */
    COLUMN_FREEZE = 1 << 5,

    /*! Column is hidden by default. The user needs to make it visible through 
        the table contextual menu when clicking on a column header. */
    COLUMN_HIDE_DEFAULT = 1 << 6,

    /*! Column is stretched to fill the remaining space. */
    COLUMN_STRETCH = 1 << 7,

    /*! Column is a dynamic value, meaning it will be computed at runtime. 
     *  This can be set to have sorting of these columns fetch the data while sorting.
     *  Not that this can make any sorting operation slower.
     */
    COLUMN_DYNAMIC_VALUE = 1 << 8,

    /*! Column is a number and should be rounded. */
    COLUMN_ROUND_NUMBER = 1 << 9,

    /*! Column header text is hidden. */
    COLUMN_HIDE_HEADER_TEXT = 1 << 11,

    /*! Column is using custom drawing. 
     *  Custom drawing can be used to draw anything in a table cell for that column.
     *  This is useful for drawing images, buttons, etc.
     * 
     *  In the fetch_value callback of the cell, you need to check the column flags for 
     *  #COLUMN_RENDER_ELEMENT before proceeding with the drawing, otherwise simply return a cell value.
     */
    COLUMN_CUSTOM_DRAWING = 1 << 12,

    /*! Column content is not clipped. */
    COLUMN_NOCLIP_CONTENT = 1 << 13,

    /*! Column number is abbreviated. */
    COLUMN_NUMBER_ABBREVIATION = 1 << 14,

    /*! When drawing the summary row, we make an average of the values instead of a sum. */
    COLUMN_SUMMARY_AVERAGE = 1 << 15,

    /*! Column is searchable. */
    COLUMN_SEARCHABLE = 1 << 16,

    /*! Column header is not localized. */
    COLUMN_NO_LOCALIZATION = 1 << 17,
    
    /*! This column will be used to first sort the table on first draw. */
    COLUMN_DEFAULT_SORT = 1 << 19,

    /*! Column cells are vertically aligned to the top. */
    COLUMN_VALIGN_TOP = 1 << 20,

    /*! This flags is dynamically set when calling #fetch_value to indicate that we are computing the summary row.
     *  This can be used to return a different value when computing the row summary.
     */
    COLUMN_COMPUTE_SUMMARY = 1 << 27,

    /*! This flags is dynamically set when calling #fetch_value to indicate that we are rendering the column.
     *  This can be used to return a different value when rendering the column.
     * 
     *  @see COLUMN_CUSTOM_DRAWING
     */
    COLUMN_RENDER_ELEMENT = 1 << 28,

    /*! This flags is dynamically set when calling #fetch_value to indicate that we are sorting the column.
     *  This can be used to return a different value when sorting the column.
     */
    COLUMN_SORTING_ELEMENT = 1 << 29,

    COLUMN_ALIGNMENT_MASK = COLUMN_LEFT_ALIGN | COLUMN_RIGHT_ALIGN | COLUMN_MIDDLE_ALIGN,
} column_flag_t;
typedef unsigned int column_flags_t;

/*! Column format that can define how column are displayed. */
typedef enum : unsigned int {

    /*! No format. */
    COLUMN_FORMAT_UNDEFINED = 0,

    /*! Column cells contain text */
    COLUMN_FORMAT_TEXT = 2,

    /*! Column cells contain string symbols (stored in the application global string table) */
    COLUMN_FORMAT_SYMBOL,

    /*! Column cells contain numbers */
    COLUMN_FORMAT_NUMBER,

    /*! Column cells contain currency values */
    COLUMN_FORMAT_CURRENCY,

    /*! Column cells contain percentages */
    COLUMN_FORMAT_PERCENTAGE,

    /*! Column cells contain dates */
    COLUMN_FORMAT_DATE,

    /*! Column render boolean value using a check mark */
    COLUMN_FORMAT_BOOLEAN,
} column_format_t;

/*! Column style types used to set some style value when #style_formatter is called. */
typedef enum : unsigned int {

    /*! No style. */
    COLUMN_COLOR_NONE       = 0,

    /*! Settings column cell text color. */
    COLUMN_COLOR_TEXT       = 1 << 0,

    /*! Settings column cell background color. */
    COLUMN_COLOR_BACKGROUND = 1 << 1
} column_style_type_t;
typedef unsigned int column_styles_t;

/*! Abstract table element pointer. */
typedef void* table_element_ptr_t;

/*! Abstract table element const pointer. */
typedef const void* table_element_ptr_const_t;

/*! Table cell styling properties. */
struct cell_style_t
{
    column_styles_t types;

    struct {
        float x, y, width, height;
    } rect;

    unsigned int text_color;
    unsigned int background_color;
};

/*! Table cell value. */
struct table_cell_t
{
    /*! Cell format, usually the same as the column. */
    column_format_t format{ COLUMN_FORMAT_UNDEFINED };

    /*! Cell value. 
     *  Depending on the format, this can be a text, a number, a symbol, etc.
     */
    union {
        const char* text{ nullptr };
        string_table_symbol_t symbol;
        double number;
        time_t time;
    };

    /*! Cell value length in case of string/symbol */
    size_t length{ 0 };

    /*! Cell styling. */
    cell_style_t style{};

    FOUNDATION_FORCEINLINE table_cell_t()
        : format(COLUMN_FORMAT_UNDEFINED)
        , text(nullptr)
        , length(0)
    {

    }

    FOUNDATION_FORCEINLINE table_cell_t(std::nullptr_t n)
        : table_cell_t()
    {
    }

    FOUNDATION_FORCEINLINE table_cell_t(const char* text, size_t text_length, column_format_t format = COLUMN_FORMAT_TEXT)
        : format(format)
        , text(text)
        , length(text_length)
    {

    }

    FOUNDATION_FORCEINLINE table_cell_t(string_const_t text, column_format_t format = COLUMN_FORMAT_TEXT)
        : table_cell_t(text.str, text.length, format)
    {
    }

    FOUNDATION_FORCEINLINE table_cell_t(const char* text, column_format_t format = COLUMN_FORMAT_TEXT)
        : table_cell_t(text, string_length(text), format)
    {
    }

    FOUNDATION_FORCEINLINE table_cell_t(string_table_symbol_t symbol)
        : format(COLUMN_FORMAT_TEXT)
    {
        string_const_t s = string_table_decode_const(symbol);
        text = s.str;
        length = s.length;
    }

    FOUNDATION_FORCEINLINE table_cell_t(double value, column_format_t format = COLUMN_FORMAT_NUMBER)
        : format(format)
        , number(value)
        , length(sizeof(value))
    {
    }

    FOUNDATION_FORCEINLINE table_cell_t(time_t time)
        : format(COLUMN_FORMAT_DATE)
        , time(time)
        , length(sizeof(time))
    {
    }

    FOUNDATION_FORCEINLINE table_cell_t(bool b)
        : format(COLUMN_FORMAT_BOOLEAN)
        , number(b ? 1.0 : 0.0)
        , length(1)
    {
    }
};

/*! Cell value handler
 *  @param element The element associated with the cell
 *  @param column  The column associated with the cell
 *  @return The cell value
 */
typedef function<table_cell_t(table_element_ptr_t element, const table_column_t* column)> cell_fetch_value_handler_t;

/*! Cell event handler
 *  @param element The element associated with the cell
 *  @param column  The column associated with the cell
 *  @param cell    The element cell being process
 *  @return The cell value
 */
typedef function<void(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)> cell_callback_handler_t;

/*! Cell style handler
 *  @param element The element associated with the cell
 *  @param column  The column associated with the cell
 *  @param cell    The element cell being process
 *  @param style   The style to be applied to the cell which should be modified by the handler.
 */
typedef function<void(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell, cell_style_t& style)> cell_style_handler_t;

/*! Callback invoked when a table cell value needs to be fetched.
 *  @param element The element associated with the cell
 *  @param column  The column associated with the cell
 *  @return The cell value
 */
typedef function<bool(table_element_ptr_t element)> table_update_cell_handler_t;

/*! Callback invoked when the table is being searched. 
 *  @param element The element being searched
 *  @param text    The search text
 *  @param length  The search text length
 */
typedef function<bool(table_element_ptr_const_t element, const char*, size_t)> table_search_handler_t;

/*! Callback invoked when the table is being sorted. 
 *  @param table          The table being sorted
 *  @param column         The column being sorted
 *  @param sort_direction The sort direction
 */
typedef function<bool(table_t* table, table_column_t*, int sort_direction)> table_sort_handler_t;

/*! Callback invoked when the main table contextual menu should be shown. 
 *  @param table The table being sorted
 */
typedef function<void(table_t* table)> table_context_menu_handler_t;

/*! Callback invoked when we are about to draw or drawing a table row. 
 *  @param table   The table being sorted
 *  @param row     The row being drawn
 *  @param element The element associated with the row
 */
typedef function<bool(table_t* table, table_row_t* row, table_element_ptr_t element)> table_row_handler_t;

/*! Callback invoked when we are about to draw or drawing a table column header. 
 *  @param table   The table being sorted
 *  @param column  The column being drawn
 *  @param element The element associated with the column
 */
typedef function<void(table_t* table, const table_column_t* column, int column_index)> column_header_render_handler_t;

/*! Column data structure */
struct table_column_t
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

    column_header_render_handler_t header_render;

    hash_t hovered_cell{ 0 };
    tick_t hovered_time{ 0 };
    table_t* table{ nullptr };

    FOUNDATION_FORCEINLINE string_const_t get_name() const
    {
        return string_table_decode_const(name);
    }

    /*! Sets the style formatter callback for a given column.
     *  The style formatter callback is called for each cell in the column
     * 
     *  @param handler The style formatter callback (See #cell_style_handler_t)
     *  @return The column
     */
    FOUNDATION_FORCEINLINE table_column_t& set_style_formatter(const cell_style_handler_t& handler)
    {
        style_formatter = handler;
        return *this;
    }

    FOUNDATION_FORCEINLINE table_column_t& set_context_menu_callback(const cell_callback_handler_t& handler)
    {
        context_menu = handler;
        return *this;
    }

    FOUNDATION_FORCEINLINE table_column_t& set_selected_callback(const cell_callback_handler_t& handler)
    {
        selected = handler;
        return *this;
    }

    FOUNDATION_FORCEINLINE table_column_t& set_tooltip_callback(const cell_callback_handler_t& handler)
    {
        tooltip = handler;
        return *this;
    }

    FOUNDATION_FORCEINLINE table_column_t& set_header_render_callback(const column_header_render_handler_t& handler)
    {
        header_render = handler;
        return *this;
    }

    FOUNDATION_FORCEINLINE table_column_t& set_width(float _width)
    {
        this->width = _width;
        return *this;
    }

    FOUNDATION_FORCEINLINE ImRect get_rect() const
    {
        return ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), ImGui::TableGetColumnIndex());
    }
};

/*! Row data structure */
struct table_row_t
{
    table_element_ptr_t element;
    float height { 0 };
    bool fetched{ false };

    ImRect rect;
    ImU32 background_color{ 0 };
    bool hovered{ false };
};

/*! Table data structure */
struct table_t
{
    string_t name { nullptr, 0 };
    table_flags_t flags;

    table_column_t columns[64];

    table_element_ptr_const_t elements{ nullptr };
    int element_count{ 0 };
    size_t element_size{ 0 };

    table_row_t* rows{ nullptr };
    int rows_visible_count{ 0 };
    hash_t ordered_hash { 0 };
    float row_fixed_height{ -1.0f };

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

    void* user_data{ nullptr };
};

/*! Table sorting context */
struct table_sorting_context_t
{
    table_t* table;
    table_column_t* sorting_column;
    int sort_direction;
    bool completly_sorted{ true };
    string_const_t search_filter;
};

/*! Create a new empty table. 
 *  @param name  The table name
 *  @param flags The table flags (See #table_flags_t)
 *  @return The table
 */
table_t* table_allocate(const char* name, table_flags_t flags = TABLE_DEFAULT_OPTIONS);

/*! Destroy a table. 
 *  @param table The table to destroy
 */
void table_deallocate(table_t* table);

/*! Returns the number of columns in the table. 
 *  @param table The table
 *  @return The number of columns
 */
size_t table_column_count(table_t* table);

/*! Render a table. 
 *  @param table            The table to render
 *  @param elements         The elements to render
 *  @param element_count    The number of elements
 *  @param element_size     The size of each element
 *  @param outer_size_x     The outer size of the table in X
 *  @param outer_size_y     The outer size of the table in Y
 */
void table_render(table_t* table, table_element_ptr_const_t elements, const int element_count, size_t element_size, float outer_size_x, float outer_size_y);

/*! Render a table using a dynamic array. 
 *  @param table            The table to render
 *  @param elements         The elements to render
 *  @param outer_size_x     The outer size of the table in X
 *  @param outer_size_y     The outer size of the table in Y
 */
template<typename T>
void table_render(table_t* table, const T* elements, float outer_size_x = 0.0F, float outer_size_y = 0.0F)
{
    table_render(table, elements, array_size(elements), sizeof(T), outer_size_x, outer_size_y);
}

/*! Add a new column to the table. 
 *  @param table                The table
 *  @param name                 The column name
 *  @param name_length          The column name length
 *  @param fetch_value_handler  The fetch value handler (See #cell_fetch_value_handler_t)
 *  @param format               The column format (See #column_format_t)
 *  @param flags                The column flags (See #column_flags_t)
 *  @return The column
 */
table_column_t& table_add_column(table_t* table, 
    const char* name, size_t name_length, 
    const cell_fetch_value_handler_t& fetch_value_handler,
    column_format_t format = COLUMN_FORMAT_TEXT,
    column_flags_t flags = COLUMN_OPTIONS_NONE);

/*! Add a new column to the table. 
 *  @param table                The table
 *  @param name                 The column name (string literal)
 *  @param fetch_value_handler  The fetch value handler (See #cell_fetch_value_handler_t)
 *  @param format               The column format (See #column_format_t)
 *  @param flags                The column flags (See #column_flags_t)
 *  @return The column
 */
template <size_t N> FOUNDATION_FORCEINLINE
table_column_t& table_add_column(table_t* table,
    const char(&name)[N],
    const cell_fetch_value_handler_t& fetch_value_handler,
    column_format_t format = COLUMN_FORMAT_TEXT,
    column_flags_t flags = COLUMN_OPTIONS_NONE)
{
    return table_add_column(table, name, N - 1, fetch_value_handler, format, flags);
}

/*! Add a new column to the table. 
 * 
 *  This function is a convenience function that allows to pass a lambda function as fetch value handler as the second parameter.
 * 
 *  @param table                The table
 *  @param fetch_value_handler  The fetch value handler (See #cell_fetch_value_handler_t)
 *  @param name                 The column name
 *  @template N                 The column name length
 *  @param format               The column format (See #column_format_t)
 *  @param flags                The column flags (See #column_flags_t)
 *  @return The column
 */
template <size_t N> FOUNDATION_FORCEINLINE
table_column_t& table_add_column(table_t* table,
    const cell_fetch_value_handler_t& fetch_value_handler,
    const char(&name)[N],
    column_format_t format = COLUMN_FORMAT_TEXT,
    column_flags_t flags = COLUMN_OPTIONS_NONE)
{
    return table_add_column(table, name, N - 1, fetch_value_handler, format, flags);
}

/*! Clear all columns from the table. 
 *  @param table The table
 */
void table_clear_columns(table_t* table);

/*! Render a table column header left aligned. 
 * 
 *  @param label        The column label
 *  @param payload      Payload passed from the table column handler
 */
void table_cell_left_aligned_column_label(const char* label, void* payload);

/*! Render a table cell aligned to the right. 
 * 
 *  @param label        The cell label
 *  @param payload      Payload passed from the table column handler
 */
void table_cell_right_aligned_column_label(const char* label, void* payload);

/*! Render a table cell aligned to the right. 
 * 
 *  @param label        The cell label
 *  @param label_length The cell label length
 *  @param url          The cell url
 *  @param url_length   The cell url length
 *  @param offset       The cell offset
 */
void table_cell_right_aligned_label(const char* label, size_t label_length, const char* url = nullptr, size_t url_length = 0, float offset = 0.0f);

/*! Render a table cell aligned to the middle. 
 * 
 *  @param label        The cell label
 *  @param payload      Payload passed from the table column handler
 */
void table_cell_middle_aligned_column_label(const char* label, void* payload);

/*! Render a table cell aligned to the middle. 
 * 
 *  @param label        The cell label
 *  @param label_length The cell label length
 */
void table_cell_middle_aligned_label(const char* label, size_t label_length);

/*! Render a table cell aligned to the left. 
 * 
 *  @param label        The cell label
 *  @param payload      Payload passed from the table column handler
 */
const ImRect& table_current_cell_rect();

/*! Returns the global default table row height. 
 *  @return The global default table row height
 */
float table_default_row_height();

/*! Sets the table search filter and updates the table. 
 *  @param table            The table
 *  @param filter           The filter string
 *  @param filter_length    The filter string length
 */
void table_set_search_filter(table_t* table, const char* filter, size_t filter_length);

/*! Export the table content into a csv file. 
 *  @param table            The table
 *  @param path             The path to the csv file
 *  @param length           The path length
 *  @return true if the file was successfully exported
 */
bool table_export_csv(table_t* table, const char* path, size_t length);
