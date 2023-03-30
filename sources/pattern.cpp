/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#include "pattern.h"
 
#include "eod.h"
#include "bulk.h"
#include "settings.h"
#include "report.h"
#include "news.h"
#include "alerts.h"

#include <framework/jobs.h>
#include <framework/session.h>
#include <framework/table.h>
#include <framework/service.h>
#include <framework/tabs.h>
#include <framework/math.h>
#include <framework/config.h>
#include <framework/string.h>
#include <framework/profiler.h>
#include <framework/window.h>
#include <framework/dispatcher.h>
#include <framework/array.h>
#include <framework/system.h>

#include <algorithm>

#define HASH_PATTERN static_hash_string("pattern", 7, 0xf53f39240bdce58aULL)

#define PATTERN_FLEX_RANGE_COUNT (90U)

constexpr int MAX_LCF_DAY_COUNT = 180;

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
    PATTERN_GRAPH_END,

    PATTERN_SIMULATION_BEGIN,
    PATTERN_LONG_COORDINATED_FLEX,
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
    nullptr,
    nullptr,
    "LCF",
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

struct plot_context_t
{
    time_t ref;
    size_t range;
    size_t stride;

    union {
        const void* data;
        const day_result_t* history;
        const pattern_flex_t* flex;
    };

    double acc{ 0 };
    double lx{ 0.0 }, ly{ 0 }, lz{ 0 };

    double x_min{ DBL_MAX }, x_max{ -DBL_MAX }, n{ 0 };
    double a{ 0 }, b{ 0 }, c{ 0 }, d{ 0 }, e{ 0 }, f{ 0 };
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
    string_const_t fmt = value < 1e3 ? CTEXT("%.3g %%") : CTEXT("%.4g %%");
    return pattern_format_number(STRING_ARGS(fmt), value, default_value);
}

FOUNDATION_STATIC int pattern_format_date_label(double value, char* buff, int size, void* user_data)
{
    pattern_graph_data_t& graph = *(pattern_graph_data_t*)user_data;
    time_t then = graph.pattern->date - (time_t)value * time_one_day();
    string_const_t date_str = string_from_date(then);
    return (int)string_format(buff, size, STRING_CONST("%.*s (%d)"), STRING_FORMAT(date_str), math_round(value)).length;
}

FOUNDATION_STATIC int pattern_format_year_label(double value, char* buff, int size, void* user_data)
{
    time_t time = (time_t)math_trunc(value);
    string_const_t date_str = string_from_date(time);
    return (int)string_format(buff, size, STRING_CONST("%.*s"), min((int)date_str.length, 4), date_str.str).length;
}

FOUNDATION_STATIC int pattern_formatx_label(double value, char* buff, int size, void* user_data)
{
    if (math_real_is_nan(value))
        return 0;

    if (value <= 0)
        return (int)string_copy(buff, size, STRING_CONST("MAX")).length;

    if (value >= 365)
    {
        value = math_round(value / 365);
        return (int)string_format(buff, size, STRING_CONST("%.0lfY"), value).length;
    }
    else if (value >= 30)
    {
        value = math_round(value / 30);
        return (int)string_format(buff, size, STRING_CONST("%.0lfM"), value).length;
    }
    else if (value >= 7)
    {
        value = math_round(value / 7);
        return (int)string_format(buff, size, STRING_CONST("%.0lfW"), value).length;
    }

    value = math_round(value);
    return (int)string_format(buff, size, STRING_CONST("%.0lfD"), value).length;
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
    if (!string_is_null(v3)) table_cell_right_aligned_label(STRING_ARGS(v3));

    ImGui::TableNextColumn();
    if (!string_is_null(v4)) table_cell_right_aligned_label(STRING_ARGS(v4));
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
        mark.change_p = (cd.adjusted_close - ed->adjusted_close) / cd.adjusted_close;
        if (mark.change_p * 100 < -999)
            mark.change_p = DNAN;
    }

    return mark.change_p;
}

FOUNDATION_STATIC string_const_t pattern_mark_change_p_to_string(const pattern_t* pattern, int mark_index)
{
    double change_p = pattern_mark_change_p(pattern, mark_index);
    if (math_real_is_nan(change_p))
        return CTEXT("-");

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
    size_t dbuf_length = (size_t)pattern_formatx_label(FIXED_MARKS[mark_index], dbuf, ARRAY_COUNT(dbuf), nullptr);

    pattern_render_planning_line(label, url,
        mark_valid ? string_const(dbuf, dbuf_length) : CTEXT("-"),
        mark_valid ? string_from_date(mark.date) : CTEXT("-"),
        change_p_str, label.length > 1 && translate);
}

FOUNDATION_STATIC void pattern_render_planning_line(string_const_t label, const pattern_t* pattern, int mark_index, bool can_skip_if_not_valid = false, bool translate = false)
{
    pattern_render_planning_url(label, string_null(), pattern, mark_index, can_skip_if_not_valid, translate);
}

FOUNDATION_STATIC bool pattern_render_stats_value(const stock_t* s, string_const_t value)
{
    ImGui::TableNextColumn();
    if (string_is_null(value))
        return false;

    table_cell_right_aligned_label(STRING_ARGS(value));

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
            if (ImGui::Selectable(tr_format(" Add a price alert of %.*s for %.*s ", STRING_FORMAT(value), STRING_FORMAT(symbol))))
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

    return true;
}

FOUNDATION_STATIC void pattern_render_stats_line(const stock_t* s, string_const_t v1, string_const_t v2, string_const_t v3, bool translate = false)
{
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 15.0f);

    string_const_t trv1 = translate && v1.length > 1 ? tr(STRING_ARGS(v1), false) : v1;
    ImGui::TextWrapped("%.*s", STRING_FORMAT(trv1));

    pattern_render_stats_value(s, v2);
    pattern_render_stats_value(s, v3);
}

