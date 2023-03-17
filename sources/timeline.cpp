/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "timeline.h"

#include "title.h"
#include "report.h"
#include "wallet.h"

#include <framework/app.h>
#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/service.h>
#include <framework/dispatcher.h>
#include <framework/string.h>
#include <framework/window.h>
#include <framework/array.h>

#define HASH_TIMELINE static_hash_string("timeline", 8, 0x8982c42357327efeULL)

typedef enum class TimelineTransactionType
{
    UNDEFINED = 0,
    BUY,
    SELL,
    DIVIDEND,
    EXCHANGE_RATE
} timeline_transaction_type_t;

struct plot_axis_format_t
{
    bool print_short_value{ true };
    time_t last_year{ 0 };
    ImPlotRect limits;
};

struct timeline_transaction_t
{
    time_t date{ 0 };
    hash_t code_key{ 0 };
    char code[16] = { '\0' };

    double qty{ NAN };
    double price{ NAN };
    timeline_transaction_type_t type { TimelineTransactionType::UNDEFINED };

    double close{ NAN };
    double split_close{ NAN };
    double adjusted_close{ NAN };
    double exchange_rate{ NAN };

    double split_factor{ 1.0 };
    double adjusted_factor{ 1.0 };
};

struct timeline_stock_t
{
    hash_t          key{ 0 };
    string_const_t  code{};
    double          qty{ 0 };
    double          total_value{ 0 };
    double          average_price{ 0 };

    // TODO: Remove if not used at the end.
    //double          total_exchange_rate{};
    //double          average_exchange_rate{ 1.0 };
};

struct timeline_t
{
    time_t date{ 0 };
    timeline_stock_t* stocks{ nullptr };
    
    double total_gain{ 0 };
    double total_dividends{ 0 };
    double total_value{ 0 };

    double total_fund{ 0 };
    double total_investment{ 0 };
};

struct timeline_report_t
{
    timeline_t* days{ nullptr };
    timeline_transaction_t* transactions{ nullptr };
    
    string_t title{};
    string_const_t preferred_currency{};

    bool first_render{ true };
};

struct timeline_plot_day_t
{
    const timeline_t* timeline;
    const function<double(const timeline_t* day)>& fn;
};

//
// # PRIVATE
//

FOUNDATION_FORCEINLINE bool operator<(const timeline_stock_t& s, const hash_t& key) { return s.key < key; }
FOUNDATION_FORCEINLINE bool operator>(const timeline_stock_t& s, const hash_t& key) { return s.key > key; }

FOUNDATION_FORCEINLINE bool operator<(const timeline_t& s, const time_t& date) { return s.date < date; }
FOUNDATION_FORCEINLINE bool operator>(const timeline_t& s, const time_t& date) { return s.date > date; }

FOUNDATION_FORCEINLINE bool operator<(const timeline_transaction_t& s, const time_t& date) { return s.date < date; }
FOUNDATION_FORCEINLINE bool operator>(const timeline_transaction_t& s, const time_t& date) { return s.date > date; }

FOUNDATION_FORCEINLINE int timeline_transaction_same_day_count(timeline_transaction_t* transactions, hash_t code, time_t date)
{
    int counter = 0;
    foreach(t, transactions)
    {
        if (t->code_key == code && t->date == date)
            counter++;
    }

    return counter;
}

