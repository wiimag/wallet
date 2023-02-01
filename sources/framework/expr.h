/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/common.h>
#include <framework/string_table.h>

#include <foundation/math.h>
#include <foundation/array.h>
#include <foundation/assert.h>
#include <foundation/hash.h>

#define HASH_EXPR static_hash_string("expr", 4, 0xe44cd53772fb5e1eLL)

#define NO_INDEX (UINT64_MAX)
#define EXPR_ZERO (expr_result_t(EXPR_RESULT_NULL)), (nullptr), (0)

struct expr_t;
struct expr_func_t;
struct expr_result_t;

template<typename T>
struct vec
{
    T* buf{ nullptr };
    int len{ 0 };
    int cap{ 0 };

    T* get(int index)
    {
        FOUNDATION_ASSERT(index < len);
        return &buf[index];
    }

    const T* get(int index) const
    {
        FOUNDATION_ASSERT(index < len);
        return &buf[index];
    }
};

typedef vec<expr_t> vec_expr_t;

expr_result_t expr_eval(expr_t* e);
const char* expr_error_cstr(int error_code);
string_const_t expr_result_to_string(const expr_result_t& result, const char* fmt = "%.6g");

typedef void (*exprfn_cleanup_t)(const expr_func_t* f, void* context);
typedef expr_result_t(*exprfn_t)(const expr_func_t* f, vec_expr_t* args, void* context);

typedef enum ExprErrorCode : int {
    EXPR_ERROR_NONE = 0,
    EXPR_ERROR_BAD_PARENS,
    EXPR_ERROR_INVALID_TOKEN,
    EXPR_ERROR_INVALID_FUNCTION_NAME,
    EXPR_ERROR_INVALID_ARGUMENT,
    EXPR_ERROR_EVALUATION_STACK_FULL,
    EXPR_ERROR_EVALUATION_TIMEOUT,
    EXPR_ERROR_EVALUATION_NOT_IMPLEMENTED,
    EXPR_ERROR_BAD_VARIABLE_NAME,

    // Parsing errors
    EXPR_ERROR_ALLOCATION_FAILED = -1,
    EXPR_ERROR_UNEXPECTED_NUMBER = -2,			// unexpected number
    EXPR_ERROR_UNEXPECTED_WORD = -3,			// unexpected word
    EXPR_ERROR_UNEXPECTED_PARENTHESIS = -4,		// unexpected parenthesis
    EXPR_ERROR_MISSING_OPERAND = -5,			// missing expected operand
    EXPR_ERROR_UNKNOWN_OPERATOR = -6,			// unknown operator
    EXPR_ERROR_STRING_LITERAL_NOT_CLOSED = -7,	// missing closing " for string literal
    EXPR_ERROR_UNEXPECTED_SET = -8,				// unexpected set, i.e. {1, 2, 3}
} expr_error_code_t;

/*
 * Expression data types
 */
typedef enum ExprResultType {
    EXPR_RESULT_NULL,
    EXPR_RESULT_FALSE,
    EXPR_RESULT_TRUE,
    EXPR_RESULT_NUMBER,
    EXPR_RESULT_SYMBOL, // string stored using string_table_enconde (global string table)
    EXPR_RESULT_ARRAY,
    EXPR_RESULT_POINTER,
} expr_result_type_t;

typedef enum ExprOperatorType {
    OP_UNKNOWN,
    OP_UNARY_MINUS,
    OP_UNARY_LOGICAL_NOT,
    OP_UNARY_BITWISE_NOT,

    OP_POWER,
    OP_DIVIDE,
    OP_MULTIPLY,
    OP_REMAINDER,

    OP_PLUS,
    OP_MINUS,

    OP_SHL,
    OP_SHR,

    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,
    OP_EQ,
    OP_NE,

    OP_BITWISE_AND,
    OP_BITWISE_OR,
    OP_BITWISE_XOR,

    OP_LOGICAL_AND,
    OP_LOGICAL_OR,

    OP_ASSIGN,
    OP_COMMA,

    OP_CONST,
    OP_VAR,
    OP_FUNC,
    OP_SET,

    OP_COUNT
} expr_type_t;

typedef struct ExprError
{
    expr_error_code_t code;
    char message[1024];
    size_t message_length{ 0 };

    ExprError(expr_error_code_t code, const char* msg = nullptr, ...)
    {
        this->code = code;

        if (msg)
        {
            va_list list;
            va_start(list, msg);
            message_length = string_vformat(STRING_CONST_CAPACITY(message), msg, string_length(msg), list).length;
            va_end(list);
        }
        else
        {
            const char* expr_error_msg = expr_error_cstr(code);
            size_t expr_error_msg_length = string_length(expr_error_msg);
            message_length = string_copy(STRING_CONST_CAPACITY(message), expr_error_msg, expr_error_msg_length).length;
        }
    }
} expr_error_t;

