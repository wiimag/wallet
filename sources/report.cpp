/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */

#include "report.h"

#include "stock.h"
#include "title.h"
#include "settings.h"
#include "symbols.h"
#include "eod.h"
#include "logo.h"
#include "realtime.h"
#include "wallet.h"
#include "pattern.h"
#include "timeline.h"
#include "news.h"
#include "financials.h"
#include "alerts.h"
#include "events.h"

#include <framework/app.h>
#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/session.h>
#include <framework/table.h>
#include <framework/module.h>
#include <framework/tabs.h>
#include <framework/dispatcher.h>
#include <framework/math.h>
#include <framework/expr.h>
#include <framework/database.h>
#include <framework/console.h>
#include <framework/string.h>
#include <framework/array.h>
#include <framework/localization.h>
#include <framework/system.h>
#include <framework/window.h>

#include <foundation/uuid.h>
#include <foundation/path.h>

#include <stdexcept>

#define E32(FN, P1, P2, P3) L2(FN(P1, P2, P3))

static const ImU32 BACKGROUND_WATCH_COLOR = ImColor::HSV(120 / 360.0f, 0.30f, 0.61f);

typedef enum report_column_formula_enum_t : unsigned int {
    REPORT_FORMULA_NONE = 0,
    REPORT_FORMULA_CURRENCY,
    REPORT_FORMULA_PRICE,
    REPORT_FORMULA_DAY_CHANGE,
    REPORT_FORMULA_YESTERDAY_CHANGE,
    REPORT_FORMULA_BUY_QUANTITY,
    REPORT_FORMULA_TOTAL_GAIN,
    REPORT_FORMULA_TOTAL_GAIN_P,
    REPORT_FORMULA_TOTAL_FUNDAMENTAL,
    REPORT_FORMULA_EXCHANGE_RATE,
    REPORT_FORMULA_TYPE,
    REPORT_FORMULA_PS,
} report_column_formula_t;

static report_t* _reports = nullptr;
static bool* _last_show_ui_ptr = nullptr;
static string_const_t REPORTS_DIR_NAME = CTEXT("reports");

// 
// # PRIVATE
//

FOUNDATION_STATIC title_t* report_title_find(report_t* report, string_const_t code)
{
    foreach (pt, report->titles)
    {
        title_t* t = *pt;
        if (string_equal(t->code, t->code_length, STRING_ARGS(code)))
            return t;
    }

    return nullptr;
}

FOUNDATION_STATIC title_t* report_title_add(report_t* report, string_const_t code)
{
    title_t* title = report_title_find(report, code);
    if (title)
        return title;

    auto titles_data = config_set_object(report->data, STRING_CONST("titles"));
    auto title_data = config_set_object(titles_data, STRING_ARGS(code));
    config_set_array(title_data, STRING_CONST("orders"));
    
    title = title_allocate(report->wallet, title_data);
    report->titles = array_insert(report->titles, report->active_titles, title);
    report->active_titles++;

    return title;
}

FOUNDATION_STATIC void report_title_remove(report_handle_t report_handle, const title_t* title)
{
    report_t* report = report_get(report_handle);
    auto ctitles = report->data["titles"];
    if (config_remove(ctitles, title->code, title->code_length))
    {
        for (int i = 0, end = array_size(report->titles); i != end; ++i)
        {
            if (title == report->titles[i])
            {
                title_deallocate(report->titles[i]);
                array_erase(report->titles, i);
                report->active_titles--;
                break;
            }
        }

        report->dirty = true;
        report_summary_update(report);
    }
}

FOUNDATION_STATIC void report_filter_out_titles(report_t* report)
{
    report->active_titles = array_size(report->titles);

    if (report->show_sold_title && report->show_no_transaction_title)
        return;

    for (unsigned i = 0; i < report->active_titles; ++i)
    {
        const title_t* title = report->titles[i];

        const bool discard_if_sold = !report->show_sold_title && title_sold(title);
        const bool discard_if_no_transaction = !report->show_no_transaction_title && title->buy_total_count == 0;
        
        if (discard_if_sold || discard_if_no_transaction)
        {
            // Hide titles that are sold or those with no transactions
            array_swap<title_t*>(report->titles, i, report->active_titles - 1);
            i--;
            report->active_titles--;
        }
    }
}

FOUNDATION_STATIC bool report_table_update(table_element_ptr_t element)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return false;

    return title_update(title, 0.0);
}

FOUNDATION_STATIC bool report_table_search(table_element_ptr_const_t element, const char* search_filter, size_t search_filter_length)
{
    if (search_filter_length == 0)
        return true;

    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return false;

    if (string_contains_nocase(title->code, title->code_length, SETTINGS.search_filter, search_filter_length))
        return true;

    const stock_t* s = title->stock;
    if (s)
    {
        string_const_t name = string_table_decode_const(s->name);
        if (string_contains_nocase(STRING_ARGS(name), SETTINGS.search_filter, search_filter_length))
            return true;

        string_const_t type = string_table_decode_const(s->type);
        if (string_contains_nocase(STRING_ARGS(type), SETTINGS.search_filter, search_filter_length))
            return true;
    }

    return false;
}

FOUNDATION_STATIC bool report_table_row_begin(table_t* table, table_row_t* row, table_element_ptr_t element)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr)
        return false;

    double real_time_elapsed_seconds = 0;

    const float decrease_timelapse = 60.0f * 25.0f;
    const float increase_timelapse = 60.0f * 25.0f;

    row->background_color = 0;

    if (title_is_index(t))
    {
        return (row->background_color = BACKGROUND_INDX_COLOR);
        return (row->background_color = BACKGROUND_INDX_COLOR);
    }
    else if (title_sold(t))
    {
        return (row->background_color = BACKGROUND_SOLD_COLOR);
    }
    else if (t->buy_total_count == 0 && t->sell_total_count == 0)
    {
        return (row->background_color = BACKGROUND_WATCH_COLOR);
    }
    else if (title_has_increased(t, nullptr, increase_timelapse, &real_time_elapsed_seconds))
    {
        ImVec4 hsv = ImGui::ColorConvertU32ToFloat4(BACKGROUND_GOOD_COLOR);
        hsv.w = (increase_timelapse - (float)real_time_elapsed_seconds) / increase_timelapse;
        if (hsv.w > 0)
        {
            row->background_color = ImGui::ColorConvertFloat4ToU32(hsv);
            return true;
        }
    }
    else if (title_has_decreased(t, nullptr, decrease_timelapse, &real_time_elapsed_seconds))
    {
        ImVec4 hsv = ImGui::ColorConvertU32ToFloat4(BACKGROUND_BAD_COLOR);
        hsv.w = (decrease_timelapse - (float)real_time_elapsed_seconds) / decrease_timelapse;
        if (hsv.w > 0)
        {
            row->background_color = ImGui::ColorConvertFloat4ToU32(hsv);
            return true;
        }
    }

    return false;
}

FOUNDATION_STATIC bool report_table_row_end(table_t* table, table_row_t* row, table_element_ptr_t element)
{
    if (element == nullptr)
        return false;

    return false;
}

FOUNDATION_STATIC void report_table_setup(report_handle_t report_handle, table_t* table)
{
    table->flags |= ImGuiTableFlags_ScrollX
        | TABLE_SUMMARY
        | TABLE_HIGHLIGHT_HOVERED_ROW
        | TABLE_LOCALIZATION_CONTENT;

    table->update = report_table_update;
    table->search = report_table_search;
    table->context_menu = L3(report_table_context_menu(report_handle, _1, _2, _3));
    table->row_begin = report_table_row_begin;
    table->row_end = report_table_row_end;
}

FOUNDATION_STATIC bool report_column_show_alternate_data()
{
    return ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
}

FOUNDATION_STATIC table_cell_t report_column_get_buy_price(table_element_ptr_t element, const table_column_t* column)
{
    const title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;

    const bool show_alternate_buy_price = report_column_show_alternate_data();

    table_cell_t cell(!show_alternate_buy_price ? t->average_price : t->average_price_rated);
    if (t->average_price < t->stock->current.price)
    {
        cell.style.types |= COLUMN_COLOR_TEXT;
        cell.style.text_color = TEXT_GOOD_COLOR;
    }
    
    return cell;
}

FOUNDATION_STATIC table_cell_t report_column_day_gain(table_element_ptr_t element, const table_column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr)
        return nullptr;

    const stock_t* s = t->stock;
    if (s == nullptr)
        return nullptr;

    if (title_is_index(t))
    {
        if (column->flags & COLUMN_COMPUTE_SUMMARY)
            return 0;
        return s->current.change;
    }

    return title_get_day_change(t, s);
}

FOUNDATION_STATIC table_cell_t report_column_average_days_held(table_element_ptr_t element, const table_column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr)
        return nullptr;

    return title_average_days_held(t);
}

FOUNDATION_STATIC table_cell_t report_column_get_ask_price(table_element_ptr_t element, const table_column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;

    // If all titles are sold, return the sold average price.
    if (title_sold(t))
        return t->sell_total_price / t->sell_total_quantity;

    if (t->average_quantity == 0)
        return nullptr;

    if (t->average_ask_price > 0)
    {
        table_cell_t ask_price_cell(t->average_ask_price);
        ask_price_cell.style.types |= COLUMN_COLOR_TEXT;
        ask_price_cell.style.text_color = TEXT_WARN_COLOR;
        return ask_price_cell;
    }

    const double ask_price = t->ask_price.fetch();
    const double avg = math_ifzero(t->average_price, t->stock->current.adjusted_close);
    const double c_avg = t->stock->current.adjusted_close;
    const double average_fg = (t->average_price + t->stock->current.adjusted_close) / 2.0;
    const double days_held = title_average_days_held(t);
    const double if_gain_price = average_fg * (1.0 + t->wallet->profit_ask - (days_held - t->wallet->average_days) / 20.0 / 100.0);

    if (!math_real_is_nan(ask_price) && ask_price < t->average_price)
    {
        table_cell_t ask_price_cell(ask_price);
        ask_price_cell.style.types |= COLUMN_COLOR_TEXT;

        const double p = (ask_price - if_gain_price) / if_gain_price * 100.0;
        if (ask_price < average_fg || (p < 0.0 && math_abs(p) > (t->wallet->target_ask * 100.0)))
            ask_price_cell.style.text_color = TEXT_WARN2_COLOR;
        else
            ask_price_cell.style.text_color = TEXT_WARN_COLOR;
        return ask_price_cell;
    }

    return ask_price;
}

FOUNDATION_STATIC table_cell_t report_column_earning_actual(table_element_ptr_t element, const table_column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;
    return t->stock->earning_trend_actual.fetch();
}

FOUNDATION_STATIC table_cell_t report_column_earning_estimate(table_element_ptr_t element, const table_column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;
    return t->stock->earning_trend_estimate.fetch();
}

FOUNDATION_STATIC table_cell_t report_column_earning_difference(table_element_ptr_t element, const table_column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;
    return t->stock->earning_trend_difference.fetch();
}

FOUNDATION_STATIC table_cell_t report_column_earning_percent(table_element_ptr_t element, const table_column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;
    return (double)math_round(t->stock->earning_trend_percent.fetch());
}