FOUNDATION_STATIC timeline_transaction_t* timeline_report_compute_transactions(const report_t* report, string_const_t preferred_currency)
{
    timeline_transaction_t* transactions = nullptr;

    foreach(tt, report->titles)
    {
        title_t* t = *tt;
        string_const_t code = string_const(t->code, t->code_length);

        while (!title_is_resolved(t) && title_update(t, 10.0))
            dispatcher_wait_for_wakeup_main_thread(10000);
                    
        for (auto order : t->data["orders"])
        {
            string_const_t date_string = order["date"].as_string();
            if (date_string.length == 0)
            {
                log_warnf(HASH_TIMELINE, WARNING_INVALID_VALUE, STRING_CONST("Invalid %.*s date for order"), STRING_FORMAT(code));
                continue;
            }

            const time_t date = string_to_date(STRING_ARGS(date_string));
            if (date <= 0)
            {
                log_warnf(HASH_TIMELINE, WARNING_INVALID_VALUE, STRING_CONST("Invalid %.*s date for order on %.*s"), STRING_FORMAT(code), STRING_FORMAT(date_string));
                continue;
            }
            
            const bool buy = order["buy"].as_boolean();
            const bool sell = order["sell"].as_boolean();
            
            if (buy == sell)
            {
                log_warnf(HASH_TIMELINE, WARNING_INVALID_VALUE, STRING_CONST("Invalid %.*s order type for order on %.*s"), STRING_FORMAT(code), STRING_FORMAT(date_string));
                continue;
            }
            
            const double qty = order["qty"].as_number(0);
            if (qty <= 0)
            {
                log_warnf(HASH_TIMELINE, WARNING_INVALID_VALUE, STRING_CONST("Invalid %.*s quantity for order on %.*s"), STRING_FORMAT(code), STRING_FORMAT(date_string));
                continue;
            }
                
            const double price = order["price"].as_number();
            if (!math_real_is_finite(price))
            {
                log_warnf(HASH_TIMELINE, WARNING_INVALID_VALUE, STRING_CONST("Invalid %.*s price for order on %.*s"), STRING_FORMAT(code), STRING_FORMAT(date_string));
                continue;
            }
            
            timeline_transaction_t transaction{};
            FOUNDATION_ASSERT(t->code_length < 16);
            string_copy(transaction.code, 16, t->code, t->code_length);
            transaction.code_key = string_hash(t->code, t->code_length);
            transaction.date = date;
            transaction.qty = qty;
            transaction.price = price;
            transaction.type = buy ? TimelineTransactionType::BUY : TimelineTransactionType::SELL;

            day_result_t ed = stock_get_eod(STRING_ARGS(code), date);
            transaction.close = ed.close;
            transaction.adjusted_close = ed.adjusted_close;
            
            string_const_t title_currency = SYMBOL_CONST(t->stock->currency);
            transaction.exchange_rate = stock_exchange_rate(STRING_ARGS(title_currency), STRING_ARGS(preferred_currency), date);

            transaction.split_factor = stock_get_split_factor(STRING_ARGS(code), date);
            transaction.split_close = ed.close * transaction.split_factor;
            transaction.adjusted_factor = transaction.adjusted_close / transaction.split_close;

            array_push(transactions, transaction);
        }
    }

    transactions = array_sort(transactions, [transactions](const timeline_transaction_t& a, const timeline_transaction_t& b)
    {
        if (a.date < b.date)
            return -1;

        if (a.date > b.date)
            return 1;

        int ca = timeline_transaction_same_day_count(transactions, a.code_key, a.date);
        int cb = timeline_transaction_same_day_count(transactions, b.code_key, a.date);

        if (ca < cb)
            return -1;

        if (ca > cb)
            return 1;

        if (ca == 1)
        {
            // Sort buy transactions first for the same day.
            if (a.type > b.type)
                return -1;

            if (a.type < b.type)
                return 1;
        }

        // Sort buy transactions first for the same day.
        if (a.type < b.type)
            return -1;

        if (a.type > b.type)
            return 1;

        return 0;
    });

    return transactions;
}

FOUNDATION_STATIC int timeline_add_new_stock(const timeline_transaction_t* t, timeline_stock_t*& stocks, int insert_at)
{
    FOUNDATION_ASSERT(t);
    FOUNDATION_ASSERT(insert_at >= 0);

    timeline_stock_t s;
    s.key = t->code_key;
    s.qty = 0;
    s.total_value = 0;
    s.average_price = 0;
    s.code = string_const(t->code, string_length(t->code));

    array_insert(stocks, insert_at, s);
    return insert_at;
}

FOUNDATION_STATIC double timeline_compute_day_total_value(const timeline_stock_t* stocks, time_t at, string_const_t preferred_currency)
{
    double total_value = 0;
    foreach(s, stocks)
    {
        const day_result_t ed = stock_get_eod(STRING_ARGS(s->code), at);
        
        string_const_t stock_currency = stock_get_currency(STRING_ARGS(s->code));
        const double that_day_exchange_rate = stock_exchange_rate(STRING_ARGS(stock_currency), STRING_ARGS(preferred_currency));

        const double investment_value = s->qty * s->average_price;
        const double current_value = s->qty * ed.close * that_day_exchange_rate;

        #if BUILD_DEBUG
        const double idiff = math_abs(investment_value - s->total_value);
        if (idiff > 0.001)
        {
            log_warnf(HASH_TIMELINE, WARNING_SUSPICIOUS,
                STRING_CONST("Compare investment and stock total value: %.2lf <> %.2lf"),
                investment_value, s->total_value);
        }
        #endif

        total_value += current_value;
        FOUNDATION_ASSERT(math_real_is_finite(total_value));
    }

    return total_value;
}

