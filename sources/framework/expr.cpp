/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * This module uses code from https://github.com/zserge/expr
 */

#include "expr.h"

#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/service.h>
#include <framework/profiler.h>
#include <framework/config.h>
#include <framework/dispatcher.h>

#include <foundation/time.h>
#include <foundation/random.h>
#include <foundation/process.h>
#include <foundation/system.h>

#include <ctype.h> /* for isdigit, isspace */

#include <numeric>

thread_local char EXPR_ERROR_MSG[256];
thread_local expr_error_code_t EXPR_ERROR_CODE;
thread_local const expr_result_t expr_result_t::NIL{};

static thread_local expr_var_list_t _global_vars = { 0 };
static thread_local const expr_result_t** _expr_lists = nullptr;
static expr_func_t* _expr_user_funcs = nullptr;
static string_t* _expr_user_funcs_names = nullptr;

static expr_evaluator_t* _evaluators = nullptr;

static struct {
    const expr_string_t token;
    const expr_type_t op;
} OPS[] = {
    {STRING_CONST("-u"), OP_UNARY_MINUS},
    {STRING_CONST("!u"), OP_UNARY_LOGICAL_NOT},
    {STRING_CONST("^u"), OP_UNARY_BITWISE_NOT},
    {STRING_CONST("**"), OP_POWER},
    {STRING_CONST("*"), OP_MULTIPLY},
    {STRING_CONST("/"), OP_DIVIDE},
    {STRING_CONST("%"), OP_REMAINDER},
    {STRING_CONST("+"), OP_PLUS},
    {STRING_CONST("-"), OP_MINUS},
    {STRING_CONST("<<"), OP_SHL},
    {STRING_CONST(">>"), OP_SHR},
    {STRING_CONST("<"), OP_LT},
    {STRING_CONST("<="), OP_LE},
    {STRING_CONST(">"), OP_GT},
    {STRING_CONST(">="), OP_GE},
    {STRING_CONST("=="), OP_EQ},
    {STRING_CONST("!="), OP_NE},
    {STRING_CONST("&"), OP_BITWISE_AND},
    {STRING_CONST("|"), OP_BITWISE_OR},
    {STRING_CONST("^"), OP_BITWISE_XOR},
    {STRING_CONST("&&"), OP_LOGICAL_AND},
    {STRING_CONST("||"), OP_LOGICAL_OR},
    {STRING_CONST("="), OP_ASSIGN},
    {STRING_CONST(","), OP_COMMA},

    /* These are used by lexer and must be ignored by parser, so we put them at the end */
    {STRING_CONST("-"), OP_UNARY_MINUS},
    {STRING_CONST("!"), OP_UNARY_LOGICAL_NOT},
    {STRING_CONST("^"), OP_UNARY_BITWISE_NOT},
};

enum ExprParsingOptions {
    EXPR_TOP = (1 << 0),
    EXPR_TOPEN = (1 << 1),
    EXPR_TCLOSE = (1 << 2),
    EXPR_TNUMBER = (1 << 3),
    EXPR_TWORD = (1 << 4),
    EXPR_TDEFAULT = (EXPR_TOPEN | EXPR_TNUMBER | EXPR_TWORD),

    EXPR_UNARY = (1 << 5),
    EXPR_COMMA = (1 << 6),
    EXPR_SET = (1 << 7),
};

enum ExprParsingParens {
    EXPR_PAREN_ALLOWED = 0,
    EXPR_PAREN_EXPECTED = 1,
    EXPR_PAREN_FORBIDDEN = 2,
};

typedef vec<expr_arg_t> vec_arg_t;
typedef vec<expr_string_t> vec_str_t;
typedef vec<expr_macro_t> vec_macro_t;

FOUNDATION_FORCEINLINE bool isfirstvarchr(char c)
{
    return (((unsigned char)c >= '@' && c != '^' && c != '|' && c != '[' && c != ']') || c == '$');
}

FOUNDATION_FORCEINLINE bool isvarchr(char c)
{
    return (((unsigned char)c >= '@' && c != '^' && c != '|' && c != '[' && c != ']') || c == '$' || c == '#' || c == '.' || (c >= '0' && c <= '9'));
}

#define vec_len(v)			((v)->len)
#define vec_unpack(v)		(char **)&(v)->buf, &(v)->len, &(v)->cap, sizeof(*(v)->buf)
#define vec_push(v, val)	expr_vec_expand(vec_unpack(v)) ? EXPR_ERROR_ALLOCATION_FAILED : ((v)->buf[(v)->len++] = (val), 0)
#define vec_nth(v, i)		(v)->buf[i]
#define vec_peek(v)			(v)->buf[(v)->len - 1]
#define vec_pop(v)			(v)->buf[--(v)->len]
#define vec_free(v)			(memory_deallocate((v)->buf), (v)->buf = NULL, (v)->len = (v)->cap = 0)
#define vec_foreach(v, var, iter)                                              \
  if ((v)->len > 0)                                                            \
    for ((iter) = 0; (iter) < (v)->len && (((var) = (v)->buf[(iter)]), 1);     \
         ++(iter))

/*
 * Simple expandable vector implementation
 */
FOUNDATION_STATIC int expr_vec_expand(char** buf, int* length, int* cap, int memsz)
{
    MEMORY_TRACKER(HASH_EXPR);

    if (*length + 1 > *cap)
    {
        void* ptr;
        int n = (*cap == 0) ? 1 : *cap << 1;
        ptr = memory_reallocate(*buf, n * memsz, 8, *cap * memsz/*memory_size(*buf)*/, MEMORY_PERSISTENT);
        if (ptr == NULL)
        {
            log_errorf(HASH_EXPR, ERROR_OUT_OF_MEMORY, STRING_CONST("Failed to allocate memory to expand vector"));
            return EXPR_ERROR_ALLOCATION_FAILED; /* allocation failed */
        }
        *buf = (char*)ptr;
        *cap = n;
    }
    return 0;
}

FOUNDATION_FORCEINLINE expr_t expr_init(expr_type_t type)
{
    return expr_t{ type, {}, {}, {nullptr, 0} };
}

FOUNDATION_FORCEINLINE expr_t expr_init(expr_type_t type, string_const_t token)
{
    return expr_t{ type, {}, {}, token };
}

FOUNDATION_FORCEINLINE expr_t expr_init(expr_type_t type, const char* token, size_t token_length = -1)
{
    return expr_t{ type, {}, {}, {token, token_length != -1 ? token_length : string_length(token)} };
}

template<typename T>
string_const_t expr_result_string_join(const expr_result_t& e, const char* fmt)
{
    const uint32_t element_count = e.element_count();
    if (element_count > 99)
        return string_format_static(STRING_CONST("[too many values (%u)...]"), element_count);
    return string_join<const T>((const T*)e.ptr, e.element_count(), [fmt](const T& v)
        {
            static thread_local char buf[32];
            string_t f = string_format(STRING_CONST_CAPACITY(buf), fmt, string_length(fmt), v);
            return string_to_const(f);
        }, CTEXT(", "), CTEXT("["), CTEXT("]"));
}

string_const_t expr_result_t::as_string(const char* fmt /*= nullptr*/) const
{
    if (type == EXPR_RESULT_NULL)
        return CTEXT("nil");

    if (type == EXPR_RESULT_NUMBER)
    {
        if (fmt)
            return string_format_static(fmt, string_length(fmt), value);
        return string_from_real_static(value, 0, 0, 0);
    }
    if (type == EXPR_RESULT_TRUE)
        return CTEXT("true");
    if (type == EXPR_RESULT_FALSE)
        return CTEXT("false");
    if (type == EXPR_RESULT_SYMBOL)
        return string_table_decode_const(math_trunc(value));

    if (type == EXPR_RESULT_ARRAY)
    {
        string_const_t list_sep = array_size(list) > 8 ? CTEXT(",\n\t ") : CTEXT(", ");
        return string_join<const expr_result_t>(list,
            [fmt](const expr_result_t& e) { return e.as_string(fmt); },
            list_sep, CTEXT("["), CTEXT("]"));
    }

    if (type == EXPR_RESULT_POINTER)
    {
        const uint32_t element_count = this->element_count();
        if (ptr == nullptr || element_count == 0)
            return CTEXT("nil");

        uint16_t element_size = this->element_size();
        if ((index & EXPR_POINTER_ARRAY_FLOAT))
        {
            if (element_size == 4) return expr_result_string_join<float>(*this, "%.4f");
            else if (element_size == 8) return expr_result_string_join<double>(*this, "%.4lf");
        }
        else if ((index & EXPR_POINTER_ARRAY_INTEGER))
        {
            if ((index & EXPR_POINTER_ARRAY_UNSIGNED) == EXPR_POINTER_ARRAY_UNSIGNED)
            {
                if (element_size == 1) return expr_result_string_join<uint8_t>(*this, "%u");
                else if (element_size == 2) return expr_result_string_join<uint16_t>(*this, "%hu");
                else if (element_size == 4) return expr_result_string_join<uint32_t>(*this, "%u");
                else if (element_size == 8) return expr_result_string_join<uint64_t>(*this, "%llu");
            }
            else
            {
                if (element_size == 1) return expr_result_string_join<int8_t>(*this, "%d");
                else if (element_size == 2) return expr_result_string_join<int16_t>(*this, "%hd");
                else if (element_size == 4) return expr_result_string_join<int32_t>(*this, "%d");
                else if (element_size == 8) return expr_result_string_join<int64_t>(*this, "%lld");
            }
        }
        return string_format_static(STRING_CONST("0x%p (%d [%d])"), ptr, this->element_count(), this->element_size());
    }

    return string_null();
}

expr_result_t expr_eval_symbol(string_table_symbol_t symbol)
{
    expr_result_t r(EXPR_RESULT_SYMBOL);
    r.value = (double)symbol;
    return r;
}

const expr_result_t* expr_eval_list(const expr_result_t* list)
{
    if (list)
        array_push(_expr_lists, list);
    return list;
}

static expr_result_t expr_eval_set(expr_t* e)
{
    expr_result_t* resolved_values = nullptr;

    for (int i = 0; i < e->args.len; ++i)
    {
        expr_result_t r = expr_eval(&e->args.buf[i]);
        array_push(resolved_values, r);
    }

    return expr_eval_list(resolved_values);
}

expr_result_t expr_eval_merge(const expr_result_t& key, const expr_result_t& value, bool keep_nulls)
{
    expr_result_t* kvp = nullptr;
    if (key.type == EXPR_RESULT_ARRAY)
    {
        for (auto e : key)
        {
            if (keep_nulls || !e.is_null())
                array_push(kvp, e);
        }
    }
    else if (keep_nulls || !key.is_null())
        array_push(kvp, key);

    if (value.type == EXPR_RESULT_ARRAY)
    {
        for (auto e : value)
        {
            if (keep_nulls || !e.is_null())
                array_push(kvp, e);
        }
    }
    else if (keep_nulls || !value.is_null())
        array_push(kvp, value);

    if (array_size(kvp) == 1)
    {
        expr_result_t single_value = kvp[0];
        array_deallocate(kvp);
        return single_value;
    }

    return expr_eval_list(kvp);
}

