/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */
 
#include "pattern.h"
 
#include "eod.h"
#include "bulk.h"
#include "settings.h"
#include "report.h"
#include "news.h"
#include "alerts.h"
#include "openai.h"
#include "financials.h"
#include "logo.h"
#include "watches.h"

#include <framework/app.h>
#include <framework/jobs.h>
#include <framework/session.h>
#include <framework/table.h>
#include <framework/module.h>
#include <framework/tabs.h>
#include <framework/math.h>
#include <framework/config.h>
#include <framework/string.h>
#include <framework/profiler.h>
#include <framework/window.h>
#include <framework/dispatcher.h>
#include <framework/array.h>
#include <framework/system.h>
#include <framework/shared_ptr.h>
#include <framework/plot.h>

#include <algorithm>

#define HASH_PATTERN static_hash_string("pattern", 7, 0xf53f39240bdce58aULL)

#define PATTERN_FLEX_RANGE_COUNT (90U)

constexpr int FIXED_MARKS[] = { 1, 3, 7, 14, 30, 90, 180, 365, 365 * 2, 365 * 3, 365 * 6, -1 };
constexpr const char* DAY_LABELS[] = { "1D", "3D", "1W", "2W", "1M",  "3M",  "6M",  "1Y", "2Y", "3Y",  "6Y", "MAX" };

const FetchLevel FETCH_ALL =
    FetchLevel::EOD |
    FetchLevel::REALTIME |
    FetchLevel::FUNDAMENTALS |
    FetchLevel::TECHNICAL_SMA |
    FetchLevel::TECHNICAL_EMA |
    FetchLevel::TECHNICAL_WMA |
    FetchLevel::TECHNICAL_SLOPE |
    FetchLevel::TECHNICAL_CCI |
    FetchLevel::TECHNICAL_SAR;

enum PatternType : int {
    PATTERN_ALL_BEGIN = 0,
    PATTERN_GRAPH_BEGIN = PATTERN_ALL_BEGIN,
    PATTERN_GRAPH_DEFAULT = 0,
    PATTERN_GRAPH_ANALYSIS,
    PATTERN_GRAPH_FLEX,
    PATTERN_GRAPH_TRENDS,
    PATTERN_GRAPH_YOY,
    PATTERN_GRAPH_INTRADAY,
    PATTERN_GRAPH_END,

    PATTERN_SIMULATION_BEGIN,
    PATTERN_ACTIVITY,
    PATTERN_SIMULATION_END,

    PATTERN_ALL_END = PATTERN_SIMULATION_END
};

constexpr const char* GRAPH_TYPES[PATTERN_ALL_END] = {
    "Default",
    "Analysis",
    "Flex",
    "Trends",
    "Y/Y",
    "Intraday",
    nullptr,
    nullptr,
    "Activity"
};

typedef enum class PatternRenderFlags : int {
    None = 0,

    HideTableHeaders = 1 << 0
} pattern_render_flags_t;
DEFINE_ENUM_FLAGS(PatternRenderFlags);

struct pattern_activity_t
{
    time_t date{ 0 };
    double polarity{ 0 };
    double count{ 0 };
};

struct pattern_fundamentals_field_info_t
{
    string_t response;
};

struct pattern_graph_data_t
{
    pattern_t* pattern;
    double x_data[ARRAY_COUNT(FIXED_MARKS)];
    double y_data[ARRAY_COUNT(FIXED_MARKS)];

    unsigned int x_count{ ARRAY_COUNT(x_data) };
    double min_d{ DBL_MAX }, max_d{ -DBL_MAX };
    double min_p{ DBL_MAX }, max_p{ -DBL_MAX };

    bool refresh{ false };
    bool compact{ false };
};

static pattern_t* _patterns = nullptr;
static pattern_activity_t* _activities{ nullptr };

FOUNDATION_STATIC pattern_t* pattern_get(pattern_handle_t handle)
{
    const size_t pattern_count = array_size(_patterns);
    if (handle < 0 || handle >= pattern_count)
        return nullptr;
    return &_patterns[handle];
}

FOUNDATION_STATIC string_const_t pattern_today()
{
    return string_from_date(time_now());
}

FOUNDATION_STATIC time_t pattern_date(const pattern_t* pattern, int days)
{
    time_t pdate = time_add_days(pattern->date, days);
    tm tm;
    if (time_to_local(pdate, &tm))
        return pdate;

    if (tm.tm_wday == 0)
        return time_add_days(pdate, -2);

    if (tm.tm_wday == 6)
        return time_add_days(pdate, -1);

    return pdate;
}

FOUNDATION_STATIC string_const_t pattern_date_to_string(const pattern_t* pattern, int days)
{
    return string_from_date(pattern_date(pattern, days));
}

FOUNDATION_STATIC string_const_t pattern_format_number(const char* fmt, size_t fmt_length, double value, double default_value = DNAN)
{
    if (math_real_is_nan(value) && math_real_is_nan(default_value))
        return CTEXT("-");

    return string_format_static(fmt, fmt_length, math_ifnan(value, default_value));
}

FOUNDATION_STATIC string_const_t pattern_format_currency(double value, double default_value = DNAN)
{
    if (value < 0.05)
        return pattern_format_number(STRING_CONST("%.3lf $"), value, default_value);

    return pattern_format_number(STRING_CONST("%.2lf $"), value, default_value);
}

FOUNDATION_STATIC string_const_t pattern_format_percentage(double value, double default_value = DNAN)
{
    if (math_abs(value) > 1e3)
        return pattern_format_number(STRING_CONST("%.3gK %%"), value / 1000.0, default_value);
    
    return pattern_format_number(STRING_CONST("%.3g %%"), value, default_value);
}

FOUNDATION_STATIC int pattern_format_date_label(double value, char* buff, int size, void* user_data)
{
    pattern_graph_data_t& graph = *(pattern_graph_data_t*)user_data;
    time_t then = graph.pattern->date - (time_t)value * time_one_day();
    string_const_t date_str = string_from_date(then);
    return (int)string_format(buff, size, STRING_CONST("%.*s (%d)"), STRING_FORMAT(date_str), math_round(value)).length;
}

FOUNDATION_STATIC void pattern_render_planning_line(string_const_t v1, string_const_t v1_url, string_const_t v2, string_const_t v3, string_const_t v4, bool translate = false)
{
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    if (!string_is_null(v1)) 
    {
        string_const_t trv1 = translate ? tr(STRING_ARGS(v1), false) : v1;
        table_cell_right_aligned_label(STRING_ARGS(trv1), STRING_ARGS(v1_url));
    }

    ImGui::TableNextColumn();
    ImGui::SetWindowFontScale(0.7f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
    if (!string_is_null(v2)) table_cell_middle_aligned_label(STRING_ARGS(v2));
    ImGui::SetWindowFontScale(0.8f);

    ImGui::TableNextColumn();
    if (!string_is_null(v3)) 
    {
        table_cell_right_aligned_label(STRING_ARGS(v3));
    }

    ImGui::TableNextColumn();
    if (!string_is_null(v4)) 
    {
        char value_with_thin_spaces_buffer[16] = { 0 };
        string_t value_with_thin_spaces = string_copy(STRING_BUFFER(value_with_thin_spaces_buffer), STRING_ARGS(v4));
        value_with_thin_spaces = string_replace(STRING_ARGS(value_with_thin_spaces), sizeof(value_with_thin_spaces_buffer), STRING_CONST(" "), STRING_CONST(THIN_SPACE), true);
        table_cell_right_aligned_label(STRING_ARGS(value_with_thin_spaces));
    }
}

FOUNDATION_STATIC void pattern_render_planning_line(string_const_t v1, string_const_t v2, string_const_t v3, string_const_t v4, bool translate = false)
{
    pattern_render_planning_line(v1, string_null(), v2, v3, v4, translate);
}

FOUNDATION_STATIC double pattern_mark_change_p(const pattern_t* pattern, int mark_index)
{
    pattern_mark_t& mark = ((pattern_t*)pattern)->marks[mark_index];
    if (!mark.fetched)
    {
        const stock_t* s = pattern->stock;
        if (s == nullptr || !s->has_resolve(FetchLevel::EOD | FetchLevel::REALTIME))
            return DNAN;

        mark.fetched = true;
        const day_result_t* ed = stock_get_EOD(s, mark.date, mark.date == 0);
        if (ed == nullptr)
            return DNAN;

        const day_result_t& cd = s->current;
        mark.date = ed->date;
        mark.change_p = (cd.adjusted_close - ed->adjusted_close) / ed->adjusted_close;
    }

    return mark.change_p;
}

FOUNDATION_STATIC string_const_t pattern_mark_change_p_to_string(const pattern_t* pattern, int mark_index)
{
    double change_p = pattern_mark_change_p(pattern, mark_index);
    if (math_real_is_nan(change_p))
        return CTEXT("-");

    const double abs_change_p = math_abs(change_p);
    if (abs_change_p > 10)
        return string_format_static(STRING_CONST("%.3gK %%"), change_p / 10.0);

    return string_format_static(STRING_CONST("%.*g %%"), math_abs(change_p) < 0.01 ? 2 : 3, change_p * 100.0);
}

FOUNDATION_STATIC void pattern_render_planning_url(string_const_t label, string_const_t url, const pattern_t* pattern, int mark_index, 
    bool can_skip_if_not_valid = false, bool translate = false)
{
    pattern_mark_t& mark = ((pattern_t*)pattern)->marks[mark_index];
    string_const_t change_p_str = pattern_mark_change_p_to_string(pattern, mark_index);

    const bool mark_valid = mark.fetched && !math_real_is_nan(mark.change_p);
    if (can_skip_if_not_valid && !mark_valid)
        return;

    char dbuf[16];
    size_t dbuf_length = (size_t)plot_value_format_elapsed_time_short(FIXED_MARKS[mark_index], dbuf, ARRAY_COUNT(dbuf), nullptr);

    pattern_render_planning_line(label, url,
        mark_valid ? string_const(dbuf, dbuf_length) : CTEXT("-"),
        mark_valid ? string_from_date(mark.date) : CTEXT("-"),
        change_p_str, label.length > 1 && translate);

    if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
    {
        const double initial_investment = 10000.0;
        const double today_price = stock_current_price(pattern->stock);
        const double priceat_date = stock_price_on_date(pattern->stock, mark.date);
        double change = 0;
        double initial_qty = 0;
        double gain = 0;

        if (mark_index < ARRAY_COUNT(FIXED_MARKS) - 1)
        {
            const double priceat_date_3_months_before = stock_price_on_date(pattern->stock, time_add_days(mark.date, -pattern->range));
            change = (priceat_date - priceat_date_3_months_before);
            initial_qty = initial_investment / priceat_date_3_months_before;
            gain = change * initial_qty;
            const char* label2 = tr_format("If you would've invested {0,currency:10k} {1} days before {2:date} and sold on that day you would of {3,translate:gain} {4:currency}",
                initial_investment, pattern->range, mark.date, gain >= 0 ? ICON_MD_TRENDING_UP " gained" : ICON_MD_TRENDING_DOWN " lost", gain);
            ImGui::BulletTextWrapped(label2);
        }

        ImGui::Dummy(ImVec2(IM_SCALEF(550), 1));
        change = (today_price - priceat_date);
        initial_qty = initial_investment / priceat_date;
        gain = change * initial_qty;
        const char* label = tr_format("If you would have invested {0,currency:10k} in {1:date} ({1:since}) you would of {3,translate:gain} {2:currency}", 
            initial_investment, mark.date, gain, gain >= 0 ? ICON_MD_TRENDING_UP " gained" : ICON_MD_TRENDING_DOWN " lost");
        
        ImGui::BulletTextWrapped(label);

        ImGui::Dummy(ImVec2(1, 1)); // Ensure similar spacing to top of tooltip

        ImGui::EndTooltip();
    }
}

FOUNDATION_STATIC void pattern_render_planning_line(string_const_t label, const pattern_t* pattern, int mark_index, bool can_skip_if_not_valid = false, bool translate = false)
{
    pattern_render_planning_url(label, string_null(), pattern, mark_index, can_skip_if_not_valid, translate);
}

FOUNDATION_STATIC bool pattern_render_stats_value(const stock_t* s, string_const_t value)
{
    #if BUILD_APPLICATION
    ImGui::TableNextColumn();
    if (string_is_null(value))
        return false;

    char value_with_thin_spaces_buffer[16] = { 0 };
    string_t value_with_thin_spaces = string_copy(STRING_BUFFER(value_with_thin_spaces_buffer), STRING_ARGS(value));
    value_with_thin_spaces = string_replace(STRING_ARGS(value_with_thin_spaces), sizeof(value_with_thin_spaces_buffer), STRING_CONST(" "), STRING_CONST(THIN_SPACE), true);
    table_cell_right_aligned_label(STRING_ARGS(value_with_thin_spaces));

    if (s == nullptr)
        return false;

    // Check if the value has a dollar
    const size_t dollar_sign_pos = string_rfind(STRING_ARGS(value), '$', STRING_NPOS);
    if (dollar_sign_pos != STRING_NPOS)
    {
        // Open contextual menu to add a price alert
        string_const_t symbol = SYMBOL_CONST(s->code);
        if (ImGui::BeginPopupContextItem(value.str))
        {
            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable(tr_format(" Add a price alert of {0:currency} for {1:symbol} ", value, symbol)))
            {
                double price_alert = string_to_real(value.str, dollar_sign_pos);
                FOUNDATION_ASSERT(price_alert > 0);

                if (s->current.price > price_alert)
                {
                    alerts_add_price_decrease(STRING_ARGS(symbol), price_alert);
                }
                else
                {
                    alerts_add_price_increase(STRING_ARGS(symbol), price_alert);
                }
            }
            ImGui::EndPopup();
        }
    }
    #endif

    return true;
}

FOUNDATION_STATIC void pattern_render_stats_line(const stock_t* s, string_const_t v1, string_const_t v2, string_const_t v3, bool translate = false)
{
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 15.0f);

    // Split v1 with ||
    string_const_t label, tooltip;
    string_const_t trv1 = translate && v1.length > 1 ? tr(STRING_ARGS(v1), false) : v1;
    string_split(STRING_ARGS(trv1), STRING_CONST("||"), &label, &tooltip, false);
    if (tooltip.length > 0)
    {
        ImGui::TextWrapped("%.*s", STRING_FORMAT(label));

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip())
        {
            ImGui::Dummy(ImVec2(IM_SCALEF(405), IM_SCALEF(4)));
            ImGui::MoveCursor(IM_SCALEF(5), IM_SCALEF(0));
            ImGui::PushTextWrapPos(IM_SCALEF(400));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%.*s", STRING_FORMAT(tooltip));
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(IM_SCALEF(405), IM_SCALEF(8)));
            ImGui::EndTooltip();
        }
    }
    else
    {
        ImGui::TextWrapped("%.*s", STRING_FORMAT(label));
    }

    pattern_render_stats_value(s, v2);
    pattern_render_stats_value(s, v3);
}

FOUNDATION_STATIC bool pattern_render_decision_line(int rank, bool* check, const char* text, size_t text_length)
{
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    char cid[32];
    string_format(STRING_BUFFER(cid), STRING_CONST("##CHECK_%lu"), (uintptr_t)check);
    if (check && ImGui::Checkbox(cid, check))
        log_infof(0, STRING_CONST("Reason %d %s"), rank, *check ? "checked" : "unchecked");

    ImGui::TableNextColumn();
    ImGui::Text("%d.", rank);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered())
    {
        if (check)
            *check = !(*check);
    }

    ImGui::TableNextColumn();
    ImGui::TextWrapped("%.*s", (int)text_length, text);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered())
        return true;

    return false;
}

FOUNDATION_STATIC bool pattern_render_decision_line(int rank, bool* check, const char* text)
{
    return pattern_render_decision_line(rank, check, text, string_length(text));
}

FOUNDATION_STATIC string_const_t pattern_price(const pattern_t* pattern)
{
    const stock_t* s = pattern->stock;
    if (s == nullptr)
        return CTEXT("-");

    return string_from_currency(s->current.adjusted_close);
}

FOUNDATION_STATIC string_const_t pattern_currency_conversion(const pattern_t* pattern)
{
    const stock_t* s = pattern->stock;
    if (s == nullptr)
        return CTEXT("-");

    string_t buf = string_static_buffer(32);

    string_const_t currency = string_table_decode_const(s->currency);
    double exg_rate = stock_exchange_rate(STRING_ARGS(currency), STRING_ARGS(string_const(SETTINGS.preferred_currency)), pattern->date);

    return string_to_const(string_format(buf.str, buf.length, 
        STRING_CONST("%.*s(%.*s)"), 
        STRING_FORMAT(string_table_decode_const(s->currency)), 
        STRING_FORMAT(string_from_currency(exg_rate, "9.99"))));
}

