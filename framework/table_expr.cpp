/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 *
 * TABLE expression module
 *
 * Examples: TABLE(test, R(_300K, name), ['name', $2], ['col 1', S($1, open)], ['col 2', S($1, close)])
 *           TABLE(test, R(favorites, name), ['title', $1], ['name', $2], ['open', S($1, open)], ['close', S($1, close)])
 *           TABLE('Test', [U.US, GFL.TO], ['Title', $1], ['Price', S($1, close), currency])
 *
 *           TABLE('Unity Best Days', FILTER(S(U.US, close, ALL), $2 > 60), ['Date', DATESTR($1)], ['Price', $2, currency])
 *           T=U.US, TABLE('Unity Best Days', FILTER(S(T, close, ALL), $2 > 60), 
 *              ['Date', DATESTR($1)],
 *              ['Price', $2, currency],
 *              ['%', S(T, change_p, $1), percentage])
 *
 *           # For each titles in a report, compare shorts and the % change since 180 days
 *           $SINCE=180
 *           $REPORT='300K'
 *           TABLE('Shares ' + $REPORT, R($REPORT, [name, price, S($TITLE, close, NOW() - (60 * 60 * 24 * $SINCE))]),
 *              ['Name', $2],
 *              ['Shorts', F($1, "Technicals.SharesShort")/F($1, "SharesStats.SharesFloat")*100, percentage],
 *              ['Since %', ($3 - $4) / $4 * 100, percentage])
 *
 *           TABLE('Retained Earnings', R('300K', [name, F($TITLE, "Financials.Balance_Sheet.quarterly.0.retainedEarnings")]), ['Name', $2], ['Value', $3, currency])
 */

#include <framework/app.h>
#include <framework/expr.h>
#include <framework/table.h>
#include <framework/array.h>
#include <framework/system.h>

#define HASH_TABLE_EXPRESSION static_hash_string("table_expr", 10, 0x20a95260d96304aULL)

struct table_expr_type_drawer_t
{
    string_t type{};
    function<void(const cell_t& cell)> handler{};
};

static table_expr_type_drawer_t* _table_expr_type_drawers{ nullptr };

typedef enum TableExprValueType {
    DYNAMIC_TABLE_VALUE_NULL = 0,
    DYNAMIC_TABLE_VALUE_TRUE = 1,
    DYNAMIC_TABLE_VALUE_FALSE = 2,
    DYNAMIC_TABLE_VALUE_NUMBER = 3,
    DYNAMIC_TABLE_VALUE_TEXT = 4
} table_expr_record_value_type_t;

struct table_expr_column_t
{
    string_t name;
    string_t expression;
    expr_t* ee;
    int value_index;
    column_format_t format{ COLUMN_FORMAT_TEXT };
    table_expr_type_drawer_t* drawer{ nullptr };
};

struct table_expr_record_value_t
{
    table_expr_record_value_type_t type;
    union {
        string_t text;
        double number;
    };
};

struct table_expr_record_t
{
    expr_result_t* values{ nullptr };
    table_expr_record_value_t* resolved{ nullptr };
};

struct table_expr_t
{
    string_t             name;
    table_expr_column_t* columns;
    table_expr_record_t* records;
    table_t*             table;
    char                 search_filter[64];
};

FOUNDATION_STATIC void table_expr_add_record_values(table_expr_record_t& record, const expr_result_t& e)
{
    if (e.is_set())
    {
        for (auto ee : e)
            table_expr_add_record_values(record, ee);
    }
    else
    {
        array_push(record.values, e);
    }
}

FOUNDATION_STATIC void table_expr_cleanup_columns(table_expr_column_t* columns)
{
    foreach(c, columns)
    {
        string_deallocate(c->name.str);
        string_deallocate(c->expression.str);
    }
    array_deallocate(columns);
}

FOUNDATION_STATIC void table_expr_deallocate(table_expr_t* report)
{
    table_deallocate(report->table);
    table_expr_cleanup_columns(report->columns);
    foreach(r, report->records)
    {
        foreach(vr, r->resolved)
        {
            if (vr->type == DYNAMIC_TABLE_VALUE_TEXT)
                string_deallocate(vr->text.str);
        }
        array_deallocate(r->resolved);
        array_deallocate(r->values);
    }
    array_deallocate(report->records);
    string_deallocate(report->name.str);
    memory_deallocate(report);
}