FOUNDATION_STATIC void pattern_render_decision_line(int rank, bool* check, const char* text, size_t text_length)
{
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    char cid[32];
    string_format(STRING_BUFFER(cid), STRING_CONST("##CHECK_%lu"), (uintptr_t)check);
    if (check && ImGui::Checkbox(cid, check))
        log_infof(0, STRING_CONST("Reason %d %s"), rank, *check ? "checked" : "unchecked");

    ImGui::TableNextColumn();
    ImGui::Text("%d.", rank);

    ImGui::TableNextColumn();
    ImGui::TextWrapped("%.*s", (int)text_length, text);
}

FOUNDATION_STATIC void pattern_render_decision_line(int rank, bool* check, const char* text)
{
    pattern_render_decision_line(rank, check, text, string_length(text));
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

double pattern_get_bid_price(pattern_handle_t pattern_handle)
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

    double buy_limit = min(today_price + (today_price * (flex_low_p + math_abs(mcp))), today_price - (today_price * pattern->flex_high.fetch()));
    return buy_limit;
}

FOUNDATION_STATIC float pattern_render_stats(const pattern_t* pattern)
{
    ImGuiTableFlags flags =
        ImGuiTableFlags_NoSavedSettings |
        ImGuiTableFlags_NoClip |
        ImGuiTableFlags_NoHostExtendY |
        ImGuiTableFlags_PreciseWidths |
        ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX;

    if (!ImGui::BeginTable("Stats", 3, flags))
        return 0;

    ImGui::TableSetupColumn("Labels", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("V1", ImGuiTableColumnFlags_WidthFixed, IM_SCALEF(60));
    ImGui::TableSetupColumn("V2", ImGuiTableColumnFlags_WidthFixed, IM_SCALEF(45));
    //ImGui::TableHeadersRow();

    const stock_t* s = pattern->stock;
    if (s)
    {
        double share_p = s->current.volume / s->shares_count;
        pattern_render_stats_line(nullptr, CTEXT("Volume"), 
            pattern_format_number(STRING_CONST("%.0lf"), s->current.volume),
            pattern_format_number(STRING_CONST("%.2lf %%"), share_p * 100.0), true);
        pattern_render_stats_line(s, CTEXT("High 52"), 
            pattern_format_currency(s->high_52),
            pattern_format_percentage(s->current.adjusted_close / s->high_52 * 100.0), true);
        pattern_render_stats_line(s, CTEXT("Low 52"), 
            pattern_format_currency(s->low_52),
            pattern_format_percentage(s->low_52 / s->current.adjusted_close * 100.0), true);
        
        const double yielding = s->dividends_yield.get_or_default() * 100.0;
        const double performance_ratio = pattern->yy_ratio.get_or_default(pattern->performance_ratio.get_or_default(0.0));
        const double performance_ratio_combined = max(pattern->yy_ratio.get_or_default(pattern->performance_ratio.fetch()), yielding);

        string_const_t fmttr = RTEXT("Yield %s");
        string_const_t yield_label = string_format_static(STRING_ARGS(fmttr), 
            pattern->yy_ratio.fetch() >= performance_ratio ? ICON_MD_TRENDING_UP : ICON_MD_TRENDING_DOWN);
        ImGui::PushStyleColor(ImGuiCol_Text, performance_ratio <= 0 || performance_ratio_combined < SETTINGS.good_dividends_ratio * 100.0 ? TEXT_WARN_COLOR : TEXT_GOOD_COLOR);
        pattern_render_stats_line(nullptr, yield_label,
            pattern_format_percentage(yielding),
            pattern_format_percentage(performance_ratio), false);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(tr(" Year after Year yielding (Overall ratio %.3g %%) (%.0lf last years) "), 
                pattern->performance_ratio.fetch(), pattern->years.fetch());
        }

        pattern_render_stats_line(nullptr, CTEXT("Beta"), 
            pattern_format_percentage(s->beta * 100.0),
            pattern_format_percentage(s->dma_200 / s->dma_50 * 100.0), true);

        const double eps_diff = s->earning_trend_difference.fetch();
        const double eps_percent = s->earning_trend_percent.fetch();
        ImGui::PushStyleColor(ImGuiCol_Text, eps_diff <= 0.1 ? TEXT_WARN_COLOR : TEXT_GOOD_COLOR);
        pattern_render_stats_line(nullptr, CTEXT("Earnings"),
            pattern_format_currency(s->diluted_eps_ttm),
            pattern_format_percentage(eps_percent), true);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(tr(" Earnings:  1 Year /  Actual /  Estimate /  Diff.  / Surprise /   Gain \n"
                                  "           %5.2lf $ / %5.2lf $ /   %5.2lf $ / %5.2lf $ /   %.3lg %% / %.3lg %% "), 
                s->diluted_eps_ttm, s->earning_trend_actual.fetch(), s->earning_trend_estimate.fetch(), s->earning_trend_difference.fetch(), eps_percent, 
                s->diluted_eps_ttm / s->current.close * 100.0);
        
        if (s->pe != 0)
        {
            pattern_render_stats_line(nullptr,
                pattern_format_number(STRING_CONST("P/E (%.3g)"), s->pe, 0.0),
                pattern_format_percentage(s->current.change / s->pe * 100.0),
                pattern_format_percentage(math_average_parallel(&pattern->marks[7].change_p, 5, sizeof(pattern_mark_t)) * 100.0));
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

FOUNDATION_STATIC float pattern_render_decisions(const pattern_t* pattern)
{
    ImGuiTableFlags flags =
        ImGuiTableFlags_NoSavedSettings |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_NoHostExtendY |
        ImGuiTableFlags_PreciseWidths |
        ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX;

    if (!ImGui::BeginTable("Decisions", 3, flags))
        return 0;

    ImGui::TableSetupColumn("Check", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40.0f);
    ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch);
    //ImGui::TableHeadersRow();

    pattern_check_t* checks = ((pattern_t*)pattern)->checks;
    pattern_render_decision_line(1, &checks[0].checked, tr("Price trend is positive."));
    pattern_render_decision_line(2, &checks[1].checked, tr("Moving trends are null or slightly negative and about to go up (crossing each others)."));
    pattern_render_decision_line(3, &checks[2].checked, tr("Activity polarity trend is positive."));
    pattern_render_decision_line(4, &checks[3].checked, tr("Sell limit is higher or equal to 3%."));
    pattern_render_decision_line(5, &checks[4].checked, tr("Beta is higher or equal to 90%."));
    pattern_render_decision_line(6, &checks[5].checked, tr("Flex difference is higher than 6%."));
    pattern_render_decision_line(7, &checks[6].checked, tr("MAX mark is higher than 25%."));
    pattern_render_decision_line(8, &checks[7].checked, tr("Target limits are interesting."));

    float y_offset = ImGui::GetCursorPosY();
    ImGui::EndTable();
    return y_offset;
}

FOUNDATION_STATIC void pattern_render_graph_change_high(pattern_t* pattern, const stock_t* s)
{
    const size_t max_render_count = 1024;
    plot_context_t c{ pattern->date, s->history_count, (s->history_count / max_render_count) + 1, s->history };
    ImPlot::PlotLineG("Flex H", [](int idx, void* user_data)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)user_data;
        const day_result_t* history = c->history;

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
    ImPlot::PlotLineG("Flex L", [](int idx, void* user_data)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)user_data;
        const day_result_t* history = c->history;

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
    ImPlot::PlotLineG("% Acc.", [](int idx, void* user_data)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)user_data;
        const day_result_t* history = c->history;

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

FOUNDATION_STATIC void pattern_compute_trend(plot_context_t& c)
{
    // Trend
    // Y = a + bX
    //        c      d e         f         d  
    // b = (Σ(xy) - (ΣxΣy)/n) / (Σ(x^2) - (Σx)^2/n)
    //      e          d
    // a = (Σy)/n - b((Σx)/n)

    c.b = (c.c - (c.d * c.e) / c.n) / (c.f - math_pow(c.d, 2) / c.n);
    c.a = (c.e / c.n) - c.b * (c.d / c.n);
}

FOUNDATION_STATIC bool pattern_build_trend(plot_context_t& c, double x, double y)
{
    if (math_real_is_nan(x) || math_real_is_nan(y))
        return false;
    c.n++;
    c.x_min = min(c.x_min, x);
    c.x_max = max(x, c.x_max);
    c.c += x * y;
    c.d += x;
    c.e += y;
    c.f += math_pow(x, 2);
    return true;
}

FOUNDATION_STATIC void pattern_render_graph_trend(const char* label, double x1, double x2, double a, double b, bool x_axis_inverted)
{
    // Y = a + bX
    const double x_diff = x2 - x1;
    const double range[]{ x1, x2 };
    const double trend[]{ a + b * x1, a + b * x2};
    double y_diff = trend[1] - trend[0];
    if (math_real_is_nan(trend[0]) || math_real_is_nan(trend[1]))
        return;

    ImColor pc = ImPlot::GetLastItemColor();
    ImPlot::SetNextLineStyle(pc);

    if (x_axis_inverted)
    {
        b *= -1.0;
        a += y_diff;
        y_diff *= -1.0;
    }

    const char* tag = string_format_static_const("%s %s", label, b > 0 ? ICON_MD_TRENDING_UP : ICON_MD_TRENDING_DOWN);
    ImPlot::TagY(a + b * (x_axis_inverted ? x2 : x1), pc, "%s", tag);
    ImPlot::PlotLine(tag, range, trend, ARRAY_COUNT(trend), ImPlotLineFlags_NoClip);

    ImPlot::Annotation(x_axis_inverted ? x1 : x2, x_axis_inverted ? trend[0] : trend[1], ImVec4(0.3f, 0.3f, 0.5f, 1.0f),
        ImVec2(0, 10.0f * (b > 0 ? -1.0f : 1.0f)), true,
        "%s = %.2g %s %.1gx (" ICON_MD_CHANGE_HISTORY  "%.2g)", label, a, b < 0 ? "-" : "+", math_abs(b), y_diff);
}

FOUNDATION_STATIC void pattern_render_trend(const char* label, const plot_context_t& c, bool x_axis_inverted)
{
    if (c.n <= 0)
        return;
    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
    pattern_render_graph_trend(label, c.x_min, c.x_max, c.a, c.b, x_axis_inverted);
    ImPlot::PopStyleVar(1);
}

FOUNDATION_STATIC void pattern_render_graph_day_value(const char* label, pattern_t* pattern, const stock_t* s, ImAxis y_axis, size_t offset, bool x_axis_inverted)
{
    plot_context_t c{ pattern->date, min((size_t)4096, s->history_count), offset, s->history };
    c.acc = pattern->range;
    ImPlot::SetAxis(y_axis);
    ImPlot::PlotLineG(label, [](int idx, void* user_data)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)user_data;
        const day_result_t* history = c->history;
        const time_t ONE_DAY = time_one_day();

        const day_result_t* ed = &history[idx];
        if ((ed->date / ONE_DAY) >= (c->ref / ONE_DAY))
            return ImPlotPoint(DNAN, DNAN);

        double x = math_round((c->ref - ed->date) / (double)ONE_DAY);
        double y = *(const double*)(((const uint8_t*)ed)+c->stride);

        if (time_elapsed_days(ed->date, c->ref) <= c->acc)
            pattern_build_trend(*c, x, y);

        return ImPlotPoint(x, y);
    }, &c, (int)c.range, ImPlotLineFlags_SkipNaN | ImPlotLineFlags_Segments);

    if (c.n > 0 && pattern->show_limits)
    {
        pattern_compute_trend(c);
        ImPlot::HideNextItem(true, ImPlotCond_Once);
        pattern_render_trend(label, c, x_axis_inverted);
    }
}

FOUNDATION_STATIC void pattern_render_graph_price(pattern_t* pattern, const stock_t* s, ImAxis y_axis, bool x_axis_inverted)
{
    plot_context_t c{ pattern->date, min(size_t(4096), s->history_count), 1, s->history };
    c.acc = pattern->range;
    ImPlot::SetAxis(y_axis);
    ImPlot::PlotLineG(tr("Price"), [](int idx, void* user_data)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)user_data;
        const day_result_t* history = c->history;
        const time_t ONE_DAY = time_one_day();

        const day_result_t& ed = history[idx];
        const double days_diff = time_elapsed_days(ed.date, c->ref);
        const double x = math_round(days_diff);
        const double y = ed.adjusted_close;

        if (days_diff <= c->acc)
            pattern_build_trend(*c, x, y);

        return ImPlotPoint(x, y);
    }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);

    if (c.n > 0 && pattern->show_limits)
    {
        pattern_compute_trend(c);
        pattern_render_trend(tr("Price"), c, x_axis_inverted);
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
    const double one_day = (double)time_one_day();
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
    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_formatx_label, nullptr);
    ImPlot::SetupAxisTicks(ImAxis_X1, graph.x_data, pattern_label_max_range(graph), nullptr/*DAY_LABELS*/, false);
    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_format_date_label, &graph);
    ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Log10);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, max(graph.min_d, 1.0), graph.max_d);
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
    if ((graph.refresh || !pattern->autofit) && (s == nullptr || s->has_resolve(FETCH_ALL)))
    {
        ImPlot::SetNextAxesToFit();
        pattern->autofit = true;
        graph.refresh = false;
    }
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
    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_formatx_label, nullptr);
    ImPlot::SetupAxisTicks(ImAxis_X1, min_d, max_d, 7, nullptr, false);
    ImPlot::SetupAxisLimits(ImAxis_X1, min_d, max_d, ImPlotCond_Once);
    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_format_date_label, &graph);

    ImPlot::SetupAxis(ImAxis_Y1, "##Pourcentage", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3g %%");

    plot_context_t c{ pattern->date, array_size(pattern->flex), 1, pattern->flex};
    ImPlot::PlotBarsG(tr("Flex"), [](int idx, void* user_data)->ImPlotPoint
    {
        const plot_context_t* c = (plot_context_t*)user_data;
        const pattern_flex_t& f = c->flex[idx];
        double x = f.days;
        double y = f.change_p * 100.0;
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

void pattern_fetch_lcf_data(const pattern_t* pattern, const json_object_t& json, pattern_lcf_t* lcf)
{
    bulk_t* symbols = nullptr;
    if (json.root->value_length == 0)
        return;

    const day_result_t* ed = stock_get_EOD(pattern->stock, lcf->date, true);
    if (ed == nullptr)
        return;
    if (math_real_eq(ed->open, ed->adjusted_close, 3))
        return;

    bulk_t ps{};
    ps.name = pattern->stock->name;
    ps.code = pattern->stock->symbol;
    ps.type = pattern->stock->type;
    ps.exchange = pattern->stock->exchange;
    ps.close = ed->adjusted_close;
    ps.open = ed->open;
    ps.date = ed->date;
    if (ps.close > ps.open)
        lcf->type = 1;
    else if(ps.close < ps.open)
        lcf->type = -1;

    symbols = array_push(symbols, ps);

    for (int i = 0, end = json.root->value_length; i != end; ++i)
    {
        const json_object_t& e = json[i];

        bulk_t s{};
        s.code = string_table_encode(e["code"].as_string());
        if (s.code == ps.code)
            continue;

        s.type = string_table_encode(e["type"].as_string());

        if (s.type != pattern->stock->type)
            continue;

        s.date = string_to_date(STRING_ARGS(e["date"].as_string()));		
        s.code = string_table_encode(e["code"].as_string());
        s.name = string_table_encode_unescape(e["name"].as_string());
        s.exchange = string_table_encode(e["exchange_short_name"].as_string());
        s.market_capitalization = e["MarketCapitalization"].as_number();
        s.beta = e["Beta"].as_number();
        s.open = e["open"].as_number();
        s.high = e["high"].as_number();
        s.low = e["low"].as_number();
        s.close = e["close"].as_number();
        s.adjusted_close = e["adjusted_close"].as_number();
        s.volume = e["volume"].as_number();
        s.ema_50d = e["ema_50d"].as_number();
        s.ema_200d = e["ema_200d"].as_number();
        s.hi_250d = e["hi_250d"].as_number();
        s.lo_250d = e["lo_250d"].as_number();
        s.avgvol_14d = e["avgvol_14d"].as_number();
        s.avgvol_50d = e["avgvol_50d"].as_number();
        s.avgvol_200d = e["avgvol_200d"].as_number();

        symbols[0].date = s.date;
        symbols = array_push(symbols, s);
    }

    lcf->symbols = symbols;
}

FOUNDATION_STATIC int pattern_load_lcf_thread(payload_t* payload)
{
    MEMORY_TRACKER(HASH_PATTERN);
    
    pattern_lcf_t* lcfarr = nullptr;
    pattern_t* pattern = (pattern_t*)payload;

    time_t ref_date = pattern->date;
    while ((int)array_size(lcfarr) < min(pattern->range, MAX_LCF_DAY_COUNT))
    {
        pattern_lcf_t lcf{ ref_date, nullptr };
        string_const_t exchange = string_table_decode_const(pattern->stock->exchange);
        string_const_t first_date_string = string_from_date(ref_date);
        if (eod_fetch("eod-bulk-last-day", exchange.str, FORMAT_JSON_CACHE,
            "filter", "extended",
            ref_date != pattern->date ? "date" : "ignore", first_date_string.str,
            [pattern, &lcf](const json_object_t& _1) { pattern_fetch_lcf_data(pattern, _1, &lcf); }, 30 * 24 * 60 * 60ULL))
        {
            if (lcf.symbols != nullptr)
            {
                ref_date = lcf.date = lcf.symbols[0].date;
                array_push(lcfarr, lcf);
            }
        }

        ref_date -= time_one_day();
    }

    pattern_lcf_symbol_t* lcf_symbols = nullptr;

    // Find days with most symbols
    int date_count = array_size(lcfarr);
    const pattern_lcf_t& first_lcf = lcfarr[0];
    const bulk_t* symbols = first_lcf.symbols;
    unsigned symbol_count = array_size(first_lcf.symbols);
    for (size_t i = 1; i < date_count; ++i)
    {
        unsigned cz = array_size(lcfarr[i].symbols);
        if (cz > symbol_count)
        {
            symbols = lcfarr[i].symbols;
            symbol_count = cz;
        }
    }

    double total_match = 0;
    static double average_match = 0;

    int ref_matches = 0;
    for (unsigned n = 0; n < symbol_count; ++n)
    {
        const string_table_symbol_t code = symbols[n].code;
        if (code == 0)
            continue;

        pattern_lcf_symbol_t lcfs{};
        lcfs.bulk = &symbols[n];
        lcfs.code = code;
        lcfs.matches = 0;
        lcfs.sequence = nullptr;
        array_reserve(lcfs.sequence, date_count);
        
        for (size_t i = 0; i < min(pattern->range, date_count); ++i)
        {
            const pattern_lcf_t& lcf = lcfarr[i];
            if (lcf.type == 0)
                continue;

            bool ignored = true;

            for (size_t s = 0; s < symbol_count; ++s)
            {
                if (s >= array_size(lcf.symbols) || lcf.symbols[s].code != code)
                    continue;

                const bulk_t& sl = lcf.symbols[s];
                if (lcf.type == 1)
                {
                    if (sl.close > sl.open)
                    {
                        ignored = false;
                        array_push(lcfs.sequence, 'U');
                        lcfs.matches++;
                    }
                    else
                    {
                        ignored = false;
                        array_push(lcfs.sequence, 'X');
                    }
                }
                else if (lcf.type == -1)
                {
                    if (sl.close < sl.open)
                    {
                        ignored = false;
                        array_push(lcfs.sequence, 'D');
                        lcfs.matches++;
                    }
                    else
                    {
                        ignored = false;
                        array_push(lcfs.sequence, 'X');
                    }
                }

                break;
            }

            if (ignored)
                array_push(lcfs.sequence, '_');
        }

        array_push(lcf_symbols, lcfs);
    }

    foreach(lcfs, pattern->lcf_symbols)
        array_deallocate(lcfs->sequence);
    array_deallocate(pattern->lcf_symbols);

    foreach(e, pattern->lcf)
        array_deallocate(e->symbols);
    array_deallocate(pattern->lcf);
    
    pattern->lcf = lcfarr;
    pattern->lcf_symbols = array_sort(lcf_symbols, LC2(_2.matches - _1.matches));
    return 0;
}

FOUNDATION_STATIC void pattern_render_lcf_table(pattern_t* pattern)
{
    int date_count = array_size(pattern->lcf);

    const ImVec2 graph_offset = ImVec2(0, -ImGui::GetStyle().CellPadding.y);
    if (ImGui::BeginTable("##LCF", 3,
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_NoSavedSettings |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_NoBordersInBody |
        ImGuiTableFlags_SizingFixedSame |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY, graph_offset))
    {
        ImGui::TableSetupScrollFreeze(1, 2);

        string_const_t pattern_code = string_table_decode_const(pattern->code);
        ImGui::TableSetupColumn(tr("Title"), ImGuiTableColumnFlags_None);
        ImGui::TableSetupColumn(string_format_static(STRING_CONST("DNA (%d)"), min(pattern->range, date_count)).str, ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(tr("Hits"), ImGuiTableColumnFlags_None);
        ImGui::TableHeadersRow();
        
        double total_match = 0;
        static double average_match = 0;

        const int ref_matches = date_count;
        const int symbol_count = array_size(pattern->lcf_symbols);
        const pattern_lcf_symbol_t* symbols = pattern->lcf_symbols;
        
        ImGuiListClipper clipper;
        clipper.Begin(symbol_count);
        while (clipper.Step())
        {
            for (size_t n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n)
            {
                const string_table_symbol_t code = symbols[n].code;
                if (code == 0)
                    continue;

                int matches = symbols[n].matches;
                ImGui::TableNextRow();
                {
                    ImGui::TableNextColumn();

                    string_const_t code_string = string_table_decode_const(symbols[n].code);
                    ImGui::TextUnformatted(code_string.str);
                    if (ImGui::IsItemHovered())
                    {
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            string_const_t code_id = string_format_static(STRING_CONST("%s.%s"), code_string.str, string_table_decode(symbols[n].bulk->exchange));
                            pattern_open(STRING_ARGS(code_id));
                        }
                        else
                        {
                            ImGui::SetTooltip("%s\n%s", string_table_decode(symbols[n].bulk->name), string_table_decode(symbols[n].bulk->type));
                        }
                    }
                }

                {
                    ImGui::TableNextColumn();
                   
                    bool first_write = true;
                    int offset = 0;
                    ImGui::BeginGroup();
                    for (unsigned c = 0; c < array_size(symbols[n].sequence); ++c)
                    {
                        const char ch = symbols[n].sequence[c];
                        if (ch == 'D')
                        {
                            if (!first_write) ImGui::SameLine(0, 1); else first_write = false;
                            ImGui::TextColored(ImVec4(0.6f, 0.4f, 0.3f, 1.0f), "D");
                        }
                        else if (ch == 'U')
                        {
                            if (!first_write) ImGui::SameLine(0, 1); else first_write = false;
                            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "U");
                        }
                        else if (ch == 'X')
                        {
                            if (!first_write) ImGui::SameLine(0, 1); else first_write = false;
                            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.3f, 0.1f), "X");
                        }
                        else
                        {
                            if (!first_write) ImGui::SameLine(0, 1); else first_write = false;
                            ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.3f, 0.1f), " ");
                        }
                    }
                    ImGui::EndGroup();
                }

                if (symbols[n].matches > 0)
                {
                    ImGui::TableNextColumn();
                    if (matches > (ref_matches * pattern->percent / 100.0))
                    {
                        ImGui::TextColored(ImVec4(0.2f, matches / (float)average_match, 0.3f, matches / (float)ref_matches),
                            "%.3g %%", matches / (double)ref_matches * 100.0);
                    }

                    total_match += matches;
                }
            }
        }

        average_match = total_match / date_count;

        ImGui::EndTable();
    }

    ImGui::SetWindowFontScale(1.0f);
}