expr_result_t expr_eval_pair(const expr_result_t& key, const expr_result_t& value)
{
    expr_result_t* kvp = nullptr;
    array_push(kvp, key);
    array_push(kvp, value);
    if (kvp)
        array_push(_expr_lists, kvp);
    return expr_result_t(kvp, 1ULL);
}

string_const_t expr_eval_get_string_arg(const vec_expr_t* args, size_t idx, const char* message)
{
    if (idx >= args->len)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Missing arguments: %s", message);

    const expr_t& arg = vec_nth(args, idx);
    if (arg.type != OP_VAR || arg.token.length == 0)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid argument %.*s: %s", STRING_FORMAT(arg.token), message);

    return arg.token;
}

string_const_t expr_eval_get_string_copy_arg(const vec_expr_t* args, size_t idx, const char* message)
{
    const auto& arg_string = expr_eval_get_string_arg(args, idx, message);

    string_t arg_buffer = string_static_buffer(arg_string.length + 1);
    return string_to_const(string_copy(STRING_ARGS(arg_buffer), STRING_ARGS(arg_string)));
}

FOUNDATION_STATIC expr_result_t expr_eval_date_to_string(const expr_func_t* f, vec_expr_t* args, void* c)
{
    if (args->len != 1)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments: DATESTR(<unix time stamp>)");

    time_t time = (time_t)expr_eval(args->get(0)).as_number(0);
    return string_from_date(time);
}

FOUNDATION_STATIC expr_result_t expr_eval_create_date(const expr_func_t* f, vec_expr_t* args, void* c)
{
    if (args->len != 3)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid argument count for DATE");

    const int year = (int)expr_eval(args->get(0)).as_number(0);
    if (year < 1970)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid year argument, must be >=1970");
    const int month = (int)expr_eval(args->get(1)).as_number(0);
    if (month <= 0 || month > 12)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid month argument, must be between 1 and 12");
    const int day = (int)expr_eval(args->get(2)).as_number(0);
    if (day <= 0 || day > 31)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid day argument, must be between 1 and 31");

    return (double)time_make(year, month, day, 0, 0, 0, 0);
}


FOUNDATION_STATIC expr_result_t expr_eval_time_now(const expr_func_t* f, vec_expr_t* args, void* c)
{
    return (double)time_now();
}

template<typename T> T min_range(const T* ptr, unsigned count)
{
    T m = std::numeric_limits<T>::max();
    for (unsigned i = 0; i < count; ++i, ++ptr)
        m = min(*ptr, m);
    return m;
}

template<typename T> T max_range(const T* ptr, unsigned count)
{
    T m = std::numeric_limits<T>::min();
    for (unsigned i = 0; i < count; ++i, ++ptr)
        m = max(*ptr, m);
    return m;
}

FOUNDATION_STATIC expr_result_t expr_eval_raw_math_min(void* ptr, uint16_t element_size, uint32_t element_count, uint64_t flags)
{
    if ((flags & EXPR_POINTER_ARRAY_FLOAT))
    {
        if (element_size == 4)
            return (double)min_range((const float*)ptr, element_count);

        FOUNDATION_ASSERT(element_size == 8);
        return min_range((const double*)ptr, element_count);
    }

    if ((flags & EXPR_POINTER_ARRAY_INTEGER))
    {
        if ((flags & EXPR_POINTER_ARRAY_UNSIGNED) == EXPR_POINTER_ARRAY_UNSIGNED)
        {
            if (element_size == 1) return (double)min_range((const uint8_t*)ptr, element_count);
            if (element_size == 2) return (double)min_range((const uint16_t*)ptr, element_count);
            if (element_size == 4) return (double)min_range((const uint32_t*)ptr, element_count);

            FOUNDATION_ASSERT(element_size == 8);
            return (double)min_range((const uint64_t*)ptr, element_count);
        }

        if (element_size == 1) return (double)min_range((const int8_t*)ptr, element_count);
        if (element_size == 2) return (double)min_range((const int16_t*)ptr, element_count);
        if (element_size == 4) return (double)min_range((const int32_t*)ptr, element_count);

        FOUNDATION_ASSERT(element_size == 8);
        return (double)min_range((const int64_t*)ptr, element_count);
    }

    FOUNDATION_ASSERT(!"Unsupported");
    return NIL;
}

FOUNDATION_STATIC expr_result_t expr_eval_raw_math_max(void* ptr, uint16_t element_size, uint32_t element_count, uint64_t flags)
{
    if ((flags & EXPR_POINTER_ARRAY_FLOAT))
    {
        if (element_size == 4)
            return (double)max_range((const float*)ptr, element_count);

        FOUNDATION_ASSERT(element_size == 8);
        return max_range((const double*)ptr, element_count);
    }

    if ((flags & EXPR_POINTER_ARRAY_INTEGER))
    {
        if ((flags & EXPR_POINTER_ARRAY_UNSIGNED) == EXPR_POINTER_ARRAY_UNSIGNED)
        {
            if (element_size == 1) return (double)max_range((const uint8_t*)ptr, element_count);
            if (element_size == 2) return (double)max_range((const uint16_t*)ptr, element_count);
            if (element_size == 4) return (double)max_range((const uint32_t*)ptr, element_count);

            FOUNDATION_ASSERT(element_size == 8);
            return (double)max_range((const uint64_t*)ptr, element_count);
        }

        if (element_size == 1) return (double)max_range((const int8_t*)ptr, element_count);
        if (element_size == 2) return (double)max_range((const int16_t*)ptr, element_count);
        if (element_size == 4) return (double)max_range((const int32_t*)ptr, element_count);

        FOUNDATION_ASSERT(element_size == 8);
        return (double)max_range((const int64_t*)ptr, element_count);
    }

    FOUNDATION_ASSERT(!"Unsupported");
    return NIL;
}

FOUNDATION_STATIC expr_result_t expr_eval_raw_math_sum(void* ptr, uint16_t element_size, uint32_t element_count, uint64_t flags)
{
    if ((flags & EXPR_POINTER_ARRAY_FLOAT))
    {
        if (element_size == 4)
            return std::accumulate((const float*)ptr, (const float*)ptr + element_count, 0.0);

        FOUNDATION_ASSERT(element_size == 8);
        return std::accumulate((const double*)ptr, (const double*)ptr + element_count, 0.0);
    }

    if ((flags & EXPR_POINTER_ARRAY_INTEGER))
    {
        if ((flags & EXPR_POINTER_ARRAY_UNSIGNED) == EXPR_POINTER_ARRAY_UNSIGNED)
        {
            if (element_size == 1) return std::accumulate((const uint8_t*)ptr, (const uint8_t*)ptr + element_count, 0.0);
            if (element_size == 2) return std::accumulate((const uint16_t*)ptr, (const uint16_t*)ptr + element_count, 0.0);
            if (element_size == 4) return std::accumulate((const uint32_t*)ptr, (const uint32_t*)ptr + element_count, 0.0);

            FOUNDATION_ASSERT(element_size == 8);
            return std::accumulate((const uint64_t*)ptr, (const uint64_t*)ptr + element_count, 0.0);
        }

        if (element_size == 1) return std::accumulate((const int8_t*)ptr, (const int8_t*)ptr + element_count, 0.0);
        if (element_size == 2) return std::accumulate((const int16_t*)ptr, (const int16_t*)ptr + element_count, 0.0);
        if (element_size == 4) return std::accumulate((const int32_t*)ptr, (const int32_t*)ptr + element_count, 0.0);

        FOUNDATION_ASSERT(element_size == 8);
        return std::accumulate((const int64_t*)ptr, (const int64_t*)ptr + element_count, 0.0);
    }

    FOUNDATION_ASSERT(!"Unsupported");
    return NIL;
}

FOUNDATION_STATIC expr_result_t expr_eval_raw_math_avg(void* ptr, uint16_t element_size, uint32_t element_count, uint64_t flags)
{
    expr_result_t sum = expr_eval_raw_math_sum(ptr, element_size, element_count, flags);
    return sum / (expr_result_t)(double)element_count;
}

FOUNDATION_STATIC expr_result_t expr_eval_math_min(const expr_result_t* list)
{
    if (list == nullptr)
        return NIL;

    expr_result_t min;
    for (size_t i = 0; i < array_size(list); ++i)
    {
        expr_result_t e = list[i];

        if (e.is_set() && e.index == NO_INDEX)
            e = expr_eval_math_min(e.list);
        else if (e.is_raw_array())
            e = expr_eval_raw_math_min(e.ptr, e.element_size(), e.element_count(), e.index);

        if (e.is_null(e.index))
            continue;

        if (e < min)
            min = e;
    }

    return min;
}

FOUNDATION_STATIC expr_result_t expr_eval_math_max(const expr_result_t* list)
{
    if (list == nullptr)
        return NIL;

    expr_result_t max;
    for (size_t i = 0; i < array_size(list); ++i)
    {
        expr_result_t e = list[i];

        if (e.is_set() && e.index == NO_INDEX)
            e = expr_eval_math_max(e.list);
        else if (e.is_raw_array())
            e = expr_eval_raw_math_max(e.ptr, e.element_size(), e.element_count(), e.index);

        if (e.is_null(e.index))
            continue;

        if (e > max)
            max = e;
    }

    return max;
}

FOUNDATION_STATIC expr_result_t expr_eval_math_sum(const expr_result_t* list)
{
    if (list == nullptr)
        return NIL;

    expr_result_t sum(0.0);
    for (size_t i = 0; i < array_size(list); ++i)
    {
        expr_result_t e = list[i];

        if (e.is_set() && e.index == NO_INDEX)
            e = expr_eval_math_sum(e.list);
        else if (e.is_raw_array())
            e = expr_eval_raw_math_sum(e.ptr, e.element_size(), e.element_count(), e.index);

        if (e.is_null(e.index))
            continue;

        sum += e;
    }

    return sum;
}

FOUNDATION_STATIC expr_result_t expr_eval_math_avg(const expr_result_t* list)
{
    if (list == nullptr)
        return NIL;

    expr_result_t sum;
    size_t element_count = 0;
    for (size_t i = 0; i < array_size(list); ++i)
    {
        expr_result_t e = list[i];

        if (e.is_set() && e.index == NO_INDEX)
            e = expr_eval_math_avg(e.list);
        else if (e.is_raw_array())
            e = expr_eval_raw_math_avg(e.ptr, e.element_size(), e.element_count(), e.index);

        if (e.is_null(e.index))
            continue;

        sum += e;
        element_count++;
    }

    return sum / (expr_result_t)(double)element_count;
}

FOUNDATION_STATIC expr_result_t expr_eval_math_count(const expr_result_t* list)
{
    if (list == nullptr)
        return NIL;

    expr_result_t element_count(0.0);
    for (size_t i = 0; i < array_size(list); ++i)
    {
        expr_result_t e = list[i];

        if (e.is_set() && e.index == NO_INDEX)
            element_count += expr_eval_math_count(e.list);
        else if (e.is_raw_array())
            element_count += (expr_result_t)(double)e.element_count();
        else
            element_count.value++;
    }

    return element_count;
}

FOUNDATION_STATIC const expr_result_t* expr_eval_expand_args(vec_expr_t* args)
{
    if (args == nullptr)
        return nullptr;

    int arg_index = 0;
    expr_result_t* list = nullptr;
    if (args->len == 1 && (args->buf[arg_index].type == OP_SET || args->buf[arg_index].type == OP_FUNC))
    {
        expr_result_t fexpr = expr_eval(&args->buf[arg_index++]);
        if (fexpr.is_set())
            return fexpr.list;
        array_push(list, fexpr);
    }

    for (; arg_index < args->len; ++arg_index)
    {
        const expr_result_t& e = expr_eval(&vec_nth(args, arg_index));
        array_push(list, e);
    }

    return expr_eval_list(list);
}

