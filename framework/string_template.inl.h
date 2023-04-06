/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 *
 * Declare the string template variadics
 */

#pragma once

#include <foundation/string.h>

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

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(bool t) { return StringArgumentType::BOOL; }
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(int t, std::true_type) { return StringArgumentType::INT32; }
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(unsigned int t, std::true_type) { return StringArgumentType::UINT32; }
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(int64_t t, std::true_type) { return StringArgumentType::INT64; }
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(uint64_t t, std::true_type) { return StringArgumentType::UINT64; }
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(float t, std::true_type) { return StringArgumentType::FLOAT; }
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(double t, std::false_type) { return StringArgumentType::DOUBLE; }
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(const char* t, std::false_type) { return StringArgumentType::CSTRING; }
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(std::nullptr_t t, std::false_type) { return StringArgumentType::POINTER; }

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(const string_t& t) 
{ 
    return StringArgumentType::STRING; 
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(const string_const_t& t) 
{ 
    return StringArgumentType::STRING; 
}

template<class T>
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(T* arr, std::false_type) 
{ 
    return StringArgumentType::POINTER; 
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(string_template_stream_handler_t func) 
{ 
    return StringArgumentType::STREAM; 
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(int* arr) { return StringArgumentType::ARRAY_INT; }

template<typename T>
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_argument_type_t string_template_type(const T& t) 
{ 
    return string_template_type(t, typename std::is_integral<T>::type());
}

string_t string_format_allocate_template(const char* format, size_t format_length, string_argument_type_t t1, ...);

string_t string_format_template(char* buffer, size_t capacity, const char* format, size_t format_length, string_argument_type_t t1, ...);

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

template<size_t N, typename P1>
FOUNDATION_FORCEINLINE string_t string_template_static(const char(&format)[N], const P1& p1)
{
    string_t buffer = string_static_buffer(max(SIZE_C(64), SIZE_C(N + 16)));
    return string_format_template(buffer.str, buffer.length, format, N - 1, string_template_type(p1), p1);
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

template<size_t N, typename P1, typename P2>
FOUNDATION_FORCEINLINE string_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2)
{
    string_t buffer = string_static_buffer(max(SIZE_C(64), SIZE_C(N + 16 * 2)));
    return string_format_template(buffer.str, buffer.length, format, N - 1, string_template_type(p1), p1, string_template_type(p2), p2);
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

template<size_t N, typename P1, typename P2, typename P3>
FOUNDATION_FORCEINLINE string_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3)
{
    string_t buffer = string_static_buffer(max(SIZE_C(64), SIZE_C(N + 16 * 3)));
    return string_format_template(buffer.str, buffer.length, format, N - 1, string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3);
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

template<size_t N, typename P1, typename P2, typename P3, typename P4>
FOUNDATION_FORCEINLINE string_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4)
{
    string_t buffer = string_static_buffer(max(SIZE_C(128), SIZE_C(N + 16 * 4)));
    return string_format_template(buffer.str, buffer.length, format, N - 1,
           string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4);
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

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5>
FOUNDATION_FORCEINLINE string_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5)
{
    string_t buffer = string_static_buffer(max(SIZE_C(128), SIZE_C(N + 16 * 5)));
    return string_format_template(buffer.str, buffer.length, format, N - 1,
              string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5);
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

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6>
FOUNDATION_FORCEINLINE string_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6)
{
    string_t buffer = string_static_buffer(max(SIZE_C(128), SIZE_C(N + 16 * 6)));
    return string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3,
        string_template_type(p4), p4, string_template_type(p5), p5, string_template_type(p6), p6);
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

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7>
FOUNDATION_FORCEINLINE string_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7)
{
    string_t buffer = string_static_buffer(max(SIZE_C(128), SIZE_C(N + 16 * 7)));
    return string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4,
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7);
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

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8>
FOUNDATION_FORCEINLINE string_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8)
{
    string_t buffer = string_static_buffer(max(SIZE_C(256), SIZE_C(N + 16 * 8)));
    return string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4,
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8);
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

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9>
FOUNDATION_FORCEINLINE string_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9)
{
    string_t buffer = string_static_buffer(max(SIZE_C(256), SIZE_C(N + 16 * 9)));
    return string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4,
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9);
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

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10>
FOUNDATION_FORCEINLINE string_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10)
{
    string_t buffer = string_static_buffer(max(SIZE_C(256), SIZE_C(N + 16 * 10)));
    return string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4,
        string_template_type(p5), p5, string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8,
        string_template_type(p9), p9, string_template_type(p10), p10);
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

template<size_t N, typename P1, typename P2, typename P3, typename P4, typename P5, typename P6, typename P7, typename P8, typename P9, typename P10, typename P11>
FOUNDATION_FORCEINLINE string_t string_template_static(const char(&format)[N], const P1& p1, const P2& p2, const P3& p3, const P4& p4, const P5& p5, const P6& p6, const P7& p7, const P8& p8, const P9& p9, const P10& p10, const P11& p11)
{
    string_t buffer = string_static_buffer(max(SIZE_C(256), SIZE_C(N + 16 * 11)));
    return string_format_template(buffer.str, buffer.length, format, N - 1,
        string_template_type(p1), p1, string_template_type(p2), p2, string_template_type(p3), p3, string_template_type(p4), p4, string_template_type(p5), p5,
        string_template_type(p6), p6, string_template_type(p7), p7, string_template_type(p8), p8, string_template_type(p9), p9, string_template_type(p10), p10,
        string_template_type(p11), p11);
}
