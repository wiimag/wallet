/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include <framework/common.h>
#include <framework/string.h>
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

/*
 * Expression error codes
 */
typedef enum ExprErrorCode : int {
    EXPR_ERROR_NONE = 0,
    EXPR_ERROR_BAD_PARENS,
    EXPR_ERROR_INVALID_TOKEN,
    EXPR_ERROR_INVALID_FUNCTION_NAME,
    EXPR_ERROR_INVALID_ARGUMENT,
    EXPR_ERROR_EVALUATION_STACK_FULL,
    EXPR_ERROR_EVALUATION_TIMEOUT,
    EXPR_ERROR_EXCEPTION,
    EXPR_ERROR_EVALUATION_NOT_IMPLEMENTED,
    EXPR_ERROR_BAD_VARIABLE_NAME,
    EXPR_ERROR_EMPTY_SET,
    EXPR_ERROR_EVAL_FUNCTION,

    EXPR_ERROR_FATAL_ERROR = 8000,

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

/*! Error message for the last expression evaluation */
extern thread_local char EXPR_ERROR_MSG[256];

/*! Error code for the last expression evaluation */
extern thread_local expr_error_code_t EXPR_ERROR_CODE;

/*! Expression string typedef for visibility. */
typedef string_const_t expr_string_t;

/*! Returns the constant string for the given error code. 
 * 
 *  @param error_code Error code to get the string for
 * 
 *  @return Constant string for the given error code
 */
const char* expr_error_cstr(int error_code);

/*! Simple vector template to store expression buffers. */
template<typename T>
struct vec
{
    T* buf{ nullptr };
    int len{ 0 };
    int cap{ 0 };

    FOUNDATION_FORCEINLINE T* get(int index)
    {
        FOUNDATION_ASSERT(index < len);
        return &buf[index];
    }

    FOUNDATION_FORCEINLINE const T* get(int index) const
    {
        FOUNDATION_ASSERT(index < len);
        return &buf[index];
    }
};

/*! Expression buffer type */
typedef vec<expr_t> vec_expr_t;

/*! Expression cleanup handler signature
 *
 *  @param f Function that is invoked to cleanup the function allocated context
 *  @param context Context pointer to cleanup
 */
typedef void (*exprfn_cleanup_t)(const expr_func_t* f, void* context);

/*! Expression function signature
 *
 *  @param f       Function that is invoked to evaluate the expression
 *  @param args    Arguments to the function
 *  @param context Context pointer to pass to the function
 *
 *  @return Result of the function evaluation
 */
typedef expr_result_t(*exprfn_t)(const expr_func_t* f, vec_expr_t* args, void* context);

/*! Expression operator types. */
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

/*! Expression error thrown when parsing or evaluating an expression. */
typedef struct ExprError
{
    expr_error_code_t code;
    expr_error_code_t outer;
    char message[1024];
    size_t message_length{ 0 };

    ExprError(expr_error_code_t code, const char* msg = nullptr, ...);
    ExprError(expr_error_code_t code, expr_error_code_t outer, const char* msg = nullptr, ...);
    ExprError(expr_error_code_t code, const expr_func_t* f, vec_expr_t* args, unsigned arg_index, const char* msg, ...);
} expr_error_t;

/*! Flags used to represent an expression result storing a pointer value. */
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

/*! Create an evaluation list that will be managed by the expression system.
 *
 *  @param list Newly created expression result list that will get disposed next update.
 *
 *  @return Stored expression result list.
 */
const expr_result_t* expr_eval_list(const expr_result_t* list);

/*! Expression result. 
 * 
 *  @note The @list member is used to store the result of an expression that
 *        returns an set of values, such as a function call. The @index member
 *        is used to store the index of the result in the global result buffer.
 */
struct expr_result_t
{
    /*! Nil result */
    static thread_local const expr_result_t NIL;

    /*! Expression result type. The type indicates how the result should be interpret. */
    expr_result_type_t type{ EXPR_RESULT_NULL };

    /*! #index will be used as a flags if type is EXPR_RESULT_POINTER, see @EXPR_POINTER_CONTENT */
    size_t index{ NO_INDEX };

    /*! Result value */
    union {
        /*! Numeric value if the expression result type is #EXPR_RESULT_NUMBER */
        double value{ 0 };

        /*! Symbol value if the expression result type is #EXPR_RESULT_ARRAY */
        const expr_result_t* list;

        /*! Symbol value if the expression result type is #EXPR_RESULT_POINTER */
        void* ptr;
    };

    FOUNDATION_FORCEINLINE expr_result_t(expr_result_type_t type = EXPR_RESULT_NULL)
        : type(type)
        , list(nullptr)
    {
    }

    FOUNDATION_FORCEINLINE expr_result_t(expr_result_type_t type, int symbol, size_t length)
        : type(type)
        , value(symbol)
        , index(length)
    {
    }

    FOUNDATION_FORCEINLINE expr_result_t(double value)
        : type(EXPR_RESULT_NUMBER)
        , value(value)
    {
    }

    expr_result_t(bool value)
        : type(value ? EXPR_RESULT_TRUE : EXPR_RESULT_FALSE)
        , value(value ? 1.0 : 0)
    {
    }

    FOUNDATION_FORCEINLINE expr_result_t(const char* FOUNDATION_RESTRICT str)
        : type(str ? EXPR_RESULT_SYMBOL : EXPR_RESULT_NULL)
        , index(string_length(str))
        , value(string_table_encode(str, index))
    {
    }

    FOUNDATION_FORCEINLINE expr_result_t(string_const_t str)
        : type(str.length ? EXPR_RESULT_SYMBOL : EXPR_RESULT_NULL)
        , index(str.length)
        , value(string_table_encode(str))
    {
    }

    FOUNDATION_FORCEINLINE expr_result_t(const expr_result_t* list, size_t index = NO_INDEX)
        : type(list ? EXPR_RESULT_ARRAY : EXPR_RESULT_NULL)
        , list(list)
        , index(index)
    {
    }

    FOUNDATION_FORCEINLINE expr_result_t(std::nullptr_t)
        : type(EXPR_RESULT_NULL)
        , ptr(nullptr)
        , index(0)
    {
    }

    FOUNDATION_FORCEINLINE expr_result_t(void* ptr, size_t size = EXPR_POINTER_UNSAFE)
        : type(ptr ? EXPR_RESULT_POINTER : EXPR_RESULT_NULL)
        , ptr(ptr)
        , index(size)
    {
    }

    FOUNDATION_FORCEINLINE expr_result_t(void* arr, uint16_t element_size, uint32_t element_count, uint64_t content_flags = EXPR_POINTER_UNSAFE)
        : type(arr ? EXPR_RESULT_POINTER : EXPR_RESULT_NULL)
        , ptr(arr)
        , index((EXPR_POINTER_ARRAY | content_flags) |
            (((size_t)element_size << EXPR_POINTER_ELEMENT_SIZE_SHIFT) & EXPR_POINTER_ELEMENT_SIZE_MASK) |
            (((size_t)element_count << EXPR_POINTER_ELEMENT_COUNT_SHIFT) & EXPR_POINTER_ELEMENT_COUNT_MASK))
    {
    }

    /*! Returns the numeric value of a result, or the default value if the result is not a number 
     * 
     *  @param default_value Default value to return if the result is not a number
     *  @param vindex        Index of the value to return if the result is an array
     * 
     *  @return Numeric value of the result, or the default value if the result is not a number
     */
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
        {
            string_const_t str = as_string();
            if (str.length)
            {
                if (str.length == 4)
                {
                    if (string_equal_nocase(STRING_ARGS(str), STRING_CONST("true")))
                        return 1.0;
                    if (string_equal_nocase(STRING_ARGS(str), STRING_CONST("null")))
                        return 0.0;
                }
                if (str.length == 5 && string_equal_nocase(STRING_ARGS(str), STRING_CONST("false")))
                    return 0.0;
                if (str.length == 3 && string_equal_nocase(STRING_ARGS(str), STRING_CONST("nil")))
                    return 0.0;

                double d;
                if (string_try_convert_number(STRING_ARGS(str), d))
                    return d;
            }
            return default_value;
        }

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
                if (element_size == 4) 
                    return (double)*((const float*)ptr + vindex);
                else if (element_size == 8)  
                    return *((const double*)ptr + vindex);
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

    bool as_boolean(size_t vindex = NO_INDEX) const
    {
        if (type == EXPR_RESULT_NULL)
            return false;

        if (type == EXPR_RESULT_NUMBER)
            return !math_real_is_nan(value) && !math_real_is_zero(value);

        if (type == EXPR_RESULT_TRUE)
            return true;

        if (type == EXPR_RESULT_FALSE)
            return false;

        if (type == EXPR_RESULT_SYMBOL)
        {
            if (value == 0)
                return false;

            string_const_t str = as_string();
            if (str.length == 4 && string_equal_nocase(STRING_ARGS(str), STRING_CONST("true")))
                return true;

            return false;
        }

        if (is_set())
            return element_at(vindex == NO_INDEX ? (index == NO_INDEX ? 0 : to_uint(index)) : to_uint(vindex)).as_boolean();

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return false;
    }

    /*! Returns the string value of a result, or the default value if the result is not a string
     *
     *  @param fmt          Format string to use for conversion if the value is a number for instance.
     *
     *  @return String representation of the result
     */
    string_const_t as_string(const char* fmt = nullptr) const;

    /*! Checks if the value is null (not defined or not a number)
     *  
     *  @param vindex Index of the value to check if the result is an array.
     *  
     *  @return True if the value is null, false otherwise
     */
    bool is_null(size_t vindex = NO_INDEX) const
    {
        if (type == EXPR_RESULT_NULL)
            return true;

        if (type == EXPR_RESULT_TRUE || type == EXPR_RESULT_FALSE)
            return false;

        if (type == EXPR_RESULT_NUMBER)
            return !math_real_is_finite(value);

        if (type == EXPR_RESULT_SYMBOL)
        {
            if (value == 0.0)
                return true;
            string_const_t str = as_string();
            if (str.length)
            {
                if (str.length == 4 && string_equal_nocase(STRING_ARGS(str), STRING_CONST("null")))
                    return true;
                if (str.length == 3 && string_equal_nocase(STRING_ARGS(str), STRING_CONST("nil")))
                    return true;
            }
            return false;
        }

        if (type == EXPR_RESULT_ARRAY)
        {
            if (vindex == NO_INDEX)
                return list == nullptr;

            if (vindex >= array_size(list))
                return true;

            return list[vindex].is_null();
        }

        if (type == EXPR_RESULT_POINTER)
            return ptr == nullptr;

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return true;
    }

    /*! Checks if the value representation a set of value, i.e. when the type is an array or a pointer
     *
     *  @return True if the value can be used as a set or to be enumerated using #begin and #end
     */
    FOUNDATION_FORCEINLINE bool is_set() const
    {
        if (type == EXPR_RESULT_ARRAY)
            return true;
        if (type == EXPR_RESULT_POINTER)
            return true;
        return false;
    }

    /*! Checks if the value is a pointer to a raw array
     *
     *  @return True if the value is a pointer to a raw array
     */
    FOUNDATION_FORCEINLINE bool is_raw_array() const
    {
        if (type == EXPR_RESULT_POINTER && (index & EXPR_POINTER_ARRAY) == EXPR_POINTER_ARRAY)
            return true;
        return false;
    }

    /*! Get the element value at a given index in case the result is a set of value.
     *  If the result is not a set, the result is returned.
     * 
     *  @param vindex Index of the value to get if the result is an array.
     * 
     *  @return Value at the given index
     */
    FOUNDATION_FORCEINLINE expr_result_t element_at(unsigned vindex) const
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

    /*! Get the element sizeof of a set of value. 
     *  If the result is not a set, 0 is returned.
     * 
     *  @remark This is useful for result of type array or pointer or when enumerating a set.
     *
     *  @return Element size of the set of value
     */
    uint16_t element_size() const
    {
        if (element_count() == 0)
            return 0;

        if (type == EXPR_RESULT_TRUE || type == EXPR_RESULT_FALSE)
            return 1;

        if (type == EXPR_RESULT_NUMBER)
            return sizeof(value);

        if (type == EXPR_RESULT_POINTER)
            return (uint16_t)((index & EXPR_POINTER_ELEMENT_SIZE_MASK) >> EXPR_POINTER_ELEMENT_SIZE_SHIFT);

        if (type == EXPR_RESULT_SYMBOL)
            return (uint16_t)string_table_decode_const((int)value).length;

        if (type == EXPR_RESULT_ARRAY)
            return list[index == NO_INDEX ? 0 : index].element_size();

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return sizeof(value);
    }

    /*! Get the element count of a set of value.
     *  If the result is not a set, 1 is returned.
     *
     *  @remark This is useful for result of type array or pointer or when enumerating a set.
     *
     *  @return Element count of the set of value
     */
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

    /*! Returns true if the value is defined (i.e. not nil). */
    FOUNDATION_FORCEINLINE operator bool() const
    {
        if (type == EXPR_RESULT_NULL)
            return false;
        if (type == EXPR_RESULT_TRUE)
            return true;
        if (type == EXPR_RESULT_FALSE)
            return false;
        if (type == EXPR_RESULT_NUMBER && math_real_is_zero(value))
            return false;
        if (type == EXPR_RESULT_SYMBOL && value == 0.0)
            return false;
        return !is_null();
    }

    /*! Returns the value as a number. */
    FOUNDATION_FORCEINLINE operator double() const
    {
        return this->as_number();
    }

    /*! Returns the negated value of the result. */
    expr_result_t operator-() const
    {
        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(-value);

        if (type == EXPR_RESULT_TRUE)
            return expr_result_t(false);

        if (type == EXPR_RESULT_FALSE)
            return expr_result_t(true);

        if (type == EXPR_RESULT_SYMBOL)
            return *this; // Return same thing

        if (is_set())
        {
            expr_result_t* elements = nullptr;
            for (unsigned i = 0, end = element_count(); i < end; ++i)
                array_push(elements, -element_at(i));
            return expr_result_t(expr_eval_list(elements));
        }

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns the result of the multiplication of the two values. */
    expr_result_t operator*(const expr_result_t& rhs) const
    {
        if (is_null() || rhs.is_null())
            return NIL;

        if (type == EXPR_RESULT_TRUE)
            return rhs;

        if (type == EXPR_RESULT_FALSE || rhs.type == EXPR_RESULT_FALSE)
            return expr_result_t(false);

        if (rhs.type == EXPR_RESULT_TRUE)
            return *this;

        if (!is_set() && rhs.is_set())
        {
            expr_result_t* elements = nullptr;
            for (unsigned i = 0, end = rhs.element_count(); i < end; ++i)
                array_push(elements, *this * rhs.element_at(i));
            return expr_result_t(expr_eval_list(elements));
        }

        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value * rhs.as_number(0.0));

        if (type == EXPR_RESULT_SYMBOL)
            return expr_result_t(as_number(NAN) * rhs.as_number(0.0));

        if (is_set() && !rhs.is_set())
        {
            expr_result_t* elements = nullptr;
            for (unsigned i = 0, end = element_count(); i < end; ++i)
                array_push(elements, element_at(i) * rhs);
            return expr_result_t(expr_eval_list(elements));
        }

        if (is_set() && rhs.is_set())
        {
            expr_result_t* elements = nullptr;
            for (unsigned i = 0, end = min(element_count(), rhs.element_count()); i < end; ++i)
            {
                expr_result_t result = element_at(i) * rhs.element_at(i);
                array_push(elements, result);
            }
            return expr_result_t(expr_eval_list(elements));
        }

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns the result of the division of the two values. */
    expr_result_t operator/(const expr_result_t& rhs) const
    {
        if (is_null() || rhs.is_null())
            return NIL;

        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value / rhs.as_number(1.0));

        if (is_set())
        {
            expr_result_t* elements = nullptr;
            for (unsigned i = 0, end = element_count(); i < end; ++i)
            {
                expr_result_t div = element_at(i) / rhs;
                array_push_memcpy(elements, &div);
            }
            return expr_result_t(expr_eval_list(elements));
        }

        if (type == EXPR_RESULT_SYMBOL)
            return rhs;

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns the result of the addition of the two values. */
    expr_result_t operator+(const expr_result_t& rhs) const
    {
        if (is_null() || rhs.is_null())
            return NIL;

        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value + rhs.as_number(0.0));

        if (type == EXPR_RESULT_SYMBOL || rhs.type == EXPR_RESULT_SYMBOL)
        {
            // Concat values into a new string.
            string_const_t s1 = this->as_string();
            string_const_t s2 = rhs.as_string();

            const size_t capacity = s1.length + s2.length + 1;
            string_t sc = string_allocate(0, capacity);
            sc = string_concat(sc.str, capacity, STRING_ARGS(s1), STRING_ARGS(s2));

            expr_result_t r(string_to_const(sc));
            string_deallocate(sc.str);
            return r;
        }

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns the result of the addition with another value. */
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

    /*! Returns the result of the subtraction of the two values. */
    expr_result_t operator-(const expr_result_t& rhs) const
    {
        if (is_null() || rhs.is_null())
            return NIL;

        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value - rhs.as_number(0.0));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns a boolean result if the current value is less than the other value. */
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

        if (type == EXPR_RESULT_SYMBOL)
        {
            string_const_t s1 = as_string();
            string_const_t s2 = rhs.as_string();
            return string_compare_less(STRING_ARGS(s1), STRING_ARGS(s2));
        }

        if (is_set())
        {
            for (const auto& e : *this)
            {
                if (e >= rhs)
                    return false;
            }
            return true;
        }

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns a boolean result if the current value is greater than the other value. */
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

        if (is_set())
        {
            for (const auto& e : *this)
            {
                if (e <= rhs)
                    return false;
            }

            return true;
        }

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns a boolean result if the current value is less than or equal to the other value. */
    expr_result_t operator<=(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NULL && rhs.type == EXPR_RESULT_NULL)
            return true;

        if (type == EXPR_RESULT_NULL || rhs.type == EXPR_RESULT_NULL)
            return false;

        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value <= rhs.as_number());

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns a boolean result if the current value is greater than or equal to the other value. */
    expr_result_t operator>=(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NULL && rhs.type == EXPR_RESULT_NULL)
            return true;

        if (type == EXPR_RESULT_NULL || rhs.type == EXPR_RESULT_NULL)
            return false;

        if (type == EXPR_RESULT_NUMBER)
            return expr_result_t(value >= rhs.as_number());

        if (is_set())
        {
            for (const auto& e : *this)
            {
                if (e < rhs)
                    return false;
            }

            return true;
        }

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns a boolean result if the current value is equal to the other value. */
    expr_result_t operator==(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NULL && rhs.is_null())
            return true;

        if (type == EXPR_RESULT_NULL && rhs.type == EXPR_RESULT_NUMBER)
            return rhs.as_number(0) == 0;

        if (type == EXPR_RESULT_NULL)
            return false;

        if (type == EXPR_RESULT_TRUE && rhs.type == EXPR_RESULT_NULL)
            return false;

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
                if (list[i].as_number(NAN) != rhs.as_number(DNAN, i))
                    return false;
            }

            return true;
        }

        if (type == EXPR_RESULT_POINTER)
        {
            const uint16_t esize = element_size();
            for (unsigned i = 0, end = max(element_count(), rhs.element_count()); ptr && i < end; ++i)
            {
                if (as_number(DNAN, i) != rhs.as_number(DNAN, i))
                    return false;
            }

            return true;
        }

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns a boolean result if the current value is not equal to the other value. */
    FOUNDATION_FORCEINLINE expr_result_t operator!=(const expr_result_t& rhs) const
    {
        return !operator==(rhs);
    }

    /*! Returns the result of the logical and of the two values. */
    expr_result_t operator!() const
    {
        if (type == EXPR_RESULT_NUMBER)
        {
            if (math_real_is_nan(value))
                return expr_result_t(true);
            return expr_result_t((double)!math_trunc(value));
        }

        if (type == EXPR_RESULT_TRUE)
            return expr_result_t(false);

        if (type == EXPR_RESULT_FALSE)
            return expr_result_t(true);

        if (type == EXPR_RESULT_SYMBOL)
            return !math_real_is_zero(value) && index > 0;

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns the result of the logical and of the two values. */
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

    /*! Returns the result of shifting the value. */
    expr_result_t operator<<(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER && rhs.type == EXPR_RESULT_NUMBER)
            return (double)(math_trunc(value) << math_trunc(rhs.value));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns the result of shifting the value. */
    expr_result_t operator>>(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER && rhs.type == EXPR_RESULT_NUMBER)
            return (double)(math_trunc(value) >> math_trunc(rhs.value));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns the & result of two value. */
    expr_result_t operator&(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_FALSE || rhs.type == EXPR_RESULT_FALSE)
            return false;

        if (type == EXPR_RESULT_TRUE)
            return rhs;

        if (rhs.type == EXPR_RESULT_TRUE)
            return *this;

        if (type == EXPR_RESULT_NUMBER && rhs.type == EXPR_RESULT_NUMBER)
            return (double)(math_trunc(value) & math_trunc(rhs.value));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns the | result of two value. */
    expr_result_t operator|(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NULL && rhs.is_null())
            return NIL;

        if (type == EXPR_RESULT_NUMBER && rhs.type == EXPR_RESULT_NUMBER)
            return (double)(math_trunc(value) | math_trunc(rhs.value));

        if (!is_null())
            return *this;

        if (type == EXPR_RESULT_NULL && !rhs.is_null())
            return rhs;

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Returns the ^ result of two value. */
    expr_result_t operator^(const expr_result_t& rhs) const
    {
        if (type == EXPR_RESULT_NUMBER && rhs.type == EXPR_RESULT_NUMBER)
            return (double)(math_trunc(value) ^ math_trunc(rhs.value));

        FOUNDATION_ASSERT_FAIL("Unsupported");
        return *this;
    }

    /*! Represent an iterator over the elements of an array. */
    struct iterator
    {
        unsigned index;
        const expr_result_t* set;

        FOUNDATION_FORCEINLINE bool operator!=(const iterator& other) const
        {
            if (set != other.set)
                return true;
            return (index != other.index);
        }

        FOUNDATION_FORCEINLINE bool operator==(const iterator& other) const
        {
            if (set != other.set)
                return false;
            return (index == other.index);
        }

        FOUNDATION_FORCEINLINE iterator& operator++()
        {
            ++index;
            return *this;
        }

        FOUNDATION_FORCEINLINE expr_result_t operator*() const
        {
            FOUNDATION_ASSERT(set && index != UINT_MAX);
            return set->element_at(index);
        }
    };

    /*! Returns an iterator to the first element of the array. */
    FOUNDATION_FORCEINLINE iterator begin() const
    {
        FOUNDATION_ASSERT(is_set());
        return iterator{ 0, this };
    }

    /*! Returns an iterator to the end of the array. */
    FOUNDATION_FORCEINLINE iterator end() const
    {
        FOUNDATION_ASSERT(is_set());
        return iterator{ element_count(), this };
    }

    /*! Returns the first element in the set.
     *  @note Only valid for sets. 
     *  @return The first element in the set.
     */
    FOUNDATION_FORCEINLINE expr_result_t first() const
    {
        FOUNDATION_ASSERT(is_set());
        return element_at(0);
    }

    /*! Returns the last element in the set.
     *  @note Only valid for sets. 
     *  @return The last element in the set.
     */
    FOUNDATION_FORCEINLINE expr_result_t last() const
    {
        FOUNDATION_ASSERT(is_set());
        return element_at(element_count() - 1);
    }
};

