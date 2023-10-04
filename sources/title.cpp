/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 */
 
#include "title.h"

#include "eod.h"
#include "wallet.h"
 
#include <framework/math.h>
#include <framework/query.h>
#include <framework/string.h>
#include <framework/array.h>
#include <framework/jobs.h>

#define FIELD_FILTERS_INTERNAL "::filters"

#define HASH_TITLE static_hash_string("title", 5, 0xf0e1318ee776a40aULL)

//
// # PRIVATE
//

template<typename T = double>
FOUNDATION_STATIC FOUNDATION_FORCEINLINE T title_get_compute_value(
    const title_t* title_compute_data,
    size_t field_offset,
    const function<T(const title_t*)>& get_value_fn = nullptr)
{
    T res = 0;
    if (get_value_fn)
        res = get_value_fn(title_compute_data);
    else
        res = field_offset != -1 ? *(T*)(((uint8_t*)title_compute_data) + field_offset) : T{};
    return res;
}

FOUNDATION_STATIC bool title_recently_changed(const title_t* t, double* out_delta, double since_seconds, double* out_elapsed_seconds)
{
    const stock_t* s = t->stock;
    if (s == nullptr)
        return false;

    const day_result_t* last_results = array_last(s->previous);
    if (last_results == nullptr)
        return false;

    double elapsed_seconds = time_elapsed_days(last_results->date, s->current.date) * 86400.0;
    if (out_elapsed_seconds)
        *out_elapsed_seconds = time_elapsed_days(s->current.date, time_now()) * 86400.0;
    if (elapsed_seconds > since_seconds)
        return false;

    if (out_delta)
        *out_delta = s->current.adjusted_close - last_results->adjusted_close;
    return true;
}

FOUNDATION_STATIC double title_compute_ps(const title_t* t, const stock_t* s)
{
    if (t->average_price == 0)
        return DNAN;

    const double profit_ask = t->wallet->profit_ask;
    const double average_days = t->wallet->average_days;
    const double target_ask = t->wallet->target_ask;

    double M = 0;
    double average_fg = (t->average_price + s->current.adjusted_close) / 2.0;
    const double days_held = title_average_days_held(t);
    if (days_held >= 30)
        M = max(t->average_price, average_fg) * (1.0 + profit_ask - ((days_held - average_days) / 20 / 100));
    else
        M = max(t->average_price, average_fg) * max(1.2, 1 + target_ask);
    double K = M / t->average_price - 1.0;
    double N = s->current.change_p;
    double O = title_get_yesterday_change(t, s);
    double P = title_get_range_change_p(t, s, -7, true);
    double Q = title_get_range_change_p(t, s, -31, true);

    int average_nq_count = 0;
    double average_nq = 0.0;
    if (!math_real_is_nan(N)) { average_nq_count++; average_nq += N; }
    if (!math_real_is_nan(O)) { average_nq_count++; average_nq += O; }
    if (!math_real_is_nan(P)) { average_nq_count++; average_nq += P; }
    if (!math_real_is_nan(Q)) { average_nq_count++; average_nq += Q; }
    if (average_nq_count > 0)
        average_nq = average_nq / average_nq_count;
    double R = s->dividends_yield.get_or_default(0);

    return title_get_total_gain_p(t) * (1.0 + K) - average_nq + N + R;
}

FOUNDATION_STATIC bool title_fetch_ps(const title_t* t, double& value)
{
    const stock_t* s = t->stock;
    if (s == nullptr || !s->has_resolve(FetchLevel::REALTIME | FetchLevel::FUNDAMENTALS))
        return false;

    if (title_is_index(t))
    {
        value = (s->current.sar - s->current.price) / s->current.price * 100.0;
        return s->has_resolve(FetchLevel::TECHNICAL_SAR);
    }

    // Handle cases where the stock has been dismissed from the market.
    if (math_real_is_nan(s->current.adjusted_close))
    {
        value = DNAN;
        return true;
    }

    if (title_sold(t))
    {
        // Return the prediction in case the stock was kept (when sold)
        const double average_sell_price = t->sell_total_price_rated / t->sell_total_quantity;
        value = ((average_sell_price - s->current.adjusted_close) / s->current.adjusted_close) * 100.0;
    }
    else
        value = title_compute_ps(t, s);

    return !math_real_is_nan(value);
}