FOUNDATION_STATIC void timeline_day_update_total_value(timeline_t& day, string_const_t preferred_currency)
{
    day.total_value = timeline_compute_day_total_value(day.stocks, day.date, preferred_currency);
}

FOUNDATION_STATIC void timeline_update_day(timeline_t& day, const timeline_transaction_t* t, string_const_t preferred_currency)
{
    int sidx = array_binary_search(day.stocks, array_size(day.stocks), t->code_key);
    if (sidx < 0)
        sidx = timeline_add_new_stock(t, day.stocks, ~sidx);

    timeline_stock_t& s = day.stocks[sidx];

    if (t->type == TimelineTransactionType::BUY)
    {
        const double buy_cost = t->qty * t->price * t->exchange_rate;
        s.qty += t->qty;
        FOUNDATION_ASSERT(s.qty >= 0);
        s.total_value += buy_cost;
        s.average_price = s.total_value / s.qty;

        if (day.total_fund >= buy_cost)
        {
            day.total_fund -= buy_cost;
        }
        else
        {
            day.total_investment += buy_cost - day.total_fund;
            day.total_fund = 0;
        }

        day.total_dividends += buy_cost * (1.0 - t->adjusted_factor);
    }
    else if (t->type == TimelineTransactionType::SELL)
    {
        double adjusted_quantity_if_error = t->qty;
        if (s.qty - t->qty < 0)
        {
            string_const_t date_string = string_from_date(t->date);
            log_warnf(HASH_TIMELINE, WARNING_SUSPICIOUS, STRING_CONST("[%s] %.*s -> Selling more stock (%.0lf) than available (%.0lf) [Make sure dates are accurante?]"),
                t->code, STRING_FORMAT(date_string), t->qty, s.qty);

            adjusted_quantity_if_error = s.qty;
        }

        // Compute gain
        const double sell_total = adjusted_quantity_if_error * t->price * t->exchange_rate;
        const double cost_total = s.average_price * adjusted_quantity_if_error;
        const double gain = sell_total - cost_total;

        s.qty -= adjusted_quantity_if_error;
        FOUNDATION_ASSERT(s.qty >= 0); 

        s.total_value -= cost_total;
        s.average_price = math_real_is_zero(s.qty) ? 0 : s.total_value / s.qty;
        FOUNDATION_ASSERT(math_real_is_finite(s.average_price));

        day.total_gain += gain;
        day.total_fund += sell_total;
        day.total_investment += gain;

        day.total_dividends -= cost_total * (1.0 - t->adjusted_factor);
    }
    else
    {
        FOUNDATION_ASSERT_FAIL("Transaction type not supported");
    }

    timeline_day_update_total_value(day, preferred_currency);
}

FOUNDATION_STATIC int timeline_add_new_day(const timeline_transaction_t* t, timeline_t*& days, int insert_at)
{
    FOUNDATION_ASSERT(t);
    FOUNDATION_ASSERT(insert_at >= 0);

    timeline_t* previous_day = array_last(days);

    timeline_t day;
    day.date = t->date;
    day.stocks = nullptr;

    if (previous_day == nullptr)
    {
        day.total_gain = 0;
        day.total_dividends = 0;
        day.total_value = 0;
        day.total_fund = 0;
        day.total_investment = 0;
    }
    else
    {
        day.total_gain = previous_day->total_gain;
        day.total_dividends = previous_day->total_dividends;
        day.total_value = previous_day->total_value;
        day.total_fund = previous_day->total_fund;
        day.total_investment = previous_day->total_investment;

        // Copy stocks which still have some qty
        foreach(s, previous_day->stocks)
        {
            if (s->qty > 0)
            {
                array_push(day.stocks, *s);
            }
            #if BUILD_DEBUG
            else
            {
                string_const_t date_string = string_from_date(t->date);
                log_debugf(HASH_TIMELINE, STRING_CONST("\t\t\t\t  Disposing of %.*s on %.*s"), STRING_FORMAT(s->code), STRING_FORMAT(date_string));
            }
            #endif
        }       
    }

    array_insert(days, insert_at, day);
    return insert_at;
}