FOUNDATION_STATIC string_const_t pattern_eod_to_google_exchange(string_const_t eod_exchange)
{
    if (string_equal(STRING_ARGS(eod_exchange), STRING_CONST("TO")))
        return CTEXT("TSE");

    if (string_equal(STRING_ARGS(eod_exchange), STRING_CONST("V")))
        return CTEXT("CVE");

    if (string_equal(STRING_ARGS(eod_exchange), STRING_CONST("OTCQX")))
        return CTEXT("OTCMKTS");

    return eod_exchange;
}

FOUNDATION_STATIC string_const_t pattern_tsx_money_url(const pattern_t* pattern)
{
    const stock_t* s = pattern->stock;
    if (s == nullptr)
        return string_null();

    string_t url_buf = string_static_buffer(2048);

    string_const_t symbol = string_table_decode_const(s->symbol);
    string_t url = string_format(url_buf.str, url_buf.length, STRING_CONST("https://money.tmx.com/en/quote/%.*s"), STRING_FORMAT(symbol));
    return string_to_const(url);
}

FOUNDATION_STATIC string_const_t pattern_google_finance_url(const pattern_t* pattern)
{
    const stock_t* s = pattern->stock;
    if (s == nullptr)
        return string_null();

    string_t url_buf = string_static_buffer(2048);
    string_const_t google_finance_exchange = pattern_eod_to_google_exchange(string_table_decode_const(s->exchange));
    string_const_t symbol_name = string_table_decode_const(s->symbol);
    string_t url = string_format(url_buf.str, url_buf.length, 
        STRING_CONST("https://www.google.com/finance/quote/%.*s:%.*s?window=6M"), 
        STRING_FORMAT(symbol_name),
        STRING_FORMAT(google_finance_exchange));
    return string_to_const(url);
}

FOUNDATION_STATIC string_const_t pattern_lapresse_news_url(const pattern_t* pattern)
{
    string_const_t name = stock_get_short_name(pattern->stock);
    string_const_t encoded_name = url_encode(STRING_ARGS(name));
    return string_format_static(STRING_CONST("https://www.google.com/search?q=%.*s+site:lapresse.ca&tbs=qdr:w"), STRING_FORMAT(encoded_name));
}

FOUNDATION_STATIC string_const_t pattern_google_news_url(const pattern_t* pattern)
{
    const stock_t* s = pattern->stock;
    if (s == nullptr)
        return string_null();

    string_const_t stock_name = string_table_decode_const(s->name);
    string_const_t encoded_name = url_encode(STRING_ARGS(stock_name));

    string_t url_buf = string_static_buffer(2048);
    string_t url = string_format(url_buf.str, url_buf.length,
        STRING_CONST("https://www.google.com/search?tbs=sbd:1&q=%.*s&source=lnms&tbm=nws"),
        STRING_FORMAT(encoded_name));
    return string_to_const(url);
}

FOUNDATION_STATIC float pattern_render_planning(const pattern_t* pattern)
{
    ImGuiTableFlags flags =
        ImGuiTableFlags_NoSavedSettings |
        //ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_NoClip |
        ImGuiTableFlags_NoHostExtendY |
        ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX;

    if (!ImGui::BeginTable("Planning##8", 4, flags))
        return 0;

    const stock_t* s = pattern->stock;

    ImGui::TableSetupColumn("Labels", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Indices", ImGuiTableColumnFlags_WidthFixed, IM_SCALEF(25));
    ImGui::TableSetupColumn("V1", ImGuiTableColumnFlags_WidthFixed, IM_SCALEF(60));
    ImGui::TableSetupColumn("V2", ImGuiTableColumnFlags_WidthFixed, IM_SCALEF(45));
    //ImGui::TableHeadersRow();

    pattern_render_planning_line(CTEXT("Today"),
        CTEXT(""), 
        string_from_date(time_now()), 
        pattern_format_percentage(s->current.change_p), true);

    pattern_render_planning_line(CTEXT("Planning"), pattern, 0, false, true);

    string_const_t url = string_null();
    string_const_t updated_at = string_null();
    string_const_t code = string_table_decode_const(pattern->code);

    if (s)
    {
        url = string_table_decode_const(s->url);
        updated_at = string_from_date(s->updated_at);
    }

    const double updated_elapsed_time = time_elapsed_days(s->updated_at, time_now());
    pattern_render_planning_url(code, url, pattern, 1);
    if (s->updated_at != 0 && updated_elapsed_time > 15)
        ImGui::PushStyleColor(ImGuiCol_Text, TEXT_WARN_COLOR);
    pattern_render_planning_line(updated_at, pattern, 2);
    if (s->updated_at != 0 && updated_elapsed_time > 15)
        ImGui::PopStyleColor();
    pattern_render_planning_line(pattern_price(pattern), pattern, 3);
    pattern_render_planning_line(pattern_currency_conversion(pattern), pattern, 4);

    if (s && string_table_symbol_equal(s->exchange, STRING_CONST("TO")))
        pattern_render_planning_url(CTEXT("La Presse"), pattern_lapresse_news_url(pattern), pattern, 5, false);
    else
        pattern_render_planning_line(CTEXT(""), pattern, 5);
    pattern_render_planning_url(CTEXT("Google"), pattern_google_finance_url(pattern), pattern, 6);
    pattern_render_planning_url(CTEXT("News"), pattern_google_news_url(pattern), pattern, 7, false, true);
    if (s && string_table_symbol_equal(s->exchange, STRING_CONST("TO")))
        pattern_render_planning_url(CTEXT("TSX"), pattern_tsx_money_url(pattern), pattern, 8, false);
    else
        pattern_render_planning_line(CTEXT(""), pattern, 8, true);
    pattern_render_planning_line(CTEXT(""), pattern, 9, true);
    pattern_render_planning_line(CTEXT(""), pattern, 10, true);
    pattern_render_planning_line(CTEXT(""), pattern, 11, true);

    float y_offset = ImGui::GetCursorPosY();
    ImGui::EndTable();
    return y_offset;
}

FOUNDATION_STATIC void pattern_compute_years_performance_ratios(pattern_t* pattern)
{
    // Get year after year yield
    const stock_t* s = pattern->stock;
    if (s == nullptr || !s->has_resolve(FetchLevel::FUNDAMENTALS | FetchLevel::EOD))
        return;

    if (!pattern->performance_ratio.initialized)
    {
        pattern->performance_ratio = (s->high_52 / math_ifnan(s->ws_target, s->low_52))
            * math_ifnan(math_ifnan(s->pe, s->peg), 1.0);
    }

    if (pattern->yy_ratio.initialized)
        return;

    if (array_size(s->history) <= 1)
        return;

    day_result_t* recent = array_first(s->history);
    day_result_t* oldest = array_last(s->history) - 300;
    if (oldest < recent)
        oldest = array_last(s->history);

    const double max_change = (recent->adjusted_close - oldest->adjusted_close) / oldest->adjusted_close;
    
    pattern->years = (recent->date - oldest->date) / (365.0 * 24.0 * 60.0 * 60.0);
    pattern->performance_ratio = max_change / pattern->years.fetch() * 100.0;

    double* yratios = nullptr;
    recent = &s->history[0];
    unsigned start = 260, end = array_size(s->history);

    if (end > 500)
        end -= 260;
    else
        array_push(yratios, pattern->performance_ratio);

    for (; start < end; start += 260)
    {
        oldest = s->history + start;
        const double change_p = (recent->adjusted_close - oldest->adjusted_close) / oldest->adjusted_close * 100.0;
        recent = oldest;
        array_push(yratios, change_p);
    }

    double median, average;
    array_sort(yratios);
    double mavg = math_median_average(yratios, median, average);
    pattern->yy_ratio = median;
    array_deallocate(yratios);
}

FOUNDATION_STATIC const stock_t* pattern_refresh(pattern_t* pattern, FetchLevel minimal_required_levels = FetchLevel::NONE)
{
    string_const_t code = SYMBOL_CONST(pattern->code);
    pattern->stock = stock_request(STRING_ARGS(code), FETCH_ALL);
    array_deallocate(pattern->flex);
    for (auto& m : pattern->marks)
        m.fetched = false;

    if (minimal_required_levels != FetchLevel::NONE)
    {
        tick_t timeout = time_current();
        while (!pattern->stock->has_resolve(minimal_required_levels) && time_elapsed(timeout) < 10)
            dispatcher_wait_for_wakeup_main_thread();
    }

    return pattern->stock;
}

double pattern_get_bid_price_low(pattern_handle_t pattern_handle)
{
    pattern_t* pattern = pattern_get(pattern_handle);
    if (!pattern)
        return NAN;
    const stock_t* s = pattern_refresh(pattern, FetchLevel::EOD | FetchLevel::REALTIME);
    if (!s)
        return NAN;

    const double flex_low_p = pattern->flex_low.fetch();
    const double flex_high_p = pattern->flex_high.fetch();
    const double today_price = s->current.adjusted_close;
    
    double mcp = 0;
    for (int i = 0; i < 3; ++i)
        mcp += pattern_mark_change_p(pattern, i);
    mcp += s->current.change_p / 100.0;
    mcp /= 4.0;

    return min(today_price + (today_price * (flex_low_p + math_abs(mcp))), today_price - (today_price * pattern->flex_high.fetch()));
}

double pattern_get_bid_price_high(pattern_handle_t pattern_handle)
{
    pattern_t* pattern = pattern_get(pattern_handle);
    if (!pattern)
        return NAN;
    const stock_t* s = pattern_refresh(pattern, FetchLevel::EOD | FetchLevel::REALTIME);
    if (!s)
        return NAN;

    const double flex_low_p = pattern->flex_low.fetch();
    const double flex_high_p = pattern->flex_high.fetch();
    const double today_price = s->current.adjusted_close;
    
    double mcp = 0;
    for (int i = 0; i < 3; ++i)
        mcp += pattern_mark_change_p(pattern, i);
    mcp += s->current.change_p / 100.0;
    mcp /= 4.0;

    return today_price + (today_price * (flex_high_p - mcp));
}

FOUNDATION_STATIC double pattern_average_volume_3months(pattern_t* pattern)
{
    if (pattern->average_volume_3months.initialized)
        return pattern->average_volume_3months.fetch();

    const stock_t* s = pattern->stock;
    if (s == nullptr)
        return NAN; 
        
    if (!s->has_resolve(FetchLevel::EOD))
        return s->current.volume;

    double occurence = 0;
    double total_volume = 0;
    for (unsigned i = 0, end = min(SIZE_C(60), s->history_count); i < end; ++i)
    {
        if (s->history[i].volume == 0)
            continue;

        occurence += 1.0;
        total_volume += s->history[i].volume;
    }

    pattern->average_volume_3months = total_volume / occurence;
    return pattern->average_volume_3months.fetch();
}

FOUNDATION_STATIC float pattern_render_stats(pattern_t* pattern)
{
    ImGuiTableFlags flags =
        ImGuiTableFlags_NoSavedSettings |
        ImGuiTableFlags_NoClip |
        ImGuiTableFlags_NoHostExtendY |
        ImGuiTableFlags_PreciseWidths |
        ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX;

    if (!ImGui::BeginTable("Stats##1", 3, flags))
        return 0;

    ImGui::TableSetupColumn("Labels", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("V1", ImGuiTableColumnFlags_WidthFixed, IM_SCALEF(60));
    ImGui::TableSetupColumn("V2", ImGuiTableColumnFlags_WidthFixed, IM_SCALEF(50));
    //ImGui::TableHeadersRow();

    const stock_t* s = pattern->stock;
    if (!stock_is_index(s))
    {
        const double current_volume = s->current.volume;
        const double average_volume = pattern_average_volume_3months(pattern);
        const double volume_p = current_volume / average_volume * 100.0;
        pattern_render_stats_line(nullptr, CTEXT("Volume"),
            string_template_static("{0,abbreviate}/{1,abbreviate}", current_volume, average_volume),
            pattern_format_number(STRING_CONST("%.2lf %%"), volume_p), true);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && average_volume > 0)
        {
            ImGui::BeginTooltip();

            string_const_t volumestr = string_template_static("{0,abbreviate}", current_volume);
            string_const_t averagestr = string_template_static("{0,abbreviate}", average_volume);
            ImGui::TrText("Today's volume is %.2lf %% of the average volume over the last 3 months (%.*s/%.*s)", volume_p,
                STRING_FORMAT(volumestr), STRING_FORMAT(averagestr));
            ImGui::EndTooltip();
        }

        pattern_render_stats_line(nullptr, CTEXT("Market Cap|| Units / Value $"),
            string_template_static("{0,abbreviate}", s->shares_count),
            string_template_static("{0,currency}", s->market_cap), true);

        pattern_render_stats_line(s, CTEXT("High 52"),
            pattern_format_currency(s->high_52),
            pattern_format_percentage(s->current.adjusted_close / s->high_52 * 100.0), true);
        pattern_render_stats_line(s, CTEXT("Low 52"),
            pattern_format_currency(s->low_52),
            pattern_format_percentage(s->low_52 / s->current.adjusted_close * 100.0), true);

        const double yielding = s->dividends_yield.get_or_default() * 100.0;
        const double performance_ratio = pattern->yy_ratio.get_or_default(pattern->performance_ratio.get_or_default(0.0));
        const double performance_ratio_combined = max(pattern->yy_ratio.get_or_default(pattern->performance_ratio.fetch()), yielding);

        string_const_t fmttr = RTEXT("Yield %s||Dividends / Yield Year after Year");
        string_const_t yield_label = string_format_static(STRING_ARGS(fmttr),
            pattern->yy_ratio.fetch() >= performance_ratio ? ICON_MD_TRENDING_UP : ICON_MD_TRENDING_DOWN);
        ImGui::PushStyleColor(ImGuiCol_Text, performance_ratio <= 0 || performance_ratio_combined < SETTINGS.good_dividends_ratio * 100.0 ? TEXT_WARN_COLOR : TEXT_GOOD_COLOR);
        pattern_render_stats_line(nullptr, yield_label,
            pattern_format_percentage(yielding),
            pattern_format_percentage(performance_ratio), false);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(tr(" Year after Year yielding (Overall ratio %.3g %%) (%.0lf last years) \n Adjusted Yield based on last year data: %.3g %% (" ICON_MD_CHANGE_HISTORY " %.3g%%) "),
                pattern->yy_ratio.fetch(), pattern->years.fetch(), pattern->performance_ratio.fetch(), pattern->performance_ratio.fetch() - pattern->yy_ratio.fetch());
        }

        pattern_render_stats_line(nullptr, CTEXT("Beta"),
            pattern_format_percentage(s->beta * 100.0),
            pattern_format_percentage(s->dma_200 / s->dma_50 * 100.0), true);

        const double eps_diff = s->earning_trend_difference.fetch();
        const double eps_percent = s->earning_trend_percent.fetch();
        ImGui::PushStyleColor(ImGuiCol_Text, eps_diff <= 0.1 ? TEXT_WARN_COLOR : TEXT_GOOD_COLOR);
        pattern_render_stats_line(nullptr, CTEXT("Earnings / Share||EPS stands for earnings per share. "
            "It is a financial metric that measures the amount of profit that a company has generated on a per-share basis over a "
            "specific period, usually a quarter or a year. EPS is calculated by dividing a company's total earnings by the number of shares outstanding."),
            pattern_format_currency(s->diluted_eps_ttm),
            pattern_format_percentage(eps_percent), true);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tr(" Earnings:  1 Year /  Actual /  Estimate /  Diff.  / Surprise /   Gain \n"
                "           %5.2lf $ / %5.2lf $ /   %5.2lf $ / %5.2lf $ /   %.3lg %% / %.3lg %% "),
                s->diluted_eps_ttm, s->earning_trend_actual.fetch(), s->earning_trend_estimate.fetch(), s->earning_trend_difference.fetch(), eps_percent,
                s->diluted_eps_ttm / s->current.close * 100.0);

        if (math_real_is_finite(s->pe) || math_real_is_finite(s->peg))
        {
            pattern_render_stats_line(nullptr, CTEXT("Price Earnings||Price Earnings / To Growth\n\n"
                "The P/E ratio, or price-to-earnings ratio, compares a company's current stock price to its earnings per share (EPS). "
                "It is calculated by dividing the stock price by the EPS. The P/E ratio provides a snapshot of how much investors are "
                "willing to pay for each dollar of earnings generated by the company.\n\nThe PEG ratio, or price-to-earnings-to-growth ratio, "
                "takes into account a company's expected earnings growth rate in addition to its P/E ratio. "
                "The PEG ratio is calculated by dividing the P/E ratio by the expected earnings growth rate for the company. "
                "The PEG ratio is a more comprehensive measure of a company's valuation compared to the P/E ratio, "
                "because it considers both the company's current earnings and its expected future growth potential."),
                pattern_format_percentage(s->pe),
                pattern_format_percentage(s->peg), true);
        }

        double flex_low_p = pattern->flex_low.fetch();
        double flex_high_p = pattern->flex_high.fetch();
        pattern_render_stats_line(s, CTEXT("Flex"), 
            CTEXT("-"),
            pattern_format_percentage(flex_low_p * 100.0), false);
        pattern_render_stats_line(s, CTEXT(""), 
            pattern_format_percentage((flex_high_p - flex_low_p) * 100.0),
            pattern_format_percentage(flex_high_p * 100.0));
        
        double mcp = 0;
        for (int i = 0; i < 3; ++i)
            mcp += pattern->marks[i].change_p;
        mcp += s->current.change_p / 100.0;
        mcp /= 4.0;

        double buy_limit = min(s->current.adjusted_close + (s->current.adjusted_close * (flex_low_p + math_abs(mcp))), s->current.adjusted_close - (s->current.adjusted_close * pattern->flex_high.fetch()));
        pattern_render_stats_line(s, CTEXT("Buy Limit"), 
            pattern_format_percentage((buy_limit / s->current.adjusted_close - 1.0) * 100.0),
            pattern_format_currency(buy_limit), true);

        const double flex_price_high = s->current.adjusted_close + (s->current.adjusted_close * (flex_high_p - mcp));
        const double sell_limit_p = (flex_price_high / buy_limit - 1.0) * 100.0;
        ImGui::PushStyleColor(ImGuiCol_Text, sell_limit_p < 0 ? TEXT_BAD_COLOR : (sell_limit_p > 3 ? TEXT_GOOD_COLOR : TEXT_WARN_COLOR));
        pattern_render_stats_line(s, CTEXT("Sell Limit"), 
            pattern_format_percentage(sell_limit_p),
            pattern_format_currency(flex_price_high), true);

        const double profit_price = s->dma_50;
        const double profit_percentage = (profit_price / flex_price_high - 1) * 100.0;
        ImGui::PushStyleColor(ImGuiCol_Text, profit_percentage < 0 ? TEXT_WARN_COLOR : TEXT_GOOD_COLOR);
        pattern_render_stats_line(s, CTEXT("Target Limit"),
            pattern_format_percentage(profit_percentage),
            pattern_format_currency(profit_price), true);

        const double ws_limit = max(s->ws_target, max(s->current.adjusted_close * s->peg, s->dma_200));
        const double ws_limit_percentage = (ws_limit / flex_price_high - 1) * 100.0;
        ImGui::PushStyleColor(ImGuiCol_Text, ws_limit_percentage < 50.0 ? TEXT_WARN_COLOR : TEXT_GOOD_COLOR);
        pattern_render_stats_line(s, CTEXT(""),
            pattern_format_percentage(ws_limit_percentage),
            pattern_format_currency(ws_limit));

        ImGui::PopStyleColor(3);
    }
    
    float y_offset = ImGui::GetCursorPosY();
    ImGui::EndTable();
    return y_offset;
}

