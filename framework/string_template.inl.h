/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2023 Wiimag inc. All rights reserved.
 *
 * Declare the string template variadics
 */

#pragma once

#include <foundation/string.h>

extern string_t string_static_buffer(size_t required_length, bool clear_memory);

/*! @def {i,hex}
 *  @brief Format integer as hex
 *  @example string_template("0x{0,hex}", 0x1234) -> "0x1234"
 */
constexpr string_const_t HEX_OPTION = { STRING_CONST("hex") };

/*! @def {i,hex0x}
 *  @brief Format integer as hex with 0x prefix and use zero padding over 4 bytes or 8 bytes for 64 bit integers
 *  @example string_template("{0,hex0x}", 0x1234) -> "0x00001234"
 */
constexpr string_const_t HEX_0X_OPTION = { STRING_CONST("hex0x") };

/*! @def {i,hex0x2}
 *  @brief Format integer as hex with 0x prefix and use zero padding over 2 bytes
 *  @example string_template("{0,hex0x2}", '\n') -> "0x0a"
 */
constexpr string_const_t HEX_0X_BYTE_OPTION = { STRING_CONST("hex0x2") };

/*! @def {i,lowercase}
 *  @brief Format integer as hex with 0x prefix and use zero padding over 2 bytes
 *  @example string_template("{0,lowercase}", "HELLO") -> "hello"
 */
constexpr string_const_t LOWERCASE_OPTION = { STRING_CONST("lowercase") };

/*! @def {i,uppercase}
 *  @brief Format integer as hex with 0x prefix and use zero padding over 2 bytes
 *  @example string_template("{0,uppercase}", "heLLo") -> "HELLO"
 */
constexpr string_const_t UPPERCASE_OPTION = { STRING_CONST("uppercase") };

/*! @def {i,currency}
 *  @brief Format number as currency using the 9 999 999.00 $ format
 *  @example string_template("{0,currency}", 1234567.89) -> "1 234 567.89 $"
 */
constexpr string_const_t CURRENCY_OPTION = { STRING_CONST("currency") };

/*! @def {i,st} 
 *  @brief Format the 32 bit integer value as a symbol in the global string table.
 *  @see string_table_t
 *  @example string_template("{0,st} {1,st}", string_table_encode("Hello"), string_table_encode("World")) -> "Hello World"
 */
constexpr string_const_t STRING_TABLE_SYMBOL_OPTION = { STRING_CONST("st") };

/*! @def {i,date}
 *  @brief Format the value as a date using the YYYY-MM-DD format
 *  @example string_template("{0,date}", 1234567890) -> "2009-02-13"
 */
constexpr string_const_t DATE_OPTION = { STRING_CONST("date") };

/*! @def {i,since}
 *  @brief Format the date value as a time since the current time using the 1 day ago, 2 hours ago, 3 minutes ago, 4 seconds ago, etc. format
 *  @example string_template("{0,since}", 1234567890) -> "1 day ago"
 */
constexpr string_const_t SINCE_OPTION = { STRING_CONST("since") };

/*! @def {i,round} 
 *  @brief Round the floating point value to the nearest integer
 *  @example string_template("{0,round}", 1234.5678) -> "1235"
 */
constexpr string_const_t ROUND_OPTION = { STRING_CONST("round") };

/*! @def {i,translate}
 *  @brief Translate the string using the localization system.
 *  @see tr
 *  @note This option only works if the translations are available in locales.sjson
 *  @example string_template("{0,translate}", "Hello") -> "Bonjour"
 */
constexpr string_const_t TRANSLATE_OPTION = { STRING_CONST("translate") };

/*! @def {i,abbreviate}
 *  @brief Abbreviate the string using the localization system.
 *  @see tr
 *  @example string_template("{0,abbreviate}", "Hello") -> "H."
 *  @example string_template("{0,abbreviate}", 100e6) -> "100M"
 */
constexpr string_const_t ABBREVIATE_OPTION = { STRING_CONST("abbreviate") };

/*! @def {i,short}
 *  @brief Print a number value using the short format (1.2k, 1.2M, 1.2G, 1.2T, etc.)
 *  @remark This formatter is similar to the #abbreviate formatter
 *  @example string_template("{0,short}", 256045.4) -> "256k"
 *  @example string_template("{0,short}", 0.0045) -> "4.5m"
 */
constexpr string_const_t SHORT_OPTION = { STRING_CONST("short") };

