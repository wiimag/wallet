/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */

#include "watches.h"

#include <framework/app.h>
#include <framework/array.h>
#include <framework/memory.h>
#include <framework/table.h>
#include <framework/expr.h>
#include <framework/session.h>

#define HASH_WATCHES static_hash_string("watches", 7, 0xd9a79e530f96dc6cULL)

watch_context_t* _shared_context = nullptr;
watch_context_t* _active_context = nullptr;

FOUNDATION_STATIC watch_variable_t* watch_find_variable(watch_context_t* context, const char* name, size_t name_length)
{
    FOUNDATION_ASSERT(context != nullptr);

    for (unsigned i = 0, end = array_size(context->variables); i < end; ++i)
    {
        watch_variable_t* var = context->variables + i;
        if (string_equal(var->name.str, var->name.length, name, name_length))
            return var;
    }

    return nullptr;
}

FOUNDATION_STATIC table_cell_t watch_point_column_name(table_element_ptr_t element, const table_column_t* column)
{
    watch_point_t* point = (watch_point_t*)element;

    return table_cell_t(STRING_ARGS(point->name));
}

FOUNDATION_STATIC watch_point_t* watch_point_find(watch_context_t* context, const char* name, size_t length)
{
    for (unsigned i = 0, end = array_size(context->points); i < end; ++i)
    {
        watch_point_t* p = context->points + i;
        if (string_equal(p->name.str, p->name.length, name, length))
            return p;
    }

    return nullptr;
}

FOUNDATION_STATIC bool watch_point_evaluate(watch_context_t* context, watch_point_t* point, bool share = false)
{
    if (point->expression.length == 0)
        return false;

    for (unsigned i = 0, end = array_size(context->variables); i < end; ++i)
    {
        watch_variable_t* var = context->variables + i;
        expr_result_t var_value = NIL;
        if (var->value.type == WATCH_VALUE_TEXT)
            var_value = expr_result_t(string_to_const(var->value.text));
        else
            var_value = expr_result_t(var->value.number);
        expr_set_or_create_global_var(STRING_ARGS(var->name), var_value);
    }

    expr_result_t result = eval(point->expression.str, point->expression.length);

    if (point->record.type == WATCH_VALUE_TEXT)
        string_deallocate(point->record.text.str);

    if (point->type == WATCH_POINT_UNDEFINED)
        point->type = WATCH_POINT_VALUE;
        
    if (result.type == EXPR_RESULT_NULL)
    {
        point->record.type = WATCH_VALUE_NULL;
    }
    else if (result.type == EXPR_RESULT_NUMBER)
    {
        point->record.type = WATCH_VALUE_NUMBER;
        point->record.number = result.as_number();
    }
    else
    {
        string_const_t str = result.as_string();
        point->record.type = WATCH_VALUE_TEXT;
        point->record.text = string_clone(str.str, str.length);
    }

    // Update shared context with this watch point
    if (share && _shared_context)
    { 
        watch_point_t* wsp = watch_point_find(_shared_context, point->name.str, point->name.length);
        if (wsp)
        {
            wsp->type = point->type;
            
            string_deallocate(wsp->expression.str);
            wsp->expression = string_clone(STRING_ARGS(point->expression));
        }
        else
        {
            watch_point_add(_shared_context, 
                point->name.str, point->name.length, 
                point->expression.str, point->expression.length, false);
        }
    }

    return !result.is_null();
}

FOUNDATION_STATIC const char* watch_point_format_string(watch_point_type_t type)
{
    switch (type)
    {
        case WATCH_POINT_VALUE:
            return tr("Default");
        case WATCH_POINT_INTEGER:
            return tr("Integer");
        case WATCH_POINT_DATE:
            return tr("Date");
        case WATCH_POINT_PLOT:
            return tr("Plot");
        case WATCH_POINT_TABLE:
            return tr("Table");
    }

    return tr("Undefined");
}