FOUNDATION_STATIC void pattern_render_lcf(pattern_t* pattern, pattern_graph_data_t& graph)
{
    if (!pattern->stock->has_resolve(FetchLevel::EOD))
    {
        ImGui::TextUnformatted("Resolving technical end of day data...");
        return;
    }

    size_t lcf_count = array_size(pattern->lcf);
    if (lcf_count > 0 && lcf_count == min(pattern->range, MAX_LCF_DAY_COUNT))
    {
        if (pattern->lcf_job && job_completed(pattern->lcf_job))
        {
            job_deallocate(pattern->lcf_job);
            pattern->lcf_job = nullptr;
        }

        pattern_render_lcf_table(pattern);
    }
    else if (pattern->lcf_job == nullptr)
    {
        pattern->lcf_job = job_execute(pattern_load_lcf_thread, (payload_t*)pattern);
    }
    else
    {
        ImGui::TextUnformatted("Loading data...");
    }
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
    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_format_year_label, &graph);
    
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

FOUNDATION_STATIC void pattern_render_graph_trends(pattern_t* pattern, pattern_graph_data_t& graph)
{
    const stock_t* s = pattern->stock;
    if (s == nullptr)
    {
        ImGui::TextUnformatted("No data");
        return;
    }

    const auto& plot_screen_pos = ImGui::GetCursorScreenPos();

    const ImVec2 graph_offset = ImVec2(-ImGui::GetStyle().CellPadding.x, -ImGui::GetStyle().CellPadding.y);
    if (!ImPlot::BeginPlot("Pattern Trends##1", graph_offset, ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle))
        return;

    ImPlot::SetupLegend(ImPlotLocation_NorthWest);

    static time_t trend_date = /*1663606289*/ time_now();
    const size_t iteration_count = (size_t)pattern->range + (pattern->date - trend_date) / time_one_day();

    const double trend_min_d = max(graph.min_d, 1.0);
    const double trend_max_d = pattern->range + math_ceil(iteration_count / 4.3) * 2.0;
    ImPlot::SetupAxis(ImAxis_X1, "##Days", ImPlotAxisFlags_LockMin | ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight | (pattern->x_axis_inverted ? ImPlotAxisFlags_Invert : 0));
    ImPlot::SetupAxisLimits(ImAxis_X1, trend_min_d, trend_max_d, ImPlotCond_Once);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, trend_min_d, trend_max_d);
    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_formatx_label, nullptr);
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
    ImPlot::SetupAxis(ImAxis_Y1, "##Values", ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_NoSideSwitch);
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
        c.lx = 0.0;
        c.ly = (math_ifnan(s->beta, 0.5) + math_ifnan(s->short_ratio - 1.0, 0.0))
            * math_ifzero(max(max(math_ifnan(s->pe, 1.0), s->forward_pe), s->revenue_per_share_ttm), 1.0) 
            * math_ifzero(math_abs(s->profit_margin), 1.0)
            * math_ifzero(s->peg, math_ifzero(s->pe, 1.0));
        c.lz = s->diluted_eps_ttm * 2.0;
        c.acc = pattern->range;
        ImPlot::PlotLineG("##Slopes", [](int idx, void* user_data)->ImPlotPoint
        {
            plot_context_t* c = (plot_context_t*)user_data;
            constexpr const time_t ONE_DAY = time_one_day();

            const day_result_t* history = c->history;
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

            if (!pattern_build_trend(*c, x, y))
                return ImPlotPoint(DNAN, DNAN);

            return ImPlotPoint(x, y);
        }, &c, (int)c.range, ImPlotLineFlags_SkipNaN);

        pattern_compute_trend(c);
        pattern_render_trend(tr("Trend"), c, pattern->x_axis_inverted);
    }
    else
    {
        ImPlot::Annotation((trend_max_d - trend_min_d) / 2.0, 0, ImVec4(0.8f, 0.6f, 0.54f, 0.8f), ImVec2(0,-10), true, tr("Loading data..."));
    }

    ImPlot::EndPlot();

    if (ImGui::IsKeyDown((ImGuiKey)341/*GLFW_KEY_LEFT_CONTROL*/))
    {
        ImGui::SetCursorScreenPos(ImVec2(plot_screen_pos.x + 350, plot_screen_pos.y + 20));
        ImGui::SetNextItemWidth(250.0f);
        tm tm_date = *localtime(&trend_date);
        if (ImGui::DateChooser("##Date", tm_date, "%Y-%m-%d", true))
            trend_date = mktime(&tm_date);
    }
    pattern_render_graph_end(pattern, s, graph);
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
    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_formatx_label, nullptr);
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
    if (shortcut_executed('6')) pattern->type = PATTERN_LONG_COORDINATED_FLEX;
    if (shortcut_executed('7') || shortcut_executed('A')) pattern->type = PATTERN_ACTIVITY;

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.10f);
    if (ImGui::BeginCombo("##Type", GRAPH_TYPES[pattern->type], ImGuiComboFlags_None))
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
        pattern->autofit = false;
        graph.refresh = true;
    }

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

        if (pattern->type != PATTERN_GRAPH_YOY)
        {
            ImGui::SameLine();
            if (ImGui::Checkbox(tr("Extra Charts"), &pattern->extra_charts))
                graph.refresh = true;

            ImGui::SameLine();
            if (ImGui::Checkbox(tr("Invert Time"), &pattern->x_axis_inverted))
                graph.refresh = true;
        }
    }
    else if (pattern->type == PATTERN_LONG_COORDINATED_FLEX)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
        if (ImGui::SliderFloat("##Percent", &pattern->percent, 0, 100.0f, "%.3g %%", ImGuiSliderFlags_AlwaysClamp))
        {
            graph.refresh = true;
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

FOUNDATION_STATIC int pattern_activity_format_date(double value, char* buff, int size, void* user_data)
{
    time_t d = (time_t)value;
    if (d == 0 || d == -1)
        return 0;

    string_const_t date_str = string_from_date(d);
    return (int)string_copy(buff, size, STRING_ARGS(date_str)).length;
}

FOUNDATION_STATIC void pattern_render_activity(pattern_t* pattern, pattern_graph_data_t& graph)
{
    static hash_t activity_hash = 0;

    const hash_t chash = hash(pattern, sizeof(pattern_t));
    if (activity_hash != chash)
    {
        activity_hash = chash;
        array_clear(_activities);

        eod_fetch("news", nullptr, FORMAT_JSON_CACHE, "s", string_table_decode(pattern->code), "limit", "1000", [](const json_object_t& json)
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

    const double bar_width = time_one_day() * 0.8;
    ImPlot::SetupLegend(ImPlotLocation_NorthWest);

    ImPlot::SetupAxis(ImAxis_X1, "##Date", ImPlotAxisFlags_LockMax | ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_X1, pattern_activity_format_date, nullptr);
    ImPlot::SetupAxisLimits(ImAxis_X1, (double)min_d - time_one_day() * 7, (double)max_d + time_one_day() * 7, ImPlotCond_Once);
    
    ImPlot::SetupAxis(ImAxis_Y1, "##Value", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_Y1, "%.3g");

    plot_context_t c{ pattern->date, min(365U, array_size(_activities)), 1, _activities };
    c.acc = pattern->range;
    ImPlot::SetAxis(ImAxis_Y1);
    static int last_index = -1;
    ImPlot::PlotBarsG(tr("Polarity"), [](int idx, void* user_data)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)user_data;
        const pattern_activity_t& h = ((pattern_activity_t*)c->data)[idx];
        const double x = (double)h.date;
        const double y = h.polarity / h.count;

        double diff = time_elapsed_days(h.date, c->ref);
        if (last_index != idx && h.count > 0 && diff <= c->acc)
        {
            pattern_build_trend(*c, x, y);
            last_index = idx;
        }

        return ImPlotPoint(x, y);
    }, &c, (int)c.range, bar_width, ImPlotBarsFlags_None);

    pattern_compute_trend(c);
    pattern_render_trend(tr("Popularity"), c, !pattern->x_axis_inverted);

    //ImPlot::SetAxis(ImAxis_Y2);
    ImPlot::PlotScatterG(tr("Hits"), [](int idx, void* user_data)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)user_data;
        const pattern_activity_t& h = ((pattern_activity_t*)c->data)[idx];
        if (h.count <= 1 && h.polarity > 0)
            return ImPlotPoint(NAN, NAN);
        const double x = (double)h.date;
        return ImPlotPoint(x, (h.polarity < 0 ? 0 : 1.0) + (0.05 * h.count) * (h.polarity < 0 ? -1.0 : 1.0));
    }, &c, (int)c.range, ImPlotScatterFlags_NoClip);

    if (const day_result_t* history = pattern->stock->history)
    {
        plot_context_t c2{ pattern->date, min(1024U, array_size(history)), 1, history };
        c2.acc = (double)min_d;
        c2.lx = (double)pattern->range;
        ImPlot::SetAxis(ImAxis_Y1);
        ImPlot::PlotScatterG(tr("Change"), [](int idx, void* user_data)->ImPlotPoint
        {
            plot_context_t* c = (plot_context_t*)user_data;
            const day_result_t& h = c->history[idx];

            if (h.date < c->acc)
                return ImPlotPoint(NAN, NAN);

            const double x = (double)h.date;
            const double y = h.slope;

            return ImPlotPoint(x, y);
        }, &c2, (int)c2.range, ImPlotScatterFlags_NoClip);
    }

    ImPlot::EndPlot();
}

