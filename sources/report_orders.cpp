/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "report.h"

#include "title.h"
#include "stock.h"
#include "wallet.h"

#include <framework/imgui.h>
#include <framework/table.h>
#include <framework/math.h>
#include <framework/string.h>
#include <framework/array.h>

#include <algorithm>

struct report_title_order_t
{
    title_t* title;
    report_t* report;
    config_handle_t data;
    bool deleted{ false };

    double exchange_rate{ NAN };
    double close_price{ NAN };
    double split_price{ NAN };
    double adjusted_price{ NAN };

    double price_factor{ NAN };
    double split_factor{ NAN };
    double adjusted_split_factor{ NAN };
};

FOUNDATION_STATIC void report_trigger_update(report_t* report)
{
    report->dirty = true;
    report->fully_resolved = 0;
}

FOUNDATION_FORCEINLINE time_t report_order_get_date(const report_title_order_t* order)
{
    string_const_t date_str = order->data["date"].as_string();
    return string_to_date(STRING_ARGS(date_str));
}

FOUNDATION_STATIC double report_order_fetch_split_price(report_title_order_t* order)
{
    if (math_real_is_nan(order->split_price))
    {
        const time_t odate = report_order_get_date(order);
        const auto d = stock_get_split(order->title->code, order->title->code_length, odate);
        order->split_price = d.close;
    }

    return order->split_price;
}

FOUNDATION_STATIC double report_order_fetch_close_price(report_title_order_t* order)
{
    if (math_real_is_nan(order->close_price))
    {
        const time_t odate = report_order_get_date(order);
        const day_result_t* d = stock_get_EOD(order->title->stock, odate, true);
        if (d)
            order->close_price = d->close;
    }

    return order->close_price;
}

FOUNDATION_STATIC double report_order_fetch_adjusted_price(report_title_order_t* order)
{
    if (math_real_is_nan(order->adjusted_price))
    {
        const time_t odate = report_order_get_date(order);
        const day_result_t* d = stock_get_EOD(order->title->stock, odate, true);
        if (d)
            order->adjusted_price = d->adjusted_close;
    }

    return order->adjusted_price;
}

FOUNDATION_STATIC double report_order_price_factor(report_title_order_t* order)
{
    const double close_price = report_order_fetch_close_price(order);
    const double adjusted_price = report_order_fetch_adjusted_price(order);
    const double price_factor = close_price / adjusted_price;
    return price_factor;
}

FOUNDATION_STATIC double report_order_split_price_factor(report_title_order_t* order)
{
    const double close_price = report_order_fetch_close_price(order);
    const double split_price = report_order_fetch_split_price(order);
    const double price_factor = close_price / split_price;
    return price_factor;
}

FOUNDATION_STATIC void report_order_type_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    report_title_order_t* order = (report_title_order_t*)element;
    string_const_t tooltip = order->data["buy"].as_boolean() ? CTEXT("Buy") : CTEXT("Sell");
    ImGui::Text("%.*s", STRING_FORMAT(tooltip));
}