FOUNDATION_STATIC bool title_fetch_today_exchange_rate(const title_t* t, double& value)
{
    const stock_t* s = t->stock;
    if (s == nullptr || s->currency == 0)
        return false;

    string_const_t title_currency = string_table_decode_const(s->currency);
    const auto exchange_rate = stock_exchange_rate(STRING_ARGS(title_currency), STRING_ARGS(t->wallet->preferred_currency));
    value = math_ifnan(exchange_rate, s->current.adjusted_close);
    return true;
}

FOUNDATION_STATIC bool title_fetch_ask_price(const title_t* t, double& value)
{
    const stock_t* s = t->stock;
    if (s == nullptr || !s->has_resolve(FetchLevel::REALTIME | FetchLevel::FUNDAMENTALS))
        return false;

    const double profit_ask = t->wallet->profit_ask;
    const double average_days = t->wallet->average_days;
    const double target_ask = t->wallet->target_ask;

    const double days_held = math_max(90, title_average_days_held(t));
    const double average_fg = (t->average_price + s->current.adjusted_close) / 2.0;
    unsigned samples = 0;
    double sampling_average_fg = 0.0f;
    unsigned max_samping_days = math_floor(days_held / 2.0f);
    for (unsigned i = 2, end = array_size(s->history); i < end && samples < max_samping_days; ++i)
    {
        if (s->history[i].date > t->date_average)
        {
            sampling_average_fg += s->history[i].adjusted_close;
            samples++;
        }
    }

    if (samples > 0)
    {
        sampling_average_fg /= samples;
        sampling_average_fg = (t->average_price + s->current.adjusted_close + sampling_average_fg) / 3.0;
    }
    else
    {
        sampling_average_fg = max(t->average_price, average_fg);
    }

    value = sampling_average_fg * (1.0 + profit_ask - (days_held - average_days) / 20.0 / 100.0);
    
    return !math_real_is_nan(value);
}

//
// # PUBLIC API
//

double title_get_total_value(const title_t* t)
{
    if (title_sold(t))
        return t->sell_total_price_rated;

    const stock_t* s = t->stock;
    if (s && s->has_resolve(FetchLevel::REALTIME))
    {
        const double total_value = t->average_quantity * s->current.adjusted_close * t->today_exchange_rate.fetch();
        return total_value;
    }

    return t->average_quantity * t->average_price_rated;
}

double title_total_bought_price(const title_t* t)
{
    if (title_sold(t))
        return 0;

    return t->buy_total_price_rated - t->sell_total_price_rated;
}

double title_get_total_investment(const title_t* t)
{
    if (title_sold(t))
        return t->buy_total_price_rated;
    return t->average_quantity * t->average_price_rated;
}

double title_get_total_gain(const title_t* t)
{
    if (t->average_quantity == 0 && t->sell_total_quantity == 0)
        return DNAN;
    if (t->average_quantity == 0)
        return (t->sell_total_price_rated - t->buy_total_price_rated) + t->total_dividends;

    return title_get_total_value(t) - title_get_total_investment(t) + t->total_dividends;
}

double title_get_total_gain_p(const title_t* t)
{
    if (t->average_quantity == 0 && t->sell_total_quantity == 0)
        return DNAN;

    if (title_sold(t))
    {
        if (t->buy_total_price_rated > 0)
            return (t->sell_total_price_rated - t->buy_total_price_rated) / t->buy_total_price_rated * 100.0;
    }

    double total_investment = title_get_total_investment(t);
    if (total_investment != 0)
        return title_get_total_gain(t) * 100.0 / total_investment;

    return 0.0;
}

double title_get_yesterday_change(const title_t* t, const stock_t* s)
{
    const day_result_t* ed = stock_get_EOD(s, -1);
    return ed ? ed->change_p : DNAN;
}