FOUNDATION_STATIC timeline_t* timeline_build(const timeline_transaction_t* transactions, string_const_t preferred_currency)
{
    LOG_PREFIX(false);

    timeline_t* days = nullptr;
    foreach(t, transactions)
    {
        #if BUILD_DEVELOPMENT
        // Print transactions
        string_const_t date_string = string_from_date(t->date);
        log_infof(HASH_TIMELINE, STRING_CONST("[%3u] Transaction: %s%-15s %.*s %7.0lf x %7.2lf $ x %5.4lg = %8.2lf $ (%.2lf, %.4lf)"),
            i, t->type == TimelineTransactionType::BUY ? "+" : "-",
            t->code, STRING_FORMAT(date_string),
            t->qty, t->price, t->exchange_rate, t->qty * t->price * t->exchange_rate,
            t->split_factor, t->adjusted_factor);
        #endif

        int fidx = array_binary_search(days, array_size(days), t->date);
        if (fidx < 0)
            fidx = timeline_add_new_day(t, days, ~fidx);
        
        timeline_t& day = days[fidx];
        timeline_update_day(day, t, preferred_currency);

        #if BUILD_DEBUG
        log_debugf(HASH_TIMELINE, STRING_CONST(
            "\t\t\t\t\tFund:       %9.2lf $\n"
            "\t\t\t\t\tGain:       %9.2lf $\n"
            "\t\t\t\t\tDividends:  %9.2lf $\n"
            "\t\t\t\t\tInvestment: %9.2lf $\n"
            "\t\t\t\t\tTotal [%2u]: %9.2lf $ (%.2lf $)"),
            day.total_fund, day.total_gain, day.total_dividends, day.total_investment, 
            array_size(day.stocks), day.total_value,
            day.total_value + day.total_gain + day.total_dividends + day.total_fund);
        #endif
    }

    #if BUILD_DEVELOPMENT
    foreach(d, days)
    {
        string_const_t date_string = string_from_date(d->date);
        log_infof(HASH_TIMELINE, STRING_CONST("Timeline: [%2u] %.*s -> Funds: %8.2lf $ -> Investment: %9.2lf $ -> Gain: %8.2lf $ (%8.2lf $) -> Total: %8.2lf $ (%8.2lf $)"),
            array_size(d->stocks), STRING_FORMAT(date_string),
            d->total_fund, d->total_investment, d->total_gain, d->total_dividends, d->total_value, d->total_value + d->total_dividends + d->total_fund);
    }
    #endif

    return days;
}

FOUNDATION_STATIC void timeline_deallocate(timeline_t*& timeline)
{
    foreach(d, timeline)
        array_deallocate(d->stocks);
    array_deallocate(timeline);
    timeline = nullptr;
}

FOUNDATION_STATIC timeline_report_t* timeline_report_allocate(const report_t* report)
{
    timeline_report_t* timeline_report = (timeline_report_t*)memory_allocate(HASH_TIMELINE, sizeof(timeline_report_t), 0, MEMORY_PERSISTENT);
    timeline_report->days = nullptr;
    timeline_report->transactions = nullptr;
    
    string_const_t report_name = SYMBOL_CONST(report->name);
    timeline_report->title = string_allocate_format(STRING_CONST("Timeline %.*s"), STRING_FORMAT(report_name));
    timeline_report->preferred_currency = string_to_const(report->wallet->preferred_currency);

    return timeline_report;
}

FOUNDATION_STATIC void timeline_report_deallocate(timeline_report_t*& timeline_report)
{
    string_deallocate(timeline_report->title.str);
    timeline_deallocate(timeline_report->days);
    array_deallocate(timeline_report->transactions);
    memory_deallocate(timeline_report);
    timeline_report = nullptr;
}

