/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "table.h"

#include "common.h"
#include "session.h"
#include "scoped_string.h"
#include "imgui.h"

#include <foundation/assert.h>
#include <foundation/math.h>
#include <foundation/array.h>
#include <foundation/time.h>

#include <mnyfmt.h>
#include <sys/timeb.h>
#include <algorithm>

#define ENABLE_ROW_HEIGHT_MIDDLE 1

static thread_local ImRect _table_last_cell_rect;

FOUNDATION_FORCEINLINE bool format_is_numeric(column_format_t format)
{
    return (format == COLUMN_FORMAT_NUMBER || format == COLUMN_FORMAT_CURRENCY || format == COLUMN_FORMAT_PERCENTAGE);
}

FOUNDATION_FORCEINLINE bool cell_format_is_numeric(const cell_t& cell)
{
    return format_is_numeric(cell.format);
}

FOUNDATION_STATIC string_const_t cell_number_value_to_string(const cell_t& cell, column_format_t format = COLUMN_FORMAT_UNDEFINED, column_flags_t flags = COLUMN_OPTIONS_NONE)
{
    if (math_real_is_nan(cell.number))
        return CTEXT("-");

    if (format_is_numeric(format) && (flags & COLUMN_ZERO_USE_DASH) && math_real_is_zero(cell.number))
        return CTEXT("-");

    double value = cell.number;
    if (flags & COLUMN_ROUND_NUMBER)
        value = math_round(value);
    double abs_value = math_abs(value);
    format = format == COLUMN_FORMAT_UNDEFINED ? cell.format : format;
    if (format == COLUMN_FORMAT_CURRENCY && abs_value > 999.99)
    {
        if (flags & COLUMN_NUMBER_ABBREVIATION)
        {
            if (abs_value >= 1e12)
                return string_format_static(STRING_CONST("%.3gT $"), value / 1e12);
            if (abs_value >= 1e9)
                return string_format_static(STRING_CONST("%.3gB $"), value / 1e9);
            else if (abs_value >= 1e6)
                return string_format_static(STRING_CONST("%.3gM $"), value / 1e6);
            else if (abs_value >= 1e3)
                return string_format_static(STRING_CONST("%.3gK $"), value / 1e3);
        }
        return string_from_currency(value, STRING_CONST("9 999 999.99 $"));
    }

    if (format == COLUMN_FORMAT_NUMBER && (flags & COLUMN_NUMBER_ABBREVIATION))
    {
        if (abs_value >= 1e9)
            return string_format_static(STRING_CONST("%.0lf B"), value / 1e9);
        else if (abs_value >= 1e6)
            return string_format_static(STRING_CONST("%.0lf M"), value / 1e6);
        else if (abs_value >= 1e3)
            return string_format_static(STRING_CONST("%.0lf K"), value / 1e3);
    }
    
    string_const_t format_string = CTEXT("%3.2lf");
    if (format == COLUMN_FORMAT_CURRENCY)
    {
        if (value == 0 || abs_value > 0.05)
            format_string = CTEXT("%.2lf $");
        else
            format_string = CTEXT("%.3lf $");
    }
    else if (format == COLUMN_FORMAT_PERCENTAGE)
    {
        if (abs_value < 0.1)
            format_string = CTEXT("%.2g %%"); 
        else if (abs_value < 1)
            format_string = CTEXT("%.2lf %%");
        else if (abs_value > 999)
            format_string = CTEXT("%.5g %%");
        else if (flags & COLUMN_ROUND_NUMBER || abs_value <= 100)
            format_string = CTEXT("%.3g %%"); 
        else
            format_string = CTEXT("%.4g %%");
    }
    else if (format == COLUMN_FORMAT_DATE)
        format_string = CTEXT("%x");

    string_t label_text = string_static_buffer(64);
    string_t formatted_value = string_format(STRING_ARGS(label_text), STRING_ARGS(format_string), value);

    if (format == COLUMN_FORMAT_NUMBER)
    {
        // Remove leading 0
        const char* c = formatted_value.str + formatted_value.length - 1;
        while (formatted_value.length > 0 && c > formatted_value.str)
        {
            char k = *c;
            if (k != '0' && k != '.')
                break;
            formatted_value.str[formatted_value.length - 1] = '\0';
            formatted_value.length--;
            if (k == '.')
                break;
            c--;
        }
    }

    return string_to_const(formatted_value);
}

