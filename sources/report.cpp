/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "report.h"

#include "app.h"
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

#include <framework/imgui.h>
#include <framework/session.h>
#include <framework/table.h>
#include <framework/service.h>
#include <framework/tabs.h>
#include <framework/dispatcher.h>
#include <framework/math.h>
#include <framework/expr.h>
#include <framework/database.h>
#include <framework/console.h>
#include <framework/string.h>
 
#include <foundation/uuid.h>
#include <foundation/path.h>

#include <algorithm>

#define E32(FN, P1, P2, P3) L2(FN(P1, P2, P3))

typedef enum report_column_formula_enum_t : unsigned int {
    REPORT_FORMULA_NONE = 0,
    REPORT_FORMULA_TITLE,
    REPORT_FORMULA_CURRENCY,
    REPORT_FORMULA_PRICE,
    REPORT_FORMULA_DAY_CHANGE,
    REPORT_FORMULA_YESTERDAY_CHANGE,
    REPORT_FORMULA_BUY_QUANTITY,
    REPORT_FORMULA_TOTAL_GAIN,
    REPORT_FORMULA_TOTAL_GAIN_P,
    REPORT_FORMULA_TOTAL_FUNDAMENTAL,
    REPORT_FORMULA_ELAPSED_DAYS,
    REPORT_FORMULA_EXCHANGE_RATE,
    REPORT_FORMULA_DAY_GAIN,
    REPORT_FORMULA_TYPE,
    REPORT_FORMULA_PS,
    REPORT_FORMULA_ASK,
} report_column_formula_t;

struct report_expression_column_t
{
    char name[64];
    char expression[256];
    column_format_t format{ COLUMN_FORMAT_TEXT };
    mutable int store_counter{ 0 };
};

struct report_expression_cache_value_t
{
    hash_t key;
    tick_t time;
    column_format_t format;

    union {
        time_t date;
        double number;
        string_table_symbol_t symbol;
    };
};

static report_t* _reports = nullptr;
static bool* _last_show_ui_ptr = nullptr;
static string_const_t REPORTS_DIR_NAME = CTEXT("reports");

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL hash_t hash(const report_expression_cache_value_t& value)
{
    return value.key;
}

database<report_expression_cache_value_t>* _report_expression_cache;

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

