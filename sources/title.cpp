/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#include "title.h"

#include "eod.h"
#include "wallet.h"
 
#include <framework/math.h>
#include <framework/query.h>

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

    return title_get_total_gain_p(t, s) * (1.0 + K) - average_nq + N + R;
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

    if (t->average_quantity == 0 && t->sell_total_quantity > 0)
    {
        // Return the prediction in case the stock was kept (when sold)
        value = ((t->sell_adjusted_price - s->current.adjusted_close) / s->current.adjusted_close) * 100.0;
    }
    else
        value = title_compute_ps(t, s);

    return !math_real_is_nan(value);
}

FOUNDATION_STATIC bool title_fetch_buy_exchange_rate(const title_t* t, double& value)
{
    const stock_t* s = t->stock;
    if (s == nullptr || s->currency == 0)
        return false;

    string_const_t title_currency = string_table_decode_const(s->currency);
    value = math_ifnan(stock_exchange_rate(STRING_ARGS(title_currency), STRING_ARGS(t->wallet->preferred_currency), t->date_average), s->current.previous_close);
    return true;
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

double title_get_total_value(const title_t* t, const stock_t* s)
{
    const double adjusted_quantity = math_ifnan(t->buy_adjusted_quantity - t->sell_adjusted_quantity, t->average_quantity);
    const double total_value = 
        adjusted_quantity *
        math_ifnan(s->current.adjusted_close, s->current.previous_close) *
        math_ifnan(t->today_exchange_rate.fetch(), 1.0)
        + t->total_dividends;
    return math_ifnan(total_value, 0.0);
}

double title_get_total_investment(const title_t* t)
{
    return t->average_quantity * math_ifzero(t->average_price_rated, t->average_price * t->exchange_rate.fetch());
}

double title_get_total_gain(const title_t* t, const stock_t* s)
{
    if (t->average_quantity == 0 && t->sell_total_quantity == 0)
        return NAN;
    if (t->average_quantity == 0)
        return (t->sell_total_price - t->buy_total_price) * math_ifnan(t->today_exchange_rate.fetch(), 1.0);

    return title_get_total_value(t, s) - title_get_total_investment(t);
}

double title_get_total_gain_p(const title_t* t, const stock_t* s)
{
    if (t->average_quantity == 0 && t->sell_total_quantity == 0)
        return NAN;

    double total_investment = title_get_total_investment(t);
    if (total_investment != 0)
        return title_get_total_gain(t, s) * 100.0 / total_investment;

    if (t->buy_total_price > 0)
        return (t->sell_total_price - t->buy_total_price) / t->buy_total_price * 100.0;

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

void title_init(wallet_t* wallet, title_t* t, const config_handle_t& data)
{
    // Compute title quantity and average price
    const config_tag_t& TITLE_DATE = config_get_tag(data, STRING_CONST("date"));
    const config_tag_t& TITLE_BUY = config_get_tag(data, STRING_CONST("buy"));
    const config_tag_t& TITLE_QTY = config_get_tag(data, STRING_CONST("qty"));
    const config_tag_t& TITLE_PRICE = config_get_tag(data, STRING_CONST("price"));

    string_const_t title_code = config_name(data);
    t->data = data;
    t->wallet = wallet;
    t->code_length = string_copy(STRING_CONST_CAPACITY(t->code), STRING_ARGS(title_code)).length;
    t->buy_total_quantity = 0;
    t->buy_total_price = 0;
    t->date_average = 0;
    t->sell_total_quantity = 0;
    t->sell_total_price = 0;	
    t->average_quantity = 0;
    t->average_ask_price = 0;
    t->sell_total_price_rated = 0;
    t->buy_total_price_rated = 0;
    t->total_dividends = 0;

    double total_ask_price = 0;
    double total_ask_count = 0;
    double total_buy_limit_price = 0;
    
    double buy_single_count = 0;
    double buy_single_total = 0;

    double buy_total_adjusted_qty = 0;
    double buy_total_adjusted_price = 0;
    double sell_total_adjusted_qty = 0;
    double sell_total_adjusted_price = 0;

    int valid_dates = 0;
    const stock_t* s = t->stock;
    for (auto order : data["orders"])
    {
        const bool buy = order[TITLE_BUY].as_boolean();
        const double qty = order[TITLE_QTY].as_number();
        const double price = order[TITLE_PRICE].as_number();
        string_const_t date = order[TITLE_DATE].as_string();
        time_t order_date = string_to_date(STRING_ARGS(date));

        double dividends_yield = 0;
        double exchange_rate = 1.0;
        const day_result_t* order_day_results = nullptr;
        if (s)
        {
            order_day_results = stock_get_EOD(s, order_date, true);
            string_const_t stock_currency = string_table_decode_const(s->currency);
            exchange_rate = math_ifzero(stock_exchange_rate(STRING_ARGS(stock_currency), STRING_ARGS(t->wallet->preferred_currency), order_date), exchange_rate);
            dividends_yield = s->dividends_yield;
        }

        if (order_date != 0)
        {
            if (t->date_min == 0 || t->date_min > order_date)
                t->date_min = order_date;

            if (t->date_max == 0 || t->date_max < order_date)
                t->date_max = order_date;

            t->date_average += order_date;
            valid_dates++;
        }

        if (buy)
        {
            buy_single_count++;
            buy_single_total += price;
            t->buy_total_quantity += qty;
            t->buy_total_price += qty * price;
            t->buy_total_price_rated += qty * price * exchange_rate;

            double ask_price = order["ask"].as_number();
            if (ask_price > 0)
            {
                total_buy_limit_price += price;
                total_ask_price += ask_price;
                total_ask_count++;
            }

            if (order_day_results)
            {
                double factor = math_ifnan(order_day_results->price_factor, 1.0);
                buy_total_adjusted_qty += qty / factor;
                buy_total_adjusted_price += (qty / factor) * price * factor;
            }

            // FIXME: Change how dividends are computed over time
            t->total_dividends += (qty * price) * time_elapsed_days(order_date, time_now()) / 365.0 * dividends_yield * exchange_rate;
        }
        else
        {
            t->sell_total_quantity += qty;
            t->sell_total_price += qty * price;
            t->sell_total_price_rated += qty * price * exchange_rate;

            if (order_day_results)
            {
                double factor = math_ifnan(order_day_results->price_factor, 1.0);
                sell_total_adjusted_qty += qty / factor;
                sell_total_adjusted_price += (qty / factor) * price * factor;
            }

            t->total_dividends -= (qty * price) * time_elapsed_days(order_date, time_now()) / 365.0 * dividends_yield * exchange_rate;
            if (t->total_dividends < 0)
                t->total_dividends = 0;
        }
    }

    if (sell_total_adjusted_price > 0)
    {
        t->sell_adjusted_quantity = (double)math_ceil(sell_total_adjusted_qty);
        t->sell_adjusted_price = sell_total_adjusted_price / sell_total_adjusted_qty;
    }
    else if (buy_total_adjusted_qty > 0)
    {
        t->sell_adjusted_price = 0;
        t->sell_adjusted_quantity = 0;
    }

    if (buy_total_adjusted_qty > 0)
    {
        t->buy_adjusted_quantity = (double)math_ceil(buy_total_adjusted_qty);
        t->buy_adjusted_price = buy_total_adjusted_price / buy_total_adjusted_qty;
    }

    if ((buy_total_adjusted_qty - sell_total_adjusted_qty) > 0)
    {
        double total_average_price = buy_total_adjusted_price / buy_total_adjusted_qty;
        t->average_quantity = buy_total_adjusted_qty - sell_total_adjusted_qty;
        t->average_price = (buy_total_adjusted_price - (sell_total_adjusted_qty * total_average_price)) / t->average_quantity;
    }
    else if ((t->buy_total_quantity - t->sell_total_quantity) > 0)
    {
        double total_average_price = t->buy_total_price / t->buy_total_quantity;
        t->average_quantity = t->buy_total_quantity - t->sell_total_quantity;
        t->average_price = (t->buy_total_price - (t->sell_total_quantity * total_average_price)) / t->average_quantity;
    }
    else if (buy_single_count > 0 && t->buy_total_quantity == 0)
    {
        t->average_price = buy_single_total / buy_single_count;
    }

    if ((t->buy_total_quantity - t->sell_total_quantity) <= 0)
        t->average_quantity = 0;

    if (valid_dates > 0)
    {
        t->date_average /= valid_dates;
        t->elapsed_days = time_elapsed_days(t->date_min, t->average_quantity == 0 ? t->date_max : time_now());
    }

    if (t->average_quantity > 0)
        t->average_price_rated = (t->buy_total_price_rated - t->sell_total_price_rated) / t->average_quantity;

    if (total_ask_count > 0)
    {
        if (t->average_quantity == 0)
            t->average_price = total_buy_limit_price / total_ask_count;
        t->average_ask_price = total_ask_price / total_ask_count;
    }

    t->ps.reset([t](double& value){ return title_fetch_ps(t, value); });
    t->ask_price.reset([t](double& value){ return title_fetch_ask_price(t, value); });
    t->exchange_rate.reset([t](double& value) { return title_fetch_buy_exchange_rate(t, value); });
    t->today_exchange_rate.reset([t](double& value){ return title_fetch_today_exchange_rate(t, value); });
}

bool title_refresh(title_t* title)
{
    const stock_t* s = title->stock;
    if (s == nullptr)
        return false;

    title_init(title->wallet, title, title->data);
    return true;
}

bool title_update(title_t* t, double timeout /*= 3.0*/)
{	    
    bool resolved = stock_update(t->code, t->code_length, t->stock, TITLE_MINIMUM_FETCH_LEVEL, timeout);
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
        return false;
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

title_t* title_allocate()
{
    return MEM_NEW(HASH_TITLE, title_t);
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
