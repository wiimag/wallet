/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 *
 * Report dividends management module
 */

#include "report.h"

#include "title.h"

#include <framework/app.h>
#include <framework/table.h>
#include <framework/array.h>

struct report_dividends_dialog_t;

struct report_title_dividends_element_t
{
    tm date;
    time_t ts;
    double amount;
    double exchange_rate;

    config_handle_t cv;
    report_dividends_dialog_t* dlg;
};

struct report_dividends_dialog_t
{
    report_t* report{ nullptr };
    title_t*  title{ nullptr };
    table_t*  table{ nullptr };

    report_title_dividends_element_t* elements{ nullptr };
};

FOUNDATION_STATIC void report_dividends_edited(report_dividends_dialog_t* dlg)
{
    FOUNDATION_ASSERT(dlg);
    dlg->report->dirty = true;
    dlg->table->needs_sorting = true;
    title_refresh(dlg->title);
}


FOUNDATION_STATIC void report_dividends_edited(report_title_dividends_element_t* e)
{
    FOUNDATION_ASSERT(e);
    if (e->dlg == nullptr)
        return;

    report_dividends_edited(e->dlg);
}

FOUNDATION_STATIC report_title_dividends_element_t* report_dividends_add_new(report_dividends_dialog_t* dlg, const config_handle_t& cv)
{
    report_title_dividends_element_t element;
    element.cv = cv;
    element.dlg = dlg;
    element.ts = cv["date"].as_time();
    element.date = *localtime(&element.ts);
    element.amount = cv["amount"].as_number();
    element.exchange_rate = cv["xcg"].as_number();
    array_push(dlg->elements, element);

    return array_last(dlg->elements);
}

FOUNDATION_STATIC table_cell_t report_dividends_column_date(table_element_ptr_t element, const table_column_t* column)
{
    report_title_dividends_element_t* e = (report_title_dividends_element_t*)element;

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        ImGui::ExpandNextItem();
        if (ImGui::DateChooser("##Date", e->date, "%Y-%m-%d", true))
        {
            e->ts = mktime(&e->date);

            if (e->dlg)
            {
                // Resetting exchange as it will be refresh next time
                config_remove(e->cv, "xcg");

                string_const_t datestr = string_from_date(e->ts);
                config_set(e->cv, "date", STRING_ARGS(datestr));
                report_dividends_edited(e);
            }
        }
    }

    return e->ts;
}

FOUNDATION_STATIC table_cell_t report_dividends_column_amount(table_element_ptr_t element, const table_column_t* column)
{
    report_title_dividends_element_t* e = (report_title_dividends_element_t*)element;

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        ImGui::ExpandNextItem();
        if (ImGui::InputDouble("##Amount", &e->amount, 0.0, 0.0, "%.2lf $", ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (e->dlg)
            {
                config_set(e->cv, "amount", (double)e->amount);
                report_dividends_edited(e);
            }
        }
    }
    
    return e->amount;
}

FOUNDATION_STATIC table_cell_t report_dividends_column_rate(table_element_ptr_t element, const table_column_t* column)
{
    report_title_dividends_element_t* e = (report_title_dividends_element_t*)element;

    if (column->flags & COLUMN_ADD_NEW_ELEMENT)
    {
        if (math_real_is_zero(e->exchange_rate))
            e->exchange_rate = 1.0;
    }

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        ImGui::ExpandNextItem();
        if (ImGui::InputDouble("##Rate", &e->exchange_rate, 0.0, 0.0, "%.4lf", ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (e->dlg)
            {
                config_set(e->cv, "xcg", (double)e->exchange_rate);
                report_dividends_edited(e);
            }
        }
    }

    return e->exchange_rate;
}

