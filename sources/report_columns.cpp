/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 *
 * Manage report expression columns
 */

#include "report.h"

#include "title.h"

#include <framework/app.h>
#include <framework/expr.h>
#include <framework/table.h>
#include <framework/database.h>
#include <framework/array.h>
#include <framework/console.h>

struct report_expression_column_t
{
    char name[64];
    char expression[256];
    column_format_t format{ COLUMN_FORMAT_TEXT };
};

struct report_expression_cache_value_t
{
    hash_t key{0};
    tick_t time{0};
    column_format_t format;

    union {
        time_t date;
        double number;
        string_table_symbol_t symbol;
    };
};

FOUNDATION_FORCEINLINE hash_t hash(const report_expression_cache_value_t& value)
{
    return value.key;
}

static tick_t _report_expression_last_eval_ts = 0;
static database<report_expression_cache_value_t>* _report_expression_cache;

FOUNDATION_STATIC table_cell_t report_column_cache_value_to_cell(const report_expression_cache_value_t& cvalue, const report_expression_column_t* ec)
{
    if (cvalue.key == 0)
        return nullptr;

    if (cvalue.format == ec->format)
    {
        if (ec->format == COLUMN_FORMAT_DATE)
            return cvalue.date;
        if (ec->format == COLUMN_FORMAT_BOOLEAN)
            return math_real_is_zero(cvalue.number) ? false : true;
        if (ec->format == COLUMN_FORMAT_CURRENCY ||
            ec->format == COLUMN_FORMAT_NUMBER ||
            ec->format == COLUMN_FORMAT_PERCENTAGE)
            return cvalue.number;
        return SYMBOL_CONST(cvalue.symbol);
    }

    return nullptr;
}

FOUNDATION_STATIC table_cell_t report_column_evaluate_expression(table_element_ptr_t element, const table_column_t* column, 
                                                           report_handle_t report_handle, const report_expression_column_t* ec)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr || title_is_index(title))
        return nullptr;
        
    report_t* report = report_get(report_handle);
    string_const_t report_name = SYMBOL_CONST(report->name);
    string_const_t title_code = string_const(title->code, title->code_length);
    string_const_t expression_string = string_const(ec->expression, string_length(ec->expression));
    hash_t key = hash_combine(
        string_hash(STRING_ARGS(report_name)), 
        string_hash(STRING_ARGS(title_code)), 
        string_hash(STRING_ARGS(expression_string)));

    report_expression_cache_value_t cvalue{};
    if (_report_expression_cache->select(key, cvalue) && time_elapsed(cvalue.time) < 5 * 60.0)
    {
        if (cvalue.format == ec->format)
        {
            return report_column_cache_value_to_cell(cvalue, ec);
        }
        else
        {
            log_debugf(HASH_REPORT, STRING_CONST("Cached expression '%.*s' for title '%.*s' in report '%.*s' has different format"),
                STRING_FORMAT(expression_string), STRING_FORMAT(title_code), STRING_FORMAT(report_name));
        }
    }

    // Check if we are ready to evaluate another expression right away?
    // We are doing this here so we do not block the UI thread for too long
    if (time_elapsed(_report_expression_last_eval_ts) < 0.050)
         return report_column_cache_value_to_cell(cvalue, ec);

    if (string_find_string(STRING_ARGS(expression_string), STRING_CONST("$TITLE"), 0) != STRING_NPOS)
    {
        if (!title_is_resolved(title))
            return report_column_cache_value_to_cell(cvalue, ec);
    }

    if (string_find_string(STRING_ARGS(expression_string), STRING_CONST("$REPORT"), 0) != STRING_NPOS)
    {
        if (report_is_loading(report))
            return report_column_cache_value_to_cell(cvalue, ec);
    }

    log_debugf(HASH_REPORT, STRING_CONST("Evaluating expression '%.*s' for title '%.*s' in report '%.*s'"),
           STRING_FORMAT(expression_string), STRING_FORMAT(title_code), STRING_FORMAT(report_name));

    cvalue.key = key;
    cvalue.format = ec->format;
    _report_expression_last_eval_ts = cvalue.time = time_current();

    string_const_t column_name = string_const(ec->name, string_length(ec->name));
    
    expr_set_or_create_global_var(STRING_CONST("$TITLE"), expr_result_t(title_code));
    expr_set_or_create_global_var(STRING_CONST("$REPORT"), expr_result_t(report_name));
    expr_set_or_create_global_var(STRING_CONST("$COLUMN"), expr_result_t(column_name));
    expr_set_or_create_global_var(STRING_CONST("$FORMAT"), expr_result_t((double)ec->format));
    auto result = eval(STRING_ARGS(expression_string));

    if (ec->format == COLUMN_FORMAT_CURRENCY || ec->format == COLUMN_FORMAT_NUMBER || ec->format == COLUMN_FORMAT_PERCENTAGE)
    { 
        cvalue.number = result.as_number();
        _report_expression_cache->put(cvalue);
        return cvalue.number;
    }
    if (ec->format == COLUMN_FORMAT_BOOLEAN)
    {
        cvalue.number = result.as_boolean() ? 1.0 : 0.0;
        _report_expression_cache->put(cvalue);
        return cvalue.number;
    }
    if (ec->format == COLUMN_FORMAT_DATE)
    {
        cvalue.date = (time_t)result.as_number();
        _report_expression_cache->put(cvalue);
        return cvalue.date;
    }
    
    string_const_t str_value = result.as_string();
    cvalue.symbol = string_table_encode(str_value);
    _report_expression_cache->put(cvalue);
    return str_value;
}