FOUNDATION_STATIC table_cell_t report_column_get_value(table_element_ptr_t element, const table_column_t* column, report_column_formula_t formula)
{
    title_t* t = *(title_t**)element;

    if ((column->flags & COLUMN_COMPUTE_SUMMARY) && title_is_index(t))
        return nullptr;

    if (t == nullptr)
        return nullptr;

    switch (formula)
    {
    case REPORT_FORMULA_PS:
        return t->ps.fetch();

    case REPORT_FORMULA_EXCHANGE_RATE:
        return t->average_exchange_rate;
    }

    // Stock accessors
    const stock_t* stock_data = t->stock;
    if (stock_data)
    {
        switch (formula)
        {
        case REPORT_FORMULA_CURRENCY:	return stock_data->currency;
        case REPORT_FORMULA_TYPE:		return stock_data->type;
        case REPORT_FORMULA_PRICE:
            if (title_is_index(t) && t->average_quantity == 0)
                return NAN;
            return stock_data->current.adjusted_close;
        case REPORT_FORMULA_DAY_CHANGE:
            if (t->average_quantity == 0 && column->flags & COLUMN_COMPUTE_SUMMARY)
                return 0;
            return stock_data->current.change_p;

        case REPORT_FORMULA_TOTAL_GAIN:
            return title_get_total_gain(t);

        case REPORT_FORMULA_TOTAL_GAIN_P:
            return title_get_total_gain_p(t);

        case REPORT_FORMULA_YESTERDAY_CHANGE:   
            return title_get_yesterday_change(t, stock_data);

        default:
            FOUNDATION_ASSERT_FAILFORMAT("Cannot get %.*s value for %.*s (%u)", STRING_FORMAT(column->get_name()), (int)t->code_length, t->code, formula);
            break;
        }
    }

    return table_cell_t();
}

FOUNDATION_STATIC void report_column_price_alert_menu(const title_t* title)
{
    const double current_price = title_current_price(title);
    if (!math_real_is_finite(current_price) || !ImGui::TrBeginMenu("Price Alerts"))
        return;

    ImGui::MoveCursor(8.0f, 4.0f);
    ImGui::BeginGroup();

    if (ImGui::TrMenuItem("Add ask price alert"))
    {
        const double ask_price = title_get_ask_price(title);
        alerts_add_price_increase(title->code, title->code_length, ask_price);
    }

    if (ImGui::TrMenuItem("Add bought price alert"))
    {
        const double ask_price = title_get_bought_price(title);
        alerts_add_price_increase(title->code, title->code_length, ask_price);
    }

    pattern_handle_t pattern = pattern_load(title->code, title->code_length);
    const double bid_low = pattern_get_bid_price_low(pattern);
    if (bid_low < current_price)
    {
        const char* big_low_label = tr_format("Add bid price alert (Low: {0, currency})", bid_low);
        if (ImGui::MenuItem(big_low_label))
        {
            alerts_add_price_decrease(title->code, title->code_length, bid_low);
        }
    }

    const double bid_high = pattern_get_bid_price_high(pattern);
    if (bid_high > bid_low && bid_high > current_price)
    {
        const char* big_high_label = tr_format("Add bid price alert (High: {0, currency})", bid_high);
        if (ImGui::MenuItem(big_high_label))
        {
            alerts_add_price_increase(title->code, title->code_length, bid_high);
        }
    }

    ImGui::EndGroup();
    ImGui::EndMenu();
}

FOUNDATION_STATIC void report_column_contextual_menu(report_handle_t report_handle, table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    #if BUILD_APPLICATION
    const title_t* title = *(const title_t**)element;

    ImGui::MoveCursor(8.0f, 4.0f);
    ImGui::BeginGroup();
    {
        ImGui::BeginDisabled(true);
        ImGui::MenuItem(title->code);
        ImGui::Separator();
        ImGui::EndDisabled();

        if (ImGui::MenuItem(tr("Buy")))
            ((title_t*)title)->show_buy_ui = true;

        if (ImGui::MenuItem(tr("Sell")))
            ((title_t*)title)->show_sell_ui = true;

        if (ImGui::MenuItem(tr("Details")))
            ((title_t*)title)->show_details_ui = true;

        ImGui::Separator();

        pattern_contextual_menu(title->code, title->code_length);

        report_column_price_alert_menu(title);

        ImGui::Separator();

        if (ImGui::TrMenuItem("Read News"))
            news_open_window(title->code, title->code_length);

        if (ImGui::TrMenuItem("Show Financials"))
            financials_open_window(title->code, title->code_length);

        #if BUILD_DEVELOPMENT
        if (ImGui::TrMenuItem("Browse Fundamentals"))
            system_execute_command(eod_build_url("fundamentals", title->code, FORMAT_JSON).str);
        #endif

        ImGui::Separator();

        if (ImGui::MenuItem(tr("Remove")))
            report_title_remove(report_handle, title);
    }
    ImGui::EndGroup();
    #endif
}

FOUNDATION_STATIC void report_column_title_header_render(report_handle_t report_handle, table_t* table, const table_column_t* column, int column_index)
{
    string_const_t title = column->get_name();
    ImGui::Text("%.*s", STRING_FORMAT(title));

    const float button_width = IM_SCALEF(14.0);
    const float available_space = ImGui::GetColumnWidth();
    const float column_right_offset = (ImGui::TableGetColumnFlags(column_index) & ImGuiTableColumnFlags_IsSorted) ? IM_SCALEF(10) : 0.0f;
    ImGui::SameLine();

    const float horizontal_scroll_offset = ImGui::GetScrollX();
    ImGui::SetCursorPosX(available_space - button_width - column_right_offset + horizontal_scroll_offset);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    if (ImGui::SmallButton(ICON_MD_ADD))
    {
        report_t* report = report_get(report_handle);
        FOUNDATION_ASSERT(report);

        report->show_add_title_ui = true;
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(tr("Add title"));
}

FOUNDATION_STATIC table_cell_t report_column_draw_title(table_element_ptr_t element, const table_column_t* column)
{
    title_t* title = *(title_t**)element;

    #if BUILD_APPLICATION
    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        const char* formatted_code = title->code;

        bool can_show_banner = SETTINGS.show_logo_banners && !ImGui::IsKeyDown(ImGuiKey_B);
        if (title_has_increased(title, nullptr, 30.0 * 60.0))
        {
            formatted_code = string_format_static_const("%s %s", title->code, ICON_MD_TRENDING_UP);
            can_show_banner = false;
        }
        else if (title_has_decreased(title, nullptr, 30.0 * 60.0))
        {
            formatted_code = string_format_static_const("%s %s", title->code, ICON_MD_TRENDING_DOWN);
            can_show_banner = false;
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImRect& cell_rect = table_current_cell_rect();
        const ImVec2& space = cell_rect.GetSize();
        const ImVec2& text_size = ImGui::CalcTextSize(formatted_code);
        const float button_width = text_size.y;
        const bool has_orders = title_has_transactions(title);

        ImGui::PushStyleCompact();
        int logo_banner_width = 0, logo_banner_height = 0, logo_banner_channels = 0;
        ImU32 logo_banner_color = 0xFFFFFFFF, fill_color = 0xFFFFFFFF;
        if (logo_has_banner(title->code, title->code_length, 
                logo_banner_width, logo_banner_height, logo_banner_channels, logo_banner_color, fill_color) &&
                can_show_banner && 
                space.x > IM_SCALEF(100))
        {
            const float ratio = logo_banner_height / text_size.y;
            logo_banner_height = text_size.y;
            logo_banner_width /= ratio;

            if (logo_banner_channels == 4)
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImColor bg_logo_banner_color = fill_color;
                dl->AddRectFilled(cell_rect.Min, cell_rect.Max, fill_color);

                ImGui::PushStyleColor(ImGuiCol_Text, (ImU32)imgui_color_text_for_background(fill_color));
            }
            else if (logo_banner_channels == 3)
            {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(cell_rect.Min, cell_rect.Max, fill_color);

                const ImU32 best_text_color = imgui_color_text_for_background(fill_color);
                ImGui::PushStyleColor(ImGuiCol_Text, best_text_color);
            }

            const float max_width = ImGui::GetContentRegionAvail().x - button_width - IM_SCALEF(2.0f);
            const float max_height = cell_rect.GetHeight();
            const float max_scale = logo_banner_width > max_width ? max_width / logo_banner_width : 
                (logo_banner_height > cell_rect.GetHeight() ? cell_rect.GetHeight() / logo_banner_height : 1.0f);
            ImVec2 logo_size(max_width, max_height);
            if (logo_banner_channels == 3)
                ImGui::MoveCursor(-style.FramePadding.x, -style.FramePadding.y - 1.0f, false);
            if (!logo_render_banner(title->code, title->code_length, logo_size, false, false))
            {
                ImGui::TextUnformatted(formatted_code);
            }
            else
            {
                if (logo_banner_channels == 3)
                    ImGui::MoveCursor(style.FramePadding.x, style.FramePadding.y + 1.0f, false);
                ImGui::Dummy(ImVec2(logo_banner_width * max_scale, logo_banner_height * max_scale));
            }

            if (ImGui::IsItemHovered())
            {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    pattern_open(title->code, title->code_length);
                else
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, 0xFFEEEEEE);
                    ImGui::SetTooltip("%.*s", (int)title->code_length, title->code);
                    ImGui::PopStyleColor();
                }
            }
            
            const float space_left = ImGui::GetContentRegionAvail().x - (logo_banner_width * max_scale) - (style.FramePadding.x * 2.0f);
            if (button_width < space_left + IM_SCALEF(10))
            {
                ImGui::MoveCursor(space_left - button_width - style.FramePadding.x / 2.0f, IM_SCALEF(2.0f), true);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 0, 0, 0));
                if (ImGui::SmallButton(ICON_MD_FORMAT_LIST_BULLETED))
                {
                    if (has_orders)
                        title->show_details_ui = true;
                    else
                        title->show_buy_ui = true;
                }
                ImGui::PopStyleColor(1);
            }

            ImGui::PopStyleColor();
        }
        else
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (logo_banner_width > 0)
            {
                dl->AddRectFilled(cell_rect.Min, cell_rect.Max, logo_banner_color); // ABGR
                const ImU32 best_text_color = imgui_color_text_for_background(logo_banner_color);
                ImGui::PushStyleColor(ImGuiCol_Text, best_text_color);
            }

            const float code_width = text_size.x + (style.ItemSpacing.x * 2.0f);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(formatted_code);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                pattern_open(title->code, title->code_length);

            float logo_size = button_width;
            float space_left = ImGui::GetContentRegionAvail().x - code_width;
            ImGui::MoveCursor(space_left - button_width - logo_size + IM_SCALEF(7), IM_SCALEF(2), true);
            ImVec2 logo_size_v = ImVec2(logo_size, logo_size);
            if (ImGui::GetCursorPos().x < code_width || !logo_render_icon(title->code, title->code_length, logo_size_v, true, true))
                ImGui::Dummy(ImVec2(logo_size, logo_size));
            else
                ImGui::Dummy(ImVec2(logo_size, logo_size));

            space_left = ImGui::GetContentRegionAvail().x - code_width;
            if (button_width < space_left + IM_SCALEF(25))
            {
                ImGui::MoveCursor(-IM_SCALEF(7), IM_SCALEF(1.0f), true);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 0, 0, 0));
                if (ImGui::SmallButton(ICON_MD_FORMAT_LIST_BULLETED))
                {
                    if (has_orders)
                        title->show_details_ui = true;
                    else
                        title->show_buy_ui = true;
                }
                ImGui::PopStyleColor(1);
            }

            if (logo_banner_width > 0)
                ImGui::PopStyleColor();
        }
        
        ImGui::PopStyleCompact();
    }
    #endif

    return title->code;
}

FOUNDATION_STATIC table_cell_t report_column_get_change_value(table_element_ptr_t element, const table_column_t* column, int rel_days)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return nullptr;
    if ((column->flags & COLUMN_COMPUTE_SUMMARY) && title_is_index(title))
        return nullptr;

    const stock_t* stock_data = title->stock;
    if (!stock_data)
        return DNAN;

    return title_get_range_change_p(title, stock_data, rel_days, rel_days < -365);
}

