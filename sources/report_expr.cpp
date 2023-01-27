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

#include <foundation/thread.h>

struct dynamic_table_column_t
{
    string_t name;
    string_t expression;
    expr_t* ee;
    int value_index;
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
    dynamic_table_record_value_t* values{ nullptr };
    dynamic_table_record_value_t* resolved{ nullptr };
    //string_t cached_value{};
};

struct dynamic_report_t
{
    string_t name;
    dynamic_table_column_t* columns;
    dynamic_table_record_t* records;
    table_t* table;
};

#define HASH_REPORT_EXPRESSION static_hash_string("report_expression", 17, 0x6eb03c48b763689fULL)

#define IS_NOT_A_NUMBER SL1(math_real_is_nan(_1.as_number()))
#define EVAL_STOCK_FIELD(field_name) FOUNDATION_PREPROCESSOR_TOSTRING(field_name), SL2(_2->field_name), IS_NOT_A_NUMBER, FetchLevel::FUNDAMENTALS

static struct {
    const char* property_name;
    function<expr_result_t(title_t* t, const stock_t* s)> handler;
    function<bool(const expr_result_t& v)> filter_out;
    FetchLevel required_level{ FetchLevel::NONE };
} report_field_property_evalutors[] = {
    { "sold",  SL2(_1->average_quantity ? false : true), SL1(_1.as_number() == 0) },
    { "active",  SL2(_1->average_quantity ? true : false), SL1(_1.as_number() == 0) },
    { "qty",  SL2(_1->average_quantity), SL1(_1.as_number() == 0 || math_real_is_nan(_1.as_number())) },
    { "buy",  SL2(_1->buy_adjusted_price), IS_NOT_A_NUMBER },
    { "day",  SL2(title_get_day_change(_1, _2)), IS_NOT_A_NUMBER },

    { "title",  SL2(_1->code), SL1(_1.index == 0) },
    { "ps",  SL2(_1->ps.fetch()), IS_NOT_A_NUMBER },
    { "ask",  SL2(_1->ask_price.fetch()), nullptr },

    { "price",  SL2(_2->current.close), nullptr },
    { "date",  SL2((double)_2->current.date), nullptr },
    { "gmt",  SL2((double)_2->current.gmtoffset), nullptr},
    { "open",  SL2(_2->current.open), nullptr },
    { "close",  SL2(_2->current.close), nullptr },
    { "previous_close",  SL2(_2->current.previous_close), nullptr},
    { "low",  SL2(_2->current.low), nullptr },
    { "high",  SL2(_2->current.high), nullptr },
    { "change",  SL2(_2->current.change), IS_NOT_A_NUMBER },
    { "change_p",  SL2(_2->current.change_p), IS_NOT_A_NUMBER },
    { "volume",  SL2(_2->current.volume), nullptr },

    { "price_factor",  SL2(_2->current.price_factor), nullptr, FetchLevel::TECHNICAL_EOD | FetchLevel::TECHNICAL_INDEXED_PRICE },
    { "change_p_high",  SL2(_2->current.change_p_high), nullptr, FetchLevel::TECHNICAL_EOD | FetchLevel::TECHNICAL_INDEXED_PRICE },

    { "wma",  SL2(_2->current.wma), nullptr, FetchLevel::TECHNICAL_WMA },
    { "ema",  SL2(_2->current.ema), nullptr, FetchLevel::TECHNICAL_EMA },
    { "sma",  SL2(_2->current.sma), nullptr, FetchLevel::TECHNICAL_SMA },
    { "uband",  SL2(_2->current.uband), nullptr, FetchLevel::TECHNICAL_BBANDS },
    { "mband",  SL2(_2->current.mband), nullptr, FetchLevel::TECHNICAL_BBANDS },
    { "lband",  SL2(_2->current.lband), nullptr, FetchLevel::TECHNICAL_BBANDS },
    { "sar",  SL2(_2->current.sar), nullptr, FetchLevel::TECHNICAL_SAR },
    { "slope",  SL2(_2->current.slope), nullptr, FetchLevel::TECHNICAL_SLOPE },
    { "cci",  SL2(_2->current.cci), nullptr, FetchLevel::TECHNICAL_CCI },

    { "dividends",  SL2(_2->dividends_yield.fetch()), nullptr },

    { "name",  SL2(string_table_decode(_2->name)), SL1(_1.index == 0)},
    { "description",  SL2(string_table_decode(_2->description.fetch())), nullptr},
    { "country",  SL2(string_table_decode(_2->country)), SL1(_1.index == 0)},
    { "type",  SL2(string_table_decode(_2->type)), SL1(_1.index == 0)},
    { "currency",  SL2(string_table_decode(_2->currency)), SL1(_1.index == 0)},
    { "url",  SL2(string_table_decode(_2->url)), SL1(_1.index == 0)},
    { "updated_at",  SL2(string_table_decode(_2->updated_at)), SL1(_1.index == 0)},
    { "exchange",  SL2(string_table_decode(_2->exchange)), SL1(_1.index == 0)},
    { "symbol",  SL2(string_table_decode(_2->symbol)), SL1(_1.index == 0)},

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

// 
// # PRIVATE
//

FOUNDATION_STATIC expr_result_t report_expr_eval_stock(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: S(GLF.TO, open)
    //           S(GFL.TO, close) - S(GFL.TO, open)

    if (args->len < 2)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    string_const_t code = expr_eval(args->get(0)).as_string();
    string_const_t field = expr_eval(args->get(1)).as_string();

    double value = NAN;
    eod_fetch("real-time", code.str, FORMAT_JSON, [field, &value](const json_object_t& json)
    {
        value = json[field].as_number();
    }, 10ULL * 60ULL);

    return value;
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
        else if (ref.root->type == JSON_OBJECT)
        {
            expr_result_t* child_fields = nullptr;

            for (const auto& e : ref)
            {
                string_const_t id = e.id();
                string_const_t evalue = e.as_string();

                char value_preview_buffer[64];
                string_t value_preview = string_format(STRING_CONST_CAPACITY(value_preview_buffer), STRING_CONST("%.*s = %.*s"), 
                    min(48, (int)id.length), id.str, STRING_FORMAT(evalue));
                string_table_symbol_t store_value_symbol = string_table_encode(value_preview);
                
                expr_result_t r { EXPR_RESULT_SYMBOL, store_value_symbol, value_preview.length };
                array_push(child_fields, r);
            }

            value = expr_eval_list(child_fields);
        }
        else
            value = ref.as_number();
    }, 12 * 60ULL * 60ULL);

    return value;
}