/*! Null value used statically when evaluating an expression */
thread_local const expr_result_t NIL = expr_result_t::NIL;

/*! Expression function. */
struct expr_func_t
{
    /*! Function name. */
    expr_string_t name;

    /*! Function handler. */
    exprfn_t handler;

    /*! Function cleanup handler. */
    exprfn_cleanup_t cleanup;

    /*! Function context size. */
    size_t ctxsz;
};

/*! Expression node. */
struct expr_t
{
    /*! Expression type. */
    expr_type_t type;

    /*! Expression arguments. */
    vec_expr_t args{};

    /*! Expression parameters based on the node type. */
    union {

        /*! Expression function. */
        struct {
            /*! Function pointer. */
            expr_func_t* f{ nullptr };
            
            /*! Function context dynamically allocated. */
            void* context{ nullptr };
        } func;

        /*! Expression result used for intermediate expression evaluation for to store the last evaluated node . */
        struct {
            expr_result_t value{ EXPR_RESULT_NULL };
        } result;

        /*! Expression variable payload. */
        struct {
            expr_result_t* value;
        } var;
    } param;

    /*! Expression token from the original expression. */
    expr_string_t token;
};

/*! Expression variable. 
 * 
 *  IMPORTANT: Variable string name buffer is stored at the end of the structure and name points to it.
 *  
 *  The actual expression var size is > sizeof(#expr_var_t) + strlen(name) + 1.
 */