FOUNDATION_STATIC const char* report_expression_column_format_name(column_format_t format)
{
    switch (format)
    {
    case COLUMN_FORMAT_CURRENCY:
        return "Currency";
    case COLUMN_FORMAT_DATE:
        return "Date";
    case COLUMN_FORMAT_PERCENTAGE:
        return "Percent";
    case COLUMN_FORMAT_NUMBER:
        return "Number";
    case COLUMN_FORMAT_BOOLEAN:
        return "Boolean";
    default:
        return "String";
    }
}

FOUNDATION_STATIC bool report_render_expression_columns_dialog(void* user_data)
{
    report_t* report = (report_t*)user_data;
    FOUNDATION_ASSERT(report);

    if (!ImGui::BeginTable("Columns", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY))
        return false;

    ImGui::TableSetupColumn(tr("Name"), ImGuiTableColumnFlags_None);
    ImGui::TableSetupColumn(tr("Expression||Macros:\n"
        "$TITLE: Represents the active title symbol code, i.e. \"ZM.US\"\n"
        "$REPORT: Represents the active report name, i.e. \"MyReport\"\n\n"
        "Double click the input field to edit and test in the console window"), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(tr("Format"), ImGuiTableColumnFlags_None);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, imgui_get_font_ui_scale(40.0f));

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();

    bool update_table = false;

    foreach(c, report->expression_columns)
    {
        ImGui::TableNextRow();

        ImGui::PushID(c);
        
        // Name field
        if (ImGui::TableNextColumn())
        {
            ImGui::ExpandNextItem();
            if (ImGui::InputText("##Name", c->name, sizeof(c->name), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                update_table = true;
            }
        }
        
        // Expression field
        if (ImGui::TableNextColumn())
        {
            ImGui::ExpandNextItem();
            ImGui::InputText("##Expression", c->expression, sizeof(c->expression));
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::TrMenuItem("Edit in Console"))
                    console_set_expression(c->expression, string_length(c->expression));
                ImGui::EndPopup();
            }
        }

        // Format selector
        if (ImGui::TableNextColumn())
        {
            ImGui::ExpandNextItem();
            if (ImGui::BeginCombo("##Format", report_expression_column_format_name(c->format)))
            {
                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_TEXT), c->format == COLUMN_FORMAT_TEXT, ImGuiSelectableFlags_None))
                {
                    c->format = COLUMN_FORMAT_TEXT;
                    update_table = true;
                }

                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_NUMBER), c->format == COLUMN_FORMAT_NUMBER, ImGuiSelectableFlags_None))
                {
                    c->format = COLUMN_FORMAT_NUMBER;
                    update_table = true;
                }

                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_CURRENCY), c->format == COLUMN_FORMAT_CURRENCY, ImGuiSelectableFlags_None))
                {
                    c->format = COLUMN_FORMAT_CURRENCY;
                    update_table = true;
                }

                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_PERCENTAGE), c->format == COLUMN_FORMAT_PERCENTAGE, ImGuiSelectableFlags_None))
                {
                    c->format = COLUMN_FORMAT_PERCENTAGE;
                    update_table = true;
                }

                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_DATE), c->format == COLUMN_FORMAT_DATE, ImGuiSelectableFlags_None))
                {
                    c->format = COLUMN_FORMAT_DATE;
                    update_table = true;
                }

                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_BOOLEAN), c->format == COLUMN_FORMAT_BOOLEAN, ImGuiSelectableFlags_None))
                {
                    c->format = COLUMN_FORMAT_BOOLEAN;
                    update_table = true;
                }
                ImGui::EndCombo();
            }
        }

        // Delete expression action
        if (ImGui::TableNextColumn() && ImGui::Button(ICON_MD_DELETE_FOREVER, { ImGui::GetContentRegionAvail().x, 0 }))
        {
            array_erase_ordered_safe(report->expression_columns, i);
            update_table = true;
            ImGui::PopID();
            break;
        }

        ImGui::PopID();
    }

    {
        ImGui::PushID("NewColumn");

        bool add = false;
        static char name[64] = "";
        static char expression[256] = "";
        static column_format_t format = COLUMN_FORMAT_TEXT;

        // Column name
        if (ImGui::TableNextColumn())
        {
            ImGui::ExpandNextItem();
            ImGui::InputTextWithHint("##Name", tr("Column name"), name, sizeof(name));
        }

        // Expression
        if (ImGui::TableNextColumn())
        {
            ImGui::ExpandNextItem();
            if (ImGui::InputTextWithHint("##Expression", tr("Expression i.e. S(GFL.TO, open)"), expression, sizeof(expression), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                add = true;
            }
        }

        // Format selector
        if (ImGui::TableNextColumn())
        {
            ImGui::ExpandNextItem();
            if (ImGui::BeginCombo("##Format", report_expression_column_format_name(format)))
            {
                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_TEXT), false, ImGuiSelectableFlags_None))
                    format = COLUMN_FORMAT_TEXT;

                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_NUMBER), false, ImGuiSelectableFlags_None))
                    format = COLUMN_FORMAT_NUMBER;

                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_CURRENCY), false, ImGuiSelectableFlags_None))
                    format = COLUMN_FORMAT_CURRENCY;

                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_PERCENTAGE), false, ImGuiSelectableFlags_None))
                    format = COLUMN_FORMAT_PERCENTAGE;

                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_DATE), false, ImGuiSelectableFlags_None))
                    format = COLUMN_FORMAT_DATE;

                if (ImGui::Selectable(report_expression_column_format_name(COLUMN_FORMAT_BOOLEAN), false, ImGuiSelectableFlags_None))
                    format = COLUMN_FORMAT_BOOLEAN;
                ImGui::EndCombo();
            }
        }

        // Add action
        if (ImGui::TableNextColumn())
        {
            ImGui::BeginDisabled(name[0] == 0 || expression[0] == 0);
            if (ImGui::Button(ICON_MD_ADD, ImVec2(ImGui::GetContentRegionAvail().x, 0)) || add)
            {
                report_expression_column_t ec{};
                string_copy(STRING_BUFFER(ec.name), name, string_length(name));
                string_copy(STRING_BUFFER(ec.expression), expression, string_length(expression));
                ec.format = format;
                array_push(report->expression_columns, ec);
                update_table = true;

                name[0] = 0;
                expression[0] = 0;
            }
            ImGui::EndDisabled();
        }

        ImGui::PopID();
    }

    if (update_table)
    {
        report_table_rebuild(report);
        report_refresh(report);
    }

    ImGui::EndTable();
    return true;
}