FOUNDATION_STATIC bool report_column_is_numeric(column_format_t format)
{
    return format == COLUMN_FORMAT_CURRENCY || format == COLUMN_FORMAT_NUMBER || format == COLUMN_FORMAT_PERCENTAGE;
}

FOUNDATION_STATIC table_cell_t report_column_get_dividends_yield(table_element_ptr_t element, const table_column_t* column)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return DNAN;

    const stock_t* s = title->stock;
    if (s == nullptr)
        return DNAN;
        
    return s->dividends_yield.fetch() * 100.0f;
}

FOUNDATION_STATIC table_cell_t report_column_get_fundamental_value(table_element_ptr_t element, const table_column_t* column, const char* filter_name, size_t filter_name_length)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return DNAN;

    const config_handle_t& filter_value = title_get_fundamental_config_value(title, filter_name, filter_name_length);
    if (!filter_value)
        return DNAN;

    column_format_t format = column->format;
    if (report_column_is_numeric(format))
    {
        double fn = config_value_as_number(filter_value);
        column_flags_t flags = column->flags;
        if (fn == 0 && (flags & COLUMN_ZERO_USE_DASH))
            return DNAN;

        if (format == COLUMN_FORMAT_PERCENTAGE)
            fn *= 100.0;
        return fn;
    }

    return config_value_as_string(filter_value);
}

FOUNDATION_STATIC table_cell_t report_column_get_total_investment(table_element_ptr_t element, const table_column_t* column)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return nullptr;

    if (report_column_show_alternate_data())
        return title->buy_total_price_rated;

    return title_get_total_investment(title);
}

FOUNDATION_STATIC table_cell_t report_column_get_total_value(table_element_ptr_t element, const table_column_t* column)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return nullptr;

    return title_get_total_value(title);
}

FOUNDATION_STATIC table_cell_t report_column_get_name(table_element_ptr_t element, const table_column_t* column)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return nullptr;
    return title->stock->name;
}

FOUNDATION_STATIC table_cell_t report_column_buy_quantity(table_element_ptr_t element, const table_column_t* column)
{
    const title_t* t = *(title_t**)element;
    if (t == nullptr)
        return nullptr;

    return (double)math_round(t->average_quantity);
}

FOUNDATION_STATIC table_cell_t report_column_get_date(table_element_ptr_t element, const table_column_t* column)
{
    const title_t* t = *(title_t**)element;
    if (t == nullptr)
        return nullptr;
    return t->date_average;
}

FOUNDATION_STATIC void report_title_pattern_open(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;
    pattern_open(title->code, title->code_length);
}

FOUNDATION_STATIC void report_title_open_details_view(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;
    title->show_details_ui = true;
}

FOUNDATION_STATIC void report_title_day_change_tooltip(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;

    tm tm_now;
    int time_lapse_hours = 24;
    const time_t now = time_now();
    if (time_to_local(now, &tm_now))
    {
        if (tm_now.tm_hour >= 11 && tm_now.tm_hour < 17)
            time_lapse_hours = 8;
    }

    const stock_t* s = title->stock;
    if (s)
    {
        string_const_t name = SYMBOL_CONST(s->name);
        const tick_t tick_updated = s->current.date * 1000;
        const tick_t system_time = time_system();
        const char* time_elapsed_unit = "minute";
        double elapsed_time_updated = time_diff(tick_updated, system_time) / 1000.0 / 60.0;
        if (elapsed_time_updated > 1440)
        {
            time_elapsed_unit = "day";
            elapsed_time_updated /= 1440;
        }
        else if (elapsed_time_updated > 60)
        {
            time_elapsed_unit = "hour";
            elapsed_time_updated /= 60;
        }
        char time_buffer[64];
        string_t last_update = localization_string_from_time(STRING_BUFFER(time_buffer), tick_updated);
        ImGui::AlignTextToFramePadding();
        ImGui::Text(" Updated %.0lf %s(s) ago (%.*s) \n %.*s [%.*s] -> %.2lf $ (%.3lg %%) ",
            elapsed_time_updated, time_elapsed_unit, STRING_FORMAT(last_update),
            STRING_FORMAT(name), (int)title->code_length, title->code, 
            s->current.close, math_ifnan(s->current.change_p, 0));
        ImGui::Spacing();
    }
   
    realtime_render_graph(title->code, title->code_length, time_add_hours(time_now(), -time_lapse_hours), 1300.0f, 600.0f);
}

FOUNDATION_STATIC void report_title_live_price_tooltip(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;
    eod_fetch("real-time", title->code, FORMAT_JSON_CACHE, "s", title->code, [title](const json_object_t& json)
    {
        const stock_t* s = title->stock;
        char time_buffer[64];
        string_t time_str = localization_string_from_time(STRING_BUFFER(time_buffer), (tick_t)(json["timestamp"].as_number() * 1000.0));
        
        if (s == nullptr || time_str.length == 0)
        {
            return ImGui::TrText(" %s (%s) \n Data not available \n",
                title->code, string_table_decode(title->stock->name));
        }

        day_result_t d{};
        const double old_price = s->current.adjusted_close;
        stock_index_t stock_index = ::stock_index(title->code, title->code_length);
        stock_read_real_time_results(stock_index, json, d);

        if (math_real_is_nan(d.price))
        {
            ImGui::TrTextUnformatted("No real-time data available");
        }
        else
        {

            ImGui::TextColored(ImColor(TOOLTIP_TEXT_COLOR), tr(" %s (%s) \n %.*s \n"
                "\tPrice %.2lf $\n"
                "\tOpen: %.2lf $\n"
                "\tChange: %.2lf $ (%.3g %%)\n"
                "\tYesterday: %.2lf $ (%.3g %%)\n"
                "\tLow %.2lf $\n"
                "\tHigh %.2lf $ (%.3g %%)\n"
                "\tDMA (50d) %.2lf $ (%.3g %%)\n"
                "\tDMA (200d) %.2lf $ (%.3g %%)\n"
                "\tVolume %.6g (%.*s)"), title->code, string_table_decode(s->name), STRING_FORMAT(time_str),
                d.close,
                d.open,
                d.close - d.open, (d.close - d.open) / d.open * 100.0,
                d.previous_close, (d.close - d.previous_close) / d.previous_close * 100.0,
                d.low,
                d.high, (d.high - d.low) / d.close * 100.0,
                math_ifnan(s->dma_50, 0), math_ifnan(s->dma_50 / d.close * 100.0, 0),
                math_ifnan(s->dma_200, 0), math_ifnan(s->dma_200 / s->high_52 * 100.0, 0),
                d.volume, STRING_FORMAT(string_from_currency(d.volume * d.change, "9 999 999 999 $")));

            if (d.close != old_price)
                title_refresh(title);
        }
    }, 60ULL);

    time_t since = title_last_transaction_date(title);
    if (since == 0)
        since = time_add_days(time_now(), -9);
    
    realtime_render_graph(title->code, title->code_length, since, max(ImGui::GetContentRegionAvail().x, 900.0f), 300.0f);
}

FOUNDATION_STATIC void report_title_price_alerts_formatter(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell, cell_style_t& style)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;

    const double current_price = title->stock->current.adjusted_close;
    if (title_is_index(title))
        return;

    if (title->average_price > 0 && current_price >= title->ask_price.fetch())
    {
        style.types |= COLUMN_COLOR_BACKGROUND | COLUMN_COLOR_TEXT;
        style.background_color = ImColor::HSV(130 / 360.0f, 0.94f, 0.94f); // hsv(176, 94%, 94%)
        style.text_color = imgui_color_text_for_background(style.background_color);
    }
    else if (title->average_price > 0 && current_price >= (title->average_price * (1.0 + title->wallet->profit_ask)))
    {
        style.types |= COLUMN_COLOR_BACKGROUND | COLUMN_COLOR_TEXT;
        style.background_color = ImColor::HSV(130 / 360.0f, 0.94f, 0.94f); // hsv(176, 94%, 94%)
        style.text_color = imgui_color_text_for_background(style.background_color);
    }
    else if (current_price > math_ifnan(title->stock->dma_200, INFINITY))
    {
        style.types |= COLUMN_COLOR_BACKGROUND | COLUMN_COLOR_TEXT;
        style.background_color = ImColor::HSV(55 / 360.0f, 0.69f, 0.87f, 0.8f); // hsv(55, 69%, 97%)
        style.text_color = imgui_color_text_for_background(style.background_color);
    }
    else if (current_price > math_ifnan(title->stock->dma_50, INFINITY))
    {
        style.types |= COLUMN_COLOR_BACKGROUND | COLUMN_COLOR_TEXT;
        style.background_color = ImColor::HSV(30 / 360.0f, 0.69f, 0.87f, 0.8f); // hsv(30, 69%, 97%)
        style.text_color = imgui_color_text_for_background(style.background_color);
    }
    else if (title->average_price > 0 && current_price > title->average_price)
    {
        style.types |= COLUMN_COLOR_TEXT;
        style.text_color = ImColor::HSV(117 / 360.0f, 0.68f, 0.90f); // hsv(117, 68%, 90%)
    }
}

FOUNDATION_STATIC void report_title_total_gain_alerts_formatter(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell, cell_style_t& style)
{
    const title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;

    if (!math_real_is_nan(title->wallet->enhanced_earnings) && title->average_quantity > 0 && cell->number > title->wallet->enhanced_earnings)
    {
        style.types |= COLUMN_COLOR_BACKGROUND | COLUMN_COLOR_TEXT;
        style.background_color = ImColor::HSV(130 / 360.0f, 0.94f, 0.974f, (float)(cell->number / title->wallet->enhanced_earnings / (title->wallet->target_ask * 100.0)));;
        style.text_color = imgui_color_text_for_background(style.background_color);
    }
}

FOUNDATION_STATIC void report_title_total_gain_p_alerts_formatter(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell, cell_style_t& style)
{
    const title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;

    const double current_gain_p = title_get_total_gain_p(title);
    if (current_gain_p >= title->wallet->profit_ask * 100.0)
    {
        style.types |= COLUMN_COLOR_BACKGROUND | COLUMN_COLOR_TEXT;
        //style.text_color = ImColor::HSV(40 / 360.0f, 0.94f, 0.14f);
        if (title->elapsed_days < 30 && title->average_quantity > 0)
            style.background_color = ImColor::HSV(130 / 360.0f, 0.94f, 0.74f, 0.8f);
        else if (title->average_quantity > 0)
            style.background_color = ImColor::HSV(130 / 360.0f, 0.94f, 0.94f, 0.8f); // hsv(176, 94%, 94%)
        else
            style.background_color = ImColor::HSV(130 / 360.0f, 0.94f, 0.94f, 0.5f); // hsv(176, 94%, 94%)
        style.text_color = imgui_color_text_for_background(style.background_color);
    }
    else
    {
        if (current_gain_p < 3.0)
        {
            style.types |= COLUMN_COLOR_BACKGROUND | COLUMN_COLOR_TEXT;
            style.background_color = ImColor::HSV(350 / 360.0f, 0.94f, 0.88f,
                (float)(math_abs(current_gain_p) / (title->wallet->main_target * 200.0))); // hsv(349, 94%, 88%)
            style.text_color = imgui_color_text_for_background(style.background_color);
        }
        else
        {
            style.types |= COLUMN_COLOR_BACKGROUND | COLUMN_COLOR_TEXT;
            style.background_color = ImColor::HSV(186 / 360.0f, 0.26f, 0.92f,
                (float)(current_gain_p / (title->wallet->target_ask * 100.0))); // hsv(186, 26 %, 92 %)

            if (current_gain_p >= title->wallet->target_ask * 60.0)
            {
                style.types |= COLUMN_COLOR_TEXT;
                style.text_color = ImColor::HSV(130 / 360.0f, 0.94f, 0.04f);
            }
            else
                style.text_color = imgui_color_text_for_background(style.background_color);

        }
    }
}