typedef string_const_t expr_string_t;

enum EXPR_POINTER_CONTENT : uint64_t
{
    EXPR_POINTER_NONE = 0,
    EXPR_POINTER_UNSAFE = (1ULL << 63ULL),
    EXPR_POINTER_ARRAY = (1ULL << 62ULL),
    EXPR_POINTER_ARRAY_FLOAT = (1ULL << 61ULL), // floats and double (when element size == 8)
    EXPR_POINTER_ARRAY_INTEGER = (1ULL << 60ULL),
    EXPR_POINTER_ARRAY_UNSIGNED = (EXPR_POINTER_ARRAY_INTEGER | (1ULL << 59ULL)),

    EXPR_POINTER_TYPE_MASK = 0xFF00000000000000ULL,
    EXPR_POINTER_ELEMENT_SIZE_MASK = 0x000FFFF000000000ULL,
    EXPR_POINTER_ELEMENT_COUNT_MASK = 0x000000000FFFFFFFULL,

    EXPR_POINTER_ELEMENT_SIZE_SHIFT = 36ULL,
    EXPR_POINTER_ELEMENT_COUNT_SHIFT = 0ULL,
};

struct expr_result_t
{
    static thread_local const expr_result_t NIL;

    expr_result_type_t type{ EXPR_RESULT_NULL };

    union {
        double value{ 0 };
        const expr_result_t* list;
        void* ptr;
    };

    // @index will be used as a flags if type is EXPR_RESULT_POINTER, see @EXPR_POINTER_CONTENT
    size_t index{ NO_INDEX };

    expr_result_t(expr_result_type_t type = EXPR_RESULT_NULL)
        : type(type)
        , list(nullptr)
    {
    }

    expr_result_t(expr_result_type_t type, int symbol, size_t length)
        : type(type)
        , value(symbol)
        , index(length)
    {
    }

    expr_result_t(double value)
        : type(EXPR_RESULT_NUMBER)
        , value(value)
    {
    }

    expr_result_t(bool value)
        : type(value ? EXPR_RESULT_TRUE : EXPR_RESULT_FALSE)
        , value(value ? 1.0 : 0)
    {
    }

    expr_result_t(const char* FOUNDATION_RESTRICT str)
        : type(str ? EXPR_RESULT_SYMBOL : EXPR_RESULT_NULL)
        , index(string_length(str))
        , value(string_table_encode(str, string_length(str)))
    {
    }

    expr_result_t(string_const_t str)
        : type(str.length ? EXPR_RESULT_SYMBOL : EXPR_RESULT_NULL)
        , index(str.length)
        , value(string_table_encode(str))
    {
    }

    expr_result_t(const expr_result_t* list, size_t index = NO_INDEX)
        : type(list ? EXPR_RESULT_ARRAY : EXPR_RESULT_NULL)
        , list(list)
        , index(index)
    {
    }

    expr_result_t(std::nullptr_t)
        : type(EXPR_RESULT_NULL)
        , ptr(nullptr)
        , index(0)
    {
    }

    expr_result_t(void* ptr, size_t size = EXPR_POINTER_UNSAFE)
        : type(ptr ? EXPR_RESULT_POINTER : EXPR_RESULT_NULL)
        , ptr(ptr)
        , index(size)
    {
    }

    expr_result_t(void* arr, uint16_t element_size, uint32_t element_count, uint64_t content_flags = EXPR_POINTER_UNSAFE)
        : type(arr ? EXPR_RESULT_POINTER : EXPR_RESULT_NULL)
        , ptr(arr)
        , index((EXPR_POINTER_ARRAY | content_flags) |
            (((size_t)element_size << EXPR_POINTER_ELEMENT_SIZE_SHIFT) & EXPR_POINTER_ELEMENT_SIZE_MASK) |
            (((size_t)element_count << EXPR_POINTER_ELEMENT_COUNT_SHIFT) & EXPR_POINTER_ELEMENT_COUNT_MASK))
    {
    }