template<size_t L, size_t D>
FOUNDATION_STATIC bool pattern_render_decision_mark(const pattern_t* pattern, unsigned rank, const char (&label)[L], const char (&description)[D])
{
    pattern_check_t* checks = ((pattern_t*)pattern)->checks;
    bool clicked = pattern_render_decision_line(rank, &checks[rank-1].checked, tr(label));

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginTooltip())
    {
        ImGui::Dummy(ImVec2(IM_SCALEF(405), IM_SCALEF(4)));
        ImGui::MoveCursor(IM_SCALEF(5), IM_SCALEF(0));
        ImGui::PushTextWrapPos(IM_SCALEF(400));
        ImGui::AlignTextToFramePadding();
        ImGui::TrTextUnformatted(description);
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(IM_SCALEF(405), IM_SCALEF(8)));
        ImGui::EndTooltip();
    }

    return clicked;
}

FOUNDATION_STATIC pattern_graph_data_t const pattern_render_build_graph_data(pattern_t* pattern)
{
    pattern_graph_data_t graph_data{ pattern };
    for (int i = 0; i < ARRAY_COUNT(FIXED_MARKS); ++i)
    {
        graph_data.x_data[i] = math_round((pattern->date - pattern->marks[i].date) / (double)time_one_day());
        graph_data.y_data[i] = pattern_mark_change_p(pattern, i) * 100.0;
    }

    const size_t x_count = graph_data.x_count;
    for (int i = 0; i < x_count; ++i)
    {
        const bool is_valid = !math_real_is_nan(graph_data.y_data[i]);
        double xdd = !is_valid ? FIXED_MARKS[i] : graph_data.x_data[i];
        graph_data.min_d = max(1.0, min(graph_data.min_d, xdd));
        if (i == 0 || is_valid)
            graph_data.max_d = max(graph_data.max_d, xdd);
        graph_data.min_p = min(graph_data.min_p, graph_data.y_data[i]);
        graph_data.max_p = max(graph_data.y_data[i], graph_data.max_p);
    }

    return graph_data;
}

FOUNDATION_STATIC void pattern_render_graph_limit(const char* label, double min, double max, double value)
{
    const double range[]{ min, max };
    const double limit[]{ value, value };
    ImPlot::PlotLine(label, range, limit, ARRAY_COUNT(limit), ImPlotLineFlags_NoClip);
}

FOUNDATION_STATIC void pattern_render_graph_limit(const char* label, pattern_graph_data_t& graph, double value)
{
    pattern_render_graph_limit(label, graph.min_d, graph.max_d, value);
}

FOUNDATION_STATIC void pattern_render_graph_end(pattern_t* pattern, const stock_t* s, pattern_graph_data_t& graph)
{
    if (graph.compact)
        return;

    if ((graph.refresh || !pattern->autofit) && (s == nullptr || s->has_resolve(FETCH_ALL)))
    {
        ImPlot::SetNextAxesToFit();
        pattern->autofit = true;
        graph.refresh = false;
    }
}

FOUNDATION_STATIC void pattern_render_graph_trends(pattern_t* pattern, pattern_graph_data_t& graph, ImVec2 graph_size = {})
{
    const stock_t* s = pattern->stock;
    if (s == nullptr)
    {
        ImGui::TextUnformatted("No data");
        return;
    }

    const auto& plot_screen_pos = ImGui::GetCursorScreenPos();

    if (graph_size.x == 0)
        graph_size = ImVec2(-ImGui::GetStyle().CellPadding.x, -ImGui::GetStyle().CellPadding.y);

    ImPlotFlags flags = ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle;
    if (graph.compact)
        flags = ImPlotFlags_CanvasOnly;
    if (!ImPlot::BeginPlot("Pattern Trends##1", graph_size, flags))
        return;

    ImPlot::SetupLegend(ImPlotLocation_NorthWest);

    static time_t trend_date = /*1663606289*/ time_now();
    const size_t iteration_count = (size_t)pattern->range + (pattern->date - trend_date) / time_one_day();

    const double trend_min_d = max(graph.min_d, 1.0);
    const double trend_max_d = pattern->range + math_ceil(iteration_count / 4.3) * 2.0;
    ImPlotAxisFlags trend_axis_flags = ImPlotAxisFlags_LockMin | ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight | (pattern->x_axis_inverted ? ImPlotAxisFlags_Invert : 0);
    if (graph.compact)
        trend_axis_flags |= ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_AutoFit;

    ImPlot::SetupAxis(ImAxis_X1, "##Days", trend_axis_flags);
    ImPlot::SetupAxisLimits(ImAxis_X1, trend_min_d, trend_max_d, ImPlotCond_Once);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, trend_min_d, trend_max_d);
    ImPlot::SetupAxisFormat(ImAxis_X1, plot_value_format_elapsed_time_short, nullptr);
    if (pattern->range > 365 * 2)
    {
        ImPlot::SetupAxisTicks(ImAxis_X1, graph.x_data, (int)graph.x_count - (graph.x_data[10] > graph.x_data[11] ? 1 : 0), DAY_LABELS, false);
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    }
    else
    {
        ImPlot::SetupAxisTicks(ImAxis_X1, trend_min_d, trend_max_d, 10);
    }

    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_format_date_label, &graph);

    ImPlotAxisFlags trend_axis_flags_y = ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoSideSwitch;
    if (graph.compact)
        trend_axis_flags_y |= ImPlotAxisFlags_NoLabel | ImPlotAxisFlags_NoTickLabels | ImPlotAxisFlags_AutoFit;
    ImPlot::SetupAxis(ImAxis_Y1, "##Values", trend_axis_flags_y);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.4g");

    // Render limits
    if (pattern->show_limits)
    {
        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
        pattern_render_graph_limit(tr("Zero"), graph, 0);

        ImPlot::PopStyleVar(1);
    }

    if (s->has_resolve(FetchLevel::TECHNICAL_SLOPE | FetchLevel::TECHNICAL_CCI))
    {
        ImPlot::SetAxis(ImAxis_Y1);
        plot_context_t c{ trend_date, min(s->history_count, iteration_count), 1, s->history };
        c.show_trend_equation = pattern->show_trend_equation;
        c.lx = 0.0;
        c.ly = (math_ifnan(s->beta, 0.5) + math_ifnan(s->short_ratio - 1.0, 0.0))
            * math_ifzero(max(max(math_ifnan(s->pe, 1.0), s->forward_pe), s->revenue_per_share_ttm), 1.0) 
            * math_ifzero(math_abs(s->profit_margin), 1.0)
            * math_ifzero(s->peg, math_ifzero(s->pe, 1.0));
        c.lz = s->diluted_eps_ttm * 2.0;
        c.acc = pattern->range;
        c.x_axis_inverted = pattern->x_axis_inverted;
        ImPlot::PlotLineG("##Slopes", [](int idx, void* user_data)->ImPlotPoint
        {
            plot_context_t* c = (plot_context_t*)user_data;
            constexpr const time_t ONE_DAY = time_one_day();

            const day_result_t* history = (const day_result_t*)c->user_data;
            const day_result_t* ed = &history[idx];
            if (idx == 0 || (ed->date / ONE_DAY) >= (c->ref / ONE_DAY))
                return ImPlotPoint(DNAN, DNAN);

            size_t send = array_size(history);
            int yedi = idx + math_round(c->acc);
            if (yedi >= send)
                return ImPlotPoint(DNAN, DNAN);

            if (c->lx == 0)
            {
                const day_result_t* yed = &history[idx + yedi];
                c->lx = yed->adjusted_close;
            }
            double ps = (ed->ema - ed->sar) / ed->sar;
            double x = math_round((c->ref - ed->date) / (double)ONE_DAY);
            double y = ed->slope * ps * c->lx * c->ly;

            if (!plot_build_trend(*c, x, y))
                return ImPlotPoint(DNAN, DNAN);

            return ImPlotPoint(x, y);
        }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);
            
        plot_compute_trend(c);
        plot_render_trend(tr("Trend"), c);
    }
    else
    {
        ImPlot::Annotation((trend_max_d - trend_min_d) / 2.0, 0, ImVec4(0.8f, 0.6f, 0.54f, 0.8f), ImVec2(0,-10), true, tr("Loading data..."));
    }

    ImPlot::EndPlot();

    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
    {
        ImGui::SetCursorScreenPos(ImVec2(plot_screen_pos.x + 350, plot_screen_pos.y + 20));
        ImGui::SetNextItemWidth(250.0f);
        tm tm_date = *localtime(&trend_date);
        if (ImGui::DateChooser("##Date", tm_date, "%Y-%m-%d", true))
            trend_date = mktime(&tm_date);
    }
    pattern_render_graph_end(pattern, s, graph);
}

FOUNDATION_STATIC float pattern_render_decisions(pattern_t* pattern)
{
    ImGuiTableFlags flags =
        ImGuiTableFlags_NoSavedSettings |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_NoHostExtendY |
        ImGuiTableFlags_PreciseWidths |
        ImGuiTableFlags_NoBordersInBody |
        //ImGuiTableFlags_NoClip | 
        ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX;

    if (!ImGui::BeginTable("Decisions", 3, flags))
        return 0;

    string_const_t code = SYMBOL_CONST(pattern->code);

    ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthFixed, IM_SCALEF(25));
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, IM_SCALEF(20));
    ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
    //ImGui::TableHeadersRow();

    if (pattern_render_decision_mark(pattern, 1, 
        "Price trends are positive.", 
        "It's important to examine the price trend of the company to determine if it's growing or declining. "
        "\n\nPrice trends such as 50-day moving average, 200-day moving average, and 52-week high can help evaluate a company's financial performance."
        "\n\nMarket trends can impact the stock price. Investors should monitor market trends to determine if the company is likely to outperform or "
        "under perform the market as a whole."
        "\n " ICON_MD_CHECK_BOX " Check the Trend graphic"
        "\n " ICON_MD_CHECK_BOX " Check the Market Trend (EMA, SMA, WMA, etc.)"
        "\n"))
    {
        pattern->type = (int)PATTERN_GRAPH_TRENDS;
    }

    if (pattern->type != PATTERN_GRAPH_TRENDS)
    {
        pattern_graph_data_t gd = pattern_render_build_graph_data(pattern);
        gd.compact = true;
        pattern_render_graph_trends(pattern, gd, { -ImGui::GetStyle().CellPadding.x, IM_SCALEF(100) });
    }

    if (pattern_render_decision_mark(pattern, 2, 
        "Company fundamentals and diversification are considered",
        "It's important to understand the company's fundamentals, including its business model, competition, and future prospects. "
        "If the company has a competitive advantage and positive future prospects, it can be a good sign for investors."
        "\n\nInvestors should be aware of the importance of diversifying their investment portfolio. "
        "It's recommended not to invest all funds in one stock but to diversify the portfolio by investing in different companies and industries."
        "\n " ICON_MD_CHECK_BOX " Check the company website"
        "\n " ICON_MD_CHECK_BOX " Check the company's annual report"
        "\n"))
    {
        pattern->notes_opened = true;
        pattern->fundamentals_dialog_opened = true;
    }

    if (pattern_render_decision_mark(pattern, 3,
       "Recent events are positive.",
       "It's important to monitor recent events related to the company, such as financial results announcements, "
       "management changes, and product developments. These events can impact the stock price."
       "\n " ICON_MD_CHECK_BOX " Check the company's news"
       "\n " ICON_MD_CHECK_BOX " Check the company's social media"
       "\n " ICON_MD_CHECK_BOX " Check the Activity graphic"
       "\n"))
    {
        news_open_window(STRING_ARGS(code));
    }

    if (pattern_render_decision_mark(pattern, 4,
        "Financial performance",
        "It's important to examine the financial performance of the company over the last few quarters to determine if it's growing or declining. "
        "Financial ratios such as price-to-earnings ratio, price-to-book ratio, and dividend yield ratio can help evaluate a company's financial performance."
        "\n " ICON_MD_CHECK_BOX " Check the Financials charts"
        "\n " ICON_MD_CHECK_BOX " Check the company's financial ratios"
        "\n " ICON_MD_CHECK_BOX " Check the company's financial statements"
        "\n"))
    {
        financials_open_window(STRING_ARGS(code));
    }

    if (pattern_render_decision_mark(pattern, 5,
        "Stock liquidity",
        "It's important to choose a stock that is sufficiently liquid so that the investor can buy and sell quickly and easily without "
        "suffering significant losses due to lack of liquidity."
        "\n " ICON_MD_CHECK_BOX " Check the daily transaction volume."
        "\n " ICON_MD_CHECK_BOX " Check the company capitalization."
        "\n " ICON_MD_CHECK_BOX " Check the company's market share"
        "\n"))
    {
        pattern->type = (int)PATTERN_GRAPH_YOY;
    }

    if (pattern_render_decision_mark(pattern, 6,
        "Stock volatility",
        "Investors should be aware of the stock's volatility, or the extent to which the stock price fluctuates. "
        "More volatile stocks may offer higher potential gains but also carry higher risk."
        "\n " ICON_MD_CHECK_BOX " Beta is higher or equal to 90%."
        "\n " ICON_MD_CHECK_BOX " Flex difference is higher than 6%."
        "\n " ICON_MD_CHECK_BOX " Sell limit is higher or equal to 3%."
        "\n"))
    {
        pattern->type = (int)PATTERN_GRAPH_FLEX;
    }

    if (pattern_render_decision_mark(pattern, 7,
        "Target limits are interesting",
        "Analyst opinions can provide an indication of the stock's future direction. "
        "Investors may consider analyst opinions to get an idea of the company's prospects."
        "\n " ICON_MD_CHECK_BOX " Check the Wall Street target"
        "\n " ICON_MD_CHECK_BOX " Check the year low and year high"
        "\n"))
    {
        pattern->type = (int)PATTERN_GRAPH_DEFAULT;
    }

    if (pattern_render_decision_mark(pattern, 8,
        "Company perspectives are positive. (MAX >= 25%)",
        "It's important to compare the company with its peers to determine if it's growing or declining. Also take a close look to the company's "
        "financial performance and future prospects year after year."
        "\n" ICON_MD_CHECK_BOX " Look for green value!"
        "\n" ICON_MD_CHECK_BOX " Is company dividend yield high?"
        "\n"))
    {
        pattern->type = (int)PATTERN_GRAPH_ANALYSIS;
    }

    float y_offset = ImGui::GetCursorPosY();
    ImGui::EndTable();
    return y_offset;
}