FOUNDATION_STATIC expr_result_t expr_eval_math_min(const expr_func_t* f, vec_expr_t* args, void* c)
{
    if (args == nullptr)
        return NIL;

    return expr_eval_math_min(expr_eval_expand_args(args));
}

FOUNDATION_STATIC expr_result_t expr_eval_math_max(const expr_func_t* f, vec_expr_t* args, void* c)
{
    if (args == nullptr)
        return NIL;

    return expr_eval_math_max(expr_eval_expand_args(args));
}

FOUNDATION_STATIC expr_result_t expr_eval_math_sum(const expr_func_t* f, vec_expr_t* args, void* c)
{
    if (args == nullptr)
        return NIL;

    return expr_eval_math_sum(expr_eval_expand_args(args));
}

FOUNDATION_STATIC expr_result_t expr_eval_math_avg(const expr_func_t* f, vec_expr_t* args, void* c)
{
    if (args == nullptr)
        return NIL;

    return expr_eval_math_avg(expr_eval_expand_args(args));
}

FOUNDATION_STATIC expr_result_t expr_eval_math_count(const expr_func_t* f, vec_expr_t* args, void* c)
{
    if (args == nullptr)
        return NIL;

    return expr_eval_math_count(expr_eval_expand_args(args));
}

FOUNDATION_STATIC expr_result_t expr_eval_ceil(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: CEIL(1.2345) == 2.0

    if (args == nullptr || args->len != 1)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    return (double)math_ceil(expr_eval(&args->buf[0]).as_number());
}

FOUNDATION_STATIC expr_result_t expr_eval_floor(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: FLOOR(1.2345) == 1.0

    if (args == nullptr || args->len != 1)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    return (double)math_floor(expr_eval(&args->buf[0]).as_number());
}

FOUNDATION_STATIC expr_result_t expr_eval_random(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: RANDOM(5) => [0..5[
    //           RANDOM() => [0..1[
    //           RANDOM(4, 77) => [4..77[

    if (args == nullptr || args->len > 2)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    if (args->len == 0)
        return random_normalized();

    if (args->len == 1)
        return random_range(0, expr_eval(&args->buf[0]).as_number());

    return random_range(expr_eval(&args->buf[0]).as_number(), expr_eval(&args->buf[1]).as_number());
}

FOUNDATION_STATIC void expr_array_sort(expr_result_t* elements, bool (*comparer)(const expr_result_t& a, const expr_result_t& b, bool ascending, size_t vindex), bool ascending, size_t vindex)
{
    if (elements == nullptr)
        return;

    const int len = array_size(elements);
    for (int i = 0; i < len - 1; ++i)
    {
        for (int j = 0; j < len - i - 1; ++j)
        {
            if (!comparer(elements[j], elements[j + 1], ascending, vindex))
                std::swap(elements[j], elements[j + 1]);
        }
    }
}

FOUNDATION_STATIC bool expr_sort_results_comparer(const expr_result_t& a, const expr_result_t& b, bool ascending, size_t vindex)
{
    if (a.type == EXPR_RESULT_ARRAY && vindex == SIZE_MAX)
        expr_array_sort((expr_result_t*)a.list, expr_sort_results_comparer, ascending, vindex);

    if (b.type == EXPR_RESULT_ARRAY && vindex == SIZE_MAX)
        expr_array_sort((expr_result_t*)b.list, expr_sort_results_comparer, ascending, vindex);

    if (a.type == EXPR_RESULT_SYMBOL && b.type == EXPR_RESULT_NUMBER)
        return ascending;

    if (a.type == EXPR_RESULT_SYMBOL)
    {
        string_const_t sa = a.as_string();
        string_const_t sb = b.as_string();
        const bool cless = string_compare_less(STRING_ARGS(sa), STRING_ARGS(sb));
        if (ascending)
            return cless;
        return !cless;
    }

    const double n1 = a.as_number(DNAN, vindex == SIZE_MAX ? 0 : vindex);
    const double n2 = b.as_number(DNAN, vindex == SIZE_MAX ? 0 : vindex);
    if (ascending)
        return n1 < n2;
    return n1 >= n2;
}

FOUNDATION_STATIC expr_result_t expr_eval_sort(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: SORT([2, 1, 3]) => [1, 2, 3]
    //           SORT([33, 1, 0, true, 6, [2, 14]], 1, 1) == [0, 1, 6, [2, 14], 33, true]
    //           SORT(R(_300K, ps), DESC, 1)
    //           MAP(SORT(R(_300K, change_p), DESC, 1), INDEX($1, 0))

    if (args == nullptr || args->len < 1 || args->len > 3)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    // Get first argument elements
    expr_result_t elements = expr_eval(args->get(0));
    if (!elements.is_set())
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "First argument must be a set");

    size_t vindex = SIZE_MAX;
    bool ascending = true;
    if (args->len >= 2)
    {
        string_const_t sort_dir_string = CTEXT("ASC");
        auto sort_dir_arg = args->get(1);
        if (sort_dir_arg->type == OP_VAR)
            sort_dir_string = sort_dir_arg->token;
        else
        {
            auto sort_dir_result = expr_eval(sort_dir_arg);
            if (sort_dir_result.type == EXPR_RESULT_SYMBOL)
                sort_dir_string = sort_dir_result.as_string();
            else if (sort_dir_result.as_number() == 0)
                sort_dir_string = CTEXT("DESC");
        }

        if (string_equal_nocase(STRING_ARGS(sort_dir_string), STRING_CONST("ASC")))
            ascending = true;
        else if (string_equal_nocase(STRING_ARGS(sort_dir_string), STRING_CONST("DESC")))
            ascending = false;
        else
            throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Sort direction `%.*s` not supported", STRING_FORMAT(sort_dir_string));

        if (args->len == 3)
            vindex = expr_eval(args->get(2)).as_number(0);
    }

    // Sort elements
    expr_array_sort((expr_result_t*)elements.list, expr_sort_results_comparer, ascending, vindex);

    return elements;
}

FOUNDATION_STATIC expr_result_t expr_eval_reduce(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: REDUCE([1, 2, 3], ADD($0, $1))
    //           REDUCE([1, 2, 3], ADD(), 5) == 11

    if (args == nullptr || args->len < 2 || args->len > 3)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    // Make sure first argument is a set
    expr_result_t elements = expr_eval(&args->buf[0]);
    if (!elements.is_set())
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "First argument must be a set");

    // Evaluate function
    expr_result_t result{ elements.element_at(0).type };
    if (args->len == 3)
        result = expr_eval(args->get(2));

    // Loop on all elements and invoke function
    for (auto e : elements)
    {
        expr_var_t* vr = eval_get_or_create_global_var(STRING_CONST("$0"));
        vr->value = result;

        expr_var_t* ve = eval_get_or_create_global_var(STRING_CONST("$1"));
        ve->value = e;

        if (args->buf[1].type == OP_FUNC)
        {
            vec_expr_t fargs;
            for (int i = 0; i < args->buf[1].args.len; ++i)
            {
                auto& p = args->buf[1].args.buf[i];
                expr_t vexpr = expr_init(OP_CONST, STRING_ARGS(p.token));
                vexpr.param.result.value = expr_eval(&p);
                vec_push(&fargs, vexpr);
            }

            {
                expr_t vexpr = expr_init(OP_CONST, STRING_CONST("RESULT"));
                vexpr.param.result.value = result;
                vec_push(&fargs, vexpr);
            }

            {
                expr_t vexpr = expr_init(OP_CONST, STRING_CONST("ELEMENT"));
                vexpr.param.result.value = e;
                vec_push(&fargs, vexpr);
            }

            auto fn = args->buf[1].param.func;
            result = fn.f->handler(fn.f, &fargs, fn.context ? fn.context : c);

            vec_free(&fargs);
        }
        else
        {
            result = expr_eval(args->get(1));
        }
    }

    return result;
}

FOUNDATION_STATIC expr_result_t expr_eval_repeat(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: REPEAT(RANDOM($i, $count), 5)

    if (args == nullptr || args->len == 0 || args->len > 2)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    expr_result_t* results = nullptr;
    const int repeat_count = math_round(expr_eval(&args->buf[1]).as_number());

    expr_var_t* v = eval_get_or_create_global_var(STRING_CONST("$count"));
    v->value = expr_result_t((double)repeat_count);

    for (int i = 0; i < repeat_count; ++i)
    {
        expr_var_t* vi = eval_get_or_create_global_var(STRING_CONST("$i"));
        vi->value = expr_result_t((double)i);

        expr_result_t r = expr_eval(&args->buf[0]);
        array_push_memcpy(results, &r);
    }

    return expr_eval_list(results);
}

FOUNDATION_STATIC expr_result_t expr_eval_round(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: ROUND(1.2345) == 1.0
    //           ROUND(1.2345, 2) == 1.23
    //           ROUND(144.23455567, -2) == 100

    if (args == nullptr || args->len < 1 || args->len > 2)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    const double r = expr_eval(&args->buf[0]).as_number();
    if (args->len == 1)
        return (double)math_round(r);

    // Round ##r at decimal place ##round_at
    const double round_at = expr_eval(&args->buf[1]).as_number(0);
    const double rpow = math_pow(10.0, round_at);
    return (double)math_round(r * rpow) / rpow;
}

FOUNDATION_STATIC expr_result_t expr_eval_inline(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: EVAL(1+1, 2+2, 3+3) == [2, 4, 6]
    //       

    if (args == nullptr || args->len < 1)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    if (args->len == 1)
        return expr_eval(&args->buf[0]);

    expr_result_t* results = nullptr;
    for (int i = 0; i < args->len; ++i)
    {
        const auto& r = expr_eval(&args->buf[i]);
        array_push_memcpy(results, &r);
    }
    return expr_eval_list(results);
}

FOUNDATION_STATIC expr_result_t expr_eval_filter(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: FILTER([1, 2, 3], EVAL($1 >= 3)) == [3]
    //           FILTER([2, 1, 4, 5, 0, 55, 6], $1 > 3) == [4, 5, 55, 6]
    //           SUM(INDEX(FILTER(R(_300K, day), INDEX($0, 1) > 0), 1))
    //           SUM(MAP(FILTER(R(_300K, day), INDEX($0, 1) > 0), INDEX($0, 1)))

    if (args == nullptr || args->len != 2)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    expr_result_t elements = expr_eval(&args->buf[0]);
    if (!elements.is_set())
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "First argument must be a result set");

    expr_result_t* results = nullptr;
    for (auto e : elements)
    {
        if (!e.is_set())
        {
            eval_set_or_create_global_var(STRING_CONST("$1"), e);
        }
        else
        {
            char varname[4];
            int i = 1;
            for (auto m : e)
            {
                string_t macro = string_format(STRING_CONST_CAPACITY(varname), STRING_CONST("$%d"), i);
                eval_set_or_create_global_var(STRING_ARGS(macro), m);
                i++;
            }
        }

        expr_result_t r = expr_eval(&args->buf[1]);
        if (r.type == EXPR_RESULT_FALSE)
            continue;
        if (r.type == EXPR_RESULT_TRUE || r.as_number() != 0)
            array_push_memcpy(results, &e);
    }

    return expr_eval_list(results);
}