FOUNDATION_STATIC cell_t table_expr_cell_value(const table_expr_record_value_t* v, column_format_t format)
{
    if (v->type == DYNAMIC_TABLE_VALUE_NULL)
        return cell_t(nullptr);
                
    if (v->type == DYNAMIC_TABLE_VALUE_TRUE)
        return cell_t(true);

    if (v->type == DYNAMIC_TABLE_VALUE_FALSE)
        return cell_t(false);

    if (v->type == DYNAMIC_TABLE_VALUE_TEXT)
        return cell_t(string_to_const(v->text));

    if (v->type == DYNAMIC_TABLE_VALUE_NUMBER)
    {
        if (format == COLUMN_FORMAT_DATE)
            return cell_t((time_t)v->number);
        return cell_t(v->number);
    }

    return cell_t();
}

FOUNDATION_STATIC bool table_expr_render_dialog(table_expr_t* report)
{
    if (report->table == nullptr)
    {
        report->table = table_allocate(report->name.str, TABLE_SUMMARY | TABLE_HIGHLIGHT_HOVERED_ROW);
        foreach(c, report->columns)
        {
            column_flags_t column_flags = COLUMN_SORTABLE;
            if (c->format == COLUMN_FORMAT_TEXT)
                column_flags |= COLUMN_SEARCHABLE;
            if (c->drawer)
                column_flags |= COLUMN_CUSTOM_DRAWING;
            table_add_column(report->table, STRING_ARGS(c->name), [c](table_element_ptr_t element, const column_t* column) 
            {
                table_expr_record_t* record = (table_expr_record_t*)element;
                const table_expr_record_value_t* v = &record->resolved[c->value_index];

                const cell_t cell = table_expr_cell_value(v, column->format);

                if ((column->flags & COLUMN_RENDER_ELEMENT) && c->drawer)
                    c->drawer->handler.invoke(cell);

                return cell;
            }, c->format, column_flags);
        }
    }

    const float export_button_width = IM_SCALEF(20);

    ImGui::ExpandNextItem(export_button_width);
    if (ImGui::InputTextWithHint("##Search", tr("Search table..."), 
        STRING_BUFFER(report->search_filter), ImGuiInputTextFlags_None))
    {
        table_set_search_filter(report->table, STRING_LENGTH(report->search_filter));
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_MD_BACKUP_TABLE, {export_button_width, 0}))
    {
        system_save_file_dialog(
            tr("Export table to CSV..."), 
            tr("Comma-Separated-Value (*.csv)|*.csv"), 
            nullptr, [table=report->table](string_const_t save_path)
        {
            FOUNDATION_ASSERT(table);
            return table_export_csv(table, STRING_ARGS(save_path));
        });
    }
    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    {
        ImGui::SetTooltip("%s", tr("Export table"));
    }

    table_render(report->table, report->records, array_size(report->records), sizeof(table_expr_record_t), 0.0f, 0.0f);
    return true;
}