FOUNDATION_STATIC report_handle_t report_get_handle(const report_t* report_ptr)
{
    foreach (p, _reports)
    {
        if (p == report_ptr)
            return p->id;
    }

    return report_handle_t{0};
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

    if (!report->show_sold_title)
    {
        for (int i = 0; i < report->active_titles; ++i)
        {
            title_t* title = report->titles[i];
            if (title_sold(title))
            {
                title_t* b = report->titles[report->active_titles - 1];
                report->titles[report->active_titles - 1] = title;
                report->titles[i--] = b;
                report->active_titles--;
            }
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

FOUNDATION_STATIC bool report_table_row_begin(table_t* table, row_t* row, table_element_ptr_t element)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr)
        return false;

    double real_time_elapsed_seconds = 0;

    const float decrease_timelapse = 60.0f * 25.0f;
    const float increase_timelapse = 60.0f * 25.0f;

    row->background_color = 0;

    if (row && title_is_index(t))
    {
        return (row->background_color = BACKGROUND_INDX_COLOR);
    }
    else if (title_sold(t))
    {
        return (row->background_color = BACKGROUND_SOLD_COLOR);
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

FOUNDATION_STATIC bool report_table_row_end(table_t* table, row_t* row, table_element_ptr_t element)
{
    if (element == nullptr)
        return false;

    return false;
}

FOUNDATION_STATIC void report_table_setup(report_handle_t report_handle, table_t* table)
{
    table->flags = ImGuiTableFlags_ScrollX
        | TABLE_SUMMARY
        | TABLE_HIGHLIGHT_HOVERED_ROW;

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

FOUNDATION_STATIC cell_t report_column_get_buy_price(table_element_ptr_t element, const column_t* column)
{
    const title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;

    const bool show_alternate_buy_price = report_column_show_alternate_data();

    if (!show_alternate_buy_price && title_sold(t))
        return cell_t(t->buy_adjusted_price);

    const double adjusted_price = math_ifzero(show_alternate_buy_price ? t->average_price_rated : t->average_price, t->buy_adjusted_price);

    cell_t buy_price_cell(adjusted_price);
    if (t->average_price_rated > adjusted_price)
    {
        buy_price_cell.style.types |= COLUMN_COLOR_TEXT;
        buy_price_cell.style.text_color = TEXT_WARN_COLOR;
    }
    else if (t->average_price_rated < 0)
    {
        buy_price_cell.style.types |= COLUMN_COLOR_TEXT;
        buy_price_cell.style.text_color = TEXT_GOOD_COLOR;
    }
    return buy_price_cell;
}

FOUNDATION_STATIC cell_t report_column_get_ask_price(table_element_ptr_t element, const column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;

    // If all titles are sold, return the sold average price.
    if (title_sold(t))
        return t->sell_adjusted_price;

    if (t->average_ask_price > 0)
    {
        cell_t ask_price_cell(t->average_ask_price);
        ask_price_cell.style.types |= COLUMN_COLOR_TEXT;
        ask_price_cell.style.text_color = TEXT_WARN_COLOR;
        return ask_price_cell;
    }

    const double ask_price = t->ask_price.fetch();
    const double avg = math_ifzero(t->average_price, t->stock->current.adjusted_close);
    const double c_avg = t->stock->current.adjusted_close;
    const double average_fg = (t->average_price + t->stock->current.adjusted_close) / 2.0;
    const double if_gain_price = average_fg * (1.0 + t->wallet->profit_ask - (t->elapsed_days - t->wallet->average_days) / 20.0 / 100.0);

    if (!math_real_is_nan(ask_price) && ask_price < t->average_price)
    {
        cell_t ask_price_cell(ask_price);
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

FOUNDATION_STATIC void report_title_ask_price_gain_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t) || !t->stock || math_real_is_nan(cell->number))
    {
        ImGui::CloseCurrentPopup();
        return;
    }

    const double avg = math_ifzero(t->average_price, t->stock->current.adjusted_close);
    const double c_avg = t->stock->current.adjusted_close;
    const double average_fg = (t->average_price + t->stock->current.adjusted_close) / 2.0;
    const double if_gain_price = average_fg * (1.0 + t->wallet->profit_ask - (t->elapsed_days - t->wallet->average_days) / 20.0 / 100.0);
    if (!math_real_is_nan(avg))
    {
        if (t->average_quantity == 0 && math_ifnan(t->sell_total_adjusted_qty, 0) > 0)
        {
            const double sell_gain_diff = (t->sell_adjusted_price - t->stock->current.adjusted_close) * t->sell_total_adjusted_qty;
            ImGui::TextColored(ImColor(sell_gain_diff < 0 ? TEXT_BAD_COLOR : TOOLTIP_TEXT_COLOR), " %s %.*s ",
                sell_gain_diff > 0 ? "Saved" : "Lost", STRING_FORMAT(string_from_currency(math_abs(sell_gain_diff), "999 999 999 $")));
        }
        ImGui::TextColored(ImColor(TOOLTIP_TEXT_COLOR),
            " Bought Price Gain: %.3g %% \n"
            " Current Price Gain: %.3g %% (" ICON_MD_EXPOSURE " %.2lf $) \n"
            " Ask Safe Price Gain: %.2lf $ (" ICON_MD_EXPOSURE " %.3g %%) \n",
            (cell->number - avg) / avg * 100.0,
            (cell->number - c_avg) / c_avg * 100.0, (cell->number - c_avg),
            if_gain_price, (cell->number - if_gain_price) / if_gain_price * -100.0);
    }
    else
    {
        ImGui::TextUnformatted("Data not available");
    }
}

FOUNDATION_STATIC cell_t report_column_earning_actual(table_element_ptr_t element, const column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;
    return t->stock->earning_trend_actual.fetch();
}

FOUNDATION_STATIC cell_t report_column_earning_estimate(table_element_ptr_t element, const column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;
    return t->stock->earning_trend_estimate.fetch();
}

FOUNDATION_STATIC cell_t report_column_earning_difference(table_element_ptr_t element, const column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;
    return t->stock->earning_trend_difference.fetch();
}

FOUNDATION_STATIC cell_t report_column_earning_percent(table_element_ptr_t element, const column_t* column)
{
    title_t* t = *(title_t**)element;
    if (t == nullptr || title_is_index(t))
        return nullptr;
    return (double)math_round(t->stock->earning_trend_percent.fetch());
}

FOUNDATION_STATIC cell_t report_column_get_value(table_element_ptr_t element, const column_t* column, report_column_formula_t formula)
{
    title_t* t = *(title_t**)element;

    if ((column->flags & COLUMN_COMPUTE_SUMMARY) && title_is_index(t))
        return nullptr;

    if (t == nullptr)
        return nullptr;

    switch (formula)
    {
    case REPORT_FORMULA_TITLE:
        return t->code;

    case REPORT_FORMULA_BUY_QUANTITY:
        return (double)math_round(t->average_quantity);

    case REPORT_FORMULA_ELAPSED_DAYS:
        return t->elapsed_days;

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
        case REPORT_FORMULA_DAY_CHANGE:	return stock_data->current.change_p;

        case REPORT_FORMULA_DAY_GAIN:			return title_get_day_change(t, stock_data);
        case REPORT_FORMULA_TOTAL_GAIN:			return title_get_total_gain(t);
        case REPORT_FORMULA_TOTAL_GAIN_P:		return title_get_total_gain_p(t);
        case REPORT_FORMULA_YESTERDAY_CHANGE:	return title_get_yesterday_change(t, stock_data);

        default:
            FOUNDATION_ASSERT_FAILFORMAT("Cannot get %.*s value for %.*s (%u)", STRING_FORMAT(column->get_name()), (int)t->code_length, t->code, formula);
            break;
        }
    }

    return cell_t();
}

FOUNDATION_STATIC void report_column_title_context_menu(report_handle_t report_handle, table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    const title_t* title = *(const title_t**)element;

    ImGui::MoveCursor(8.0f, 4.0f);
    if (ImGui::MenuItem("Buy"))
        ((title_t*)title)->show_buy_ui = true;

    ImGui::MoveCursor(8.0f, 2.0f);
    if (ImGui::MenuItem("Sell"))
        ((title_t*)title)->show_sell_ui = true;

    ImGui::MoveCursor(8.0f, 2.0f);
    if (ImGui::MenuItem("Details"))
        ((title_t*)title)->show_details_ui = true;

    ImGui::Separator();

    ImGui::MoveCursor(8.0f, 2.0f);
    if (ImGui::MenuItem("Remove"))
        report_title_remove(report_handle, title);

    ImGui::Separator();

    ImGui::MoveCursor(8.0f, 2.0f);
    if (ImGui::MenuItem("Load Pattern"))
        pattern_open(title->code, title->code_length);

    ImGui::MoveCursor(8.0f, 2.0f);
    if (ImGui::MenuItem("Read News"))
        news_open_window(title->code, title->code_length);

    ImGui::MoveCursor(0.0f, 2.0f);
}

FOUNDATION_STATIC cell_t report_column_draw_title(table_element_ptr_t element, const column_t* column)
{
    title_t* title = *(title_t**)element;

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
                space.x > 225.0f)
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

            const float max_width = ImGui::GetContentRegionAvail().x - button_width - imgui_get_font_ui_scale(2.0f);
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
            if (button_width < space_left + 15.0f)
            {
                ImGui::MoveCursor(space_left - button_width - style.FramePadding.x / 2.0f, 1.0f, true);
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
            ImGui::TextUnformatted(formatted_code);
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                pattern_open(title->code, title->code_length);

            float logo_size = button_width;
            float space_left = ImGui::GetContentRegionAvail().x - code_width;
            ImGui::MoveCursor(space_left - button_width - logo_size + 10.0f, 0, true);
            ImVec2 logo_size_v = ImVec2(logo_size, logo_size);
            if (ImGui::GetCursorPos().x < code_width || !logo_render_icon(title->code, title->code_length, logo_size_v, true, true))
                ImGui::Dummy(ImVec2(logo_size, logo_size));
            else
                ImGui::Dummy(ImVec2(logo_size, logo_size));

            space_left = ImGui::GetContentRegionAvail().x - code_width;
            if (button_width < space_left + 35.0f)
            {
                ImGui::MoveCursor(-14.0f, 1.0f, true);
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

    return title->code;
}

FOUNDATION_STATIC cell_t report_column_get_change_value(table_element_ptr_t element, const column_t* column, int rel_days)
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

FOUNDATION_STATIC cell_t report_column_get_dividends_yield(table_element_ptr_t element, const column_t* column)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return DNAN;

    const stock_t* s = title->stock;
    if (s == nullptr)
        return DNAN;
        
    return s->dividends_yield.fetch() * 100.0f;
}

FOUNDATION_STATIC cell_t report_column_evaluate_expression(table_element_ptr_t element, const column_t* column, 
                                                           report_handle_t report_handle, const report_expression_column_t* ec)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr || title_is_index(title))
        return DNAN;
        
    report_t* report = report_get(report_handle);
    string_const_t report_name = SYMBOL_CONST(report->name);
    const size_t expression_length = string_length(ec->expression);
    hash_t key = hash_combine(
        string_hash(STRING_ARGS(report_name)), 
        string_hash(title->code, title->code_length), 
        string_hash(ec->expression, expression_length));

    report_expression_cache_value_t cvalue;
    if (_report_expression_cache->select(key, cvalue))
    {
        if (cvalue.format == ec->format)
        {
            if (ec->format == COLUMN_FORMAT_DATE)
                return cvalue.date;
            if (ec->format == COLUMN_FORMAT_CURRENCY || ec->format == COLUMN_FORMAT_NUMBER || ec->format == COLUMN_FORMAT_PERCENTAGE)
                return cvalue.number;
            return SYMBOL_CONST(cvalue.symbol);
        }
    }

    if (!title_is_resolved(title) || report_is_loading(report))
        return DNAN;

    cvalue.key = key;
    cvalue.format = ec->format;
    cvalue.time = time_current();
    
    expr_set_or_create_global_var(STRING_CONST("$TITLE"), expr_result_t(title->code));
    auto result = eval(ec->expression, expression_length);
    if (ec->format == COLUMN_FORMAT_CURRENCY || ec->format == COLUMN_FORMAT_NUMBER || ec->format == COLUMN_FORMAT_PERCENTAGE)
    { 
        cvalue.number = result.as_number();
        if (ec->store_counter++ > 0)
            _report_expression_cache->put(cvalue);
        return cvalue.number;
    }
    if (ec->format == COLUMN_FORMAT_DATE)
    {
        cvalue.date = (time_t)result.as_number();
        if (ec->store_counter++ > 0)
            _report_expression_cache->put(cvalue);
        return cvalue.date;
    }
    
    string_const_t str_value = result.as_string();
    cvalue.symbol = string_table_encode(str_value);
    if (ec->store_counter++ > 0)
        _report_expression_cache->put(cvalue);
    return str_value;
}

