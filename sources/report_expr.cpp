/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */

#include "report.h"

#include "eod.h"
#include "title.h"

#include <framework/expr.h>
#include <framework/module.h>
#include <framework/dispatcher.h>
#include <framework/array.h>

#define HASH_REPORT_EXPRESSION static_hash_string("report_expr", 11, 0x44456b54e62624e0ULL)

#define IS_NOT_A_NUMBER SC1(math_real_is_nan(_1.as_number()))
#define EVAL_STOCK_FIELD(field_name) FOUNDATION_PREPROCESSOR_TOSTRING(field_name), SC2(_2->field_name), IS_NOT_A_NUMBER, FetchLevel::FUNDAMENTALS

constexpr uint32_t STOCK_ONLY_PROPERTY_EVALUATOR_START_INDEX = 36;

FOUNDATION_FORCEINLINE expr_result_t stock_change_p_range(const stock_t* s, int rel_days)
{
    string_const_t code = SYMBOL_CONST(s->code);
    const double price = s->current.adjusted_close;
    const double price_rel = stock_get_eod(STRING_ARGS(code), time_add_days(time_now(), -rel_days)).adjusted_close;
    return expr_result_t((price - price_rel) / price_rel * 100.0);
}

static struct {
    const char* property_name;
    function<expr_result_t(const title_t* t, const stock_t* s)> handler;
    function<bool(const expr_result_t& v)> filter_out;
    FetchLevel required_level{ FetchLevel::NONE };
} report_field_property_evalutors[] = {
    
    // Title & Stocks
    
    { "sold",    SC2(_1->average_quantity ? false : true), SL1(_1.as_number() == 0)},                       /*0*/
    { "active", SC2(_1->average_quantity ? true : false), SL1(_1.as_number() == 0) },
    { "qty",    SC2(_1->average_quantity), SL1(_1.as_number() == 0 || math_real_is_nan(_1.as_number())) },
    { "buy",    SC2(_1->buy_adjusted_price), IS_NOT_A_NUMBER },
    { "day",    SC2(title_get_day_change(_1, _2)), IS_NOT_A_NUMBER },

    { "buy_total_adjusted_qty",     SC2(_1->buy_total_adjusted_qty), IS_NOT_A_NUMBER },                    /*5*/
    { "buy_total_adjusted_price",   SC2(_1->buy_total_adjusted_price), IS_NOT_A_NUMBER },
    { "sell_total_adjusted_qty",    SC2(_1->sell_total_adjusted_qty), IS_NOT_A_NUMBER },
    { "sell_total_adjusted_price",  SC2(_1->sell_total_adjusted_price), IS_NOT_A_NUMBER },

    { "buy_total_price",    SC2(_1->buy_total_price), IS_NOT_A_NUMBER },                                    /*9*/
    { "buy_total_quantity", SC2(_1->buy_total_quantity), IS_NOT_A_NUMBER },

    { "sell_total_price",       SC2(_1->sell_total_price), IS_NOT_A_NUMBER },                               /*11*/
    { "sell_total_quantity",    SC2(_1->sell_total_quantity), IS_NOT_A_NUMBER },

    { "buy_total_price_rated_adjusted",     SC2(_1->buy_total_price_rated_adjusted), IS_NOT_A_NUMBER },     /*13*/
    { "sell_total_price_rated_adjusted",    SC2(_1->sell_total_price_rated_adjusted), IS_NOT_A_NUMBER },

    { "buy_total_price_rated",  SC2(_1->buy_total_price_rated), IS_NOT_A_NUMBER },                          /*15*/
    { "sell_total_price_rated", SC2(_1->sell_total_price_rated), IS_NOT_A_NUMBER },

    { "buy_adjusted_price",     SC2(_1->buy_adjusted_price), IS_NOT_A_NUMBER },                             /*17*/
    { "sell_adjusted_price",    SC2(_1->sell_adjusted_price), IS_NOT_A_NUMBER },

    { "average_price",              SC2(_1->average_price), IS_NOT_A_NUMBER },                              /*19*/
    { "average_price_rated",        SC2(_1->average_price_rated), IS_NOT_A_NUMBER },
    { "average_quantity",           SC2(_1->average_quantity), IS_NOT_A_NUMBER },
    { "average_buy_price",          SC2(_1->average_buy_price), IS_NOT_A_NUMBER },
    { "average_buy_price_rated",    SC2(_1->average_buy_price_rated), IS_NOT_A_NUMBER },
    { "remaining_shares",           SC2(_1->remaining_shares), IS_NOT_A_NUMBER },
    { "total_dividends",            SC2(_1->total_dividends), IS_NOT_A_NUMBER },
    { "average_ask_price",          SC2(_1->average_ask_price), IS_NOT_A_NUMBER },
    { "average_exchange_rate",      SC2(_1->average_exchange_rate), IS_NOT_A_NUMBER },

    { "date_min",       SC2((double)_1->date_min), IS_NOT_A_NUMBER },                                       /*28*/
    { "date_max",       SC2((double)_1->date_max), IS_NOT_A_NUMBER },
    { "date_average",   SC2((double)_1->date_average), IS_NOT_A_NUMBER},

    { "title",                  SC2(_1->code), SL1(_1.index == 0) },                                        /*31*/
    { "ps",                     SC2(_1->ps.fetch()), IS_NOT_A_NUMBER },                                 
    { "ask",                    SC2(_1->ask_price.fetch()), nullptr },
    { "today_exchange_rate",    SC2(_1->today_exchange_rate.fetch()), nullptr },

    { "gain",    SC2(title_get_total_gain(_1)), nullptr},

    //Stock only (Start at index 36 <== !!!UPDATE INDEX #STOCK_ONLY_PROPERTY_EVALUATOR_START_INDEX IF YOU ADD NEW EVALUATOR ABOVE!!!)

    { "price",      SC2(_2->current.price), IS_NOT_A_NUMBER, FetchLevel::REALTIME },                       /*36*/
    { "date",       SC2((double)_2->current.date), nullptr, FetchLevel::REALTIME },
    { "gmt",        SC2((double)_2->current.gmtoffset), nullptr, FetchLevel::REALTIME },
    { "open",       SC2(_2->current.open), IS_NOT_A_NUMBER, FetchLevel::REALTIME },
    { "close",      SC2(_2->current.adjusted_close), IS_NOT_A_NUMBER, FetchLevel::REALTIME },
    { "yesterday",  SC2(_2->current.previous_close), nullptr, FetchLevel::REALTIME },
    { "low",        SC2(_2->current.low), nullptr, FetchLevel::REALTIME },
    { "high",       SC2(_2->current.high), nullptr, FetchLevel::REALTIME },
    { "change",     SC2(_2->current.change), IS_NOT_A_NUMBER, FetchLevel::REALTIME },
    { "change_p",   SC2(_2->current.change_p), IS_NOT_A_NUMBER, FetchLevel::REALTIME },
    { "volume",     SC2(_2->current.volume), nullptr, FetchLevel::REALTIME },

    { "change_3d",     SC2(stock_change_p_range(_2, 3)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_5d",     SC2(stock_change_p_range(_2, 5)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_1w",     SC2(stock_change_p_range(_2, 7)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_2w",     SC2(stock_change_p_range(_2, 14)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_4w",     SC2(stock_change_p_range(_2, 28)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_1m",     SC2(stock_change_p_range(_2, 30)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_2m",     SC2(stock_change_p_range(_2, 30 * 2)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_3m",     SC2(stock_change_p_range(_2, 30 * 3)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_6m",     SC2(stock_change_p_range(_2, 30 * 6)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_1y",     SC2(stock_change_p_range(_2, 365)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_2y",     SC2(stock_change_p_range(_2, 365 * 2)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_3y",     SC2(stock_change_p_range(_2, 365 * 3)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_6y",     SC2(stock_change_p_range(_2, 365 * 6)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_10y",     SC2(stock_change_p_range(_2, 365 * 10)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},
    { "change_max",     SC2(stock_change_p_range(_2, 365 * 100)), IS_NOT_A_NUMBER, FetchLevel::REALTIME | FetchLevel::EOD},

    { "price_factor",   SC2(_2->current.price_factor), nullptr, FetchLevel::EOD },
    { "change_p_high",  SC2(_2->current.change_p_high), nullptr, FetchLevel::EOD },

    { "wma",    SC2(_2->current.wma), nullptr, FetchLevel::TECHNICAL_WMA },
    { "ema",    SC2(_2->current.ema), nullptr, FetchLevel::TECHNICAL_EMA },
    { "sma",    SC2(_2->current.sma), nullptr, FetchLevel::TECHNICAL_SMA },
    { "uband",  SC2(_2->current.uband), nullptr, FetchLevel::TECHNICAL_BBANDS },
    { "mband",  SC2(_2->current.mband), nullptr, FetchLevel::TECHNICAL_BBANDS },
    { "lband",  SC2(_2->current.lband), nullptr, FetchLevel::TECHNICAL_BBANDS },
    { "sar",    SC2(_2->current.sar), nullptr, FetchLevel::TECHNICAL_SAR },
    { "slope",  SC2(_2->current.slope), nullptr, FetchLevel::TECHNICAL_SLOPE },
    { "cci",    SC2(_2->current.cci), nullptr, FetchLevel::TECHNICAL_CCI },

    { "dividends",  SC2(_2->dividends_yield.fetch()), nullptr, FetchLevel::FUNDAMENTALS },
    { "earning_trend_actual",  SC2(_2->earning_trend_actual.fetch()), nullptr, FetchLevel::NONE },
    { "earning_trend_estimate",  SC2(_2->earning_trend_estimate.fetch()), nullptr, FetchLevel::NONE },
    { "earning_trend_difference",  SC2(_2->earning_trend_difference.fetch()), nullptr, FetchLevel::NONE },
    { "earning_trend_percent",  SC2(_2->earning_trend_percent.fetch()), nullptr, FetchLevel::NONE },

    { "name",           SC2(string_table_decode(_2->name)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "description",    SC2(string_table_decode(_2->description.fetch())), nullptr, FetchLevel::FUNDAMENTALS },
    { "country",        SC2(string_table_decode(_2->country)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "type",           SC2(string_table_decode(_2->type)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "currency",       SC2(string_table_decode(_2->currency)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "url",            SC2(string_table_decode(_2->url)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "updated_at",     SC2((double)_2->updated_at), nullptr, FetchLevel::FUNDAMENTALS },
    { "exchange",       SC2(string_table_decode(_2->exchange)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "symbol",         SC2(string_table_decode(_2->symbol)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },

    { EVAL_STOCK_FIELD(shares_count) },
    { EVAL_STOCK_FIELD(low_52) },
    { EVAL_STOCK_FIELD(high_52) },
    { EVAL_STOCK_FIELD(pe) },
    { EVAL_STOCK_FIELD(peg) },
    { EVAL_STOCK_FIELD(ws_target) },
    { EVAL_STOCK_FIELD(beta) },
    { EVAL_STOCK_FIELD(dma_50) },
    { EVAL_STOCK_FIELD(dma_200) },
    { EVAL_STOCK_FIELD(revenue_per_share_ttm) },
    { EVAL_STOCK_FIELD(trailing_pe) },
    { EVAL_STOCK_FIELD(forward_pe) },
    { EVAL_STOCK_FIELD(short_ratio) },
    { EVAL_STOCK_FIELD(short_percent) },
    { EVAL_STOCK_FIELD(profit_margin) },
    { EVAL_STOCK_FIELD(diluted_eps_ttm) }
};

static struct {
    string_const_t property_name;
    function<expr_result_t(const stock_t* s, const day_result_t* ed)> handler;
    FetchLevel required_level{ FetchLevel::NONE };
} stock_end_of_day_property_evalutors[] = {

    { CTEXT("date"),           SC2((double)_2->date), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("gmtoffset"),      SC2((double)_2->gmtoffset), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("open"),           SC2(_2->open), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("close"),          SC2(_2->adjusted_close), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("previous_close"), SC2(_2->previous_close), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("price_factor"),   SC2(_2->price_factor), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("low"),            SC2(_2->low), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("high"),           SC2(_2->high), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("change"),         SC2(_2->change), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("change_p"),       SC2(_2->change_p), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("change_p_high"),  SC2(_2->change_p_high), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("volume"),         SC2(_2->volume), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("wma"),            SC2(_2->wma), FetchLevel::REALTIME | FetchLevel::TECHNICAL_WMA },
    { CTEXT("ema"),            SC2(_2->ema), FetchLevel::REALTIME | FetchLevel::TECHNICAL_EMA },
    { CTEXT("sma"),            SC2(_2->sma), FetchLevel::REALTIME | FetchLevel::TECHNICAL_SMA },
    { CTEXT("uband"),          SC2(_2->uband), FetchLevel::REALTIME | FetchLevel::TECHNICAL_BBANDS },
    { CTEXT("mband"),          SC2(_2->mband), FetchLevel::REALTIME | FetchLevel::TECHNICAL_BBANDS },
    { CTEXT("lband"),          SC2(_2->lband), FetchLevel::REALTIME | FetchLevel::TECHNICAL_BBANDS },
    { CTEXT("sar"),            SC2(_2->sar), FetchLevel::REALTIME | FetchLevel::TECHNICAL_SAR },
    { CTEXT("slope"),          SC2(_2->slope), FetchLevel::REALTIME | FetchLevel::TECHNICAL_SLOPE },
    { CTEXT("cci"),            SC2(_2->cci), FetchLevel::REALTIME | FetchLevel::TECHNICAL_CCI }
};

// 
// # PRIVATE
//

FOUNDATION_STATIC bool report_eval_report_field_resolve_level(stock_handle_t& stock_handle, FetchLevel request_level, const double timeout_expired = 2.0)
{
    const stock_t* s = stock_handle;
    if (s == nullptr)
        return false;

    if (!s->has_resolve(request_level))
    {
        if (stock_resolve(stock_handle, request_level) >= 0)
        {
            const tick_t timeout = time_current();
            while (!s->has_resolve(request_level) && time_elapsed(timeout) < timeout_expired)
                dispatcher_wait_for_wakeup_main_thread(timeout_expired * 100);

            if (time_elapsed(timeout) >= timeout_expired)
                log_warnf(0, WARNING_PERFORMANCE, STRING_CONST("Failed to resolve %d for %s in time"), request_level, SYMBOL_CSTR(s->code));
        }
    }

    return s->has_resolve(request_level);
}

FOUNDATION_STATIC bool report_eval_report_field_resolve_level(title_t* t, FetchLevel request_level)
{
    return report_eval_report_field_resolve_level(t->stock, request_level);
}

FOUNDATION_STATIC bool report_eval_report_field_test(
    const char* property_name, stock_handle_t& stock_handle, string_const_t field_name,
    const function<expr_result_t(title_t* t, const stock_t* s)>& property_evalutor,
    const function<bool(const expr_result_t& v)>& property_filter_out,
    expr_result_t** results, FetchLevel required_level)
{
    if (!string_equal_nocase(property_name, string_length(property_name), STRING_ARGS(field_name)))
        return false;
                    
    if (required_level != FetchLevel::NONE)
        report_eval_report_field_resolve_level(stock_handle, required_level);

    const stock_t* s = stock_handle;
    expr_result_t value = property_evalutor(nullptr, s);
    if (!property_filter_out || !property_filter_out(value))
    {
        const expr_result_t& kvp = expr_eval_pair(expr_eval_symbol(s->code), value);
        array_push(*results, kvp);
    }

    return true;
}

FOUNDATION_STATIC bool report_eval_report_field_test(
    const char* property_name, report_t* report,
    string_const_t title_filter, string_const_t field_name,
    const function<expr_result_t(title_t* t, const stock_t* s)>& property_evalutor,
    const function<bool(const expr_result_t& v)>& property_filter_out,
    expr_result_t** results, FetchLevel required_level)
{
    if (!string_equal_nocase(property_name, string_length(property_name), STRING_ARGS(field_name)))
        return false;
        
    foreach (pt, report->titles)
    {
        title_t* t = *pt;
        const stock_t* s = t->stock;
        if (!s || !s->has_resolve(FetchLevel::REALTIME))
            continue;

        if (title_filter.length && !string_equal_nocase(STRING_ARGS(title_filter), t->code, t->code_length))
            continue;

        if (required_level != FetchLevel::NONE)
            report_eval_report_field_resolve_level(t, required_level);

        expr_result_t value = property_evalutor(t, s);
        if (title_filter.length || !property_filter_out || !property_filter_out(value))
        {
            const expr_result_t& kvp = expr_eval_pair(expr_eval_symbol(s->code), value);
            array_push(*results, kvp);
        }

        if (title_filter.length && string_equal_nocase(STRING_ARGS(title_filter), t->code, t->code_length))
            return true;
    }

    return true;
}

FOUNDATION_STATIC expr_result_t report_expr_eval_stock(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: S(GLF.TO, open)
    //           S(GFL.TO, close) - S(GFL.TO, open)
    //           S(GFL.TO, high, '2022-10-12')
    //           S(GFL.TO, high, 1643327732)
    //           S(U.US, close, ALL)

    if (args->len < 2 || args->len > 3)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    string_const_t code = expr_eval(args->get(0)).as_string();
    string_const_t field_name = expr_eval(args->get(1)).as_string();
    
    stock_handle_t stock_handle = stock_request(STRING_ARGS(code), FetchLevel::REALTIME);
    if (!stock_handle)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Failed to resolve stock %.*s", STRING_FORMAT(code));

    expr_result_t* results = nullptr;

    if (args->len == 2)
    {   
        FOUNDATION_ASSERT(string_equal(CTEXT("price"), string_to_const(report_field_property_evalutors[STOCK_ONLY_PROPERTY_EVALUATOR_START_INDEX].property_name)));
        // Handle default case getting latest information
        for (int i = STOCK_ONLY_PROPERTY_EVALUATOR_START_INDEX; i < ARRAY_COUNT(report_field_property_evalutors); ++i)
        {
            const auto& pe = report_field_property_evalutors[i];
            if (report_eval_report_field_test(pe.property_name, stock_handle, field_name, pe.handler, pe.filter_out, &results, pe.required_level))
                break;
        }

        if (results == nullptr)
            return NIL;

        if (array_size(results) == 1)
        {
            expr_result_t single_value = results[0];
            array_deallocate(results);
            return single_value.list[1];
        }

        return expr_eval_list(results);
    }
    else
    {
        string_const_t check_all = expr_eval(args->get(2)).as_string();
        if (string_equal_nocase(STRING_ARGS(check_all), STRING_CONST("ALL")))
        {
            // Return all end of day results for the requested field name.
            expr_result_t* results = nullptr;
            const stock_t* s = stock_handle;
            for (int i = 0; i < ARRAY_COUNT(stock_end_of_day_property_evalutors); ++i)
            {
                const auto& se = stock_end_of_day_property_evalutors[i];

                if (!string_equal_nocase(STRING_ARGS(field_name), STRING_ARGS(se.property_name)))
                    continue;

                if (!report_eval_report_field_resolve_level(stock_handle, se.required_level))
                    throw ExprError(EXPR_ERROR_EVALUATION_TIMEOUT, "Failed to resolve %s stock history data", SYMBOL_CSTR(stock_handle->code));

                array_push(results, expr_eval_pair((double)s->current.date, se.handler(s, &s->current)));
                
                // Find the closest date in the stock history
                const day_result_t* history = s->history;
                foreach(d, history)
                {
                    array_push(results, expr_eval_pair((double)d->date, se.handler(s, d)));
                }

                return expr_eval_list(results);
            }
        }
        else
        {
            // Query the stock data at a given date.        
            // First, get the date either as a string or a unix time stamp
            time_t time = 0;
            expr_result_t date_arg = expr_eval(args->get(2));
            if (date_arg.type == EXPR_RESULT_SYMBOL)
            {
                string_const_t date_string = date_arg.as_string();
                time = string_to_date(STRING_ARGS(date_string));
            }
            else
            {
                time = (time_t)date_arg.as_number(0);
            }

            if (time == 0)
                throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Failed to parse date argument `%.*s`", STRING_FORMAT(args->get(2)->token));

            const stock_t* s = stock_handle;
            for (int i = 0; i < ARRAY_COUNT(stock_end_of_day_property_evalutors); ++i)
            {
                const auto& se = stock_end_of_day_property_evalutors[i];

                if (!string_equal_nocase(STRING_ARGS(field_name), STRING_ARGS(se.property_name)))
                    continue;

                if (!report_eval_report_field_resolve_level(stock_handle, se.required_level))
                    throw ExprError(EXPR_ERROR_EVALUATION_TIMEOUT, "Failed to resolve %s stock history data", SYMBOL_CSTR(stock_handle->code));

                if (time >= s->current.date)
                    return se.handler(s, &s->current);

                // Find the closest date in the stock history
                const day_result_t* history = s->history;
                foreach(d, history)
                {
                    if (d->date <= time)
                        return se.handler(s, d);
                }

                throw ExprError(EXPR_ERROR_EVALUATION_TIMEOUT, "Failed to resolve date %ull for %s", time, SYMBOL_CSTR(stock_handle->code));
            }
        }
    }

    throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid field name %.*s", STRING_FORMAT(field_name));
}

FOUNDATION_STATIC expr_result_t report_expr_eval_stock_fundamental(const json_object_t& json)
{
    if (json.root->type == JSON_PRIMITIVE)
    {
        string_const_t s = json.as_string();

        if (s.length == 0 || string_equal_nocase(s.str, s.length, STRING_CONST("null")))
            return NIL;

        double n;
        if (string_try_convert_number(s.str, s.length, n))
            return expr_result_t(n);
        
        if (string_equal_nocase(s.str, s.length, STRING_CONST("true")))
            return expr_result_t(true);
        
        if (string_equal_nocase(s.str, s.length, STRING_CONST("false")))
            return expr_result_t(false);
        
        return expr_result_t(s);
    }

    if (json.root->type == JSON_STRING)
    {
        string_const_t s = json.as_string();
        if (string_equal_nocase(s.str, s.length, STRING_CONST("NA")))
            return NIL;
        return expr_result_t(s);
    }

    if (json.root->type == JSON_ARRAY)
    {
        expr_result_t* results = nullptr;
        for (auto e : json)
        {
            expr_result_t r = report_expr_eval_stock_fundamental(e);
            array_push(results, r);
        }

        return expr_eval_list(results);
    }

    if (json.root->type == JSON_OBJECT)
    {
        expr_result_t* results = nullptr;
        for (auto e : json)
        {
            expr_result_t* kvp = nullptr;    

            string_const_t id = e.id();
            expr_result_t r = report_expr_eval_stock_fundamental(e);
            array_push(kvp, expr_result_t(id));
            array_push(kvp, r);
            array_push(results, expr_eval_list(kvp));
        }
        return expr_eval_list(results);
    }

    return NIL;
}

FOUNDATION_STATIC expr_result_t report_expr_eval_stock_fundamental(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: F(PFE.NEO, "General.ISIN")
    //           F("CSH-UN.TO", "Highlights.WallStreetTargetPrice")
    //           F("U.US", "Technicals")

    string_const_t code = expr_eval_get_string_arg(args, 0, "Invalid symbol code");
    string_const_t field_arg = expr_eval_get_string_arg(args, 1, "Invalid field name");

    string_t field_name = string_copy(SHARED_BUFFER(256), field_arg.str, field_arg.length);
    field_name = string_replace(field_name.str, field_name.length, 256, STRING_CONST("."), STRING_CONST("::"), true);

    expr_result_t value = NIL;
    eod_fetch("fundamentals", code.str, FORMAT_JSON_CACHE, "filter", field_name.str, [&value](const json_object_t& json)
    {
        if (json.root == nullptr)
            return;

        value = report_expr_eval_stock_fundamental(json);
    }, 5 * 24 * 60ULL * 60ULL);

    return value;
}

FOUNDATION_STATIC expr_result_t report_eval_report_field(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: R('FLEX', 'ps')
    //           R('_300K', BB.TO, 'ps')
    //           R('_300K', 'buy')
    //           R('300K', PFE.NEO, transactions)
    //           R('300K', [name, description])
    //           $SINCE=90,$REPORT=FLEX,R($REPORT, [name, close, S($TITLE, close, NOW() - (60 * 60 * 24 * $SINCE))])

    string_const_t report_name = expr_eval_get_string_arg(args, 0, "Invalid report name");
    if (report_name.length < 2)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid report name %.*s", STRING_FORMAT(report_name));

    char report_name_buffer[128];
    report_name = string_to_const(string_copy(STRING_BUFFER(report_name_buffer), STRING_ARGS(report_name)));
    
    report_handle_t report_handle = report_find_no_case(STRING_ARGS(report_name));
    if (!report_handle_is_valid(report_handle))
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Cannot find report %.*s", STRING_FORMAT(report_name));

    int field_name_index = 1;
    string_const_t title_filter{};
    if (args->len >= 3)
    {
        title_filter = expr_eval_get_string_arg(args, 1, "Invalid title name");
        field_name_index = 2;
    }

    report_t* report = report_get(report_handle);
    if (!report)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Cannot find report %.*s", STRING_FORMAT(report_name));

    tick_t s = time_current();
    while (title_filter.length == 0 && !report_sync_titles(report))
    {
        if (time_elapsed(s) > 30.0f)
            throw ExprError(EXPR_ERROR_EVALUATION_TIMEOUT, "Sync timeout, retry later...", STRING_FORMAT(report_name));
        dispatcher_wait_for_wakeup_main_thread(100);
    }

    expr_result_t* results = nullptr;
    expr_t* field_expr = args->get(field_name_index);

    if (field_expr->type == OP_SET)
    {
        foreach (pt, report->titles)
        {
            title_t* t = *pt;

            if (title_is_index(t))
                continue;

            if (title_filter.length && !string_equal_nocase(STRING_ARGS(title_filter), t->code, t->code_length))
                continue;

            expr_result_t* title_results = nullptr;    
            expr_result_t title_code_expr(string_const(t->code, t->code_length));
            array_push(title_results, title_code_expr);
            for (int i = 0; i < field_expr->args.len; ++i)
            {
                expr_t* fe = field_expr->args.get(i);

                expr_set_or_create_global_var(STRING_CONST("$TITLE"), title_code_expr);
                expr_set_or_create_global_var(STRING_CONST("$REPORT"), expr_result_t(report_name));

                bool was_evaluated = false;
                expr_result_t fe_result = NIL;
                try
                {
                    fe_result = expr_eval(fe);
                }
                catch (ExprError e)
                {
                    // Consider other expression empty set errors as null values
                    if (e.code != EXPR_ERROR_EMPTY_SET)
                        throw e;
                }

                if (fe_result.type == EXPR_RESULT_SYMBOL)
                {
                    string_const_t field_name = fe_result.as_string();
                    for (int i = 0; i < ARRAY_COUNT(report_field_property_evalutors); ++i)
                    {
                        const auto& pe = report_field_property_evalutors[i];

                        if (!string_equal_nocase(STRING_ARGS(field_name), STRING_LENGTH(pe.property_name)))
                            continue;

                        if (pe.required_level != FetchLevel::NONE)
                            report_eval_report_field_resolve_level(t, pe.required_level);

                        const stock_t* s = t->stock;
                        if (s)
                        {
                            expr_result_t value = pe.handler(t, t->stock);
                            array_push(title_results, value);
                        }
                        else
                        {
                            array_push(title_results, NIL);
                        }

                        was_evaluated = true;
                    }
                }

                if (!was_evaluated)
                    array_push(title_results, fe_result);
            }

            expr_result_t title_result_list = expr_eval_list(title_results);
            array_push(results, title_result_list);
        }
    }
    else
    {
        char field_name_buffer[64];
        string_const_t field_name_temp = expr_eval(field_expr).as_string();
        string_const_t field_name = string_to_const(string_copy(STRING_BUFFER(field_name_buffer), STRING_ARGS(field_name_temp)));

        if (string_equal_nocase(STRING_ARGS(field_name), STRING_CONST("transactions")))
        {
            // Return a set of all transactions for the given title
            if (title_filter.length == 0)
                throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Title filter required for transactions");

            for (unsigned i = 0, end = array_size(report->titles); i < end; ++i)
            {
                title_t* t = report->titles[i];
                if (!string_equal_nocase(STRING_ARGS(title_filter), t->code, t->code_length))
                    continue;

                for (auto cv : t->data["orders"])
                {
                    string_const_t datestr = cv["date"].as_string();
                    string_const_t buy_or_sell = cv["buy"].as_boolean() ? CTEXT("buy") : CTEXT("sell");
                    const time_t date = string_to_date(STRING_ARGS(datestr));
                    const double qty = cv["qty"].as_number();
                    const double price = cv["price"].as_number();

                    expr_result_t* title_field_values = nullptr;
                    array_push(title_field_values, expr_result_t(datestr));
                    array_push(title_field_values, expr_result_t((double)date));
                    array_push(title_field_values, expr_result_t(buy_or_sell));
                    array_push(title_field_values, expr_result_t(qty));
                    array_push(title_field_values, expr_result_t(price));

                    array_push(results, expr_eval_list(title_field_values));
                }
            }
        }
        else
        {
            // Evaluate the field for the given title
            for (int i = 0; i < ARRAY_COUNT(report_field_property_evalutors); ++i)
            {
                const auto& pe = report_field_property_evalutors[i];
                if (report_eval_report_field_test(pe.property_name, report, title_filter, field_name, pe.handler, pe.filter_out, &results, pe.required_level))
                    break;
            }

            if (results == nullptr)
                throw ExprError(EXPR_ERROR_EVALUATION_NOT_IMPLEMENTED, "Field %.*s not supported", STRING_FORMAT(field_name));
        }
    }

    if (array_size(results) == 1)
    {
        expr_result_t single_value = results[0];
        array_deallocate(results);
        return single_value.list[1];
    }

    return expr_eval_list(results);
}

FOUNDATION_STATIC void report_eval_read_object_fields(const json_object_t& json, const json_token_t* obj, expr_result_t** field_names, const char* s = nullptr, size_t len = 0)
{
    unsigned int e = obj->child;
    for (size_t i = 0; i < obj->value_length; ++i)
    {
        const json_token_t* t = &json.tokens[e];
        e = t->sibling;
        if (t->id_length == 0)
            continue;

        char path[256];
        size_t path_length = 0;
        if (s && len > 0)
            path_length = string_format(STRING_BUFFER(path), STRING_CONST("%.*s.%.*s"), (int)len, s, (int)t->id_length, &json.buffer[t->id]).length;
        else
            path_length = string_copy(STRING_BUFFER(path), &json.buffer[t->id], t->id_length).length;

        if (t->type == JSON_OBJECT)
            report_eval_read_object_fields(json, t, field_names, path, path_length);
        else
        {
            expr_result_t ss;
            ss.type = EXPR_RESULT_SYMBOL;
            ss.value = string_table_encode(path, path_length);
            array_push(*field_names, ss);
        }
    }
}

FOUNDATION_STATIC expr_result_t report_eval_list_fields(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: FIELDS("U.US", 'real-time')
    //           FIELDS("U.US", 'fundamentals')

    const auto& code = expr_eval_get_string_arg(args, 0, "Invalid symbol code");
    const auto& api = expr_eval_get_string_arg(args, 1, "Invalid API end-point");

    expr_result_t* field_names = nullptr;
    string_const_t url = eod_build_url(api.str, code.str, FORMAT_JSON_CACHE);
    query_execute_json(url.str, FORMAT_JSON_CACHE, [&field_names](const json_object_t& json)
    {
        if (json.root == nullptr)
            return;

        if (json.root->type == JSON_OBJECT)
            report_eval_read_object_fields(json, json.root, &field_names);

        if (json.root->type == JSON_ARRAY)
            report_eval_read_object_fields(json, &json.tokens[json.root->child], &field_names);

    }, 96 * 60 * 60ULL);

    return expr_eval_list(field_names);
}

//
// # SYSTEM
//

FOUNDATION_STATIC void report_expr_initialize()
{
    expr_register_function("S", report_expr_eval_stock);
    expr_register_function("F", report_expr_eval_stock_fundamental);
    expr_register_function("R", report_eval_report_field);
    expr_register_function("FIELDS", report_eval_list_fields);
}

FOUNDATION_STATIC void report_expr_shutdown()
{
    
}

DEFINE_MODULE(REPORT_EXPRESSION, report_expr_initialize, report_expr_shutdown, MODULE_PRIORITY_MODULE);