double title_get_range_change_p(const title_t* t, const stock_t* s, int rel_days, bool take_last /*= false*/)
{
    const day_result_t* ed = stock_get_EOD(s, rel_days, take_last);
    if (ed == nullptr)
        return DNAN;

    const day_result_t& current = s->current;
    return (current.adjusted_close - ed->adjusted_close) / ed->adjusted_close * 100.0;
}

double title_get_day_change(const title_t* t, const stock_t* s)
{
    if (t->average_quantity == 0)
        return DNAN;

    double change = s->current.change;
    if (math_real_is_finite_nz(s->current.previous_close))
        change = s->current.price - s->current.previous_close;

    return change * t->average_quantity * t->today_exchange_rate.fetch();
}

config_handle_t title_get_fundamental_config_value(title_t* title, const char* filter_name, size_t filter_name_length)
{
    auto filter_string = string_const(filter_name, filter_name_length);
    auto filters = config_set_object(title->data, STRING_CONST(FIELD_FILTERS_INTERNAL));
    auto filter_value = config_find(filters, STRING_ARGS(filter_string));
    if (filter_value)
        return filter_value;

    filter_value = config_set(filters, STRING_ARGS(filter_string), STRING_CONST("..."));
    if (eod_fetch_async("fundamentals", title->code, FORMAT_JSON_CACHE, [filter_value, filter_string](const json_object_t& json)
    {
        const bool allow_nulls = false;
        json_object_t ref = json.find(STRING_ARGS(filter_string), allow_nulls);
        if (!ref.is_null())
        {
            config_set(filter_value, STRING_ARGS(ref.as_string()));
        }
        else
        {
            // No match
            config_set(filter_value, STRING_CONST("-"));
        }
    }, 3 * 24ULL * 3600ULL))
    {
        return filter_value;
    }

    config_remove(filters, STRING_ARGS(filter_string));
    return config_null();
}