FOUNDATION_STATIC void timeline_report_plot_day_value(const char* title, const timeline_t* timeline, function<double(const timeline_t* day)>&& fn, float line_weight = 2.0f, bool default_hide = false)
{
    timeline_plot_day_t plot{ timeline, fn };
    
    ImPlot::SetAxis(ImAxis_Y1);
    ImPlot::HideNextItem(default_hide, ImPlotCond_Once);

    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, line_weight);
    ImPlot::PlotLineG(title, [](int idx, void* user_data)->ImPlotPoint
    {
        timeline_plot_day_t* plot = (timeline_plot_day_t*)user_data;
        const timeline_t* t = plot->timeline + idx;
        const double x = (double)t->date;
        const double y = plot->fn(t);
        return ImPlotPoint(x, y);
    }, &plot, array_size(timeline), ImPlotLineFlags_SkipNaN);
    ImPlot::PopStyleVar(1);
}

FOUNDATION_STATIC void timeline_report_plot_day_bar_value(const char* title, const timeline_t* timeline, function<double(const timeline_t* day)>&& fn, double bar_size = 8 * 60 * 60.0, bool default_hide = false)
{
    timeline_plot_day_t plot{ timeline, fn };

    if (bar_size == 0)
        bar_size = 8 * 60 * 60.0;
    
    ImPlot::SetAxis(ImAxis_Y2);
    ImPlot::HideNextItem(default_hide, ImPlotCond_Once);

    ImPlot::PlotBarsG(title, [](int idx, void* user_data)->ImPlotPoint
    {
        timeline_plot_day_t* plot = (timeline_plot_day_t*)user_data;
        const timeline_t* t = plot->timeline + idx;
        const double x = (double)t->date;
        const double y = t->stocks ? plot->fn(t) : NAN;
        return ImPlotPoint(x, y);
    }, &plot, array_size(timeline), bar_size, ImPlotBarsFlags_None);
}

FOUNDATION_STATIC void timeline_report_graph_limit(const char* label, double min, double max, double value)
{
    const double range[]{ min, max };
    const double limit[]{ value, value };
    ImPlot::PlotLine(label, range, limit, ARRAY_COUNT(limit), ImPlotLineFlags_NoClip);
}

FOUNDATION_STATIC int timeline_report_graph_date_format(double value, char* buff, int size, void* user_data)
{
    const time_t time = (time_t)value;
    const plot_axis_format_t& f = *(plot_axis_format_t*)user_data;

    if (size > 0)
        buff[0] = '\0';

    if (f.print_short_value)
    {
        tm tm_year = *localtime(&time);
        if (tm_year.tm_mon == 0 && tm_year.tm_mday < 5)
            return 0;

        if (tm_year.tm_mon == 11 && tm_year.tm_mday > 26)
            return 0;

        string_const_t value_str = string_from_date(time);
        return (int)string_copy(buff, size, value_str.str + 5, 5).length;
    }

    string_const_t value_str = string_from_date(time);
    return (int)string_copy(buff, size, value_str.str, value_str.length).length;
}

FOUNDATION_STATIC int timeline_report_graph_total_amount_format(double value, char* buff, int size, void* user_data)
{
    const plot_axis_format_t& f = *(plot_axis_format_t*)user_data;
    if (f.print_short_value)
    {
        const double abs_value = math_abs(value);

        #if 0 // Will I ever be that rich?
        if (abs_value >= 1e12)
            return (int)string_format(buff, size, STRING_CONST("$ %3.3gT"), value / 1e12).length;
        if (abs_value >= 1e9)
            return (int)string_format(buff, size, STRING_CONST("$ %3.3gB"), value / 1e9).length;
        #endif

        if (abs_value >= 1e6)
            return (int)string_format(buff, size, STRING_CONST("$ %3.3gM"), value / 1e6).length;

        if (abs_value >= 1e3)
            return (int)string_format(buff, size, STRING_CONST("$ %3.3gK"), value / 1e3).length;

        return (int)string_format(buff, size, STRING_CONST("$ %.0lf"), value).length;
    }

    string_const_t value_str = string_from_currency(value, "9 999 999 $");
    return (int)string_copy(buff, size, STRING_ARGS(value_str)).length;
}