FOUNDATION_STATIC void report_title_gain_total_tooltip(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    const title_t* t = *(const title_t**)element;
    if (t == nullptr)
        return;

    const double total_value = title_get_total_value(t);
    ImGui::TrText(" Total Investment %12s ", string_from_currency(title_get_total_investment(t)).str);
    ImGui::TrText(" Total Value      %12s ", string_from_currency(total_value).str);

    if (t->total_dividends > 0)
        ImGui::TrText(" Total Dividends  %12s ", string_from_currency(t->total_dividends).str);

    if (t->average_exchange_rate != 1.0 && t->average_quantity > 0)
    {
        const double exchange_diff = t->today_exchange_rate.fetch() - t->average_exchange_rate;
        ImGui::TrText(" Exchange Gain    %12s ", string_from_currency(exchange_diff * total_value).str);
    }

    if (title_sold(t))
    {
        ImGui::Separator();
        const double current_gain = title_sell_gain_if_kept(t);
        ImGui::TrText("     If Kept Gain %12s ", string_from_currency(current_gain).str);
    }
}

FOUNDATION_STATIC void report_title_days_held_tooltip(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    const title_t* t = *(const title_t**)element;
    if (t == nullptr)
        return;

    const time_t last_date = title_last_transaction_date(t);
    const time_t first_date = title_first_transaction_date(t);

    if (last_date == 0 || first_date == 0)
        return;

    ImGui::TextUnformatted(tr_format("  Last transaction: {0:date} ({0:since}) ", last_date));
    ImGui::TextUnformatted(tr_format(" First transaction: {0:date} ({0:since}) ", first_date));
    ImGui::TrTextUnformatted("\n The days held field reflects the average number of days held \n for each transaction weighted by the quantity of each transaction. ");
}

FOUNDATION_STATIC void report_title_dividends_total_tooltip(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;

    const double avg = math_ifzero(title->average_price, title->stock->current.adjusted_close);
    ImGui::TextColored(ImColor(TOOLTIP_TEXT_COLOR), tr(" Total Dividends %.2lf $ "), title->total_dividends);

    // Get year after year yield
    const stock_t* s = title->stock;
    if (s != nullptr && array_size(s->history) > 1)
    {
        day_result_t* recent = array_first(s->history);
        day_result_t* oldest = array_last(s->history);

        const double years = (recent->date - oldest->date) / (365.0 * 24.0 * 60.0 * 60.0);
        const double max_change = (recent->adjusted_close - oldest->adjusted_close) / oldest->adjusted_close;
        const double yield = max_change / years * 100.0;

        ImGui::TextColored(ImColor(TOOLTIP_TEXT_COLOR), tr(" Y./Y. %.2lf %% (%.0lf years) "), yield, years);
    }
}

FOUNDATION_STATIC void report_title_open_buy_view(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;

    title->show_buy_ui = true;
}

FOUNDATION_STATIC void report_title_open_sell_view(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;

    if (title->average_quantity == 0)
        title->show_details_ui = true;
    else
        title->show_sell_ui = true;
}

FOUNDATION_STATIC void report_table_add_default_columns(report_handle_t report_handle, table_t* table)
{
    table_add_column(table, STRING_CONST("Title"),
        report_column_draw_title, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_FREEZE | COLUMN_CUSTOM_DRAWING)
        .set_header_render_callback(L3(report_column_title_header_render(report_handle, _1, _2, _3)))
        .set_context_menu_callback(L3(report_column_contextual_menu(report_handle, _1, _2, _3)));

    table_add_column(table, STRING_CONST(ICON_MD_BUSINESS " Name"),
        report_column_get_name, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);

    table_add_column(table, STRING_CONST(ICON_MD_TODAY " Date"),
        report_column_get_date, COLUMN_FORMAT_DATE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ZERO_USE_DASH)
        .set_selected_callback(report_title_open_details_view);

    table_add_column(table, STRING_CONST(" " ICON_MD_NUMBERS "||" ICON_MD_NUMBERS " Quantity"),
        report_column_buy_quantity, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_NUMBER_ABBREVIATION | COLUMN_ZERO_USE_DASH)
        .set_selected_callback(report_title_open_details_view);

    table_add_column(table, STRING_CONST("  Buy " ICON_MD_LOCAL_OFFER "||" ICON_MD_LOCAL_OFFER " Average Cost"),
        report_column_get_buy_price, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_SUMMARY_AVERAGE | COLUMN_ZERO_USE_DASH)
        .set_selected_callback(report_title_open_buy_view);

    table_add_column(table, STRING_CONST("Price " ICON_MD_MONETIZATION_ON "||" ICON_MD_MONETIZATION_ON " Market Price"),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_PRICE), COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_DYNAMIC_VALUE | COLUMN_SUMMARY_AVERAGE | COLUMN_ZERO_USE_DASH)
        .set_selected_callback(report_title_open_details_view)
        .set_tooltip_callback(report_title_live_price_tooltip)
        .set_style_formatter(report_title_price_alerts_formatter);

    table_add_column(table, STRING_CONST("  Ask " ICON_MD_PRICE_CHECK "||" ICON_MD_PRICE_CHECK " Ask Price"),
        report_column_get_ask_price, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_DYNAMIC_VALUE | COLUMN_SUMMARY_AVERAGE | COLUMN_ZERO_USE_DASH)
        .set_selected_callback(report_title_open_sell_view);

    table_add_column(table, "   Day " ICON_MD_ATTACH_MONEY "||" ICON_MD_ATTACH_MONEY " Day Gain. ",
        report_column_day_gain, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE)
        .set_tooltip_callback(report_title_day_change_tooltip);

    table_add_column(table, STRING_CONST("PS " ICON_MD_TRENDING_UP "||" ICON_MD_TRENDING_UP " Prediction Sensor"),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_PS), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_ROUND_NUMBER | COLUMN_DYNAMIC_VALUE)
        .set_selected_callback(report_title_pattern_open);
        
    table_add_column(table, STRING_CONST("EPS " ICON_MD_TRENDING_UP "||" ICON_MD_TRENDING_UP " Earning Trend"),
        report_column_earning_percent, COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | COLUMN_ZERO_USE_DASH);

    table_add_column(table, STRING_CONST(" Day %||" ICON_MD_PRICE_CHANGE " Day % "),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_DAY_CHANGE), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_DYNAMIC_VALUE)
        .set_tooltip_callback(report_title_day_change_tooltip);

    table_add_column(table, STRING_CONST("  Y. " ICON_MD_CALENDAR_VIEW_DAY "||" ICON_MD_CALENDAR_VIEW_DAY " Yesterday % "),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_YESTERDAY_CHANGE), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_DYNAMIC_VALUE);
    table_add_column(table, STRING_CONST("  1W " ICON_MD_CALENDAR_VIEW_WEEK "||" ICON_MD_CALENDAR_VIEW_WEEK " % since 1 week"),
        E32(report_column_get_change_value, _1, _2, -7), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_DYNAMIC_VALUE);
    table_add_column(table, STRING_CONST("  1M " ICON_MD_CALENDAR_VIEW_MONTH "||" ICON_MD_CALENDAR_VIEW_MONTH " % since 1 month"),
        E32(report_column_get_change_value, _1, _2, -31), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_DYNAMIC_VALUE | COLUMN_ROUND_NUMBER);
    table_add_column(table, STRING_CONST("  3M " ICON_MD_CALENDAR_VIEW_MONTH "||" ICON_MD_CALENDAR_VIEW_MONTH " % since 3 months"),
        E32(report_column_get_change_value, _1, _2, -90), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | COLUMN_ROUND_NUMBER);
    table_add_column(table, STRING_CONST("1Y " ICON_MD_CALENDAR_MONTH "||" ICON_MD_CALENDAR_MONTH " % since 1 year"),
        E32(report_column_get_change_value, _1, _2, -365), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | COLUMN_ROUND_NUMBER);
    table_add_column(table, STRING_CONST("10Y " ICON_MD_CALENDAR_MONTH "||" ICON_MD_CALENDAR_MONTH " % since 10 years"),
        E32(report_column_get_change_value, _1, _2, -365 * 10), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | COLUMN_ROUND_NUMBER);

    table_add_column(table, STRING_CONST(ICON_MD_FLAG "||" ICON_MD_FLAG " Currency"),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_CURRENCY), COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_CENTER_ALIGN | COLUMN_SEARCHABLE);
    table_add_column(table, STRING_CONST("   " ICON_MD_CURRENCY_EXCHANGE "||" ICON_MD_CURRENCY_EXCHANGE " Exchange Rate"),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_EXCHANGE_RATE), COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | COLUMN_SUMMARY_AVERAGE);

    table_add_column(table, STRING_CONST("R. " ICON_MD_ASSIGNMENT_RETURN "||" ICON_MD_ASSIGNMENT_RETURN " Return Rate (Yield)"),
        L2(report_column_get_dividends_yield(_1, _2)), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_ZERO_USE_DASH)
        .set_tooltip_callback(report_title_dividends_total_tooltip);

    table_add_column(table, STRING_CONST("     I. " ICON_MD_SAVINGS "||" ICON_MD_SAVINGS " Total Investments (based on average cost)"),
        report_column_get_total_investment, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ROUND_NUMBER | COLUMN_ZERO_USE_DASH);
    table_add_column(table, STRING_CONST("     V. " ICON_MD_ACCOUNT_BALANCE_WALLET "||" ICON_MD_ACCOUNT_BALANCE_WALLET " Total Value (as of today)"),
        report_column_get_total_value, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ROUND_NUMBER | COLUMN_ZERO_USE_DASH);

    table_add_column(table, STRING_CONST(" Gain " ICON_MD_DIFFERENCE "||" ICON_MD_DIFFERENCE " Total Gain (as of today)"),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_TOTAL_GAIN), COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE/* | COLUMN_ROUND_NUMBER*/)
        .set_style_formatter(report_title_total_gain_alerts_formatter)
        .set_tooltip_callback(report_title_gain_total_tooltip);
    table_add_column(table, STRING_CONST(" % " ICON_MD_PRICE_CHANGE "||" ICON_MD_PRICE_CHANGE " Total Gain % "),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_TOTAL_GAIN_P), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_ROUND_NUMBER)
        .set_style_formatter(report_title_total_gain_p_alerts_formatter);

    table_add_column(table, STRING_CONST(ICON_MD_INVENTORY " Type    "),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_TYPE), COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE);
    table_add_column(table, STRING_CONST(ICON_MD_STORE " Sector"),
        E32(report_column_get_fundamental_value, _1, _2, STRING_CONST("General.Sector|Category|Type")), COLUMN_FORMAT_TEXT, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_SEARCHABLE)
        .width = 200.0f;

    table_add_column(table, STRING_CONST(ICON_MD_DATE_RANGE "||" ICON_MD_DATE_RANGE " Elapsed Days"),
        report_column_average_days_held, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_SUMMARY_AVERAGE | COLUMN_ROUND_NUMBER | COLUMN_MIDDLE_ALIGN)
        .set_tooltip_callback(report_title_days_held_tooltip);

    // Add custom expression columns
    report_add_expression_columns(report_handle, table);    
}