void title_init(title_t* t, wallet_t* wallet, const config_handle_t& data)
{
    const config_tag_t TITLE_DATE = config_tag(data, STRING_CONST("date"));
    const config_tag_t TITLE_BUY = config_tag(data, STRING_CONST("buy"));
    const config_tag_t TITLE_SELL = config_tag(data, STRING_CONST("sell"));
    const config_tag_t TITLE_QTY = config_tag(data, STRING_CONST("qty"));
    const config_tag_t TITLE_PRICE = config_tag(data, STRING_CONST("price"));
    const config_tag_t TITLE_ASK_PRICE = config_tag(data, STRING_CONST("ask"));
    const config_tag_t TITLE_EXCHANGE_RATE = config_tag(data, STRING_CONST("xcg"));
    const config_tag_t TITLE_SPLIT_FACTOR = config_tag(data, STRING_CONST("split"));

    t->data = data;
    t->wallet = wallet;

    t->date_min = 0;
    t->date_max = 0;
    t->date_average = 0;
    t->elapsed_days = 0;
    
    t->buy_total_count = 0;
    t->sell_total_count = 0;
    
    t->buy_total_price = 0;
    t->buy_total_quantity = 0;
    
    t->sell_total_price = 0;
    t->sell_total_quantity = 0;
    
    t->buy_total_price_rated = 0;
    t->sell_total_price_rated = 0;

    t->average_price = 0;
    t->average_quantity = 0;
    t->average_price_rated = 0;

    t->total_gain = 0;
    t->total_dividends = 0;
    t->average_ask_price = 0;
    t->average_exchange_rate = 1;

    string_const_t title_code = config_name(data);
    t->code_length = string_copy(STRING_BUFFER(t->code), STRING_ARGS(title_code)).length;

    #if 0
    // Initiate to resolve the title stock right away in case it has never been done before.
    if (main_is_interactive_mode(true) && t->wallet && t->wallet->track_history)
    {
        const auto fetch_level = title_minimum_fetch_level(t);
        if (!t->stock)
            t->stock = stock_request(t->code, t->code_length, fetch_level);
    }
    #endif

    int valid_dates = 0;
    double total_ask_price = 0;
    double total_ask_count = 0;
    double total_buy_limit_price = 0;
    double total_exchange_rate = 0;
    double total_exchange_rate_count = 0;
    
    const stock_t* s = title_is_resolved(t) ? t->stock : nullptr;
    string_t preferred_currency = t->wallet->preferred_currency;
    string_const_t stock_currency = s ? string_table_decode_const(s->currency) : string_null();
    const double dividends_yield = s ? math_ifnan(s->dividends_yield.fetch(), 0) : 0.0;

    // Check if the title has been fully sold
    double total_current_quantity = 0;
    for (auto order : data["orders"])
    {
        const double qty = order[TITLE_QTY].as_number();
        const bool buy = order[TITLE_BUY].as_boolean();
        const bool sell = order[TITLE_SELL].as_boolean();

        if (buy)
        {
            total_current_quantity += qty;
        }
        else if (sell)
        {
            total_current_quantity -= qty;
        }
    }

    for (auto order : data["orders"])
    {
        string_const_t date = order[TITLE_DATE].as_string();
        const bool buy = order[TITLE_BUY].as_boolean();
        const bool sell = order[TITLE_SELL].as_boolean();
        const double qty = order[TITLE_QTY].as_number();
        const double price = order[TITLE_PRICE].as_number();
        const double ask_price = order[TITLE_ASK_PRICE].as_number();
        const time_t order_date = string_to_date(STRING_ARGS(date));

        double order_split_factor = order[TITLE_SPLIT_FACTOR].as_number();
        double order_exchange_rate = order[TITLE_EXCHANGE_RATE].as_number();
        if (s)
        {
            if (math_real_is_nan(order_split_factor))
            {
                order_split_factor = stock_get_split_factor(STRING_ARGS(title_code), order_date);
                config_set(order, "split", order_split_factor);
            }

            if (math_real_is_nan(order_exchange_rate))
            {
                order_exchange_rate = stock_exchange_rate(STRING_ARGS(stock_currency), STRING_ARGS(preferred_currency), order_date);
                order_exchange_rate = math_ifzero(order_exchange_rate, 1.0);
                config_set(order, "xcg", order_exchange_rate);
            }
        }
        else
        {
            if (math_real_is_nan(order_split_factor))
                order_split_factor = 1.0;

            if (math_real_is_nan(order_exchange_rate))
                order_exchange_rate = 1.0;
        }

        total_exchange_rate_count += qty;
        total_exchange_rate += order_exchange_rate * qty;

        // Compute date stats.
        if (order_date != 0)
        {
            if (t->date_min == 0 || t->date_min > order_date)
                t->date_min = order_date;

            if (t->date_max == 0 || t->date_max < order_date)
                t->date_max = order_date;

            valid_dates++;
            t->date_average += order_date;
        }
        
        if (ask_price > 0)
        {
            total_ask_count++;
            total_ask_price += ask_price;
            total_buy_limit_price += price;
        }

        const double split_quantity = qty / order_split_factor;

        if (buy)
        {
            t->buy_total_count++;
            t->buy_total_quantity += split_quantity;
            t->buy_total_price += qty * price;
            t->buy_total_price_rated += qty * price * order_exchange_rate;
            t->average_quantity += split_quantity;
        }
        else if (sell)
        {
            t->sell_total_count++;
            t->sell_total_quantity += split_quantity;
            t->sell_total_price += qty * price;
            t->sell_total_price_rated += qty * price * order_exchange_rate;
            t->average_quantity -= split_quantity;
        }
        else
        {
            FOUNDATION_ASSERT_FAIL("Invalid order type");
        }

        if (math_real_is_zero(t->average_quantity))
        {
            if (total_current_quantity > 0)
            {
                t->total_gain += t->sell_total_price_rated - t->buy_total_price_rated;

                // Reset buy and sell stats
                t->buy_total_quantity = 0;
                t->buy_total_price = 0;
                t->buy_total_price_rated = 0;

                t->sell_total_quantity = 0;
                t->sell_total_price = 0;
                t->sell_total_price_rated = 0;
            }
        }
    }

    // Compute dividends
    t->total_dividends = 0;
    for (auto dividends : data["dividends"])
    {
        double xgrate = dividends["xcg"].as_number();
        if (stock_currency.length && math_real_is_nan(xgrate))
        {
            time_t date = dividends["date"].as_time();
            xgrate = stock_exchange_rate(STRING_ARGS(stock_currency), STRING_ARGS(preferred_currency), date);
            config_set(dividends, "xcg", xgrate);
        }
        t->total_dividends += dividends["amount"].as_number(0) * math_ifnan(xgrate, 1.0);
    }

    FOUNDATION_ASSERT(t->average_quantity >= 0);

    if (total_current_quantity == 0)
        t->average_quantity = 0;
    
    // Update the average price
    t->average_exchange_rate = total_exchange_rate_count > 0 ? total_exchange_rate / total_exchange_rate_count : 0;

    if (t->average_quantity > 0)
    {
        t->average_price = math_ifnan((t->buy_total_price - t->sell_total_price) / t->average_quantity, 0.0);
        t->average_price_rated = math_ifnan((t->buy_total_price_rated - t->sell_total_price_rated) / t->average_quantity, 0.0);
    }
    else
    {
        t->average_price = 0;
        t->average_price_rated = 0;
    }
    
    if (valid_dates > 0)
    {
        t->date_average /= valid_dates;
        t->elapsed_days = time_elapsed_days(t->date_min, t->average_quantity == 0 ? t->date_max : time_now());
    }

    if (total_ask_count > 0)
    {
        if (t->average_quantity == 0)
            t->average_price = total_buy_limit_price / total_ask_count;
        t->average_ask_price = total_ask_price / total_ask_count;
    }

    // Reset any deferred computed values
    t->average_days_held.reset();
    t->ps.reset([t](double& value){ return title_fetch_ps(t, value); });
    t->ask_price.reset([t](double& value){ return title_fetch_ask_price(t, value); });
    t->today_exchange_rate.reset([t](double& value){ return title_fetch_today_exchange_rate(t, value); });
}