FOUNDATION_STATIC void pattern_render_graph_change_high(pattern_t* pattern, const stock_t* s)
{
    const size_t max_render_count = 1024;
    plot_context_t c{ pattern->date, s->history_count, (s->history_count / max_render_count) + 1, s->history };
    c.show_trend_equation = pattern->show_trend_equation;
    ImPlot::PlotLineG("Flex H", [](int idx, void* user_data)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)user_data;
        const day_result_t* history = (const day_result_t*)c->user_data;

        size_t ed_index = min(idx * c->stride, c->range - 1);
        const day_result_t* ed = &history[ed_index];
        double x = math_round((c->ref - ed->date) / (double)time_one_day());
        double y = ed->change_p_high;
        return ImPlotPoint(x, y);
    }, & c, (int)min(s->history_count, max_render_count), ImPlotLineFlags_Shaded);
}

FOUNDATION_STATIC void pattern_render_graph_change(pattern_t* pattern, const stock_t* s)
{
    const size_t max_render_count = 1024;
    plot_context_t c{ pattern->date, s->history_count, (s->history_count / max_render_count) + 1, s->history };
    c.show_trend_equation = pattern->show_trend_equation;
    ImPlot::PlotLineG("Flex L", [](int idx, void* context)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)context;
        const day_result_t* history = (const day_result_t*)c->user_data;

        size_t ed_index = min(idx * c->stride, c->range - 1);
        const day_result_t* ed = &history[ed_index];
        double x = math_round((c->ref - ed->date) / (double)time_one_day());
        double y = ed->change_p;
        return ImPlotPoint(x, y);
    }, & c, (int)min(s->history_count, max_render_count), ImPlotLineFlags_Shaded);
}

FOUNDATION_STATIC void pattern_render_graph_change_acc(pattern_t* pattern, const stock_t* s)
{
    ImPlot::HideNextItem(true, ImPlotCond_Once);
    plot_context_t c{ pattern->date, min(s->history_count, (size_t)pattern->range), 1, s->history, 0.0 };
    c.show_trend_equation = pattern->show_trend_equation;
    ImPlot::PlotLineG("% Acc.", [](int idx, void* context)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)context;
        const day_result_t* history = (const day_result_t*)c->user_data;

        size_t ed_index = max((size_t)0, (size_t)min(idx * c->stride, c->range - 1));
        const day_result_t* ed = &history[c->range - ed_index - 1];
        if ((ed->date/ time_one_day()) >= (c->ref/time_one_day()))
            return ImPlotPoint(DNAN, DNAN);

        c->acc += ed->change_p;
        c->lx = math_round((c->ref - ed->date) / (double)time_one_day());
        double y = c->acc;
        return ImPlotPoint(c->lx, y);
    }, &c, (int)c.range, ImPlotLineFlags_None);

    if (c.acc != 0)
        ImPlot::Annotation(pattern->range, c.acc, ImVec4(1.0f, 0, 0, 1.0f), ImVec2(4, -4), true, true);
}

FOUNDATION_STATIC void pattern_render_graph_day_value(const char* label, pattern_t* pattern, const stock_t* s, ImAxis y_axis, size_t offset, bool x_axis_inverted, bool relative_dates = true)
{
    plot_context_t c{ pattern->date, min((size_t)4096, s->history_count), offset, s->history };
    c.show_trend_equation = pattern->show_trend_equation;
    c.acc = pattern->range;
    c.mouse_pos = ImPlot::GetPlotMousePos();
    c.relative_dates = relative_dates;
    c.x_axis_inverted = pattern->x_axis_inverted;

    ImPlot::SetAxis(y_axis);
    ImPlot::PlotLineG(label, [](int idx, void* context)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)context;
        const day_result_t* history = (const day_result_t*)c->user_data;
        constexpr const time_t ONE_DAY = time_one_day();

        const day_result_t* ed = &history[idx];
        if ((ed->date / ONE_DAY) >= (c->ref / ONE_DAY))
            return ImPlotPoint(DNAN, DNAN);

        double x = c->relative_dates ? math_round((c->ref - ed->date) / (double)ONE_DAY) : ed->date;
        double y = *(const double*)(((const uint8_t*)ed)+c->stride);

        if (time_elapsed_days(ed->date, c->ref) <= c->acc)
            plot_build_trend(*c, x, y);

        return ImPlotPoint(x, y);
    }, &c, (int)c.range, ImPlotLineFlags_SkipNaN | ImPlotLineFlags_Segments);

    if (c.n > 0 && pattern->show_limits && relative_dates)
    {
        plot_compute_trend(c);
        ImPlot::HideNextItem(true, ImPlotCond_Once);
        plot_render_trend(label, c);
    }
}

FOUNDATION_STATIC void pattern_render_graph_price(pattern_t* pattern, const stock_t* s, ImAxis y_axis, bool x_axis_inverted)
{
    plot_context_t c{ pattern->date, min(size_t(8096), s->history_count), 1, s->history };
    c.show_trend_equation = pattern->show_trend_equation;
    c.acc = pattern->range;
    c.cursor_xy1 = { DBL_MAX, DNAN };
    c.cursor_xy2 = { DNAN, DNAN };
    c.mouse_pos = ImPlot::GetPlotMousePos();
    c.x_axis_inverted = pattern->x_axis_inverted;

    ImPlot::SetAxis(y_axis);
    ImPlot::PlotLineG(tr("Price"), [](int idx, void* context)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)context;
        const day_result_t* history = (const day_result_t*)c->user_data;
        constexpr const time_t ONE_DAY = time_one_day();

        const day_result_t& ed = history[idx];
        const double days_diff = time_elapsed_days(ed.date, c->ref);
        //const double x = math_round(days_diff);
        const double x = days_diff;
        const double y = ed.adjusted_close;

        if (days_diff <= c->acc)
            plot_build_trend(*c, x, y);

        if (math_real_is_finite(c->mouse_pos.x))
        {
            const double diffx = math_abs(c->mouse_pos.x - x);
            if (x < c->mouse_pos.x)
            {
                c->cursor_xy1 = ImPlotPoint(x, y);
            }
            else if (x > c->mouse_pos.x && math_real_is_nan(c->cursor_xy2.x))
            {
                c->cursor_xy2 = ImPlotPoint(x, y);
            }
            
        }

        return ImPlotPoint(x, y);
    }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);

    if (c.n > 0 && pattern->show_limits)
    {
        plot_compute_trend(c);
        plot_render_trend(tr("Price"), c);
    }

    if (ImPlot::IsPlotHovered() && !ImGui::IsAnyMouseDown() && math_real_is_finite(c.cursor_xy1.x) && math_real_is_finite(c.cursor_xy2.x))
    {
        // Interpolate the mouse position for xy1 and xy2 points
        const double x1 = c.cursor_xy1.x;
        const double y1 = c.cursor_xy1.y;
        const double x2 = c.cursor_xy2.x;
        const double y2 = c.cursor_xy2.y;
        const double x = c.mouse_pos.x;
        double y = 0;
        if (x_axis_inverted)
            y = (y2 - y1) / (x2 - x1) * (x - x1) + y1;
        else
            y = (y1 - y2) / (x1 - x2) * (x - x2) + y2;

        char date_buffer[16];
        const time_t then = pattern->date - (time_t)x * time_one_day();
        string_t date_str = string_from_date(STRING_BUFFER(date_buffer), then);
        const auto* ed = stock_get_EOD(pattern->stock, then, false);
        if (ed && math_real_is_finite(y))
        {
            float offset = -20.0f;
            if (c.mouse_pos.y < y)
                offset = 40.0f;
            ImPlot::Annotation(x, c.mouse_pos.y, (ImColor)IM_COL32(55, 55, 55, 155), { 0, offset }, true,
                "%s %10.*s  \nPrice: %6.2lf $\n  SMA: %6.2lf $", 
                ed->slope > 0 ? ICON_MD_TRENDING_UP : ICON_MD_TRENDING_DOWN,
                STRING_FORMAT(date_str), y, math_ifnan(ed->sma, 0));
        }
        ImPlot::Annotation(x, y, (ImColor)IM_COL32(55, 55, 55, 5), {0, 0}, false, ICON_MD_CIRCLE);

        //ImPlot::Annotation(c.cursor_xy1.x, c.cursor_xy1.y, (ImColor)IM_COL32(155, 55, 55, 155), {0, 0}, false, ICON_MD_CIRCLE);
        //ImPlot::Annotation(c.cursor_xy2.x, c.cursor_xy2.y, (ImColor)IM_COL32(55, 155, 55, 155), {0, 0}, false, ICON_MD_CIRCLE);
    }
}

FOUNDATION_STATIC bool pattern_flex_update(pattern_t* pattern)
{
    const stock_t* s = pattern->stock;
    if (!s || array_size(s->history) == 0)
        return false;

    if (pattern->flex == nullptr)
        array_reserve(pattern->flex, PATTERN_FLEX_RANGE_COUNT);
    array_clear(pattern->flex);

    double* buy_values = nullptr;
    double* execute_values = nullptr;

    bool first = true;
    const day_result_t& c = s->current;
    constexpr const double one_day = (double)time_one_day();
    for (int i = min(PATTERN_FLEX_RANGE_COUNT, array_size(s->history)) - 1; i >= 0; --i)
    {
        pattern_flex_t f{};
        const day_result_t& ed = s->history[i];

        f.history_index = i;
        f.days = math_round((pattern->date - ed.date) / one_day);

        if (first)
        {
            f.change_p = (ed.close / ed.open) - 1.0;
            first = false;
        }
        else
        {
            f.change_p = ((ed.change >= 0 ? ed.high : ed.low) / ed.previous_close) - 1.0;
        }

        if (f.change_p > 0) // E
        {
            execute_values = array_push(execute_values, f.change_p);
        }
        else if (f.change_p < 0) // B
        {
            buy_values = array_push(buy_values, f.change_p);
        }

        pattern->flex = array_push(pattern->flex, f);
    }

    pattern->flex_buy.medavg = math_median_average(buy_values, pattern->flex_buy.median, pattern->flex_buy.average);
    pattern->flex_execute.medavg = math_median_average(execute_values, pattern->flex_execute.median, pattern->flex_execute.average);

    double* buy_low_values = nullptr;
    for (size_t i = 0; i < array_size(buy_values); ++i)
    {
        if (buy_values[i] <= pattern->flex_buy.median)
            buy_low_values = array_push(buy_low_values, buy_values[i]);
    }

    double* execute_high_values = nullptr;
    for (size_t i = 0; i < array_size(execute_values); ++i)
    {
        if (execute_values[i] <= pattern->flex_execute.median)
            execute_high_values = array_push(execute_high_values, execute_values[i]);
    }

    pattern->flex_buy.medavg = math_median_average(buy_low_values, pattern->flex_buy.median, pattern->flex_buy.average);
    pattern->flex_execute.medavg = math_median_average(execute_high_values, pattern->flex_execute.median, pattern->flex_execute.average);

    array_deallocate(buy_values);
    array_deallocate(buy_low_values);
    array_deallocate(execute_values);
    array_deallocate(execute_high_values);

    return true;
}

FOUNDATION_STATIC bool pattern_flex_update(pattern_handle_t handle)
{
    pattern_t* pattern = &_patterns[handle];
    return pattern_flex_update(pattern);
}

FOUNDATION_STATIC int pattern_label_max_range(const pattern_graph_data_t& graph)
{
    for (int i = 0; i < (int)graph.x_count; i++)
    {
        if (graph.x_data[i] >= graph.max_d)
            return i + 1;
    }

    return (int)graph.x_count - (graph.x_data[10] > graph.x_data[11] ? 1 : 0);
}

FOUNDATION_STATIC void pattern_render_graph_setup_days_axis(pattern_t* pattern, pattern_graph_data_t& graph, bool x_axis_inverted)
{
    ImPlot::SetupAxis(ImAxis_X1, "##Days", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight | (x_axis_inverted ? ImPlotAxisFlags_Invert : 0));
    ImPlot::SetupAxisFormat(ImAxis_X1, plot_value_format_elapsed_time_short, nullptr);
    ImPlot::SetupAxisTicks(ImAxis_X1, graph.x_data, pattern_label_max_range(graph), nullptr/*DAY_LABELS*/, false);
    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_format_date_label, &graph);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, max(graph.min_d, 1.0), graph.max_d);
}

FOUNDATION_STATIC void pattern_render_graph_flex(pattern_t* pattern, pattern_graph_data_t& graph)
{
    if (pattern->flex == nullptr && !pattern_flex_update(pattern))
        return;

    if (array_size(pattern->flex) < 1)
        return;

    const ImVec2 graph_offset = ImVec2(-ImGui::GetStyle().CellPadding.x, -ImGui::GetStyle().CellPadding.y);
    if (!ImPlot::BeginPlot("Pattern Flex##1", graph_offset, ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle | (pattern->show_limits ? 0 : ImPlotFlags_NoLegend)))
        return;

    ImPlot::SetupLegend(ImPlotLocation_North);

    double min_d = pattern->flex[0].days, max_d = min_d;
    for (int i = 1, end = array_size(pattern->flex); i != end; ++i)
    {
        min_d = min(min_d, (double)pattern->flex[i].days);
        max_d = max(max_d, (double)pattern->flex[i].days);
    }

    ImPlot::SetupAxis(ImAxis_X1, "##Days", ImPlotAxisFlags_Lock | ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Invert);
    ImPlot::SetupAxisFormat(ImAxis_X1, plot_value_format_elapsed_time_short, nullptr);
    ImPlot::SetupAxisTicks(ImAxis_X1, min_d, max_d, 7, nullptr, false);
    ImPlot::SetupAxisLimits(ImAxis_X1, min_d, max_d, ImPlotCond_Once);
    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_format_date_label, &graph);

    ImPlot::SetupAxis(ImAxis_Y1, "##Pourcentage", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3g %%");

    plot_context_t c{ pattern->date, array_size(pattern->flex), 1, pattern->flex};
    c.show_trend_equation = pattern->show_trend_equation;
    ImPlot::PlotBarsG(tr("Flex"), [](int idx, void* user_data)->ImPlotPoint
    {
        const plot_context_t* c = (plot_context_t*)user_data;
        const pattern_flex_t* f = ((const pattern_flex_t*)c->user_data) + idx;
        const double x = f->days;
        const double y = f->change_p * 100.0;
        return ImPlotPoint(x, y);
    }, &c, (int)c.range, 0.42, ImPlotBarsFlags_None);

    if (pattern->show_limits)
    {
        const double fl = pattern->flex_low.fetch() * 100.0;
        const double fh = pattern->flex_high.fetch() * 100.0;
        pattern_render_graph_limit(tr("Low"), min_d, max_d, fl);
        pattern_render_graph_limit(tr("High"), min_d, max_d, fh);
        pattern_render_graph_limit(tr("Today"), min_d, max_d, pattern->stock->current.change_p);

        ImPlot::TagY(pattern->flex_high.fetch() * 100.0, ImColor::HSV(139 / 360.0f, 0.63f, 1.0f), ICON_MD_CHANGE_HISTORY " %.3g %%", fh - fl);
    }

    ImPlot::EndPlot();
    pattern_render_graph_end(pattern, nullptr, graph);
}