FOUNDATION_STATIC table_cell_t report_dividends_column_add_or_delete(table_element_ptr_t element, const table_column_t* column)
{
    report_title_dividends_element_t* e = (report_title_dividends_element_t*)element;

    // Center button in available space.
    auto rect = table_current_cell_rect();
    ImGui::MoveCursor((ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(ICON_MD_DELETE_FOREVER).x) / 2.0f - IM_SCALEF(4), 0);
    if (column->flags & COLUMN_ADD_NEW_ELEMENT)
    {
        ImGui::BeginDisabled(math_real_is_zero(e->amount));
        if (ImGui::Button(ICON_MD_ADD))
        {
            FOUNDATION_ASSERT(column->table->user_data);
            report_dividends_dialog_t* dlg = (report_dividends_dialog_t*)column->table->user_data;

            auto cv_dividends = config_set_array(dlg->title->data, "dividends");
            auto cv_dividend = config_array_push(cv_dividends);

            if (e->ts == 0)
                e->ts = time_now();

            string_const_t datestr = string_from_date(e->ts);
            config_set(cv_dividend, "date", STRING_ARGS(datestr));
            config_set(cv_dividend, "amount", (double)e->amount);

            // Reset new element buffer
            e->amount = 0.0;

            ImGui::EndDisabled();

            report_title_dividends_element_t* new_element = report_dividends_add_new(dlg, cv_dividend);
            if (new_element)
            {
                report_dividends_edited(new_element);
                return TABLE_CELL_EVENT_NEW_ELEMENT;
            }
        }
        ImGui::EndDisabled();
    }
    else if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        if (ImGui::Button(ICON_MD_DELETE_FOREVER))
        {
            auto cv_dividends = config_set_array(e->dlg->title->data, "dividends");
            if (config_remove(cv_dividends, e->cv))
            {
                unsigned del_pos = e - e->dlg->elements;
                array_erase_ordered_safe(e->dlg->elements, del_pos);

                report_dividends_edited((report_dividends_dialog_t*)column->table->user_data);

                return TABLE_CELL_EVENT_DELETED_ELEMENT;
            }
        }
    }

    return false;
}

FOUNDATION_STATIC table_t* report_dividends_create_table(report_dividends_dialog_t* dlg)
{
    table_t* table = table_allocate("Dividends", 
        TABLE_ADD_NEW_ROW | TABLE_SUMMARY | TABLE_LOCALIZATION_CONTENT | TABLE_HIGHLIGHT_HOVERED_ROW | 
        ImGuiTableFlags_SizingStretchSame);

    table->user_data = dlg;

    table_add_column(table, report_dividends_column_date, "Date", COLUMN_FORMAT_DATE, COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING);
    table_add_column(table, report_dividends_column_amount, "Amount", COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING | COLUMN_LEFT_ALIGN);
    table_add_column(table, report_dividends_column_rate, "Exchange Rate", COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING | COLUMN_HIDE_DEFAULT | COLUMN_SUMMARY_AVERAGE | COLUMN_LEFT_ALIGN);
    table_add_column(table, report_dividends_column_add_or_delete, ICON_MD_DELETE "||Delete", COLUMN_FORMAT_BOOLEAN, COLUMN_CUSTOM_DRAWING | COLUMN_CENTER_ALIGN)
        .set_width(IM_SCALEF(20));

    return table;
}

FOUNDATION_STATIC bool report_dividends_render_dialog(void* user_data)
{
    report_dividends_dialog_t* dlg = (report_dividends_dialog_t*)user_data;

    if (dlg->table == nullptr)
        dlg->table = report_dividends_create_table(dlg);

    table_render(dlg->table, dlg->elements, 0.0f, 0.0f);

    return true;
}

FOUNDATION_STATIC report_dividends_dialog_t* report_dividends_create_dialog(report_t* report, title_t* title)
{
    report_dividends_dialog_t* dlg = MEM_NEW(HASH_REPORT, report_dividends_dialog_t);
    dlg->report = report;
    dlg->title = title;

    for (const auto& e : title->data["dividends"])
    {
        report_dividends_add_new(dlg, e);
    }

    return dlg;
}

FOUNDATION_STATIC void report_dividends_close_handler(void* user_data)
{
    report_dividends_dialog_t* dlg = (report_dividends_dialog_t*)user_data;

    report_refresh(dlg->report);

    table_deallocate(dlg->table);
    array_deallocate(dlg->elements);
    MEM_DELETE(dlg);
}

//
// PUBLIC
//

void report_open_dividends_dialog(report_t* report, title_t* title)
{
    report_dividends_dialog_t* dlg = report_dividends_create_dialog(report, title);
    
    char dialog_title[64];
    string_const_t report_name = ::report_name(report);
    string_const_t title_name = string_const(title->code, title->code_length);
    tr_format(STRING_BUFFER(dialog_title), "{1} Dividends - {0}", report_name, title_name);
    app_open_dialog(dialog_title, report_dividends_render_dialog, IM_SCALEF(260), IM_SCALEF(300), true, dlg, report_dividends_close_handler);
}