FOUNDATION_STATIC expr_result_t table_expr_eval(const expr_func_t* f, vec_expr_t* args, void* c)
{
    if (args->len < 3)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Requires at least two arguments");

    // Get the data set
    expr_result_t elements = expr_eval(args->get(1));
    if (!elements.is_set())
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Second argument must be a dataset");

    // Then for each remaining arguments, evaluate them as columns.
    table_expr_column_t* columns = nullptr;
    for (int i = 2, col_index = 0; i < args->len; ++i, ++col_index)
    {
        table_expr_column_t col;
        col.ee = args->get(i);
        col.format = COLUMN_FORMAT_TEXT;
        col.drawer = nullptr;
        if (col.ee->type == OP_SET && col.ee->args.len >= 2)
        {
            // Get the column name
            string_const_t col_name = expr_eval((expr_t*)col.ee->args.get(0)).as_string(string_format_static_const("col %d", i - 2));

            if (col.ee->args.len >= 3)
            {
                string_const_t format_string = expr_eval((expr_t*)col.ee->args.get(2)).as_string();
                if (!string_is_null(format_string))
                {
                    if (string_equal_nocase(STRING_ARGS(format_string), STRING_CONST("currency")))
                        col.format = COLUMN_FORMAT_CURRENCY;
                    else if (string_equal_nocase(STRING_ARGS(format_string), STRING_CONST("percentage")))
                        col.format = COLUMN_FORMAT_PERCENTAGE;
                    else if (string_equal_nocase(STRING_ARGS(format_string), STRING_CONST("date")))
                        col.format = COLUMN_FORMAT_DATE;
                    else if (string_equal_nocase(STRING_ARGS(format_string), STRING_CONST("number")))
                        col.format = COLUMN_FORMAT_NUMBER;
                    else if (string_equal_nocase(format_string.str, min(format_string.length, SIZE_C(4)), STRING_CONST("bool")))
                        col.format = COLUMN_FORMAT_BOOLEAN;
                    else
                    {
                        // Check if we have a registered drawer for the format string
                        for (unsigned int j = 0; j < array_size(_table_expr_type_drawers); ++j)
                        {
                            auto* drawer = _table_expr_type_drawers + j;
                            if (string_equal_nocase(STRING_ARGS(format_string), STRING_ARGS(drawer->type)))
                            {
                                col.drawer = drawer;
                                break;
                            }
                        }
                    }
                }
            }
            
            col.ee = col.ee->args.get(1);
            string_const_t col_expression = col.ee->token;

            col.name = string_utf8_unescape(STRING_ARGS(col_name));
            col.expression = string_clone(STRING_ARGS(col_expression));
            
            col.value_index = col_index;

            array_push_memcpy(columns, &col);
        }
        else
        {
            table_expr_cleanup_columns(columns);
            throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Column argument must be a set of at least two elements, i.e. [name, evaluator[, ...options]");
        }
    }

    table_expr_record_t* records = nullptr;
    for (auto e : elements)
    {
        if (e.type == EXPR_RESULT_NULL)
            continue;

        table_expr_record_t record{};
        table_expr_add_record_values(record, e);

        for(unsigned ic = 0, endc = array_size(columns); ic < endc; ++ic)
        {
            table_expr_column_t* c = &columns[ic];
            
            foreach(v, record.values)
            {
                string_const_t arg_macro = string_format_static(STRING_CONST("$%u"), i + 1);
                expr_set_or_create_global_var(STRING_ARGS(arg_macro), *v);
            }

            expr_result_t cv = expr_eval(c->ee);
            if (cv.type == EXPR_RESULT_TRUE)
            {
                array_push(record.resolved, table_expr_record_value_t{ DYNAMIC_TABLE_VALUE_TRUE });
            }
            else if (cv.type == EXPR_RESULT_FALSE)
            {
                array_push(record.resolved, table_expr_record_value_t{ DYNAMIC_TABLE_VALUE_FALSE });
            }
            else if (cv.type == EXPR_RESULT_NUMBER)
            {
                table_expr_record_value_t v{ DYNAMIC_TABLE_VALUE_NUMBER };
                v.number = cv.as_number();
                array_push(record.resolved, v);
            }
            else if (cv.type == EXPR_RESULT_SYMBOL)
            {
                table_expr_record_value_t v{ DYNAMIC_TABLE_VALUE_TEXT };
                string_const_t e_text = cv.as_string();
                v.text = string_clone(STRING_ARGS(e_text));

                if (c->format == COLUMN_FORMAT_CURRENCY || c->format == COLUMN_FORMAT_PERCENTAGE || c->format == COLUMN_FORMAT_NUMBER)
                {
                    string_t text = v.text;
                    if (string_try_convert_number(text.str, text.length, v.number))
                    {
                        v.type = DYNAMIC_TABLE_VALUE_NUMBER;
                        string_deallocate(text);
                    }
                }

                array_push(record.resolved, v);
            }
            else if (cv.is_set())
            {
                table_expr_record_value_t v{ DYNAMIC_TABLE_VALUE_NUMBER };
                v.number = cv.as_number(DNAN, 0);
                array_push(record.resolved, v);
            }
            else
            {
                array_push(record.resolved, table_expr_record_value_t{ DYNAMIC_TABLE_VALUE_NULL });
            }
        }

        FOUNDATION_ASSERT(array_size(columns) == array_size(record.resolved));
        array_push_memcpy(records, &record);
    }

    // Get the table name from the first argument.
    string_const_t table_name = expr_eval(args->get(0)).as_string("none");

    // Create the dynamic report
    table_expr_t* report = (table_expr_t*)memory_allocate(HASH_TABLE_EXPRESSION, sizeof(table_expr_t), 0, MEMORY_PERSISTENT);
    report->name = string_clone(STRING_ARGS(table_name));
    report->columns = columns;
    report->records = records;
    report->table = nullptr;
    report->search_filter[0] = 0;

    app_open_dialog(table_name.str, 
        L1(table_expr_render_dialog((table_expr_t*)_1)), 800, 600, true,
        report, L1(table_expr_deallocate((table_expr_t*)_1)));
    return elements;
}

//
// # PUBLIC
//

void table_expr_add_type_drawer(const char* type, size_t length, const function<void(const cell_t& value)>& handler)
{
    table_expr_type_drawer_t drawer{};
    drawer.type = string_clone(type, length);
    drawer.handler = handler;
    array_push_memcpy(_table_expr_type_drawers, &drawer);
}

void table_expr_initialize()
{
    expr_register_function("TABLE", table_expr_eval);
}

void table_expr_shutdown()
{
    for (unsigned i = 0, end = array_size(_table_expr_type_drawers); i < end; ++i)
    {
        table_expr_type_drawer_t* drawer = _table_expr_type_drawers + i;
        string_deallocate(drawer->type);
    }
    array_deallocate(_table_expr_type_drawers);
}