FOUNDATION_STATIC void pattern_render_graph_intraday(pattern_t* pattern, pattern_graph_data_t& graph)
{
    if (pattern->intradays == nullptr)
    {
        array_reserve(pattern->intradays, 1);
        const char* code = SYMBOL_CSTR(pattern->code);
        pattern_handle_t pattern_handle = pattern - _patterns;
        eod_fetch_async("intraday", code, FORMAT_JSON_CACHE, "interval", "1h", [pattern_handle](const auto& json)
        {
            double previous_close = DNAN;
            day_result_t* intradays = nullptr;
            for (auto e : json)
            {
                day_result_t intraday{};
                intraday.volume = e["volume"].as_number();

                if (math_real_is_nan(intraday.volume))
                    continue;

                intraday.ts = e["timestamp"].as_number();
                intraday.open = e["open"].as_number();
                intraday.adjusted_close = e["close"].as_number();
                intraday.price = intraday.adjusted_close;
                intraday.close = intraday.adjusted_close;
                intraday.low = e["low"].as_number();
                intraday.high = e["high"].as_number();
                intraday.change = intraday.close - intraday.open;
                intraday.previous_close = previous_close;
                previous_close = intraday.close;
                array_push_memcpy(intradays, &intraday);
            }

            if (intradays)
            {
                pattern_t* pattern = pattern_get(pattern_handle);
                if (pattern)
                {
                    day_result_t* old = pattern->intradays;
                    pattern->intradays = intradays;
                    dispatch([pattern_handle]()
                    {
                        pattern_t* pattern = pattern_get(pattern_handle);
                        if (pattern)
                            pattern->autofit = false;
                    });
                    array_deallocate(old);
                }
                else
                {
                    array_deallocate(intradays);
                }
            }
        }, 60 * 60 * 24ULL);
    }

    const size_t intraday_count = array_size(pattern->intradays);
    if (intraday_count <= 1)
        return ImGui::TextUnformatted("No data");

    const ImVec2 graph_offset = ImVec2(-ImGui::GetStyle().CellPadding.x, -ImGui::GetStyle().CellPadding.y);
    if (!ImPlot::BeginPlot("Pattern Intraday##1", graph_offset, ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle))
        return;

    ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);

    const double time_end = array_last(pattern->intradays)->ts;
    const double time_start = array_first(pattern->intradays)->ts;

    // The price graph is always shown inverted by default.
    ImPlot::SetupAxis(ImAxis_X1, "##Days", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, time_start, time_end);
    ImPlot::SetupAxisFormat(ImAxis_X1, plot_value_format_date, nullptr);

    ImPlot::SetupAxis(ImAxis_Y1, "##Currency", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoSideSwitch | ImPlotAxisFlags_Opposite);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0.0, INFINITY);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.2lf $");

    plot_context_t c{ pattern->date, intraday_count, 1, pattern->intradays };
    c.show_trend_equation = pattern->show_trend_equation;
    c.acc = pattern->range;
    c.cursor_xy1 = { DBL_MAX, DNAN };
    c.cursor_xy2 = { DNAN, DNAN };
    c.mouse_pos = ImPlot::GetPlotMousePos();
    c.x_axis_inverted = pattern->x_axis_inverted;

    ImPlot::PlotLineG(tr("Price"), [](int idx, void* user_data)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)user_data;
        const day_result_t* history = (const day_result_t*)c->user_data;
        constexpr time_t ONE_DAY = time_one_day();

        const day_result_t& ed = history[idx];
        const double x = ed.ts;
        const double y = ed.price;

        plot_build_trend(*c, x, y);

        if (math_real_is_finite(c->mouse_pos.x))
        {
            const double diffx = math_abs(c->mouse_pos.x - x);
            if (x < c->mouse_pos.x)
                c->cursor_xy1 = ImPlotPoint(x, y);
            else if (x > c->mouse_pos.x && math_real_is_nan(c->cursor_xy2.x))
                c->cursor_xy2 = ImPlotPoint(x, y);
        }

        return ImPlotPoint(x, y);
    }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);

    if (ImPlot::GetPlotLimits().X.Size() < time_one_day() * 3.0)
    {
        day_result_t* it = pattern->intradays;
        ImPlot::PlotErrorBars(tr("Price"), &it->ts, &it->price, &it->change,
            to_int(intraday_count), ImPlotErrorBarsFlags_None, 0, sizeof(day_result_t));
    }

    if (ImGui::IsWindowHovered() && math_real_is_finite(c.cursor_xy1.x) && math_real_is_finite(c.cursor_xy2.x))
    {
        // Interpolate the mouse position for xy1 and xy2 points
        const double x1 = c.cursor_xy1.x;
        const double y1 = c.cursor_xy1.y;
        const double x2 = c.cursor_xy2.x;
        const double y2 = c.cursor_xy2.y;
        const double x = c.mouse_pos.x;
        const double y = (y1 - y2) / (x1 - x2) * (x - x2) + y2;

        char date_buffer[32];
        const time_t ts = (time_t)x;
        string_t date_str = localization_string_from_time(STRING_BUFFER(date_buffer), ts * 1000ULL, false);
        const auto* ed = stock_get_EOD(pattern->stock, ts, false);
        if (ed)
        {
            float offset = -20.0f;
            if (c.mouse_pos.y < y)
                offset = 40.0f;

            const double change_p = (ed->close - ed->previous_close) / ed->previous_close * 100.0;

            ImPlot::Annotation(x, c.mouse_pos.y, (ImColor)IM_COL32(55, 55, 55, 155), { 0, offset }, true,
                tr("%s %10.*s \n Price: %5.2lf $ (%.2g %%)\n   SMA: %5.2lf $"), 
                ed->slope > 0 ? ICON_MD_TRENDING_UP : ICON_MD_TRENDING_DOWN,
                STRING_FORMAT(date_str), y, change_p, ed->sma);
            ImPlot::Annotation(x, y, (ImColor)IM_COL32(55, 55, 55, 5), {0, 0}, false, ICON_MD_CIRCLE);
        }
    }

    const stock_t* s = pattern->stock;
    if (s)
    {
        pattern_render_graph_day_value("SMA", pattern, s, ImAxis_Y1, offsetof(day_result_t, sma), false, false);
        pattern_render_graph_day_value("EMA", pattern, s, ImAxis_Y1, offsetof(day_result_t, ema), false, false);
        pattern_render_graph_day_value("WMA", pattern, s, ImAxis_Y1, offsetof(day_result_t, wma), false, false);

        ImPlot::TagY(s->low_52, ImColor::HSV(29 / 360.0f, 0.63f, 1.0f), "Low 52");
        ImPlot::TagY(s->high_52, ImColor::HSV(149 / 360.0f, 0.63f, 1.0f), "High 52");
        ImPlot::TagY(s->current.low, ImColor::HSV(39 / 360.0f, 0.63f, 1.0f), "Low");
        ImPlot::TagY(s->current.high, ImColor::HSV(139 / 360.0f, 0.63f, 1.0f), "High");

        ImPlot::TagY(s->dma_50, ImColor::HSV(339 / 360.0f, 0.63f, 1.0f), "DMA");
        ImPlot::TagY(s->ws_target, ImColor::HSV(349 / 360.0f, 0.63f, 1.0f), "WS");
    }

    plot_compute_trend(c);
    plot_render_trend(tr("Trend"), c);

    ImPlot::EndPlot();
    pattern_render_graph_end(pattern, s, graph);
}

FOUNDATION_STATIC void pattern_render_graph_yoy(pattern_t* pattern, pattern_graph_data_t& graph)
{
    const unsigned yy_count = array_size(pattern->yy);
    if (yy_count <= 1)
    {
        ImGui::TextUnformatted("No data");
        return;
    }

    const ImVec2 graph_offset = ImVec2(-ImGui::GetStyle().CellPadding.x, -ImGui::GetStyle().CellPadding.y);
    if (!ImPlot::BeginPlot("Pattern YOY##1", graph_offset, ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle))
        return;

    ImPlot::SetupLegend(ImPlotLocation_NorthWest);

    const double time_beg = (double)array_first(pattern->yy)->beg;
    const double time_end = (double)array_last(pattern->yy)->end;

    ImPlot::SetupAxis(ImAxis_X1, "##Days", ImPlotAxisFlags_LockMax | ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisLimits(ImAxis_X1, time_beg, time_end, ImPlotCond_Once);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, time_beg, time_end);
    ImPlot::SetupAxisFormat(ImAxis_X1, plot_value_format_year, &graph);
    
    ImPlot::SetupAxis(ImAxis_Y1, "##Percentage", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoSideSwitch);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3g %%");

    ImPlot::PlotBarsG("##Slopes", [](int idx, void* user_data)->ImPlotPoint
    {
        const pattern_t::yy_t* data = (const pattern_t::yy_t*)user_data;
        const pattern_t::yy_t* c = data + idx;
        
        const double x = (c->end + c->beg) / 2.0;
        const double y = c->change_p;

        return ImPlotPoint(x, y);
    }, &pattern->yy[0], (int)yy_count, time_one_day() * 180.0, ImPlotBarsFlags_None);

    ImPlot::EndPlot();
}

FOUNDATION_STATIC void pattern_render_graph_price(pattern_t* pattern, pattern_graph_data_t& graph)
{
    // FIXME: stock would need to be locked here...
    const stock_t* s = pattern->stock;
    if (s == nullptr || !s->has_resolve(FetchLevel::REALTIME | FetchLevel::EOD))
        return;

    if (!pattern->autofit && !math_real_is_nan(pattern->price_limits.xmin))
    {
        pattern->autofit = true;
        ImPlotRect limits_rect = *(const ImPlotRect*)&pattern->price_limits;
        ImPlot::SetNextAxesLimits(pattern->price_limits.xmin, pattern->price_limits.xmax, pattern->price_limits.ymin, pattern->price_limits.ymax, ImGuiCond_Once);
    }

    const ImVec2 graph_offset = ImVec2(-ImGui::GetStyle().CellPadding.x, -ImGui::GetStyle().CellPadding.y);
    if (!ImPlot::BeginPlot("Pattern Price##2", graph_offset, ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle))
        return;

    ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);

    // The price graph is always shown inverted by default.
    const bool x_axis_inverted = !pattern->x_axis_inverted;
    ImPlot::SetupAxis(ImAxis_X1, "##Days", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight | (x_axis_inverted ? ImPlotAxisFlags_Invert : 0));
    ImPlot::SetupAxisFormat(ImAxis_X1, plot_value_format_elapsed_time_short, nullptr);
    ImPlot::SetupAxisTicks(ImAxis_X1, graph.x_data, pattern_label_max_range(graph), nullptr/*DAY_LABELS*/, false);
    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_format_date_label, &graph);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, max(graph.min_d, 1.0), graph.max_d);

    ImPlot::SetupAxis(ImAxis_Y1, "##Currency", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoSideSwitch | ImPlotAxisFlags_Opposite);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0.0, INFINITY);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.2lf $");

    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.0f);

    if (s->has_resolve(FetchLevel::TECHNICAL_SMA))
    {
        pattern_render_graph_day_value("SMA", pattern, s, ImAxis_Y1, offsetof(day_result_t, sma), x_axis_inverted);
    }

    if (s->has_resolve(FetchLevel::TECHNICAL_EMA))
    {
        ImPlot::HideNextItem(true, ImPlotCond_Once);
        pattern_render_graph_day_value("EMA", pattern, s, ImAxis_Y1, offsetof(day_result_t, ema), x_axis_inverted);
    }

    if (s->has_resolve(FetchLevel::TECHNICAL_WMA))
    {
        ImPlot::HideNextItem(true, ImPlotCond_Once);
        pattern_render_graph_day_value("WMA", pattern, s, ImAxis_Y1, offsetof(day_result_t, wma), x_axis_inverted);
    }

    if (pattern->extra_charts && s->has_resolve(FetchLevel::TECHNICAL_SAR))
    {
        ImPlot::HideNextItem(true, ImPlotCond_Once);
        pattern_render_graph_day_value("SAR", pattern, s, ImAxis_Y1, offsetof(day_result_t, sar), x_axis_inverted);
    }

    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
    pattern_render_graph_price(pattern, s, ImAxis_Y1, x_axis_inverted);

    ImPlot::TagY(s->low_52, ImColor::HSV(29 / 360.0f, 0.63f, 1.0f), "Low 52");
    ImPlot::TagY(s->high_52, ImColor::HSV(149 / 360.0f, 0.63f, 1.0f), "High 52");
    ImPlot::TagY(s->current.low, ImColor::HSV(39 / 360.0f, 0.63f, 1.0f), "Low");
    ImPlot::TagY(s->current.high, ImColor::HSV(139 / 360.0f, 0.63f, 1.0f), "High");

    ImPlot::TagY(s->dma_50, ImColor::HSV(339 / 360.0f, 0.63f, 1.0f), "DMA");
    ImPlot::TagY(s->ws_target, ImColor::HSV(349 / 360.0f, 0.63f, 1.0f), "WS");

    if (s->history_count > 1)
    {
        double sd = s->history[0].slope - s->history[1].slope;
        ImPlot::TagY(s->current.adjusted_close + s->current.adjusted_close * sd, ImColor::HSV(239 / 360.0f, 0.73f, 1.0f), "PS %.2lf $", s->current.adjusted_close * sd);
    }

    ImPlot::TagY(s->current.adjusted_close, ImColor::HSV(239 / 360.0f, 0.63f, 1.0f), "Current");

    if (pattern->autofit)
    {
        const ImPlotRect limits = ImPlot::GetPlotLimits();
        pattern->price_limits = *(const pattern_limits_t*)&limits;
    }

    // Render limits
    if (pattern->show_limits)
    {
        ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
        const double buy_flex = s->current.adjusted_close + (s->current.adjusted_close * pattern->flex_low.fetch());
        pattern_render_graph_limit(tr("Buy"), graph, buy_flex);
        ImPlot::TagY(buy_flex, ImPlot::GetLastItemColor(), tr("Buy"));

        const double sell_flex = s->current.adjusted_close + (s->current.adjusted_close * pattern->flex_high.fetch());
        pattern_render_graph_limit(tr("Sell"), graph, sell_flex);
        ImPlot::TagY(sell_flex, ImPlot::GetLastItemColor(), tr("Sell"));
        ImPlot::PopStyleVar(1);
    }

    ImPlot::PopStyleVar(1);

    ImPlot::EndPlot();
    pattern_render_graph_end(pattern, s, graph);
}

FOUNDATION_STATIC void pattern_render_graph_analysis(pattern_t* pattern, pattern_graph_data_t& graph)
{
    const stock_t* s = pattern->stock;
    const ImVec2 graph_offset = ImVec2(-ImGui::GetStyle().CellPadding.x, -ImGui::GetStyle().CellPadding.y);
    if (!s || !s->has_resolve(FetchLevel::EOD) || !ImPlot::BeginPlot("Pattern Graph##26", graph_offset, ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle))
        return;

    double max_days = math_round((pattern->date - pattern->marks[ARRAY_COUNT(FIXED_MARKS) - 1].date) / (double)time_one_day());
    ImPlot::SetupLegend(ImPlotLocation_SouthWest);

    pattern_render_graph_setup_days_axis(pattern, graph, pattern->x_axis_inverted);

    ImPlot::SetupAxis(ImAxis_Y1, "##Pourcentage", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3g %%");
    ImPlot::SetupAxisLimits(ImAxis_Y1, graph.min_p - 10.0, graph.max_p * 1.2, ImPlotCond_Once);
    
    if (pattern->extra_charts)
    {
        ImPlot::SetupAxis(ImAxis_Y2, "##Currency", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoGridLines | ImPlotAxisFlags_NoSideSwitch);
        ImPlot::SetupAxisFormat(ImAxis_Y2, "%.2lf $");

        pattern_render_graph_change_high(pattern, s);
        pattern_render_graph_change(pattern, s);
    }

    // Render limits
    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);

    if (pattern->show_limits)
    {
        pattern_render_graph_limit(tr("Flex Low"), graph, pattern->flex_low.get_or_default() * 100.0);
        pattern_render_graph_limit(tr("Flex High"), graph, pattern->flex_high.get_or_default() * 100.0);
        pattern_render_graph_limit(tr("WS"), graph, (pattern->stock->ws_target - pattern->stock->current.adjusted_close) / pattern->stock->current.adjusted_close * 100.0);
    }

    // Render patterns
    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 4.0f);
    ImPlot::PlotLine("Pattern", graph.x_data, graph.y_data, graph.x_count, ImPlotLineFlags_SkipNaN);
    ImPlot::PlotScatter("Pattern", graph.x_data, graph.y_data, graph.x_count, ImPlotLineFlags_SkipNaN);

    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.0f);
    pattern_render_graph_change_acc(pattern, s);

    ImPlot::PopStyleVar(3);

    ImPlot::EndPlot();
    pattern_render_graph_end(pattern, s, graph);
}

FOUNDATION_STATIC void pattern_history_min_max_price(pattern_t* pattern, time_t ref, double& min, double& max)
{
    min = DBL_MAX;
    max = -DBL_MAX;
    time_t last = 0;
    foreach (h, pattern->stock->history)
    {
        if (h->date < ref)
            break;
        max = ::max(max, h->adjusted_close);
        min = ::min(min, h->adjusted_close);
    }
}

FOUNDATION_STATIC void pattern_render_graph_zoom(pattern_t* pattern, pattern_graph_data_t& graph)
{
    double ymin = 0, ymax = 0;
    pattern_history_min_max_price(pattern, time_add_days(pattern->date, -pattern->range), ymin, ymax);
    const double delta_space = (ymax - ymin) * 0.05;
    ImPlot::SetNextAxesLimits(graph.min_d, pattern->range + 5, ymin - delta_space, ymax + delta_space, ImGuiCond_Always);
}

