/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "timeline.h"

#include "title.h"
#include "report.h"
#include "wallet.h"

#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/service.h>
#include <framework/dispatcher.h>

#define HASH_TIMELINE static_hash_string("timeline", 8, 0x8982c42357327efeULL)

typedef enum class TimelineTransactionType
{
    UNDEFINED = 0,
    BUY,
    SELL,
    DIVIDEND,
    EXCHANGE_RATE
} timeline_transaction_type_t;

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

static struct TIMELINE_MODULE {
    


} *_timeline;

//
// # PRIVATE
//

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL bool operator<(const timeline_stock_t& s, const hash_t& key) { return s.key < key; }
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL bool operator>(const timeline_stock_t& s, const hash_t& key) { return s.key > key; }

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL bool operator<(const timeline_t& s, const time_t& date) { return s.date < date; }
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL bool operator>(const timeline_t& s, const time_t& date) { return s.date > date; }

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

    // Sort transactions by date
    return array_sort_by(transactions, [](const timeline_transaction_t& a, const timeline_transaction_t& b)
    {
        if (a.date < b.date)
            return -1;

        if (a.date > b.date)
            return 1;

        // Sort buy transactions first for the same day.
        if (a.type < b.type)
            return -1;

        if (a.type > b.type)
            return 1;

        return 0;
    });
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

    //s.total_exchange_rate = 0;
    //s.average_exchange_rate = 1.0;

    array_insert(stocks, insert_at, s);
    return insert_at;
}

FOUNDATION_STATIC void timeline_day_update_total_value(timeline_t& day, string_const_t preferred_currency)
{
    double total_value = 0;
    foreach(s, day.stocks)
    {
        const day_result_t ed = stock_get_eod(STRING_ARGS(s->code), day.date);
        
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

    day.total_value = total_value;
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

        //s.total_exchange_rate += t->exchange_rate * t->qty;
        //s.average_exchange_rate = s.total_exchange_rate / s.qty;
        //FOUNDATION_ASSERT(math_real_is_finite(s.average_exchange_rate));

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
        // Stock will be remove on the next transaction day?

        s.total_value -= cost_total;
        s.average_price = math_real_is_zero(s.qty) ? 0 : s.total_value / s.qty;
        FOUNDATION_ASSERT(math_real_is_finite(s.average_price));

        //s.total_exchange_rate -= t->exchange_rate * adjusted_quantity_if_error;
        //s.average_exchange_rate = math_real_is_zero(s.qty) ? 1.0 : s.total_exchange_rate / s.qty;

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
            else
            {
                string_const_t date_string = string_from_date(t->date);
                log_infof(HASH_TIMELINE, STRING_CONST("\t\t\t\t  Disposing of %.*s on %.*s"), STRING_FORMAT(s->code), STRING_FORMAT(date_string));
            }
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
        string_const_t date_string = string_from_date(t->date);

        // Print transactions
        log_infof(HASH_TIMELINE, STRING_CONST("[%3u] Transaction: %s%-15s %.*s %7.0lf x %7.2lf $ x %5.4lg = %8.2lf $ (%.2lf, %.4lf)"),
            i, t->type == TimelineTransactionType::BUY ? "+" : "-",
            t->code, STRING_FORMAT(date_string),
            t->qty, t->price, t->exchange_rate, t->qty * t->price * t->exchange_rate,
            t->split_factor, t->adjusted_factor);

        int fidx = array_binary_search(days, array_size(days), t->date);
        if (fidx < 0)
            fidx = timeline_add_new_day(t, days, ~fidx);
        
        timeline_t& day = days[fidx];
        timeline_update_day(day, t, preferred_currency);

        #if BUILD_DEBUG
        log_infof(HASH_TIMELINE, STRING_CONST(
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

    {
        foreach(d, days)
        {
            string_const_t date_string = string_from_date(d->date);
            log_infof(HASH_TIMELINE, STRING_CONST("Timeline: [%2u] %.*s -> Funds: %8.2lf $ -> Investment: %9.2lf $ -> Gain: %8.2lf $ (%8.2lf $) -> Total: %8.2lf $ (%8.2lf $)"),
                array_size(d->stocks), STRING_FORMAT(date_string),
                d->total_fund, d->total_investment, d->total_gain, d->total_dividends, d->total_value, d->total_value + d->total_gain + d->total_dividends + d->total_fund);
        }
    }

    return days;
}

FOUNDATION_STATIC void timeline_deallocate(timeline_t*& timeline)
{
    foreach(d, timeline)
        array_deallocate(d->stocks);
    array_deallocate(timeline);
    timeline = nullptr;
}

//
// # PUBLIC API
//

void timeline_render_graph(const report_t* report)
{
    string_const_t preferred_currency = string_to_const(report->wallet->preferred_currency);
    auto transactions = timeline_report_compute_transactions(report, preferred_currency);
    auto timeline = timeline_build(transactions, preferred_currency);
    
    timeline_deallocate(timeline);
    array_deallocate(transactions);    
}

//
// # SYSTEM
//

FOUNDATION_STATIC void timeline_initialize()
{
    _timeline = MEM_NEW(HASH_TIMELINE, TIMELINE_MODULE);
}

FOUNDATION_STATIC void timeline_shutdown()
{

    MEM_DELETE(_timeline);
    
}

DEFINE_SERVICE(TIMELINE, timeline_initialize, timeline_shutdown, SERVICE_PRIORITY_UI);