FOUNDATION_STATIC expr_result_t expr_eval_map(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Examples: MAP([[a, 1], [b, 2], [c, 3]], $2) == [1, 2, 3]
    //           MAP([[a, 1], [b, 2], [c, 3]], ADD($0, $2)) == [1, 3, 6]
    //           SUM(MAP(R("300K", day), $1))
    //           MAP(FILTER(S(U.US, close, ALL), $2 > 60), [DATESTR($1), $2])

    if (args == nullptr || args->len != 2)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    expr_result_t elements = expr_eval(&args->buf[0]);
    if (!elements.is_set())
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "First argument must be a result set");

    expr_result_t* results = nullptr;
    for (auto e : elements)
    {
        if (!e.is_set())
        {
            eval_set_or_create_global_var(STRING_CONST("$1"), e);
        }
        else
        {
            char varname[4];
            int i = 1;
            for (auto m : e)
            {
                string_t macro = string_format(STRING_CONST_CAPACITY(varname), STRING_CONST("$%d"), i);
                eval_set_or_create_global_var(STRING_ARGS(macro), m);
                i++;
            }
        }

        expr_result_t r = expr_eval(&args->buf[1]);
        array_push_memcpy(results, &r);
    }

    return expr_eval_list(results);
}

FOUNDATION_STATIC expr_result_t expr_eval_array_index(const expr_func_t* f, vec_expr_t* args, void* c)
{
    if (args == nullptr || args->len < 1)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid arguments");

    expr_result_t arr = expr_eval(&args->buf[0]);
    if (!arr.is_set())
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Nothing to index (%d)", arr.type);

    for (int i = 1; i < args->len; ++i)
    {
        const expr_result_t& e_index_value = expr_eval(&args->buf[i]);
        const double index_value = e_index_value.as_number(DNAN);
        if (math_real_is_nan(index_value))
            throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid index `%.*s` (%d)", STRING_FORMAT(args->buf[i].token), i);

        expr_result_t elm = arr.element_at((unsigned int)index_value);
        if (!elm.is_set() || (i + 1) >= args->len)
            return elm;

        arr = elm;
    }

    return arr;
}

expr_result_t expr_error(expr_error_code_t err_code, expr_string_t expr_string, const char* token_pos, const char* err_msg, ...)
{
    EXPR_ERROR_CODE = err_code;
    va_list list;
    va_start(list, err_msg);
    string_t formatted_error_message = string_vformat(STRING_CONST_CAPACITY(EXPR_ERROR_MSG), err_msg, string_length(err_msg), list);
    va_end(list);

    if (log_handler() == nullptr)
    {
        log_errorf(HASH_EXPR, ERROR_SCRIPT, STRING_CONST("[%d] %.*s -> %.*s:%ld"),
            err_code, STRING_FORMAT(formatted_error_message), STRING_FORMAT(expr_string), token_pos ? (token_pos - expr_string.str) : 0);
    }

    return NAN;
}

string_const_t expr_result_to_string(const expr_result_t& result, const char* fmt /*= "%.6g"*/)
{
    return result.as_string(fmt);
}

FOUNDATION_STATIC int expr_eval_format_date_range_label(double value, char* buff, int size, void* user_data)
{
    expr_evaluator_t* ev = (expr_evaluator_t*)user_data;
    if (math_real_is_nan(value))
        return 0;

    const double diff = (double)ev->last_run_time - (time_t)value;
    value = diff / time_one_day();

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
    else if (value >= 1)
    {
        value = math_round(value);
        return (int)string_format(buff, size, STRING_CONST("%.0lfD"), value).length;
    }
    else if (value >= 0.042)
    {
        value = math_round(value * 24);
        return (int)string_format(buff, size, STRING_CONST("%.0lfH"), value).length;
    }

    value = math_round(value * 24 * 60);
    return (int)string_format(buff, size, STRING_CONST("%.3g mins."), value).length;
}

const char* expr_error_cstr(int error_code)
{
    switch (error_code)
    {
    case EXPR_ERROR_ALLOCATION_FAILED: return "Allocation failed";
    case EXPR_ERROR_UNEXPECTED_NUMBER: return "Unexpected number";
    case EXPR_ERROR_UNEXPECTED_WORD: return "Unexpected word";
    case EXPR_ERROR_UNEXPECTED_PARENTHESIS: return "Unexpected parenthesis";
    case EXPR_ERROR_MISSING_OPERAND: return "Missing operand";
    case EXPR_ERROR_UNKNOWN_OPERATOR: return "Unknown operator";
    case EXPR_ERROR_STRING_LITERAL_NOT_CLOSED: return "Missing closing \" for string literal";
    case EXPR_ERROR_EVALUATION_TIMEOUT: return "Evaluation timeout";
    case EXPR_ERROR_EVALUATION_NOT_IMPLEMENTED: return "Evaluation not implemented";
    case EXPR_ERROR_UNEXPECTED_SET: return "Unexpected set, i.e. {1, 2, 3}";
    }

    return "Unknown error";
}

FOUNDATION_FORCEINLINE int expr_is_unary(const expr_type_t op)
{
    return op == OP_UNARY_MINUS || op == OP_UNARY_LOGICAL_NOT || op == OP_UNARY_BITWISE_NOT;
}

FOUNDATION_FORCEINLINE int expr_is_binary(const expr_type_t op)
{
    return !expr_is_unary(op) && op != OP_CONST && op != OP_VAR && op != OP_FUNC && op != OP_SET && op != OP_UNKNOWN;
}

FOUNDATION_FORCEINLINE int expr_prec(const expr_type_t a, const expr_type_t b)
{
    static constexpr const int prec[] = { 0, 1, 1, 1, 2, 2, 2, 2,  3,  3,  4,  4, 5, 5,
                                          5, 5, 5, 5, 6, 7, 8, 9, 10, 11, 12/*OP_COMMA*/,  0, 0, 0, 0 };
    FOUNDATION_ASSERT(ARRAY_COUNT(prec) == OP_COUNT);
    int left = expr_is_binary(a) && a != OP_ASSIGN && a != OP_POWER && a != OP_COMMA;
    return (left && prec[a] >= prec[b]) || (prec[a] > prec[b]);
}

static const expr_type_t expr_op(const char* s, size_t len, int unary)
{
    for (size_t i = 0, end = sizeof(OPS) / sizeof(OPS[0]); i != end; ++i)
    {
        if (string_equal(STRING_ARGS(OPS[i].token), s, len) && (unary == -1 || expr_is_unary(OPS[i].op) == unary))
            return OPS[i].op;
    }
    return OP_UNKNOWN;
}

static double expr_parse_number(const char* s, size_t len)
{
    double num = 0;
    unsigned int frac = 0;
    unsigned int digits = 0;

    num = string_to_float64(s, len);
    if (!math_real_is_nan(num) && !math_real_is_zero(num))
        return num;

    for (unsigned int i = 0; i < len; i++) {
        if (s[i] == '.' && frac == 0) {
            frac++;
            continue;
        }
        if (isdigit(s[i])) {
            digits++;
            if (frac > 0) {
                frac++;
            }
            num = num * 10 + (s[i] - '0');
        }
        else {
            return NAN;
        }
    }
    while (frac > 1) {
        num = num / 10;
        frac--;
    }
    return (digits > 0 ? num : NAN);
}

/*
 * Functions
 */

static expr_func_t* expr_func(expr_func_t* funcs, const char* s, size_t len)
{
    for (expr_func_t* f = funcs; f->name.str; f++)
    {
        if (string_equal_nocase(STRING_ARGS(f->name), s, len))
            return f;
    }
    return NULL;
}

/*
 * Variables
 */

static expr_var_t* expr_var(expr_var_list_t* vars, const char* s, size_t len)
{
    expr_var_t* v = NULL;

    if (len > 2 && ((*s == '"' && s[len - 1] == '"') || (*s == '\'' && s[len - 1] == '\'')))
    {
        s++;
        len -= 2;
    }
    else if (len == 0 || !isfirstvarchr(*s)) {
        return NULL;
    }

    for (v = vars->head; v; v = v->next)
    {
        if (string_equal(STRING_ARGS(v->name), s, len))
            return v;
    }
    v = (expr_var_t*)memory_allocate(HASH_EXPR, sizeof(expr_var_t) + len + 1, 8, MEMORY_PERSISTENT);

    if (v == NULL)
    {
        log_errorf(HASH_EXPR, ERROR_OUT_OF_MEMORY, STRING_CONST("Failed to allocate memory for var %.*s"), (int)len, s);
        return NULL; /* allocation failed */
    }

    memset(v, 0, sizeof(expr_var_t) + len + 1);
    v->next = vars->head;
    v->name = string_copy((char*)v + sizeof(expr_var_t), len + 1, s, len);
    v->value = expr_result_t(EXPR_RESULT_SYMBOL, string_table_encode(v->name.str), v->name.length);
    vars->head = v;
    return v;
}

expr_result_t expr_eval_var(expr_t* e)
{
    const expr_result_t r = *e->param.var.value;
    if (r.is_null())
        log_debugf(HASH_EXPR, STRING_CONST("var '%.*s' is not defined"), STRING_FORMAT(e->token));
    return r;
}

