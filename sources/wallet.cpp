/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#include "wallet.h"

#include "settings.h"
#include "report.h"

#include "framework/table.h"
#include "framework/common.h"
#include "framework/imgui.h"

#include <foundation/array.h>
#include <foundation/uuid.h>
#include <foundation/thread.h>

#include <algorithm>

struct plot_context_t
{
    time_t ref;
    size_t range;
    size_t stride;

    union {
        const void* data;
        const history_t* history;
    };

    double x_min{ DBL_MAX }, x_max{ -DBL_MAX }, n{ 0 };
    double a{ 0 }, b{ 0 }, c{ 0 }, d{ 0 }, e{ 0 }, f{ 0 };
};

bool wallet_draw(wallet_t* wallet, float available_space)
{
    bool updated = false;

    const float control_width = imgui_get_font_ui_scale(180.0f);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("History");
        ImGui::MoveCursor(available_space - imgui_get_font_ui_scale(158.0f), 0, true);
        if (ImGui::Checkbox("##History", &wallet->track_history))
            updated |= true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Track historical data for this report.");
    }

    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Target %");
        ImGui::MoveCursor(available_space - ImGui::GetCursorPos().x - control_width - imgui_get_font_ui_scale(128.0f), 0, true);
        ImGui::SetNextItemWidth(control_width);
        double p100 = wallet->main_target * 100.0;
        if (ImGui::InputDouble("##Target%", &p100, 0, 0, "%.3g %%", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
        {
            wallet->main_target = p100 / 100.0;
            updated |= true;
        }
    }

    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Currency");

        char currency[64];
        string_copy(STRING_CONST_CAPACITY(currency), STRING_ARGS(wallet->preferred_currency));

        ImGui::MoveCursor(available_space - ImGui::GetCursorPos().x - control_width - imgui_get_font_ui_scale(128.0f), 0, true);
        ImGui::SetNextItemWidth(control_width);
        if (ImGui::InputTextWithHint("##Currency", "i.e. USD", STRING_CONST_CAPACITY(currency), ImGuiInputTextFlags_AutoSelectAll))
        {
            char* old = wallet->preferred_currency.str;
            wallet->preferred_currency = string_clone(currency, string_length(currency));
            string_deallocate(old);
            updated |= true;
        }
    }

    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Fund");
        ImGui::MoveCursor(available_space - ImGui::GetCursorPos().x - control_width - imgui_get_font_ui_scale(70.0f), 0, true);
        ImGui::SetNextItemWidth(control_width);
        if (ImGui::InputDouble("##Fund", &wallet->funds, 0, 0, "%.2lf $", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
            updated |= true;
    }

    return updated;
}

static void wallet_history_sort(wallet_t* wallet)
{
    std::sort(wallet->history, wallet->history + array_size(wallet->history), [](const history_t& a, const history_t& b)
    {
        return b.date < a.date;
    });
}

static void wallet_history_update_entry(report_t* report, wallet_t* wallet, history_t& entry)
{
    while (!report_sync_titles(report))
        dispatcher_wait_for_wakeup_main_thread(50);
    report->dirty = true;

    entry.date = time_now();
    entry.source = wallet;
    entry.show_edit_ui = true;
    entry.funds = wallet->funds;
    entry.investments = report->total_investment;
    entry.total_value = report->total_value;
    entry.gain = report->wallet->sell_total_gain;
}

static void wallet_history_add_new_entry(report_t* report, wallet_t* wallet)
{
    time_t today = time_now();

    // Check if we already have an entry for today
    for (auto& h : generics::fixed_array(wallet->history))
    {
        if (time_date_equal(today, h.date))
        {
            wallet_history_update_entry(report, wallet, h);
            h.show_edit_ui = true;
            return;
        }
    }

    history_t new_entry{ today };
    wallet_history_update_entry(report, wallet, new_entry);

    if (array_size(wallet->history) > 0)
    {
        const history_t& fh = wallet->history[0];
        new_entry.broker_value = fh.broker_value;
        new_entry.other_assets = fh.other_assets;
    }

    wallet->history = array_push(wallet->history, new_entry);
    wallet_history_sort(wallet);
}