FOUNDATION_STATIC void report_order_total_value_adjusted_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    report_title_order_t* order = (report_title_order_t*)element;
    
    const bool buy_order = order->data["buy"].as_boolean();
    const double price = order->data["price"].as_number();
    const double quantity = order->data["qty"].as_number();

    const double current = order->title->stock->current.adjusted_close;

    if (math_real_is_nan(order->split_factor))
        order->split_factor = stock_get_split_factor(order->title->code, order->title->code_length, report_order_get_date(order));
    if (math_real_is_nan(order->price_factor))
        order->price_factor = stock_get_eod_price_factor(order->title->code, order->title->code_length, report_order_get_date(order));
    if (math_real_is_nan(order->adjusted_split_factor))
        order->adjusted_split_factor = stock_get_split_adjusted_factor(order->title->code, order->title->code_length, report_order_get_date(order));

    if (order->split_factor != 1.0)
        ImGui::TrText(" Split Factor: %.3lg", order->split_factor);

    ImGui::TrText(" %s Price: %.2lf $ (%d) ", buy_order ? "Bought" : "Sell", price, math_round(quantity));
    
    const int split_quantity = math_round(quantity / order->split_factor);
    if (order->split_factor != 1.0)
        ImGui::TrText(" Split Price: %.2lf $ (%d)", price * order->split_factor, split_quantity);

    if (order->price_factor != order->split_factor)
    {
        ImGui::TrText(" Adjust Factor: %.3lg", order->price_factor);
        ImGui::TrText(" Adjusted Price: %.3lf $ ", price * order->price_factor);
    }

    const double adjusted_price = price * order->adjusted_split_factor;
    if (buy_order && order->adjusted_split_factor != 1.0)
    {
        ImGui::TrText(" Split Adjusted Factor: %.3lg (%.3lg)", order->adjusted_split_factor, order->split_factor * order->price_factor);
        ImGui::TrText(" Split Adjusted Price: %.3lf $", adjusted_price);
    }

    ImGui::Spacing();
    ImGui::Separator();
    
    const double total_value = quantity * price;
    ImGui::TrText(" %s Value: %.2lf $ ", buy_order ? "Bought" : "Sell", total_value);

    const double adjusted_value = total_value * order->adjusted_split_factor;
    if (buy_order && order->adjusted_split_factor != 1.0)
        ImGui::TrText(" Adjusted Value (%d x %.2lf $): %.2lf $ ", split_quantity, adjusted_price, adjusted_value);

    const double current_price = order->title->stock->current.adjusted_close;
    if (!math_real_is_nan(current_price))
    {
        ImGui::Spacing();
        ImGui::Separator();

        const double worth_value = split_quantity * current_price;
        double gain = worth_value - adjusted_value;
        if (!buy_order)
            gain *= -1.0;
        if (gain < 0)
            ImGui::TrText(" Lost Value : %.2lf $ ", gain);
        else
            ImGui::TrText(" Gain Value : %.2lf $ ", gain);
        
        ImGui::Spacing();

        ImGui::TrText(" Worth Value (%d x %.2lf $): %.2lf $ ", split_quantity, current_price, worth_value);
    }
}

FOUNDATION_STATIC cell_t report_order_column_type(table_element_ptr_t element, const column_t* column)
{
    report_title_order_t* order = (report_title_order_t*)element;
    return order->data["buy"].as_boolean() ? CTEXT("") : CTEXT(ICON_MD_SELL);
}

FOUNDATION_STATIC cell_t report_order_column_date(table_element_ptr_t element, const column_t* column)
{
    tm tm_date;
    report_title_order_t* order = (report_title_order_t*)element;
    string_const_t date_str = order->data["date"].as_string();
    time_t odate = string_to_date(STRING_ARGS(date_str), &tm_date);

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::DateChooser("##Date", tm_date, "%Y-%m-%d", true))
        {
            odate = mktime(&tm_date);
            date_str = string_from_date(odate);
            config_set(order->data, STRING_CONST("date"), STRING_ARGS(date_str));
            title_refresh(order->title);
        }
    }

    return odate;
}

FOUNDATION_STATIC cell_t report_order_column_quantity(table_element_ptr_t element, const column_t* column)
{
    report_title_order_t* order = (report_title_order_t*)element;

    double quantity = order->data["qty"].as_number();

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::InputDouble("##Quantity", &quantity, 10.0f, 100.0f, "%.0lf", ImGuiInputTextFlags_None))
        {
            config_set(order->data, STRING_CONST("qty"), quantity);
            title_refresh(order->title);
            report_trigger_update(order->report);
        }
    }

    return quantity;
}

FOUNDATION_STATIC cell_t report_order_column_exchange_rate(table_element_ptr_t element, const column_t* column)
{
    report_title_order_t* order = (report_title_order_t*)element;

    if (math_real_is_nan(order->exchange_rate))
    {
        string_const_t currency = SYMBOL_CONST(order->title->stock->currency);
        order->exchange_rate = stock_exchange_rate(
            STRING_ARGS(currency),
            STRING_ARGS(order->report->wallet->preferred_currency),
            report_order_get_date(order));
    }
    return order->exchange_rate;
}