    double as_number(double default_value = NAN, size_t vindex = NO_INDEX) const
    {
        if (type == EXPR_RESULT_NULL)
            return default_value;

        if (type == EXPR_RESULT_NUMBER)
        {
            if (math_real_is_nan(value))
                return default_value;
            return value;
        }

        if (type == EXPR_RESULT_TRUE)
            return 1.0;

        if (type == EXPR_RESULT_FALSE)
            return 0.0;

        if (type == EXPR_RESULT_SYMBOL)
            return (double)math_trunc(value);

        if (type == EXPR_RESULT_POINTER)
        {
            const uint32_t element_count = this->element_count();
            if (ptr == nullptr || element_count == 0)
                return default_value;

            if (vindex == NO_INDEX)
                vindex = 0;

            uint16_t element_size = this->element_size();
            if ((index & EXPR_POINTER_ARRAY_FLOAT))
            {
                if (element_size == 4) return (double)*((const float*)ptr + vindex);
                else if (element_size == 8)  return *((const double*)ptr + vindex);
            }
            else if ((index & EXPR_POINTER_ARRAY_INTEGER))
            {
                if ((index & EXPR_POINTER_ARRAY_UNSIGNED) == EXPR_POINTER_ARRAY_UNSIGNED)
                {
                    if (element_size == 1) return (double)*((const uint8_t*)ptr + vindex);
                    else if (element_size == 2) return (double)*((const uint16_t*)ptr + vindex);
                    else if (element_size == 4) return (double)*((const uint32_t*)ptr + vindex);
                    else if (element_size == 8) return (double)*((const uint64_t*)ptr + vindex);
                }
                else
                {
                    if (element_size == 1) return (double)*((const int8_t*)ptr + vindex);
                    else if (element_size == 2) return (double)*((const int16_t*)ptr + vindex);
                    else if (element_size == 4) return (double)*((const int32_t*)ptr + vindex);
                    else if (element_size == 8) return (double)*((const int64_t*)ptr + vindex);
                }
            }

            return default_value;
        }

        if (type == EXPR_RESULT_ARRAY)
        {
            const size_t element_count = array_size(list);
            if (element_count == 0)
                return default_value;

            if (vindex != NO_INDEX && vindex < element_count)
                return list[vindex].as_number(default_value);

            if (element_count > 1 && index == NO_INDEX)
                log_warnf(HASH_EXPR, WARNING_SUSPICIOUS,
                    STRING_CONST("Expression set has many results (%u), returning first"), array_size(list));
            return list[index != NO_INDEX ? (min(index, element_count - 1)) : 0].as_number(default_value);
        }

        return default_value;
    }

    string_const_t as_string(const char* fmt = nullptr) const;

    bool is_null(size_t vindex = NO_INDEX) const
    {
        if (type == EXPR_RESULT_NULL)
            return true;

        if (type == EXPR_RESULT_TRUE)
            return false;

        if (type == EXPR_RESULT_FALSE)
            return true;

        if (type == EXPR_RESULT_NUMBER)
        {
            if (math_real_is_nan(value))
                return true;

            return false;
        }

        if (type == EXPR_RESULT_SYMBOL)
            return math_trunc(value) == 0;

        if (type == EXPR_RESULT_ARRAY)
        {
            if (vindex == NO_INDEX || vindex >= array_size(list))
                return list == nullptr;

            return list[vindex].is_null();
        }

        if (type == EXPR_RESULT_POINTER)
            return ptr == nullptr;

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return true;
    }

    bool is_set() const
    {
        if (type == EXPR_RESULT_ARRAY && array_size(list) > 0)
            return true;
        if (type == EXPR_RESULT_POINTER)
            return true;
        return false;
    }

    bool is_raw_array() const
    {
        if (type == EXPR_RESULT_POINTER && (index & EXPR_POINTER_ARRAY) == EXPR_POINTER_ARRAY)
            return true;
        return false;
    }

    expr_result_t element_at(unsigned vindex) const
    {
        if (type == EXPR_RESULT_ARRAY)
        {
            if (vindex >= array_size(list))
                return NIL;

            return list[vindex];
        }

        if (type == EXPR_RESULT_POINTER)
            return as_number(NAN, vindex);

        return *this;
    }

    uint16_t element_size() const
    {
        if (element_count() == 0)
            return 0;

        if (type == EXPR_RESULT_POINTER)
            return (uint16_t)((index & EXPR_POINTER_ELEMENT_SIZE_MASK) >> EXPR_POINTER_ELEMENT_SIZE_SHIFT);

        if (type == EXPR_RESULT_TRUE || type == EXPR_RESULT_FALSE)
            return 1;

        if (type == EXPR_RESULT_NUMBER)
            return sizeof(value);

        if (type == EXPR_RESULT_SYMBOL)
            return (uint16_t)string_table_decode_const((int)value).length;

        if (type == EXPR_RESULT_ARRAY)
            return list[index == NO_INDEX ? 0 : index].element_size();

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return sizeof(value);
    }