FOUNDATION_STATIC bool timeline_report_graph(timeline_report_t* report)
{
    if (!ImPlot::BeginPlot("Timeline", { -1,-1 }, ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle))
        return false;

    if (ImGui::IsWindowAppearing())
        dispatch(L0(ImPlot::SetNextAxesToFit()));

    auto summary = array_last(report->days);

    plot_axis_format_t axis_format{};
    const double min_d = (double)report->days[0].date;
    const double max_d = (double)array_last(report->days)->date;

    ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_None);

    ImPlot::SetupAxis(ImAxis_X1, "##Date", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, min_d, max_d + time_one_day() * 5.0);
    ImPlot::SetupAxisFormat(ImAxis_X1, timeline_report_graph_date_format, &axis_format);

    ImPlot::SetupAxis(ImAxis_Y1, "##TotalAmounts", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Opposite);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, INFINITY);
    ImPlot::SetupAxisFormat(ImAxis_Y1, timeline_report_graph_total_amount_format, &axis_format);

    ImPlot::SetupAxis(ImAxis_Y2, "##SmallAmounts", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisFormat(ImAxis_Y2, timeline_report_graph_total_amount_format, &axis_format);

    ImPlot::SetupFinish();

    const ImPlotRect& limits = ImPlot::GetPlotLimits();
    axis_format.limits = limits;
    axis_format.print_short_value = false;

    ImPlot::SetAxis(ImAxis_Y2);
    ImPlot::HideNextItem(false, ImPlotCond_Once);
    timeline_report_plot_day_bar_value("Gain", report->days, L1(_1->total_gain + _1->total_dividends));

    ImPlot::SetAxis(ImAxis_Y1);
    ImPlot::HideNextItem(true, ImPlotCond_Once);
    timeline_report_graph_limit("Stock Value", min_d, max_d, summary->total_value);
    timeline_report_plot_day_value("Stock Value", report->days, L1(_1->total_value), 2.0f, true);

    ImPlot::SetAxis(ImAxis_Y2);
    ImPlot::HideNextItem(false, ImPlotCond_Once);
    timeline_report_plot_day_bar_value("+Funds", report->days, L1(_1->total_fund), 0, true);

    ImPlot::SetAxis(ImAxis_Y2);
    ImPlot::HideNextItem(true, ImPlotCond_Once);
    timeline_report_graph_limit("+Dividends", min_d, max_d, summary->total_dividends);
    timeline_report_plot_day_bar_value("+Dividends", report->days, L1(_1->total_dividends), 0, true);

    timeline_report_plot_day_value("+Funds", report->days, L1(_1->total_value + _1->total_fund), 1.0f, true);
    timeline_report_plot_day_value("+Dividends", report->days, L1(_1->total_value + _1->total_dividends), 1.0f, true);

    ImPlot::SetAxis(ImAxis_Y1);
    timeline_report_graph_limit("Investments", min_d, max_d, summary->total_investment - summary->total_dividends - summary->total_gain);
    timeline_report_plot_day_value("Investments", report->days, L1(_1->total_investment - _1->total_dividends - _1->total_gain), 2.0f);

    ImPlot::SetAxis(ImAxis_Y1);
    timeline_report_graph_limit("Total Value##5", min_d, max_d, summary->total_value + summary->total_fund + summary->total_dividends);
    timeline_report_plot_day_value("Total Value##5", report->days, L1(_1->total_value + _1->total_fund + _1->total_dividends), 4.0f);

    ImPlot::SetAxis(ImAxis_Y1);
    ImPlot::HideNextItem(true, ImPlotCond_Once);
    timeline_report_graph_limit("Total Wealth", min_d, max_d, summary->total_investment);
    timeline_report_plot_day_value("Total Wealth", report->days, L1(_1->total_investment), 2.0f, true);

    const time_t min_time = (time_t)limits.X.Min + time_one_day() * 5;
    const int year_range = math_ceil(time_elapsed_days((time_t)min_time, (time_t)max_d) / 365.0);

    tm tm_year = *localtime(&min_time);
    tm_year.tm_yday = 0;
    tm_year.tm_mday = 1;
    tm_year.tm_mon = 0;
    ImPlot::TagX((double)min_time, ImColor::HSV(155 / 360.0f, 0.75f, 0.5f), "%d", 1900 + tm_year.tm_year);
    for (int i = 0, end = year_range; i != end; ++i)
    {
        tm_year.tm_year++;
        const double y = (double)mktime(&tm_year);
        ImPlot::TagX(y, ImColor::HSV(155 / 360.0f, 0.75f, 0.5f), "%d", 1900 + tm_year.tm_year);
    }

    if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl))
    {
        ImPlotPoint ppos = ImPlot::GetPlotMousePos(ImAxis_X1, ImAxis_Y1);
        time_t ppos_date = (time_t)ppos.x;

        const unsigned transaction_count = array_size(report->transactions);
        int tidx = array_binary_search(report->transactions, transaction_count, ppos_date);
        if (tidx < 0)
            tidx = ~tidx;

        const int max_transactions = 10;
        for (int i = max(0, tidx - max_transactions/2), end = min(tidx + max_transactions/2, to_int(transaction_count)); i < end; ++i)
        {
            timeline_transaction_t* t = &report->transactions[i];

            // Compute point position around #ppos that make a circle for each transaction
            const float angle = i * (2 * FLT_PI / max_transactions) + ((t->code_key % 120) / 120.0f + 0.3f);
            const float radius = (t->type == TimelineTransactionType::BUY ? 80.0f : 190.0f) + ((t->code_key % 120));
            const ImVec2 offset{ radius * cosf(angle), radius * sinf(angle) };

            if (t->type == TimelineTransactionType::BUY)
            {
                ImPlot::Annotation(ppos.x, ppos.y, ImColor(ImU32(t->code_key | 0xFF000000)), offset, true, "+%s", t->code);
            }
            else
            {
                ImPlot::Annotation(ppos.x, ppos.y, ImColor(ImU32(t->code_key | 0xAA110000)), offset, true, "-%s", t->code);
            }
        }
    }

    ImPlot::EndPlot();

    return true;
}