FOUNDATION_STATIC cell_t report_column_get_fundamental_value(table_element_ptr_t element, const column_t* column, const char* filter_name, size_t filter_name_length)
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

FOUNDATION_STATIC cell_t report_column_get_total_investment(table_element_ptr_t element, const column_t* column)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return nullptr;

    if (report_column_show_alternate_data())
        return title->buy_total_price_rated;

    return title_get_total_investment(title);
}

FOUNDATION_STATIC cell_t report_column_get_total_value(table_element_ptr_t element, const column_t* column)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return nullptr;

    return title_get_total_value(title);
}

FOUNDATION_STATIC cell_t report_column_get_name(table_element_ptr_t element, const column_t* column)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return nullptr;
    return title->stock->name;
}

FOUNDATION_STATIC cell_t report_column_get_date(table_element_ptr_t element, const column_t* column)
{
    const title_t* t = *(title_t**)element;
    if (t == nullptr)
        return nullptr;
    return t->date_average;
}

FOUNDATION_STATIC void report_title_pattern_open(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;
    pattern_open(title->code, title->code_length);
}

FOUNDATION_STATIC void report_title_open_details_view(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;
    title->show_details_ui = true;
}

FOUNDATION_STATIC void report_title_day_change_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
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
        string_const_t last_update = string_from_time_static(tick_updated, true);
        ImGui::AlignTextToFramePadding();
        ImGui::Text(" Updated %.0lf %s(s) ago (%.*s) \n %.*s [%.*s] -> %.2lf $ (%.3lg %%) ", 
            elapsed_time_updated, time_elapsed_unit, STRING_FORMAT(last_update),
            STRING_FORMAT(name), (int)title->code_length, title->code, 
            s->current.close, s->current.change_p);
        ImGui::Spacing();
    }
   
    realtime_render_graph(title->code, title->code_length, time_add_hours(time_now(), -time_lapse_hours), 1300.0f, 600.0f);
}

FOUNDATION_STATIC void report_title_live_price_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;
    eod_fetch("real-time", title->code, FORMAT_JSON_CACHE, "s", title->code, [title](const json_object_t& json)
    {
        const stock_t* s = title->stock;
        string_const_t time_str = string_from_time_static((tick_t)(json["timestamp"].as_number() * 1000.0), true);
        
        if (s == nullptr || time_str.length == 0)
        {
            return ImGui::Text(" %s (%s) \n Data not available \n",
                title->code, string_table_decode(title->stock->name));
        }

        day_result_t d{};
        const double old_price = s->current.adjusted_close;
        stock_index_t stock_index = ::stock_index(title->code, title->code_length);
        stock_read_real_time_results(stock_index, json, d);
        
        ImGui::TextColored(ImColor(TOOLTIP_TEXT_COLOR), " %s (%s) \n %.*s \n"
            "\tPrice %.2lf $\n"
            "\tOpen: %.2lf $\n"
            "\tChange: %.2lf $ (%.3g %%)\n"
            "\tYesterday: %.2lf $ (%.3g %%)\n"
            "\tLow %.2lf $\n"
            "\tHigh %.2lf $ (%.3g %%)\n"
            "\tDMA (50d) %.2lf $ (%.3g %%)\n"
            "\tDMA (200d) %.2lf $ (%.3g %%)\n"
            "\tVolume %.6g (%.*s)", title->code, string_table_decode(s->name), STRING_FORMAT(time_str),
            d.close,
            d.open,
            d.close - d.open, (d.close - d.open) / d.open * 100.0,
            d.previous_close, (d.close - d.previous_close) / d.previous_close * 100.0,
            d.low,
            d.high, (d.high - d.low) / d.close * 100.0,
            s->dma_50, s->dma_50 / d.close * 100.0,
            s->dma_200, s->dma_200 / s->high_52 * 100.0,
            d.volume, STRING_FORMAT(string_from_currency(d.volume * d.change, "9 999 999 999 $")));
        if (d.close != old_price)
            title_refresh(title);
    }, 60ULL);

    time_t since = time_add_days(time_now(), -9);
    title_get_last_transaction_date(title, &since);
    
    realtime_render_graph(title->code, title->code_length, since, max(ImGui::GetContentRegionAvail().x, 900.0f), 300.0f);
}