static void wallet_history_delete_entry(report_t* report, history_t* h)
{
    size_t pos = h - &h->source->history[0];
    array_erase(h->source->history, pos);
    wallet_history_sort(report->wallet);
    report_summary_update(report);
    report->dirty = true;
}

static void wallet_history_draw_toolbar(report_handle_t& selected_report_id)
{
    report_t* selected_report = report_get(selected_report_id);

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::SameLine(0, 8);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Report");
    ImGui::SameLine(0, 16);
    ImGui::SetNextItemWidth(300.0f);
    if (ImGui::BeginCombo("##Report", !selected_report ? "None" : string_table_decode(selected_report->name), ImGuiComboFlags_None))
    {
        for (int i = 0; i < report_count(); ++i)
        {
            report_t* report = report_get_at(i);
            if (report->wallet->track_history)
            {
                const bool is_selected = selected_report && uuid_equal(selected_report->id, report->id);
                if (ImGui::Selectable(string_table_decode(report->name), is_selected))
                {
                    selected_report_id = report->id;
                    selected_report = report;
                    break;
                }

                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (selected_report != nullptr)
    {
        wallet_t* wallet = selected_report->wallet;
        ImGui::SameLine();
        if (ImGui::Button("Add Entry"))
        {
            wallet_history_add_new_entry(selected_report, wallet);
        }

        ImGui::SameLine(0, 100.0f);
        if (ImGui::Checkbox("Show Extra Charts", &wallet->show_extra_charts))
            ImPlot::SetNextAxesToFit();
    }
    ImGui::EndGroup();
}

static report_handle_t wallet_history_select_initial_report()
{
    time_t recent = 0;
    static report_handle_t selected_report_id = uuid_null();
    if (uuid_is_null(selected_report_id))
    {
        for (int i = 0; i < report_count(); ++i)
        {
            report_t* report = report_get_at(i);
            if (report->wallet->track_history && array_size(report->wallet->history))
            {
                const history_t& fh = report->wallet->history[0];
                if (fh.date > recent)
                {
                    recent = fh.date;
                    selected_report_id = report->id;
                }
            }
        }
    }

    return selected_report_id;
}

static cell_t wallet_history_column_date(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        ImGui::BeginGroup();
        string_const_t field_str = string_from_date(h->date);
        const float width = ImGui::GetContentRegionAvail().x;
        const float field_width = ImGui::CalcTextSize(field_str.str, field_str.str + field_str.length).x;
        ImGui::TextUnformatted(field_str.str, field_str.str + field_str.length);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            h->show_edit_ui = true;

        if ((field_width + 40.0f) < width)
        {
            ImGui::MoveCursor(width - field_width - imgui_get_font_ui_scale(48.0f), 0, true);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 0, 0, 0));
            if (ImGui::SmallButton(ICON_MD_FORMAT_LIST_BULLETED))
                h->show_edit_ui = true;
            ImGui::PopStyleColor(1);
        }
        ImGui::EndGroup();
    }
    return h->date;
}

static cell_t wallet_history_column_funds(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return h->funds;
}

static cell_t wallet_history_column_broker_value(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return h->broker_value;
}

static cell_t wallet_history_column_investments(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return h->investments;
}

static cell_t wallet_history_column_total_value(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return h->total_value;
}

static cell_t wallet_history_column_total_gain(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    const double total_gain = h->total_value - h->investments;
    const double adjusted_total_gain = total_gain + h->gain;
    return math_ifzero(adjusted_total_gain, total_gain);
}

static cell_t wallet_history_column_assets(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return h->other_assets;
}

static double wallet_history_total_gain_p(const history_t* h)
{
    if (h->investments == 0)
        return NAN;
    const double total_gain = h->total_value - h->investments;
    const double adjusted_total_gain = total_gain + h->gain;
    return math_ifzero(adjusted_total_gain, total_gain) / h->investments * 100.0;
}

static cell_t wallet_history_column_total_gain_p(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return wallet_history_total_gain_p(h);
}