struct expr_var_t
{
    /*! Variable value if non constant. */
    expr_result_t value;

    /*! Next variable in the list. */
    expr_var_t* next;

    /*! Variable name. */
    string_t name;
    
    // IMPORTANT: Variable string name buffer is stored at the end of the structure and name points to it.
};

/*! Expression variable list. */
struct expr_var_list_t
{
    /* Variable list head */
    expr_var_t* head;
};

/*! Expression argument list. */
struct expr_arg_t
{
    /* Operator stack length */
    int oslen{};

    /* Expression stack length */
    int eslen{};

    /* Expression arguments buffer */
    vec_expr_t args{};
};

/*! Expression macro used to declare a dynamic function. */
struct expr_macro_t
{
    expr_string_t name;
    vec_expr_t body;
};

/*! Evaluate an expression node.
 *
 *  @param e Expression node to evaluate.
 *
 *  @return Result of the expression evaluation.
 */
expr_result_t expr_eval(expr_t* e);

/*! Evaluate an expression.
 * 
 *  @param expression Expression to evaluate.
 * 
 *  @return Result of the expression evaluation.
 */
expr_result_t eval(string_const_t expression);

/*! Evaluate an expression.
 *
 * @remark This version of the function does not clean the evaluation context, allowing for
 *         multiple expressions to be evaluated in sequence.
 * 
 *  @param expression Expression to evaluate.
 * 
 *  @return Result of the expression evaluation.
 */