FOUNDATION_STATIC void pattern_add_to_report_menu(pattern_handle_t handle)
{
    // Gather all reports and sort them by alphabetical order
    report_t** reports = report_sort_alphabetically();

    // Add a Add To menu item for each report
    for (size_t i = 0, end = array_size(reports); i < end; ++i)
    {
        report_t* report = reports[i];
        string_const_t report_name = string_table_decode_const(report->name);
        if (ImGui::MenuItem(report_name.str))
        {
            pattern_t* pattern = (pattern_t*)pattern_get(handle);
            string_const_t title_code = string_table_decode_const(pattern->code);
            report_add_title(report, STRING_ARGS(title_code));
            report->opened = true;
        }
    }
    array_deallocate(reports);
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

        case PATTERN_LONG_COORDINATED_FLEX:
            pattern_render_lcf(pattern, graph_data);
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
    pattern_update_year_after_year_results(pattern);
    pattern_compute_years_performance_ratios(pattern);
}

FOUNDATION_STATIC void pattern_render(pattern_handle_t handle, pattern_render_flags_t render_flags = PatternRenderFlags::None)
{
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Resizable |
        ImGuiTableFlags_Hideable |
        ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_NoBordersInBodyUntilResize |
        ImGuiTableFlags_SizingStretchProp | 
        ImGuiTableFlags_PadOuterX;

    pattern_t* pattern = (pattern_t*)pattern_get(handle);
    string_const_t code = string_table_decode_const(pattern->code);
    if (!pattern->stock->is_resolving(FETCH_ALL))
        stock_update(STRING_ARGS(code), pattern->stock, FETCH_ALL, 8.0);

    char pattern_id[64];
    string_format(STRING_BUFFER(pattern_id), STRING_CONST("Pattern###%.*s_7"), STRING_FORMAT(code));
    if (!ImGui::BeginTable(pattern_id, 3, flags))
        return;

    pattern_update(pattern);
        
    string_const_t code_str = string_table_decode_const(pattern->code);
    ImGui::TableSetupColumn(code_str.str, ImGuiTableColumnFlags_WidthFixed, 
        IM_SCALEF(220), 0U, table_cell_right_aligned_column_label, nullptr);

    string_const_t graph_column_title = CTEXT("Graph");
    const stock_t* stock_data = pattern->stock;
    bool show_graph_title = stock_data && stock_data->name;
    if (show_graph_title)
        graph_column_title = string_table_decode_const(stock_data->name);
    ImGui::TableSetupColumn(graph_column_title.str, (show_graph_title ? 0 : ImGuiTableColumnFlags_NoHeaderLabel) | ImGuiTableColumnFlags_NoClip);

    ImGui::TableSetupColumn("Additional Information", 
        ImGuiTableColumnFlags_WidthFixed | 
        ImGuiTableColumnFlags_NoHeaderLabel |
        ImGuiTableColumnFlags_DefaultHide, IM_SCALEF(200));

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

            ImGui::SetWindowFontScale(0.8f);
            ImGui::SetCursorPos(ImVec2(0.0f, y_pos + 10.0f));
            y_pos = pattern_render_decisions(pattern);
            ImGui::SetWindowFontScale(1.0f);
        } ImGui::EndChild();
    }

    if (ImGui::TableNextColumn())
    {
        pattern_render_graphs(pattern);
    }

    static bool focus_notes = false;
    if (shortcut_executed('N'))
    {
        ImGui::TableSetColumnEnabled(2, !(ImGui::TableGetColumnFlags(2) & ImGuiTableColumnFlags_IsEnabled));
        focus_notes = true;
    }
    if (ImGui::TableNextColumn())
    {
        ImGui::TrText("Notes");
        if (focus_notes)
        {
            ImGui::SetKeyboardFocusHere();
            focus_notes = false;
        }
        ImGui::InputTextMultiline("##Notes", pattern->notes, ARRAY_COUNT(pattern->notes), ImVec2(-1, -1), ImGuiInputTextFlags_None);
    }

    ImGui::EndTable();	

    pattern_handle_shortcuts(pattern);
}