bool title_refresh(title_t* title)
{
    const stock_t* s = title->stock;
    if (s == nullptr)
        return false;

    title_init(title, title->wallet, title->data);
    return true;
}

bool title_active(const title_t* t)
{
    FOUNDATION_ASSERT(t);
    return t->average_quantity > 0;
}

bool title_is_resolved(const title_t* t)
{
    const stock_t* s = t->stock;
    if (s == nullptr)
        return false;
        
    return s->has_resolve(title_minimum_fetch_level(t));
}

fetch_level_t title_minimum_fetch_level(const title_t* t)
{
    if (title_is_index(t))
        return INDEX_MINIMUM_FETCH_LEVEL;
    return TITLE_MINIMUM_FETCH_LEVEL;
}

bool title_update(title_t* t, double timeout /*= 3.0*/)
{	    
    bool resolved = stock_update(t->code, t->code_length, t->stock, title_minimum_fetch_level(t), timeout);
    if (!resolved)
        return false;
    
    const stock_t* stock_data = t->stock;
    if (stock_data == nullptr)
        return false;

    return true;
}

bool title_is_index(const title_t* t)
{
    if (t == nullptr)
        return false;

    const stock_t* s = t->stock;
    if (s == nullptr || s->exchange == 0)
    {
        return string_ends_with(t->code, t->code_length, STRING_CONST(".INDX")) ||
               string_ends_with(t->code, t->code_length, STRING_CONST(".FOREX"));
    }

    string_const_t exchange = string_table_decode_const(t->stock->exchange);
    return string_equal(STRING_ARGS(exchange), STRING_CONST("INDX")) ||
           string_equal(STRING_ARGS(exchange), STRING_CONST("FOREX"));
}

bool title_has_increased(const title_t* t, double* out_delta /*= nullptr*/, double since_seconds /*= 15.0 * 60.0*/, double* elapsed_seconds /*= nullptr*/)
{
    double delta = DNAN;
    if (!title_recently_changed(t, &delta, since_seconds, elapsed_seconds))
        return false;
    return delta > 0;
}

bool title_has_decreased(const title_t* t, double* out_delta /*= nullptr*/, double since_seconds /*= 15.0 * 60.0*/, double* elapsed_seconds /*= nullptr*/)
{
    double delta = DNAN;
    if (!title_recently_changed(t, &delta, since_seconds, elapsed_seconds))
        return false;
    return delta < 0;
}