expr_result_t eval_inline(const char* expression, size_t expression_length);

/*! Evaluate an expression.
 *
 *  @param expression Expression to evaluate.
 *  @param expression_length Length of the expression string, or -1 if null terminated.
 *
 *  @return Result of the expression evaluation.
 */
expr_result_t eval(const char* expression, size_t expression_length = -1);

/*! Set a global expression variable to point to an application pointer.
 * 
 *  @remark Nothing special is done to manage the ptr lifespan. It is up to the application to ensure
 *          the pointer is valid for the duration of the expression evaluation.
 * 
 *  @param name Name of the variable.
 *  @param ptr  Pointer to the variable.
 *  @param size Size of the variable data payload, or 0 if unknown.
 */
bool expr_set_global_var(const char* name, void* ptr, size_t size = 0);

/*! Set the global variable to a given string value. 
 * 
 *  @param name Name of the variable.
 *  @param str  String value to set.
 *  @param str_length Length of the string, or -1 if null terminated.
 */
bool expr_set_global_var(const char* name, size_t name_length, const char* str, size_t str_length);

/*! Set the global variable to an expression result value.
 * 
 *  @param name Name of the variable.
 *  @param value Expression result value to set.
 *  @return true if the variable was set, false if the variable was not found.
 */
bool expr_set_global_var(const char* name, size_t name_length, const expr_result_t& value);

