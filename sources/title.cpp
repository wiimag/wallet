/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#include "title.h"

#include "eod.h"
#include "wallet.h"
 
#include <framework/math.h>
#include <framework/query.h>
#include <framework/string.h>

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
        return NAN;

    const double profit_ask = t->wallet->profit_ask;
    const double average_days = t->wallet->average_days;
    const double target_ask = t->wallet->target_ask;

    double M = 0;
    double average_fg = (t->average_price + s->current.adjusted_close) / 2.0;
    if (t->elapsed_days >= 30)
        M = max(t->average_price, average_fg) * (1.0 + profit_ask - ((t->elapsed_days - average_days) / 20 / 100));
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

    // Handle cases where the stock has been dismissed from the market.
    if (math_real_is_nan(s->current.adjusted_close))
    {
        value = DNAN;
        return true;
    }

    if (title_sold(t))
    {
        // Return the prediction in case the stock was kept (when sold)
        value = ((t->sell_adjusted_price - s->current.adjusted_close) / s->current.adjusted_close) * 100.0;
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

    const double average_fg = (t->average_price + s->current.adjusted_close) / 2.0;
    if (t->elapsed_days > 30)
    {
        unsigned samples = 0;
        double sampling_average_fg = 0.0f;
        unsigned max_samping_days = math_floor(t->elapsed_days / 2.0f);
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

        value = sampling_average_fg * (1.0 + profit_ask - (t->elapsed_days - average_days) / 20.0 / 100.0);
    }
    else
    {
        value = max(t->average_price, average_fg) * max(1 + max(t->wallet->main_target, profit_ask), 1 + target_ask);
    }

    return !math_real_is_nan(value);
}

//
// # PUBLIC API
//

double title_get_total_value(const title_t* t)
{
    if (title_sold(t))
        return t->sell_total_price_rated_adjusted;

    const stock_t* s = t->stock;
    if (s && s->has_resolve(FetchLevel::REALTIME))
    {
        const double total_value = t->total_dividends + t->average_quantity *
                s->current.adjusted_close * t->today_exchange_rate.fetch();
        return total_value;
    }

    return t->total_dividends + t->average_quantity * t->average_price * t->average_exchange_rate;
}

double title_get_total_investment(const title_t* t)
{
    if (title_sold(t))
        return t->buy_total_price_rated_adjusted;
    return t->average_quantity * t->average_price_rated;
}

double title_get_total_gain(const title_t* t)
{
    if (t->average_quantity == 0 && t->sell_total_quantity == 0)
        return NAN;
    if (t->average_quantity == 0)
        return (t->sell_total_price_rated - t->buy_total_price_rated);

    return title_get_total_value(t) - title_get_total_investment(t);
}