// TODO (ideas):
// - {i,time} - Format as time
// - {i,datetime} - Format as date and time
// - {i,url} - Format as url
// - {i,base64} - Format as base64
// - {i,path} - Format as path
// - {i,fullpath} - Format as an absolute path
// - {i,hexdump} - Format as hexdump
// - {i,until} - Format as time until (in 1 day, in 2 hours, in 3 minutes, in 4 seconds, etc.)
// - {i,ordinal} - Format as ordinal (1st, 2nd, 3rd, 4th, etc.)
// - {i,tag} - Format as (1A2B,3C4D)
// - {i,escaped} - Format as escaped string (escape \n, \r, \t, \", \', \\, \u3453, etc.)
// - {i,unescaped} - Format as unescaped string (unescape \n, \r, \t, \", \', \\, \u3453, etc.)
// - {i,expr} - Evaluate the expression and format as the result (i.e. {i,expr:1+2} -> 3)
// - {i,%tag} - Format the value using sprintf (i.e. {i,%d} -> 1234)

typedef enum class StringArgumentType : unsigned int
{
    BOOL,
    INT32,
    INT64,
    UINT32,
    UINT64,
    FLOAT,
    DOUBLE,
    STRING,
    CSTRING,

    STREAM,
    POINTER,

    ARRAY_INT
} string_argument_type_t;

typedef enum class StringTokenOption
{
    None = 0,
    Hex = 1 << 0,
    HexPrefix = 1 << 1,
    HexBytePrefix = 1 << 2,
    Lowercase = 1 << 3,
    Uppercase = 1 << 4,
    Array = 1 << 5,
    Currency = 1 << 6,
    StringTableSymbol = 1 << 7,
    ShortDate = 1 << 8,
    Since = 1 << 9,
    Until = 1 << 10,
    Round = 1 << 11,
    Translate = 1 << 12,
    Abbreviate = 1 << 13,
    Short = 1 << 14,

} string_token_option_t;
DEFINE_ENUM_FLAGS(StringTokenOption);

typedef string_t(*string_template_stream_handler_t)(char* buffer, size_t size, void* ptr);

struct string_template_token_t
{
    int index;
    size_t start, end;

    // Options
    int precision{ 0 };
    string_token_option_t options{ string_token_option_t::None };
};

struct string_template_arg_value_t
{
    string_argument_type_t type;
    union {
        double f;
        int64_t i;
        string_const_t str;
        void* ptr;
    };

    string_template_stream_handler_t stream;
};

FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(bool t) { return StringArgumentType::BOOL; }
FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(int32_t t, std::true_type) { return StringArgumentType::INT32; }
FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(uint32_t t, std::true_type) { return StringArgumentType::UINT32; }
FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(int64_t t, std::true_type) { return StringArgumentType::INT64; }
FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(uint64_t t, std::true_type) { return StringArgumentType::UINT64; }
FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(float t, std::true_type) { return StringArgumentType::FLOAT; }
FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(double t, std::false_type) { return StringArgumentType::DOUBLE; }
FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(const char* t, std::false_type) { return StringArgumentType::CSTRING; }
FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(std::nullptr_t t, std::false_type) { return StringArgumentType::POINTER; }

#if FOUNDATION_PLATFORM_MACOS
    FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(time_t t, std::true_type) { return StringArgumentType::INT64; }
#endif

FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(const string_t& t) 
{ 
    return StringArgumentType::STRING; 
}

FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(const string_const_t& t) 
{ 
    return StringArgumentType::STRING; 
}

template<class T>
FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(T* arr, std::false_type) 
{ 
    return StringArgumentType::POINTER; 
}

FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(string_template_stream_handler_t func) 
{ 
    return StringArgumentType::STREAM; 
}

FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(int* arr) { return StringArgumentType::ARRAY_INT; }

template<typename T>
FOUNDATION_FORCEINLINE string_argument_type_t string_template_type(const T& t) 
{ 
    return string_template_type(t, typename std::is_integral<T>::type());
}

string_t string_format_allocate_template(const char* format, size_t format_length, string_argument_type_t t1, ...);

string_t string_format_template(char* buffer, size_t capacity, const char* format, size_t format_length, string_argument_type_t t1, ...);

FOUNDATION_FORCEINLINE string_t string_allocate_template(const char* fmt, size_t length) { return string_clone(fmt, length); }
template<size_t N> FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N]) { return string_clone(format, N - 1); }

// 1
template<typename P1>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const string_const_t& format, const P1& p1)
{
    return string_format_template(buffer, capacity, STRING_ARGS(format), string_template_type(p1), p1);
}

template<size_t N, typename P1>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const char(&format)[N], const P1& p1)
{
    return string_format_template(buffer, capacity, format, N - 1, string_template_type(p1), p1);
}

template<size_t N, typename P1>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N], const P1& p1)
{
    return string_format_allocate_template(format, N - 1, string_template_type(p1), p1);
}