FOUNDATION_STATIC cell_t report_order_column_split_price(table_element_ptr_t element, const column_t* column)
{
    report_title_order_t* order = (report_title_order_t*)element;
    const double split_price = report_order_fetch_split_price(order);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
    {
        if (math_real_is_nan(order->split_factor))
            order->split_factor = stock_get_split_factor(order->title->code, order->title->code_length, report_order_get_date(order));
        return order->split_factor;
    }

    return split_price;
}

FOUNDATION_STATIC cell_t report_order_column_close_price(table_element_ptr_t element, const column_t* column)
{
    report_title_order_t* order = (report_title_order_t*)element;
    const double close_price = report_order_fetch_close_price(order);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
    {
        if (math_real_is_nan(order->price_factor))
            order->price_factor = stock_get_eod_price_factor(order->title->code, order->title->code_length, report_order_get_date(order));
        return order->price_factor;
    }

    return close_price;
}

FOUNDATION_STATIC cell_t report_order_column_adjusted_price(table_element_ptr_t element, const column_t* column)
{
    report_title_order_t* order = (report_title_order_t*)element;

    const double price = order->data["price"].as_number();
    if (math_real_is_nan(order->price_factor))
        order->price_factor = stock_get_eod_price_factor(order->title->code, order->title->code_length, report_order_get_date(order));
    const double adjusted_price = price * order->price_factor;

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
    {
        if (math_real_is_nan(order->adjusted_split_factor))
            order->adjusted_split_factor = stock_get_split_adjusted_factor(order->title->code, order->title->code_length, report_order_get_date(order));
        return order->adjusted_split_factor * price;
    }

    return adjusted_price;
}

FOUNDATION_STATIC cell_t report_order_column_price(table_element_ptr_t element, const column_t* column)
{
    report_title_order_t* order = (report_title_order_t*)element;

    double price = order->data["price"].as_number();

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        double price_scale = price / 10.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::InputDouble("##Price", &price, price < 0.5 ? 0.005 : 0.1, price < 0.5 ? 0.01 : 0.5,
            math_real_is_nan(price) ? "-" : (price < 0.5 ? "%.4lg $" : "%.2lf $"), ImGuiInputTextFlags_None))
        {
            config_set(order->data, STRING_CONST("price"), price);
            title_refresh(order->title);
            report_trigger_update(order->report);
        }
    }

    return price;
}

FOUNDATION_STATIC cell_t report_order_column_ask_price(table_element_ptr_t element, const column_t* column)
{
    report_title_order_t* order = (report_title_order_t*)element;

    double price = order->data["ask"].as_number();

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        double price_scale = price / 10.0f;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::InputDouble("##Ask", &price, price_scale, price_scale * 2.0f,
            math_real_is_nan(price) ? "-" : (price < 0.5 ? "%.3lf $" : "%.2lf $"), ImGuiInputTextFlags_None))
        {
            config_set(order->data, STRING_CONST("ask"), price);
            title_refresh(order->title);
        }
    }

    return price;
}

FOUNDATION_STATIC cell_t report_order_column_total_gain(table_element_ptr_t element, const column_t* column)
{
    report_title_order_t* order = (report_title_order_t*)element;

    const bool buy_order = order->data["buy"].as_boolean();
    const double price_factor = report_order_price_factor(order);
    const double current = order->title->stock->current.adjusted_close;
    const double price = order->data["price"].as_number() / price_factor;
    const double quantity = order->data["qty"].as_number() * price_factor;
    const double total_value = (price * quantity);
    const double actual_value = quantity * current;

    double gain = (actual_value - total_value) * (buy_order ? 1.0 : -1.0);

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        if (math_real_is_nan(gain))
        {
            table_cell_right_aligned_label(STRING_CONST("-"));
        }
        else
        {
            double gain_p = ((actual_value - total_value) / total_value * 100.0) * (buy_order ? 1.0 : -1.0);
            if (math_real_is_nan(gain_p))
            {
                table_cell_right_aligned_label(STRING_CONST("-"));
            }
            else
            {
                string_const_t label = string_format_static(STRING_CONST("%.2lf $ (%.2g %%)"), gain, gain_p);
                table_cell_right_aligned_label(STRING_ARGS(label));
            }
        }
    }

    return gain;
}