FOUNDATION_STATIC void report_title_price_alerts_formatter(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)
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
        //style.text_color = ImColor::HSV(130 / 360.0f, 0.94f, 0.04f);
        style.background_color = ImColor::HSV(130 / 360.0f, 0.94f, 0.94f); // hsv(176, 94%, 94%)
        style.text_color = imgui_color_text_for_background(style.background_color);
    }
    else if (title->average_price > 0 && current_price >= (title->average_price * (1.0 + title->wallet->profit_ask)))
    {
        style.types |= COLUMN_COLOR_BACKGROUND | COLUMN_COLOR_TEXT;
        //style.text_color = ImColor::HSV(130 / 360.0f, 0.94f, 0.04f);
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

FOUNDATION_STATIC void report_title_total_gain_alerts_formatter(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)
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

FOUNDATION_STATIC void report_title_total_gain_p_alerts_formatter(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)
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

FOUNDATION_STATIC void report_title_gain_total_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    const title_t* t = *(const title_t**)element;
    if (t == nullptr)
        return;

    const double total_value = title_get_total_value(t);
    ImGui::Text(" Total Investment %12s ", string_from_currency(title_get_total_investment(t)).str);
    ImGui::Text(" Total Value      %12s ", string_from_currency(total_value).str);

    if (t->average_exchange_rate != 1.0 && t->average_quantity > 0)
    {
        const double exchange_diff = t->today_exchange_rate.fetch() - t->average_exchange_rate;
        ImGui::Text(" Exchange Gain    %12s ", string_from_currency(exchange_diff * total_value).str);
    }
}

FOUNDATION_STATIC void report_title_adjusted_price_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    const title_t* t = *(const title_t**)element;
    if (t == nullptr)
        return;

    const double bought_price = title_get_bought_price(t);
    ImGui::TextColored(ImColor(TOOLTIP_TEXT_COLOR),
        " (%s $) Bought Price: %.2lf $ ",
        string_table_decode(t->stock->currency), bought_price);

    ImGui::TextColored(ImColor(TOOLTIP_TEXT_COLOR),
        " (%.*s $) Average Cost: %.3lf $ ",
        STRING_FORMAT(t->wallet->preferred_currency), math_ifzero(t->average_price_rated, 0));

    if (t->buy_adjusted_price != bought_price)
    {
        ImGui::TextColored(ImColor(TOOLTIP_TEXT_COLOR),
            " (Split) Adjusted Price: %.2lf $ ",
            math_ifzero(t->buy_adjusted_price, 0));
    }
}

FOUNDATION_STATIC void report_title_dividends_total_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;

    const double avg = math_ifzero(title->average_price, title->stock->current.adjusted_close);
    ImGui::TextColored(ImColor(TOOLTIP_TEXT_COLOR), "Total Dividends %.2lf $", title->total_dividends);
}

FOUNDATION_STATIC void report_title_open_buy_view(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;

    title->show_buy_ui = true;
}

FOUNDATION_STATIC void report_title_open_sell_view(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    title_t* title = *(title_t**)element;
    if (title == nullptr)
        return;

    if (title->average_quantity == 0)
        title->show_details_ui = true;
    else
        title->show_sell_ui = true;
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
    default:
        return "String";
    }
}

FOUNDATION_STATIC void report_table_add_default_columns(report_handle_t report_handle, table_t* table)
{
    auto& ctitle = table_add_column(table, STRING_CONST("Title"),
        report_column_draw_title, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_FREEZE | COLUMN_CUSTOM_DRAWING)
        .set_context_menu_callback(L3(report_column_title_context_menu(report_handle, _1, _2, _3)));

    table_add_column(table, STRING_CONST(ICON_MD_BUSINESS " Name"),
        report_column_get_name, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);

    table_add_column(table, STRING_CONST(ICON_MD_TODAY " Date"),
        report_column_get_date, COLUMN_FORMAT_DATE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ZERO_USE_DASH)
        .set_selected_callback(report_title_open_details_view);

    table_add_column(table, STRING_CONST("  " ICON_MD_NUMBERS "||" ICON_MD_NUMBERS " Quantity"),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_BUY_QUANTITY), COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_NUMBER_ABBREVIATION | COLUMN_ZERO_USE_DASH)
        .set_selected_callback(report_title_open_details_view);

    table_add_column(table, STRING_CONST("   Buy " ICON_MD_LOCAL_OFFER "||" ICON_MD_LOCAL_OFFER " Average Cost"),
        report_column_get_buy_price, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_SUMMARY_AVERAGE | COLUMN_ZERO_USE_DASH)
        .set_selected_callback(report_title_open_buy_view)
        .set_tooltip_callback(report_title_adjusted_price_tooltip);

    table_add_column(table, STRING_CONST(" Price " ICON_MD_MONETIZATION_ON "||" ICON_MD_MONETIZATION_ON " Market Price"),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_PRICE), COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_DYNAMIC_VALUE | COLUMN_SUMMARY_AVERAGE | COLUMN_ZERO_USE_DASH)
        .set_selected_callback(report_title_open_details_view)
        .set_tooltip_callback(report_title_live_price_tooltip)
        .set_style_formatter(report_title_price_alerts_formatter);

    table_add_column(table, STRING_CONST("   Ask " ICON_MD_PRICE_CHECK "||" ICON_MD_PRICE_CHECK " Ask Price"),
        report_column_get_ask_price, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_DYNAMIC_VALUE | COLUMN_SUMMARY_AVERAGE | COLUMN_ZERO_USE_DASH)
        .set_selected_callback(report_title_open_sell_view)
        .set_tooltip_callback(report_title_ask_price_gain_tooltip);

    table_add_column(table, STRING_CONST("   Day " ICON_MD_ATTACH_MONEY "||" ICON_MD_ATTACH_MONEY " Day Gain. "),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_DAY_GAIN), COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE)
        .set_tooltip_callback(report_title_day_change_tooltip);

    table_add_column(table, STRING_CONST("PS " ICON_MD_TRENDING_UP "||" ICON_MD_TRENDING_UP " Prediction Sensor"),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_PS), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_ROUND_NUMBER | COLUMN_DYNAMIC_VALUE)
        .set_selected_callback(report_title_pattern_open);

#if 0
    table_add_column(table, STRING_CONST("E. Actual " ICON_MD_TRENDING_UP "||" ICON_MD_TRENDING_UP " Earning Actual"),
        report_column_earning_actual, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE);

    table_add_column(table, STRING_CONST("E. Estimate " ICON_MD_TRENDING_UP "||" ICON_MD_TRENDING_UP " Earning Estimate"),
        report_column_earning_estimate, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE);

    table_add_column(table, STRING_CONST("E. Diff. " ICON_MD_TRENDING_NEUTRAL "||" ICON_MD_TRENDING_NEUTRAL " Earning Difference"),
        report_column_earning_difference, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE);