FOUNDATION_STATIC void report_table_context_menu(report_handle_t report_handle, table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    if (element == nullptr)
    {
        report_t* report = report_get(report_handle);
        if (ImGui::MenuItem(tr(ICON_MD_ADD " Add title")))
            report->show_add_title_ui = true;

        if (ImGui::MenuItem(tr(ICON_MD_DASHBOARD_CUSTOMIZE " Expression Columns")))
            report_open_expression_columns_dialog(report_handle);
    }
    else
    {
        report_column_contextual_menu(report_handle, element, column, cell);
    }
}

FOUNDATION_STATIC report_handle_t report_create(const char* name, size_t name_length)
{
    report_handle_t report_handle = report_find(name, name_length);
    if (uuid_is_null(report_handle))
        report_handle = report_allocate(name, name_length);

    report_t* report = report_get(report_handle);
    report->save = true;
    report->show_summary = true;
    report->show_add_title_ui = true;

    log_infof(HASH_REPORT, STRING_CONST("Created report %.*s"), (int)name_length, name);

    return report_handle;
}

FOUNDATION_STATIC string_const_t report_get_save_file_path(report_t* report)
{
    string_const_t report_file_name = string_table_decode_const(report->name);

    if (!uuid_is_null(report->id))
    {
        report_file_name = string_from_uuid_static(report->id);
        config_set(report->data, STRING_CONST("id"), STRING_ARGS(report_file_name));
    }
    report_file_name = fs_clean_file_name(STRING_ARGS(report_file_name));
    return session_get_user_file_path(STRING_ARGS(report_file_name), STRING_ARGS(REPORTS_DIR_NAME), STRING_CONST("json"));
}

FOUNDATION_STATIC void report_rename(report_t* report, string_const_t name)
{
    report->name = string_table_encode(name);
    report->dirty = true;
}

FOUNDATION_STATIC void report_delete(report_t* report)
{
    report->save = false;
    report->opened = false;
    string_const_t report_save_file = report_get_save_file_path(report);
    if (fs_is_file(STRING_ARGS(report_save_file)))
        fs_remove_file(STRING_ARGS(report_save_file));
}

FOUNDATION_STATIC void report_toggle_show_summary(report_t* report)
{
    report->show_summary = !report->show_summary;
    report_summary_update(report);
}

FOUNDATION_STATIC void report_render_summary_line(report_t* report, const char* field_name, double value, const char* fmt, bool negative_parens = false)
{
    char value_buffer[16] = {0};

    string_t formatted_value = string_from_currency(STRING_BUFFER(value_buffer), math_abs(value), fmt);

    if (negative_parens && value < 0)
    {
        char neg_value_buffer[sizeof(value_buffer)] = {0};
        formatted_value = string_format(STRING_BUFFER(neg_value_buffer), STRING_CONST("(%.*s)"), STRING_FORMAT(formatted_value));
    }

    const float padding = IM_SCALEF(4);
    const float available_space = ImGui::GetContentRegionAvail().x;
    const float label_text_width = ImGui::CalcTextSize(field_name).x;
    const float value_text_width = ImGui::CalcTextSize(STRING_RANGE(formatted_value)).x;
    const float combined_text_width = label_text_width + value_text_width + padding;
 
    if (combined_text_width < available_space)
    {
        ImGui::TextUnformatted(field_name);
    }
    else
    {
        ImGui::PushTextWrapPos(available_space - value_text_width - padding);
        ImGui::TextUnformatted(field_name);
        ImGui::PopTextWrapPos();
    }

    ImGui::SameLine(available_space - value_text_width);
    ImGui::TextUnformatted(STRING_RANGE(formatted_value));
}

FOUNDATION_STATIC void report_render_summary(report_t* report)
{
    ImGuiTableFlags flags = 
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_NoClip |
        ImGuiTableFlags_NoHostExtendY |
        ImGuiTableFlags_SizingFixedSame |
        ImGuiTableFlags_NoBordersInBodyUntilResize |
        ImGuiTableFlags_PadOuterX |
        ImGuiTableFlags_Resizable;

    const ImVec2 space = ImGui::GetContentRegionAvail();

    // TODO: Draw closing X button

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(IM_SCALEF(4.0f), IM_SCALEF(4.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(IM_SCALEF(4.0f), IM_SCALEF(4.0f)));
    if (!ImGui::BeginChild("##Summary", {-1, -1}, false, ImGuiWindowFlags_AlwaysUseWindowPadding))
        return ImGui::EndChild();
    ImGui::TrTextUnformatted(ICON_MD_WALLET " Wallet");
    ImGui::SameLine();
    ImGui::MoveCursor(ImGui::GetContentRegionAvail().x - IM_SCALEF(18.0f), IM_SCALEF(1));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.6f, 0.4f, 0.4f, 0.5f));
    if (ImGui::Selectable(ICON_MD_CLOSE, false, 0, { IM_SCALEF(14), IM_SCALEF(14)}))
        report->show_summary = false;
    ImGui::PopStyleColor(1);

    if (wallet_draw(report->wallet, space.x))
    {
        report->dirty = true;
        report_refresh(report);
    }

    constexpr const char* currency_fmt = "-9 999 999.99 $";
    constexpr const char* pourcentage_fmt = "-9999.99 %";
    constexpr const char* integer_fmt = "-9 999 999  ";

    report_render_summary_line(report, tr("Target"), report->wallet->target_ask * 100.0, pourcentage_fmt);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::TrTooltip("Adjusted target based on the report current performance.");
    report_render_summary_line(report, tr("Profit"), report->wallet->profit_ask * 100.0, pourcentage_fmt);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::TrTooltip("Adjusted target based on the report overall performance and timelapse.");

    string_const_t user_preferred_currency = string_const(SETTINGS.preferred_currency);
    const double today_exchange_rate = stock_exchange_rate(STRING_CONST("USD"), STRING_ARGS(user_preferred_currency));
    report_render_summary_line(report, string_format_static_const("USD%s", SETTINGS.preferred_currency), today_exchange_rate, currency_fmt);
    if (ImGui::IsItemHovered())
    {
        double average_count = 0;
        double average_rate = 0.0f;
        const unsigned title_count = array_size(report->titles);
        for (unsigned i = 0, end = title_count; i != end; ++i)
        {
            const title_t* t = report->titles[i];
            if (string_equal(SYMBOL_CONST(t->stock->currency), string_const("USD")))
            {
                average_count++;
                average_rate += t->average_exchange_rate;
            }
        }
        average_rate /= average_count;
        if (!math_real_is_nan(average_rate))
            ImGui::SetTooltip(tr(" Average Rate (USD): %.2lf $ \n Based on the average acquisition time of every titles (%.0lf). "), 
                average_rate, average_count);
    }

    report_render_summary_line(report, tr("Avg. Days"), report->wallet->average_days, integer_fmt);
    report_render_summary_line(report, tr("Daily average"), report->total_daily_average_p, pourcentage_fmt, true);

    ImGui::PushStyleColor(ImGuiCol_Text, report->total_day_gain > 0 ? TEXT_GOOD_COLOR : TEXT_WARN_COLOR);
    report_render_summary_line(report, tr("Day Gain"), report->total_day_gain, currency_fmt, true);
    ImGui::PopStyleColor(1);

    const double total_funds = wallet_total_funds(report->wallet);
    const double cash_balance = total_funds + report->wallet->sell_total_gain - report->total_investment + report->wallet->total_dividends;
    if (report->wallet->total_title_sell_count > 0)
    {
        ImGui::Separator();

        report_render_summary_line(report, tr("Sell Count"), report->wallet->total_title_sell_count, integer_fmt);
        report_render_summary_line(report, tr("Sell Total"), report->wallet->sell_total_gain, currency_fmt, true);
        report_render_summary_line(report, tr("Sell Average"), report->wallet->sell_gain_average, currency_fmt, true);

        report_render_summary_line(report, tr("Enhanced earnings"), report->wallet->enhanced_earnings, currency_fmt);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip(tr("Minimal amount (%.2lf) to sell titles if you want to increase your gain considerably."), report->wallet->enhanced_earnings);

        const double sell_greediness = report->wallet->total_sell_gain_if_kept;
        ImGui::PushStyleColor(ImGuiCol_Text, sell_greediness <= 0 ? TEXT_GOOD_COLOR : TEXT_WARN_COLOR);
        report_render_summary_line(report, tr("Sell Greediness"), sell_greediness, currency_fmt, true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip(tr(" Loses or (Gains) if titles were kept longer before being sold"));
        ImGui::PopStyleColor(1);
    }

    ImGui::Separator();

    if (total_funds > 0)
        report_render_summary_line(report, tr("Cash Balance"), cash_balance, currency_fmt, true);

    if (report->wallet->total_dividends > 0)
        report_render_summary_line(report, tr("Dividends"), report->wallet->total_dividends, currency_fmt);
    report_render_summary_line(report, tr("Investments"), report->total_investment, currency_fmt);
    report_render_summary_line(report, tr("Total Value"), report->total_value, currency_fmt);

    const double total_gain_with_sales_and_dividends = report->total_gain + report->wallet->sell_total_gain + report->wallet->total_dividends;
    ImGui::PushStyleColor(ImGuiCol_Text, total_gain_with_sales_and_dividends > 0 ? TEXT_GOOD_COLOR : TEXT_WARN_COLOR);
    report_render_summary_line(report, tr("Total Gain"), total_gain_with_sales_and_dividends, currency_fmt, true);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::TrTooltip(" Total Gain (Includes current value gain, sells and dividends)");

    if (total_funds > 0)
    {
        const double gain_p = total_gain_with_sales_and_dividends / total_funds * 100.0;
        report_render_summary_line(report, "", math_ifnan(gain_p, report->total_gain_p * 100.0), pourcentage_fmt, true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::TrTooltip(" Total Gain %% (based on the initial funds)");
    }
    else
    {
        const double gain_p = (report->total_value - report->total_investment) / report->total_investment * 100.0;
        report_render_summary_line(report, "", gain_p, pourcentage_fmt, true);
    }

    ImGui::PopStyleColor(1);

    if (report_is_loading(report))
        report_render_summary_line(report, tr("Loading data..."), NAN, nullptr);

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
}

FOUNDATION_STATIC void report_render_add_title_from_ui(report_t* report, string_const_t code)
{
    title_t* new_title = report_title_add(report, code);
    new_title->show_buy_ui = true;
    report->show_add_title_ui = false;
    report_refresh(report);
}

FOUNDATION_STATIC string_const_t report_render_input_dialog(string_const_t title, string_const_t apply_label, string_const_t initial_value, string_const_t hint, bool* show_ui)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(IM_SCALEF(6), IM_SCALEF(10)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(IM_SCALEF(6), IM_SCALEF(10)));
    if (!report_render_dialog_begin(title, show_ui, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar))
    {
        ImGui::PopStyleVar(2);
        return string_null();
    }

    bool applied = false;
    bool can_apply = false;
    static char input[64] = { '\0' };
    size_t input_length = 0;

    if (ImGui::IsWindowAppearing())
    {
        input_length = string_copy(STRING_BUFFER(input), STRING_ARGS(initial_value)).length;
    }

    const float available_space = ImGui::GetContentRegionAvail().x;

    if (ImGui::BeginChild("##Content", ImVec2(IM_SCALEF(350.0f), IM_SCALEF(90.0f)), false))
    {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();

        ImGui::ExpandNextItem();
        if (ImGui::InputTextEx("##InputField", hint.str, STRING_BUFFER(input), ImVec2(0, 0),
            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue))
        {
            applied = true;
        }

        input_length = string_length(input);
        if (input_length > 0)
            can_apply = true;

        static float apply_button_width = IM_SCALEF(90);
        static float cancel_button_width = IM_SCALEF(90);
        const float button_between_space = IM_SCALEF(4);

        ImGui::MoveCursor(available_space - cancel_button_width - apply_button_width - button_between_space, IM_SCALEF(8));
        if (ImGui::Button(tr("Cancel"), { IM_SCALEF(90), IM_SCALEF(24) }))
        {
            applied = false;
            *show_ui = false;
        }
        cancel_button_width = ImGui::GetItemRectSize().x;

        ImGui::SameLine();
        ImGui::BeginDisabled(!can_apply);
        if (ImGui::Button(apply_label.str, { IM_SCALEF(90), IM_SCALEF(24) }))
            applied = true;
        apply_button_width = ImGui::GetItemRectSize().x;
        ImGui::EndDisabled();

        if (can_apply && applied)
            *show_ui = false;
    } ImGui::EndChild();

    ImGui::PopStyleVar(2);
    report_render_dialog_end();
    if (can_apply && applied)
        return string_const(input, input_length);

    return string_null();
}