FOUNDATION_STATIC bool report_eval_report_field_resolve_level(title_t* t, FetchLevel request_level)
{
    const stock_t* s = t->stock;
    if (s == nullptr)
        return false;

    if (!s->has_resolve(request_level))
    {
        if (stock_resolve(t->stock, request_level))
        {
            const double timeout_expired = 2.0f;
            const tick_t timeout = time_current();
            while (!s->has_resolve(request_level) && time_elapsed(timeout) < timeout_expired)
                thread_try_wait(timeout_expired * 100);

            if (time_elapsed(timeout) >= timeout_expired)
            {
                log_warnf(0, WARNING_PERFORMANCE, STRING_CONST("Failed to resolving %d for %.*s in time"), request_level, (int)t->code_length, t->code);
            }
        }
    }

    return s->has_resolve(request_level);
}

FOUNDATION_STATIC bool report_eval_report_field_test(
    const char* property_name, report_t* report, 
    string_const_t title_filter, string_const_t field_name, 
    const function<expr_result_t(title_t* t, const stock_t* s)> property_evalutor,
    const function<bool(const expr_result_t& v)> property_filter_out,
    expr_result_t** results, FetchLevel required_level)
{
    if (!string_equal_nocase(property_name, string_length(property_name), STRING_ARGS(field_name)))
        return false;

    for (auto t : generics::fixed_array(report->titles))
    {
        const stock_t* s = t->stock;
        if (!s || s->has_resolve(FetchLevel::EOD | FetchLevel::REALTIME))
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

    size_t field_name_index = 1;
    string_const_t title_filter{};
    if (args->len >= 3)
    {
        title_filter = expr_eval_get_string_arg(args, 1, "Invalid title name");
        field_name_index = 2;
    }

    report_t* report = report_get(report_handle);
    const auto& field_name = expr_eval_get_string_arg(args, field_name_index, "Invalid field name");

    tick_t s = time_current();
    while (!report_sync_titles(report))
    {
        if (time_elapsed(s) > 30.0f)
            throw ExprError(EXPR_ERROR_EVALUATION_TIMEOUT, "Sync timeout, retry later...", STRING_FORMAT(report_name));
        thread_try_wait(100);
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

    }, 15 * 60 * 60ULL);

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
        foreach(v, r->values)
        {
            if (v->type == DYNAMIC_TABLE_VALUE_TEXT)
                string_deallocate(v->text.str);
        }
        array_deallocate(r->values);

        foreach(vr, r->resolved)
        {
            if (vr->type == DYNAMIC_TABLE_VALUE_TEXT)
                string_deallocate(vr->text.str);
        }
        array_deallocate(r->resolved);
        //string_deallocate(r->cached_value.str);
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
            }, COLUMN_FORMAT_TEXT, COLUMN_SORTABLE);
        }
    }

    table_render(report->table, report->records, array_size(report->records), sizeof(dynamic_table_record_t), 0.0f, 0.0f);
    return true;
}