static double wallet_history_wealth(const history_t* h)
{
    return h->total_value + h->other_assets + h->gain;
}

static cell_t wallet_history_column_wealth(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return wallet_history_wealth(h);
}

static const history_t* wallet_history_get_previous(const history_t* h)
{
    if (!h->source)
        return nullptr;

    const size_t h_count = array_size(h->source->history);
    if (h_count < 2)
        return nullptr;

    const history_t* p = h + 1;
    const size_t pos = pointer_diff(p, &h->source->history[0]) / sizeof(history_t);

    if (pos >= h_count)
        return nullptr;

    if (h->source->history_period == WALLET_HISTORY_MONTLY)
    {
        // TODO:
    }
    else if (h->source->history_period == WALLET_HISTORY_YEARLY)
    {
        // TODO:
    }

    return p;
}

static cell_t wallet_history_column_change(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    const history_t* p = wallet_history_get_previous(h);
    if (!p)
        return NAN;

    return (h->total_value + math_ifnan(h->gain, 0)) - (p->total_value + math_ifnan(p->gain, 0));
}

static double wallet_history_change_p(const history_t* h)
{
    const history_t* p = wallet_history_get_previous(h);
    if (!p)
        return NAN;

    if (math_real_is_zero(p->total_value))
        return NAN;

    return (h->total_value - p->total_value) / p->total_value * 100.0;
}

static cell_t wallet_history_column_change_p(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    cell_t cv(wallet_history_change_p(h));
    
    if (cv.number <= 0)
    {
        cv.style.types |= COLUMN_COLOR_TEXT;
        cv.style.text_color = TEXT_BAD_COLOR;
    }
    else
    {
        cv.style.types |= COLUMN_COLOR_TEXT;
        cv.style.text_color = TEXT_GOOD_COLOR;
    }

    return cv;
}

static void wallet_history_edit_value(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    history_t* h = (history_t*)element;
    h->show_edit_ui = true;
}

static table_t* wallet_history_create_table(report_t* report)
{
    table_t* history_table = table_allocate(string_format_static_const("History###%s", string_table_decode(report->name)));
    history_table->flags |= ImGuiTableFlags_NoHostExtendY | ImGuiTableFlags_SizingFixedFit;
    history_table->selected = wallet_history_edit_value;

    table_add_column(history_table, STRING_CONST(ICON_MD_TODAY " Date      "), wallet_history_column_date, COLUMN_FORMAT_DATE, COLUMN_CUSTOM_DRAWING);
    table_add_column(history_table, STRING_CONST("      " ICON_MD_WALLET " Funds||" ICON_MD_WALLET " Funds"), wallet_history_column_funds, COLUMN_FORMAT_CURRENCY, COLUMN_ZERO_USE_DASH);
    table_add_column(history_table, STRING_CONST("     " ICON_MD_REAL_ESTATE_AGENT " Broker||" ICON_MD_REAL_ESTATE_AGENT " Brokerage Value"), wallet_history_column_broker_value, COLUMN_FORMAT_CURRENCY, COLUMN_HIDE_DEFAULT);
    table_add_column(history_table, STRING_CONST(" " ICON_MD_SAVINGS " Investments||" ICON_MD_SAVINGS " Investments"), wallet_history_column_investments, COLUMN_FORMAT_CURRENCY, COLUMN_ZERO_USE_DASH);
    table_add_column(history_table, STRING_CONST("      " ICON_MD_ACCOUNT_BALANCE_WALLET " Value||" ICON_MD_ACCOUNT_BALANCE_WALLET " Total Value"), wallet_history_column_total_value, COLUMN_FORMAT_CURRENCY, COLUMN_ZERO_USE_DASH);
    table_add_column(history_table, STRING_CONST("        " ICON_MD_PRICE_CHANGE " " ICON_MD_ATTACH_MONEY "||" ICON_MD_PRICE_CHANGE " Total Gain $"), wallet_history_column_total_gain, COLUMN_FORMAT_CURRENCY, COLUMN_ZERO_USE_DASH);
    table_add_column(history_table, STRING_CONST("   " ICON_MD_PRICE_CHANGE " %||" ICON_MD_PRICE_CHANGE " Total Gain % "), wallet_history_column_total_gain_p, COLUMN_FORMAT_PERCENTAGE, COLUMN_ZERO_USE_DASH);
    table_add_column(history_table, STRING_CONST("     " ICON_MD_COTTAGE " Assets||" ICON_MD_COTTAGE " Any other accounted assets"), wallet_history_column_assets, COLUMN_FORMAT_CURRENCY, COLUMN_HIDE_DEFAULT);
    table_add_column(history_table, STRING_CONST("     " ICON_MD_ACCOUNT_BALANCE " Wealth||" ICON_MD_ACCOUNT_BALANCE " Total wealth of all your earnings"), wallet_history_column_wealth, COLUMN_FORMAT_CURRENCY, COLUMN_ZERO_USE_DASH);
    table_add_column(history_table, STRING_CONST("         " ICON_MD_CHANGE_HISTORY " $||" ICON_MD_CHANGE_HISTORY " Change in $ since last time"), wallet_history_column_change, COLUMN_FORMAT_CURRENCY, COLUMN_ZERO_USE_DASH);
    table_add_column(history_table, STRING_CONST("    " ICON_MD_CHANGE_HISTORY " %||" ICON_MD_CHANGE_HISTORY " Change in % since last time"), wallet_history_column_change_p, COLUMN_FORMAT_PERCENTAGE, COLUMN_ZERO_USE_DASH);

    return history_table;
}