FOUNDATION_STATIC string_const_t cell_value_to_string(const cell_t& cell, const column_t& column)
{
    if (cell.format == COLUMN_FORMAT_UNDEFINED)
        return CTEXT("-");
    
    if (cell.format == COLUMN_FORMAT_TEXT)
        return string_const(cell.text, cell.length);

    if (cell.format == COLUMN_FORMAT_SYMBOL)
        return string_table_decode_const(cell.symbol);

    if (cell_format_is_numeric(cell))
        return cell_number_value_to_string(cell, column.format, column.flags);

    if (cell.format == COLUMN_FORMAT_DATE)
        return cell.time == 0 ? CTEXT("-") : string_from_date(cell.time);
    
    FOUNDATION_ASSERT_FAILFORMAT("Column format %u is not supported", cell.format);
    return CTEXT("-");
}

FOUNDATION_STATIC void cell_label_wrapped(row_t& row, string_const_t label)
{
    if (!label.str || label.length == 0)
        return;

    ImGui::TextWrapped("%.*s", STRING_FORMAT(label));
}

FOUNDATION_STATIC void cell_label(string_const_t label)
{
    if (label.str && label.length > 0)
    {
        const float space = ImGui::GetContentRegionAvail().x;
        ImGui::TextUnformatted(label.str, label.str + label.length);
        if (ImGui::IsItemHovered() && ImGui::GetItemRectSize().x >space)
            ImGui::SetTooltip(" %.*s ", STRING_FORMAT(label));
    }
}

void table_cell_middle_aligned_label(const char* label, size_t label_length)
{
    const char* end_label = label_length > 0 ? label + label_length : label + strlen(label);
    const char* text_display_end = label;
    while (text_display_end < end_label&&* text_display_end != '\0' &&
        (text_display_end[0] != '|' || text_display_end[1] != '|'))
        text_display_end++;

    auto sx = ImGui::GetCursorPosX();
    auto cx = (sx + (ImGui::GetColumnWidth() - ImGui::CalcTextSize(label, text_display_end).x) / 2.0f);
    if (cx > sx)
        ImGui::SetCursorPosX(cx);
    ImGui::TextUnformatted(label, text_display_end);
}

void table_cell_right_aligned_label(const char* label, size_t label_length, const char* url /*= nullptr*/, size_t url_length /*= 0*/, float offset /*= 0.0f*/)
{
    const char* end_label = label_length > 0 ? label + label_length : label + strlen(label);
    const char* text_display_end = label;
    while (text_display_end < end_label && *text_display_end != '\0' &&
        (text_display_end[0] != '|' || text_display_end[1] != '|'))
        text_display_end++;

    auto sx = ImGui::GetCursorPosX();
    auto tx = ImGui::CalcTextSize(label, text_display_end).x;
    auto cx = (sx + ImGui::GetColumnWidth() - tx - ImGui::GetStyle().CellPadding.x / 2.0f) + offset;
    ImGui::SetCursorPosX(cx);
    if (url && url_length > 0)
        ImGui::TextURL(label, text_display_end, url, url_length);
    else
        ImGui::TextUnformatted(label, text_display_end);
    if (ImGui::IsItemHovered() && tx > ImGui::GetColumnWidth() * 1.05f)
        ImGui::SetTooltip(" %.*s ", (int)label_length, label);
}

void table_cell_right_aligned_column_label(const char* label, void* payload)
{
    FOUNDATION_UNUSED(payload);
    table_cell_right_aligned_label(label, 0, nullptr, 0, (ImGui::TableGetColumnFlags() & ImGuiTableColumnFlags_IsSorted) ? -10.0f : -1.0f);
}

void table_cell_middle_aligned_column_label(const char* label, void* payload)
{
    FOUNDATION_UNUSED(payload);
    table_cell_middle_aligned_label(label, 0);
}