FOUNDATION_STATIC cell_t report_order_column_total_value(table_element_ptr_t element, const column_t* column)
{
    report_title_order_t* order = (report_title_order_t*)element;

    const double price = order->data["price"].as_number();
    const double quantity = order->data["qty"].as_number();
    
    const double total_value = quantity * price;
    return total_value;
}

FOUNDATION_STATIC cell_t report_order_column_actions(table_element_ptr_t element, const column_t* column)
{
    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        report_title_order_t* order = (report_title_order_t*)element;
        if (ImGui::SmallButton(ICON_MD_DELETE_FOREVER))
        {
            auto corders = order->title->data["orders"];
            if (config_remove(corders, order->data))
            {
                order->deleted = true;
                title_refresh(order->title);
                report_trigger_update(order->report);
            }
        }
    }

    return CTEXT("DELETE");
}

FOUNDATION_STATIC table_t* report_create_title_details_table(const bool title_is_sold, const bool show_ask_price)
{
    table_t* table = table_allocate("Orders##3", ImGuiTableFlags_SizingFixedFit | TABLE_LOCALIZATION_CONTENT);

    table_add_column(table, STRING_CONST("||Order Type"), report_order_column_type,
        COLUMN_FORMAT_TEXT, COLUMN_MIDDLE_ALIGN | COLUMN_HIDE_HEADER_TEXT | COLUMN_SORTABLE)
        .set_width(IM_SCALEF(20.0f))
        .set_tooltip_callback(report_order_type_tooltip);

    table_add_column(table, STRING_CONST(ICON_MD_TODAY " Date"), report_order_column_date,
        COLUMN_FORMAT_DATE, COLUMN_CUSTOM_DRAWING | COLUMN_SORTABLE | COLUMN_DEFAULT_SORT)
        .set_width(IM_SCALEF(110.0f));

    table_add_column(table, STRING_CONST("Quantity " ICON_MD_NUMBERS "||" ICON_MD_NUMBERS " Order Quantity"), report_order_column_quantity,
        COLUMN_FORMAT_NUMBER, COLUMN_CUSTOM_DRAWING | COLUMN_LEFT_ALIGN | COLUMN_SORTABLE)
        .set_width(IM_SCALEF(95.0f));

    table_add_column(table, STRING_CONST("Price " ICON_MD_MONETIZATION_ON "||" ICON_MD_MONETIZATION_ON " Order Price"), 
        report_order_column_price, COLUMN_FORMAT_CURRENCY, COLUMN_CUSTOM_DRAWING | COLUMN_LEFT_ALIGN | COLUMN_SORTABLE)
        .set_width(IM_SCALEF(120.0f));

    table_add_column(table, STRING_CONST("Close " ICON_MD_MONETIZATION_ON "||" ICON_MD_MONETIZATION_ON " Close Price"),
        report_order_column_close_price, COLUMN_FORMAT_CURRENCY, (!title_is_sold ? COLUMN_HIDE_DEFAULT : COLUMN_OPTIONS_NONE) | COLUMN_RIGHT_ALIGN | COLUMN_ZERO_USE_DASH)
        .set_width(IM_SCALEF(80.0f));

    table_add_column(table, STRING_CONST("Split " ICON_MD_MONETIZATION_ON "||" ICON_MD_MONETIZATION_ON " Split Price"),
        report_order_column_split_price, COLUMN_FORMAT_CURRENCY, (!title_is_sold ? COLUMN_HIDE_DEFAULT : COLUMN_OPTIONS_NONE) | COLUMN_RIGHT_ALIGN | COLUMN_ZERO_USE_DASH)
        .set_width(IM_SCALEF(80.0f));

    table_add_column(table, STRING_CONST("Rate " ICON_MD_CURRENCY_EXCHANGE "||" ICON_MD_CURRENCY_EXCHANGE " Exchange Rate"),
        report_order_column_exchange_rate, COLUMN_FORMAT_CURRENCY, COLUMN_HIDE_DEFAULT | COLUMN_RIGHT_ALIGN | COLUMN_ZERO_USE_DASH)
        .set_width(IM_SCALEF(80.0f));

    table_add_column(table, STRING_CONST("Adjusted " ICON_MD_MONETIZATION_ON "||" ICON_MD_MONETIZATION_ON " Adjusted Price"),
        report_order_column_adjusted_price, COLUMN_FORMAT_CURRENCY, COLUMN_RIGHT_ALIGN | COLUMN_ZERO_USE_DASH)
        .set_width(IM_SCALEF(95.0f));

    table_add_column(table, STRING_CONST("Ask " ICON_MD_MONETIZATION_ON "||" ICON_MD_MONETIZATION_ON " Ask Price"), report_order_column_ask_price,
        COLUMN_FORMAT_CURRENCY, (!show_ask_price ? COLUMN_HIDE_DEFAULT : COLUMN_OPTIONS_NONE) | COLUMN_CUSTOM_DRAWING | COLUMN_LEFT_ALIGN | COLUMN_SORTABLE)
        .set_width(IM_SCALEF(130.0f));

    table_add_column(table, STRING_CONST("   Value " ICON_MD_ACCOUNT_BALANCE_WALLET "||" ICON_MD_ACCOUNT_BALANCE_WALLET " Total Value (as of today)"),
        report_order_column_total_value, COLUMN_FORMAT_CURRENCY, COLUMN_ZERO_USE_DASH | COLUMN_SORTABLE)
        .set_tooltip_callback(report_order_total_value_adjusted_tooltip)
        .set_width(IM_SCALEF(100.0f));

    table_add_column(table, STRING_CONST("           Gain " ICON_MD_PRICE_CHANGE "||" ICON_MD_PRICE_CHANGE " Total Gain"),
        report_order_column_total_gain, COLUMN_FORMAT_CURRENCY, COLUMN_HIDE_DEFAULT | COLUMN_RIGHT_ALIGN | COLUMN_CUSTOM_DRAWING);

    table_add_column(table, STRING_CONST(ICON_MD_SMART_BUTTON "||" ICON_MD_SMART_BUTTON " Actions"), report_order_column_actions,
        COLUMN_FORMAT_TEXT, COLUMN_CUSTOM_DRAWING | COLUMN_STRETCH | COLUMN_LEFT_ALIGN);

    return table;
}