#endif

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
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_CURRENCY), COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_CENTER_ALIGN);
    table_add_column(table, STRING_CONST("   " ICON_MD_CURRENCY_EXCHANGE "||" ICON_MD_CURRENCY_EXCHANGE " Exchange Rate"),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_EXCHANGE_RATE), COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | COLUMN_SUMMARY_AVERAGE);

    table_add_column(table, STRING_CONST(" R. " ICON_MD_ASSIGNMENT_RETURN "||" ICON_MD_ASSIGNMENT_RETURN " Return Rate (Yield)"),
        L2(report_column_get_dividends_yield(_1, _2)), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_ZERO_USE_DASH)
        .set_tooltip_callback(report_title_dividends_total_tooltip);

    table_add_column(table, STRING_CONST("      I. " ICON_MD_SAVINGS "||" ICON_MD_SAVINGS " Total Investments (based on average cost)"),
        report_column_get_total_investment, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ROUND_NUMBER | COLUMN_ZERO_USE_DASH);
    table_add_column(table, STRING_CONST("      V. " ICON_MD_ACCOUNT_BALANCE_WALLET "||" ICON_MD_ACCOUNT_BALANCE_WALLET " Total Value (as of today)"),
        report_column_get_total_value, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ROUND_NUMBER | COLUMN_ZERO_USE_DASH);

    table_add_column(table, STRING_CONST("   Gain " ICON_MD_DIFFERENCE "||" ICON_MD_DIFFERENCE " Total Gain (as of today)"),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_TOTAL_GAIN), COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE/* | COLUMN_ROUND_NUMBER*/)
        .set_style_formatter(report_title_total_gain_alerts_formatter)
        .set_tooltip_callback(report_title_gain_total_tooltip);
    table_add_column(table, STRING_CONST("  % " ICON_MD_PRICE_CHANGE "||" ICON_MD_PRICE_CHANGE " Total Gain % "),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_TOTAL_GAIN_P), COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_ROUND_NUMBER)
        .set_style_formatter(report_title_total_gain_p_alerts_formatter);

    table_add_column(table, STRING_CONST(ICON_MD_INVENTORY " Type    "),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_TYPE), COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE);
    table_add_column(table, STRING_CONST(ICON_MD_STORE " Sector"),
        E32(report_column_get_fundamental_value, _1, _2, STRING_CONST("General.Sector|Category|Type")), COLUMN_FORMAT_TEXT, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_SEARCHABLE)
        .width = 200.0f;

    table_add_column(table, STRING_CONST(" " ICON_MD_DATE_RANGE "||" ICON_MD_DATE_RANGE " Elapsed Days"),
        E32(report_column_get_value, _1, _2, REPORT_FORMULA_ELAPSED_DAYS), COLUMN_FORMAT_NUMBER,
        COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_SUMMARY_AVERAGE | COLUMN_ROUND_NUMBER | COLUMN_MIDDLE_ALIGN);

    // Add custom expression columns
    report_t* report = report_get(report_handle);
    foreach(c, report->expression_columns)
    {
        string_const_t column_name = string_format_static(STRING_CONST("%s||" ICON_MD_VIEW_COLUMN " %s (%.*s)"),
            c->name, c->name, min(16, (int)string_length(c->expression)), c->expression);
        table_add_column(table, STRING_ARGS(column_name), LC2(report_column_evaluate_expression(_1, _2, report_handle, c)), c->format, 
            COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | (c->format == COLUMN_FORMAT_TEXT ? COLUMN_SEARCHABLE : COLUMN_OPTIONS_NONE));
    }
}

FOUNDATION_STATIC bool report_render_expression_columns_dialog(void* user_data)
{
    report_t* report = (report_t*)user_data;
    FOUNDATION_ASSERT(report);

    if (!ImGui::BeginTable("Columns", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY))
        return false;

    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None);
    ImGui::TableSetupColumn("Expression||Macros:\n"
        "$TITLE: Represents the active title symbol code, i.e. \"ZM.US\"\n"
        "$REPORT: Represents the active report name, i.e. \"MyReport\"\n\n"
        "Double click the input field to edit and test in the console window", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_None);
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
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::InputText("##Name", c->name, sizeof(c->name), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                update_table = true;
            }
        }
        
        // Expression field
        if (ImGui::TableNextColumn())
        {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputText("##Expression", c->expression, sizeof(c->expression));
            if (ImGui::IsMouseDoubleClicked(0) && ImGui::IsItemHovered())
            {
                console_set_expression(c->expression, string_length(c->expression));
            }
        }

        // Format selector
        if (ImGui::TableNextColumn())
        {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
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
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            ImGui::InputTextWithHint("##Name", "Column name", name, sizeof(name));
        }

        // Expression
        if (ImGui::TableNextColumn())
        {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::InputTextWithHint("##Expression", "Expression i.e. S(GFL.TO, open)", expression, sizeof(expression), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                add = true;
            }
        }

        // Format selector
        if (ImGui::TableNextColumn())
        {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
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
        report->dirty = true;
        table_clear_columns(report->table);
        report_table_add_default_columns(report_get_handle(report), report->table);
    }

    ImGui::EndTable();
    return true;
}

FOUNDATION_STATIC void report_open_expression_columns_dialog(report_t* report)
{
    app_open_dialog(ICON_MD_DASHBOARD_CUSTOMIZE " Expression Columns", report_render_expression_columns_dialog, 900U, 400U, true, report, nullptr);
}