//
// PUBLIC API
//

void report_expression_columns_initialize()
{
    _report_expression_cache = MEM_NEW(HASH_REPORT, std::remove_pointer<decltype(_report_expression_cache)>::type);
}

void report_expression_columns_finalize()
{
    MEM_DELETE(_report_expression_cache);
}

void report_expression_column_reset(report_t* report)
{
    for (auto e = _report_expression_cache->begin_exclusive_lock(); e != _report_expression_cache->end_exclusive_lock(); ++e)
    {
        e->time = 0;
    }
    //_report_expression_cache->clear();
}

void report_expression_columns_save(report_t* report)
{
    auto cv_columns = config_set_array(report->data, STRING_CONST("columns"));
    config_array_clear(cv_columns);
    foreach(c, report->expression_columns)
    {
        auto cv_column = config_array_push(cv_columns, CONFIG_VALUE_OBJECT);
        config_set(cv_column, "name", c->name, string_length(c->name));
        config_set(cv_column, "expression", c->expression, string_length(c->expression));
        config_set(cv_column, "format", (double)c->format);
    }
}

void report_load_expression_columns(report_t* report)
{
    for (auto e : report->data["columns"])
    {
        string_const_t name = e["name"].as_string();
        string_const_t expr = e["expression"].as_string();
        column_format_t format = (column_format_t)e["format"].as_number();
        report_expression_column_t ec{};
        string_copy(ec.name, sizeof(ec.name), name.str, name.length);
        string_copy(ec.expression, sizeof(ec.expression), expr.str, expr.length);
        ec.format = format;
        array_push(report->expression_columns, ec);
    }
}