FOUNDATION_STATIC int table_qsort_cells(void* pcontext, void const* va, void const* vb)
{
    row_t* ra = (row_t*)va;
    row_t* rb = (row_t*)vb;
    table_element_ptr_t a = ra->element;
    table_element_ptr_t b = rb->element;

    table_sorting_context_t* context = (table_sorting_context_t*)pcontext;
    const table_t* table = context->table;
    const column_t* sorting_column = context->sorting_column;
    const int pdiff = (int)pointer_diff(b, a);
    const bool sort_acsending = context->sort_direction == 1;
    if (sorting_column->flags & COLUMN_DYNAMIC_VALUE)
    {
        if (!ra->fetched && table->update)
        {
            if (!(ra->fetched = table->update(a)))
            {
                context->completly_sorted = false;
                return 1;
            }
        }

        if (!rb->fetched && table->update)
        {
            if (!(rb->fetched = table->update(b)))
            {
                context->completly_sorted = false;
                return 1;
            }
        }
    }

    const column_format_t format = sorting_column->format;
    const cell_t& ca = sorting_column->fetch_value(a, sorting_column);
    const cell_t& cb = sorting_column->fetch_value(b, sorting_column);

    if (format_is_numeric(format) || (format_is_numeric(ca.format) && format_is_numeric(cb.format)))
    {
        double sa = ca.number;
        double sb = cb.number;

        if (math_real_eq(sa, sb, 3))
            return 0;

        if (math_real_is_nan(sa)) return 1;
        if (math_real_is_nan(sb)) return -1;

        if (sa < sb)
            return sort_acsending ? -1 : 1;
        return sort_acsending ? 1 : -1;
    }

    if (format == COLUMN_FORMAT_DATE || (ca.format == COLUMN_FORMAT_DATE && cb.format == COLUMN_FORMAT_DATE))
    {
        time_t sa = ca.time;
        time_t sb = cb.time;

        if (sort_acsending)
        {
            if (sa == 0) sa = INT64_MAX;
            if (sb == 0) sb = INT64_MAX;
        }

        return (int)(sa - sb) * (sort_acsending ? 1 : -1);
    }

    if (ca.length == 0 && cb.length > 0)
        return 1;
    else if (ca.length > 0 && cb.length == 0)
        return -1;

    if (ca.text == cb.text || ca.format != COLUMN_FORMAT_TEXT || cb.format != COLUMN_FORMAT_TEXT)
        return 0;

    return strncmp(ca.text, cb.text, min(ca.length, cb.length)) * (sort_acsending ? 1 : -1);
}

FOUNDATION_STATIC bool table_default_sorter(table_t* table, column_t* sorting_column, int sort_direction)
{
    if (table == nullptr || sorting_column == nullptr)
        return true;

    table_sorting_context_t sorting_context{ table, sorting_column, sort_direction };
    sorting_context.search_filter = table->search_filter;
    #if FOUNDATION_PLATFORM_WINDOWS
    qsort_s(table->rows, table->rows_visible_count, sizeof(row_t), table_qsort_cells, &sorting_context);
    #else
    std::sort(table->rows, table->rows + table->rows_visible_count, [&sorting_context](const row_t& a, const row_t& b)
    {
        return table_qsort_cells(&sorting_context, &a, &b) < 0;
    });
    #endif

    return sorting_context.completly_sorted;
}