FOUNDATION_STATIC void report_table_context_menu(report_handle_t report_handle, table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    if (element == nullptr)
    {
        report_t* report = report_get(report_handle);
        if (ImGui::MenuItem(ICON_MD_ADD " Add title"))
            report->show_add_title_ui = true;

        if (ImGui::MenuItem(ICON_MD_DASHBOARD_CUSTOMIZE " Expression Columns"))
            report_open_expression_columns_dialog(report);
    }
    else
    {
        ImGui::CloseCurrentPopup();
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
    string_const_t report_save_file = report_get_save_file_path(report);
    if (fs_is_file(STRING_ARGS(report_save_file)))
        fs_remove_file(STRING_ARGS(report_save_file));
}

FOUNDATION_STATIC void report_toggle_show_summary(report_t* report)
{
    report->show_summary = !report->show_summary;
    report_summary_update(report);
}

FOUNDATION_STATIC void report_render_summary_info(report_t* report, const char* field_name, double value, const char* fmt, bool negative_parens = false)
{
    ImGui::TableNextRow();
    ImGui::TableNextColumn(); 
    ImGui::TextWrapped("%s", field_name);
    
    ImGui::TableNextColumn(); 
    if (negative_parens && value < 0)
    {
        string_const_t formatted_value = string_from_currency(-1.0 * value, fmt);
        string_const_t formatted_label = string_format_static(STRING_CONST("(%.*s)"), STRING_FORMAT(formatted_value));
        table_cell_right_aligned_label(STRING_ARGS(formatted_label));
    }
    else
        table_cell_right_aligned_label(STRING_ARGS(string_from_currency(value, fmt)));
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

    ImVec2 space = ImGui::GetContentRegionAvail();
    if (!ImGui::BeginTable("Report Summary 9", 2, flags))
        return;

    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4());

    ImGui::TableSetupColumn(ICON_MD_WALLET " Wallet", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderWidth, 260.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize | ImGuiTableColumnFlags_NoHeaderLabel, 0, 0, [](const char* name, void* payload)
    {
        ImGui::MoveCursor(ImGui::GetContentRegionAvail().x - 34.0f, 0);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.6f, 0.4f, 0.4f, 0.5f));
        if (ImGui::Selectable(ICON_MD_CLOSE))
        {
            report_t* r = (report_t*)payload;
            r->show_summary = false;
        }
        ImGui::PopStyleColor(1);
    }, report);
        
    ImGui::TableHeadersRow();

    static const char* currency_fmt = "-9 999 999.99 $";
    static const char* pourcentage_fmt = "-9999.99 %";
    static const char* integer_fmt = "-9 999 999  ";

    if (wallet_draw(report->wallet, space.x))
    {
        report->dirty = true;
        report_refresh(report);
    }    

    ImGui::TableNextRow();
    report_render_summary_info(report, "Target", report->wallet->target_ask * 100.0, pourcentage_fmt);
    report_render_summary_info(report, "Profit", report->wallet->profit_ask * 100.0, pourcentage_fmt);
    report_render_summary_info(report, "Avg. Days", report->wallet->average_days, integer_fmt);

    ImGui::TableNextRow();
    string_const_t user_preferred_currency = string_const(SETTINGS.preferred_currency);
    const double today_exchange_rate = stock_exchange_rate(STRING_CONST("USD"), STRING_ARGS(user_preferred_currency));
    report_render_summary_info(report, string_format_static_const("USD%s", SETTINGS.preferred_currency), today_exchange_rate, currency_fmt);
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
            ImGui::SetTooltip(" Average Rate (USD): %.2lf $ \n Based on the average acquisition time of every titles (%.0lf). ", 
                average_rate, average_count);
    }

    ImGui::TableNextRow();
    report_render_summary_info(report, "Daily average", report->total_daily_average_p, pourcentage_fmt, true);
    ImGui::PushStyleColor(ImGuiCol_Text, report->total_day_gain > 0 ? TEXT_GOOD_COLOR : TEXT_WARN_COLOR);
    report_render_summary_info(report, "Day Gain", report->total_day_gain, currency_fmt, true);
    ImGui::PopStyleColor(1);

    ImGui::TableNextRow();
    const double capital = max(0.0, report->wallet->funds - report->total_investment);
    report_render_summary_info(report, "Dividends", report->wallet->total_dividends, currency_fmt);

    if (report->wallet->funds > 0)
        report_render_summary_info(report, "Capital", max(0.0, report->wallet->funds - report->total_investment + report->wallet->sell_total_gain), currency_fmt);

    const double total_gain_with_sells = report->total_gain + report->wallet->sell_total_gain;

    if (report->wallet->total_title_sell_count > 0)
    {
        report_render_summary_info(report, "Enhanced earnings", report->wallet->enhanced_earnings, currency_fmt);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Minimal amount (%.2lf) to sell titles if you want to increase your gain considerably.", report->wallet->total_sell_gain_if_kept);

        ImGui::TableNextRow();
        report_render_summary_info(report, "Sell Count", report->wallet->total_title_sell_count, integer_fmt);
        report_render_summary_info(report, "Sell Total", report->wallet->sell_total_gain, currency_fmt, true);
        report_render_summary_info(report, "Sell Average", report->wallet->sell_gain_average, currency_fmt, true);

        ImGui::PushStyleColor(ImGuiCol_Text, report->wallet->total_sell_gain_if_kept_p <= 0 ? TEXT_GOOD_COLOR : TEXT_WARN_COLOR);
        ImGui::BeginGroup();
        report_render_summary_info(report, "Sell Greediness", report->wallet->total_sell_gain_if_kept_p * 100.0, pourcentage_fmt, true);
        report_render_summary_info(report, "", report->wallet->total_sell_gain_if_kept, currency_fmt, true);
        ImGui::EndGroup();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(" Loses or (Gains) if titles were kept longer before being sold");
        report_render_summary_info(report, "Sell Profit (Loses) or Gain", total_gain_with_sells - report->wallet->total_sell_gain_if_kept, currency_fmt, true);
        ImGui::PopStyleColor(1);
    }

    ImGui::TableNextRow();
    report_render_summary_info(report, "Investments", report->total_investment, currency_fmt);
    report_render_summary_info(report, "Total Value", report->total_value, currency_fmt);

    ImGui::PushStyleColor(ImGuiCol_Text, report->total_gain > 0 ? TEXT_GOOD_COLOR : TEXT_WARN_COLOR);
    report_render_summary_info(report, "Total Gain", report->total_gain, currency_fmt, true);
    ImGui::PopStyleColor(1);

    ImGui::PushStyleColor(ImGuiCol_Text, total_gain_with_sells > 0 ? TEXT_GOOD_COLOR : TEXT_BAD_COLOR);
    if (report->wallet->sell_total_gain != 0)
    {
        report_render_summary_info(report, "", total_gain_with_sells, currency_fmt, true);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Gain including previous sells (%.2lf $)", report->wallet->sell_total_gain);
    }

    const double gain_p = total_gain_with_sells / report->total_investment * 100.0;
    report_render_summary_info(report, "", math_ifnan(gain_p, report->total_gain_p * 100.0), pourcentage_fmt, true);
    ImGui::PopStyleColor(1);

    ImGui::TableNextRow();
    if (report_is_loading(report))
        report_render_summary_info(report, "Loading data...", NAN, nullptr);

    ImGui::PopStyleColor(1);

    ImGui::EndTable();
}

FOUNDATION_STATIC void report_render_add_title_from_ui(report_t* report, string_const_t code)
{
    title_t* new_title = report_title_add(report, code);
    new_title->show_buy_ui = true;
    report->show_add_title_ui = false;
}