FOUNDATION_STATIC void pattern_render_graph_toolbar(pattern_t* pattern, pattern_graph_data_t& graph)
{
    int previous_graph_type = pattern->type;
    if (shortcut_executed('1')) pattern->type = PATTERN_GRAPH_DEFAULT;
    if (shortcut_executed('2')) pattern->type = PATTERN_GRAPH_ANALYSIS;
    if (shortcut_executed('3') || shortcut_executed('F')) pattern->type = PATTERN_GRAPH_FLEX;
    if (shortcut_executed('4') || shortcut_executed('T')) pattern->type = PATTERN_GRAPH_TRENDS;
    if (shortcut_executed('5') || shortcut_executed('Y')) pattern->type = PATTERN_GRAPH_YOY;
    if (shortcut_executed('6') || shortcut_executed('Y')) pattern->type = PATTERN_GRAPH_INTRADAY;
    if (shortcut_executed('7') || shortcut_executed('A')) pattern->type = PATTERN_ACTIVITY;

    ImGui::SetNextItemWidth(IM_SCALEF(120));
    string_const_t graph_type_label_preview = string_to_const(GRAPH_TYPES[min(pattern->type, (int)ARRAY_COUNT(GRAPH_TYPES)-1)]);
    if (ImGui::BeginCombo("##Type", tr(STRING_ARGS(graph_type_label_preview), true).str, ImGuiComboFlags_None))
    {
        for (int n = 0; n < ARRAY_COUNT(GRAPH_TYPES); n++)
        {
            if (GRAPH_TYPES[n] == nullptr)
                continue;
            const bool is_selected = (pattern->type == n);
            if (ImGui::Selectable(tr(GRAPH_TYPES[n], 0, true).str, is_selected))
            {
                pattern->type = n;
                break;
            }

            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (previous_graph_type != pattern->type)
    {
        graph.refresh = true;
        pattern->autofit = false;
    }

    if (pattern->type != PATTERN_GRAPH_INTRADAY)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
        if (ImGui::SliderInt("##Range", &pattern->range, (int)graph.min_d, (int)graph.max_d, tr("%d days"), ImGuiSliderFlags_Logarithmic))
        {
            if (pattern->type == PATTERN_GRAPH_TRENDS)
                graph.refresh = true;
        }

        if (shortcut_executed(ImGuiKey_Z))
            pattern_render_graph_zoom(pattern, graph);
        if (pattern->type == PATTERN_GRAPH_DEFAULT && ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem(tr("Zoom")))
                pattern_render_graph_zoom(pattern, graph);
            ImGui::EndPopup();
        }

        if (pattern->type >= PATTERN_GRAPH_BEGIN && pattern->type < PATTERN_GRAPH_END)
        {
            ImGui::SameLine();
            if (ImGui::Checkbox(tr("Limits"), &pattern->show_limits))
                graph.refresh = true;

            if (pattern->type != PATTERN_GRAPH_YOY && pattern->type != PATTERN_GRAPH_INTRADAY)
            {
                ImGui::SameLine();
                if (ImGui::Checkbox(tr("Extra Charts"), &pattern->extra_charts))
                    graph.refresh = true;

                ImGui::SameLine();
                if (ImGui::Checkbox(tr("Invert Time"), &pattern->x_axis_inverted))
                    graph.refresh = true;
            }
        }
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Refresh")))
    {
        pattern_refresh(pattern);
        graph.refresh = true;
        pattern->autofit = false;
    }
}

FOUNDATION_STATIC pattern_activity_t* pattern_find_activity(pattern_activity_t* activities, time_t d)
{
    for (size_t i = 0; i < array_size(activities); ++i)
    {
        if (time_date_equal(activities[i].date, d))
            return &activities[i];
    }

    return nullptr;
}

FOUNDATION_STATIC void pattern_activity_min_max_date(pattern_activity_t* activities, time_t& min, time_t& max, double& space)
{
    min = time_now();
    max = 0;
    space = 1;
    time_t last = 0;
    foreach (h, activities)
    {
        if (last != 0)
            space = math_round(time_elapsed_days(h->date, last));
        last = h->date;
        max = ::max(max, h->date);
        min = ::min(min, h->date);
    }
}

FOUNDATION_STATIC bool pattern_render_fundamental_field_tooltip(pattern_t* pattern, string_const_t field_name, string_const_t value_string)
{
    if (!ImGui::IsItemHovered() || !openai_available())
        return false;

    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return false;

    char value_copy_buffer[128];
    string_t value_copy = string_copy(STRING_BUFFER(value_copy_buffer), STRING_ARGS(value_string));

    char buffer[2048];
    string_const_t company_name = stock_get_name(pattern->stock);
    string_t p1 = tr_format(STRING_BUFFER(buffer),
        "Can you explain what the value {0} for {1} means given that this is associated to the public company {2}. "
        "Also please explain briefly what {1} means for an investor and if it is good or not regarding {2}. "
        "Please reword any \"CamelCase\" words to something understandable and convert numerical values into the appropriate unit, i.e. $, %, etc.---\n",
        value_copy, field_name, company_name);

    auto field_info = shared_ptr<pattern_fundamentals_field_info_t>::create(HASH_PATTERN);
    field_info->response = {};

    openai_completion_options_t options{};
    options.max_tokens = 250;
    options.temperature = 0.4f;
    options.frequency_penalty = -0.4f;
    if (openai_complete_prompt(STRING_ARGS(p1), options, [field_info](string_t response) {
        log_info(HASH_PATTERN, STRING_ARGS(response));
        field_info->response = response;
    })) 
    {
        static int occ = 0;    
        app_open_dialog(tr_format("Field Description - {0}##{1}", field_name, ++occ), [field_info](void* context)
        {
            if (field_info->response.length != 0)
                ImGui::TextWrapped("%.*s", STRING_FORMAT(field_info->response));
            else
                ImGui::TextWrapped(tr("Fetching field information..."));
            return true;
        }, IM_SCALEF(400), IM_SCALEF(300), true, nullptr, [field_info](void* context)
        {
            string_deallocate(field_info->response.str);
        });

        return true;
    }

    return false;
}

FOUNDATION_STATIC void pattern_render_fundamentals_object(pattern_t* pattern, config_handle_t obj, int level = 0)
{
    for (auto e : obj)
    {
        auto type = config_value_type(e);
        if (type == CONFIG_VALUE_OBJECT || type == CONFIG_VALUE_ARRAY)
        {
            if (config_size(e) == 0)
                continue;

            char id_buffer[64];
            string_const_t cv_id = config_name(e);
            string_t id = string_copy(STRING_BUFFER(id_buffer), STRING_ARGS(cv_id));

            double d;
            if (string_try_convert_number(STRING_ARGS(cv_id), d))
            {
                auto cv_name = e["Name"];
                if (cv_name)
                {
                    string_const_t cv_name_str = cv_name.as_string();
                    id = string_copy(STRING_BUFFER(id_buffer), STRING_ARGS(cv_name_str));
                }
                else
                {
                    cv_name = e["name"];
                    if (cv_name)
                    {
                        string_const_t cv_name_str = cv_name.as_string();
                        id = string_copy(STRING_BUFFER(id_buffer), STRING_ARGS(cv_name_str));
                    }
                }
            }

            if (ImGui::TreeNode(id.str))
            {
                ImGui::NextColumn();
                ImGui::NextColumn();
                pattern_render_fundamentals_object(pattern, e, level + 1);
                ImGui::TreePop();
            }
            ImGui::NextColumn();

            ImGui::Dummy({0,0});
            ImGui::NextColumn();
        }
    }

    for (auto e : obj)
    {
        auto type = config_value_type(e);

        if (type == CONFIG_VALUE_ARRAY || type == CONFIG_VALUE_OBJECT)
            continue;

        if (config_is_null(e))
            continue;

        string_const_t cv_id = config_name(e);

        // Skip field id with "name"
        if (string_equal_nocase(STRING_ARGS(cv_id), STRING_CONST("name")))
            continue;

        string_const_t cv_value = e.as_string();

        ImGui::TextUnformatted(STRING_RANGE(cv_id));
        pattern_render_fundamental_field_tooltip(pattern, cv_id, cv_value);
        ImGui::NextColumn();        
        
        ImGui::TextWrapped("%.*s", STRING_FORMAT(cv_value));
        pattern_render_fundamental_field_tooltip(pattern, cv_id, cv_value);
        ImGui::NextColumn();
    }
}

FOUNDATION_STATIC void pattern_render_fundamentals(pattern_t* pattern)
{
    if (!pattern->fundamentals_fetched)
    {
        const char* symbol = string_table_decode(pattern->code);
        eod_fetch_async("fundamentals", symbol, FORMAT_JSON_CACHE, [pattern](const json_object_t& json)
        {
            if (json.resolved())
                pattern->fundamentals = config_parse(STRING_LENGTH(json.buffer), CONFIG_OPTION_PRESERVE_INSERTION_ORDER);
            else
                pattern->fundamentals = config_allocate();
        });

        pattern->fundamentals_fetched = true;
    }
    else if (config_size(pattern->fundamentals) == 0)
    {
        ImGui::TrTextUnformatted("No data available");
    }
    else
    {
        if (ImGui::BeginChild("Fundamentals"))
        {
            ImGui::Columns(2, "FC##1", true);
            pattern_render_fundamentals_object(pattern, pattern->fundamentals);
            ImGui::Columns(1, "##STOP", false);
        }
        ImGui::EndChild();
    }
}

FOUNDATION_STATIC void pattern_render_activity(pattern_t* pattern, pattern_graph_data_t& graph)
{
    static hash_t activity_hash = 0;

    const hash_t chash = hash(pattern, sizeof(pattern_t));
    if (activity_hash != chash)
    {
        activity_hash = chash;
        array_clear(_activities);

        eod_fetch("news", nullptr, FORMAT_JSON_CACHE, "s", string_table_decode(pattern->code), "limit", "250", [](const json_object_t& json)
        {
            for (size_t i = 0; i < json.root->value_length; ++i)
            {
                const auto& e = json[i];
                string_const_t date_str = e["date"].as_string();
                if (date_str.length < 10)
                    continue;

                time_t d = string_to_date(date_str.str, 10);
                if (d == 0 || d == -1)
                    continue;

                pattern_activity_t* act = pattern_find_activity(_activities, d);
                if (act == nullptr)
                {
                    array_push(_activities, pattern_activity_t{ d });
                    act = array_last(_activities);
                }

                act->count++;
                
                const auto& sentiment = e["sentiment"];
                double p = sentiment["polarity"].as_number(NAN);
                if (!math_real_is_nan(p))
                    act->polarity += p;
            }

            array_sort(_activities, [](const pattern_activity_t& a, const pattern_activity_t& b)
            {
                return a.date - b.date;
            });
        }, 6 * 60 * 60ULL);
    }

    if (array_size(_activities) == 0)
        return;

    const ImVec2 graph_offset = ImVec2(-ImGui::GetStyle().CellPadding.x, -ImGui::GetStyle().CellPadding.y);
    if (!ImPlot::BeginPlot(string_format_static_const("Activity###%s", string_table_decode(pattern->code)), graph_offset, ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle))
        return;

    double day_space;
    time_t min_d, max_d;
    pattern_activity_min_max_date(_activities, min_d, max_d, day_space);

    const double day_range = time_elapsed_days(min_d, max_d);

    constexpr const double bar_width = time_one_day() * 0.8;
    ImPlot::SetupLegend(ImPlotLocation_NorthWest);

    ImPlot::SetupAxis(ImAxis_X1, "##Date", ImPlotAxisFlags_LockMax | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_X1, plot_value_format_date, nullptr);
    ImPlot::SetupAxisLimits(ImAxis_X1, (double)min_d - time_one_day() * 7, (double)max_d + time_one_day() * 7, ImPlotCond_Once);
    
    ImPlot::SetupAxis(ImAxis_Y1, "##Value", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3g");

    plot_context_t c{ pattern->date, min(365U, array_size(_activities)), 1, _activities };
    c.show_trend_equation = pattern->show_trend_equation;
    c.acc = pattern->range;
    c.x_axis_inverted = !pattern->x_axis_inverted;

    ImPlot::SetAxis(ImAxis_Y1);
    static int last_index = -1;
    ImPlot::PlotBarsG(tr("Polarity"), [](int idx, void* user_data)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)user_data;
        const pattern_activity_t* h = ((const pattern_activity_t*)c->user_data) + idx;
        const double x = (double)h->date;
        const double y = h->polarity / h->count;

        double diff = time_elapsed_days(h->date, c->ref);
        if (last_index != idx && h->count > 0 && diff <= c->acc)
        {
            plot_build_trend(*c, x, y);
            last_index = idx;
        }

        return ImPlotPoint(x, y);
    }, &c, (int)c.range, bar_width, ImPlotBarsFlags_None);

    plot_compute_trend(c);
    plot_render_trend(tr("Popularity"), c);

    ImPlot::PlotScatterG(tr("Hits"), [](int idx, void* user_data)->ImPlotPoint
    {
        const plot_context_t* c = (const plot_context_t*)user_data;
        const pattern_activity_t* h = ((const pattern_activity_t*)c->user_data) + idx;
        if (h->count <= 1 && h->polarity > 0)
            return ImPlotPoint(NAN, NAN);
        const double x = (double)h->date;
        return ImPlotPoint(x, (h->polarity < 0 ? 0 : 1.0) + (0.05 * h->count) * (h->polarity < 0 ? -1.0 : 1.0));
    }, &c, (int)c.range, ImPlotScatterFlags_NoClip);

    if (const day_result_t* history = pattern->stock->history)
    {
        plot_context_t c2{ pattern->date, min(1024U, array_size(history)), 1, history };
        c2.show_trend_equation = pattern->show_trend_equation;
        c2.acc = (double)min_d;
        c2.lx = (double)pattern->range;
        ImPlot::SetAxis(ImAxis_Y1);
        ImPlot::PlotScatterG(tr("Change"), [](int idx, void* user_data)->ImPlotPoint
        {
            plot_context_t* c = (plot_context_t*)user_data;
            const day_result_t* h = (const day_result_t*)c->user_data + idx;

            if (h->date < c->acc)
                return ImPlotPoint(NAN, NAN);

            const double x = (double)h->date;
            const double y = h->slope;

            return ImPlotPoint(x, y);
        }, &c2, (int)c2.range, ImPlotScatterFlags_NoClip);
    }

    ImPlot::EndPlot();
}

FOUNDATION_STATIC bool pattern_add_to_report_menu(const char* symbol, size_t symbol_length)
{
    bool report_opened = false;

    // Gather all reports and sort them by alphabetical order
    report_t** reports = report_sort_alphabetically();

    // Add a Add To menu item for each report
    for (size_t i = 0, end = array_size(reports); i < end; ++i)
    {
        report_t* report = reports[i];
        string_const_t report_name = string_table_decode_const(report->name);
        if (ImGui::MenuItem(report_name.str))
        {
            string_const_t title_code = string_const(symbol, symbol_length);
            report_add_title(report, STRING_ARGS(title_code));
            report->opened = true;
            report_opened = true;
        }
    }
    array_deallocate(reports);

    return report_opened;
}

FOUNDATION_STATIC void pattern_add_to_report_menu(pattern_handle_t handle)
{
    pattern_t* pattern = (pattern_t*)pattern_get(handle);
    if (pattern == nullptr)
        return;
    string_const_t title_code = string_table_decode_const(pattern->code);
    pattern_add_to_report_menu(STRING_ARGS(title_code));
}

FOUNDATION_STATIC void pattern_render_graphs(pattern_t* pattern)
{
    pattern_graph_data_t graph_data = pattern_render_build_graph_data(pattern);

    pattern_render_graph_toolbar(pattern, graph_data);

    ImGui::SetWindowFontScale(0.8f);

    switch (pattern->type)
    {
        case PATTERN_GRAPH_FLEX: 
            pattern_render_graph_flex(pattern, graph_data);
            break;
        
        case PATTERN_GRAPH_DEFAULT: 
            pattern_render_graph_price(pattern, graph_data);
            break;

        case PATTERN_GRAPH_TRENDS:
            pattern_render_graph_trends(pattern, graph_data);
            break;

        case PATTERN_GRAPH_YOY:
            pattern_render_graph_yoy(pattern, graph_data);
            break;

        case PATTERN_GRAPH_INTRADAY:
            pattern_render_graph_intraday(pattern, graph_data);
            break;

        case PATTERN_ACTIVITY:
            pattern_render_activity(pattern, graph_data);
            break;

        default: // PATTERN_GRAPH_PRICE
            pattern_render_graph_analysis(pattern, graph_data); 
            break;
    }

    ImGui::SetWindowFontScale(1.0f);
}

FOUNDATION_STATIC bool pattern_handle_shortcuts(pattern_t* pattern)
{
    FOUNDATION_ASSERT(pattern);

    if (ImGui::Shortcut(ImGuiKey_Escape, 0, ImGuiInputFlags_RouteFocused))
    {
        pattern->opened = false;
        return true;
    }

    if (ImGui::Shortcut(ImGuiMod_Alt | ImGuiKey_E, 0, ImGuiInputFlags_RouteFocused))
    {
        string_const_t code = string_table_decode_const(pattern->code);
        pattern_open_watch_window(code.str, code.length);
    }

    if (shortcut_executed('N'))
    {
        pattern->notes_opened = true;
        return true;
    }

    return false;
}

