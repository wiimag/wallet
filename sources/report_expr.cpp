/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "report.h"

#include "app.h"
#include "eod.h"
#include "stock.h"
#include "title.h"

#include <framework/expr.h>
#include <framework/service.h>
#include <framework/table.h>
#include <framework/dispatcher.h>
#include <framework/string.h>

#include <foundation/thread.h>

struct dynamic_table_column_t
{
    string_t name;
    string_t expression;
    expr_t* ee;
    int value_index;
    column_format_t format{ COLUMN_FORMAT_TEXT };
};

typedef enum DynamicTableValueType {
    DYNAMIC_TABLE_VALUE_NULL = 0,
    DYNAMIC_TABLE_VALUE_TRUE = 1,
    DYNAMIC_TABLE_VALUE_FALSE = 2,
    DYNAMIC_TABLE_VALUE_NUMBER = 3,
    DYNAMIC_TABLE_VALUE_TEXT = 4
} dynamic_table_record_value_type_t;


struct dynamic_table_record_value_t
{
    dynamic_table_record_value_type_t type;
    union {
        string_t text;
        double number;
    };
};

struct dynamic_table_record_t
{
    expr_result_t* values{ nullptr };
    dynamic_table_record_value_t* resolved{ nullptr };
};

struct dynamic_report_t
{
    string_t name;
    dynamic_table_column_t* columns;
    dynamic_table_record_t* records;
    table_t* table;
};

#define HASH_REPORT_EXPRESSION static_hash_string("report_expr", 11, 0x44456b54e62624e0ULL)

#define IS_NOT_A_NUMBER SL1(math_real_is_nan(_1.as_number()))
#define EVAL_STOCK_FIELD(field_name) FOUNDATION_PREPROCESSOR_TOSTRING(field_name), SL2(_2->field_name), IS_NOT_A_NUMBER, FetchLevel::FUNDAMENTALS