title_t* title_allocate(wallet_t* wallet /*= nullptr*/, const config_handle_t& data /*= nullptr*/)
{
    title_t* new_title = MEM_NEW(HASH_TITLE, title_t);
    if (wallet || data)
        title_init(new_title, wallet, data);
    return new_title;
}

void title_deallocate(title_t*& title)
{
    MEM_DELETE(title);
}

time_t title_last_transaction_date(const title_t* t)
{
    time_t last_date = 0;
    for (auto corder : t->data["orders"])
    {
        string_const_t date_str = corder["date"].as_string();
        if (date_str.length == 0)
            continue;
        const time_t odate = string_to_date(STRING_ARGS(date_str));
        if (odate > last_date)
            last_date = odate;
    }

    return last_date;
}

time_t title_first_transaction_date(const title_t* t)
{
    time_t first_date = INT64_MAX;
    for (auto corder : t->data["orders"])
    {
        string_const_t date_str = corder["date"].as_string();
        if (date_str.length == 0)
            continue;
        const time_t odate = string_to_date(STRING_ARGS(date_str));
        if (odate < first_date)
            first_date = odate;
    }

    return first_date;
}

bool title_sold(const title_t* t)
{
    return t->sell_total_quantity > 0 && t->average_quantity == 0;
}

bool title_has_transactions(const title_t* t)
{
    return (t->buy_total_quantity > 0 || t->sell_total_quantity > 0);
}

double title_get_bought_price(const title_t* t)
{
    return math_ifzero(t->buy_total_price / t->buy_total_quantity, t->average_price);
}

double title_get_sell_gain_rated(const title_t* t, bool only_if_completly_sold)
{
    if (t->sell_total_quantity <= 0)
        return 0.0;

    if (only_if_completly_sold && t->average_quantity > 0) 
        return 0.0;

    const double buy_average_price = t->buy_total_price_rated / t->buy_total_quantity;
    const double sell_average_price = t->sell_total_price_rated / t->sell_total_quantity;
    const double gain = (sell_average_price - buy_average_price) * t->sell_total_quantity;
    return gain;
}

double title_get_ask_price(const title_t* title)
{
    if (title_sold(title))
        return title->sell_total_price / title->sell_total_quantity;
    
    return title->ask_price.fetch();
}

double title_current_price(const title_t* title)
{
    if (title == nullptr)
        return DNAN;

    const stock_t* stock = title->stock;
    if (stock == nullptr)
        return DNAN;

    return stock->current.price;
}

double title_average_days_held(const title_t* title)
{
    if (title->average_quantity == 0)
        return title->elapsed_days;

    if (title_sold(title) || title_is_index(title))
        return DNAN;

    double average_days_held = 0;
    if (title->average_days_held.try_get(average_days_held))
        return average_days_held;

    auto orders = title->data["orders"];
    double buy_total_price = title->buy_total_price;

    // Compute in average how many days the title is help.
    // We weight each transaction total price by the title total transaction price.
    double current_quantity = 0;
    for (auto e : orders)
    {
        const bool buy = e["buy"].as_boolean();
        const bool sell = e["sell"].as_boolean();
        const double qty = e["qty"].as_number();

        if (buy)
        {
            const time_t order_date = e["date"].as_time();
            const double price = e["price"].as_number();
            const double total_price = qty * price;

            const double ratio = total_price / buy_total_price;
            average_days_held += order_date * ratio;

            current_quantity += qty;
        }
        else if (sell)
        {
            current_quantity -= qty;
        }

        if (current_quantity == 0 && title->average_quantity > 0)
        {
            average_days_held = 0;
        }
    }

    title->average_days_held = time_elapsed_days(average_days_held, time_now());
    return title->average_days_held.fetch();
}

double title_sell_gain_if_kept(const title_t* t)
{
    const double current_value = math_ifnan(t->stock->current.price, 0.0) * t->sell_total_quantity;
    const double average_sell_price = t->sell_total_price / t->sell_total_quantity;
    return current_value - t->sell_total_price;
}