/*! Set the global variable to a given number value. 
 * 
 *  @param name Name of the variable.
 *  @param value Number value to set.
 */
bool expr_set_global_var(const char* name, size_t name_length, double value);

/*! Register a function with the expression system.
 *
 *  @param name         Name of the function.
 *  @param fn           Function pointer to register.
 *  @param cleanup      Function pointer to cleanup function, or nullptr if none.
 *  @param context_size Size of the context to allocate for the function, or 0 if none.
 */
void expr_register_function(const char* name, exprfn_t fn, exprfn_cleanup_t cleanup = nullptr, size_t context_size = 0);

/*! Unregister a function from the expression system.
 *
 *  @param name Name of the function.
 *  @param fn   Function pointer to unregister, or nullptr to unregister all functions with the given name.
 *
 *  @return True if the function was unregistered, false if not found.
 */
bool expr_unregister_function(const char* name, exprfn_t fn = nullptr);

/*! Get a global variable accessible thought the expression system.
 *  If the variable does not exist, it will be created with a NULL value.
 *
 *  @param name Name of the variable.
 *  @param name_length Length of the name, or 0 if null terminated.
 *
 *  @return Variable if found, or a null value if newly created.
 */
expr_var_t* expr_get_or_create_global_var(const char* name, size_t name_length = 0ULL);