FOUNDATION_STATIC bool watch_point_edit_expression_render_dialog(void* user_data)
{
    watch_point_t* point = (watch_point_t*)user_data;
    FOUNDATION_ASSERT(point != nullptr);
    FOUNDATION_ASSERT(point->context != nullptr);

    if (ImGui::IsWindowAppearing())
    {
        string_copy(STRING_BUFFER(point->name_buffer), STRING_ARGS(point->name));
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TrTextUnformatted("Name");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
    if (ImGui::InputText("##Name", STRING_BUFFER(point->name_buffer)))
    {
        string_deallocate(point->name.str);
        point->name = string_utf8_unescape(STRING_LENGTH(point->name_buffer));
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Evaluate")))
    {
        string_deallocate(point->expression.str);
        point->expression = string_clone(point->expression_edit_buffer, string_length(point->expression_edit_buffer));
        watch_point_evaluate(point->context, point, true);
    }

    ImGui::SameLine();
    const char* preview_format = watch_point_format_string(point->type);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
    if (ImGui::BeginCombo("##Format", preview_format))
    {
        if (ImGui::Selectable(tr("Default"), point->type == WATCH_POINT_VALUE))
            point->type = WATCH_POINT_VALUE;
        if (ImGui::Selectable(tr("Integer"), point->type == WATCH_POINT_INTEGER))
            point->type = WATCH_POINT_INTEGER;
        if (ImGui::Selectable(tr("Date"), point->type == WATCH_POINT_DATE))
            point->type = WATCH_POINT_DATE;
        if (ImGui::Selectable(tr("Plot"), point->type == WATCH_POINT_PLOT))
            point->type = WATCH_POINT_PLOT;
        if (ImGui::Selectable(tr("Table"), point->type == WATCH_POINT_TABLE))
            point->type = WATCH_POINT_TABLE;
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    if (ImGui::TreeNode(tr("Variables")))
    {
        for (unsigned i = 0, end = array_size(point->context->variables); i < end; ++i)
        {
            watch_variable_t* var = point->context->variables + i;
            if (var->value.type == WATCH_VALUE_TEXT)
                ImGui::Text("%.*s=%.*s", STRING_FORMAT(var->name), STRING_FORMAT(var->value.text));
            else 
                ImGui::Text("%.*s=%lf", STRING_FORMAT(var->name), var->value.number);
        }
        ImGui::TreePop();
    }

    if (point->expression_edit_buffer_size == 0)
    {
        point->expression_edit_buffer_size = max(size_t(point->expression.length * 1.5), SIZE_C(4096));
        point->expression_edit_buffer = (char*)memory_allocate(HASH_WATCHES, point->expression_edit_buffer_size, 0, MEMORY_PERSISTENT);

        string_copy(point->expression_edit_buffer, point->expression_edit_buffer_size, STRING_ARGS(point->expression));
    }

    if (ImGui::InputTextMultiline("##Expression", point->expression_edit_buffer, point->expression_edit_buffer_size, ImGui::GetContentRegionAvail()))
    {
        size_t edit_expr_length = string_length(point->expression_edit_buffer);
        double capacity_usage = (double)edit_expr_length / (double)point->expression_edit_buffer_size;
        if (capacity_usage > 0.5)
        {
            const size_t new_capacity = size_t(point->expression_edit_buffer_size * 1.5);
            string_resize(point->expression_edit_buffer, edit_expr_length, point->expression_edit_buffer_size, new_capacity, 0);
            point->expression_edit_buffer_size = new_capacity;
        }
    }

    return true;
}

FOUNDATION_STATIC void watch_point_edit_expression(watch_context_t* context, watch_point_t* point)
{
    char title_buffer[256];
    string_t title = tr_format(STRING_BUFFER(title_buffer), "Edit {0} Expression - {1}", point->name, context->name);
    app_open_dialog(title.str, watch_point_edit_expression_render_dialog, IM_SCALEF(400), IM_SCALEF(300), true, point, nullptr);
}

FOUNDATION_STATIC table_cell_t watch_point_column_edit_expression(table_element_ptr_t element, const table_column_t* column)
{
    watch_point_t* point = (watch_point_t*)element;
    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        if (ImGui::Button(ICON_MD_EDIT))
        {
            watch_point_edit_expression(point->context, point);
        }
        else if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            if (point->expression.length)
                ImGui::TrText("Edit expression : \n%.*s", STRING_FORMAT(point->expression));
            else
                ImGui::TrTextUnformatted("Edit expression");

            ImGui::EndTooltip();
        }
    }

    return (double)point->record.type;
}

FOUNDATION_STATIC table_cell_t watch_point_column_value(table_element_ptr_t element, const table_column_t* column)
{
    watch_point_t* point = (watch_point_t*)element;
    FOUNDATION_ASSERT(point != nullptr);
    FOUNDATION_ASSERT(point->context != nullptr);

    if (point->type == WATCH_POINT_VALUE)
    {
        if (point->record.type == WATCH_VALUE_UNDEFINED)
        {
            if (!watch_point_evaluate(point->context, point))
                return nullptr;
            FOUNDATION_ASSERT(point->record.type != WATCH_VALUE_UNDEFINED);
        }

        if (column->flags & COLUMN_RENDER_ELEMENT)
        {
            ImGui::AlignTextToFramePadding();
            if (point->record.type == WATCH_VALUE_NUMBER)
            {
                ImGui::Text("%lf", point->record.number);
            }
            else if (point->record.type == WATCH_VALUE_TEXT)
            {
                ImGui::TextUnformatted(STRING_RANGE(point->record.text));
            }
            else if (point->record.type == WATCH_VALUE_BOOLEAN)
            {
                bool b = math_real_is_zero(point->record.number) ? false : true;
                ImGui::Checkbox("##Bool", &b);
                point->record.number = b ? 1.0 : 0;
            }
            else if (point->record.type == WATCH_VALUE_DATE)
            {
                string_const_t datestr = string_from_date((time_t)point->record.number);
                ImGui::TextUnformatted(STRING_RANGE(datestr));
            }
        }

        if (point->record.type == WATCH_VALUE_NUMBER)
            return table_cell_t(point->record.number);

        if (point->record.type == WATCH_VALUE_TEXT)
            return table_cell_t(STRING_ARGS(point->record.text));

        if (point->record.type == WATCH_VALUE_BOOLEAN)
            return table_cell_t(!math_real_is_zero(point->record.number));

        if (point->record.type == WATCH_VALUE_DATE)
            return table_cell_t((time_t)point->record.number);        
    }
    else if (point->type == WATCH_POINT_DATE)
    {
        if (point->record.type == WATCH_VALUE_UNDEFINED)
        {
            if (!watch_point_evaluate(point->context, point))
                return nullptr;
            FOUNDATION_ASSERT(point->record.type != WATCH_VALUE_UNDEFINED);
        }

        if (column->flags & COLUMN_RENDER_ELEMENT)
        {
            string_const_t datestr = string_from_date((time_t)point->record.number);
            ImGui::TextUnformatted(STRING_RANGE(datestr));
        }

        return table_cell_t((time_t)point->record.number);  
    }
    else if (point->type == WATCH_POINT_INTEGER)
    {
        if (point->record.type == WATCH_VALUE_UNDEFINED)
        {
            if (!watch_point_evaluate(point->context, point))
                return nullptr;
            FOUNDATION_ASSERT(point->record.type != WATCH_VALUE_UNDEFINED);
        }

        if (column->flags & COLUMN_RENDER_ELEMENT)
            ImGui::Text("%.0lf", point->record.number);

        return table_cell_t(point->record.number);  
    }
    else if (point->type == WATCH_POINT_TABLE || point->type == WATCH_POINT_PLOT)
    {
        if (column->flags & COLUMN_RENDER_ELEMENT && ImGui::SmallButton(tr("Execute")))
           watch_point_evaluate(point->context, point);            
    }
        
    return nullptr;
}

FOUNDATION_STATIC void watch_point_deallocate(watch_point_t* w)
{
    if (w->record.type == WATCH_VALUE_TEXT)
        string_deallocate(w->record.text.str);
            
    memory_deallocate(w->expression_edit_buffer);
    string_deallocate(w->expression.str);
    string_deallocate(w->name.str);
}

FOUNDATION_STATIC void watch_table_contextual_menu(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    watch_point_t* point = (watch_point_t*)element;
    if (!point)
    {
        ImGui::BeginDisabled(_active_context == nullptr);
        if (ImGui::TrBeginMenu("Add Shared Watch"))
        {
            for (unsigned i = 0, end = array_size(_shared_context->points); i < end; ++i)
            {
                watch_point_t* wsp = _shared_context->points + i;
                if (ImGui::Selectable(wsp->name.str, false))
                {
                    watch_point_add(_active_context , 
                        wsp->name.str, wsp->name.length, 
                        wsp->expression.str, wsp->expression.length, 
                        true, false);
                    break;
                }
                else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                {
                    ImGui::SetTooltip("%.*s", STRING_FORMAT(wsp->expression));
                }
            }

            ImGui::EndMenu();
        }
        ImGui::EndDisabled();
    }
    else
    {
        if (ImGui::TrMenuItem(ICON_MD_EDIT " Edit"))
        {
            watch_point_edit_expression(point->context, point);
        }

        if (ImGui::TrBeginMenu(ICON_MD_PIN " Format"))
        {
            if (ImGui::Selectable(tr("Default"), point->type == WATCH_POINT_VALUE))
                point->type = WATCH_POINT_VALUE;
            if (ImGui::Selectable(tr("Integer"), point->type == WATCH_POINT_INTEGER))
                point->type = WATCH_POINT_INTEGER;
            if (ImGui::Selectable(tr("Date"), point->type == WATCH_POINT_DATE))
                point->type = WATCH_POINT_DATE;
            if (ImGui::Selectable(tr("Plot"), point->type == WATCH_POINT_PLOT))
                point->type = WATCH_POINT_PLOT;
            if (ImGui::Selectable(tr("Table"), point->type == WATCH_POINT_TABLE))
                point->type = WATCH_POINT_TABLE;

            ImGui::EndMenu();
        }

        if (ImGui::TrMenuItem(ICON_MD_DELETE " Delete"))
        {
            unsigned pos = point - point->context->points;
            watch_point_deallocate(point);
            array_erase_safe(point->context->points, pos);
        }
    }
}

FOUNDATION_STATIC bool watch_table_sort_columns(table_t* table, table_column_t* column, int sort_direction)
{
    string_const_t tr_column_name = RTEXT("Name");
    string_const_t column_name = string_table_decode_const(column->name);
    if (string_equal(STRING_ARGS(column_name), STRING_ARGS(tr_column_name)))
    {
        array_sort(table->rows, [sort_direction](const table_row_t& a, const table_row_t& b)
        {
            const watch_point_t* wpa = (const watch_point_t*)a.element;
            const watch_point_t* wpb = (const watch_point_t*)b.element;

            int order = sort_direction == 2 ? -1 : 1;
            return string_compare_skip_code_points(STRING_ARGS(wpa->name), STRING_ARGS(wpb->name)) * order;
        });

        return true;
    }

    return table_default_sorter(table, column, sort_direction);
}

FOUNDATION_STATIC table_t* watch_create_table(watch_context_t*)
{
    table_t* table = table_allocate("WatchPoints", 
        TABLE_HIGHLIGHT_HOVERED_ROW | TABLE_LOCALIZATION_CONTENT);

    table_add_column(table, watch_point_column_name, "Name", COLUMN_FORMAT_TEXT, COLUMN_SORTABLE | COLUMN_SEARCHABLE);
    table_add_column(table, watch_point_column_value, "Value", COLUMN_FORMAT_TEXT, COLUMN_SORTABLE | COLUMN_SEARCHABLE | COLUMN_CUSTOM_DRAWING);
    table_add_column(table, watch_point_column_edit_expression, ICON_MD_FUNCTIONS "||Edit Expression", COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING | COLUMN_CENTER_ALIGN)
        .set_width(IM_SCALEF(20));

    table->sort = watch_table_sort_columns;
    table->context_menu = watch_table_contextual_menu;

    return table;
}

FOUNDATION_STATIC bool watch_render_dialog(void* user_data)
{
    watch_context_t* context = (watch_context_t*)user_data;
    if (!context)
        return false;

    watches_render(context);
    return true;
}

FOUNDATION_STATIC bool watch_render_new_point(watch_context_t* context)
{
    FOUNDATION_ASSERT(context != nullptr);
    
    ImGui::ExpandNextItem(IM_SCALEF(24) * 2);
    ImGui::InputTextWithHint("##Name", tr("Enter new watch name..."), STRING_BUFFER(context->name_buffer), ImGuiInputTextFlags_None);

    ImGui::SameLine();
    ImGui::BeginDisabled(_shared_context == nullptr || array_size(_shared_context->points) == 0);
    if (ImGui::Button(ICON_MD_ARROW_DROP_DOWN))
        ImGui::OpenPopup("##SharedWatch");
    else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::TrTooltip("Add Shared Watch");

    if (ImGui::BeginPopup("##SharedWatch", ImGuiWindowFlags_AlwaysAutoResize))
    {
        static float max_label_width = 100.0f;
        const char* add_all_label = tr(ICON_MD_COPY_ALL " Add All");

        if (ImGui::IsWindowAppearing())
        {
            // Sort shared watch points by name
            array_sort(_shared_context->points, [](const watch_point_t& a, const watch_point_t& b)
            {
                return string_compare_skip_code_points(STRING_ARGS(a.name), STRING_ARGS(b.name));
            });


            // Calculate max label width
            max_label_width = ImGui::CalcTextSize(add_all_label).x;
            for (unsigned i = 0, end = array_size(_shared_context->points); i < end; ++i)
            {
                watch_point_t* wsp = _shared_context->points + i;
                max_label_width = math_max(max_label_width, ImGui::CalcTextSize(wsp->name.str).x);
            }
        }

        // Render "Add All" button
        if (ImGui::Selectable(add_all_label, false, ImGuiSelectableFlags_AllowItemOverlap, {0, 0.0f}))
        {
            for (unsigned i = 0, end = array_size(_shared_context->points); i < end; ++i)
            {
                watch_point_t* wsp = _shared_context->points + i;
                watch_point_add(context, 
                    wsp->name.str, wsp->name.length, 
                    wsp->expression.str, wsp->expression.length, 
                    true, false);
            }
        }

        ImGui::Separator();

        for (unsigned i = 0, end = array_size(_shared_context->points); i < end; ++i)
        {
            watch_point_t* wsp = _shared_context->points + i;
            if (ImGui::Selectable(wsp->name.str, false, ImGuiSelectableFlags_AllowItemOverlap, {max_label_width, 0.0f}))
            {
                watch_point_add(context, 
                    wsp->name.str, wsp->name.length, 
                    wsp->expression.str, wsp->expression.length, 
                    true, false);
                break;
            }
            else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            {
                ImGui::SetTooltip("%.*s", STRING_FORMAT(wsp->expression));
            }

            ImGui::SameLine(max_label_width + IM_SCALEF(12));
            ImGui::PushID(wsp);
            if (ImGui::SmallButton(ICON_MD_DELETE_FOREVER))
            {
                watch_point_deallocate(wsp);
                array_erase_safe(_shared_context->points, i);
                --i;
                --end;
            }
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();

    const size_t name_length = string_length(context->name_buffer);
    ImGui::SameLine();
    ImGui::BeginDisabled(name_length == 0);
    if (ImGui::Button(ICON_MD_NEW_LABEL))
    {
        watch_point_add(context, context->name_buffer, name_length);
        ImGui::EndDisabled();
        return string_copy(STRING_BUFFER(context->name_buffer), STRING_CONST("")).length == 0;
    }
    ImGui::EndDisabled();

    return false;
}

//
// PUBLIC API
//

void watches_render(watch_context_t* context)
{
    FOUNDATION_ASSERT(context != nullptr);

    if (context->table == nullptr)
        context->table = watch_create_table(context);

    watch_render_new_point(context);

    _active_context = context;
    table_render(
        context->table, 
        context->points, array_size(context->points), sizeof(watch_point_t), 
        0, 0);
}

void watch_open_dialog(watch_context_t* context)
{
    app_open_dialog(context->name.str, watch_render_dialog, IM_SCALEF(250), IM_SCALEF(400), true, context, nullptr);
}

void watch_set_variable(watch_context_t* context, const char* name, size_t name_length, double number)
{
    FOUNDATION_ASSERT(context != nullptr);

    watch_variable_t* var = watch_find_variable(context, name, name_length);
    if (var)
    {
        if (var->value.type == WATCH_VALUE_TEXT)
            string_deallocate(var->value.text.str);

        var->value.type = WATCH_VALUE_NUMBER;
        var->value.number = (double)number;
    }
    else
    {
        watch_variable_t new_var;
        new_var.name = string_clone(name, name_length);
        new_var.value.type = WATCH_VALUE_NUMBER;
        new_var.value.number = (double)number;
        array_push_memcpy(context->variables, &new_var);
    }
}

void watch_point_add(watch_context_t* context, 
    const char* name, size_t name_length, 
    const char* expression, size_t expression_length, 
    bool evaluate /*= true*/, bool edit /*= true*/)
{
    FOUNDATION_ASSERT(context != nullptr);

    watch_point_t new_point;
    new_point.type = WATCH_POINT_UNDEFINED;
    new_point.name = string_clone(name, name_length);
    new_point.expression = string_clone(expression, expression_length);
    new_point.record.type = WATCH_VALUE_UNDEFINED;
    new_point.record.number = 0;
    new_point.context = context;
    new_point.expression_edit_buffer = nullptr;
    new_point.expression_edit_buffer_size = 0;

    if (new_point.expression.length > 0)
    {
        new_point.type = WATCH_POINT_VALUE;
        if (evaluate)
            watch_point_evaluate(context, &new_point);
    }
    array_push_memcpy(context->points, &new_point);

    if (evaluate && edit)
        watch_point_edit_expression(context, array_last(context->points));
}

void watch_set_variable(watch_context_t* context, const char* name, size_t name_length, time_t date)
{
    FOUNDATION_ASSERT(context != nullptr);

    watch_variable_t* var = watch_find_variable(context, name, name_length);
    if (var)
    {
        if (var->value.type == WATCH_VALUE_TEXT)
            string_deallocate(var->value.text.str);

        var->value.type = WATCH_VALUE_DATE;
        var->value.number = (double)date;
    }
    else
    {
        watch_variable_t new_var;
        new_var.name = string_clone(name, name_length);
        new_var.value.type = WATCH_VALUE_DATE;
        new_var.value.number = (double)date;
        array_push_memcpy(context->variables, &new_var);
    }
}

void watch_set_variable(watch_context_t* context, const char* name, size_t name_length, const char* value, size_t value_length)
{
    FOUNDATION_ASSERT(context != nullptr);

    watch_variable_t* var = watch_find_variable(context, name, name_length);
    if (var)
    {
        if (var->value.type == WATCH_VALUE_TEXT)
            string_deallocate(var->value.text.str);

        var->value.type = WATCH_VALUE_TEXT;
        var->value.text = string_clone(value, value_length);
    }
    else
    {
        watch_variable_t new_var;
        new_var.name = string_clone(name, name_length);
        new_var.value.type = WATCH_VALUE_TEXT;
        new_var.value.text = string_clone(value, value_length);
        array_push_memcpy(context->variables, &new_var);
    }
}

void watch_load(watch_context_t* context, config_handle_t data)
{
    for (auto e : data)
    {
        string_const_t name = e["name"].as_string();
        string_const_t expression = e["expression"].as_string();
        watch_point_type_t type = (watch_point_type_t)e["type"].as_integer();

        watch_point_t p;
        p.name = string_clone(name.str, name.length);
        p.expression = string_clone(expression.str, expression.length);
        p.type = type;
        p.record.type = WATCH_VALUE_UNDEFINED;
        p.context = context;
        p.expression_edit_buffer = nullptr;
        p.expression_edit_buffer_size = 0;

        array_push_memcpy(context->points, &p);
    }
}

void watch_save(watch_context_t* context, config_handle_t data)
{
    FOUNDATION_ASSERT(context);
    FOUNDATION_ASSERT(config_value_type(data) == CONFIG_VALUE_ARRAY);

    for (unsigned i = 0, end = array_size(context->points); i < end; ++i)
    {
        watch_point_t* p = context->points + i;
        auto e = config_array_push(data, CONFIG_VALUE_OBJECT);
        config_set(e, "name", STRING_ARGS(p->name));
        config_set(e, "expression", STRING_ARGS(p->expression));
        config_set(e, "type", (double)p->type);
    }
}

watch_context_t* watch_create(const char* name, size_t length, config_handle_t data /*= nullptr*/)
{
    MEMORY_TRACKER(HASH_WATCHES);

    watch_context_t* context = memory_allocate<watch_context_t>();
    context->name = string_clone(name, length);
    context->points = nullptr;
    context->variables = nullptr;
    context->table = nullptr;
    context->name_buffer[0] = 0;

    if (config_value_type(data) == CONFIG_VALUE_ARRAY)
        watch_load(context, data);

    return context;
}

void watch_destroy(watch_context_t*& context)
{
    if (context == nullptr)
        return;

    for (unsigned i = 0, end = array_size(context->variables); i < end; ++i)
    {
        watch_variable_t* var = context->variables + i;
        if (var->value.type == WATCH_VALUE_TEXT)
            string_deallocate(var->value.text.str);
        string_deallocate(var->name.str);
    }
    array_deallocate(context->variables);

    for (unsigned i = 0, end = array_size(context->points); i < end; ++i)
    {
        watch_point_t* w = context->points + i;
        watch_point_deallocate(w);
    }
    array_deallocate(context->points);

    table_deallocate(context->table);
    string_deallocate(context->name.str);

    memory_deallocate(context);
    context = nullptr;
}

string_const_t watches_shared_file_path()
{
    return session_get_user_file_path(STRING_CONST("watches.json"));
}

void watches_init()
{
    if (!main_is_interactive_mode())
        return;

    if (_shared_context == nullptr)
    {
        string_const_t watches_shared_path = watches_shared_file_path();
        config_handle_t watches_config = config_parse_file(STRING_ARGS(watches_shared_path), CONFIG_OPTION_PRESERVE_INSERTION_ORDER);
        _shared_context = watch_create(STRING_CONST("shared"), watches_config);
        config_deallocate(watches_config);
    }
}

void watches_shutdown()
{
    _active_context = nullptr;
    if (_shared_context)
    {
        string_const_t watches_shared_path = watches_shared_file_path();
        config_handle_t data = config_allocate(CONFIG_VALUE_ARRAY);
        watch_save(_shared_context, data);
        config_write_file(watches_shared_path, data);
        config_deallocate(data);
        watch_destroy(_shared_context);
    }
}

DEFINE_MODULE(WATCHES, watches_init, watches_shutdown, MODULE_PRIORITY_MODULE);