static struct {
    const char* property_name;
    function<expr_result_t(title_t* t, const stock_t* s)> handler;
    function<bool(const expr_result_t& v)> filter_out;
    FetchLevel required_level{ FetchLevel::NONE };
} report_field_property_evalutors[] = {
    
    // Title & Stocks

    { "sold",   SL2(_1->average_quantity ? false : true), SL1(_1.as_number() == 0) },
    { "active", SL2(_1->average_quantity ? true : false), SL1(_1.as_number() == 0) },
    { "qty",    SL2(_1->average_quantity), SL1(_1.as_number() == 0 || math_real_is_nan(_1.as_number())) },
    { "buy",    SL2(_1->buy_adjusted_price), IS_NOT_A_NUMBER },
    { "day",    SL2(title_get_day_change(_1, _2)), IS_NOT_A_NUMBER },

    { "buy_total_adjusted_qty",    SL2(_1->buy_total_adjusted_qty), IS_NOT_A_NUMBER },
    { "buy_total_adjusted_price",    SL2(_1->buy_total_adjusted_price), IS_NOT_A_NUMBER },
    { "sell_total_adjusted_qty",    SL2(_1->sell_total_adjusted_qty), IS_NOT_A_NUMBER },
    { "sell_total_adjusted_price",    SL2(_1->sell_total_adjusted_price), IS_NOT_A_NUMBER },

    { "buy_total_price",    SL2(_1->buy_total_price), IS_NOT_A_NUMBER },
    { "buy_total_quantity",    SL2(_1->buy_total_quantity), IS_NOT_A_NUMBER },

    { "sell_total_price",    SL2(_1->sell_total_price), IS_NOT_A_NUMBER },
    { "sell_total_quantity",    SL2(_1->sell_total_quantity), IS_NOT_A_NUMBER },

    { "buy_total_price_rated_adjusted",    SL2(_1->buy_total_price_rated_adjusted), IS_NOT_A_NUMBER },
    { "sell_total_price_rated_adjusted",    SL2(_1->sell_total_price_rated_adjusted), IS_NOT_A_NUMBER },

    { "buy_total_price_rated",    SL2(_1->buy_total_price_rated), IS_NOT_A_NUMBER },
    { "sell_total_price_rated",    SL2(_1->sell_total_price_rated), IS_NOT_A_NUMBER },

    { "buy_adjusted_price",    SL2(_1->buy_adjusted_price), IS_NOT_A_NUMBER },
    { "sell_adjusted_price",    SL2(_1->sell_adjusted_price), IS_NOT_A_NUMBER },

    { "average_price",    SL2(_1->average_price), IS_NOT_A_NUMBER },
    { "average_price_rated",    SL2(_1->average_price_rated), IS_NOT_A_NUMBER },
    { "average_quantity",    SL2(_1->average_quantity), IS_NOT_A_NUMBER },
    { "average_buy_price",    SL2(_1->average_buy_price), IS_NOT_A_NUMBER },
    { "average_buy_price_rated",    SL2(_1->average_buy_price_rated), IS_NOT_A_NUMBER },
    { "remaining_shares",    SL2(_1->remaining_shares), IS_NOT_A_NUMBER },
    { "total_dividends",    SL2(_1->total_dividends), IS_NOT_A_NUMBER },
    { "average_ask_price",    SL2(_1->average_ask_price), IS_NOT_A_NUMBER },
    { "average_exchange_rate",    SL2(_1->average_exchange_rate), IS_NOT_A_NUMBER },

    { "date_min",    SL2((double)_1->date_min), IS_NOT_A_NUMBER },
    { "date_max",    SL2((double)_1->date_max), IS_NOT_A_NUMBER },
    { "date_average",    SL2((double)_1->date_average), IS_NOT_A_NUMBER},

    { "title",  SL2(_1->code), SL1(_1.index == 0) },
    { "ps",     SL2(_1->ps.fetch()), IS_NOT_A_NUMBER },
    { "ask",    SL2(_1->ask_price.fetch()), nullptr },
    { "today_exchange_rate",    SL2(_1->today_exchange_rate.fetch()), nullptr },

    // Stock only (Start at index 35 <== !!!UPDATE INDEX IF YOU ADD NEW EVALUATOR ABOVE!!!)

    { "price",      SL2(_2->current.adjusted_close), nullptr, FetchLevel::REALTIME },
    { "date",       SL2((double)_2->current.date), nullptr, FetchLevel::REALTIME },
    { "gmt",        SL2((double)_2->current.gmtoffset), nullptr, FetchLevel::REALTIME },
    { "open",       SL2(_2->current.open), nullptr, FetchLevel::REALTIME },
    { "close",      SL2(_2->current.adjusted_close), nullptr, FetchLevel::REALTIME },
    { "yesterday",  SL2(_2->current.previous_close), nullptr, FetchLevel::REALTIME },
    { "low",        SL2(_2->current.low), nullptr, FetchLevel::REALTIME },
    { "high",       SL2(_2->current.high), nullptr, FetchLevel::REALTIME },
    { "change",     SL2(_2->current.change), IS_NOT_A_NUMBER, FetchLevel::REALTIME },
    { "change_p",   SL2(_2->current.change_p), IS_NOT_A_NUMBER, FetchLevel::REALTIME },
    { "volume",     SL2(_2->current.volume), nullptr, FetchLevel::REALTIME },

    { "price_factor",   SL2(_2->current.price_factor), nullptr, FetchLevel::EOD },
    { "change_p_high",  SL2(_2->current.change_p_high), nullptr, FetchLevel::EOD },

    { "wma",    SL2(_2->current.wma), nullptr, FetchLevel::TECHNICAL_WMA },
    { "ema",    SL2(_2->current.ema), nullptr, FetchLevel::TECHNICAL_EMA },
    { "sma",    SL2(_2->current.sma), nullptr, FetchLevel::TECHNICAL_SMA },
    { "uband",  SL2(_2->current.uband), nullptr, FetchLevel::TECHNICAL_BBANDS },
    { "mband",  SL2(_2->current.mband), nullptr, FetchLevel::TECHNICAL_BBANDS },
    { "lband",  SL2(_2->current.lband), nullptr, FetchLevel::TECHNICAL_BBANDS },
    { "sar",    SL2(_2->current.sar), nullptr, FetchLevel::TECHNICAL_SAR },
    { "slope",  SL2(_2->current.slope), nullptr, FetchLevel::TECHNICAL_SLOPE },
    { "cci",    SL2(_2->current.cci), nullptr, FetchLevel::TECHNICAL_CCI },

    { "dividends",  SL2(_2->dividends_yield.fetch()), nullptr, FetchLevel::FUNDAMENTALS },
    { "earning_trend_actual",  SL2(_2->earning_trend_actual.fetch()), nullptr, FetchLevel::NONE },
    { "earning_trend_estimate",  SL2(_2->earning_trend_estimate.fetch()), nullptr, FetchLevel::NONE },
    { "earning_trend_difference",  SL2(_2->earning_trend_difference.fetch()), nullptr, FetchLevel::NONE },
    { "earning_trend_percent",  SL2(_2->earning_trend_percent.fetch()), nullptr, FetchLevel::NONE },

    { "name",           SL2(string_table_decode(_2->name)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "description",    SL2(string_table_decode(_2->description.fetch())), nullptr, FetchLevel::FUNDAMENTALS },
    { "country",        SL2(string_table_decode(_2->country)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "type",           SL2(string_table_decode(_2->type)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "currency",       SL2(string_table_decode(_2->currency)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "url",            SL2(string_table_decode(_2->url)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "updated_at",     SL2(string_table_decode(_2->updated_at)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "exchange",       SL2(string_table_decode(_2->exchange)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },
    { "symbol",         SL2(string_table_decode(_2->symbol)), SL1(_1.index == 0), FetchLevel::FUNDAMENTALS },

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

    { CTEXT("date"),           SL2((double)_2->date), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("gmtoffset"),      SL2((double)_2->gmtoffset), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("open"),           SL2(_2->open), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("close"),          SL2(_2->adjusted_close), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("previous_close"), SL2(_2->previous_close), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("price_factor"),   SL2(_2->price_factor), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("low"),            SL2(_2->low), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("high"),           SL2(_2->high), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("change"),         SL2(_2->change), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("change_p"),       SL2(_2->change_p), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("change_p_high"),  SL2(_2->change_p_high), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("volume"),         SL2(_2->volume), FetchLevel::REALTIME | FetchLevel::EOD },
    { CTEXT("wma"),            SL2(_2->wma), FetchLevel::REALTIME | FetchLevel::TECHNICAL_WMA },
    { CTEXT("ema"),            SL2(_2->ema), FetchLevel::REALTIME | FetchLevel::TECHNICAL_EMA },
    { CTEXT("sma"),            SL2(_2->sma), FetchLevel::REALTIME | FetchLevel::TECHNICAL_SMA },
    { CTEXT("uband"),          SL2(_2->uband), FetchLevel::REALTIME | FetchLevel::TECHNICAL_BBANDS },
    { CTEXT("mband"),          SL2(_2->mband), FetchLevel::REALTIME | FetchLevel::TECHNICAL_BBANDS },
    { CTEXT("lband"),          SL2(_2->lband), FetchLevel::REALTIME | FetchLevel::TECHNICAL_BBANDS },
    { CTEXT("sar"),            SL2(_2->sar), FetchLevel::REALTIME | FetchLevel::TECHNICAL_SAR },
    { CTEXT("slope"),          SL2(_2->slope), FetchLevel::REALTIME | FetchLevel::TECHNICAL_SLOPE },
    { CTEXT("cci"),            SL2(_2->cci), FetchLevel::REALTIME | FetchLevel::TECHNICAL_CCI }
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
        // Handle default case getting latest information
        for (int i = 35; i < ARRAY_COUNT(report_field_property_evalutors); ++i)
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

FOUNDATION_STATIC expr_result_t report_expr_eval_stock_fundamental(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: F(PFE.NEO, "General.ISIN")
    //           F("CSH-UN.TO", "Highlights.WallStreetTargetPrice")
    //           F("U.US", "Technicals")

    const auto& code = expr_eval_get_string_arg(args, 0, "Invalid symbol code");
    const auto& field = expr_eval_get_string_arg(args, 1, "Invalid field name");

    expr_result_t value;
    eod_fetch("fundamentals", code.str, FORMAT_JSON_CACHE, [field, &value](const json_object_t& json)
    {
        const bool allow_nulls = false;
        const json_object_t& ref = json.find(STRING_ARGS(field), allow_nulls);
        if (ref.is_null())
            return;

        if (ref.root->type == JSON_STRING)
        {
            value.type = EXPR_RESULT_SYMBOL;
            value.value = string_table_encode(ref.as_string());
        }
        else if (ref.root->type == JSON_ARRAY)
        {
            expr_result_t* child_fields = nullptr;

            for (const auto& e : ref)
            {
                if (e.root->type == JSON_PRIMITIVE)
                    array_push(child_fields, e.as_number());
                else
                    array_push(child_fields, e.as_string());
            }

            value = expr_eval_list(child_fields);
        }
        else if (ref.root->type == JSON_OBJECT)
        {
            expr_result_t* child_fields = nullptr;

            for (const auto& e : ref)
            {
                string_const_t id = e.id();

                if (e.root->type == JSON_PRIMITIVE)
                    array_push(child_fields, expr_eval_pair(id, e.as_number()));
                else
                {
                    string_const_t evalue = e.as_string();
                    array_push(child_fields, expr_eval_pair(id, evalue));
                }
            }

            value = expr_eval_list(child_fields);
        }
        else
            value = ref.as_number();
    }, 12 * 60ULL * 60ULL);

    return value;
}

FOUNDATION_STATIC expr_result_t report_eval_report_field(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: R('FLEX', 'ps')
    //           R('_300K', BB.TO, 'ps')
    //           R('_300K', 'buy')

    const auto& report_name = expr_eval_get_string_arg(args, 0, "Invalid report name");
    if (report_name.length < 2)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid report name %.*s", STRING_FORMAT(report_name));

    const bool underscore_name = report_name.str[0] == '_';
    report_handle_t report_handle = report_find_no_case(
        underscore_name ? report_name.str+1 : report_name.str, 
        underscore_name ? report_name.length - 1 : report_name.length);
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
    
    char field_name_buffer[64];
    string_const_t field_name_temp = expr_eval(args->get(field_name_index)).as_string();
    string_const_t field_name = string_to_const(string_copy(STRING_CONST_CAPACITY(field_name_buffer), STRING_ARGS(field_name_temp)));

    tick_t s = time_current();
    while (title_filter.length == 0 && !report_sync_titles(report))
    {
        if (time_elapsed(s) > 30.0f)
            throw ExprError(EXPR_ERROR_EVALUATION_TIMEOUT, "Sync timeout, retry later...", STRING_FORMAT(report_name));
        dispatcher_wait_for_wakeup_main_thread(100);
    }

    expr_result_t* results = nullptr;
    for (int i = 0; i < ARRAY_COUNT(report_field_property_evalutors); ++i)
    {
        const auto& pe = report_field_property_evalutors[i];
        if (report_eval_report_field_test(pe.property_name, report, title_filter, field_name, pe.handler, pe.filter_out, &results, pe.required_level))
            break;
    }

    if (results == nullptr)
        throw ExprError(EXPR_ERROR_EVALUATION_NOT_IMPLEMENTED, "Field %.*s not supported", STRING_FORMAT(field_name));

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
            path_length = string_format(STRING_CONST_CAPACITY(path), STRING_CONST("%.*s.%.*s"), (int)len, s, (int)t->id_length, &json.buffer[t->id]).length;
        else
            path_length = string_copy(STRING_CONST_CAPACITY(path), &json.buffer[t->id], t->id_length).length;

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

FOUNDATION_STATIC void report_eval_cleanup_columns(dynamic_table_column_t* columns)
{
    foreach(c, columns)
    {
        string_deallocate(c->name.str);
        string_deallocate(c->expression.str);
    }
    array_deallocate(columns);
}

FOUNDATION_STATIC void report_eval_dynamic_table_deallocate(dynamic_report_t* report)
{
    table_deallocate(report->table);
    report_eval_cleanup_columns(report->columns);
    foreach(r, report->records)
    {
        foreach(vr, r->resolved)
        {
            if (vr->type == DYNAMIC_TABLE_VALUE_TEXT)
                string_deallocate(vr->text.str);
        }
        array_deallocate(r->resolved);
        array_deallocate(r->values);
    }
    array_deallocate(report->records);
    string_deallocate(report->name.str);
    memory_deallocate(report);
}

FOUNDATION_STATIC bool report_eval_table_dialog(dynamic_report_t* report)
{
    if (report->table == nullptr)
    {
        report->table = table_allocate(report->name.str);
        foreach(c, report->columns)
        {
            table_add_column(report->table, STRING_ARGS(c->name), [c](table_element_ptr_t element, const column_t* column) 
            {
                dynamic_table_record_t* record = (dynamic_table_record_t*)element;
                
                const dynamic_table_record_value_t* v = &record->resolved[c->value_index];
                if (v->type == DYNAMIC_TABLE_VALUE_NULL)
                    return cell_t(nullptr);
                
                if (v->type == DYNAMIC_TABLE_VALUE_TRUE)
                    return cell_t(STRING_CONST("true"));

                if (v->type == DYNAMIC_TABLE_VALUE_FALSE)
                    return cell_t(STRING_CONST("false"));

                if (v->type == DYNAMIC_TABLE_VALUE_TEXT)
                    return cell_t(string_to_const(v->text));

                if (v->type == DYNAMIC_TABLE_VALUE_NUMBER)
                    return cell_t(v->number);

                return cell_t();
            }, c->format, COLUMN_SORTABLE);
        }
    }

    table_render(report->table, report->records, array_size(report->records), sizeof(dynamic_table_record_t), 0.0f, 0.0f);
    return true;
}

FOUNDATION_STATIC void report_eval_add_record_values(dynamic_table_record_t& record, const expr_result_t& e)
{
    if (e.is_set())
    {
        for (auto ee : e)
            report_eval_add_record_values(record, ee);
    }
    else
    {
        array_push(record.values, e);
    }
}

FOUNDATION_STATIC expr_result_t report_eval_table(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: TABLE(test, R(_300K, name), ['name', $2], ['col 1', S($1, open)], ['col 2', S($1, close)])
    //           TABLE(test, R(favorites, name), ['title', $1], ['name', $2], ['open', S($1, open)], ['close', S($1, close)])
    //           TABLE('Test', [U.US, GFL.TO], ['Title', $1], ['Price', S($1, close), currency])
    //           TABLE('Unity Best Days', FILTER(S(U.US, close, ALL), $2 > 60), ['Date', DATESTR($1)], ['Price', $2, currency])
    //           T=U.US, TABLE('Unity Best Days', FILTER(S(T, close, ALL), $2 > 60), 
    //              ['Date', DATESTR($1)],
    //              ['Price', $2, currency],
    //              ['%', S(T, change_p, $1), percentage])

    if (args->len < 3)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Requires at least two arguments");

    TIME_TRACKER("report_eval_table");

    // Get the data set
    expr_result_t elements = expr_eval(args->get(1));
    if (!elements.is_set())
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Second argument must be a dataset");

    // Then for each remaining arguments, evaluate them as columns.
    dynamic_table_column_t* columns = nullptr;
    for (int i = 2, col_index = 0; i < args->len; ++i, ++col_index)
    {
        dynamic_table_column_t col;
        col.ee = args->get(i);
        col.format = COLUMN_FORMAT_TEXT;
        if (col.ee->type == OP_SET && col.ee->args.len >= 2)
        {
            // Get the column name
            string_const_t col_name = expr_eval((expr_t*)col.ee->args.get(0)).as_string(string_format_static_const("col %d", i - 2));

            if (col.ee->args.len >= 3)
            {
                string_const_t format_string = expr_eval((expr_t*)col.ee->args.get(2)).as_string();
                if (!string_is_null(format_string))
                {
                    if (string_equal_nocase(STRING_ARGS(format_string), STRING_CONST("currency")))
                        col.format = COLUMN_FORMAT_CURRENCY;
                    else if (string_equal_nocase(STRING_ARGS(format_string), STRING_CONST("percentage")))
                        col.format = COLUMN_FORMAT_PERCENTAGE;
                    else if (string_equal_nocase(STRING_ARGS(format_string), STRING_CONST("date")))
                        col.format = COLUMN_FORMAT_DATE;
                    else if (string_equal_nocase(STRING_ARGS(format_string), STRING_CONST("number")))
                        col.format = COLUMN_FORMAT_NUMBER;
                }
            }
            
            col.ee = col.ee->args.get(1);
            string_const_t col_expression = col.ee->token;

            col.name = string_clone(STRING_ARGS(col_name));
            col.expression = string_clone(STRING_ARGS(col_expression));
            
            col.value_index = col_index;

            array_push_memcpy(columns, &col);
        }
        else
        {
            report_eval_cleanup_columns(columns);
            throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Column argument must be a set of at least two elements, i.e. [name, evaluator[, ...options]");
        }
    }

    dynamic_table_record_t* records = nullptr;
    for (auto e : elements)
    {
        if (e.type == EXPR_RESULT_NULL)
            continue;

        dynamic_table_record_t record{};
        report_eval_add_record_values(record, e);

        for(unsigned ic = 0, endc = array_size(columns); ic < endc; ++ic)
        {
            dynamic_table_column_t* c = &columns[ic];
            
            foreach(v, record.values)
            {
                string_const_t arg_macro = string_format_static(STRING_CONST("$%u"), i + 1);
                eval_set_or_create_global_var(STRING_ARGS(arg_macro), *v);
            }

            TIME_TRACKER("report_eval_table_ELEMENT(%.*s)", STRING_FORMAT(c->name));

            expr_result_t cv = expr_eval(c->ee);
            if (cv.type == EXPR_RESULT_TRUE)
            {
                array_push(record.resolved, dynamic_table_record_value_t{ DYNAMIC_TABLE_VALUE_TRUE });
            }
            else if (cv.type == EXPR_RESULT_FALSE)
            {
                array_push(record.resolved, dynamic_table_record_value_t{ DYNAMIC_TABLE_VALUE_FALSE });
            }
            else if (cv.type == EXPR_RESULT_NUMBER)
            {
                dynamic_table_record_value_t v{ DYNAMIC_TABLE_VALUE_NUMBER };
                v.number = cv.as_number();
                array_push(record.resolved, v);
            }
            else if (cv.type == EXPR_RESULT_SYMBOL)
            {
                dynamic_table_record_value_t v{ DYNAMIC_TABLE_VALUE_TEXT };
                string_const_t e_text = cv.as_string();
                v.text = string_clone(STRING_ARGS(e_text));
                array_push(record.resolved, v);
            }
            else if (cv.is_set())
            {
                dynamic_table_record_value_t v{ DYNAMIC_TABLE_VALUE_NUMBER };
                v.number = cv.as_number(DNAN, 0);
                array_push(record.resolved, v);
            }
            else
            {
                array_push(record.resolved, dynamic_table_record_value_t{ DYNAMIC_TABLE_VALUE_NULL });
            }
        }

        FOUNDATION_ASSERT(array_size(columns) == array_size(record.resolved));
        array_push_memcpy(records, &record);
    }

    // Get the table name from the first argument.
    string_const_t table_name = expr_eval(args->get(0)).as_string("none");

    // Create the dynamic report
    dynamic_report_t* report = (dynamic_report_t*)memory_allocate(HASH_REPORT_EXPRESSION, sizeof(dynamic_report_t), 0, MEMORY_PERSISTENT);
    report->name = string_clone(STRING_ARGS(table_name));
    report->columns = columns;
    report->records = records;
    report->table = nullptr;

    app_open_dialog(table_name.str, 
        L1(report_eval_table_dialog((dynamic_report_t*)_1)), 800, 600, true,
        L1(report_eval_dynamic_table_deallocate((dynamic_report_t*)_1)), report);
    return elements;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void report_expr_initialize()
{
    eval_register_function("S", report_expr_eval_stock);
    eval_register_function("F", report_expr_eval_stock_fundamental);
    eval_register_function("R", report_eval_report_field);
    eval_register_function("FIELDS", report_eval_list_fields);
    eval_register_function("TABLE", report_eval_table);
}

FOUNDATION_STATIC void report_expr_shutdown()
{
    
}

DEFINE_SERVICE(REPORT_EXPRESSION, report_expr_initialize, report_expr_shutdown, SERVICE_PRIORITY_MODULE);