table_t* table_allocate(const char* name)
{
    void* table_mem = memory_allocate(0, sizeof(table_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
    table_t* new_table = new (table_mem) table_t();
    new_table->name = string_allocate_format(STRING_CONST("Table_%s_1"), name);
    new_table->sort = &table_default_sorter;
    return new_table;
}

void table_deallocate(table_t* table)
{
    if (table)
    {
        string_deallocate(table->name.str);
        array_deallocate(table->rows);
        table->~table_t();
        memory_deallocate(table);
    }
}

size_t table_column_count(table_t* table)
{
    size_t column_count = 0;
    const size_t max_column_count = sizeof(table->columns) / sizeof(table->columns[0]);
    for (int i = 0; i < max_column_count; ++i)
    {
        if (table->columns[i].used)
            column_count++;
    }
    return column_count;
}

FOUNDATION_STATIC column_t* table_column_at(table_t* table, size_t column_at)
{
    const size_t max_column_count = sizeof(table->columns) / sizeof(table->columns[0]);
    for (int i = 0; i < max_column_count; ++i)
    {
        if (table->columns[i].used && column_at-- == 0)
            return &table->columns[i];
    }
    return nullptr;
}

FOUNDATION_STATIC void table_render_columns(table_t* table, int column_count)
{
    int column_index = 0;
    bool dragging_columns = ImGui::IsMouseDragging(ImGuiMouseButton_Left, -5.0f);
    const size_t max_column_count = sizeof(table->columns) / sizeof(table->columns[0]);
    for (int i = 0; i < max_column_count; ++i)
    {
        column_t& column = table->columns[i];
        if (column_index == column_count)
            break;
        else if (!column.used)
            continue;

        ImGuiTableColumnFlags table_column_flags = ImGuiTableColumnFlags_None;
        if (column.flags & COLUMN_HIDE_DEFAULT)
            table_column_flags |= ImGuiTableColumnFlags_DefaultHide;
        if (column.flags & COLUMN_STRETCH && column.width == 0)
        {
            table_column_flags |= ImGuiTableColumnFlags_WidthStretch;
            table_column_flags &= ~ImGuiTableColumnFlags_WidthFixed;
        }
        if ((column.flags & COLUMN_SORTABLE) == 0)
            table_column_flags |= ImGuiTableColumnFlags_NoSort;
        if ((column.flags & COLUMN_DEFAULT_SORT) != 0)
            table_column_flags |= ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending;
        if ((column.flags & COLUMN_HIDE_HEADER_TEXT))
            table_column_flags |= ImGuiTableColumnFlags_NoHeaderLabel;
        if (column.flags & COLUMN_FREEZE)
        {
            table_column_flags |= ImGuiTableColumnFlags_NoHide;
            table->column_freeze = column_index + 1;
        }

        if (column.flags & COLUMN_NOCLIP_CONTENT)
            table_column_flags |= ImGuiTableColumnFlags_NoClip;

        if (column.width > 0)
        {
            table_column_flags &= ~ImGuiTableColumnFlags_WidthStretch;
            table_column_flags |= ImGuiTableColumnFlags_WidthFixed;
        }

        ImGuiTableColumnRenderHandler column_renderer = nullptr;
        if (format_is_numeric(column.format))
            column_renderer = table_cell_right_aligned_column_label;

        if (column.flags & COLUMN_LEFT_ALIGN)
            column_renderer = nullptr;
        else if (column.flags & COLUMN_RIGHT_ALIGN)
            column_renderer = table_cell_right_aligned_column_label;
        else if (column.flags & COLUMN_CENTER_ALIGN)
            column_renderer = table_cell_middle_aligned_column_label;

        string_const_t column_name = string_table_decode_const(column.name);
        ImGui::TableSetupColumn(column_name.str, table_column_flags, column.width, 0U, column_renderer, nullptr);

        column_index++;
    }

    ImGui::TableHeadersRow();
}

FOUNDATION_STATIC bool table_search_row_element(table_t* table, table_element_ptr_t element, string_const_t search_text)
{
    if (table->search && table->search(element, STRING_ARGS(search_text)))
        return true;

    // Filter searchable columns
    int column_count = (int)table_column_count(table);
    for (size_t column_index = 0; column_index < ARRAY_COUNT(table->columns); ++column_index)
    {
        const column_t& c = table->columns[column_index];
        if (column_index == column_count)
            break;
        else if (!c.used)
            continue;
        
        if ((c.flags & COLUMN_SEARCHABLE) == 0)
            continue;

        const cell_t& cell = c.fetch_value(element, &c);
        string_const_t cs = cell_value_to_string(cell, c);
        if (string_contains_nocase(STRING_ARGS(cs), STRING_ARGS(search_text)))
            return true;
    }

    return false;
}

FOUNDATION_STATIC void table_render_filter_rows(table_t* table)
{
    const size_t search_filter_length = table->search_filter.length;
    hash_t new_ordered_hash = search_filter_length == 0 ? 0 : string_hash(STRING_ARGS(table->search_filter));
    if (table->ordered_hash != new_ordered_hash)
    {
        table->rows_visible_count = table->element_count;
        if (search_filter_length > 0)
        {
            for (int i = 0; i < table->rows_visible_count; ++i)
            {
                table_element_ptr_t element = table->rows[i].element;
                if (!table_search_row_element(table, element, table->search_filter))
                {
                    const row_t b = table->rows[table->rows_visible_count - 1];
                    table->rows[table->rows_visible_count - 1] = table->rows[i];
                    table->rows[i--] = b;
                    table->rows_visible_count--;
                }
            }
        }
        table->ordered_hash = new_ordered_hash;
        table->needs_sorting = true;
    }
}

FOUNDATION_STATIC void table_render_sort_rows(table_t* table)
{
    ImGuiTableSortSpecs* table_specs = ImGui::TableGetSortSpecs();
    if (table->sort && table_specs && (table->needs_sorting || table_specs->SpecsDirty) && table_specs->SpecsCount > 0)
    {
        if (!table->needs_sorting && time_elapsed(table->last_sort_time) < 0.5)
            return;

        foreach(r, table->rows)
            r->height = table_default_row_height();

        const ImGuiTableColumnSortSpecs* column_sort_specs = table_specs->Specs;
        column_t* sorted_column = table_column_at(table, column_sort_specs->ColumnIndex);
        if (sorted_column != nullptr)
        {
            log_debugf(0, STRING_CONST("Sorting column %.*s [dir=%d]"), STRING_FORMAT(sorted_column->get_name()), column_sort_specs->SortDirection);
            table_specs->SpecsDirty = !table->sort(table, sorted_column, column_sort_specs->SortDirection);
            table->needs_sorting = false;
            table->last_sort_time = time_current();
        }
    }
}

FOUNDATION_STATIC void table_render_update_ordered_elements(table_t* table, table_element_ptr_const_t elements, const int element_count, size_t element_size)
{
    if (table->elements != elements || table->element_size != element_size || table->element_count != element_count)
    {
        row_t* rows = table->rows;
        array_resize(rows, element_count);
        if (rows && element_count > table->element_count)
            memset(rows + table->element_count, 0, (element_count - table->element_count) * sizeof(row_t));

        table_element_ptr_t element = (table_element_ptr_t)elements;
        table_element_ptr_const_t end = ((uint8_t*)elements) + (element_count * element_size);
        for (int i = 0; i < element_count; ++i, (element = ((uint8_t*)element) + element_size))
        {
            rows[i].element = element;
            rows[i].height = max(0.0f, rows[i].height);
            rows[i].fetched = false;
            rows[i].background_color = 0;
        }

        table->elements = elements;
        table->element_size = element_size;
        table->element_count = element_count;

        table->rows = rows;
        table->rows_visible_count = array_size(rows);
        table->needs_sorting = true;
    }
}

FOUNDATION_STATIC void table_render_summary_row(table_t* table, int column_count)
{
    if ((table->flags & TABLE_SUMMARY) == 0 || table->rows_visible_count <= 1)
        return;
    cell_t summary_cells[ARRAY_COUNT(table->columns)];
    memset(summary_cells, 0, sizeof(summary_cells));

    for (int element_index = 0; element_index < table->rows_visible_count; ++element_index)
    {
        row_t& row = table->rows[element_index];
        table_element_ptr_t element = row.element;

        for (int i = 1, column_index = 0; i < ARRAY_COUNT(table->columns); ++i)
        {
            column_t& column = table->columns[i];
            if (column_index == column_count)
                break;
            else if (!column.used)
                continue;

            column_index++;
            if (!column.fetch_value)
                continue;

            const ImGuiTableColumnFlags table_column_flags = ImGui::TableGetColumnFlags(i);
            if ((table_column_flags & ImGuiTableColumnFlags_IsEnabled) == 0)
                continue;
            if ((table_column_flags & ImGuiTableColumnFlags_IsVisible) == 0)
                continue;

            if ((column.flags & COLUMN_DYNAMIC_VALUE) && !row.fetched && table->update)
                row.fetched = table->update(element);

            column.flags |= COLUMN_COMPUTE_SUMMARY;
            const cell_t& cell = column.fetch_value(element, &column);
            column.flags &= ~COLUMN_COMPUTE_SUMMARY;

            // Build summary cells
            cell_t& sc = summary_cells[i];
            sc.format = column.format;
            switch (sc.format)
            {
                case COLUMN_FORMAT_CURRENCY:
                case COLUMN_FORMAT_NUMBER:
                case COLUMN_FORMAT_PERCENTAGE:
                    if (!math_real_is_nan(cell.number))
                    {
                        sc.number += cell.number;
                        sc.length++;
                    }
                    break;
                case COLUMN_FORMAT_DATE:
                    sc.time += cell.time;
                    break;
                    
                default:
                    break;
            }
        }
    }

    //ImGui::TableNextRow();
    //ImGui::TableNextRow();
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted("Summary");
    for (size_t i = 1; i < ARRAY_COUNT(summary_cells); i++)
    {	
        const column_t& column = table->columns[i];
        if (i == column_count)
            break;
        else if (!column.used)
            continue;

        if (!ImGui::TableSetColumnIndex((int)i))
            continue;

        cell_t& sc = summary_cells[i];
        if (format_is_numeric(column.format))
        {
            if ((column.flags & COLUMN_SUMMARY_AVERAGE) || column.format == COLUMN_FORMAT_PERCENTAGE)
            {
                sc.number /= (double)sc.length;
            }
        }
        else if (column.format == COLUMN_FORMAT_DATE)
        {
            sc.time /= table->rows_visible_count;
        }

        string_const_t str_value = cell_value_to_string(sc, column);
        if (str_value.length)
        {
            column_flags_t alignment_flags = column.flags & COLUMN_ALIGNMENT_MASK;
            if (alignment_flags == 0 && format_is_numeric(column.format))
                alignment_flags |= COLUMN_RIGHT_ALIGN;
            if (alignment_flags & COLUMN_RIGHT_ALIGN)
                table_cell_right_aligned_label(STRING_ARGS(str_value));
            else if (alignment_flags & COLUMN_CENTER_ALIGN)
                table_cell_middle_aligned_label(STRING_ARGS(str_value));
            else
                cell_label(str_value);
        }
        else
            ImGui::Dummy(ImVec2());
    }

    ImGui::PushStyleColor(ImGuiCol_TableRowBg, (ImVec4)ImColor::HSV(275 / 360.0f, 0.04f, 0.37f));
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, (ImVec4)ImColor::HSV(275 / 360.0f, 0.04f, 0.37f));
    ImGui::TableNextRow();
    ImGui::PopStyleColor(2);
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL bool table_column_is_number_value_trimmed(const column_t& column, const cell_t& cell)
{
    if (!math_real_is_finite(cell.number))
        return false;

    if ((column.flags & COLUMN_NUMBER_ABBREVIATION) && column.format == COLUMN_FORMAT_NUMBER && cell.number > 999)
        return true;
        
    if ((column.flags & COLUMN_ROUND_NUMBER) && column.format == COLUMN_FORMAT_PERCENTAGE)
        return true;

   return false;
}

FOUNDATION_STATIC void table_render_row_element(table_t* table, int element_index, int column_count)
{
    const auto font_height = table_default_row_height();
    
    row_t& row = table->rows[element_index];
    table_element_ptr_t element = row.element;

    row.hovered = false;
    row.background_color = 0;

    ImGui::TableNextRow(0, table->row_fixed_height);

    const auto sx = ImGui::TableGetRowRect();
    row.rect = ImRect(sx.x, sx.y, sx.z, sx.y + row.height);
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_None) && row.rect.Contains(ImGui::GetMousePos()))
        row.hovered = true;

    if (table->row_begin)
        table->row_begin(table, &row, element);

    const float row_cursor_y = ImGui::GetCursorPosY();

    #if ENABLE_ROW_HEIGHT_MIDDLE
        float middle_row_cursor_position = row_cursor_y;
        if (row.height > 0 && font_height < row.height)
            middle_row_cursor_position += (row.height - font_height) / 2.0f;
    #endif

    float max_cell_height = 0;
    ImGuiTable* ct = ImGui::GetCurrentTable();
    const size_t max_column_count = sizeof(table->columns) / sizeof(table->columns[0]);
    for (int i = 0, column_index = 0; i < max_column_count; ++i)
    {
        column_t& column = table->columns[i];
        if (column_index == column_count)
            break;
        else if (!column.used)
            continue;

        column_index++;
        if (!ImGui::TableNextColumn())
            continue;

        //has_wrapping_text |= (column.flags & COLUMN_TEXT_WRAPPING) != 0;

        if ((column.flags & COLUMN_DYNAMIC_VALUE) && !row.fetched && table->update)
            row.fetched = table->update(element);

        char cell_id_buf[64];
        string_t cell_id = string_format(STRING_CONST_CAPACITY(cell_id_buf), STRING_CONST("cell_%d_%d"), element_index, column_index);
        ImGui::PushID(cell_id.str, cell_id.str + cell_id.length);

        cell_t cell = column.fetch_value ? column.fetch_value(element, &column) : cell_t{};
        string_const_t str_value = cell_value_to_string(cell, column);

        column_flags_t alignment_flags = column.flags & COLUMN_ALIGNMENT_MASK;
        if (alignment_flags == 0 && format_is_numeric(column.format))
            alignment_flags |= COLUMN_RIGHT_ALIGN;

        const ImRect cell_rect = ImGui::TableGetCellBgRect(ct, i);
        cell.style.rect = {
            cell_rect.Min.x,
            cell_rect.Min.y,
            cell_rect.GetWidth(),
            max(row.height, cell_rect.GetHeight())
        };

        if (column.style_formatter)
            column.style_formatter(element, &column, &cell, cell.style);

        const ImVec2 cell_min = ImVec2(cell.style.rect.x, cell.style.rect.y);
        const ImVec2 cell_max = ImVec2(
            cell.style.rect.x + cell.style.rect.width,
            cell.style.rect.y + cell.style.rect.height);
        _table_last_cell_rect.Min = cell_min;
        _table_last_cell_rect.Max = cell_max;
        //if (i >= table->column_freeze) // Because of clipping reasons it seems we can't set the cell background color here
        {
            if ((table->flags & TABLE_HIGHLIGHT_HOVERED_ROW) && row.hovered)
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(cell_min, cell_max, BACKGROUND_HIGHLIGHT_COLOR, 0, 0);
            }
            else if (row.background_color != 0)
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(cell_min, cell_max, row.background_color, 0, 0);
            }
        }

        if (cell.style.types & COLUMN_COLOR_BACKGROUND)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(cell_min, cell_max, cell.style.background_color, 0, 0);
        }

        if (cell.style.types & COLUMN_COLOR_TEXT)
            ImGui::PushStyleColor(ImGuiCol_Text, cell.style.text_color);

        if (column.flags & COLUMN_CUSTOM_DRAWING && column.fetch_value)
        {
            column.flags |= COLUMN_RENDER_ELEMENT;
            cell = column.fetch_value(element, &column);
            column.flags &= ~COLUMN_RENDER_ELEMENT;
        }
        else if (string_is_null(str_value))
        {
            ImGui::Dummy(ImVec2(0, 0));
        }
        else
        {
            #if ENABLE_ROW_HEIGHT_MIDDLE
            if ((column.flags & COLUMN_VALIGN_TOP) == 0)
                ImGui::SetCursorPosY(middle_row_cursor_position);
            #endif

            if (alignment_flags & COLUMN_RIGHT_ALIGN)
                table_cell_right_aligned_label(STRING_ARGS(str_value));
            else if (alignment_flags & COLUMN_CENTER_ALIGN)
                table_cell_middle_aligned_label(STRING_ARGS(str_value));
            else
                cell_label(str_value);
        }

        if (cell.style.types & COLUMN_COLOR_TEXT)
            ImGui::PopStyleColor();

        // Handle tooltip
        if (ImGui::IsItemHovered())
        {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                if (column.selected)
                    column.selected(element, &column, &cell);
                else if (table->selected)
                    table->selected(element, &column, &cell);
            }
            else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle))
            {
                // Copy value to clipboard
                ImGui::SetClipboardText(str_value.str);
            }

            hash_t cell_hash = string_hash(STRING_ARGS(cell_id));
            if (column.hovered_cell != cell_hash)
            {
                column.hovered_cell = cell_hash;
                column.hovered_time = time_current();
            }
            else if (column.tooltip && time_elapsed(column.hovered_time) > 1.0)
            {
                ImGui::BeginTooltip();
                column.tooltip(element, &column, &cell);
                ImGui::EndTooltip();
            }
            else if (table_column_is_number_value_trimmed(column, cell))
            {
                ImGui::SetTooltip("%lg", cell.number);
            }
        }

        // Handle contextual menu
        if (column.context_menu && ImGui::BeginPopupContextItem(cell_id.str))
        {
            column.context_menu(element, &column, &cell);
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::EndPopup();
        }
        else if (table->context_menu && ImGui::BeginPopupContextItem(cell_id.str))
        {
            table->context_menu(element, &column, &cell);
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::EndPopup();
        }

        ImGui::PopID();

        if (*(uintptr_t**)element == nullptr)
            break;

        #if ENABLE_ROW_HEIGHT_MIDDLE
        const float row_cursor_height = (ImGui::GetCursorPosY() - row_cursor_y) - 4.0f;
        max_cell_height = max(max(row_cursor_height, cell_max.y - cell_min.y), max_cell_height);
        #endif
    }

    #if ENABLE_ROW_HEIGHT_MIDDLE
    row.height = max_cell_height;
    #endif

    if (table->row_end)
        table->row_end(table, &row, element);
}