FOUNDATION_STATIC void timeline_report_toolbar(timeline_report_t* report)
{
    const unsigned transaction_count = array_size(report->transactions);
    if (report == nullptr || transaction_count == 0)
        return;

    constexpr string_const_t LARGE_AMOUNT_FORMAT = CTEXT("9" THIN_SPACE "999" THIN_SPACE "999" THIN_SPACE ICON_MD_ATTACH_MONEY);
    
    ImGui::BeginGroup();

    auto last_day = array_last(report->days);
    string_const_t last_date_string = string_from_date(last_day->date);

    ImGui::TrText(ICON_MD_STACKED_LINE_CHART " [%u] %.*s", transaction_count, STRING_FORMAT(last_date_string));
    if (ImGui::IsItemHovered())
    {
        auto first_day = &report->days[0];
        string_const_t first_date_string = string_from_date(first_day->date);
        ImGui::SetTooltip("You've made %u transactions since %.*s", transaction_count, STRING_FORMAT(first_date_string));
    }

    ImGui::SameLine();
    ImGui::TrText(ICON_MD_WALLET " %.2lf $", last_day->total_fund);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("You should have about %.0lf $ fund remaining as of %.*s", last_day->total_fund, STRING_FORMAT(last_date_string));

    ImGui::SameLine();
    ImGui::TrText(ICON_MD_DIFFERENCE " %.2lf $", last_day->total_gain);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("You've made about %.0lf $ by selling stock so far", last_day->total_gain);

    ImGui::SameLine();
    ImGui::TrText(ICON_MD_ASSIGNMENT_RETURN " %.2lf $", last_day->total_dividends);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("You've made about %.0lf $ in dividend's return.", last_day->total_dividends);

    ImGui::SameLine();
    const double raw_investment_cost = last_day->total_investment - last_day->total_dividends - last_day->total_gain;
    string_const_t currency_formatted = string_from_currency(raw_investment_cost, STRING_ARGS(LARGE_AMOUNT_FORMAT));
    ImGui::TrText(ICON_MD_SAVINGS " %.*s", STRING_FORMAT(currency_formatted));
    if (ImGui::IsItemHovered())
    {
        const double imin = min(raw_investment_cost, last_day->total_investment);
        const double imax = max(raw_investment_cost, last_day->total_investment);
        ImGui::SetTooltip("You've taken about %.0lf $ out of your wallet to make those investments and re-invested gain for about %.0lf $.", imin, imax);
    }

    ImGui::SameLine();
    const double total_value_adjusted = last_day->total_value + last_day->total_dividends + last_day->total_fund;
    currency_formatted = string_from_currency(total_value_adjusted, STRING_ARGS(LARGE_AMOUNT_FORMAT));
    ImGui::TrText(ICON_MD_ACCOUNT_BALANCE_WALLET " %.*s", STRING_FORMAT(currency_formatted));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("As of %.*s your stock value is worth about %.0lf $.", STRING_FORMAT(last_date_string), total_value_adjusted);

    ImGui::SameLine();
    const double total_gain = total_value_adjusted - raw_investment_cost;
    currency_formatted = string_from_currency(total_gain, STRING_ARGS(LARGE_AMOUNT_FORMAT));
    ImGui::TrText(ICON_MD_PRICE_CHANGE " %.*s", STRING_FORMAT(currency_formatted));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("As of %.*s you can say that you've gain or lost about %.0lf $.", STRING_FORMAT(last_date_string), total_gain);

    ImGui::EndGroup();
}