    uint32_t element_count() const
    {
        if (type == EXPR_RESULT_NULL)
            return 0;

        if (type == EXPR_RESULT_ARRAY)
            return array_size(list);

        if (type == EXPR_RESULT_POINTER)
            return (uint32_t)((index & EXPR_POINTER_ELEMENT_COUNT_MASK) >> EXPR_POINTER_ELEMENT_COUNT_SHIFT);

        return 1;
    }

    operator bool() const
    {
        if (type == EXPR_RESULT_NUMBER && math_real_is_zero(value))
            return false;
        return !is_null();
    }

    expr_result_t operator-() const
    {
        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(-value);

        if (type == EXPR_RESULT_TRUE)
            return expr_result_t(false);

        if (type == EXPR_RESULT_FALSE)
            return expr_result_t(true);

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator*(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value * rhs.as_number(0.0));

        if (type == EXPR_RESULT_SYMBOL)
        {
            string_const_t symbol = string_table_decode_const((string_table_symbol_t)value);
            const double n = string_to_real(STRING_ARGS(symbol));
            return expr_result_t(n * rhs.as_number(0.0));
        }

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator/(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value / rhs.as_number(1.0));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator+(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value + rhs.as_number(0.0));

        if (type == EXPR_RESULT_NULL)
            return NIL;

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t& operator+=(const expr_result_t& rhs)
    {
        if (type == EXPR_RESULT_NUMBER)
        {
            value += rhs.as_number(0.0);
            return *this;
        }

        if (type == EXPR_RESULT_NULL)
        {
            type = rhs.type;
            value = rhs.value;
            return *this;
        }

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator-(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value - rhs.as_number(0.0));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator<(const expr_result_t& rhs) const
    {
        if (is_null(index))
            return false;
        if (rhs.is_null())
            return true;

        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value < rhs.as_number());

        if (type == EXPR_RESULT_ARRAY && index != NO_INDEX)
            return list[index] < rhs;

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator>(const expr_result_t& rhs) const
    {
        if (is_null(index))
            return false;
        if (rhs.is_null())
            return true;

        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value > rhs.as_number());

        if (type == EXPR_RESULT_ARRAY && index != NO_INDEX)
            return list[index] > rhs;

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator<=(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value <= rhs.as_number());

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator>=(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value >= rhs.as_number());

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator==(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NULL && rhs.type == EXPR_RESULT_NULL)
            return true;

        if (type == EXPR_RESULT_NULL && rhs.type == EXPR_RESULT_NUMBER)
            return rhs.as_number(0) == 0;

        if (type == EXPR_RESULT_TRUE && rhs.type == EXPR_RESULT_TRUE)
            return true;
        if (type == EXPR_RESULT_FALSE && rhs.type == EXPR_RESULT_FALSE)
            return true;

        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(math_real_eq(value, rhs.as_number(), 4));

        if (type == EXPR_RESULT_SYMBOL)
            return math_trunc(value) == math_trunc(rhs.value);

        if (type == EXPR_RESULT_ARRAY)
        {
            for (unsigned i = 0, end = max(element_count(), rhs.element_count()); i < end; ++i)
            {
                if (list[i].as_number(NAN) != rhs.as_number(NAN, i))
                    return false;
            }

            return true;
        }

        if (type == EXPR_RESULT_POINTER)
        {
            const uint16_t esize = element_size();
            for (unsigned i = 0, end = max(element_count(), rhs.element_count()); ptr && i < end; ++i)
            {
                if (as_number(NAN, i) != rhs.as_number(NAN, i))
                    return false;
            }

            return true;
        }

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator!=(const expr_result_t& rhs) const
    {
        return !operator==(rhs);
    }

    expr_result_t operator!() const
    {
        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t((double)!math_trunc(value));

        if (type == EXPR_RESULT_TRUE)
            return expr_result_t(false);

        if (type == EXPR_RESULT_FALSE)
            return expr_result_t(true);

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator~() const
    {
        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t((double)~math_trunc(value));

        if (type == EXPR_RESULT_TRUE)
            return expr_result_t(false);

        if (type == EXPR_RESULT_FALSE)
            return expr_result_t(true);

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator<<(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER && rhs.type == EXPR_RESULT_NUMBER)
            return (double)(math_trunc(value) << math_trunc(rhs.value));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator>>(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER && rhs.type == EXPR_RESULT_NUMBER)
            return (double)(math_trunc(value) >> math_trunc(rhs.value));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator&(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER && rhs.type == EXPR_RESULT_NUMBER)
            return (double)(math_trunc(value) & math_trunc(rhs.value));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator|(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER && rhs.type == EXPR_RESULT_NUMBER)
            return (double)(math_trunc(value) | math_trunc(rhs.value));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    expr_result_t operator^(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER && rhs.type == EXPR_RESULT_NUMBER)
            return (double)(math_trunc(value) ^ math_trunc(rhs.value));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    struct iterator
    {
        unsigned index;
        const expr_result_t* set;

        bool operator!=(const iterator& other) const
        {
            if (set != other.set)
                return true;
            return (index != other.index);
        }

        bool operator==(const iterator& other) const
        {
            if (set != other.set)
                return false;
            return (index == other.index);
        }

        iterator& operator++()
        {
            ++index;
            return *this;
        }

        expr_result_t operator*() const
        {
            FOUNDATION_ASSERT(set && index != UINT_MAX);
            return set->element_at(index);
        }
    };

    iterator begin() const
    {
        FOUNDATION_ASSERT(is_set());
        return iterator{ 0, this };
    }

    iterator end() const
    {
        FOUNDATION_ASSERT(is_set());
        return iterator{ element_count(), this };
    }
};

struct expr_record_t
{
    time_t time{ 0 };
    bool assertion{ false };
    string_table_symbol_t tag{ 0 };
    double value{ NAN };
};

struct expr_evaluator_t
{
    char code[32]{ '\0' };
    char label[64]{ '\0' };
    char expression[1024]{ '\0' };
    char assertion[256]{ '\0' };
    char assembled[ARRAY_COUNT(expression) + ARRAY_COUNT(assertion)]{ '\0' };
    double frequency{ 60.0 * 5 }; // 5 minutes

    expr_record_t* records{ nullptr };
    time_t last_run_time{ 0 };
};

struct expr_func_t
{
    expr_string_t name;
    exprfn_t handler;
    exprfn_cleanup_t cleanup;
    size_t ctxsz;
};

struct expr_t
{
    expr_type_t type;
    vec_expr_t args{};

    union {
        struct {
            expr_func_t* f{ nullptr };
            void* context{ nullptr };
        } func;

        struct {
            expr_result_t value{ EXPR_RESULT_NULL };
        } result;

        struct {
            expr_result_t* value;
        } var;
    } param;

    expr_string_t token;

};

struct expr_var_t
{
    expr_result_t value;
    expr_var_t* next;
    string_t name;
    // IMPORTANT: string buffer is stored at the end of the structure and name points to it.
};

struct expr_var_list_t
{
    expr_var_t* head;
};

struct expr_arg_t
{
    int oslen{};
    int eslen{};
    vec_expr_t args{};
};

struct expr_macro_t
{
    expr_string_t name;
    vec_expr_t body;
};

extern thread_local char EXPR_ERROR_MSG[256];
extern thread_local expr_error_code_t EXPR_ERROR_CODE;
thread_local const expr_result_t NIL = expr_result_t::NIL;

expr_result_t eval(string_const_t expression);
expr_result_t eval(const char* expression, size_t expression_length = -1);

const expr_result_t* expr_eval_list(const expr_result_t* list);
bool eval_set_global_var(const char* name, void* ptr, size_t size = 0);
void eval_register_function(const char* name, exprfn_t fn, exprfn_cleanup_t cleanup = nullptr, size_t context_size = 0);
bool eval_unregister_function(const char* name, exprfn_t fn = nullptr);
expr_var_t* eval_get_or_create_global_var(const char* name, size_t name_length = 0ULL);
expr_var_t* eval_set_or_create_global_var(const char* name, size_t name_length, const expr_result_t& value);

void eval_register_vec_mat_functions(expr_func_t*& funcs);

expr_result_t expr_eval_symbol(string_table_symbol_t symbol);
expr_result_t expr_eval_pair(const expr_result_t& key, const expr_result_t& value);
expr_result_t expr_eval_merge(const expr_result_t& key, const expr_result_t& value, bool keep_nulls);
string_const_t expr_eval_get_string_arg(const vec_expr_t* args, size_t idx, const char* message);
string_const_t expr_eval_get_string_copy_arg(const vec_expr_t* args, size_t idx, const char* message);

void eval_render_evaluators();