FOUNDATION_STATIC void report_eval_add_record_values(dynamic_table_record_t& record, const expr_result_t& e)
{
    if (e.type == EXPR_RESULT_TRUE)
    {
        array_push(record.values, dynamic_table_record_value_t{ DYNAMIC_TABLE_VALUE_TRUE });
    }
    else if (e.type == EXPR_RESULT_FALSE)
    {
        array_push(record.values, dynamic_table_record_value_t{ DYNAMIC_TABLE_VALUE_FALSE });
    }
    else if (e.type == EXPR_RESULT_NUMBER)
    {
        dynamic_table_record_value_t v{ DYNAMIC_TABLE_VALUE_NUMBER };
        v.number = e.as_number();
        array_push(record.values, v);
    }
    else if (e.type == EXPR_RESULT_SYMBOL)
    {
        dynamic_table_record_value_t v{ DYNAMIC_TABLE_VALUE_TEXT };
        string_const_t e_text = e.as_string();
        v.text = string_clone(STRING_ARGS(e_text));
        array_push(record.values, v);
    }
    else if (e.is_set())
    {
        for (auto ee : e)
            report_eval_add_record_values(record, ee);
    }
}

FOUNDATION_STATIC expr_result_t report_eval_table(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: TABLE(test, R(_300K, name), ['name', $2], ['col 1', S($1, open)], ['col 2', S($1, close)])
    //           TABLE(test, R(favorites, name), ['title', $1], ['name', $2], ['open', S($1, open)], ['close', S($1, close)])

    if (args->len < 2)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Requires at least two arguments");

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
        if (col.ee->type == OP_SET && col.ee->args.len >= 2)
        {
            // Get the column name
            string_const_t col_name = expr_eval((expr_t*)col.ee->args.get(0)).as_string(string_format_static_const("col %d", i - 2));
            
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

        foreach(c, columns)
        {
            foreach(v, record.values)
            {
                string_const_t arg_macro = string_format_static(STRING_CONST("$%u"), i + 1);
                expr_var_t* ev = eval_get_or_create_global_var(STRING_ARGS(arg_macro));
                if (v->type == DYNAMIC_TABLE_VALUE_NULL)
                    ev->value = {};
                else if (v->type == DYNAMIC_TABLE_VALUE_TRUE)
                    ev->value = expr_result_t(true);
                else if (v->type == DYNAMIC_TABLE_VALUE_FALSE)
                    ev->value = expr_result_t(false);
                else if (v->type == DYNAMIC_TABLE_VALUE_TEXT)
                    ev->value = expr_result_t(v->text.str);
                else if (v->type == DYNAMIC_TABLE_VALUE_NUMBER)
                    ev->value = expr_result_t(v->number);
            }

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