FOUNDATION_STATIC bool pattern_update_year_after_year_results(pattern_t* pattern)
{
    if (pattern->yy != nullptr)
        return true;

    // Get year after year yield
    const stock_t* s = pattern->stock;
    if (s == nullptr || !s->has_resolve(FetchLevel::FUNDAMENTALS | FetchLevel::EOD))
        return false;

    if (array_size(s->history) <= 1)
    {
        string_const_t code = string_table_decode_const(pattern->code);
        log_debugf(HASH_PATTERN, STRING_CONST("Pattern %.*s has no history"), STRING_FORMAT(code));

        array_reserve(pattern->yy, 1);
        return false;
    }

    day_result_t* oldest = array_last(s->history);
    day_result_t* recent = array_first(s->history);
    
    for (unsigned start = 250, end = array_size(s->history); start < end; start += 260)
    {
        oldest = s->history + start;
        const double change_p = (recent->adjusted_close - oldest->adjusted_close) / oldest->adjusted_close * 100.0;

        pattern_t::yy_t yc = { oldest->date, recent->date, change_p };
        array_insert(pattern->yy, 0, yc);
        recent = oldest;
    }

    return true;
}

FOUNDATION_STATIC void pattern_update(pattern_t* pattern)
{
    if (!pattern->stock->is_resolving(FETCH_ALL))
    {
        string_const_t code = string_table_decode_const(pattern->code);
        stock_update(STRING_ARGS(code), pattern->stock, FETCH_ALL, 8.0);
    }

    pattern_update_year_after_year_results(pattern);
    pattern_compute_years_performance_ratios(pattern);
}

