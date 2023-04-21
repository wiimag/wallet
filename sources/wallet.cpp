/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */
 
#include "wallet.h"

#include "eod.h"
#include "report.h"
#include "settings.h"
#include "stock.h"

#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/table.h>
#include <framework/common.h>
#include <framework/math.h>
#include <framework/string.h>
#include <framework/array.h>

#include <foundation/hash.h>
#include <foundation/uuid.h>

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

FOUNDATION_STATIC void wallet_render_funds_text(float available_space, float padding, string_const_t fundsstr)
{
    const float tw = ImGui::CalcTextSize(STRING_RANGE(fundsstr)).x;
    const float iw = ImGui::GetItemRectSize().x;
    ImGui::MoveCursor(available_space - tw - iw + IM_SCALEF(10) - padding, 0, true);
    ImGui::TextUnformatted(fundsstr.str);
}

bool wallet_draw(wallet_t* wallet, float available_space)
{
    bool updated = false;

    float last_item_size = 0;
    const float control_padding = IM_SCALEF(14.0f) + (ImGui::GetScrollMaxY() > 0 ? IM_SCALEF(8) : 0);

    // Draw wallet history tracking check box
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TrTextUnformatted("History");
        last_item_size = ImGui::GetItemRectSize().x;
        ImGui::MoveCursor(available_space - last_item_size - IM_SCALEF(20.0f) - control_padding, 0, true);
        if (ImGui::Checkbox("##History", &wallet->track_history))
            updated |= true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tr("Track historical data for this report."));
    }
    
    // Draw wallet target percentage
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TrTextUnformatted("Target %");
        last_item_size = ImGui::GetItemRectSize().x;

        const float control_width = IM_SCALEF(60.0f);
        ImGui::MoveCursor(available_space - last_item_size - control_width - control_padding, 0, true);
        ImGui::SetNextItemWidth(control_width);
        double p100 = wallet->main_target * 100.0;
        if (ImGui::InputDouble("##Target%", &p100, 0, 0, "%.3g %%", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
        {
            wallet->main_target = p100 / 100.0;
            updated |= true;
        }
    }

    // Draw wallet preferred currency
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TrTextUnformatted("Currency");
        last_item_size = ImGui::GetItemRectSize().x;

        char currency[64];
        string_copy(STRING_BUFFER(currency), STRING_ARGS(wallet->preferred_currency));

        const float control_width = IM_SCALEF(60.0f);
        ImGui::MoveCursor(available_space - last_item_size - control_width - control_padding, 0, true);
        ImGui::SetNextItemWidth(control_width);
        if (ImGui::InputTextWithHint("##Currency", "i.e. USD", STRING_BUFFER(currency), ImGuiInputTextFlags_AutoSelectAll))
        {
            char* old = wallet->preferred_currency.str;
            wallet->preferred_currency = string_clone(currency, string_length(currency));
            string_deallocate(old);
            updated |= true;
        }
    }

    // Draw wallet funds (expands to all currencies)
    {
        string_const_t fundsstr = tr_format_static("{0,currency}", wallet_get_total_funds(wallet));
        ImGui::MoveCursor(-IM_SCALEF(4), 0);
        if (ImGui::TreeNode(tr("Funds")))
        {
            wallet_render_funds_text(available_space, control_padding, fundsstr);
            ImGui::SetWindowFontScale(0.9f);

            ImGui::Columns(2, "funds", true);
            foreach(f, wallet->funds)
            {
                char currency_code[8];
                string_copy(STRING_BUFFER(currency_code), STRING_ARGS(f->currency));

                ImGui::PushID(f);
                if (i > 0)
                    ImGui::NextColumn();

                if (ImGui::Button(ICON_MD_DELETE))
                {
                    array_erase(wallet->funds, i);
                    updated |= true;
                    ImGui::PopID();
                    break;
                }

                ImGui::SameLine();
                ImGui::ExpandNextItem();
                if (ImGui::InputTextWithHint("##Currency", "USD", STRING_BUFFER(currency_code), ImGuiInputTextFlags_AutoSelectAll))
                {
                    updated |= true;
                    string_deallocate(f->currency);
                    const size_t len = string_length(currency_code);
                    f->currency = string_clone(currency_code, len);
                }

                ImGui::NextColumn();
                ImGui::ExpandNextItem();
                if (ImGui::InputDouble("##Amount", &f->amount, 0, 0, "%.2lf $", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    updated |= true;

                ImGui::PopID();
            }

            static bool add_new = false;

            if (add_new)
            {
                ImGui::PushID("new fund");
                bool added = false;
                static double new_fund_amount = 0.0;
                static char new_fund_currency[8] = { 0 };

                if (!array_empty(wallet->funds))
                    ImGui::NextColumn();
                ImGui::ExpandNextItem();
                if (ImGui::InputTextWithHint("##Currency", "USD", STRING_BUFFER(new_fund_currency), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    added = true;

                ImGui::NextColumn();
                ImGui::ExpandNextItem();
                if (ImGui::InputDouble("##Amount", &new_fund_amount, 0, 0, "%.2lf $", ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                    added = true;

                const size_t currency_length = string_length(new_fund_currency);
                if (added && currency_length > 1)
                {
                    wallet_fund_t fund{};
                    fund.amount = new_fund_amount;
                    fund.currency = string_clone(new_fund_currency, currency_length);
                    array_push_memcpy(wallet->funds, &fund);

                    new_fund_amount = 0.0;
                    new_fund_currency[0] = 0;

                    updated |= true;
                    add_new = false;
                }
                ImGui::PopID();

                ImGui::ExpandNextItem();
                if (ImGui::SmallButton(tr("Cancel")))
                {
                    add_new = false;
                    new_fund_amount = 0.0;
                    new_fund_currency[0] = 0;
                }
            }
            else
            {
                ImGui::NextColumn();
                ImGui::ExpandNextItem();
                if (ImGui::SmallButton(tr("Add currency")))
                {
                    add_new = true;
                }
            }

            ImGui::Columns(1, "##closefunds", false);

            ImGui::SetWindowFontScale(1.0f);
            ImGui::TreePop();
        }
        else
        {
            wallet_render_funds_text(available_space, control_padding, fundsstr);
        }
    }

    return updated;
}

FOUNDATION_STATIC void wallet_history_sort(wallet_t* wallet)
{
    array_sort(wallet->history, [](const history_t& a, const history_t& b)
    {
        return b.date - a.date;
    });
}

FOUNDATION_STATIC bool wallet_history_update_entry(report_t* report, wallet_t* wallet, history_t& entry)
{
    if (!report_sync_titles(report))
    {
        log_warnf(HASH_REPORT, WARNING_TIMEOUT,
            STRING_CONST("Failed to sync %s report titles, cannot update wallet history. Please retry later..."), SYMBOL_CSTR(report->name));
        return false;
    }
    
    report->dirty = true;
    entry.date = time_now();
    entry.source = wallet;
    entry.show_edit_ui = true;
    entry.funds = wallet_get_total_funds(wallet);
    entry.investments = report->total_investment;
    entry.total_value = report->total_value;
    entry.gain = report->wallet->sell_total_gain;

    return true;
}

FOUNDATION_STATIC void wallet_history_add_new_entry(report_t* report, wallet_t* wallet)
{
    time_t today = time_now();

    // Check if we already have an entry for today
    foreach (h, wallet->history)
    {
        if (time_date_equal(today, h->date))
        {
            if (wallet_history_update_entry(report, wallet, *h))
            {
                h->show_edit_ui = true;
                return;
            }
        }
    }

    history_t new_entry{ today };
    if (wallet_history_update_entry(report, wallet, new_entry))
    {
        if (array_size(wallet->history) > 0)
        {
            const history_t& fh = wallet->history[0];
            new_entry.broker_value = fh.broker_value;
            new_entry.other_assets = fh.other_assets;
        }

        wallet->history = array_push(wallet->history, new_entry);
        wallet_history_sort(wallet);
    }
}

FOUNDATION_STATIC void wallet_history_delete_entry(report_t* report, history_t* h)
{
    size_t pos = h - &h->source->history[0];
    array_erase(h->source->history, pos);
    wallet_history_sort(report->wallet);
    report_summary_update(report);
    report->dirty = true;
}

FOUNDATION_STATIC void wallet_history_draw_toolbar(report_handle_t& selected_report_id)
{
    report_t* selected_report = report_get(selected_report_id);

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::SameLine(0, 8);
    ImGui::AlignTextToFramePadding();
    ImGui::TrTextUnformatted("Report");
    ImGui::SameLine(0, 16);
    ImGui::SetNextItemWidth(300.0f);
    const size_t report_count = ::report_count();
    if (ImGui::BeginCombo("##Report", !selected_report ? tr("None") : string_table_decode(selected_report->name), ImGuiComboFlags_None))
    {
        for (int i = 0; i < report_count; ++i)
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
        ImGui::BeginDisabled(!eod_availalble());
        if (ImGui::Button(tr("Add Entry")))
        {
            wallet_history_add_new_entry(selected_report, wallet);
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0, 100.0f);
        if (ImGui::Checkbox(tr("Show Extra Charts"), &wallet->show_extra_charts))
            ImPlot::SetNextAxesToFit();
    }
    else if (report_count == 0)
    {
        ImGui::SameLine();
        if (ImGui::Button(tr("Create New")))
        {
            SETTINGS.show_create_report_ui = true;
        }
    }
    ImGui::EndGroup();
}

FOUNDATION_STATIC report_handle_t wallet_history_select_initial_report()
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

FOUNDATION_STATIC cell_t wallet_history_column_date(table_element_ptr_t element, const column_t* column)
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

        const float button_width = IM_SCALEF(28.0f);
        if ((field_width + button_width) < width)
        {
            ImGui::MoveCursor(width - field_width - button_width, 0, true);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 0, 0, 0));
            if (ImGui::SmallButton(ICON_MD_FORMAT_LIST_BULLETED))
                h->show_edit_ui = true;
            ImGui::PopStyleColor(1);
        }
        ImGui::EndGroup();
    }
    return h->date;
}

FOUNDATION_STATIC cell_t wallet_history_column_funds(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return h->funds;
}

FOUNDATION_STATIC cell_t wallet_history_column_broker_value(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return h->broker_value;
}

FOUNDATION_STATIC cell_t wallet_history_column_investments(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return h->investments;
}

FOUNDATION_STATIC cell_t wallet_history_column_total_value(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return h->total_value;
}

FOUNDATION_STATIC cell_t wallet_history_column_total_gain(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    const double total_gain = h->total_value - h->investments;
    const double adjusted_total_gain = total_gain + h->gain;
    return math_ifzero(adjusted_total_gain, total_gain);
}

FOUNDATION_STATIC cell_t wallet_history_column_assets(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return h->other_assets;
}

FOUNDATION_STATIC double wallet_history_total_value_gain(const history_t* h)
{
    return (h->total_value - h->investments) + (h->gain + h->funds);
}

FOUNDATION_STATIC const history_t* wallet_history_get_previous(const history_t* h)
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

FOUNDATION_STATIC double wallet_history_total_gain_p(const history_t* h)
{
    if (h->investments == 0)
        return NAN;

    const double total_gain = wallet_history_total_value_gain(h);
    const double cash_flow = math_ifzero(h->funds, h->investments);
    const double diff = total_gain - cash_flow;
    const double adjusted_total_gain = total_gain + h->gain;
    return diff / cash_flow * 100.0;
}

FOUNDATION_STATIC cell_t wallet_history_column_total_gain_p(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return wallet_history_total_gain_p(h);
}

FOUNDATION_STATIC double wallet_history_wealth(const history_t* h)
{
    return wallet_history_total_value_gain(h) + h->other_assets;
}

FOUNDATION_STATIC cell_t wallet_history_column_wealth(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    return wallet_history_wealth(h);
}

FOUNDATION_STATIC cell_t wallet_history_column_change(table_element_ptr_t element, const column_t* column)
{
    history_t* h = (history_t*)element;
    const history_t* p = wallet_history_get_previous(h);
    if (!p)
        return NAN;

    return wallet_history_total_value_gain(h) - wallet_history_total_value_gain(p);
}

FOUNDATION_STATIC double wallet_history_change_p(const history_t* h)
{
    const history_t* p = wallet_history_get_previous(h);
    if (!p)
        return NAN;

    if (math_real_is_zero(p->total_value))
        return NAN;

    const double prev_value = wallet_history_total_value_gain(p);
    const double diff = wallet_history_total_value_gain(h) - prev_value;

    if (math_real_is_zero(prev_value) || !math_real_is_finite(prev_value))
        return 0.0;
    return diff / prev_value * 100.0;
}

FOUNDATION_STATIC cell_t wallet_history_column_change_p(table_element_ptr_t element, const column_t* column)
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

FOUNDATION_STATIC void wallet_history_edit_value(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    history_t* h = (history_t*)element;
    h->show_edit_ui = true;
}

FOUNDATION_STATIC table_t* wallet_history_create_table(report_t* report)
{
    table_t* history_table = table_allocate(
        string_format_static_const("History###%s", string_table_decode(report->name)),
        ImGuiTableFlags_NoHostExtendY | ImGuiTableFlags_SizingFixedFit | TABLE_LOCALIZATION_CONTENT);
    history_table->selected = wallet_history_edit_value;

    table_add_column(history_table, STRING_CONST(ICON_MD_TODAY " Date        "), wallet_history_column_date, COLUMN_FORMAT_DATE, COLUMN_CUSTOM_DRAWING);
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

FOUNDATION_STATIC void report_render_history_edit_dialog(report_t* report, history_t* h)
{
    ImGui::SetNextWindowSize(ImVec2(IM_SCALEF(255), IM_SCALEF(240)), ImGuiCond_FirstUseEver);
    string_const_t fmttr = RTEXT("Edit History (%.*s)###EH20");
    string_const_t popup_id = string_format_static(STRING_ARGS(fmttr), STRING_FORMAT(string_from_date(h->date)));
    if (!report_render_dialog_begin(popup_id, &h->show_edit_ui, ImGuiWindowFlags_AlwaysUseWindowPadding))
        return;

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();

    ImGui::MoveCursor(10, 10);
    ImGui::BeginGroup();
    {
        ImGui::Columns(2, "##EH20", true);
        const float control_width = IM_SCALEF(110.0f);

        bool updated = false;
        time_t now = time_now();
        tm tm_date = *localtime(h->date != 0 ? &h->date : &now);

        ImGui::TrTextWrapped("Date");
        ImGui::NextColumn();
        ImGui::ExpandNextItem(IM_SCALEF(20));
        if (ImGui::DateChooser("##Date", tm_date, "%Y-%m-%d", true))
        {
            h->date = mktime(&tm_date);
            updated = true;
        }

        ImGui::PushStyleColor(ImGuiCol_Button, BACKGROUND_CRITITAL_COLOR);
        ImGui::SameLine();
        if (ImGui::Button(ICON_MD_DELETE_FOREVER))
            wallet_history_delete_entry(report, h);
        ImGui::PopStyleColor(1);

        ImGui::NextColumn();
        ImGui::TrTextWrapped("Funds");
        ImGui::NextColumn();
        ImGui::ExpandNextItem();
        if (ImGui::InputDouble("##Funds", &h->funds, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::NextColumn();
        ImGui::TrTextWrapped("Investments");
        ImGui::NextColumn();
        ImGui::ExpandNextItem();
        if (ImGui::InputDouble("##Investments", &h->investments, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::NextColumn();
        ImGui::TrTextWrapped("Total Value");
        ImGui::NextColumn();
        ImGui::ExpandNextItem();
        if (ImGui::InputDouble("##Total Value", &h->total_value, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::NextColumn();
        ImGui::TrTextWrapped("Total Gain");
        ImGui::NextColumn();
        ImGui::ExpandNextItem();
        if (ImGui::InputDouble("##Total Gain", &h->gain, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::NextColumn();
        ImGui::TrTextWrapped("Broker Value");
        ImGui::NextColumn();
        ImGui::ExpandNextItem();
        if (ImGui::InputDouble("##Broker Value", &h->broker_value, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::NextColumn();
        ImGui::TrTextWrapped("Assets Value");
        ImGui::NextColumn();
        ImGui::ExpandNextItem();
        if (ImGui::InputDouble("##Assets Value", &h->other_assets, 0, 0, "%.2lf $"))
            updated = true;

        ImGui::Spacing();

        if (h->source && (h - &h->source->history[0]) == 0)
        {
            ImGui::NextColumn();
            if (ImGui::Button(tr("Update"), { IM_SCALEF(80), IM_SCALEF(20) }))
                wallet_history_update_entry(report, h->source, *h);

            ImGui::NextColumn();
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - IM_SCALEF(80));
            if (ImGui::Button(tr("Close"), { IM_SCALEF(80), IM_SCALEF(20) }))
                h->show_edit_ui = false;
        }
        else
        {
            ImGui::NextColumn();
            ImGui::NextColumn();
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - IM_SCALEF(80));
            if (ImGui::Button(tr("Close"), { IM_SCALEF(80), IM_SCALEF(20) }))
                h->show_edit_ui = false;
        }
            
        if (updated)
        {
            WAIT_CURSOR;
            wallet_history_sort(h->source);
            report_summary_update(report);
        }
    }
    ImGui::EndGroup();

    report_render_dialog_end();
}

FOUNDATION_STATIC void wallet_history_min_max_date(wallet_t* wallet, time_t& min, time_t& max, double& space)
{
    min = time_now();
    max = 0;
    space = 1;
    time_t last = 0;
    foreach (h, wallet->history)
    {
        if (last != 0)
            space = math_round(time_elapsed_days(h->date, last));
        last = h->date;
        max = ::max(max, h->date);
        min = ::min(min, h->date);
    }
}

FOUNDATION_STATIC int wallet_history_format_currency(double value, char* buff, int size, void* user_data)
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

FOUNDATION_STATIC int wallet_history_format_date(double value, char* buff, int size, void* user_data)
{
    time_t d = (time_t)value;
    if (d == 0 || d == -1)
        return 0;

    string_const_t date_str = string_from_date(d);
    return (int)string_copy(buff, size, STRING_ARGS(date_str)).length;
}

FOUNDATION_STATIC int wallet_history_format_date_monthly(double value, char* buff, int size, void* user_data)
{
    time_t d = (time_t)value;
    if (d == 0 || d == -1)
        return 0;

    double day_space = *(double*)user_data;
    string_const_t date_str = string_from_date(d);
    if (date_str.length == 0)
        return 0;

    if (day_space <= 5)
        return (int)string_copy(buff, size, date_str.str + 5, 5).length;

    return (int)string_copy(buff, size, date_str.str, min(date_str.length, (size_t)7)).length;
}

FOUNDATION_STATIC void wallet_history_draw_graph(report_t* report, wallet_t* wallet)
{
    const unsigned history_count = array_size(wallet->history);
    if (history_count <= 1)
    {
        ImGui::TrTextUnformatted("Not enough entries to display graph");
        return;
    }

    double day_space;
    time_t min_d, max_d;
    wallet_history_min_max_date(wallet, min_d, max_d, day_space);

    const double day_range = time_elapsed_days(min_d, max_d);
    const ImVec2 graph_offset = ImVec2(-ImGui::GetStyle().CellPadding.x, -ImGui::GetStyle().CellPadding.y);
    if (!ImPlot::BeginPlot(string_format_static_const("History###%s", string_table_decode(report->name)), graph_offset, ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle))
        return;

    if (array_size(wallet->history_dates) != history_count)
    {
        array_deallocate(wallet->history_dates);
        foreach (h, wallet->history)
            array_push(wallet->history_dates, (double)h->date);
        array_sort(wallet->history_dates);
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
    ImPlot::SetupAxisLimits(ImAxis_X1, (double)min_d - time_one_day() * day_space, (double)max_d + time_one_day() * day_space, ImPlotCond_Once);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, (double)min_d - time_one_day() * day_space, (double)max_d + time_one_day() * day_space/*, ImPlotCond_Once*/);
    ImPlot::SetupAxisFormat(ImAxis_X1, wallet_history_format_date, nullptr);

    ImPlot::SetupAxis(ImAxis_Y1, "##Percentage", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3g %%");

    ImPlot::SetupAxis(ImAxis_Y2, "##Currency", ImPlotAxisFlags_LockMin | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Opposite);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y2, 0.0, INFINITY);
    ImPlot::SetupAxisFormat(ImAxis_Y2, wallet_history_format_currency, nullptr);

    plot_context_t c{ time_now(), history_count, 1, wallet->history };

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
        const double y = wallet_history_total_value_gain(&h);
        return ImPlotPoint(x, y);
    }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);

    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2);
    if (wallet->show_extra_charts)
    {
        if (array_last(wallet->history)->broker_value > 0 )
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
        }
    
        if (array_last(wallet->history)->funds > 0)
        {
            ImPlot::SetAxis(ImAxis_Y2);
            ImPlot::PlotLineG(ICON_MD_WALLET "##Funds", [](int idx, void* user_data)->ImPlotPoint
            {
                const plot_context_t* c = (plot_context_t*)user_data;
                const history_t& h = c->history[idx];
                const double x = (double)h.date;
                const double y = h.funds;
                return ImPlotPoint(x, y);
            }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);
        }
    }

    ImPlot::SetAxis(ImAxis_Y1);
    ImPlot::PlotLineG(ICON_MD_PRICE_CHANGE " %##Gain %", [](int idx, void* user_data)->ImPlotPoint
    {
        const plot_context_t* c = (plot_context_t*)user_data;
        const history_t& h = c->history[idx];
        const double x = (double)h.date;
        const double y = wallet_history_total_gain_p(&h);
        return ImPlotPoint(x, y);
    }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);

    if (wallet->show_extra_charts)
    {
        ImPlot::SetAxis(ImAxis_Y1);
        ImPlot::HideNextItem(true, ImPlotCond_Once);
        ImPlot::PlotLineG(ICON_MD_CHANGE_HISTORY "##Change %", [](int idx, void* user_data)->ImPlotPoint
        {
            const plot_context_t* c = (plot_context_t*)user_data;
            const history_t& h = c->history[idx];
            const double x = (double)h.date;
            const double y = wallet_history_change_p(&h);
            return ImPlotPoint(x, y);
        }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);
    }

    ImPlot::PopStyleVar(2);
    ImPlot::EndPlot();
}

FOUNDATION_STATIC void wallet_history_draw_summary(report_handle_t report_id)
{
    report_t* report = report_get(report_id);
    if (report == nullptr || report->wallet == nullptr)
        return;

    wallet_t* wallet = report->wallet;
    if (wallet->history_table == nullptr)
        wallet->history_table = wallet_history_create_table(report);

    const unsigned history_count = array_size(wallet->history);
    wallet->history_table->search_filter = string_to_const(SETTINGS.search_filter);
    table_render(wallet->history_table, wallet->history, history_count, sizeof(history_t), 0, ImGui::GetContentRegionAvail().y * 0.3f);

    wallet_history_draw_graph(report, wallet);

    for (int i = 0, end = history_count; i != end; ++i)
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
    wallet->main_target = wallet_data["main_target"].as_number(0.25);
    wallet->show_extra_charts = wallet_data["show_extra_charts"].as_boolean();
    wallet->preferred_currency = string_clone_string(wallet_data["currency"].as_string(STRING_ARGS(string_const(SETTINGS.preferred_currency))));
    wallet->track_history = wallet_data["track_history"].as_boolean();

    // Read funds and support old format where fund was only a number.
    config_handle_t funds_cv = wallet_data["funds"];
    if (config_value_type(funds_cv) == CONFIG_VALUE_NUMBER)
    {
        wallet_fund_t fund{};
        fund.amount = funds_cv.as_number(0.0);
        fund.currency = string_clone(STRING_ARGS(wallet->preferred_currency));
        array_push_memcpy(wallet->funds, &fund);
    }
    else
    {
        for (auto f : funds_cv)
        {
            wallet_fund_t fund{};
            string_const_t currency = f["currency"].as_string();
            fund.currency = string_clone(STRING_ARGS(currency));
            fund.amount = f["amount"].as_number(0.0);
            array_push_memcpy(wallet->funds, &fund);
        }
    }

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
    config_set(wallet_data, "main_target", wallet->main_target);
    config_set(wallet_data, "show_extra_charts", wallet->show_extra_charts);
    config_set(wallet_data, "currency", string_to_const(wallet->preferred_currency));	
    config_set(wallet_data, "track_history", wallet->track_history);

    // Save funds
    auto funds_cv = config_set_array(wallet_data, STRING_CONST("funds"));
    config_clear(funds_cv);
    foreach(f, wallet->funds)
    {
        auto c = config_array_push(funds_cv, CONFIG_VALUE_OBJECT);
        config_set(c, "currency", string_to_const(f->currency));
        config_set(c, "amount", f->amount);
    }

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

    foreach(f, wallet->funds)
        string_deallocate(f->currency);
    array_deallocate(wallet->funds);
    string_deallocate(wallet->preferred_currency.str);
    memory_deallocate(wallet);
}

double wallet_get_total_funds(wallet_t* wallet)
{
    double total = 0.0;
    foreach(f, wallet->funds)
    {
        if (string_equal(STRING_ARGS(f->currency), STRING_ARGS(wallet->preferred_currency)))
        {
            total += f->amount;
        }
        else
        {
            total += f->amount * stock_exchange_rate(STRING_ARGS(f->currency), STRING_ARGS(wallet->preferred_currency));
        }
    }

    return total;
}