expr_result_t expr_eval(expr_t* e)
{
    expr_result_t n;
    switch (e->type)
    {
    case OP_UNARY_MINUS:
        return -(expr_eval(&e->args.buf[0]));

    case OP_UNARY_LOGICAL_NOT:
        return !(expr_eval(&e->args.buf[0]));

    case OP_UNARY_BITWISE_NOT:
        return ~(expr_eval(&e->args.buf[0]));

    case OP_POWER:
        return math_pow(expr_eval(&e->args.buf[0]).as_number(), expr_eval(&e->args.buf[1]).as_number());

    case OP_MULTIPLY:
        return expr_eval(&e->args.buf[0]) * expr_eval(&e->args.buf[1]);

    case OP_DIVIDE:
        return expr_eval(&e->args.buf[0]) / expr_eval(&e->args.buf[1]);

    case OP_REMAINDER:
        return math_mod(expr_eval(&e->args.buf[0]).as_number(), expr_eval(&e->args.buf[1]).as_number());

    case OP_PLUS:
        return expr_eval(&e->args.buf[0]) + expr_eval(&e->args.buf[1]);

    case OP_MINUS:
        return expr_eval(&e->args.buf[0]) - expr_eval(&e->args.buf[1]);

    case OP_SHL:
        return expr_eval(&e->args.buf[0]) << expr_eval(&e->args.buf[1]);

    case OP_SHR:
        return expr_eval(&e->args.buf[0]) >> expr_eval(&e->args.buf[1]);

    case OP_LT:
        return expr_eval(&e->args.buf[0]) < expr_eval(&e->args.buf[1]);

    case OP_LE:
        return expr_eval(&e->args.buf[0]) <= expr_eval(&e->args.buf[1]);

    case OP_GT:
        return expr_eval(&e->args.buf[0]) > expr_eval(&e->args.buf[1]);

    case OP_GE:
        return expr_eval(&e->args.buf[0]) >= expr_eval(&e->args.buf[1]);

    case OP_EQ:
        return expr_eval(&e->args.buf[0]) == expr_eval(&e->args.buf[1]);

    case OP_NE:
        return expr_eval(&e->args.buf[0]) != expr_eval(&e->args.buf[1]);

    case OP_BITWISE_AND:
        return expr_eval(&e->args.buf[0]) & expr_eval(&e->args.buf[1]);

    case OP_BITWISE_OR:
        return expr_eval(&e->args.buf[0]) | expr_eval(&e->args.buf[1]);

    case OP_BITWISE_XOR:
        return expr_eval(&e->args.buf[0]) ^ expr_eval(&e->args.buf[1]);

    case OP_LOGICAL_AND:
        n = expr_eval(&e->args.buf[0]);
        if (!n.is_null()) {
            n = expr_eval(&e->args.buf[1]);
            if (!n.is_null()) {
                return expr_result_t(true);
            }
        }
        return expr_result_t(false);

    case OP_LOGICAL_OR:
        n = expr_eval(&e->args.buf[0]);
        if (!n.is_null()) {
            return expr_result_t(true);
        }
        else {
            n = expr_eval(&e->args.buf[1]);
            if (!n.is_null()) {
                return expr_result_t(true);
            }
        }
        return expr_result_t(false);

    case OP_ASSIGN:
        n = expr_eval(&e->args.buf[1]);
        if (vec_nth(&e->args, 0).type == OP_VAR) {
            *e->args.buf[0].param.var.value = n;
        }
        return n;
    case OP_COMMA:
        n = expr_eval(&e->args.buf[0]);
        // Exclude some patterns from returning a result set.
        if (e->args.buf[0].type == OP_ASSIGN && (e->args.buf[0].token.length == 0 || e->args.buf[0].args.buf[0].type == OP_VAR))
            return expr_eval(&e->args.buf[1]);
        return expr_eval_merge(n, expr_eval(&e->args.buf[1]), false);
    case OP_CONST:
        return e->param.result.value;

    case OP_VAR:
        return expr_eval_var(e);

    case OP_FUNC:
    {
        expr_result_t fn_result = e->param.func.f->handler(e->param.func.f, &e->args, e->param.func.context);
        expr_var_t* v = eval_get_or_create_global_var(STRING_CONST("$0"));
        v->value = fn_result;
        return fn_result;
    }

    case OP_SET:
        return expr_eval_set(e);

    default:
        expr_error(EXPR_ERROR_UNKNOWN_OPERATOR, e->token, nullptr, "Failed to evaluate operator %d", e->type);
        break;
    }

    return NAN;
}

static int expr_next_token(const char* s, size_t len, int& flags)
{
    unsigned int i = 0;
    if (len == 0) {
        return 0;
    }
    char c = s[0];
    if (c == '#') {
        for (; i < len && s[i] != '\n'; i++)
            ;
        return i;
    }
    else if (c == '\n') {
        for (; i < len && isspace(s[i]); i++)
            ;
        if (flags & EXPR_TOP) {
            if (i == len || s[i] == ')') {
                flags = flags & (~EXPR_COMMA);
            }
            else {
                flags = EXPR_TNUMBER | EXPR_TWORD | EXPR_TOPEN | EXPR_COMMA;
            }
        }
        return i;
    }
    else if (c > 0 && isspace(c)) {
        while (i < len && (s[i] > 0 && isspace(s[i])) && s[i] != '\n') {
            i++;
        }
        return i;
    }
    else if (c > 0 && isdigit(c)) {
        if ((flags & EXPR_TNUMBER) == 0) {
            return EXPR_ERROR_UNEXPECTED_NUMBER; // unexpected number
        }
        flags = EXPR_TOP | EXPR_TCLOSE;
        while ((c == '.' || (c > 0 && isdigit(c)) || (i == 1 && c == 'x')) && i < len) {
            i++;
            c = s[i];
        }
        return i;
    }
    else if (isfirstvarchr(c)) {
        if ((flags & EXPR_TWORD) == 0) {
            return EXPR_ERROR_UNEXPECTED_WORD; // unexpected word
        }
        flags = EXPR_TOP | EXPR_TOPEN | EXPR_TCLOSE;
        while ((isvarchr(c)) && i < len) {
            i++;
            c = s[i];
        }
        return i;
    }
    else if (c == '"' || c == '\'') { // parse var name literals such as "real-time" or 'schmidt'
        char end_token = c;
        ++i;
        if (i >= len)
            return EXPR_ERROR_STRING_LITERAL_NOT_CLOSED; // unexpected word

        c = s[i];
        while (c != end_token)
        {
            i++;
            c = s[i];
            if (i > len)
                return EXPR_ERROR_STRING_LITERAL_NOT_CLOSED; // unexpected word
        }
        i++;
        if (i > len)
            return EXPR_ERROR_STRING_LITERAL_NOT_CLOSED; // unexpected word
        flags = EXPR_TWORD | EXPR_TOP | EXPR_TCLOSE;
        return i;
    }
    else if (c == '(' || c == ')') {
        if (c == '(' && (flags & EXPR_TOPEN) != 0) {
            flags = EXPR_TNUMBER | EXPR_TWORD | EXPR_TOPEN | EXPR_TCLOSE;
        }
        else if (c == ')' && (flags & EXPR_TCLOSE) != 0) {
            flags = EXPR_TOP | EXPR_TCLOSE;
        }
        else {
            return EXPR_ERROR_UNEXPECTED_PARENTHESIS; // unexpected parenthesis
        }
        return 1;
    }
    else if (c == '[' || c == ']')
    {
        // Parse {...} as a set of values
        if (c == '[' && (flags & EXPR_TOPEN) != 0) {
            flags = EXPR_SET | EXPR_TNUMBER | EXPR_TWORD | EXPR_TOPEN | EXPR_TCLOSE;
        }
        else if (c == ']' && (flags & EXPR_TCLOSE) != 0) {
            flags = EXPR_TOP | EXPR_TCLOSE;
        }
        else {
            return EXPR_ERROR_UNEXPECTED_SET; // unexpected parenthesis
        }
        return 1;
    }
    else {
        if ((flags & EXPR_TOP) == 0) {
            if (expr_op(&c, 1, 1) == OP_UNKNOWN) {
                return EXPR_ERROR_MISSING_OPERAND; // missing expected operand
            }
            flags = EXPR_TNUMBER | EXPR_TWORD | EXPR_TOPEN | EXPR_UNARY;
            return 1;
        }
        else {
            int found = 0;
            while (!isvarchr(c) && !isspace(c) && c != '(' && c != ')' && i < len) {
                if (expr_op(s, i + 1, 0) != OP_UNKNOWN) {
                    found = 1;
                }
                else if (found) {
                    break;
                }
                i++;
                c = s[i];
            }
            if (!found) {
                return EXPR_ERROR_UNKNOWN_OPERATOR; // unknown operator
            }
            flags = EXPR_TNUMBER | EXPR_TWORD | EXPR_TOPEN;
            return i;
        }
    }
}

static int expr_bind(const char* s, size_t len, vec_expr_t* es)
{
    const expr_type_t op = expr_op(s, len, -1);
    if (op == OP_UNKNOWN) {
        return -1;
    }

    if (expr_is_unary(op)) {
        if (vec_len(es) < 1) {
            return -1;
        }
        expr_t arg = vec_pop(es);
        expr_t unary = expr_init(op);
        unary.type = op;
        vec_push(&unary.args, arg);
        vec_push(es, unary);
    }
    else {
        if (vec_len(es) < 2) {
            return -1;
        }
        expr_t b = vec_pop(es);
        expr_t a = vec_pop(es);
        expr_t binary = expr_init(op, s, len);
        if (op == OP_ASSIGN && a.type != OP_VAR) {
            return -1; /* Bad assignment */
        }
        vec_push(&binary.args, a);
        vec_push(&binary.args, b);
        vec_push(es, binary);
    }
    return 0;
}

static expr_t expr_const(const expr_result_t& value, const char* s, size_t len)
{
    expr_t e = expr_init(OP_CONST, s, len);
    e.param.result.value = value;
    return e;
}

static expr_t expr_varref(expr_var_t* v)
{
    expr_t e = expr_init(OP_VAR, STRING_ARGS(v->name));
    e.param.var.value = &v->value;
    return e;
}

static expr_t expr_binary(const expr_type_t type, expr_t a, expr_t b) {
    expr_t e = expr_init(type);
    vec_push(&e.args, a);
    vec_push(&e.args, b);
    return e;
}