FOUNDATION_STATIC bool timeline_report_graph_dialog(void* user_data)
{
    timeline_report_t* report = (timeline_report_t*)user_data;

    if (array_size(report->days) <= 2)
    {
        ImGui::TrTextUnformatted("No transactions to display");
        return true;
    }

    timeline_report_toolbar(report);
    return timeline_report_graph(report);
}

FOUNDATION_STATIC void timeline_report_graph_close(void* user_data)
{
    timeline_report_t* timeline_report = (timeline_report_t*)user_data;
    timeline_report_deallocate(timeline_report);
}

FOUNDATION_STATIC void timeline_window_render_report(window_handle_t window_handle)
{
    timeline_report_t* report = (timeline_report_t*)window_get_user_data(window_handle);
    FOUNDATION_ASSERT(report);

    if (array_size(report->days) <= 2)
        return ImGui::TrTextUnformatted("No transactions to display");

    timeline_report_toolbar(report);
    timeline_report_graph(report);

    if (report->first_render)
    {
        ImPlot::SetNextAxesToFit();
        report->first_render = false;
    }
}

FOUNDATION_STATIC void timeline_window_report_close(window_handle_t window_handle)
{
    timeline_report_t* report = (timeline_report_t*)window_get_user_data(window_handle);
    FOUNDATION_ASSERT(report);
    
    timeline_report_deallocate(report);
}

//
// # PUBLIC API
//

void timeline_render_graph(const report_t* report)
{
    timeline_report_t* timeline_report = timeline_report_allocate(report);
    timeline_report->transactions = timeline_report_compute_transactions(report, timeline_report->preferred_currency);
    timeline_report->days = timeline_build(timeline_report->transactions, timeline_report->preferred_currency);

    // Insert new days with updated total current value
    auto days = timeline_report->days;
    for (time_t d = days[0].date, end = time_now() + time_one_day() / 2; d <= end; d += time_one_day())
    {
        int didx = array_binary_search(days, array_size(days), d);
        if (didx >= 0)
            continue;

        didx = ~didx;        
        int idx = didx;
        timeline_t* previous_day = nullptr;
        while (idx >= 0)
        {
            if (to_uint(idx) < array_size(days) && days[idx].stocks)
            {
                previous_day = &days[idx];
                break;
            }
            idx--;
        }

        if (previous_day == nullptr)
            continue;

        timeline_t day;
        day.date = d;
        day.stocks = nullptr;
        day.total_gain = previous_day->total_gain;
        day.total_dividends = previous_day->total_dividends;
        day.total_value = day.total_value = timeline_compute_day_total_value(previous_day->stocks, day.date, timeline_report->preferred_currency);
        day.total_fund = previous_day->total_fund;
        day.total_investment = previous_day->total_investment;
        timeline_report->days = array_insert(days, didx, day);
    }

    #if 0
    app_open_dialog(timeline_report->title.str, 
        timeline_report_graph_dialog, 1200, 900, true, 
        timeline_report, timeline_report_graph_close);
    #else
    window_open(
        "timeline_window", STRING_ARGS(timeline_report->title), 
        timeline_window_render_report, timeline_window_report_close, 
        timeline_report, WindowFlags::Transient | WindowFlags::Maximized);
    #endif
}

//
// # SYSTEM
//

FOUNDATION_STATIC void timeline_initialize()
{
}

FOUNDATION_STATIC void timeline_shutdown()
{    
}

DEFINE_SERVICE(TIMELINE, timeline_initialize, timeline_shutdown, SERVICE_PRIORITY_UI);