FOUNDATION_STATIC void pattern_open_floating_window(pattern_handle_t handle)
{
    pattern_t* pattern = (pattern_t*)pattern_get(handle);
    const stock_t* stock = pattern_refresh(pattern, FetchLevel::FUNDAMENTALS);
    if (stock == nullptr)
    {
        log_warnf(HASH_PATTERN, WARNING_INVALID_VALUE, STRING_CONST("Failed to open pattern window, stock not resolved"));
        return;
    }
    
    string_const_t pattern_name = SYMBOL_CONST(stock->name);
    string_const_t pattern_code = SYMBOL_CONST(pattern->code);
    const char* pattern_window_title = string_format_static_const("%.*s (%.*s)", STRING_FORMAT(pattern_name), STRING_FORMAT(pattern_code));
    window_open(pattern_window_title, L1(pattern_render(handle, PatternRenderFlags::HideTableHeaders)), WindowFlags::InitialProportionalSize);
}

FOUNDATION_STATIC void pattern_menu(pattern_handle_t handle)
{
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::BeginMenu(tr("Add")))
        {
            pattern_add_to_report_menu(handle);
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem(tr("Float Window")))
            pattern_open_floating_window(handle);

        ImGui::EndPopup();
    }

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu(tr("Pattern")))
        {
            pattern_t* pattern = (pattern_t*)pattern_get(handle);
            string_const_t code = string_table_decode_const(pattern->code);

            if (ImGui::MenuItem(tr("Read News")))
                news_open_window(STRING_ARGS(code));

            #if BUILD_DEVELOPMENT
            if (ImGui::MenuItem(tr("EOD"), nullptr, nullptr, true))
                system_execute_command(eod_build_url("eod", code.str, FORMAT_JSON, "order", "d").str);

            if (ImGui::MenuItem(tr("Trends"), nullptr, nullptr, true))
                system_execute_command(eod_build_url("calendar", "trends", FORMAT_JSON, "symbols", code.str).str);

            if (ImGui::MenuItem(tr("Earnings"), nullptr, nullptr, true))
            {
                time_t since_last_year = time_add_days(time_now(), -465);
                string_const_t date_str = string_from_date(since_last_year);
                system_execute_command(eod_build_url("calendar", "earnings", FORMAT_JSON, "symbols", code.str, "from", date_str.str).str);
            }

            if (ImGui::MenuItem(tr("Technical"), nullptr, nullptr, true))
                system_execute_command(eod_build_url("technical", code.str, FORMAT_JSON, "order", "d", "function", "splitadjusted").str);

            if (ImGui::MenuItem(tr("Fundamentals"), nullptr, nullptr, true))
                system_execute_command(eod_build_url("fundamentals", code.str, FORMAT_JSON).str);

            if (ImGui::MenuItem(tr("Real-time"), nullptr, nullptr, true))
                system_execute_command(eod_build_url("real-time", code.str, FORMAT_JSON).str);
            #endif

            ImGui::EndMenu();
        }
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

    config_handle_t checks_data = config_set_array(pattern_data, STRING_CONST("checks"));
    for (size_t i = 0; i < ARRAY_COUNT(pattern.checks); ++i)
    {
        auto cv_check = config_array_push(checks_data, CONFIG_VALUE_OBJECT);
        config_set(cv_check, STRING_CONST("checked"), pattern.checks[i].checked);
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
            tab_draw(tab_id.str, &(pattern->opened), L0(pattern_render(handle)), L0(pattern_menu(handle)));
        }
    }
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

    array_push(_patterns, (pattern_t{ code_symbol }));
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