template<typename P1>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char* format, size_t length, const P1& p1)
{
    return string_format_allocate_template(format, length, string_template_type(p1), p1);
}

template<size_t N, typename P1>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const char(&format)[N], const P1& p1)
{
    string_t buffer = string_static_buffer((size_t)max(SIZE_C(64), SIZE_C(N + 32)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, format, N - 1, string_template_type(p1), p1));
}

// 2
template<typename P1, typename P2>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const string_const_t& format, const P1& p1, const P2& p2)
{
    return string_format_template(buffer, capacity, STRING_ARGS(format), string_template_type(p1), p1, string_template_type(p2), p2);
}

template<size_t N, typename P1, typename P2>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const char(&format)[N], const P1& p1, const P2& p2)
{
    return string_format_template(buffer, capacity, format, N - 1, string_template_type(p1), p1, string_template_type(p2), p2);
}

template<size_t N, typename P1, typename P2>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N], const P1& p1, const P2& p2)
{
    return string_format_allocate_template(format, N - 1, string_template_type(p1), p1, string_template_type(p2), p2);
}

template<typename P1, typename P2>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char* format, size_t length, const P1& p1, const P2& p2)
{
    return string_format_allocate_template(format, length, string_template_type(p1), p1, string_template_type(p2), p2);
}

template<size_t N, typename P1, typename P2>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2)
{
    string_t buffer = string_static_buffer(max(SIZE_C(64), SIZE_C(N + 32 * 2)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, format, N - 1, string_template_type(p1), p1, string_template_type(p2), p2));
}

// 3
template<typename P1, typename P2, typename P3>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const string_const_t& format, const P1& p1, const P2& p2, const P3& p3)
{
    return string_format_template(buffer, capacity, STRING_ARGS(format), string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3);
}

template<size_t N, typename P1, typename P2, typename P3>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const char(&format)[N], const P1& p1, const P2& p2, const P3& p3)
{
    return string_format_template(buffer, capacity, format, N - 1, string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3);
}

template<size_t N, typename P1, typename P2, typename P3>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3)
{
    return string_format_allocate_template(format, N - 1, string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3);
}

template<typename P1, typename P2, typename P3>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char* format, size_t length, const P1& p1, const P2& p2, const P3& p3)
{
    return string_format_allocate_template(format, length, string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3);
}

template<size_t N, typename P1, typename P2, typename P3>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3)
{
    string_t buffer = string_static_buffer(max(SIZE_C(64), SIZE_C(N + 32 * 3)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, format, N - 1, string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3));
}