FOUNDATION_STATIC void pattern_render_notes_and_analysis(pattern_t* pattern, bool& focus_notes)
{
    openai_completion_options_t& options = pattern->analysis_options;

    const size_t notes_size = string_length(pattern->notes);
    bool used_tree_node = false;

    ImGui::Spacing();

    ImGui::AlignTextToFramePadding();
    ImGui::SetNextItemOpen(notes_size > 0, ImGuiCond_Appearing);
    if (pattern->analysis_summary == nullptr || ImGui::TreeNode(tr("Notes")))
    {
        used_tree_node = pattern->analysis_summary != nullptr;

        if (used_tree_node)
            ImGui::Unindent();
        ImVec2 notes_widget_size = ImVec2(-1, IM_SCALEF(70));
        if (pattern->analysis_summary == nullptr && openai_available())
        {
            string_const_t code = string_table_decode_const(pattern->code);
            pattern->analysis_summary = openai_generate_summary_sentiment(STRING_ARGS(code), pattern->notes, notes_size, options);
            FOUNDATION_ASSERT(pattern->analysis_summary);
        }
        else if (pattern->analysis_summary == nullptr)
        {
            notes_widget_size = ImGui::GetContentRegionAvail();
        }

        if (focus_notes)
        {
            ImGui::SetKeyboardFocusHere();
            focus_notes = false;
        }

        ImGui::InputTextMultiline("##Notes", STRING_BUFFER(pattern->notes), notes_widget_size, ImGuiInputTextFlags_None);

        if (used_tree_node)
        {
            ImGui::Indent();
            ImGui::TreePop();
        }
    }

    if (pattern->analysis_summary)
    {
        if (!used_tree_node)
            ImGui::SameLine();
        if (ImGui::BeginCombo("##Options", tr("Analysis (AI)")/*, ImGuiComboFlags_NoPreview*/))
        {
            float top_p_100 = options.top_p * 100.0f;
            float temperature_100 = options.temperature * 100.0f;
            float presence_penalty_100 = options.presence_penalty * 50.0f;
            float frequency_penalty_100 = options.frequency_penalty * 50.0f;
            if (ImGui::SliderFloat(tr("Diversity"), &top_p_100, 0.0f, 100.0f, "%.3g %%", ImGuiSliderFlags_AlwaysClamp))
                options.top_p = top_p_100 / 100.0f;
            if (ImGui::SliderFloat(tr("Opportunity"), &temperature_100, 0.0f, 100.0f, "%.3g %%", ImGuiSliderFlags_AlwaysClamp))
                options.temperature = temperature_100 / 100.0f;
            if (ImGui::SliderFloat(tr("Openness"), &presence_penalty_100, 0.0f, 100.0f, "%.3g %%", ImGuiSliderFlags_AlwaysClamp))
                options.presence_penalty = presence_penalty_100 / 50.0f;
            if (ImGui::SliderFloat(tr("Variety"), &frequency_penalty_100, 0.0f, 100.0f, "%.3g %%", ImGuiSliderFlags_AlwaysClamp))
                options.frequency_penalty = frequency_penalty_100 / 50.0f;
            ImGui::SliderInt(tr("Verbosity"), &options.max_tokens, 1, 4096, "%d tokens", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderInt(tr("Possibilities"), &options.best_of, 1, 10, "%d", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(tr(" Number of different completions to try. \n"
                    " The more you produce, the more it cost in term generated tokens, so watch out! "));
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button(tr("Generate"), { -10, 0 }))
        {
            if (pattern->analysis_summary)
            {
                string_deallocate(pattern->analysis_summary->str);
                memory_deallocate(pattern->analysis_summary);
                pattern->analysis_summary = nullptr;
            }

            string_const_t code = string_table_decode_const(pattern->code);
            pattern->analysis_summary = openai_generate_summary_sentiment(STRING_ARGS(code), STRING_LENGTH(pattern->notes), options);
        }

        ImGui::Separator();
        if (ImGui::BeginChild("##Summary", ImGui::GetContentRegionAvail()))
        {
            ImGui::AlignTextToFramePadding();
            if (pattern->analysis_summary && pattern->analysis_summary->length)
                ImGui::TextWrapped("%.*s", STRING_FORMAT(*pattern->analysis_summary));
            else
                ImGui::TextWrapped(tr("No analysis available"));
        } ImGui::EndChild();
    }
}

FOUNDATION_STATIC void pattern_render_dialogs(pattern_t* pattern)
{
    if (pattern->notes_opened)
    {
        string_const_t code = string_table_decode_const(pattern->code);
        const char* title = string_format_static_const("%.*s Notes", STRING_FORMAT(code));
        ImGui::SetNextWindowSize({IM_SCALEF(400), IM_SCALEF(500)}, ImGuiCond_Appearing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(IM_SCALEF(6), IM_SCALEF(2)));
        if (ImGui::Begin(title, &pattern->notes_opened, 0))
        {
            static bool focus_notes = false;
            pattern_render_notes_and_analysis(pattern, focus_notes);
        } ImGui::End();
        ImGui::PopStyleVar();
    }

    if (pattern->fundamentals_dialog_opened)
    {
        string_const_t name = string_table_decode_const(pattern->stock->name);
        ImGui::SetNextWindowSize({ IM_SCALEF(500), IM_SCALEF(700) }, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(tr_format("{0} Fundamentals", name), &pattern->fundamentals_dialog_opened))
        {
            pattern_render_fundamentals(pattern);
        }
        ImGui::End();
    }
}

FOUNDATION_STATIC void pattern_render(pattern_handle_t handle, pattern_render_flags_t render_flags = PatternRenderFlags::None)
{
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Hideable |
        ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_NoBordersInBodyUntilResize |
        ImGuiTableFlags_SizingStretchProp | 
        ImGuiTableFlags_NoHostExtendY |
        ImGuiTableFlags_PadOuterX;

    pattern_t* pattern = (pattern_t*)pattern_get(handle);
    string_const_t code = string_table_decode_const(pattern->code);

    char pattern_id[64];
    string_format(STRING_BUFFER(pattern_id), STRING_CONST("Pattern###%.*s_7"), STRING_FORMAT(code));
    if (!ImGui::BeginTable(pattern_id, 2, flags, ImGui::GetContentRegionAvail()))
        return;

    pattern_update(pattern);
        
    ImGui::TableSetupColumn(code.str, ImGuiTableColumnFlags_WidthFixed, 
        IM_SCALEF(220), 0U, table_cell_right_aligned_column_label, nullptr);

    string_const_t graph_column_title = CTEXT("Graph");
    const stock_t* stock_data = pattern->stock;
    bool show_graph_title = stock_data && stock_data->name;
    if (show_graph_title)
        graph_column_title = string_table_decode_const(stock_data->name);
    ImGui::TableSetupColumn(graph_column_title.str, (show_graph_title ? 0 : ImGuiTableColumnFlags_NoHeaderLabel) | ImGuiTableColumnFlags_NoClip);

    if (none(render_flags, PatternRenderFlags::HideTableHeaders))
        ImGui::TableHeadersRow();

    ImGui::TableNextRow();

    if (ImGui::TableNextColumn())
    {
        if (ImGui::BeginChild("Planning", ImVec2(-1, -ImGui::GetStyle().CellPadding.y), false, ImGuiWindowFlags_None))
        {
            ImGui::SetWindowFontScale(0.9f);
            float y_pos = pattern_render_planning(pattern);

            ImGui::SetCursorPos(ImVec2(15, y_pos + 10.0f));
            y_pos = pattern_render_stats(pattern);

            if (!stock_is_index(pattern->stock))
            {
                ImGui::SetWindowFontScale(0.8f);
                ImGui::SetCursorPos(ImVec2(0.0f, y_pos + 10.0f));
                y_pos = pattern_render_decisions(pattern);
            }

            ImGui::SetWindowFontScale(1.0f);
        } ImGui::EndChild();
    }

    if (ImGui::TableNextColumn())
    {
        pattern_render_graphs(pattern);
    }

    ImGui::EndTable();	

    pattern_handle_shortcuts(pattern);
    pattern_render_dialogs(pattern);

    if (ImGui::IsWindowAppearing())
    {
        dispatch([pattern]()
        {
            pattern_refresh(pattern);
        }, 250);
    }
}

FOUNDATION_STATIC bool pattern_render_summarized_news_dialog(void* context)
{
    const openai_response_t* response = (const openai_response_t*)context;
    if (response->output.length)
        ImGui::TextWrapped("%.*s", STRING_FORMAT(response->output));
    else
        ImGui::TrTextWrapped("Please wait, reading the news for you...");
    return true;
}

FOUNDATION_STATIC void pattern_main_menu(pattern_handle_t handle)
{
    if (!ImGui::TrBeginMenu("Pattern"))
        return;
    pattern_t* pattern = (pattern_t*)pattern_get(handle);
    string_const_t code = string_table_decode_const(pattern->code);

    if (ImGui::TrMenuItem(ICON_MD_NEWSPAPER " Read News"))
        news_open_window(STRING_ARGS(code));

    if (ImGui::TrMenuItem(ICON_MD_ANALYTICS " Show Financials"))
        financials_open_window(STRING_ARGS(code));

    if (ImGui::TrMenuItem(ICON_MD_FACT_CHECK " Show Fundamentals"))
        pattern->fundamentals_dialog_opened = true;

    if (ImGui::TrMenuItem(ICON_MD_NOTES " Show Notes"))
        pattern->notes_opened = true;

    if (ImGui::TrBeginMenu(ICON_MD_SCATTER_PLOT " Plot options"))
    {
        ImGui::TrMenuItem("Show Trend Equations", nullptr, &pattern->show_trend_equation);
        ImGui::EndMenu();
    }

    ImGui::Separator();

    pattern_contextual_menu(STRING_ARGS(code), false);

    #if BUILD_DEVELOPMENT

    ImGui::Separator();

    if (ImGui::TrMenuItem(ICON_MD_LOGO_DEV " EOD", nullptr, nullptr, true))
        system_execute_command(eod_build_url("eod", code.str, FORMAT_JSON, "order", "d").str);

    if (ImGui::TrMenuItem(ICON_MD_LOGO_DEV " Trends", nullptr, nullptr, true))
        system_execute_command(eod_build_url("calendar", "trends", FORMAT_JSON, "symbols", code.str).str);

    if (ImGui::TrMenuItem(ICON_MD_LOGO_DEV " Earnings", nullptr, nullptr, true))
    {
        time_t since_last_year = time_add_days(time_now(), -465);
        string_const_t date_str = string_from_date(since_last_year);
        system_execute_command(eod_build_url("calendar", "earnings", FORMAT_JSON, "symbols", code.str, "from", date_str.str).str);
    }

    if (ImGui::TrMenuItem(ICON_MD_LOGO_DEV " Technical", nullptr, nullptr, true))
        system_execute_command(eod_build_url("technical", code.str, FORMAT_JSON, "order", "d", "function", "splitadjusted").str);

    if (ImGui::TrMenuItem(ICON_MD_LOGO_DEV " Fundamentals", nullptr, nullptr, true))
        system_execute_command(eod_build_url("fundamentals", code.str, FORMAT_JSON).str);

    if (ImGui::TrMenuItem(ICON_MD_LOGO_DEV " Real-time", nullptr, nullptr, true))
        system_execute_command(eod_build_url("real-time", code.str, FORMAT_JSON).str);

    if (openai_available())
    {
        ImGui::Separator();

        const char* title_summarize_news = tr(ICON_MD_NEWSPAPER " Summarize news URL for me...");
        if (ImGui::MenuItem(title_summarize_news))
        {
            struct pattern_news_dialog_t
            {
                char url[2048] = { 0 };
                pattern_t* pattern{ nullptr };
                const openai_response_t* response{ nullptr };
            };

            pattern_news_dialog_t* dialog = (pattern_news_dialog_t*)memory_allocate(HASH_PATTERN, sizeof(pattern_news_dialog_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
            dialog->pattern = (pattern_t*)pattern_get(handle);
            dialog->response = nullptr;
            dialog->url[0] = 0;

            app_open_dialog(title_summarize_news, [](void* context)
            {
                        
                pattern_news_dialog_t* dialog = (pattern_news_dialog_t*)context;

                ImGui::ExpandNextItem();
                ImGui::InputTextWithHint("##URL", tr("Enter the URL of news to summarize for you..."), STRING_BUFFER(dialog->url));

                ImGui::Spacing();
                ImGui::Spacing();

                if (dialog->response)
                {
                    if (dialog->response->output.length)
                        ImGui::TextWrapped("%.*s", STRING_FORMAT(dialog->response->output));
                    else
                        ImGui::TrTextWrapped("Please wait, reading the news for you...");
                }
                else
                {
                    ImGui::Dummy(ImVec2(0.0f, 0.0f));
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - IM_SCALEF(94.0f));
                    if (ImGui::Button(tr("Summarize"), { IM_SCALEF(100.0f), 0.0f }))
                    {
                        openai_completion_options_t options{};
                        options.best_of = 3;
                        options.max_tokens = 1000;
                        string_const_t code = string_table_decode_const(dialog->pattern->code);
                        dialog->response = openai_generate_news_sentiment(STRING_ARGS(code), time_now(), STRING_LENGTH(dialog->url), options);
                    }
                }

                return true;
            }, IM_SCALEF(400), IM_SCALEF(500), true, dialog, [](void* context)
            {
                pattern_news_dialog_t* dialog = (pattern_news_dialog_t*)context;
                memory_deallocate(dialog);
            });
        }

        if (BUILD_DEBUG && ImGui::TrMenuItem(ICON_MD_LOGO_DEV " Generate OpenAI Summary Prompt"))
        {
            string_const_t prompt = openai_generate_summary_prompt(STRING_ARGS(code));
            ImGui::SetClipboardText(prompt.str);
        }
    }

    #endif

    ImGui::EndMenu();
}

FOUNDATION_STATIC void pattern_render_floating_window_main_menu(pattern_handle_t handle, window_handle_t wh)
{
    if (ImGui::TrBeginMenu("File"))
    {
        if (ImGui::TrMenuItem("Close"))
            window_close(wh);
        ImGui::EndMenu();
    }

    pattern_main_menu(handle);

    if (ImGui::TrBeginMenu("Report"))
    {
        if (ImGui::TrBeginMenu("Add To"))
        {
            pattern_t* pattern = (pattern_t*)pattern_get(handle);
            string_const_t code = string_table_decode_const(pattern->code);
            pattern_add_to_report_menu(code.str, code.length);
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
}

FOUNDATION_STATIC bool pattern_open_floating_window(pattern_handle_t handle)
{
    pattern_t* pattern = (pattern_t*)pattern_get(handle);
    if (pattern == nullptr)
    {
        log_warnf(HASH_PATTERN, WARNING_INVALID_VALUE, STRING_CONST("Failed to open pattern window, pattern not found"));
        return false;
    }

    const stock_t* stock = pattern_refresh(pattern, FetchLevel::FUNDAMENTALS);
    if (stock == nullptr)
    {
        log_warnf(HASH_PATTERN, WARNING_INVALID_VALUE, STRING_CONST("Failed to open pattern window, stock not resolved"));
        return false;
    }

    // When opening a floating window, we want to close the main window pattern view if any.
    pattern->opened = false;
    
    string_const_t pattern_name = SYMBOL_CONST(stock->name);
    string_const_t pattern_code = SYMBOL_CONST(pattern->code);
    const char* pattern_window_title = string_format_static_const("%.*s (%.*s)", STRING_FORMAT(pattern_name), STRING_FORMAT(pattern_code));
    auto pattern_window_handle = window_open(pattern_window_title, 
        L1(pattern_render(handle, PatternRenderFlags::HideTableHeaders)), 
        WindowFlags::InitialProportionalSize);

    window_set_menu_render_callback(pattern_window_handle, L1(pattern_render_floating_window_main_menu(handle, _1)));

    return pattern_window_handle;
}

FOUNDATION_STATIC string_const_t pattern_code(pattern_handle_t handle)
{
    pattern_t* pattern = pattern_get(handle);
    if (pattern)
        return string_table_decode_const(pattern->code);
    return string_null();
}

FOUNDATION_STATIC void pattern_tab_menu(pattern_handle_t handle)
{
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::TrMenuItem(ICON_MD_BRANDING_WATERMARK " Float Window"))
            pattern_open_floating_window(handle);

        string_const_t code = pattern_code(handle);
        pattern_contextual_menu(STRING_ARGS(code), false);

        ImGui::EndPopup();
    }

    if (ImGui::BeginMenuBar())
    {
        pattern_main_menu(handle);
        ImGui::EndMenuBar();
    }
}

FOUNDATION_STATIC bool pattern_fetch_flex_low(pattern_handle_t handle, double& value)
{
    pattern_t* pattern = &_patterns[handle];
    if (pattern->flex == nullptr && !pattern_flex_update(handle))
        return false;

    value = pattern->flex_buy.median;
    return true;
}

FOUNDATION_STATIC bool pattern_fetch_flex_high(pattern_handle_t handle, double& value)
{
    pattern_t* pattern = &_patterns[handle];
    if (pattern->flex == nullptr && !pattern_flex_update(handle))
        return false;

    value = pattern->flex_execute.median;
    return true;
}

FOUNDATION_STATIC void pattern_initialize(pattern_handle_t handle)
{
    pattern_t* pattern = &_patterns[handle];

    FOUNDATION_ASSERT(ARRAY_COUNT(FIXED_MARKS) == ARRAY_COUNT(pattern->marks));

    pattern->date = time_now();
    pattern->flex_low.fetcher = LR1(pattern_fetch_flex_low(handle, _1));
    pattern->flex_high.fetcher = LR1(pattern_fetch_flex_high(handle, _1));
    
    // Initialize marks
    const size_t mark_count = ARRAY_COUNT(pattern->marks);
    for (int i = 0; i < ARRAY_COUNT(pattern->marks); ++i)
    {
        pattern_mark_t& mark = pattern->marks[i];
        mark.fetched = false;
        if (FIXED_MARKS[i] > 0)
        {
            mark.date = pattern_date(pattern, -FIXED_MARKS[i]);

            int rel = 0;
            while (i > 0 && pattern->marks[i-1].date == mark.date)
                mark.date = pattern_date(pattern, -FIXED_MARKS[i] - (++rel));
        }
        else
            mark.date = 0;
        mark.change_p = DNAN;
    }
}

FOUNDATION_STATIC size_t pattern_count()
{
    if (_patterns == nullptr)
        return 0;
    return array_size(_patterns);
}

FOUNDATION_STATIC string_const_t pattern_get_user_file_path()
{
    return session_get_user_file_path(STRING_CONST("patterns.json"));
}

FOUNDATION_STATIC void pattern_load(const config_handle_t& pattern_data, pattern_t& pattern)
{
    int check_index = 0;
    for (auto c : pattern_data["checks"])
        pattern.checks[check_index++] = pattern_check_t{ c["checked"].as_boolean() };

    pattern.opened = pattern_data["opened"].as_boolean();
    pattern.extra_charts = pattern_data["extra_charts"].as_boolean();
    pattern.show_limits = pattern_data["show_limits"].as_boolean();
    pattern.x_axis_inverted = pattern_data["x_axis_inverted"].as_boolean();
    pattern.range = pattern_data["range_acc"].as_integer();
    pattern.type = pattern_data["graph_type"].as_integer();
    string_copy(STRING_BUFFER(pattern.notes), STRING_ARGS(pattern_data["notes"].as_string()));

    auto cv_price_limits = pattern_data["price_limits"];
    pattern.price_limits.xmin = cv_price_limits["xmin"].as_number();
    pattern.price_limits.xmax = cv_price_limits["xmax"].as_number();
    pattern.price_limits.ymin = cv_price_limits["ymin"].as_number();
    pattern.price_limits.ymax = cv_price_limits["ymax"].as_number();

    // Load AI analysis options
    auto cv_ai = pattern_data["analysis"];
    pattern.analysis_options.best_of = cv_ai["best_of"].as_integer(3);
    pattern.analysis_options.max_tokens = cv_ai["max_tokens"].as_integer(1700);
    pattern.analysis_options.temperature = cv_ai["temperature"].as_number(0.7f);
    pattern.analysis_options.top_p = cv_ai["top_p"].as_number(0.9f);
    pattern.analysis_options.presence_penalty = cv_ai["presence_penalty"].as_number(1.50);
    pattern.analysis_options.frequency_penalty = cv_ai["frequency_penalty"].as_number(0.4);

    string_const_t saved_analysis = cv_ai["summary"].as_string();
    if (saved_analysis.length)
    {
        if (pattern.analysis_summary)
        {
            string_deallocate(pattern.analysis_summary->str);
            memory_deallocate(pattern.analysis_summary);
        }

        pattern.analysis_summary = (string_t*)memory_allocate(HASH_PATTERN, sizeof(string_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
        *pattern.analysis_summary = string_clone(STRING_ARGS(saved_analysis));
    }

    pattern.watch_context = nullptr;
    config_handle_t cv_pattern_watches = pattern_data["watches"];
    if (cv_pattern_watches)
    {
        string_const_t code = string_table_decode_const(pattern.code);
        string_const_t watch_context_name = string_format_static(STRING_CONST("Pattern %.*s"), STRING_FORMAT(code));
        pattern.watch_context = watch_create(watch_context_name.str, watch_context_name.length, cv_pattern_watches);
    }

    // Make sure this pattern gets saved again.
    pattern.save = true;
}

FOUNDATION_STATIC void pattern_save(config_handle_t pattern_data, const pattern_t& pattern)
{
    config_set(pattern_data, STRING_CONST("opened"), pattern.opened);
    config_set(pattern_data, STRING_CONST("extra_charts"), pattern.extra_charts);
    config_set(pattern_data, STRING_CONST("show_limits"), pattern.show_limits);
    config_set(pattern_data, STRING_CONST("x_axis_inverted"), pattern.x_axis_inverted);
    config_set(pattern_data, STRING_CONST("range_acc"), (double)pattern.range);
    config_set(pattern_data, STRING_CONST("graph_type"), (double)pattern.type);
    config_set(pattern_data, STRING_CONST("notes"), pattern.notes, string_length(pattern.notes));

    auto cv_price_limits = config_set_object(pattern_data, STRING_CONST("price_limits"));
    config_set(cv_price_limits, STRING_CONST("xmin"), pattern.price_limits.xmin);
    config_set(cv_price_limits, STRING_CONST("xmax"), pattern.price_limits.xmax);
    config_set(cv_price_limits, STRING_CONST("ymin"), pattern.price_limits.ymin);
    config_set(cv_price_limits, STRING_CONST("ymax"), pattern.price_limits.ymax);

    // Save AI analysis options
    auto cv_ai = config_set_object(pattern_data, STRING_CONST("analysis"));
    config_set(cv_ai, STRING_CONST("best_of"), (double)pattern.analysis_options.best_of);
    config_set(cv_ai, STRING_CONST("max_tokens"), (double)pattern.analysis_options.max_tokens);
    config_set(cv_ai, STRING_CONST("temperature"), pattern.analysis_options.temperature);
    config_set(cv_ai, STRING_CONST("top_p"), pattern.analysis_options.top_p);
    config_set(cv_ai, STRING_CONST("presence_penalty"), pattern.analysis_options.presence_penalty);
    config_set(cv_ai, STRING_CONST("frequency_penalty"), pattern.analysis_options.frequency_penalty);
    
    if (pattern.analysis_summary && pattern.analysis_summary->length)
        config_set(cv_ai, STRING_CONST("summary"), STRING_ARGS(*pattern.analysis_summary));

    config_handle_t checks_data = config_set_array(pattern_data, STRING_CONST("checks"));
    for (size_t i = 0; i < ARRAY_COUNT(pattern.checks); ++i)
    {
        auto cv_check = config_array_push(checks_data, CONFIG_VALUE_OBJECT);
        config_set(cv_check, STRING_CONST("checked"), pattern.checks[i].checked);
    }

    if (pattern.watch_context && array_size(pattern.watch_context->points))
    {
        config_handle_t cv_pattern_watches = config_set_array(pattern_data, STRING_CONST("watches"));
        watch_save(pattern.watch_context, cv_pattern_watches);
    }
}

FOUNDATION_STATIC void pattern_render_tabs()
{
    constexpr static const ImVec4 TAB_COLOR_PATTERN(0.2f, 0.4f, 0.5f, 1.0f);
    
    // Load all active patterns
    tab_set_color(TAB_COLOR_PATTERN);
    size_t pattern_count = ::pattern_count();
    for (int handle = 0; handle < pattern_count; ++handle)
    {
        pattern_t* pattern = pattern_get(handle);
        if (pattern->opened)
        {
            string_const_t code = string_table_decode_const(pattern->code);
            string_const_t tab_id = string_format_static(STRING_CONST(ICON_MD_INSIGHTS " %.*s"), STRING_FORMAT(code));
            tab_draw(tab_id.str, &pattern->opened, L0(pattern_render(handle)), L0(pattern_tab_menu(handle)));
        }
    }
}

FOUNDATION_STATIC watch_context_t* pattern_watch_context(pattern_handle_t handle)
{
    pattern_t* pattern = pattern_get(handle);
    if (!pattern)
        return nullptr;

    string_const_t code = string_table_decode_const(pattern->code);
    if (pattern->watch_context == nullptr)
    {
        string_const_t watch_context_name = string_format_static(STRING_CONST("Pattern %.*s"), STRING_FORMAT(code));
        pattern->watch_context = watch_create(watch_context_name.str, watch_context_name.length);
    }

    watch_set_variable(pattern->watch_context, STRING_CONST("$DATE"), pattern->date);
    watch_set_variable(pattern->watch_context, STRING_CONST("$RANGE"), (double)pattern->range);
    watch_set_variable(pattern->watch_context, STRING_CONST("$TITLE"), STRING_ARGS(code));

    return pattern->watch_context;
}

// 
// # PUBLIC API
//

pattern_handle_t pattern_find(const char* code, size_t code_length)
{
    string_table_symbol_t code_symbol = string_table_encode(code, code_length);
    const size_t pattern_count = array_size(_patterns);
    for (int i = 0; i < pattern_count; ++i)
    {
        pattern_t& pattern = _patterns[i];
        if (pattern.code == code_symbol)
            return i;
    }

    return -1;
}

pattern_handle_t pattern_load(const char* code, size_t code_length)
{
    pattern_handle_t handle = pattern_find(code, code_length);
    if (handle >= 0)
        return handle;

    string_table_symbol_t code_symbol = string_table_encode(code, code_length);
    string_const_t code_str = string_table_decode_const(code_symbol);

    pattern_t new_pattern{ code_symbol };
    new_pattern.opened = false;
    array_push(_patterns, new_pattern);
    handle = array_size(_patterns) - 1;
    pattern_initialize(handle);

    return handle;
}

pattern_handle_t pattern_open(const char* code, size_t code_length)
{
    pattern_handle_t handle = pattern_load(code, code_length);
    pattern_t* pattern = pattern_get(handle);
    if (pattern)
    {
        pattern->save = true;
        pattern->opened = true;
    }

    return handle;
}

pattern_handle_t pattern_open_window(const char* code, size_t code_length)
{
    pattern_handle_t handle = pattern_load(code, code_length);
    pattern_open_floating_window(handle);
    return handle;
}

void pattern_open_watch_window(const char* symbol, size_t symbol_length)
{
    pattern_handle_t pattern = pattern_load(symbol, symbol_length);
    watch_context_t* context = pattern_watch_context(pattern);
    if (context)
        watch_open_dialog(context);
}

bool pattern_contextual_menu(const char* symbol, size_t symbol_length, bool show_all /*= true*/)
{
    ImGui::BeginGroup();

    bool item_executed = false;
    if (show_all)
    {
        ImGui::AlignTextToFramePadding();
        if (ImGui::Selectable(tr("Load Pattern"), false, ImGuiSelectableFlags_AllowItemOverlap))
        {
            item_executed = true;
        }

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MD_OPEN_IN_NEW))
        {
            if (pattern_open_window(symbol, symbol_length))
            {
                item_executed = true;
                ImGui::CloseCurrentPopup();
            }
        }
        else if (item_executed)
        {
            pattern_open(symbol, symbol_length);
        }
    }

    ImGui::AlignTextToFramePadding();
    if (ImGui::Selectable(tr(ICON_MD_PUBLIC " Open Web Site " ICON_MD_OPEN_IN_NEW), false, ImGuiSelectableFlags_AllowItemOverlap))
    {
        stock_handle_t stock_handle = stock_request(symbol, symbol_length, FetchLevel::FUNDAMENTALS);
        if (stock_handle)
        {
            const stock_t* s = stock_handle.resolve();
            while (s && !s->has_resolve(FetchLevel::FUNDAMENTALS))
                dispatcher_wait_for_wakeup_main_thread();
            if (s)
            {
                const char* url = SYMBOL_CSTR(s->url);
                if (url)
                {
                    item_executed = system_execute_command(url);
                }
                else
                {
                    log_warnf(HASH_PATTERN, WARNING_INVALID_VALUE, STRING_CONST("No URL for stock %.*s"), (int)symbol_length, symbol);
                }
            }
        }
    }

    ImGui::AlignTextToFramePadding();
    if (ImGui::Selectable(tr(ICON_MD_WATCH " Open Watch Context"), false, ImGuiSelectableFlags_AllowItemOverlap))
        pattern_open_watch_window(symbol, symbol_length);
    
    ImGui::Separator();

    if (ImGui::TrBeginMenu(ICON_MD_ADD_PHOTO_ALTERNATE " Update Logo"))
    {
        if (ImGui::TrMenuItem(" Icon (32x32)"))
            logo_select_icon(symbol, symbol_length);

        if (ImGui::TrMenuItem(" Banner (200x32)"))
            logo_select_banner(symbol, symbol_length);
        ImGui::EndMenu();
    }

    if (ImGui::TrBeginMenu(ICON_MD_ADD_TO_PHOTOS " Add to report"))
    {
        pattern_add_to_report_menu(symbol, symbol_length);
        ImGui::EndMenu();
    }

    ImGui::EndGroup();
    return item_executed;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void pattern_initialize()
{
    TIME_TRACKER("pattern_initialize");

    if (_patterns == nullptr)
        array_reserve(_patterns, 8);

    if (!main_is_interactive_mode())
        return;

    string_const_t patterns_file_path = pattern_get_user_file_path();
    config_handle_t patterns_data = config_parse_file(STRING_ARGS(patterns_file_path), CONFIG_OPTION_PRESERVE_INSERTION_ORDER);
    if (patterns_data)
    {
        for (auto p : patterns_data)
        {
            string_const_t pattern_code = config_name(p);
            pattern_handle_t pattern_handle = pattern_load(STRING_ARGS(pattern_code));
            pattern_t& pattern = _patterns[pattern_handle];
            pattern_load(p, pattern);			
        }

        config_deallocate(patterns_data);
    }

    module_register_tabs(HASH_PATTERN, pattern_render_tabs);
}

FOUNDATION_STATIC void pattern_deallocate(pattern_t* pattern)
{
    array_deallocate(pattern->yy);
    array_deallocate(pattern->flex);

    if (pattern->analysis_summary)
    {
        string_deallocate(pattern->analysis_summary->str);
        memory_deallocate(pattern->analysis_summary);
        pattern->analysis_summary = nullptr;
    }

    array_deallocate(pattern->intradays);
    config_deallocate(pattern->fundamentals);
    watch_destroy(pattern->watch_context);
}

FOUNDATION_STATIC void pattern_shutdown()
{
    array_deallocate(_activities);

    if (main_is_interactive_mode())
    {
        config_write_file(pattern_get_user_file_path(), [](config_handle_t patterns)
        {
            const size_t pattern_count = ::pattern_count();
            for (int i = 0; i < pattern_count; ++i)
            {
                pattern_t& pattern = _patterns[i];

                if (pattern.save)
                {
                    string_const_t code = string_table_decode_const(pattern.code);
                    config_handle_t pattern_data = config_set_object(patterns, STRING_ARGS(code));

                    pattern_save(pattern_data, pattern);
                }

                pattern_deallocate(&pattern);
            }

            return true;
        }, CONFIG_VALUE_ARRAY,
        CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS |
            CONFIG_OPTION_PRESERVE_INSERTION_ORDER |
            CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES |
            CONFIG_OPTION_WRITE_NO_SAVE_ON_DATA_EQUAL);
    }

    array_deallocate(_patterns);
    _patterns = nullptr;
}

DEFINE_MODULE(PATTERN, pattern_initialize, pattern_shutdown, MODULE_PRIORITY_UI);