FOUNDATION_STATIC string_const_t report_render_input_dialog(string_const_t title, string_const_t apply_label, string_const_t initial_value, string_const_t hint, bool* show_ui)
{
    if (!report_render_dialog_begin(title, show_ui, ImGuiWindowFlags_NoResize))
        return string_null();

    bool applied = false;
    bool can_apply = false;
    static char input[64] = { '\0' };
    size_t input_length = 0;

    if (ImGui::IsWindowAppearing())
    {
        input_length = string_copy(STRING_BUFFER(input), STRING_ARGS(initial_value)).length;
    }

    ImGui::MoveCursor(2, 10);
    if (ImGui::BeginChild("##Content", ImVec2(560.0f, 125.0f), false))
    {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        if (ImGui::InputTextEx("##InputField", hint.str, STRING_BUFFER(input), ImVec2(-1, 0),
            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue))
        {
            applied = true;
        }

        input_length = string_length(input);
        if (input_length > 0)
            can_apply = true;

        ImGui::MoveCursor(0, 10);
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::SameLine(ImGui::GetWindowWidth() - 226);
        if (ImGui::Button("Cancel"))
        {
            applied = false;
            *show_ui = false;
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(!can_apply);
        if (ImGui::Button(apply_label.str))
            applied = true;
        ImGui::EndDisabled();

        if (can_apply && applied)
            *show_ui = false;
    } ImGui::EndChild();

    report_render_dialog_end();
    if (can_apply && applied)
        return string_const(input, input_length);

    return string_null();
}

FOUNDATION_STATIC void report_render_rename_dialog(report_t* report)
{
    string_const_t current_name = string_table_decode_const(report->name);
    string_const_t name = report_render_input_dialog(CTEXT("Rename##1"), CTEXT("Apply"), current_name, 
        current_name, &report->show_rename_ui);
    if (!string_is_null(name))
        report_rename(report, name);
}

FOUNDATION_STATIC void report_render_add_title_dialog(report_t* report)
{
    ImGui::SetNextWindowSize(ImVec2(1200, 600), ImGuiCond_Once);

    string_const_t popup_id = string_format_static(STRING_CONST("Add Title (%.*s)##5"), STRING_FORMAT(string_table_decode_const(report->name)));
    if (report_render_dialog_begin(popup_id, &report->show_add_title_ui, ImGuiWindowFlags_None))
    {
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        symbols_render_search(L1(report_render_add_title_from_ui(report, _1)));

        report_render_dialog_end();
    }
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

    //TIME_TRACKER(50.0, "report_initial_sync");

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

    foreach (title, report->titles)
        title_refresh(*title);
    report_summary_update(report);
    log_infof(HASH_REPORT, STRING_CONST("Fully resolved %s"), string_table_decode(report->name));
    if (report->table)
        report->table->needs_sorting = true;

    report->fully_resolved = 1;
    return fully_resolved;
}

FOUNDATION_STATIC report_handle_t report_allocate(const char* name, size_t name_length, const config_handle_t& data)
{
    if (_reports == nullptr)
        array_reserve(_reports, 1);

    string_table_symbol_t name_symbol = string_table_encode(name, name_length);

    for (int i = 0, end = array_size(_reports); i < end; ++i)
    {
        const report_t& r = _reports[i];
        if (r.name == name_symbol)
            return r.id;
    }

    _reports = array_push(_reports, report_t{ name_symbol });
    unsigned int report_index = array_size(_reports) - 1;
    report_t* report = &_reports[report_index];

    // Ensure default structure
    report->data = data ? data : config_allocate(CONFIG_VALUE_OBJECT);
    report->wallet = wallet_allocate(report->data["wallet"]);

    auto cid = report->data["id"];
    auto cname = config_set(report->data, STRING_CONST("name"), name, name_length);
    auto ctitles = config_set_object(report->data, STRING_CONST("titles"));

    if (cid)
    {
        report->id = string_to_uuid(STRING_ARGS(cid.as_string()));
    }
    else
    {
        report->id = uuid_generate_time();

        string_const_t id_str = string_from_uuid_static(report->id);
        cid = config_set(report->data, STRING_CONST("id"), STRING_ARGS(id_str));
    }

    report->save_index = data["order"].as_integer();
    report->show_summary = data["show_summary"].as_boolean();
    report->show_sold_title = data["show_sold_title"].as_boolean();
    report->opened = data["opened"].as_boolean(true);

    for (auto e : data["columns"])
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

    // Load titles
    title_t** titles = nullptr;
    array_reserve(titles, config_size(ctitles));
    for (auto title_data : ctitles)
    {
        string_const_t code = config_name(title_data);
        title_t* title = title_allocate(report->wallet, title_data);
        titles = array_push(titles, title);
    }
    report->titles = titles;

    report_filter_out_titles(report);
    report_summary_update(report);

    // Create table
    table_t* table = table_allocate(name);
    report_table_setup(report->id, table);
    report_table_add_default_columns(report->id, table);
    report->table = table;

    return report->id;
}

FOUNDATION_STATIC void report_render_windows()
{
    report_render_create_dialog(&SETTINGS.show_create_report_ui);
}

FOUNDATION_STATIC void report_render_menus()
{
    if (shortcut_executed(ImGuiKey_F2))
        SETTINGS.show_create_report_ui = true;

    if (!ImGui::BeginMenuBar())
        return;
        
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::BeginMenu("Create"))
        {
            if (ImGui::MenuItem("Report", "F2", &SETTINGS.show_create_report_ui))
                SETTINGS.show_create_report_ui = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Open"))
        {
            if (ImGui::MenuItem("Report...", nullptr, nullptr))
                log_warnf(HASH_REPORT, WARNING_UNSUPPORTED, STRING_CONST("TODO"));

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
    tab_draw(ICON_MD_WALLET " Wallet ", nullptr, ImGuiTabItemFlags_Leading, wallet_history_draw, nullptr);

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
            total_days += t->elapsed_days;
            total_active_titles++;
        }

        const bool title_is_sold = title_sold(t);
        if (!title_is_sold)
            total_investment += title_get_total_investment(t);

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
            const double sell_gain_if_kept = (s->current.adjusted_close - t->sell_adjusted_price) * t->sell_total_adjusted_qty;
            const double sell_p = (s->current.adjusted_close - t->sell_adjusted_price) / t->sell_adjusted_price;
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
    report->total_gain = total_value - total_investment;
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
    const size_t title_count = array_size(report->titles);
    for (size_t i = 0; i < title_count; ++i)
    {
        title_t* t = report->titles[i];
        if (report->show_sold_title || !title_sold(t))
        {
            t->stock->fetch_errors = 0;
            t->stock->resolved_level &= ~FetchLevel::REALTIME;
            if (!stock_resolve(t->stock, FetchLevel::REALTIME))
                dispatcher_wait_for_wakeup_main_thread(50);
            report->fully_resolved = 0;
        }
    }

    _report_expression_cache->clear();
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
        if (report->dirty && ImGui::MenuItem("Save"))
            report_save(report);

        if (ImGui::MenuItem("Rename"))
            report->show_rename_ui = true;

        if (ImGui::MenuItem("Delete"))
            report_delete(report);

        ImGui::EndPopup();
    }

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Report"))
        {
            if (ImGui::MenuItem(ICON_MD_ADD " Add Title"))
                report->show_add_title_ui = true;

            if (ImGui::MenuItem(ICON_MD_DASHBOARD_CUSTOMIZE " Expression Columns"))
                report_open_expression_columns_dialog(report);

            ImGui::Separator();

            if (ImGui::MenuItem(ICON_MD_SELL " Show Sold", nullptr, &report->show_sold_title))
                report_filter_out_titles(report);
            if (ImGui::MenuItem(ICON_MD_SUMMARIZE " Show Summary", "F4", &report->show_summary))
                report_summary_update(report);

            if (ImGui::MenuItem(ICON_MD_TIMELINE " Show Timeline"))
                timeline_render_graph(report);

            ImGui::MenuItem(ICON_MD_AUTO_GRAPH " Show Transactions", nullptr, &report->show_order_graph);
                
            ImGui::Separator();

            if (report->save)
            {
                if (ImGui::MenuItem(ICON_MD_SAVE " Save", ICON_MD_KEYBOARD_COMMAND "+S"))
                    report_save(report);
            }

            if (ImGui::MenuItem(ICON_MD_REFRESH " Refresh", "F5"))
                report_refresh(report);

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
    if (!ImGui::Begin(name.str, show_ui, ImGuiWindowFlags_Modal | ImGuiWindowFlags_NoCollapse | flags))
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

    string_const_t name = report_render_input_dialog(CTEXT("Create Report##1"), CTEXT("Create"), CTEXT(""), CTEXT("Name"), show_ui);
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

    config_handle_t data;
    if (fs_is_file(STRING_ARGS(report_file_path)))
        data = config_parse_file(STRING_ARGS(report_file_path), report_json_flags);

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

void report_save(report_t* report)
{
    // Replicate some memory fields
    config_set(report->data, "name", string_table_decode_const(report->name));
    config_set(report->data, "order", (double)report->save_index);
    config_set(report->data, "show_summary", report->show_summary);
    config_set(report->data, "show_sold_title", report->show_sold_title);
    config_set(report->data, "opened", report->opened);

    auto cv_columns = config_set_array(report->data, STRING_CONST("columns"));
    config_array_clear(cv_columns);
    foreach(c, report->expression_columns)
    {
        auto cv_column = config_array_push(cv_columns, CONFIG_VALUE_OBJECT);
        config_set(cv_column, "name", c->name, string_length(c->name));
        config_set(cv_column, "expression", c->expression, string_length(c->expression));
        config_set(cv_column, "format", (double)c->format);
    }

    wallet_save(report->wallet, config_set_object(report->data, STRING_CONST("wallet")));

    string_const_t report_file_path = report_get_save_file_path(report);
    if (config_write_file(report_file_path, report->data,
        CONFIG_OPTION_WRITE_SKIP_NULL | CONFIG_OPTION_WRITE_SKIP_DOUBLE_COMMA_FIELDS | CONFIG_OPTION_WRITE_NO_SAVE_ON_DATA_EQUAL)) {
        report->dirty = false;
    }
}

void report_render(report_t* report)
{
    FOUNDATION_ASSERT(report);
    const float space_left = ImGui::GetContentRegionAvail().x;

    if (shortcut_executed(ImGuiKey_F5))
    {
        log_warnf(HASH_REPORT, WARNING_PERFORMANCE, STRING_CONST("Refreshing report %s"), string_table_decode(report->name));
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
        report->table->search_filter = string_to_const(SETTINGS.search_filter);
        table_render(report->table, report->titles, (int)report->active_titles, sizeof(title_t*), 0.0f, 0.0f);
    }, summary_frame, IMGUI_SPLITTER_HORIZONTAL, (space_left - 400.0f) / space_left);
    
    report_render_dialogs(report);
    report_graph_render(report);
}

void report_sort_order()
{
    std::sort(_reports, _reports + array_size(_reports), [](const report_t& a, const report_t& b)
    {
        if (a.save_index == b.save_index)
            return string_compare_less(string_table_decode(a.name), string_table_decode(b.name));
        return a.save_index < b.save_index;
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

    // Trigger updates
    for (size_t i = 0; i < title_count; ++i)
    {
        title_t* t = report->titles[i];
        if (title_is_index(t))
            continue;

        if (!title_is_resolved(t))
        {
            log_debugf(HASH_REPORT, STRING_CONST("Syncing title %s"), t->code);
            title_update(t, 0);
        }
    }

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

report_t** report_sort_alphabetically()
{
    report_t** sorted_reports = nullptr;
    foreach(r, _reports)
        array_push(sorted_reports, r);

    return array_sort_by(sorted_reports, [](const report_t* a, const report_t* b)
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
    string_const_t report_dir_path = session_get_user_file_path(STRING_ARGS(REPORTS_DIR_NAME));
    fs_make_directory(STRING_ARGS(report_dir_path));

    string_t* paths = fs_matching_files(STRING_ARGS(report_dir_path), STRING_CONST("^.*\\.json$"), false);
    foreach (e, paths)
    {
        char report_path_buffer[1024];
        string_t report_path = path_concat(STRING_BUFFER(report_path_buffer), STRING_ARGS(report_dir_path), STRING_ARGS(*e));
        report_load(string_to_const(report_path));
    }
    string_array_deallocate(paths);

    report_sort_order();

    service_register_tabs(HASH_REPORT, report_render_tabs);
    service_register_menu(HASH_REPORT, report_render_menus);
    service_register_window(HASH_REPORT, report_render_windows);

    _report_expression_cache = MEM_NEW(HASH_REPORT, std::remove_pointer<decltype(_report_expression_cache)>::type);
}

FOUNDATION_STATIC void report_shutdown()
{
    MEM_DELETE(_report_expression_cache);
    
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

DEFINE_SERVICE(REPORT, report_initialize, report_shutdown, SERVICE_PRIORITY_HIGH);