FOUNDATION_STATIC string_const_t report_title_order_window_id(const title_t* title)
{
    const stock_t* s = title->stock;
    if (s == nullptr || math_real_is_nan(s->current.close))
        return string_format_static(STRING_CONST(ICON_MD_FORMAT_LIST_BULLETED " Orders %.*s###Orders_%.*s_4"),
            (int)title->code_length, title->code, (int)title->code_length, title->code);

    if (title_sold(title))
        return string_format_static(STRING_CONST(ICON_MD_FORMAT_LIST_BULLETED " Orders %.*s [SOLD] (%.2lf $)###Orders_%.*s_4"),
            (int)title->code_length, title->code, title->stock->current.close, (int)title->code_length, title->code);
    
    return string_format_static(STRING_CONST(ICON_MD_FORMAT_LIST_BULLETED " Orders %.*s (%.2lf $)###Orders_%.*s_4"),
        (int)title->code_length, title->code, title->stock->current.close, (int)title->code_length, title->code);
}

void report_render_title_details(report_t* report, title_t* title)
{
    const bool title_is_sold = title_sold(title);
    const bool show_ask_price = title->average_ask_price > 0 || (title->average_quantity == 0 && title->sell_total_quantity == 0);

    const float window_base_size = 1600.0f;
    ImGui::SetNextWindowSize(ImVec2(show_ask_price ? window_base_size + 200.0f : window_base_size, 650.0f), ImGuiCond_Once);

    string_const_t id = report_title_order_window_id(title);
    if (!report_render_dialog_begin(id, &title->show_details_ui, ImGuiWindowFlags_NoCollapse))
        return;

    static table_t* table = nullptr;
    static report_title_order_t* orders = nullptr;
    if (ImGui::IsWindowAppearing())
    {
        if (orders)
            array_deallocate(orders);

        if (table)
            table_deallocate(table);

        table = report_create_title_details_table(title_is_sold, show_ask_price);

        for (auto corder : title->data["orders"])
        {
            report_title_order_t o{ title, report, corder };
            array_push_memcpy(orders, &o);
        }

        array_sort(orders, ARRAY_GREATER_BY(data["date"].as_number()));
    }

    ImGui::PushStyleCompact();
    table_render(table, orders, array_size(orders), sizeof(report_title_order_t), 0.0f, 0.0f);
    foreach (order, orders)
    {
        if (order->deleted)
        {
            size_t index = order - &orders[0];
            array_erase_memcpy_safe(orders, index);
        }
    }
    ImGui::PopStyleCompact();

    if (report_render_dialog_end())
    {
        array_deallocate(orders);
        orders = nullptr;

        table_deallocate(table);
        table = nullptr;
    }
}