FOUNDATION_STATIC void table_render_elements(table_t* table, int column_count)
{
    //TIME_TRACKER(0.008, "Render table %.*s", STRING_FORMAT(table->name));

    ImGuiListClipper clipper;
    clipper.Begin(table->rows_visible_count, table->row_fixed_height);
    while (clipper.Step())
    {
        if (clipper.DisplayStart >= clipper.DisplayEnd)
            continue;

        for (int element_index = clipper.DisplayStart; 
             element_index < min(clipper.DisplayEnd, table->rows_visible_count); 
             ++element_index)
        {
            table_render_row_element(table, element_index, column_count);
        }
    }

    // Draw table summary row
    table_render_summary_row(table, column_count);

    // Handle default context menu on empty space
    if (table->context_menu)
    {
        ImGui::TableNextRow(0, table->row_fixed_height);
        int hovered_column = -1;
        for (int column = 0; column < column_count + 1; column++)
        {
            ImGui::TableNextColumn();
            string_const_t id = string_format_static(STRING_CONST("EmptyCell_%d"), column);
            ImGui::PushID(id.str);
            ImVec2 empty_space_size = ImGui::GetContentRegionAvail();
            empty_space_size.y -= 12.0f;
            ImGui::Dummy(empty_space_size);
            if (ImGui::TableGetColumnFlags(column) & ImGuiTableColumnFlags_IsHovered)
                hovered_column = column;
            if (hovered_column == column_count && !ImGui::IsAnyItemHovered() && ImGui::IsMouseReleased(1))
                ImGui::OpenPopup(id.str);
            if (ImGui::BeginPopupContextItem(id.str))
            {
                ImGui::BeginGroup();
                table->context_menu(nullptr, nullptr, nullptr);
                ImGui::EndGroup();
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    
    ImGui::EndTable();
}

void table_render(table_t* table, table_element_ptr_const_t elements, const int element_count, size_t element_size, float outer_size_x, float outer_size_y)
{	
    int column_count = (int)table_column_count(table);
    if (column_count == 0)
    {
        ImGui::Text("No columns to render for %.*s table", STRING_FORMAT(table->name));
        return;
    }

    const ImVec2 outer_size = ImVec2(outer_size_x, outer_size_y);

    const ImGuiTableFlags flags = (int)table->flags |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Sortable |
        ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_Hideable;

    if (!ImGui::BeginTable(table->name.str, column_count, flags, outer_size))
        return;

    // Make top row always visible
    ImGui::TableSetupScrollFreeze(table->column_freeze, 1);

    table_render_update_ordered_elements(table, elements, element_count, element_size);
    table_render_columns(table, column_count);

    table_render_filter_rows(table);
    table_render_sort_rows(table);

    table_render_elements(table, column_count);
}

void table_clear_columns(table_t* table)
{
    const size_t column_count = sizeof(table->columns) / sizeof(table->columns[0]);
    for (int i = 0; i < column_count; ++i)
    {
        table->columns[i].used = false;
    }
}

column_t& table_add_column(table_t* table,
    const char* name, size_t name_length,
    const cell_fetch_value_handler_t& fetch_value_handler,
    column_format_t format /*= COLUMN_FORMAT_TEXT*/,
    column_flags_t flags /*= COLUMN_OPTIONS_NONE*/)
{
    column_t* c = nullptr;
    const size_t column_count = sizeof(table->columns) / sizeof(table->columns[0]);
    for (int i = 0; i < column_count; ++i)
    {
        if (!table->columns[i].used)
        {
            c = &table->columns[i];
            c->used = true;
            c->name = string_table_encode(name, name_length);
            c->format = format;
            c->flags = flags;
            c->fetch_value = fetch_value_handler;

            return *c;
        }
    }

    return table->columns[column_count - 1];
}

const ImRect& table_current_cell_rect()
{
    return _table_last_cell_rect;
}

float table_default_row_height()
{
    static const auto font_height = imgui_get_font_ui_scale(30.0f);
    return font_height;
}

void table_set_search_filter(table_t* table, const char* filter, size_t filter_length)
{
    table->search_filter = { filter, filter_length };
}