// 4
template<typename P1, typename P2, typename P3, typename P4>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const string_const_t& format, const P1& p1, const P2& p2, const P3& p3, const P4& p4)
{
    return string_format_template(buffer, capacity, STRING_ARGS(format), string_template_type(p1), p1, 
        string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4)
{
    return string_format_template(buffer, capacity, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4)
{
    return string_format_allocate_template(format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4);
}

template<typename P1, typename P2, typename P3, typename P4>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char* format, size_t length, const P1& p1, const P2& p2, const P3& p3, const P4& p4)
{
    return string_format_allocate_template(format, length,
           string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4)
{
    string_t buffer = string_static_buffer(max(SIZE_C(128), SIZE_C(N + 32 * 4)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, format, N - 1,
           string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4));
}

// 5
template<typename P1, typename P2, typename P3, typename P4, typename P5>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const string_const_t& format, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5)
{
    return string_format_template(buffer, capacity, STRING_ARGS(format), string_template_type(p1), p1, 
        string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5)
{
    return string_format_template(buffer, capacity, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5)
{
    return string_format_allocate_template(format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5);
}

template<typename P1, typename P2, typename P3, typename P4, typename P5>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char* format, size_t length, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5)
{
    return string_format_allocate_template(format, length,
              string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5)
{
    string_t buffer = string_static_buffer(max(SIZE_C(128), SIZE_C(N + 32 * 5)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, format, N - 1,
              string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5));
}

// 6
template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const string_const_t& format, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6)
{
    return string_format_template(buffer, capacity, STRING_ARGS(format), string_template_type(p1), p1, string_template_type(p2), p2, 
        string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6)
{
    return string_format_template(buffer, capacity, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, 
        string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6)
{
    return string_format_allocate_template(format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, 
        string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6);
}

template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char* format, size_t length, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6)
{
    return string_format_allocate_template(format, length,
           string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, 
            string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6)
{
    string_t buffer = string_static_buffer(max(SIZE_C(128), SIZE_C(N + 32 * 6)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3,
        string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6));
}

// 7
template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const string_const_t& format, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7)
{
    return string_format_template(buffer, capacity, STRING_ARGS(format), string_template_type(p1), p1, string_template_type(p2), p2, 
        string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5, 
        string_template_type(p6), p6, string_template_type(p7), p7);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7)
{
    return string_format_template(buffer, capacity, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, 
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7)
{
    return string_format_allocate_template(format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, 
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7);
}

template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char* format, size_t length, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7)
{
    return string_format_allocate_template(format, length,
           string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, 
           string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7)
{
    string_t buffer = string_static_buffer(max(SIZE_C(128), SIZE_C(N + 32 * 7)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4,
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7));
}

// 8
template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const string_const_t& format, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8)
{
    return string_format_template(buffer, capacity, STRING_ARGS(format), string_template_type(p1), p1, string_template_type(p2), p2, 
        string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6, 
        string_template_type(p7), p7, string_template_type(p8), p8);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8)
{
    return string_format_template(buffer, capacity, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, 
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8)
{
    return string_format_allocate_template(format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, 
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8);
}

template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char* format, size_t length, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8)
{
    return string_format_allocate_template(format, length,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, 
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8)
{
    string_t buffer = string_static_buffer(max(SIZE_C(256), SIZE_C(N + 32 * 8)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4,
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8));
}

// 9
template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const string_const_t& format, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9)
{
    return string_format_template(buffer, capacity, STRING_ARGS(format), string_template_type(p1), p1, string_template_type(p2), p2, 
        string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6, 
        string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9)
{
    return string_format_template(buffer, capacity, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, 
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9)
{
    return string_format_allocate_template(format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, 
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9);
}

template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const string_const_t& format, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9)
{
    return string_format_allocate_template(STRING_ARGS(format), string_template_type(p1), p1, string_template_type(p2), p2, 
           string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6, 
           string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9)
{
    string_t buffer = string_static_buffer(max(SIZE_C(256), SIZE_C(N + 32 * 9)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4,
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9));
}

// 10
template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const string_const_t& format, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10)
{
    return string_format_template(buffer, capacity, STRING_ARGS(format), string_template_type(p1), p1, string_template_type(p2), p2, 
        string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6, 
        string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9, string_template_type(p10), p10);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10)
{
    return string_format_template(buffer, capacity, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, 
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8, 
        string_template_type(p9), p9, string_template_type(p10), p10);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10)
{
    return string_format_allocate_template(format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, 
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8, 
        string_template_type(p9), p9, string_template_type(p10), p10);
}

template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const string_const_t& format, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10)
{
    string_t buffer = string_static_buffer(max(SIZE_C(256), SIZE_C(format.length + 32 * 10)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, STRING_ARGS(format), string_template_type(p1), p1, string_template_type(p2), p2, 
           string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6, 
           string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9, string_template_type(p10), p10));
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10)
{
    string_t buffer = string_static_buffer(max(SIZE_C(256), SIZE_C(N + 32 * 10)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4,
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8,
        string_template_type(p9), p9, string_template_type(p10), p10));
}

// 11
template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10, typename P11>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const string_const_t& format, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10, const P11& p11)
{
    return string_format_template(buffer, capacity, STRING_ARGS(format), string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, 
        string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8, 
        string_template_type(p9), p9, string_template_type(p10), p10, string_template_type(p11), p11);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10, typename P11>
FOUNDATION_FORCEINLINE string_t string_template(char* buffer, size_t capacity, const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10, const P11& p11)
{
    return string_format_template(buffer, capacity, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5, 
        string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9, string_template_type(p10), p10, 
        string_template_type(p11), p11);
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10, typename P11>
FOUNDATION_FORCEINLINE string_t string_allocate_template(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10, const P11& p11)
{
    return string_format_allocate_template(format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5, 
        string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9, string_template_type(p10), p10, 
        string_template_type(p11), p11);
}

template<typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10, typename P11>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const string_const_t& format, const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10, const P11& p11)
{
    string_t buffer = string_static_buffer(max(SIZE_C(256), SIZE_C(format.length + 32 * 11)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, STRING_ARGS(format), string_template_type(p1), p1, string_template_type(p2), p2, 
           string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, 
           string_template_type(p8), p8, string_template_type(p9), p9, string_template_type(p10), p10, string_template_type(p11), p11));
}

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10, typename P11>
FOUNDATION_FORCEINLINE string_const_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10, const P11& p11)
{
    string_t buffer = string_static_buffer(max(SIZE_C(256), SIZE_C(N + 32 * 11)), false);
    return string_to_const(string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5,
        string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9, string_template_type(p10), p10,
        string_template_type(p11), p11));
}