/*! Return the variable value if any. 
 * 
 *  @param name Name of the variable.
 *  @param name_length Length of the name, or 0 if null terminated.
 * 
 *  @return Variable value if found, or a null value if not found.
 */
expr_result_t expr_get_global_var_value(const char* name, size_t name_length = 0ULL);

/*! Set or create a global variable accessible thought the expression system.
 *
 *  @param name  Name of the variable.
 *  @param value Value to set.
 *
 *  @return Variable that was set.
 */
expr_var_t* expr_set_or_create_global_var(const char* name, size_t name_length, const expr_result_t& value);

/*! Register a set of functions for vector and matrix operations.
 *
 *  @param funcs Array of functions to register.
 */
void expr_register_vec_mat_functions(expr_func_t*& funcs);

/*! Merge a key value pair into an expression result set, i.e. [key, value]
 *
 *  @param key   Key to pair.
 *  @param value Value to pair.
 *
 *  @return Paired expression result.
 */
expr_result_t expr_eval_pair(const expr_result_t& key, const expr_result_t& value);

/*! Evaluate a merge expression.
 *
 *  @param key        Key to merge.
 *  @param value      Value to merge.
 *  @param keep_nulls If true, then null values will be kept in the result.
 *
 *  @return Merged expression result.
 */
expr_result_t expr_eval_merge(const expr_result_t& key, const expr_result_t& value, bool keep_nulls);

/*! Evaluates the expression argument at a given index to a string.
 * 
 *  If the argument is not an evaluable argument, then we return the variable name as the constant string.
 * 
 *  @param args     Expression argument vector.
 *  @param idx      Index of the argument to evaluate.
 *  @param message  Error message if evaluation fails.
 * 
 *  @return String value of the argument.
 */
string_const_t expr_eval_get_string_arg(const vec_expr_t* args, size_t idx, const char* message);

/*! Evaluates the expression argument at a given index to a data set value. 
 * 
 *  @param args     Expression argument vector.
 *  @param idx      Index of the argument to evaluate.
 *  @param message  Error message if evaluation fails.
 * 
 *  @return Data set value of the argument.
 */
expr_result_t expr_eval_get_set_arg(const vec_expr_t* args, size_t idx, const char* message);

/*! Log expression result to the console
 * 
 *  @param expression_string   The expression string
 *  @param result              The result of the expression
 */
void expr_log_evaluation_result(string_const_t expression_string, const expr_result_t& result);