static void report_render_history_edit_dialog(report_t* report, history_t* h)
{
    ImGui::SetNextWindowSize(ImVec2(imgui_get_font_ui_scale(430), imgui_get_font_ui_scale(480)), ImGuiCond_Once);
    string_const_t popup_id = string_format_static(STRING_CONST("Edit History (%.*s)###Editor_History_10"), STRING_FORMAT(string_from_date(h->date)));
    if (!report_render_dialog_begin(popup_id, &h->show_edit_ui, ImGuiWindowFlags_NoResize))
        return;

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();

    ImGui::MoveCursor(10, 10);
    if (ImGui::BeginChild("##Content", ImVec2(0, 0)))
    {
        const float control_width = imgui_get_font_ui_scale(210.0f);

        bool updated = false;
        time_t now = time_now();
        tm tm_date = *localtime(h->date != 0 ? &h->date : &now);
        ImGui::SetNextItemWidth(control_width);
        if (ImGui::DateChooser("##Date", tm_date, "%Y-%m-%d", true))
        {
            h->date = mktime(&tm_date);
            updated = true;
        }

        ImGui::SetNextItemWidth(control_width);
        if (ImGui::InputDouble("Funds", &h->funds, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::SetNextItemWidth(control_width);
        if (ImGui::InputDouble("Investments", &h->investments, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::SetNextItemWidth(control_width);
        if (ImGui::InputDouble("Total Value", &h->total_value, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::SetNextItemWidth(control_width);
        if (ImGui::InputDouble("Total Gain", &h->gain, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::SetNextItemWidth(control_width);
        if (ImGui::InputDouble("Broker Value", &h->broker_value, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::SetNextItemWidth(control_width);
        if (ImGui::InputDouble("Assets Value", &h->other_assets, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::BeginGroup();

        ImGui::PushStyleColor(ImGuiCol_Button, BACKGROUND_CRITITAL_COLOR);
        if (ImGui::Button("Delete"))
            wallet_history_delete_entry(report, h);
        ImGui::PopStyleColor(1);

        if ((h - &h->source->history[0]) == 0)
        {
            ImGui::SameLine(0, control_width - 120.0f);
            if (ImGui::Button("Update"))
                wallet_history_update_entry(report, h->source, *h);

            ImGui::SameLine();
        }
        else
            ImGui::SameLine(0, control_width);

        if (ImGui::Button("Close"))
            h->show_edit_ui = false;
            
        ImGui::EndGroup();

        if (updated)
        {
            wallet_history_sort(h->source);
            report_summary_update(report);
        }
    } ImGui::EndChild();

    report_render_dialog_end();
}

static void wallet_history_min_max_date(wallet_t* wallet, time_t& min, time_t& max, double& space)
{
    min = time_now();
    max = 0;
    space = 1;
    time_t last = 0;
    for (const auto& h : generics::fixed_array(wallet->history))
    {
        if (last != 0)
            space = math_round(time_elapsed_days(h.date, last));
        last = h.date;
        max = ::max(max, h.date);
        min = ::min(min, h.date);
    }
}

static int wallet_history_format_currency(double value, char* buff, int size, void* user_data)
{
    double abs_value = math_abs(value);
    if (abs_value >= 1e12)
        return (int)string_format(buff, size, STRING_CONST("%.2gT $"), value / 1e12).length;
    if (abs_value >= 1e9)
        return (int)string_format(buff, size, STRING_CONST("%.2gB $"), value / 1e9).length;
    else if (abs_value >= 1e6)
        return (int)string_format(buff, size, STRING_CONST("%.3gM $"), value / 1e6).length;
    else if (abs_value >= 1e3)
        return (int)string_format(buff, size, STRING_CONST("%.3gK $"), value / 1e3).length;

    return (int)string_format(buff, size, STRING_CONST("%.2lf $"), value).length;
}

static int wallet_history_format_date(double value, char* buff, int size, void* user_data)
{
    time_t d = (time_t)value;
    if (d == 0 || d == -1)
        return 0;

    string_const_t date_str = string_from_date(d);
    return (int)string_copy(buff, size, STRING_ARGS(date_str)).length;
}

static int wallet_history_format_date_monthly(double value, char* buff, int size, void* user_data)
{
    time_t d = (time_t)value;
    if (d == 0 || d == -1)
        return 0;

    double day_space = *(double*)user_data;
    string_const_t date_str = string_from_date(d);
    if (day_space <= 5)
        return (int)string_copy(buff, size, date_str.str + 5, 5).length;

    return (int)string_copy(buff, size, date_str.str, min(date_str.length, (size_t)7)).length;
}

static void wallet_history_draw_graph(report_t* report, wallet_t* wallet)
{
    if (array_size(wallet->history) <= 1)
    {
        ImGui::TextUnformatted("Not enough entries to display graph");
        return;
    }

    double day_space;
    time_t min_d, max_d;
    wallet_history_min_max_date(wallet, min_d, max_d, day_space);

    const double day_range = time_elapsed_days(min_d, max_d);
    const ImVec2 graph_offset = ImVec2(-ImGui::GetStyle().CellPadding.x, -ImGui::GetStyle().CellPadding.y);
    if (!ImPlot::BeginPlot(string_format_static_const("History###%s", string_table_decode(report->name)), graph_offset, ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle))
        return;

    if (array_size(wallet->history_dates) != array_size(wallet->history))
    {
        array_deallocate(wallet->history_dates);
        for (const auto& h : generics::fixed_array(wallet->history))
            array_push(wallet->history_dates, (double)h.date);
        array_sort(wallet->history_dates, a < b);
        double* hd = wallet->history_dates;
        for (int i = 0, end = array_size(hd); i < end - 1; ++i)
        {
            if (time_elapsed_days((time_t)hd[i], (time_t)hd[i+1]) < day_space)
                hd[i] = NAN;
        }
    }

    const double bar_width = time_one_day() * day_space * 0.8;
    ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);

    ImPlot::SetupAxis(ImAxis_X1, "##Date", ImPlotAxisFlags_LockMax | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_X1, wallet_history_format_date_monthly, &day_space);
    ImPlot::SetupAxisTicks(ImAxis_X1, wallet->history_dates, (int)array_size(wallet->history_dates), nullptr, false);
    //ImPlot::SetupAxisTicks(ImAxis_X1, (double)min_d, (double)max_d, min(math_round(day_range * 2.0), 12), nullptr, false);
    ImPlot::SetupAxisLimits(ImAxis_X1, (double)min_d - time_one_day() * day_space, (double)max_d + time_one_day() * day_space, ImPlotCond_Once);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, (double)min_d - time_one_day() * day_space, (double)max_d + time_one_day() * day_space/*, ImPlotCond_Once*/);
    ImPlot::SetupAxisFormat(ImAxis_X1, wallet_history_format_date, nullptr);

    ImPlot::SetupAxis(ImAxis_Y1, "##Percentage", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3g %%");

    ImPlot::SetupAxis(ImAxis_Y2, "##Currency", ImPlotAxisFlags_LockMin | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Opposite);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y2, 0.0, INFINITY);
    ImPlot::SetupAxisFormat(ImAxis_Y2, wallet_history_format_currency, nullptr);

    plot_context_t c{ time_now(), array_size(wallet->history), 1, wallet->history };

    if (wallet->show_extra_charts)
    {
        ImPlot::SetAxis(ImAxis_Y2);
        ImPlot::PlotBarsG(ICON_MD_ACCOUNT_BALANCE "##Wealth_3", [](int idx, void* user_data)->ImPlotPoint
        {
            const plot_context_t* c = (plot_context_t*)user_data;
            const history_t& h = c->history[idx];
            const double x = (double)h.date;
            const double y = wallet_history_wealth(&h);
            return ImPlotPoint(x, y);
        }, &c, (int)c.range, bar_width, ImPlotBarsFlags_None);
    }

    ImPlot::SetAxis(ImAxis_Y2);
    ImPlot::PlotBarsG(ICON_MD_SAVINGS "##Investments", [](int idx, void* user_data)->ImPlotPoint
    {
        const plot_context_t* c = (plot_context_t*)user_data;
        const history_t& h = c->history[idx];
        const double x = (double)h.date;
        const double y = h.investments;
        return ImPlotPoint(x, y);
    }, &c, (int)c.range, bar_width, ImPlotBarsFlags_None);

    ImPlot::SetAxis(ImAxis_Y2);
    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 3);
    ImPlot::PlotLineG(ICON_MD_ACCOUNT_BALANCE_WALLET "##Value", [](int idx, void* user_data)->ImPlotPoint
    {
        const plot_context_t* c = (plot_context_t*)user_data;
        const history_t& h = c->history[idx];
        const double x = (double)h.date;
        const double y = h.total_value + math_ifnan(h.gain, 0);
        return ImPlotPoint(x, y);
    }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);

    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2);
    if (wallet->show_extra_charts)
    {
        ImPlot::SetAxis(ImAxis_Y2);
        ImPlot::HideNextItem(true, ImPlotCond_Once);
        ImPlot::PlotLineG(ICON_MD_REAL_ESTATE_AGENT "##Broker", [](int idx, void* user_data)->ImPlotPoint
        {
            const plot_context_t* c = (plot_context_t*)user_data;
            const history_t& h = c->history[idx];
            const double x = (double)h.date;
            const double y = h.broker_value;
            return ImPlotPoint(x, y);
        }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);
    
        ImPlot::SetAxis(ImAxis_Y2);
        ImPlot::PlotLineG(ICON_MD_WALLET "##Funds", [](int idx, void* user_data)->ImPlotPoint
        {
            const plot_context_t* c = (plot_context_t*)user_data;
            const history_t& h = c->history[idx];
            const double x = (double)h.date;
            const double y = h.funds;
            return ImPlotPoint(x, y);
        }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);

        ImPlot::SetAxis(ImAxis_Y1);
        ImPlot::PlotLineG(ICON_MD_PRICE_CHANGE " %##Gain %", [](int idx, void* user_data)->ImPlotPoint
        {
            const plot_context_t* c = (plot_context_t*)user_data;
            const history_t& h = c->history[idx];
            const double x = (double)h.date;
            const double y = wallet_history_total_gain_p(&h);
            return ImPlotPoint(x, y);
        }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);
    }

    ImPlot::SetAxis(ImAxis_Y1);
    ImPlot::PlotLineG(ICON_MD_CHANGE_HISTORY "##Change %", [](int idx, void* user_data)->ImPlotPoint
    {
        const plot_context_t* c = (plot_context_t*)user_data;
        const history_t& h = c->history[idx];
        const double x = (double)h.date;
        const double y = wallet_history_change_p(&h);
        return ImPlotPoint(x, y);
    }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);

    ImPlot::PopStyleVar(2);
    ImPlot::EndPlot();
}

static void wallet_history_draw_summary(report_handle_t report_id)
{
    report_t* report = report_get(report_id);
    if (report == nullptr || report->wallet == nullptr)
        return;

    wallet_t* wallet = report->wallet;
    if (wallet->history_table == nullptr)
        wallet->history_table = wallet_history_create_table(report);

    wallet->history_table->search_filter = string_to_const(SETTINGS.search_filter);
    table_render(wallet->history_table, wallet->history, array_size(wallet->history), sizeof(history_t), 0, ImGui::GetContentRegionAvail().y * 0.3f);

    wallet_history_draw_graph(report, wallet);

    for (int i = 0, end = array_size(wallet->history); i != end; ++i)
    {
        history_t* h = &report->wallet->history[i];
        if (h->show_edit_ui)
            report_render_history_edit_dialog(report, h);
    }
}

void wallet_history_draw()
{
    static report_handle_t selected_report_id = wallet_history_select_initial_report();

    wallet_history_draw_toolbar(selected_report_id);
    if (!uuid_is_null(selected_report_id))
    {
        wallet_history_draw_summary(selected_report_id);
    }
}

wallet_t* wallet_allocate(config_handle_t wallet_data)
{
    wallet_t* wallet = (wallet_t*)memory_allocate(0, sizeof(wallet_t), 0, MEMORY_PERSISTENT);
    *wallet = wallet_t{};
    wallet->history = nullptr;
    wallet->history_table = nullptr;
    wallet->funds = wallet_data["funds"].as_number(0.0);
    wallet->main_target = wallet_data["main_target"].as_number(0.25);
    wallet->show_extra_charts = wallet_data["show_extra_charts"].as_boolean();
    wallet->preferred_currency = string_clone_string(wallet_data["currency"].as_string(STRING_ARGS(string_const(SETTINGS.preferred_currency))));
    wallet->track_history = wallet_data["track_history"].as_boolean();

    for (const auto c : wallet_data["history"])
    {
        history_t h{};
        h.date = string_to_date(STRING_ARGS(c["date"].as_string()));
        h.funds = c["funds"].as_number();
        h.broker_value = c["broker"].as_number();
        h.investments = c["investments"].as_number();
        h.total_value = c["value"].as_number();
        h.gain = c["gain"].as_number(0);
        h.other_assets = c["assets"].as_number();
        h.source = wallet;

        wallet->history = array_push(wallet->history, h);
    }

    // Sort history from newer to older
    wallet_history_sort(wallet);

    return wallet;
}

void wallet_save(wallet_t* wallet, config_handle_t wallet_data)
{
    config_set(wallet_data, "funds", wallet->funds);
    config_set(wallet_data, "main_target", wallet->main_target);
    config_set(wallet_data, "show_extra_charts", wallet->show_extra_charts);
    config_set(wallet_data, "currency", string_to_const(wallet->preferred_currency));	
    config_set(wallet_data, "track_history", wallet->track_history);

    config_remove(wallet_data, wallet_data["history"]);
    config_handle_t history_data = config_set_array(wallet_data, STRING_CONST("history"));
    for (size_t i = 0; i < array_size(wallet->history); ++i)
    {
        const history_t& h = wallet->history[i];
        auto c = config_array_push(history_data, CONFIG_VALUE_OBJECT);
        config_set(c, "date", string_from_date(h.date));
        config_set(c, "funds", h.funds);
        config_set(c, "broker", h.broker_value);
        config_set(c, "investments", h.investments);
        config_set(c, "value", h.total_value);
        config_set(c, "gain", h.gain);
        config_set(c, "assets", h.other_assets);
    }
}

void wallet_deallocate(wallet_t* wallet)
{
    if (wallet->history_table)
    {
        table_deallocate(wallet->history_table);
        wallet->history_table = nullptr;
    }
    array_deallocate(wallet->history);
    array_deallocate(wallet->history_dates);
    string_deallocate(wallet->preferred_currency.str);
    memory_deallocate(wallet);
}