FOUNDATION_STATIC void report_render_rename_dialog(report_t* report)
{
    string_const_t current_name = string_table_decode_const(report->name);
    string_const_t name = report_render_input_dialog(RTEXT("Rename##1"), RTEXT("Apply"), current_name, 
        current_name, &report->show_rename_ui);
    if (!string_is_null(name))
        report_rename(report, name);
}

FOUNDATION_STATIC void report_render_add_title_dialog(report_t* report)
{
    ImGui::SetNextWindowSize(ImVec2(1200, 600), ImGuiCond_Once);

    string_const_t fmttr = RTEXT("Add Title (%.*s)##5");
    string_const_t popup_id = string_format_static(STRING_ARGS(fmttr), STRING_FORMAT(string_table_decode_const(report->name)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(IM_SCALEF(6), IM_SCALEF(2)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(IM_SCALEF(6), IM_SCALEF(4)));
    if (report_render_dialog_begin(popup_id, &report->show_add_title_ui, ImGuiWindowFlags_None))
    {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        symbols_render_search(L1(report_render_add_title_from_ui(report, _1)));

        report_render_dialog_end();
    }
    ImGui::PopStyleVar(2);
}

FOUNDATION_STATIC void report_render_dialogs(report_t* report)
{
    if (report->show_add_title_ui)
    {
        report_render_add_title_dialog(report);
    }
    else if (report->show_rename_ui)
    {
        report_render_rename_dialog(report);
    }
    else
    {
        for (int i = 0, end = array_size(report->titles); i != end; ++i)
        {
            title_t* title = report->titles[i];
            if (title->show_buy_ui)
                report_render_buy_lot_dialog(report, title);
            else if (title->show_sell_ui)
                report_render_sell_lot_dialog(report, title);
            else if (title->show_details_ui)
                report_render_title_details(report, title);
        }
    }
}

FOUNDATION_STATIC bool report_initial_sync(report_t* report)
{
    if (report->fully_resolved == 1)
        return true;

    // No need to retry syncing right away
    if (time_elapsed(report->fully_resolved) < 1.0)
        return false;

    bool fully_resolved = true;
    const int title_count = array_size(report->titles);
    foreach (pt, report->titles)
    {
        title_t* t = *pt;
        if (!t || title_is_index(t))
            continue;

        const bool stock_resolved = title_is_resolved(t);
        fully_resolved &= stock_resolved;

        if (!stock_resolved)
        {
            bool first_init = !t->stock;                
            if (!stock_update(t->code, t->code_length, t->stock, title_minimum_fetch_level(t), 10.0) && !first_init &&
                !dispatcher_wait_for_wakeup_main_thread(1000 / title_count) &&
                !title_is_resolved(t))
            {
                log_debugf(HASH_REPORT, STRING_CONST("Refreshing %s is taking longer than expected"), t->code);
                break;
            }
        }
    }

    report->fully_resolved = time_current();
    if (!fully_resolved)
        return false;

    job_t** update_jobs = nullptr;

    foreach (title, report->titles)
    {
        job_t* job = job_execute([](void* context)->int
        {
            title_t** title = (title_t**)context;
            title_refresh(*title);

            stock_realtime_t realtime;
            realtime.price = (*title)->stock->current.price;
            realtime.volume = (*title)->stock->current.volume;
            realtime.timestamp = (*title)->stock->current.date;
            string_copy(realtime.code, sizeof(realtime.code), (*title)->code, (*title)->code_length);

            return dispatcher_post_event(EVENT_STOCK_REQUESTED, (void*)&realtime, sizeof(realtime), DISPATCHER_EVENT_OPTION_COPY_DATA);
        }, title);
        array_push(update_jobs, job);
    }

    // Wait for updates
    for (unsigned i = 0, count = array_size(update_jobs); i < count; ++i)
    {
        job_t* job = update_jobs[i];
        while (!job_completed(job))
            dispatcher_wait_for_wakeup_main_thread();
        job_deallocate(job);
    }
    array_deallocate(update_jobs);

    report_filter_out_titles(report);
    report_summary_update(report);
    wallet_update_tracking_history(report, report->wallet);

    log_debugf(HASH_REPORT, STRING_CONST("Fully resolved %s"), string_table_decode(report->name));
    if (report->table)
        report->table->needs_sorting = true;

    report->fully_resolved = 1;
    return fully_resolved;
}

FOUNDATION_STATIC table_t* report_create_table(report_t* report)
{
    const char* name = SYMBOL_CSTR(report->name);
    table_t* table = table_allocate(name);
    report_table_setup(report->id, table);
    report_table_add_default_columns(report->id, table);

    return table;
}

FOUNDATION_STATIC report_handle_t report_allocate(const char* name, size_t name_length, config_handle_t data)
{
    string_table_symbol_t name_symbol = string_table_encode(name, name_length);

    for (unsigned i = 0, end = array_size(_reports); i < end; ++i)
    {
        const report_t& r = _reports[i];
        if (r.name == name_symbol)
            return r.id;
    }

    array_push(_reports, report_t{ name_symbol });
    report_t* report = array_last(_reports);

    if (!data)
    {
        log_warnf(HASH_REPORT, WARNING_RESOURCE, STRING_CONST("Creating new report with empty data: %.*s"), (int)name_length, name);
    }

    // Ensure default structure
    report->data = config_is_valid(data) ? data : config_allocate(CONFIG_VALUE_OBJECT, CONFIG_OPTION_PRESERVE_INSERTION_ORDER);
    report->wallet = wallet_allocate(report->data["wallet"]);

    auto cid = report->data["id"];
    auto cname = config_set(report->data, STRING_CONST("name"), name, name_length);
    auto ctitles = config_set_object(report->data, STRING_CONST("titles"));

    if (config_is_valid(cid))
    {
        string_const_t id = cid.as_string();
        report->id = string_to_uuid(STRING_ARGS(id));
    }
    else
    {
        report->id = uuid_generate_time();
        string_const_t id_str = string_from_uuid_static(report->id);
        cid = config_set(report->data, STRING_CONST("id"), STRING_ARGS(id_str));
    }

    report->save_index = report->data["order"].as_integer();
    report->show_summary = report->data["show_summary"].as_boolean();
    report->show_sold_title = report->data["show_sold_title"].as_boolean(true);
    report->show_no_transaction_title = report->data["show_no_transaction_title"].as_boolean(true);
    report->opened = report->data["opened"].as_boolean(true);    

    // Load titles
    title_t** titles = nullptr;
    for (auto title_data : ctitles)
    {
        string_const_t code = config_name(title_data);
        title_t* title = title_allocate(report->wallet, title_data);
        array_push(titles, title);
    }
    report->titles = titles;

    report_filter_out_titles(report);
    report_summary_update(report);

    // Create table
    report_load_expression_columns(report);
    report->table = nullptr;
    
    return report->id;
}

FOUNDATION_STATIC void report_render_windows()
{
    report_render_create_dialog(&SETTINGS.show_create_report_ui);
}

FOUNDATION_STATIC report_handle_t report_load(config_handle_t data)
{
    string_const_t report_name = data["name"].as_string();
    report_handle_t report_handle = report_allocate(STRING_ARGS(report_name), data);
    report_t* report = report_get(report_handle);
    report->save = true;
    return report_handle;
}

FOUNDATION_STATIC bool report_import_dialog_callback(string_const_t filepath)
{
    if (string_is_null(filepath))
        return false;

    config_handle_t report_data = nullptr;

    try
    {
        report_data = config_parse_file(STRING_ARGS(filepath));
    }
    catch (const std::runtime_error& err)
    {
        log_errorf(HASH_REPORT, ERROR_INVALID_VALUE, 
            STRING_CONST("Failed to parse report %.*s.\nReason: %s"), 
            STRING_FORMAT(filepath), err.what());
        return false;
    }

    if (!report_data)
    {
        log_errorf(HASH_REPORT, ERROR_INVALID_VALUE, STRING_CONST("Invalid report data %.*s"), STRING_FORMAT(filepath));
        return false;
    }

    // Check that we have a valid report
    config_handle_t wallet_data = report_data["wallet"];
    if (config_value_type(wallet_data) != CONFIG_VALUE_OBJECT)
    {
        log_errorf(HASH_REPORT, ERROR_INVALID_VALUE, 
            STRING_CONST("Report %.*s is missing wallet information"), STRING_FORMAT(filepath));
        config_deallocate(report_data);
        return false;
    }

    config_handle_t titles_data = report_data["titles"];
    if (config_value_type(titles_data) != CONFIG_VALUE_OBJECT)
    {
        log_errorf(HASH_REPORT, ERROR_INVALID_VALUE,
            STRING_CONST("Report %.*s is missing title information"), STRING_FORMAT(filepath));
        config_deallocate(report_data);
        return false;
    }

    string_const_t report_name = report_data["name"].as_string();
    if (string_is_null(report_name))
    {
        report_name = path_base_file_name(STRING_ARGS(filepath));
        config_set_string(report_data, STRING_CONST("name"), STRING_ARGS(report_name));
    }

    report_handle_t report_handle = report_load(report_data);
    report_t* report = report_get(report_handle);
    if (!report)
        return false;

    report->save = true;
    report->dirty = true;
    report->opened = true;

    return report_refresh(report);
}

FOUNDATION_STATIC bool report_export_dialog_callback(report_handle_t report_handle, string_const_t filepath)
{
    // Check if we can restore the report pointer.
    report_t* report = report_get(report_handle);
    if (!report)
        return false;

    return report_save(report, STRING_ARGS(filepath));
}

FOUNDATION_STATIC void report_open_import_dialog()
{
    system_open_file_dialog(tr("Import Report..."), 
        tr("Reports (*.report)|*.report;*.json|SJSON Files (*.sjson)|*.sjson"), 
        nullptr, report_import_dialog_callback);
}

FOUNDATION_STATIC void report_open_export_dialog(report_t* report)
{
    report_handle_t report_handle = report->id;
    string_const_t report_name = SYMBOL_CONST(report->name);
    system_save_file_dialog(
        tr("Export Report..."),
        tr("Reports (*.report)|*.report"), report_name.str, [report_handle](string_const_t _1)
    {
        return report_export_dialog_callback(report_handle, _1);
    });
}

FOUNDATION_STATIC void report_render_menus()
{
    if (shortcut_executed(ImGuiKey_F2))
        SETTINGS.show_create_report_ui = true;

    if (!ImGui::BeginMenuBar())
        return;
        
    if (ImGui::BeginMenu(tr("File")))
    {
        if (ImGui::BeginMenu(tr("Create")))
        {
            if (ImGui::MenuItem(tr("Report"), "F2", &SETTINGS.show_create_report_ui))
                SETTINGS.show_create_report_ui = true;
            ImGui::EndMenu();
        }

        if (ImGui::TrBeginMenu("Open"))
        {
            if (ImGui::TrMenuItem("Import...", nullptr, nullptr))
                report_open_import_dialog();

            bool first_report_that_can_be_opened = true;
            report_t** reports = report_sort_alphabetically();
            for (int i = 0, end = array_size(reports); i < end; ++i)
            {
                report_t* report = reports[i];
                if (!report->opened)
                {
                    if (first_report_that_can_be_opened)
                    {
                        ImGui::Separator();
                        first_report_that_can_be_opened = false;
                    }
                    ImGui::MenuItem(
                        string_format_static_const("%s", string_table_decode(report->name)),
                        nullptr, &report->opened);
                }
            }
            array_deallocate(reports);

            ImGui::EndMenu();
        }
            
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

FOUNDATION_STATIC void report_render_tabs()
{
    static const ImVec4 TAB_COLOR_REPORT(0.4f, 0.2f, 0.7f, 1.0f);

    tab_set_color(TAB_COLOR_APP);
    tab_draw(tr(ICON_MD_WALLET " Wallet "), nullptr, ImGuiTabItemFlags_Leading, wallet_history_draw, nullptr);

    tab_set_color(TAB_COLOR_REPORT);
    size_t report_count = ::report_count();
    for (int handle = 0; handle < report_count; ++handle)
    {
        report_t* report = report_get_at(handle);
        if (report->opened)
        {
            string_const_t id = string_from_uuid_static(report->id);
            string_const_t name = string_table_decode_const(report->name);
            string_const_t report_tab_id = string_format_static(STRING_CONST(ICON_MD_WALLET " %.*s###%.*s"), STRING_FORMAT(name), STRING_FORMAT(id));
            report->save_index = ImGui::GetTabItemVisibleIndex(report_tab_id.str);

            tab_draw(report_tab_id.str, &report->opened, (report->dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None),
                L0(report_render(report)), L0(report_menu(report)));
        }
    }
}

// 
// # PUBLIC API
//

void report_summary_update(report_t* report)
{
    // Update report average days
    double total_days = 0;
    double total_value = 0;
    double total_investment = 0;
    double total_sell_gain_if_kept = 0;
    double total_sell_gain_if_kept_p = 0;
    double total_title_sell_count = 0;
    double total_sell_rated = 0;
    double total_sell_gain_rated = 0;
    double total_buy_rated = 0;
    double average_nq = 0;
    double average_nq_count = 0;
    double total_day_gain = 0;
    double total_daily_average_p = 0;
    double title_resolved_count = 0;
    double total_dividends = 0;
    double total_active_titles = 0;
    const size_t title_count = array_size(report->titles);
    for (size_t i = 0; i < title_count; ++i)
    {
        const title_t* t = report->titles[i];
        FOUNDATION_ASSERT(t);

        if (title_is_index(t))
            continue;

        if (t->average_quantity > 0)
        {
            const double days_held = title_average_days_held(t);
            
            total_days += days_held;
            total_active_titles++;
        }

        const bool title_is_sold = title_sold(t);
        if (!title_is_sold)
            total_investment += title_total_bought_price(t);

        const stock_t* s = t->stock;
        const bool stock_valid = s && !math_real_is_nan(s->current.change_p);
        // Make sure the stock is still valid today, it might have been delisted.
        if (stock_valid)
        {
            if (!title_is_sold)
                total_value += title_get_total_value(t);
            average_nq += s->current.change_p / 100.0;
            average_nq_count++;

            average_nq += title_get_yesterday_change(t, s) / 100.0;
            average_nq_count++;

            if (!math_real_is_nan(s->current.change))
                total_day_gain += math_ifnan(title_get_day_change(t, s), 0);

            total_daily_average_p += s->current.change_p;

            title_resolved_count++;
        }
        else if (!title_is_sold)
        {
            total_value += t->average_quantity * t->average_price;
        }

        total_buy_rated += t->buy_total_price_rated;
        total_sell_rated += t->sell_total_price_rated;
        total_dividends += t->total_dividends;

        if (stock_valid && t->sell_total_quantity > 0)
        {
            const double sell_adjusted_price = t->sell_total_price_rated / t->sell_total_quantity;
            const double sell_gain_if_kept = (s->current.adjusted_close - sell_adjusted_price) * t->sell_total_quantity;
            const double sell_p = (s->current.price - sell_adjusted_price) / sell_adjusted_price;
            if (!math_real_is_nan(sell_p))
            {
                total_sell_gain_if_kept_p += sell_p;
                total_sell_gain_if_kept += sell_gain_if_kept;
                total_title_sell_count++;
                total_sell_gain_rated += title_get_sell_gain_rated(t);
            }
        }
    }

    if (total_active_titles > 0)
        report->wallet->average_days = total_days / total_active_titles;

    if (average_nq_count > 0)
        average_nq /= average_nq_count;

    report->wallet->total_title_sell_count = total_title_sell_count;
    report->wallet->total_sell_gain_if_kept = total_sell_gain_if_kept;
    if (total_title_sell_count > 0)
        total_sell_gain_if_kept_p /= total_title_sell_count;
    else
        total_sell_gain_if_kept_p = 0;

    report->total_daily_average_p = total_daily_average_p / title_resolved_count;
    report->total_value = total_value;
    report->total_investment = total_investment;
    report->total_gain = total_value - total_investment + total_dividends;
    if (total_investment != 0)
        report->total_gain_p = report->total_gain / total_investment;
    else
        report->total_gain_p = 0;
    report->total_day_gain = total_day_gain;
    report->summary_last_update = time_current();

    // Update historical values
    report->wallet->sell_average = total_sell_rated / total_title_sell_count;
    report->wallet->sell_total_gain = total_sell_gain_rated;
    report->wallet->sell_gain_average = total_sell_gain_rated / total_title_sell_count;
    report->wallet->total_sell_gain_if_kept_p = total_sell_gain_if_kept_p;
    report->wallet->target_ask = report->wallet->main_target + report->total_gain_p;
    report->wallet->profit_ask = 
        max(report->wallet->target_ask + 
            min(total_sell_gain_if_kept_p, report->wallet->target_ask * total_title_sell_count) +
            math_abs(average_nq), 0.03);
    report->wallet->enhanced_earnings = math_abs(report->wallet->sell_gain_average) * (1.0 + report->wallet->main_target);
    report->wallet->total_dividends = total_dividends;
}

bool report_is_loading(report_t* report)
{
    const size_t title_count = array_size(report->titles);
    for (size_t i = 0; i < title_count; ++i)
    {
        const title_t* t = report->titles[i];
        if (title_is_index(t))
            continue;
        if (!title_is_resolved(t))
            return true;
    }

    return false;
}

bool report_refresh(report_t* report)
{
    WAIT_CURSOR;

    const size_t title_count = array_size(report->titles);
    for (size_t i = 0; i < title_count; ++i)
    {
        title_t* t = report->titles[i];

        // If the title is sold, we don't need to refresh it.
        if (!report->show_sold_title && title_sold(t))
            continue;

        // If the title has no transaction
        if (!report->show_no_transaction_title && t->buy_total_count == 0)
            continue;

        t->stock->fetch_errors = 0;
        t->stock->resolved_level &= ~FetchLevel::REALTIME;
        if (!stock_resolve(t->stock, FetchLevel::REALTIME))
            dispatcher_wait_for_wakeup_main_thread(50);
        report->fully_resolved = 0;
    }

    // Reset custom columns data
    report_expression_column_reset(report);
    
    return report->fully_resolved == 0;
}

void report_menu(report_t* report)
{
    if (shortcut_executed(ImGuiKey_F4))
        report_toggle_show_summary(report);
    else if (shortcut_executed(true, ImGuiKey_S))
        report_save(report);

    if (ImGui::BeginPopupContextItem())
    {
        if (report->dirty && ImGui::TrMenuItem("Save"))
            report_save(report);

        if (ImGui::TrMenuItem("Export..."))
            report_open_export_dialog(report);

        ImGui::Separator();

        if (ImGui::TrMenuItem("Rename"))
            report->show_rename_ui = true;

        if (ImGui::TrMenuItem("Delete"))
            report_delete(report);

        ImGui::EndPopup();
    }

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::TrBeginMenu("Report"))
        {
            if (ImGui::TrMenuItem(ICON_MD_REFRESH " Refresh", "F5"))
                report_refresh(report);

            ImGui::Separator();

            if (ImGui::MenuItem(tr(ICON_MD_ADD " Add Title")))
                report->show_add_title_ui = true;

            if (ImGui::MenuItem(tr(ICON_MD_DASHBOARD_CUSTOMIZE " Expression Columns")))
                report_open_expression_columns_dialog(report_get_handle(report));

            ImGui::Separator();

            if (ImGui::TrMenuItem(ICON_MD_SELL " Show Sold", nullptr, &report->show_sold_title))
                report_filter_out_titles(report);

            if (ImGui::TrMenuItem(ICON_MD_NO_ENCRYPTION " Show Titles With No Transaction", nullptr, &report->show_no_transaction_title))
                report_filter_out_titles(report);

            if (ImGui::TrMenuItem(ICON_MD_SUMMARIZE " Show Summary", "F4", &report->show_summary))
                report_summary_update(report);

            if (ImGui::TrMenuItem(ICON_MD_TIMELINE " Show Timeline"))
                timeline_render_graph(report);

            if (ImGui::TrMenuItem(ICON_MD_AUTO_GRAPH " Show Transactions"))
            {
                const char* window_title = tr_format("{0} Transactions", string_table_decode(report->name));
                window_open("##Transactions", window_title, string_length(window_title), [](window_handle_t win)
                {
                    report_t* report = (report_t*)window_get_user_data(win);
                    report_graph_show_transactions(report);
                }, nullptr, report);
            }
                
            ImGui::Separator();

            if (report->save)
            {
                if (ImGui::TrMenuItem(ICON_MD_SAVE " Save", ICON_MD_KEYBOARD_COMMAND "+S"))
                    report_save(report);
            }

            if (ImGui::TrMenuItem(ICON_MD_SAVE_AS " Export..."))
                report_open_export_dialog(report);

            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

// TODO: Use app API to spawn dialog
bool report_render_dialog_begin(string_const_t name, bool* show_ui, unsigned int flags /*= ImGuiWindowFlags_NoSavedSettings*/)
{
    if (show_ui == nullptr || *show_ui == false)
        return false;
    _last_show_ui_ptr = show_ui;

    if (*show_ui && shortcut_executed(ImGuiKey_Escape))
    {
        *show_ui = false;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Once, ImVec2(0.5f, 0.5f));
    if (!ImGui::Begin(name.str, show_ui, ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysUseWindowPadding | flags))
    {
        ImGui::End();
        return false;
    }

    return true;
}

bool report_render_dialog_end(bool* show_ui /*= nullptr*/)
{
    if (show_ui == nullptr)
        show_ui = _last_show_ui_ptr;
    if (show_ui != nullptr && !ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
        *show_ui = false;

    ImGui::End();

    return show_ui && *show_ui == false;
}

void report_render_create_dialog(bool* show_ui)
{
    FOUNDATION_ASSERT(show_ui);

    string_const_t name = report_render_input_dialog(RTEXT("Create Report##1"), RTEXT("Create"), CTEXT(""), RTEXT("Name"), show_ui);
    if (!string_is_null(name))
        report_create(STRING_ARGS(name));
}

report_handle_t report_load(string_const_t report_file_path)
{
    const auto report_json_flags =
        CONFIG_OPTION_WRITE_SKIP_DOUBLE_COMMA_FIELDS |
        CONFIG_OPTION_PRESERVE_INSERTION_ORDER |
        CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES |
        CONFIG_OPTION_WRITE_TRUNCATE_NUMBERS |
        CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS;

    config_handle_t data{};
    if (fs_is_file(STRING_ARGS(report_file_path)))
    {
        data = config_parse_file(STRING_ARGS(report_file_path), report_json_flags);
        if (!data)
        {
            FOUNDATION_ASSERT(data);
            log_warnf(HASH_REPORT, WARNING_INVALID_VALUE, STRING_CONST("Failed to load report '%.*s'"), STRING_FORMAT(report_file_path));
        }
    }

    string_const_t report_name = data["name"].as_string();
    if (string_is_null(report_name))
        report_name = path_base_file_name(STRING_ARGS(report_file_path));
    report_handle_t report_handle = report_allocate(STRING_ARGS(report_name), data);
    report_t* report = report_get(report_handle);
    report->save = true;
    return report_handle;
}

report_handle_t report_load(const char* name, size_t name_length)
{
    string_const_t report_file_path = session_get_user_file_path(name, name_length, STRING_ARGS(REPORTS_DIR_NAME), STRING_CONST("json"));
    return report_load(report_file_path);
}

bool report_save(report_t* report, const char* file_path, size_t file_path_length)
{
    // Replicate some memory fields
    config_set(report->data, "name", string_table_decode_const(report->name));
    config_set(report->data, "order", (double)report->save_index);
    config_set(report->data, "show_summary", report->show_summary);
    config_set(report->data, "show_sold_title", report->show_sold_title);
    config_set(report->data, "show_no_transaction_title", report->show_no_transaction_title);
    config_set(report->data, "opened", report->opened);

    report_expression_columns_save(report);    

    wallet_save(report->wallet, config_set_object(report->data, STRING_CONST("wallet")));

    string_const_t report_file_path = string_const(file_path, file_path_length);
    return config_write_file(report_file_path, report->data,
        CONFIG_OPTION_WRITE_SKIP_NULL | CONFIG_OPTION_WRITE_SKIP_DOUBLE_COMMA_FIELDS | CONFIG_OPTION_WRITE_NO_SAVE_ON_DATA_EQUAL);
}

void report_save(report_t* report)
{
    string_const_t report_file_path = report_get_save_file_path(report);
    if (report_save(report, STRING_ARGS(report_file_path)))
        report->dirty = false;
}

void report_render(report_t* report)
{
    FOUNDATION_ASSERT(report);
    const float space_left = ImGui::GetContentRegionAvail().x;

    if (shortcut_executed(ImGuiKey_F5))
    {
        tr_warn(HASH_REPORT, WARNING_PERFORMANCE, "Refreshing report {0,st}", report->name);
        report_refresh(report);
    }

    if (report->fully_resolved != 1)
        report_initial_sync(report);

    imgui_frame_render_callback_t summary_frame = nullptr;
    if (report->show_summary)
    {
        summary_frame = [report](const ImRect& rect)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0F, 0));
            report_render_summary(report);
            ImGui::PopStyleVar();
        };   
    }

    expr_set_or_create_global_var(STRING_CONST("$REPORT"), expr_result_t(SYMBOL_CSTR(report->name)));
    
    imgui_draw_splitter("Report", [report](const ImRect& rect)
    {
        if (report->active_titles > 0)
        {
            if (report->table == nullptr)
                report->table = report_create_table(report);
            report->table->search_filter = string_to_const(SETTINGS.search_filter);
            table_render(report->table, report->titles, (int)report->active_titles, sizeof(title_t*), 0.0f, 0.0f);
        }
        else
        {
            if (ImGui::CenteredButton(tr("Add New Title"), { IM_SCALEF(180), IM_SCALEF(30) }))
                report->show_add_title_ui = true;
        }
    }, summary_frame, IMGUI_SPLITTER_HORIZONTAL, 0, (space_left - IM_SCALEF(250.0f)) / space_left);
    
    report_render_dialogs(report);
}

void report_sort_order()
{
    array_sort(_reports, [](const report_t& a, const report_t& b)
    {
        if (a.save_index == b.save_index)
            return strcmp(string_table_decode(a.name), string_table_decode(b.name));
        return a.save_index - b.save_index;
    });
}

report_handle_t report_allocate(const char* name, size_t name_length)
{
    return report_allocate(name, name_length, config_null());
}

report_t* report_get(report_handle_t report_handle)
{
    foreach (r, _reports)
    {
        if (uuid_equal(r->id, report_handle))
            return r;
    }
    return nullptr;
}

report_t* report_get_at(unsigned int index)
{
    if (index >= array_size(_reports))
        return nullptr;
    return &_reports[index];
}

size_t report_count()
{
    return array_size(_reports);
}

report_handle_t report_find(const char* name, size_t name_length)
{
    string_table_symbol_t report_name_symbol = string_table_encode(name, name_length);
    for (int i = 0, end = array_size(_reports); i != end; ++i)
    {
        report_t* report = &_reports[i];

        if (report->name == report_name_symbol)
            return report->id;
    }

    return uuid_null();
}

report_handle_t report_find_no_case(const char* name, size_t name_length)
{
    report_handle_t handle = report_find(name, name_length);
    if (report_handle_is_valid(handle))
        return handle;

    // Do long search by name with no casing
    for (int i = 0, end = array_size(_reports); i != end; ++i)
    {
        report_t* report = &_reports[i];
        string_const_t report_name = string_table_decode_const(report->name);

        if (string_equal_nocase(STRING_ARGS(report_name), name, name_length))
            return report->id;
    }

    return uuid_null();
}

bool report_handle_is_valid(report_handle_t handle)
{
    return !uuid_is_null(handle);
}

bool report_sync_titles(report_t* report, double timeout_seconds /*= 60.0*/)
{
    const size_t title_count = array_size(report->titles);

    job_t** update_jobs = nullptr;

    // Trigger updates
    for (size_t i = 0; i < title_count; ++i)
    {
        title_t* t = report->titles[i];
        if (title_is_index(t))
            continue;

        if (!title_is_resolved(t))
        {
            job_t* job = job_execute([](void* context)->int
            {
                title_t* t = (title_t*)context;
                log_debugf(HASH_REPORT, STRING_CONST("Syncing title %s"), t->code);
                title_update(t, 5.0);
                return 0;
            }, t);
            array_push(update_jobs, job);
        }
    }

    // Wait for updates
    for (unsigned i = 0, count = array_size(update_jobs); i < count; ++i)
    {
        job_t* job = update_jobs[i];
        while (!job_completed(job))
            dispatcher_wait_for_wakeup_main_thread();
        job_deallocate(job);
    }
    array_deallocate(update_jobs);

    // Wait for title resolution
    tick_t timer = time_current();
    for (size_t i = 0; i < title_count; ++i)
    {
        title_t* t = report->titles[i];
        if (title_is_index(t))
            continue;
            
        while (!title_is_resolved(t))
        {
            if (time_elapsed(timer) > timeout_seconds)
                return false;

            dispatcher_wait_for_wakeup_main_thread(50);
        }

        log_debugf(HASH_REPORT, STRING_CONST(">>> Title %s synced"), t->code);
    }

    // Update report summary
    report_summary_update(report);
    for (size_t i = 0; i < title_count; ++i)
        title_refresh(report->titles[i]);
    report_summary_update(report);
    if (report->table)
        report->table->needs_sorting = true;

    log_infof(HASH_REPORT, STRING_CONST("Report %s synced completed in %.3g seconds"), SYMBOL_CSTR(report->name), time_elapsed(timer));
    return true;
}

title_t* report_add_title(report_t* report, const char* code, size_t code_length)
{
    return report_title_add(report, string_const(code, code_length));
}

void report_table_rebuild(report_t* report)
{
    FOUNDATION_ASSERT(report);

    if (report->table)
    {
        table_clear_columns(report->table);
        report_table_add_default_columns(report_get_handle(report), report->table);
        report->dirty = true;
    }
    else
    {
        report->table = report_create_table(report);
    }
}

report_handle_t report_get_handle(const report_t* report_ptr)
{
    foreach (p, _reports)
    {
        if (p == report_ptr)
            return p->id;
    }

    return report_handle_t{0};
}

string_const_t report_name(report_t* report)
{
    FOUNDATION_ASSERT(report);
    return SYMBOL_CONST(report->name);
}

report_t** report_sort_alphabetically()
{
    report_t** sorted_reports = nullptr;
    foreach(r, _reports)
        array_push(sorted_reports, r);

    return array_sort(sorted_reports, [](const report_t* a, const report_t* b)
    {
        string_const_t ra = string_table_decode_const(a->name);
        string_const_t rb = string_table_decode_const(b->name);
        return string_compare(STRING_ARGS(ra), STRING_ARGS(rb));
    });
}

// 
// # SYSTEM
//

FOUNDATION_STATIC void report_initialize()
{
    char report_dir_path_buffer[BUILD_MAX_PATHLEN];
    string_t report_dir_path = session_get_user_file_path(
        STRING_BUFFER(report_dir_path_buffer), 
        STRING_ARGS(REPORTS_DIR_NAME), 
        nullptr, 0, 
        nullptr, 0, 
        false);
    
    if (!fs_make_directory(STRING_ARGS(report_dir_path)))
    {
        log_errorf(HASH_REPORT, ERROR_INTERNAL_FAILURE, 
            STRING_CONST("Reports directory at %.*s is not a directory"),
            STRING_FORMAT(report_dir_path));
    }

    if (main_is_interactive_mode())
    {
        log_infof(HASH_REPORT, STRING_CONST("Loading reports from %.*s"), STRING_FORMAT(report_dir_path));

        string_t* paths = fs_matching_files(STRING_ARGS(report_dir_path), STRING_CONST("^.*\\.json$"), false);
        foreach(e, paths)
        {
            char report_path_buffer[1024];
            string_t report_path = path_concat(STRING_BUFFER(report_path_buffer), STRING_ARGS(report_dir_path), STRING_ARGS(*e));
            if (!fs_is_file(STRING_ARGS(report_path)))
            {
                log_warnf(HASH_REPORT, WARNING_SUSPICIOUS,
                    STRING_CONST("Report file '%.*s' is not a file, skipping"),
                    STRING_FORMAT(report_path));
                continue;
            }
            report_load(string_to_const(report_path));
        }
        string_array_deallocate(paths);

        report_sort_order();

        module_register_tabs(HASH_REPORT, report_render_tabs);
        module_register_menu(HASH_REPORT, report_render_menus);
        module_register_window(HASH_REPORT, report_render_windows);
    }

    report_expression_columns_initialize();
}

FOUNDATION_STATIC void report_shutdown()
{
    report_expression_columns_finalize();
    
    for (int i = 0, end = array_size(_reports); i < end; ++i)
    {
        report_t& r = _reports[i];
        if (r.save)
            report_save(&r);

        table_deallocate(r.table);

        foreach (title, r.titles)
            title_deallocate(*title);
        array_deallocate(r.titles);
        array_deallocate(r.transactions);
        wallet_deallocate(r.wallet);
        config_deallocate(r.data);
        array_deallocate(r.expression_columns);
    }
    array_deallocate(_reports);
    _reports = nullptr;
}

DEFINE_MODULE(REPORT, report_initialize, report_shutdown, MODULE_PRIORITY_HIGH);