void report_open_expression_columns_dialog(report_handle_t report_handle)
{
    report_t* report = report_get(report_handle);
    app_open_dialog(tr(ICON_MD_DASHBOARD_CUSTOMIZE " Expression Columns"), 
        report_render_expression_columns_dialog, 900U, 400U, true, report, nullptr);
}

void report_add_expression_columns(report_handle_t report_handle, table_t* table)
{
    report_t* report = report_get(report_handle);
    foreach(c, report->expression_columns)
    {
        const size_t csize = string_length(c->name);
        string_t column_name = string_utf8_unescape(c->name, csize);

        bool column_has_custom_tooltip = string_find_string(STRING_ARGS(column_name), STRING_CONST("||"), 0) != STRING_NPOS;

        string_const_t column_formatted_name = string_to_const(column_name);
        if (!column_has_custom_tooltip)
        {
            bool column_has_custom_md_icon = csize > 3 && c->name[0] == '\\' && c->name[1] == 'x';
            column_formatted_name = string_format_static(STRING_CONST("%.*s||%s%.*s (%.*s)"),
                STRING_FORMAT(column_name),
                column_has_custom_md_icon ? "" : ICON_MD_VIEW_COLUMN " ",
                STRING_FORMAT(column_name),
                min(96, (int)string_length(c->expression)), c->expression);
        }

        column_flags_t flags = COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | COLUMN_NO_LOCALIZATION;
        if (c->format == COLUMN_FORMAT_TEXT)
            flags |= COLUMN_SEARCHABLE;

        if (c->format == COLUMN_FORMAT_BOOLEAN)
            flags |= COLUMN_ROUND_NUMBER | COLUMN_CENTER_ALIGN;

        table_add_column(table, STRING_ARGS(column_formatted_name), 
            LC2(report_column_evaluate_expression(_1, _2, report_handle, c)), c->format, flags);

        string_deallocate(column_name.str);
    }
}