static inline void expr_copy(expr_t* dst, expr_t* src)
{
    int i;
    expr_t arg = expr_init(OP_UNKNOWN);
    dst->type = src->type;
    if (src->type == OP_FUNC) {
        dst->param.func.f = src->param.func.f;
        vec_foreach(&src->args, arg, i) {
            expr_t tmp = expr_init(OP_UNKNOWN);
            expr_copy(&tmp, &arg);
            vec_push(&dst->args, tmp);
        }
        if (src->param.func.f->ctxsz > 0)
        {
            dst->param.func.context = memory_allocate(HASH_EXPR, src->param.func.f->ctxsz, 8, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
        }
    }
    else if (src->type == OP_CONST) {
        dst->param.result.value = src->param.result.value;
    }
    else if (src->type == OP_VAR) {
        dst->param.var = src->param.var;
    }
    else {
        vec_foreach(&src->args, arg, i) {
            expr_t tmp = expr_init(OP_UNKNOWN);
            expr_copy(&tmp, &arg);
            vec_push(&dst->args, tmp);
        }
    }
}

static void expr_destroy_args(expr_t* e)
{
    int i;
    expr_t arg = expr_init(OP_UNKNOWN);
    if (e->type == OP_FUNC) {
        vec_foreach(&e->args, arg, i) { expr_destroy_args(&arg); }
        vec_free(&e->args);
        if (e->param.func.context != NULL) {
            if (e->param.func.f->cleanup != NULL) {
                e->param.func.f->cleanup(e->param.func.f, e->param.func.context);
            }
            memory_deallocate(e->param.func.context);
        }
    }
    else if (e->type != OP_CONST && e->type != OP_VAR) {
        vec_foreach(&e->args, arg, i) { expr_destroy_args(&arg); }
        vec_free(&e->args);
    }
}

expr_t* expr_create(const char* s, size_t len, expr_var_list_t* vars, expr_func_t* funcs)
{
    expr_result_t value;
    expr_var_t* v;
    const char* id = NULL;
    size_t idn = 0;

    expr_t* result = NULL;

    vec_expr_t es;
    vec_str_t os;
    vec_arg_t as;
    vec_macro_t macros;

    EXPR_ERROR_CODE = EXPR_ERROR_NONE;
    const expr_string_t expr_string{ s, len };

    int flags = EXPR_TDEFAULT;
    int paren = EXPR_PAREN_ALLOWED;
    for (;;) {
        int n = expr_next_token(s, len, flags);
        if (n == 0) {
            break;
        }
        else if (n < 0) {
            expr_error((expr_error_code_t)n, expr_string, s, "%s '%c' at %.*s",
                expr_error_cstr(n), *s, min(7ULL, len - 1ULL), max(s - 1, expr_string.str));
            goto cleanup;
        }
        const char* tok = s;
        s = s + n;
        len = len - n;
        if (*tok == '#') {
            continue;
        }
        if (flags & EXPR_UNARY) {
            if (n == 1) {
                switch (*tok) {
                case '-': tok = "-u"; break;
                case '^': tok = "^u"; break;
                case '!': tok = "!u"; break;
                default:
                    goto cleanup;
                }
                n = 2;
            }
        }
        if (*tok == '\n' && (flags & EXPR_COMMA)) {
            flags = flags & (~EXPR_COMMA);
            n = 1;
            tok = ",";
        }
        if (*tok > 0 && isspace(*tok)) {
            continue;
        }
        int paren_next = EXPR_PAREN_ALLOWED;

        if (idn > 0) {
            if (n == 1 && *tok == '(') {
                int i;
                int has_macro = 0;
                expr_macro_t m;
                vec_foreach(&macros, m, i) {
                    if (string_equal(STRING_ARGS(m.name), id, idn))
                    {
                        has_macro = 1;
                        break;
                    }
                }
                if ((idn == 1 && id[0] == '$') || has_macro || expr_func(funcs, id, idn) != NULL) {
                    expr_string_t str = { id, idn };
                    vec_push(&os, str);
                    paren = EXPR_PAREN_EXPECTED;
                }
                else {
                    expr_error(EXPR_ERROR_INVALID_FUNCTION_NAME, expr_string, tok - idn, "Invalid function name '%.*s'", idn, id);
                    goto cleanup; /* invalid function name */
                }
            }
            else if ((v = expr_var(vars, id, idn)) != NULL) {
                vec_push(&es, expr_varref(v));
                paren = EXPR_PAREN_FORBIDDEN;
            }
            id = NULL;
            idn = 0;
        }

        if (n == 1 && (*tok == '(' || *tok == '[')) {
            if ((flags & EXPR_SET) || paren == EXPR_PAREN_EXPECTED) {
                if (flags & EXPR_SET)
                {
                    expr_string_t str = { "SET", 3 };
                    vec_push(&os, str);
                    flags &= ~EXPR_SET;
                }
                expr_string_t str = { "{", 1 };
                vec_push(&os, str);
                expr_arg_t arg = { vec_len(&os), vec_len(&es) };
                vec_push(&as, arg);
            }
            else if (paren == EXPR_PAREN_ALLOWED) {
                expr_string_t str = { "(", 1 };
                vec_push(&os, str);
            }
            else {
                goto cleanup; // Bad call
            }
        }
        else if (paren == EXPR_PAREN_EXPECTED) {
            goto cleanup; // Bad call
        }
        else if (n == 1 && (*tok == ')' || *tok == ']'))
        {
            int minlen = (vec_len(&as) > 0 ? vec_peek(&as).oslen : 0);
            while (vec_len(&os) > minlen && *vec_peek(&os).str != '(' && *vec_peek(&os).str != '{')
            {
                expr_string_t str = vec_pop(&os);
                if (expr_bind(STRING_ARGS(str), &es) == -1)
                    goto cleanup;
            }
            if (vec_len(&os) == 0)
            {
                expr_error(EXPR_ERROR_BAD_PARENS, expr_string, tok, "Invalid parentheses");
                goto cleanup; // Bad parens
            }
            expr_string_t str = vec_pop(&os);
            if (str.length == 1 && *str.str == '{')
            {
                str = vec_pop(&os);
                expr_arg_t arg = vec_pop(&as);
                if (vec_len(&es) > arg.eslen) {
                    vec_push(&arg.args, vec_pop(&es));
                }
                if (str.length == 1 && str.str[0] == '$') {
                    if (vec_len(&arg.args) < 1) {
                        vec_free(&arg.args);
                        goto cleanup; /* too few arguments for $() function */
                    }
                    expr_t* u = &vec_nth(&arg.args, 0);
                    if (u->type != OP_VAR) {
                        vec_free(&arg.args);

                        expr_error(EXPR_ERROR_INVALID_ARGUMENT, expr_string, str.str, "First argument is not a variable");
                        goto cleanup; /* first argument is not a variable */
                    }
                    for (expr_var_t* vv = vars->head; vv; vv = vv->next) {
                        if (&vv->value == u->param.var.value) {
                            expr_macro_t m = { string_to_const(vv->name), arg.args };
                            vec_push(&macros, m);
                            break;
                        }
                    }
                    vec_push(&es, expr_const(EXPR_ZERO));
                }
                else {
                    int i = 0;
                    int found = -1;
                    expr_macro_t m;
                    vec_foreach(&macros, m, i)
                    {
                        if (string_equal(STRING_ARGS(m.name), STRING_ARGS(str)))
                        {
                            found = i;
                        }
                    }
                    if (found != -1) {
                        m = vec_nth(&macros, found);
                        expr_t root = expr_const(EXPR_ZERO);
                        expr_t* p = &root;
                        /* Assign macro parameters */
                        for (int j = 0; j < vec_len(&arg.args); j++) {
                            char varname[4];
                            string_format(STRING_CONST_CAPACITY(varname), STRING_CONST("$%d"), (j + 1));
                            expr_var_t* vv = expr_var(vars, varname, string_length(varname));
                            expr_t ev = expr_varref(vv);
                            expr_t assign =
                                expr_binary(OP_ASSIGN, ev, vec_nth(&arg.args, j));
                            *p = expr_binary(OP_COMMA, assign, expr_const(EXPR_ZERO));
                            p = &vec_nth(&p->args, 1);
                        }
                        /* Expand macro body */
                        for (int j = 1; j < vec_len(&m.body); j++) {
                            if (j < vec_len(&m.body) - 1) {
                                *p = expr_binary(OP_COMMA, expr_const(EXPR_ZERO), expr_const(EXPR_ZERO));
                                expr_copy(&vec_nth(&p->args, 0), &vec_nth(&m.body, j));
                            }
                            else {
                                expr_copy(p, &vec_nth(&m.body, j));
                            }
                            p = &vec_nth(&p->args, 1);
                        }
                        vec_push(&es, root);
                        vec_free(&arg.args);
                    }
                    else if (string_equal(STRING_ARGS(str), STRING_CONST("SET")))
                    {
                        expr_t bound_set = expr_init(OP_SET);
                        bound_set.args = arg.args;
                        vec_push(&es, bound_set);
                    }
                    else {
                        expr_func_t* f = expr_func(funcs, STRING_ARGS(str));
                        expr_t bound_func = expr_init(OP_FUNC);
                        bound_func.param.func.f = f;
                        bound_func.args = arg.args;
                        bound_func.token = {str.str, (tok - str.str) + 1ULL };
                        if (f->ctxsz > 0)
                        {
                            void* p = memory_allocate(HASH_EXPR, f->ctxsz, 8, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
                            if (p == NULL) {
                                goto cleanup; /* allocation failed */
                            }
                            bound_func.param.func.context = p;
                        }
                        vec_push(&es, bound_func);
                    }
                }
            }
            paren_next = EXPR_PAREN_FORBIDDEN;
        }
        else if (!(value = expr_parse_number(tok, n)).is_null())
        {
            vec_push(&es, expr_const(value, tok, n));
            paren_next = EXPR_PAREN_FORBIDDEN;
        }
        else if (expr_op(tok, n, -1) != OP_UNKNOWN)
        {
            const expr_type_t op = expr_op(tok, n, -1);
            expr_string_t o2 = { NULL, 0 };
            if (vec_len(&os) > 0) {
                o2 = vec_peek(&os);
            }
            for (;;) {
                if (n == 1 && *tok == ',' && vec_len(&os) > 0) {
                    expr_string_t str = vec_peek(&os);
                    if (str.length == 1 && *str.str == '{')
                    {
                        expr_t e = vec_pop(&es);
                        vec_push(&vec_peek(&as).args, e);
                        break;
                    }
                }
                const expr_type_t type2 = expr_op(STRING_ARGS(o2), -1);
                if (!(type2 != OP_UNKNOWN && expr_prec(op, type2))) {
                    expr_string_t str = { tok, (size_t)n };
                    vec_push(&os, str);
                    break;
                }

                if (expr_bind(STRING_ARGS(o2), &es) == -1) {
                    goto cleanup;
                }
                (void)vec_pop(&os);
                if (vec_len(&os) > 0) {
                    o2 = vec_peek(&os);
                }
                else {
                    o2.length = 0;
                }
            }
        }
        else {
            if (n > 0 && !isdigit(*tok)) {
                /* Valid identifier, a variable or a function */
                id = tok;
                idn = n;
            }
            else {
                expr_error(EXPR_ERROR_BAD_VARIABLE_NAME, expr_string, tok, "Bad variable name %.*s", n, tok);
                goto cleanup; // Bad variable name, e.g. '2.3.4' or '4ever'
            }
        }
        paren = paren_next;
    }

    if (idn > 0) {
        vec_push(&es, expr_varref(expr_var(vars, id, idn)));
    }

    while (vec_len(&os) > 0) {
        expr_string_t rest = vec_pop(&os);
        if (rest.length == 1 && (*rest.str == '(' || *rest.str == ')')) {
            goto cleanup; // Bad paren
        }
        if (expr_bind(STRING_ARGS(rest), &es) == -1) {
            expr_error(EXPR_ERROR_BAD_PARENS, expr_string, nullptr, "Invalid closing operator %.*s", STRING_FORMAT(rest));
            goto cleanup;
        }
    }

    result = (expr_t*)memory_allocate(HASH_EXPR, sizeof(expr_t), 8, MEMORY_PERSISTENT);
    if (result != NULL) {
        if (vec_len(&es) == 0) {
            result->type = OP_CONST;
        }
        else {
            *result = vec_pop(&es);
        }
    }

cleanup:

    int i, j;
    expr_macro_t m;
    vec_foreach(&macros, m, i) {
        expr_t e = expr_init(OP_UNKNOWN);
        vec_foreach(&m.body, e, j) { expr_destroy_args(&e); }
        vec_free(&m.body);
    }
    vec_free(&macros);

    expr_t e = expr_init(OP_UNKNOWN);
    vec_foreach(&es, e, i) { expr_destroy_args(&e); }
    vec_free(&es);

    expr_arg_t a;
    vec_foreach(&as, a, i) {
        vec_foreach(&a.args, e, j) { expr_destroy_args(&e); }
        vec_free(&a.args);
    }
    vec_free(&as);

    /*vec_foreach(&os, o, i) {vec_free(&m.body);}*/
    vec_free(&os);
    return result;
}

void expr_destroy(expr_t* e, expr_var_list_t* vars)
{
    if (e != NULL)
    {
        expr_destroy_args(e);
        memory_deallocate(e);
    }
    if (vars != NULL)
    {
        for (expr_var_t* v = vars->head; v;)
        {
            expr_var_t* next = v->next;
            memory_deallocate(v);
            v = next;
        }
    }
}

expr_result_t eval(const char* expression, size_t expression_length /*= -1*/)
{
    return eval(string_const(expression, expression_length != -1 ? expression_length : string_length(expression)));
}

expr_result_t eval(string_const_t expression)
{
    memory_context_push(HASH_EXPR);

    for (size_t i = 0; i < array_size(_expr_lists); ++i)
        array_deallocate(_expr_lists[i]);
    array_clear(_expr_lists);

    expr_t* e = expr_create(STRING_ARGS(expression), &_global_vars, _expr_user_funcs);
    if (e == NULL)
    {
        memory_context_pop();
        return NIL;
    }

    eval_set_or_create_global_var(STRING_CONST("$0"), nullptr);

    expr_result_t result;
    try
    {
        EXPR_ERROR_CODE = EXPR_ERROR_NONE;
        result = expr_eval(e);
    }
    catch (ExprError err)
    {
        expr_error(err.code, expression, nullptr,
            "%.*s", err.message_length, err.message);
    }

    expr_destroy(e, nullptr);
    memory_context_pop();
    return result;
}

/**
 * Evaluation functions
 */

static string_const_t eval_evaluators_file_path()
{
    return session_get_user_file_path(STRING_CONST("evaluators.json"));
}

static void eval_load_evaluators(config_handle_t evaluators_data)
{
    for (const auto cv : evaluators_data)
    {
        expr_evaluator_t e{};
        string_copy(STRING_CONST_CAPACITY(e.code), STRING_ARGS(cv["code"].as_string()));
        string_copy(STRING_CONST_CAPACITY(e.label), STRING_ARGS(cv["label"].as_string()));
        string_copy(STRING_CONST_CAPACITY(e.expression), STRING_ARGS(cv["expression"].as_string()));
        string_copy(STRING_CONST_CAPACITY(e.assertion), STRING_ARGS(cv["assertion"].as_string()));
        e.frequency = cv["frequency"].as_number(60.0);

        for (const auto rcv : cv["records"])
        {
            expr_record_t r{};
            r.time = (time_t)rcv["time"].as_number();

            // Only keep record 5 days old an discard the rest.
            if (time_elapsed_days(r.time, time_now()) <= 5.0)
            {
                r.value = rcv["value"].as_number();
                r.tag = string_table_encode(rcv["tag"].as_string());
                r.assertion = rcv["assertion"].as_boolean();
                array_push(e.records, r);
            }
        }

        array_push(_evaluators, e);
    }
}

FOUNDATION_STATIC void eval_save_evaluators()
{
    config_write_file(eval_evaluators_file_path(), [](config_handle_t evaluators_data)
    {
        for (size_t i = 0; i < array_size(_evaluators); ++i)
        {
            expr_evaluator_t& e = _evaluators[i];

            config_handle_t ecv = config_array_push(evaluators_data, CONFIG_VALUE_OBJECT);
            config_set(ecv, "code", e.code, string_length(e.code));
            config_set(ecv, "label", e.label, string_length(e.label));
            config_set(ecv, "expression", e.expression, string_length(e.expression));
            config_set(ecv, "assertion", e.assertion, string_length(e.assertion));
            config_set(ecv, "frequency", e.frequency);

            config_handle_t records_data = config_set_array(ecv, STRING_CONST("records"));

            for (size_t ri = 0; ri < array_size(e.records); ++ri)
            {
                const expr_record_t& r = e.records[ri];
                auto rcv = config_array_push(records_data, CONFIG_VALUE_OBJECT);
                config_set(rcv, "time", (double)r.time);
                config_set(rcv, "value", r.value);
                config_set(rcv, "tag", string_table_decode_const(r.tag));
                config_set(rcv, "assertion", r.assertion);
            }

            array_deallocate(e.records);
        }

        return true;
    }, CONFIG_VALUE_ARRAY, CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS | CONFIG_OPTION_PRESERVE_INSERTION_ORDER |
    	CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES | CONFIG_OPTION_WRITE_NO_SAVE_ON_DATA_EQUAL);
}

FOUNDATION_STATIC void eval_run_evaluators()
{
    for (size_t i = 0; i < array_size(_evaluators); ++i)
    {
        expr_evaluator_t& e = _evaluators[i];

        if ((time_now() - e.last_run_time) > e.frequency)
        {
            e.last_run_time = time_now();

            const size_t expression_length = string_length(e.expression);
            if (expression_length == 0)
                continue;

            string_const_t expression = string_const(e.expression, expression_length);
            expr_result_t result = eval(expression);

            FOUNDATION_ASSERT(result.type != EXPR_RESULT_ARRAY);
            if (result.type == EXPR_RESULT_NULL)
                continue;

            expr_record_t new_record{ time_now() };
            if (result.type == EXPR_RESULT_NUMBER)
                new_record.value = result.as_number();
            else if (result.type == EXPR_RESULT_SYMBOL)
                new_record.tag = (string_table_symbol_t)result.value;
            else if (result.type == EXPR_RESULT_TRUE)
                new_record.assertion = true;
            else if (result.type == EXPR_RESULT_FALSE)
                new_record.assertion = false;

            const size_t assertion_length = string_length(e.assertion);
            if (assertion_length > 0)
            {
                string_const_t assertion = string_const(e.assertion, assertion_length);
                string_const_t assertion_build = string_format_static(STRING_CONST("E = %.*s, %.*s"), STRING_FORMAT(expression), STRING_FORMAT(assertion));
                expr_result_t assertion_result = eval(assertion_build);

                if (assertion_result.type == EXPR_RESULT_TRUE)
                    new_record.assertion = true;
                else if (assertion_result.type == EXPR_RESULT_FALSE)
                    new_record.assertion = false;

                string_format(STRING_CONST_CAPACITY(e.assembled), STRING_CONST("%.*s > %.*s"), STRING_FORMAT(result.as_string("%.6lf")), STRING_FORMAT(assertion_result.as_string()));
            }

            array_push(e.records, new_record);
            e.last_run_time = time_now();
        }
    }
}

void eval_render_evaluators()
{
    static bool has_ever_show_evaluators = session_key_exists("show_evaluators");
    static bool show_evaluators = session_get_bool("show_evaluators", false);

    bool start_show_stock_console = show_evaluators;
    if (shortcut_executed(ImGuiKey_F9))
        show_evaluators = !show_evaluators;

    eval_run_evaluators();

    if (!show_evaluators)
        return;

    if (!has_ever_show_evaluators)
        ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_Once);
    if (ImGui::Begin("Evaluators##1", &show_evaluators))
    {
        if (ImGui::BeginTable("##Evaluators", 5,
            ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Label");
            ImGui::TableSetupColumn("Title");
            ImGui::TableSetupColumn("Expression");
            ImGui::TableSetupColumn("Assertion");
            ImGui::TableSetupColumn("Monitor", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            // New row
            ImGui::TableNextRow();
            {
                static expr_evaluator_t new_entry;
                bool evaluate_expression = false;

                if (ImGui::TableNextColumn())
                {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::InputTextWithHint("##Label", "Description", STRING_CONST_CAPACITY(new_entry.label), ImGuiInputTextFlags_EnterReturnsTrue))
                        evaluate_expression = true;
                }

                if (ImGui::TableNextColumn())
                {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::InputTextWithHint("##Title", "AAPL.US", STRING_CONST_CAPACITY(new_entry.code), ImGuiInputTextFlags_EnterReturnsTrue))
                        evaluate_expression = true;
                }

                if (ImGui::TableNextColumn())
                {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::InputTextWithHint("##Expression", "S(AAPL.US, price)", STRING_CONST_CAPACITY(new_entry.expression), ImGuiInputTextFlags_EnterReturnsTrue))
                        evaluate_expression = true;
                }


                if (ImGui::TableNextColumn())
                {
                    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                    if (ImGui::InputTextWithHint("##Assertion", "E > 200.0", STRING_CONST_CAPACITY(new_entry.assertion), ImGuiInputTextFlags_EnterReturnsTrue))
                        evaluate_expression = true;
                }

                ImGui::TableNextColumn();

                if (evaluate_expression)
                {
                    string_const_t expression = string_const(new_entry.expression, string_length(new_entry.expression));
                    string_const_t assertion = string_const(new_entry.assertion, string_length(new_entry.assertion));
                    string_const_t result = eval(expression).as_string("%.6lf");

                    string_const_t assertion_build = string_format_static(STRING_CONST("E = %.*s, %.*s"), STRING_FORMAT(expression), STRING_FORMAT(assertion));
                    string_const_t assertion_result = eval(assertion_build).as_string();

                    string_format(STRING_CONST_CAPACITY(new_entry.assembled), STRING_CONST("%.*s > %.*s"), STRING_FORMAT(result), STRING_FORMAT(assertion_result));
                }

                if (ImGui::SmallButton("Add"))
                {
                    array_push(_evaluators, new_entry);
                    memset(&new_entry, 0, sizeof(expr_evaluator_t));
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(new_entry.assembled);
            }

            for (size_t i = 0; i < array_size(_evaluators); ++i)
            {
                expr_evaluator_t& ev = _evaluators[i];
                ImGui::TableNextRow(ImGuiTableRowFlags_None, 200.0f);
                {
                    bool evaluate_expression = false;

                    ImGui::PushID(&ev);

                    if (ImGui::TableNextColumn())
                    {
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                        if (ImGui::InputTextWithHint("##Label", "Description", STRING_CONST_CAPACITY(ev.label), ImGuiInputTextFlags_EnterReturnsTrue))
                            evaluate_expression = true;
                        ImGui::Text("%u Records", array_size(ev.records));
                    }

                    if (ImGui::TableNextColumn())
                    {
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                        if (ImGui::InputTextWithHint("##Title", "AAPL.US", STRING_CONST_CAPACITY(ev.code), ImGuiInputTextFlags_EnterReturnsTrue))
                            evaluate_expression = true;

                        ImGui::TextWrapped("%.*s", STRING_FORMAT(string_from_time_static(ev.last_run_time * 1000, true)));
                    }

                    if (ImGui::TableNextColumn())
                    {
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                        if (ImGui::InputTextWithHint("##Expression", "S(AAPL.US, price)", STRING_CONST_CAPACITY(ev.expression), ImGuiInputTextFlags_EnterReturnsTrue))
                            evaluate_expression = true;

                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted(ICON_MD_UPDATE);
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                        ImGui::InputDouble("##Frequency", &ev.frequency, 60.0, 0, "%.4g s.");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::BeginTooltip();
                            ImGui::TextUnformatted("Number of seconds to wait before re-evaluating these expressions.");
                            ImGui::EndTooltip();
                        }
                    }

                    if (ImGui::TableNextColumn())
                    {
                        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                        if (ImGui::InputTextWithHint("##Assertion", "E > 200.0", STRING_CONST_CAPACITY(ev.assertion), ImGuiInputTextFlags_EnterReturnsTrue))
                            evaluate_expression = true;

                        ImGui::TextWrapped("%s", ev.assembled);

                        if (ImGui::SmallButton("Clear records"))
                        {
                            array_deallocate(ev.records);
                            evaluate_expression = true;
                        }

                        ImGui::PushStyleColor(ImGuiCol_Button, BACKGROUND_CRITITAL_COLOR);
                        if (ImGui::SmallButton("Delete"))
                        {
                            array_deallocate(ev.records);
                            array_erase(_evaluators, i--);
                        }
                        ImGui::PopStyleColor();
                    }

                    if (evaluate_expression)
                        ev.last_run_time = 0;

                    ImGui::TableNextColumn();

                    if (array_size(ev.records) > 2 && ImPlot::BeginPlot("##MonitorGraph"))
                    {
                        double max = (double)array_last(ev.records)->time;
                        double min = (double)ev.records[0].time;
                        ImPlot::SetupAxis(ImAxis_X1, "##Days", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
                        ImPlot::SetupAxisFormat(ImAxis_X1, expr_eval_format_date_range_label, &ev);
                        ImPlot::SetupAxisTicks(ImAxis_X1, min, max, 6);
                        ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, min - (max - min) * 0.05, max + (max - min) * 0.05);

                        ImPlot::PlotLineG("##Values", [](int idx, void* user_data)->ImPlotPoint
                        {
                            expr_evaluator_t* c = (expr_evaluator_t*)user_data;
                            const expr_record_t* r = &c->records[idx];

                            const double x = (double)r->time;
                            const double y = r->value;
                            return ImPlotPoint(x, y);
                        }, & ev, array_size(ev.records), ImPlotLineFlags_SkipNaN);
                        ImPlot::EndPlot();
                    }

                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }
    } ImGui::End();

    if (start_show_stock_console != show_evaluators)
    {
        session_set_bool("show_evaluators", show_evaluators);
        start_show_stock_console = show_evaluators;
    }
}

void eval_register_function(const char* name, exprfn_t fn, exprfn_cleanup_t cleanup /*= nullptr*/, size_t context_size /*= 0*/)
{
    FOUNDATION_ASSERT(fn);

    memory_context_push(HASH_EXPR);

    string_t name_copy = string_clone(name, string_length(name));
    array_push(_expr_user_funcs_names, name_copy);

    expr_func_t efn;
    efn.handler = fn;
    efn.cleanup = cleanup;
    efn.ctxsz = context_size;
    efn.name = string_to_const(name_copy);
    array_insert_memcpy_safe(_expr_user_funcs, array_size(_expr_user_funcs) - 2, &efn);

    memory_context_pop();
}

bool eval_unregister_function(const char* name, exprfn_t fn /*= nullptr*/)
{
    for (unsigned i = 0, end = array_size(_expr_user_funcs); i < end; ++i)
    {
        expr_func_t& efn = _expr_user_funcs[i];
        if (efn.handler == fn || string_equal_nocase(name, string_length(name), STRING_ARGS(efn.name)))
        {
            array_erase_ordered_safe(_expr_user_funcs, i);
            return true;
        }
    }

    return false;
}

expr_var_t* eval_find_global_var(const char* name, size_t name_length)
{
    expr_var_t* v = _global_vars.head;
    while (v)
    {
        if (string_equal_nocase(STRING_ARGS(v->name), name, name_length))
            return v;
        v = v->next;
    }

    return nullptr;
}

expr_var_t* eval_get_or_create_global_var(const char* name, size_t name_length /*= 0ULL*/)
{
    name_length = name_length == 0ULL ? string_length(name) : name_length;
    expr_var_t* v = eval_find_global_var(name, name_length);
    if (v == nullptr)
    {
        v = (expr_var_t*)memory_allocate(HASH_EXPR, sizeof(expr_var_t) + name_length + 1, 0, MEMORY_PERSISTENT);
        v->name = string_copy((char*)v + sizeof(expr_var_t), name_length + 1, name, name_length);
        v->next = _global_vars.head;
        _global_vars.head = v;
    }

    return v;
}

expr_var_t* eval_set_or_create_global_var(const char* name, size_t name_length, const expr_result_t& value)
{
    expr_var_t* ev = eval_get_or_create_global_var(name, name_length);
    ev->value = value;
    return ev;
}

bool eval_set_global_var(const char* name, void* ptr, size_t size /*= 0*/)
{
    expr_var_t* v = eval_get_or_create_global_var(name);
    v->value.type = EXPR_RESULT_POINTER;
    v->value.ptr = ptr;
    v->value.index = size;
    return true;
}

bool eval_set_global_var(const char* name, double value)
{
    expr_var_t* v = eval_get_or_create_global_var(name);
    v->value.type = EXPR_RESULT_NUMBER;
    v->value.value = value;
    v->value.index = NO_INDEX;
    return true;
}

void expr_log_evaluation_result(string_const_t expression_string, const expr_result_t& result)
{
    if (result.type == EXPR_RESULT_ARRAY && result.element_count() > 1 && result.list[0].type == EXPR_RESULT_POINTER)
    {
        if (expression_string.length)
            log_infof(HASH_EXPR, STRING_CONST("%.*s\n"), STRING_FORMAT(expression_string));
        //_console_concat_messages = true;
        for (unsigned i = 0; i < result.element_count(); ++i)
            expr_log_evaluation_result({ nullptr, 0 }, result.element_at(i));
        //_console_concat_messages = false;
    }
    else if (result.type == EXPR_RESULT_POINTER && result.element_count() == 16 && result.element_size() == sizeof(float))
    {
        const float* m = (const float*)result.ptr;
        log_infof(HASH_EXPR, STRING_CONST("%.*s %s \n" \
            "\t[%7.4g, %7.4g, %7.4g, %7.4g\n" \
            "\t %7.4g, %7.4g, %7.4g, %7.4g\n" \
            "\t %7.4g, %7.4g, %7.4g, %7.4g\n" \
            "\t %7.4g, %7.4g, %7.4g, %7.4g ]\n"), STRING_FORMAT(expression_string), expression_string.length > 0 ? "=>" : "",
            m[0], m[1], m[2], m[3],
            m[4], m[5], m[6], m[7],
            m[8], m[9], m[10], m[11],
            m[12], m[13], m[14], m[15]);
    }
    else
    {
        string_const_t result_string = expr_result_to_string(result);
        if (expression_string.length)
        {
            if (expression_string.length + result_string.length > 64)
            {
                log_infof(HASH_EXPR, STRING_CONST("%.*s =>\n\t%.*s"), STRING_FORMAT(expression_string), STRING_FORMAT(result_string));
            }
            else
            {
                log_infof(HASH_EXPR, STRING_CONST("%.*s => %.*s"), STRING_FORMAT(expression_string), STRING_FORMAT(result_string));
                if (main_is_interactive_mode())
                    ImGui::SetClipboardText(result_string.str);
            }
        }
        else
            log_infof(HASH_EXPR, STRING_CONST("\t%.*s"), STRING_FORMAT(result_string));
    }
}

FOUNDATION_STATIC void eval_initialize()
{
    const auto json_flags =
        CONFIG_OPTION_WRITE_SKIP_DOUBLE_COMMA_FIELDS |
        CONFIG_OPTION_PRESERVE_INSERTION_ORDER |
        CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES |
        CONFIG_OPTION_WRITE_TRUNCATE_NUMBERS |
        CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS;

    // Set functions
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("MIN"), expr_eval_math_min, NULL, 0 })); // MIN([-1, 0, 1])
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("MAX"), expr_eval_math_max, NULL, 0 })); // MAX([1, 2, 3]) + MAX(4, 5, 6) = 9
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("SUM"), expr_eval_math_sum, NULL, 0 })); // SUM(0, 0, 1, 3) == 4
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("AVG"), expr_eval_math_avg, NULL, 0 })); // (AVG(1, [1, 1]) + AVG([1], [2], [3])) == 3
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("COUNT"), expr_eval_math_count, NULL, 0 })); // COUNT(SAMPLES())
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("INDEX"), expr_eval_array_index, NULL, 0 })); // INDEX([1, 2, 3], 2) == 2
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("MAP"), expr_eval_map, NULL, 0 })); // MAP([[a, 1], [b, 2], [c, 3]], INDEX($1, 1)) == [1, 2, 3]
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("FILTER"), expr_eval_filter, NULL, 0 })); // FILTER([1, 2, 3], EVAL($1 >= 3)) == [3]
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("EVAL"), expr_eval_inline, NULL, 0 })); // ADD(5, 5), EVAL($0 >= 10)
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("REPEAT"), expr_eval_repeat, NULL, 0 }));
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("REDUCE"), expr_eval_reduce, NULL, 0 }));
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("SORT"), expr_eval_sort, NULL, 0 }));

    // Math functions
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("ROUND"), expr_eval_round, NULL, 0 }));
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("CEIL"), expr_eval_ceil, NULL, 0 }));
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("FLOOR"), expr_eval_floor, NULL, 0 }));
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("RANDOM"), expr_eval_random, NULL, 0 }));

    // Vectors and matrices functions
    eval_register_vec_mat_functions(_expr_user_funcs);

    // Time functions
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("NOW"), expr_eval_time_now, NULL, 0 })); // // ELAPSED_DAYS(TO_DATE(F(SSE.V, General.UpdatedAt)), NOW())
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("DATE"), expr_eval_create_date, NULL, 0 }));
    array_push(_expr_user_funcs, (expr_func_t{ STRING_CONST("DATESTR"), expr_eval_date_to_string, NULL, 0 }));
    
    // Must always be last
    array_push(_expr_user_funcs, (expr_func_t{ NULL, 0, NULL, NULL, 0 }));

    eval_set_global_var("PI", DBL_PI);
    eval_set_global_var("HALFPI", DBL_HALFPI);
    eval_set_global_var("TWOPI", DBL_TWOPI);
    eval_set_global_var("SQRT2", DBL_SQRT2);
    eval_set_global_var("SQRT3", DBL_SQRT3);
    eval_set_global_var("E", DBL_E);
    eval_set_global_var("LOGN2", DBL_LOGN2);
    eval_set_global_var("LOGN10", DBL_LOGN10);

    string_const_t evaluators_file_path = eval_evaluators_file_path();
    config_handle_t evaluators_data = config_parse_file(STRING_ARGS(evaluators_file_path), json_flags);
    if (evaluators_data)
    {
        eval_load_evaluators(evaluators_data);
        config_deallocate(evaluators_data);
    }

    service_register_window(HASH_EXPR, eval_render_evaluators);

    string_const_t eval_expression;
    if (environment_command_line_arg("eval", &eval_expression))
    {
        static char command_line_eval_expression[2048] = "";
        string_copy(STRING_CONST_CAPACITY(command_line_eval_expression), STRING_ARGS(eval_expression));
        dispatch([]()
        {
            string_const_t expression_string = string_to_const(command_line_eval_expression);
            expr_result_t result = eval(expression_string);
            if (EXPR_ERROR_CODE == 0)
            {
                if (environment_command_line_arg("X"))
                {
                    string_const_t result_string = result.as_string();
                    log_info(0, STRING_ARGS(result_string));
                }
                else
                {
                    expr_log_evaluation_result(expression_string, result);
                }
            }
            else if (EXPR_ERROR_CODE != 0)
            {
                log_errorf(HASH_EXPR, ERROR_SCRIPT, STRING_CONST("[%d] %.*s -> %.*s"),
                    EXPR_ERROR_CODE, STRING_FORMAT(expression_string), (int)string_length(EXPR_ERROR_MSG), EXPR_ERROR_MSG);
            }
            
            system_post_event(FOUNDATIONEVENT_TERMINATE);
            //process_exit(0);
        });
    }
}

FOUNDATION_STATIC void eval_shutdown()
{
    eval_save_evaluators();
    array_deallocate(_evaluators);

    for (size_t i = 0; i < array_size(_expr_lists); ++i)
        array_deallocate(_expr_lists[i]);
    array_deallocate(_expr_lists);

    array_deallocate(_expr_user_funcs);
    string_array_deallocate(_expr_user_funcs_names);

    expr_destroy(nullptr, &_global_vars);
}

DEFINE_SERVICE(EXPR, eval_initialize, eval_shutdown, SERVICE_PRIORITY_SYSTEM);