double title_get_total_gain_p(const title_t* t)
{
    if (t->average_quantity == 0 && t->sell_total_quantity == 0)
        return NAN;

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
        return NAN;
    return s->current.change * t->average_quantity * t->today_exchange_rate.fetch();
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
    const config_tag_t TITLE_DATE = config_get_tag(data, STRING_CONST("date"));
    const config_tag_t TITLE_BUY = config_get_tag(data, STRING_CONST("buy"));
    const config_tag_t TITLE_QTY = config_get_tag(data, STRING_CONST("qty"));
    const config_tag_t TITLE_PRICE = config_get_tag(data, STRING_CONST("price"));
    const config_tag_t TITLE_ASK_PRICE = config_get_tag(data, STRING_CONST("ask"));

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

    t->buy_total_price_rated_adjusted = 0;
    t->sell_total_price_rated_adjusted = 0;

    t->buy_total_adjusted_qty = 0;
    t->buy_total_adjusted_price = 0;
    t->sell_total_adjusted_qty = 0;
    t->sell_total_adjusted_price = 0;

    t->buy_adjusted_price = 0;
    t->sell_adjusted_price = 0;

    t->average_price = 0;
    t->average_quantity = 0;
    t->average_price_rated = 0;

    t->total_dividends = 0;
    t->average_ask_price = 0;
    t->average_exchange_rate = 1;

    string_const_t title_code = config_name(data);
    t->code_length = string_copy(STRING_BUFFER(t->code), STRING_ARGS(title_code)).length;

    // Initiate to resolve the title stock right away in case it has never been done before.
    if (main_is_interactive_mode(true) && t->wallet && t->wallet->track_history)
    {
        const auto fetch_level = title_minimum_fetch_level(t);
        if (!t->stock)
            t->stock = stock_request(t->code, t->code_length, fetch_level);
    }

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

    for (auto order : data["orders"])
    {
        const bool buy = order[TITLE_BUY].as_boolean();
        const double qty = order[TITLE_QTY].as_number();
        const double price = order[TITLE_PRICE].as_number();
        const double ask_price = order[TITLE_ASK_PRICE].as_number();
        string_const_t date = order[TITLE_DATE].as_string();
        const time_t order_date = string_to_date(STRING_ARGS(date));

        double order_split_factor = 1.0;
        double order_exchange_rate = 1.0;
        const day_result_t* order_day_results = nullptr;
        if (s)
        {
            order_day_results = stock_get_EOD(s, order_date, true);
            order_exchange_rate = stock_exchange_rate(STRING_ARGS(stock_currency), STRING_ARGS(preferred_currency), order_date);
            order_exchange_rate = math_ifzero(order_exchange_rate, 1.0);
            order_split_factor = stock_get_split_factor(STRING_ARGS(title_code), order_date);
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
        const double adjusted_factor = order_day_results ? math_ifnan(order_day_results->price_factor, 1.0) : 1.0;
        const double split_adjusted_factor = order_split_factor / adjusted_factor;

        if (buy)
        {
            t->buy_total_count++;            
            t->buy_total_quantity += qty;
            t->buy_total_price += qty * price;
            t->buy_total_price_rated += qty * price * order_exchange_rate;

            // FIXME: Change how dividends are computed over time
            const double order_dividend_yielded = (qty * price) * time_elapsed_days(order_date, time_now()) / 365.0 * dividends_yield * order_exchange_rate;
            t->total_dividends += order_dividend_yielded;
            
            const double adjusted_buy_cost = (qty * price / split_adjusted_factor);
            t->buy_total_adjusted_qty += split_quantity;
            t->buy_total_adjusted_price += adjusted_buy_cost;
            t->buy_total_price_rated_adjusted += adjusted_buy_cost * order_exchange_rate;
        }
        else
        {
            t->sell_total_count++;
            t->sell_total_quantity += qty;
            t->sell_total_price += qty * price;
            t->sell_total_price_rated += qty * price * order_exchange_rate;

            t->total_dividends -= (qty * price) * time_elapsed_days(order_date, time_now()) / 365.0 * dividends_yield * order_exchange_rate;
            if (t->total_dividends < 0)
                t->total_dividends = 0;
                
            const double adjusted_sell_cost = qty * price;
            t->sell_total_adjusted_qty += split_quantity;
            t->sell_total_adjusted_price += adjusted_sell_cost;
            t->sell_total_price_rated_adjusted += adjusted_sell_cost * order_exchange_rate;
        }
    }

    t->remaining_shares = t->buy_total_quantity - t->sell_total_quantity; // not adjusted
    
    t->average_exchange_rate = total_exchange_rate_count > 0 ? total_exchange_rate / total_exchange_rate_count : 0;
    
    t->buy_adjusted_price = t->buy_total_adjusted_qty > 0 ? t->buy_total_adjusted_price / t->buy_total_adjusted_qty : 0;
    t->sell_adjusted_price = t->sell_total_adjusted_qty > 0 ? t->sell_total_adjusted_price / t->sell_total_adjusted_qty : 0;

    t->average_buy_price = math_ifnan(t->buy_total_adjusted_price / t->buy_total_quantity, 0);
    t->average_quantity = (double)math_round(math_ifzero(t->buy_total_adjusted_qty - t->sell_total_adjusted_qty, t->remaining_shares));
    t->average_price = t->average_quantity > 0 ? math_ifzero(
        (t->buy_total_adjusted_price - t->sell_total_adjusted_price) / t->average_quantity, 
        (t->buy_total_price - t->sell_total_price) / t->remaining_shares
    ) : t->average_buy_price;

    t->average_buy_price_rated = math_ifnan(t->buy_total_price_rated_adjusted / t->buy_total_quantity, 0);
    t->average_price_rated = t->average_quantity > 0 ? math_ifzero(
        (t->buy_total_price_rated_adjusted - t->sell_total_price_rated_adjusted) / t->average_quantity,
        (t->buy_total_price_rated - t->sell_total_price_rated) / t->remaining_shares
    ) : t->average_buy_price_rated;

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

    t->ps.reset([t](double& value){ return title_fetch_ps(t, value); });
    t->ask_price.reset([t](double& value){ return title_fetch_ask_price(t, value); });
    t->today_exchange_rate.reset([t](double& value){ return title_fetch_today_exchange_rate(t, value); });

    FOUNDATION_ASSERT(math_real_is_finite(t->sell_total_adjusted_qty));
}

bool title_refresh(title_t* title)
{
    const stock_t* s = title->stock;
    if (s == nullptr)
        return false;

    title_init(title, title->wallet, title->data);
    return true;
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
        return string_ends_with(t->code, t->code_length, STRING_CONST(".INDX"));
    string_const_t exchange = string_table_decode_const(t->stock->exchange);
    return string_equal(STRING_ARGS(exchange), STRING_CONST("INDX"));
}

bool title_has_increased(const title_t* t, double* out_delta /*= nullptr*/, double since_seconds /*= 15.0 * 60.0*/, double* elapsed_seconds /*= nullptr*/)
{
    double delta = NAN;
    if (!title_recently_changed(t, &delta, since_seconds, elapsed_seconds))
        return false;
    return delta > 0;
}

bool title_has_decreased(const title_t* t, double* out_delta /*= nullptr*/, double since_seconds /*= 15.0 * 60.0*/, double* elapsed_seconds /*= nullptr*/)
{
    double delta = NAN;
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

time_t title_get_last_transaction_date(const title_t* t, time_t* out_date /*= nullptr*/)
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

    if (out_date && last_date != 0)
        *out_date = last_date;

    return last_date;
}

time_t title_get_first_transaction_date(const title_t* t, time_t* out_date /*= nullptr*/)
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

    if (out_date&& first_date != INT64_MAX)
        *out_date = first_date;

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

double title_get_sell_gain_rated(const title_t* t)
{
    return t->sell_total_price_rated - ((t->buy_total_price_rated / t->buy_total_quantity) * t->sell_total_quantity);
}