void report_render_buy_lot_dialog(report_t* report, title_t* title)
{
    string_const_t fmttr = tr(STRING_CONST(ICON_MD_LOCAL_OFFER " Buy %.*s##13"), true);
    string_const_t title_buy_popup_id = string_format_static(STRING_ARGS(fmttr), title->code_length, title->code);
    if (!report_render_dialog_begin(title_buy_popup_id, &title->show_buy_ui, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        return;

    static double quantity = 100.0f;
    static double price = 0.0f;
    static double price_scale = 1.0f;
    static tm tm_date;
    static bool reset_date = true;

    if (ImGui::IsWindowAppearing() || math_real_is_nan(price))
    {
        quantity = max(math_round(title->average_quantity * 0.1), 100);
        price = title->stock->current.adjusted_close;
        price_scale = price / 10.0f;
        reset_date = true;

        ImGui::SetDateToday(&tm_date);
    }

    const ImVec2 content_size = ImVec2(IM_SCALEF(560.0f), IM_SCALEF(105.0f));
    ImGui::MoveCursor(IM_SCALEF(2.0f), IM_SCALEF(10.0f));
    if (ImGui::BeginChild("##Content", content_size, false))
    {
        const float control_width = (content_size.x - IM_SCALEF(40.0f)) / 3;
        ImGui::Columns(3);

        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::TrText("Quantity"); ImGui::NextColumn();
        ImGui::TrText("Date"); ImGui::NextColumn();
        ImGui::TrText("Price"); ImGui::NextColumn();

        ImGui::Columns(3);

        ImGui::SetNextItemWidth(control_width);
        ImGui::InputDouble("##Quantity", &quantity, 10.0f, 100.0f, "%.0lf", ImGuiInputTextFlags_None);
        if (quantity < 0)
            quantity = 0;

        ImGui::NextColumn();
        ImGui::SetNextItemWidth(control_width);
        if (ImGui::DateChooser("##Date", tm_date, "%Y-%m-%d", true, &reset_date))
        {
            const day_result_t* e = stock_get_EOD(title->stock, mktime(&tm_date), true);
            if (e)
                price = math_ifnan(e->adjusted_close, price);
        }

        ImGui::NextColumn();
        ImGui::SetNextItemWidth(control_width);
        ImGui::InputDouble("##Price", &price, price_scale, price_scale * 2.0f, math_real_is_nan(price) ? "-" : (price < 0.5 ? "%.3lf $" : "%.2lf $"), ImGuiInputTextFlags_None);
        if (price < 0)
            price = title->stock->current.adjusted_close;

        ImGui::NextColumn();

        ImGui::Columns(3);
        ImGui::MoveCursor(0, IM_SCALEF(10.0f));

        double orig_buy_value = quantity * price;
        double buy_value = orig_buy_value;
        ImGui::SetNextItemWidth(control_width);
        if (ImGui::InputDouble("##BuyValue", &buy_value, price * 10.0, price * 100.0,
            math_real_is_nan(price) ? "-" : (buy_value < 0.5 ? "%.3lf $" : "%.2lf $"),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank) || buy_value != orig_buy_value)
        {
            if (!math_real_is_nan(price))
                quantity = math_round(buy_value / price);
        }

        ImGui::NextColumn();
        ImGui::NextColumn();

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - IM_SCALEF(152.0f));
        if (ImGui::Button(tr("Cancel"), { IM_SCALEF(70.0f) , IM_SCALEF(24.0f) }))
            title->show_buy_ui = false;
        ImGui::SameLine();
        if (ImGui::Button(tr("Apply"), { IM_SCALEF(75.0f), IM_SCALEF(24.0f) }))
        {
            config_handle_t orders = config_set_array(title->data, STRING_CONST("orders"));
            config_handle_t new_order = config_array_push(orders, CONFIG_VALUE_OBJECT);

            string_const_t date_str = string_from_date(tm_date);
            config_set(new_order, STRING_CONST("date"), STRING_ARGS(date_str));
            config_set(new_order, STRING_CONST("buy"), true);
            config_set(new_order, STRING_CONST("qty"), quantity);
            config_set(new_order, STRING_CONST("price"), price);
            title->show_buy_ui = false;

            title_refresh(title);
            report_trigger_update(report);
        }
    } ImGui::EndChild();

    report_render_dialog_end(&title->show_buy_ui);
}

void report_render_sell_lot_dialog(report_t* report, title_t* title)
{
    string_const_t fmttr = tr(STRING_CONST(ICON_MD_SELL " Sell %.*s##7"), true);
    string_const_t title_popup_id = string_format_static(STRING_ARGS(fmttr), title->code_length, title->code);
    if (!report_render_dialog_begin(title_popup_id, &title->show_sell_ui, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
        return;

    static double quantity = 100.0f;
    static double price = 0.0f;
    static double price_scale = 1.0f;
    static tm tm_date;
    static bool reset_date = true;

    if (ImGui::IsWindowAppearing() || math_real_is_nan(price))
    {
        quantity = title->average_quantity;
        price = title->stock->current.adjusted_close;
        price_scale = price / 10.0f;
        reset_date = true;

        ImGui::SetDateToday(&tm_date);
    }

    ImGui::MoveCursor(2, 10);
    const ImVec2 content_size = ImVec2(IM_SCALEF(560.0f), IM_SCALEF(105.0f));
    if (ImGui::BeginChild("##Content", content_size, false))
    {
        const float control_width = IM_SCALEF(165.0f);
        ImGui::Columns(3);

        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::TrText("Quantity"); ImGui::NextColumn();
        ImGui::TrText("Date"); ImGui::NextColumn();
        ImGui::TrText("Price"); ImGui::NextColumn();

        ImGui::Columns(3);

        ImGui::SetNextItemWidth(control_width);
        ImGui::InputDouble("##Quantity", &quantity, 10.0f, 100.0f, "%.0lf", ImGuiInputTextFlags_None);
        if (quantity < 0)
            quantity = 0;

        ImGui::NextColumn();
        ImGui::SetNextItemWidth(control_width);
        ImGui::DateChooser("##Date", tm_date, "%Y-%m-%d", true, &reset_date);

        ImGui::NextColumn();
        ImGui::SetNextItemWidth(control_width);
        ImGui::InputDouble("##Price", &price, price_scale, price_scale * 2.0f, math_real_is_nan(price) ? "-" : (price < 0.5 ? "%.3lf $" : "%.2lf $"), ImGuiInputTextFlags_None);
        if (price < 0)
            price = title->stock->current.adjusted_close;

        ImGui::NextColumn();

        ImGui::Columns(1);
        ImGui::MoveCursor(20, 15);

        ImGui::TrText("Sell Value: %.2lf $", quantity * price);

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - IM_SCALEF(165));

        ImGui::MoveCursor(0, -5);
        if (ImGui::Button(tr("Cancel"), { IM_SCALEF(70.0f) , IM_SCALEF(24.0f) }))
            title->show_sell_ui = false;
        ImGui::SameLine();
        ImGui::MoveCursor(0, -5);
        if (ImGui::Button(tr("Apply"), { IM_SCALEF(75.0f) , IM_SCALEF(24.0f) }))
        {
            config_handle_t orders = config_set_array(title->data, STRING_CONST("orders"));
            config_handle_t new_order = config_array_push(orders, CONFIG_VALUE_OBJECT);

            string_const_t date_str = string_from_date(tm_date);
            config_set(new_order, STRING_CONST("date"), STRING_ARGS(date_str));
            config_set(new_order, STRING_CONST("sell"), true);
            config_set(new_order, STRING_CONST("qty"), quantity);
            config_set(new_order, STRING_CONST("price"), price);
            title->show_sell_ui = false;

            title_refresh(title);
            report_trigger_update(report);
        }
    } ImGui::EndChild();

    report_render_dialog_end();
}