bool pattern_menu_item(const char* symbol, size_t symbol_length)
{
    ImGui::BeginGroup();

    bool item_executed = false;
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

    ImGui::AlignTextToFramePadding();
    if (ImGui::Selectable(tr("Open Web Site " ICON_MD_OPEN_IN_NEW), false, ImGuiSelectableFlags_AllowItemOverlap))
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

    ImGui::EndGroup();
    return item_executed;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void pattern_initialize()
{
    if (_patterns == nullptr)
        array_reserve(_patterns, 8);

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

    service_register_tabs(HASH_PATTERN, pattern_render_tabs);
}

FOUNDATION_STATIC void pattern_deallocate(pattern_t* pattern)
{
    job_deallocate(pattern->lcf_job);

    foreach(lcfs, pattern->lcf_symbols)
        array_deallocate(lcfs->sequence);
    array_deallocate(pattern->lcf_symbols);

    foreach(e, pattern->lcf)
        array_deallocate(e->symbols);
    array_deallocate(pattern->lcf);

    array_deallocate(pattern->flex);
    array_deallocate(pattern->yy);
}

FOUNDATION_STATIC void pattern_shutdown()
{
    array_deallocate(_activities);

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

    array_deallocate(_patterns);
    _patterns = nullptr;
}

DEFINE_SERVICE(PATTERN, pattern_initialize, pattern_shutdown, SERVICE_PRIORITY_UI);
