/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "string.h"

#include <foundation/random.h>

#include <stdio.h>
#include <mnyfmt.h>

size_t string_occurence(const char* str, size_t len, char c)
{
    size_t occurence = 0;
    size_t offset = 0;
    while (true)
    {
        size_t foffset = string_find(str, len, c, offset);
        if (foffset == STRING_NPOS)
            break;
        offset = foffset + 1;
        occurence++;
    }
    return occurence + (offset + 1 < len);
}

size_t string_line_count(const char* str, size_t len)
{
    if (!str || len == 0)
        return 0;
    return string_occurence(str, len, STRING_NEWLINE[0]);
}

lines_t string_split_lines(const char* str, size_t len)
{
    lines_t lines;
    const size_t line_occurence = string_line_count(str, len);
    lines.items = (string_const_t*)memory_allocate(0, line_occurence * sizeof(string_const_t), 0, 0);
    lines.count = string_explode(str, len, STRING_CONST(STRING_NEWLINE), lines.items, line_occurence, false);
    FOUNDATION_ASSERT(lines.count == line_occurence);
    return lines;
}

void string_lines_finalize(lines_t& lines)
{
    memory_deallocate(lines.items);
    lines.count = 0;
}

string_t string_utf8_unescape(const char* s, size_t length)
{
    if (s == nullptr || length == 0)
        return string_t{ nullptr, 0 };

    string_t utf8 = string_allocate(0, length * 4);
    for (const char* c = s; *c && size_t(c - s) < length; ++c)
    {
        if (*c != '\\')
        {
            utf8.str[utf8.length++] = *c;
            continue;
        }

        if (size_t(c + 6 - s) < length && c[1] == 'u' &&
            is_char_alpha_num_hex(c[2]) &&
            is_char_alpha_num_hex(c[3]) &&
            is_char_alpha_num_hex(c[4]) &&
            is_char_alpha_num_hex(c[5]))
        {
            const uint16_t uc = hex_value(c[2]) << 12 | hex_value(c[3]) << 8 | hex_value(c[4]) << 4 | hex_value(c[5]);

            char utf_chars_buffer[4];
            string_t utf_chars = string_convert_utf16(utf_chars_buffer, sizeof(utf_chars_buffer), &uc, 1);
            for (int j = 0; j < utf_chars.length; ++j)
                utf8.str[utf8.length++] = utf_chars_buffer[j];
            c += 5;
        }
        else if (size_t(c + 1 - s) < length && c[1] == '/')
        {
            utf8.str[utf8.length++] = *(++c);
        }
        else
        {
            utf8.str[utf8.length++] = *c;
        }
    }
    
    utf8.str[utf8.length] = '\0';
    return utf8;
}

bool string_equal_ignore_whitespace(const char* lhs, size_t lhs_length, const char* rhs, size_t rhs_length)
{
    const char* l = lhs;
    const char* r = rhs;

    while (l && r && lhs_length > 0 && rhs_length > 0)
    {
        while (is_whitespace(*l))
        {
            if (--lhs_length == 0 || *(++l) == 0)
                return false;
        }

        while (is_whitespace(*r))
        {
            if (--rhs_length == 0 || *(++r) == 0)
                return false;
        }

        if (*l != *r)
            return false;

        ++l, ++r;
        --lhs_length, --rhs_length;
    }

    return lhs_length == rhs_length;
}

bool string_contains_nocase(const char* lhs, size_t lhs_length, const char* rhs, size_t rhs_length)
{
    if (lhs_length == 0)
        return false;

    const char* t = lhs;
    for (size_t i = 0; i < lhs_length - rhs_length + 1 && *t; ++i, ++t)
    {
        if (string_equal_nocase(t, rhs_length, rhs, rhs_length))
            return true;
    }

    return false;
}

string_const_t string_from_date(const tm& tm)
{
    string_t time_buffer = string_static_buffer(16);
    return string_to_const(string_format(STRING_ARGS(time_buffer), STRING_CONST("%d-%02d-%02d"), tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday));
}

string_const_t string_from_date(time_t at)
{
    tm tm;
    if (time_to_local(at, &tm) == false)
        return string_null();
    return string_from_date(tm);
}

string_t string_static_buffer(size_t required_length /*= 64*/, bool clear_memory /*= false*/)
{
    static thread_local size_t _thread_string_index = 0;
    static thread_local char _thread_string_buffer_ring[65536] = { 0 };
    static thread_local const size_t buffer_capacity = ARRAY_COUNT(_thread_string_buffer_ring);

    if (required_length > buffer_capacity)
    {
        FOUNDATION_ASSERT_FAILFORMAT("Required length too large %zu > %zu", required_length, buffer_capacity);
        return string_t{ 0, 0 };
    }

    char* cstr = nullptr;
    if (_thread_string_index + required_length >= ARRAY_COUNT(_thread_string_buffer_ring))
    {
        cstr = &_thread_string_buffer_ring[0];
        _thread_string_index = required_length;
    }
    else
    {
        cstr = _thread_string_buffer_ring + _thread_string_index;
        _thread_string_index += required_length;
    }

    if (clear_memory)
        memset(cstr, 0, required_length);
    else if (required_length > 0)
        cstr[0] = '\0';
    return string_t{ cstr, required_length };
}

string_const_t string_from_currency(double value, const char* money_fmt /*= nullptr*/, size_t money_fmt_length /*= 0*/)
{
    if (math_real_is_nan(value) || math_real_is_inf(value))
        return CTEXT("-");

    const double abs_value = math_abs(value);
    if (abs_value >= 1e12)
        return string_format_static(STRING_CONST("%.3gT $"), value / 1e12);
    if (abs_value >= 1e9)
        return string_format_static(STRING_CONST("%.3gB $"), value / 1e9);
    else if (abs_value >= 1e7)
        return string_format_static(STRING_CONST("%.3gM $"), value / 1e6);

    string_t fmt_buffer = string_static_buffer(32);

    if (money_fmt == nullptr)
    {
        if (value < 0.05)
            return string_to_const(string_format(STRING_ARGS(fmt_buffer), STRING_CONST("%.3lf $"), value));

        if (value < 1e3)
            return string_to_const(string_format(STRING_ARGS(fmt_buffer), STRING_CONST("%.2lf $"), value));

        money_fmt = "9 999 999.99 $";
        money_fmt_length = 0;
    }

    if (money_fmt_length == 0)
        money_fmt_length = string_length(money_fmt);

    if (money_fmt_length > 0 && (money_fmt[0] == '%' || string_find(money_fmt, max(0ULL, money_fmt_length - 1ULL), '%', 0) != STRING_NPOS))
        return string_to_const(string_format(STRING_ARGS(fmt_buffer), money_fmt, money_fmt_length, value));

    const mnyfmt_long mv = (mnyfmt_long)((value * 100.0) + 0.5);
    string_copy(STRING_ARGS(fmt_buffer), money_fmt, money_fmt_length);
    char* fmv = mnyfmt(fmt_buffer.str, '.', mv);
    if (fmv)
        return string_const(fmv, string_length(fmv));

    return string_to_const(string_format(fmt_buffer.str, fmt_buffer.length, STRING_CONST("%.2lf $"), value));
}

string_const_t string_format_static(const char* fmt, size_t fmt_length, ...)
{
    va_list list;
    va_start(list, fmt_length);
    string_t fmt_buf = string_static_buffer(max(2048ULL, fmt_length * 8ULL));
    string_t fmt_result = string_vformat(STRING_ARGS(fmt_buf), fmt, fmt_length, list);
    va_end(list);

    return string_to_const(fmt_result);
}

const char* string_format_static_const(const char fmt[], ...)
{
    va_list list;
    va_start(list, fmt);
    size_t fmt_length = string_length(fmt);
    string_t fmt_buf = string_static_buffer(max(2048ULL, fmt_length * 8ULL));
    string_t fmt_result = string_vformat(STRING_ARGS(fmt_buf), fmt, fmt_length, list);
    va_end(list);

    return fmt_result.str;
}

string_const_t string_trim(string_const_t str, char c /*= ' '*/)
{
    for (size_t i = 0; i < str.length; ++i)
    {
        if (str.str[i] != c)
            break;

        str.str++;
        str.length--;
        i--;
    }

    for (int i = (int)str.length - 1; i >= 0; --i)
    {
        if (str.str[i] != c)
            break;

        str.length--;
    }

    return str;
}

string_const_t string_to_const(const char* str)
{
    return string_const(str, string_length(str));
}

time_t string_to_date(const char* date_str, size_t date_str_length, tm* out_tm /*= nullptr*/)
{
    if (date_str == nullptr || date_str_length == 0)
        return 0;

    tm tm = {};
    if (sscanf(date_str, /*date_str_length,*/ "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) == 3)
    {
        tm.tm_mon--;
        tm.tm_year -= 1900;
        if (out_tm)
            *out_tm = tm;
        time_t r = mktime(&tm);
        if (r == -1)
            return 0;
        return r;
    }

    return 0;
}

bool string_compare_less(const char* str1, size_t str1_length, const char* str2, size_t str2_length)
{
    if (str1 == nullptr && str2 != nullptr)
        return false;

    if (str1_length == 0 && str2_length != 0)
        return false;

    if (str2 == nullptr && str1 != nullptr)
        return true;

    if (str2_length == 0 && str1_length != 0)
        return true;

    if (str2 == nullptr && str1 == nullptr)
        return false;

    if (str2_length == 0 && str1_length == 0)
        return false;

    return strncmp(str1, str2, min(str1_length, str2_length)) < 0;
}

bool string_compare_less(const char* str1, const char* str2)
{
    if (str1 == nullptr && str2 != nullptr)
        return false;

    if (str2 == nullptr && str1 != nullptr)
        return true;

    if (str2 == nullptr && str1 == nullptr)
        return false;

    return strcmp(str1, str2) < 0;
}

string_t* string_split(string_const_t str, string_const_t sep)
{
    string_const_t token, remaining;
    string_split(STRING_ARGS(str), STRING_ARGS(sep), &token, &remaining, false);

    string_t* tokens = nullptr;
    while (token.length > 0)
    {
        array_push(tokens, string_clone(STRING_ARGS(token)));
        string_split(STRING_ARGS(remaining), STRING_ARGS(sep), &token, &remaining, false);
    }

    return tokens;
}

string_const_t string_remove_line_returns(char* buffer, size_t capacity, const char* str, size_t length)
{
    if (string_find(str, length, '\n', 0) == STRING_NPOS)
        return {};

    bool space_injected = false;
    string_t result = {buffer, 0};
    for (size_t i = 0; i < length && result.length < capacity-1; ++i)
    {
        const char tok = str[i];
        if (tok <= ' ')
        {
            if (!space_injected)
            {
                result.str[result.length++] = ' ';
                space_injected = true;
            }
        }
        else
        {
            result.str[result.length++] = tok;
            space_injected = false;
        }
    }

    result.str[result.length] = '\0';
    return string_to_const(result);
}

string_t string_to_lower_ascii(char* buf, size_t capacity, const char* str, size_t length)
{
    string_t result = { buf, 0 };
    for (size_t i = 0; i < length && result.length < capacity - 1; ++i)
    {
        const char tok = str[i];
        if (tok >= 'A' && tok <= 'Z')
            result.str[result.length++] = (char)(tok - 'A' + 'a');
        else
            result.str[result.length++] = tok;
    }
    return result;
}

string_t string_to_upper_ascii(char* buf, size_t capacity, const char* str, size_t length)
{
    string_t result = { buf, 0 };
    for (size_t i = 0; i < length && result.length < capacity - 1; ++i)
    {
        const char tok = str[i];
        if (tok >= 'a' && tok <= 'z')
            result.str[result.length++] = (char)(tok - 'a' + 'A');
        else
            result.str[result.length++] = tok;
    }
    return result;
}

string_t string_to_lower_utf8(char* buf, size_t capacity, const char* str, size_t length)
{
    string_t result = string_copy(buf, capacity, str, length);
    
    unsigned char* pExtChar = 0;
    unsigned char* p = (unsigned char*)result.str;

    if (str == nullptr || length == 0 || str[0] == 0)
        return result;

    while (*p && p < (unsigned char*)(result.str + result.length)) 
    {
        if ((*p >= 0x41) && (*p <= 0x5a)) /* US ASCII */
            (*p) += 0x20;
        else if (*p > 0xc0) {
            pExtChar = p;
            p++;
            switch (*pExtChar) {
            case 0xc3: /* Latin 1 */
                if ((*p >= 0x80)
                    && (*p <= 0x9e)
                    && (*p != 0x97))
                    (*p) += 0x20; /* US ASCII shift */
                break;
            case 0xc4: /* Latin ext */
                if (((*p >= 0x80)
                    && (*p <= 0xb7)
                    && (*p != 0xb0))
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                else if ((*p >= 0xb9)
                    && (*p <= 0xbe)
                    && (*p % 2)) /* Odd */
                    (*p)++; /* Next char is lwr */
                else if (*p == 0xbf) {
                    *pExtChar = 0xc5;
                    (*p) = 0x80;
                }
                break;
            case 0xc5: /* Latin ext */
                if ((*p >= 0x81)
                    && (*p <= 0x88)
                    && (*p % 2)) /* Odd */
                    (*p)++; /* Next char is lwr */
                else if ((*p >= 0x8a)
                    && (*p <= 0xb7)
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                else if (*p == 0xb8) {
                    *pExtChar = 0xc3;
                    (*p) = 0xbf;
                }
                else if ((*p >= 0xb9)
                    && (*p <= 0xbe)
                    && (*p % 2)) /* Odd */
                    (*p)++; /* Next char is lwr */
                break;
            case 0xc6: /* Latin ext */
                switch (*p) {
                case 0x81:
                    *pExtChar = 0xc9;
                    (*p) = 0x93;
                    break;
                case 0x86:
                    *pExtChar = 0xc9;
                    (*p) = 0x94;
                    break;
                case 0x89:
                    *pExtChar = 0xc9;
                    (*p) = 0x96;
                    break;
                case 0x8a:
                    *pExtChar = 0xc9;
                    (*p) = 0x97;
                    break;
                case 0x8e:
                    *pExtChar = 0xc9;
                    (*p) = 0x98;
                    break;
                case 0x8f:
                    *pExtChar = 0xc9;
                    (*p) = 0x99;
                    break;
                case 0x90:
                    *pExtChar = 0xc9;
                    (*p) = 0x9b;
                    break;
                case 0x93:
                    *pExtChar = 0xc9;
                    (*p) = 0xa0;
                    break;
                case 0x94:
                    *pExtChar = 0xc9;
                    (*p) = 0xa3;
                    break;
                case 0x96:
                    *pExtChar = 0xc9;
                    (*p) = 0xa9;
                    break;
                case 0x97:
                    *pExtChar = 0xc9;
                    (*p) = 0xa8;
                    break;
                case 0x9c:
                    *pExtChar = 0xc9;
                    (*p) = 0xaf;
                    break;
                case 0x9d:
                    *pExtChar = 0xc9;
                    (*p) = 0xb2;
                    break;
                case 0x9f:
                    *pExtChar = 0xc9;
                    (*p) = 0xb5;
                    break;
                case 0xa9:
                    *pExtChar = 0xca;
                    (*p) = 0x83;
                    break;
                case 0xae:
                    *pExtChar = 0xca;
                    (*p) = 0x88;
                    break;
                case 0xb1:
                    *pExtChar = 0xca;
                    (*p) = 0x8a;
                    break;
                case 0xb2:
                    *pExtChar = 0xca;
                    (*p) = 0x8b;
                    break;
                case 0xb7:
                    *pExtChar = 0xca;
                    (*p) = 0x92;
                    break;
                case 0x82:
                case 0x84:
                case 0x87:
                case 0x8b:
                case 0x91:
                case 0x98:
                case 0xa0:
                case 0xa2:
                case 0xa4:
                case 0xa7:
                case 0xac:
                case 0xaf:
                case 0xb3:
                case 0xb5:
                case 0xb8:
                case 0xbc:
                    (*p)++; /* Next char is lwr */
                    break;
                default:
                    break;
                }
                break;
            case 0xc7: /* Latin ext */
                if (*p == 0x84)
                    (*p) = 0x86;
                else if (*p == 0x85)
                    (*p)++; /* Next char is lwr */
                else if (*p == 0x87)
                    (*p) = 0x89;
                else if (*p == 0x88)
                    (*p)++; /* Next char is lwr */
                else if (*p == 0x8a)
                    (*p) = 0x8c;
                else if (*p == 0x8b)
                    (*p)++; /* Next char is lwr */
                else if ((*p >= 0x8d)
                    && (*p <= 0x9c)
                    && (*p % 2)) /* Odd */
                    (*p)++; /* Next char is lwr */
                else if ((*p >= 0x9e)
                    && (*p <= 0xaf)
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                else if (*p == 0xb1)
                    (*p) = 0xb3;
                else if (*p == 0xb2)
                    (*p)++; /* Next char is lwr */
                else if (*p == 0xb4)
                    (*p)++; /* Next char is lwr */
                else if (*p == 0xb6) {
                    *pExtChar = 0xc6;
                    (*p) = 0x95;
                }
                else if (*p == 0xb7) {
                    *pExtChar = 0xc6;
                    (*p) = 0xbf;
                }
                else if ((*p >= 0xb8)
                    && (*p <= 0xbf)
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                break;
            case 0xc8: /* Latin ext */
                if ((*p >= 0x80)
                    && (*p <= 0x9f)
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                else if (*p == 0xa0) {
                    *pExtChar = 0xc6;
                    (*p) = 0x9e;
                }
                else if ((*p >= 0xa2)
                    && (*p <= 0xb3)
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                else if (*p == 0xbb)
                    (*p)++; /* Next char is lwr */
                else if (*p == 0xbd) {
                    *pExtChar = 0xc6;
                    (*p) = 0x9a;
                }
                /* 0xba three byte small 0xe2 0xb1 0xa5 */
                /* 0xbe three byte small 0xe2 0xb1 0xa6 */
                break;
            case 0xc9: /* Latin ext */
                if (*p == 0x81)
                    (*p)++; /* Next char is lwr */
                else if (*p == 0x83) {
                    *pExtChar = 0xc6;
                    (*p) = 0x80;
                }
                else if (*p == 0x84) {
                    *pExtChar = 0xca;
                    (*p) = 0x89;
                }
                else if (*p == 0x85) {
                    *pExtChar = 0xca;
                    (*p) = 0x8c;
                }
                else if ((*p >= 0x86)
                    && (*p <= 0x8f)
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                break;
            case 0xcd: /* Greek & Coptic */
                switch (*p) {
                case 0xb0:
                case 0xb2:
                case 0xb6:
                    (*p)++; /* Next char is lwr */
                    break;
                case 0xbf:
                    *pExtChar = 0xcf;
                    (*p) = 0xb3;
                    break;
                default:
                    break;
                }
                break;
            case 0xce: /* Greek & Coptic */
                if (*p == 0x86)
                    (*p) = 0xac;
                else if (*p == 0x88)
                    (*p) = 0xad;
                else if (*p == 0x89)
                    (*p) = 0xae;
                else if (*p == 0x8a)
                    (*p) = 0xaf;
                else if (*p == 0x8c) {
                    *pExtChar = 0xcf;
                    (*p) = 0x8c;
                }
                else if (*p == 0x8e) {
                    *pExtChar = 0xcf;
                    (*p) = 0x8d;
                }
                else if (*p == 0x8f) {
                    *pExtChar = 0xcf;
                    (*p) = 0x8e;
                }
                else if ((*p >= 0x91)
                    && (*p <= 0x9f))
                    (*p) += 0x20; /* US ASCII shift */
                else if ((*p >= 0xa0)
                    && (*p <= 0xab)
                    && (*p != 0xa2)) {
                    *pExtChar = 0xcf;
                    (*p) -= 0x20;
                }
                break;
            case 0xcf: /* Greek & Coptic */
                if (*p == 0x8f)
                    (*p) = 0x97;
                else if ((*p >= 0x98)
                    && (*p <= 0xaf)
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                else if (*p == 0xb4) {
                    (*p) = 0x91;
                }
                else if (*p == 0xb7)
                    (*p)++; /* Next char is lwr */
                else if (*p == 0xb9)
                    (*p) = 0xb2;
                else if (*p == 0xba)
                    (*p)++; /* Next char is lwr */
                else if (*p == 0xbd) {
                    *pExtChar = 0xcd;
                    (*p) = 0xbb;
                }
                else if (*p == 0xbe) {
                    *pExtChar = 0xcd;
                    (*p) = 0xbc;
                }
                else if (*p == 0xbf) {
                    *pExtChar = 0xcd;
                    (*p) = 0xbd;
                }
                break;
            case 0xd0: /* Cyrillic */
                if ((*p >= 0x80)
                    && (*p <= 0x8f)) {
                    *pExtChar = 0xd1;
                    (*p) += 0x10;
                }
                else if ((*p >= 0x90)
                    && (*p <= 0x9f))
                    (*p) += 0x20; /* US ASCII shift */
                else if ((*p >= 0xa0)
                    && (*p <= 0xaf)) {
                    *pExtChar = 0xd1;
                    (*p) -= 0x20;
                }
                break;
            case 0xd1: /* Cyrillic supplement */
                if ((*p >= 0xa0)
                    && (*p <= 0xbf)
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                break;
            case 0xd2: /* Cyrillic supplement */
                if (*p == 0x80)
                    (*p)++; /* Next char is lwr */
                else if ((*p >= 0x8a)
                    && (*p <= 0xbf)
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                break;
            case 0xd3: /* Cyrillic supplement */
                if (*p == 0x80)
                    (*p) = 0x8f;
                else if ((*p >= 0x81)
                    && (*p <= 0x8e)
                    && (*p % 2)) /* Odd */
                    (*p)++; /* Next char is lwr */
                else if ((*p >= 0x90)
                    && (*p <= 0xbf)
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                break;
            case 0xd4: /* Cyrillic supplement & Armenian */
                if ((*p >= 0x80)
                    && (*p <= 0xaf)
                    && (!(*p % 2))) /* Even */
                    (*p)++; /* Next char is lwr */
                else if ((*p >= 0xb1)
                    && (*p <= 0xbf)) {
                    *pExtChar = 0xd5;
                    (*p) -= 0x10;
                }
                break;
            case 0xd5: /* Armenian */
                if ((*p >= 0x80)
                    && (*p <= 0x8f)) {
                    (*p) += 0x30;
                }
                else if ((*p >= 0x90)
                    && (*p <= 0x96)) {
                    *pExtChar = 0xd6;
                    (*p) -= 0x10;
                }
                break;
            case 0xe1: /* Three byte code */
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0x82: /* Georgian asomtavruli */
                    if ((*p >= 0xa0)
                        && (*p <= 0xbf)) {
                        *pExtChar = 0x83;
                        (*p) -= 0x10;
                    }
                    break;
                case 0x83: /* Georgian asomtavruli */
                    if (((*p >= 0x80)
                        && (*p <= 0x85))
                        || (*p == 0x87)
                        || (*p == 0x8d))
                        (*p) += 0x30;
                    break;
                case 0x8e: /* Cherokee */
                    if ((*p >= 0xa0)
                        && (*p <= 0xaf)) {
                        *(p - 2) = 0xea;
                        *pExtChar = 0xad;
                        (*p) += 0x10;
                    }
                    else if ((*p >= 0xb0)
                        && (*p <= 0xbf)) {
                        *(p - 2) = 0xea;
                        *pExtChar = 0xae;
                        (*p) -= 0x30;
                    }
                    break;
                case 0x8f: /* Cherokee */
                    if ((*p >= 0x80)
                        && (*p <= 0xaf)) {
                        *(p - 2) = 0xea;
                        *pExtChar = 0xae;
                        (*p) += 0x10;
                    }
                    else if ((*p >= 0xb0)
                        && (*p <= 0xb5)) {
                        (*p) += 0x08;
                    }
                    /* 0xbe three byte small 0xe2 0xb1 0xa6 */
                    break;
                case 0xb2: /* Georgian mtavruli */
                    if (((*p >= 0x90)
                        && (*p <= 0xba))
                        || (*p == 0xbd)
                        || (*p == 0xbe)
                        || (*p == 0xbf))
                        *pExtChar = 0x83;
                    break;
                case 0xb8: /* Latin ext */
                    if ((*p >= 0x80)
                        && (*p <= 0xbf)
                        && (!(*p % 2))) /* Even */
                        (*p)++; /* Next char is lwr */
                    break;
                case 0xb9: /* Latin ext */
                    if ((*p >= 0x80)
                        && (*p <= 0xbf)
                        && (!(*p % 2))) /* Even */
                        (*p)++; /* Next char is lwr */
                    break;
                case 0xba: /* Latin ext */
                    if ((*p >= 0x80)
                        && (*p <= 0x94)
                        && (!(*p % 2))) /* Even */
                        (*p)++; /* Next char is lwr */
                    else if ((*p >= 0xa0)
                        && (*p <= 0xbf)
                        && (!(*p % 2))) /* Even */
                        (*p)++; /* Next char is lwr */
                    /* 0x9e Two byte small 0xc3 0x9f */
                    break;
                case 0xbb: /* Latin ext */
                    if ((*p >= 0x80)
                        && (*p <= 0xbf)
                        && (!(*p % 2))) /* Even */
                        (*p)++; /* Next char is lwr */
                    break;
                case 0xbc: /* Greek ex */
                    if ((*p >= 0x88)
                        && (*p <= 0x8f))
                        (*p) -= 0x08;
                    else if ((*p >= 0x98)
                        && (*p <= 0x9d))
                        (*p) -= 0x08;
                    else if ((*p >= 0xa8)
                        && (*p <= 0xaf))
                        (*p) -= 0x08;
                    else if ((*p >= 0xb8)
                        && (*p <= 0xbf))
                        (*p) -= 0x08;
                    break;
                case 0xbd: /* Greek ex */
                    if ((*p >= 0x88)
                        && (*p <= 0x8d))
                        (*p) -= 0x08;
                    else if ((*p == 0x99)
                        || (*p == 0x9b)
                        || (*p == 0x9d)
                        || (*p == 0x9f))
                        (*p) -= 0x08;
                    else if ((*p >= 0xa8)
                        && (*p <= 0xaf))
                        (*p) -= 0x08;
                    break;
                case 0xbe: /* Greek ex */
                    if ((*p >= 0x88)
                        && (*p <= 0x8f))
                        (*p) -= 0x08;
                    else if ((*p >= 0x98)
                        && (*p <= 0x9f))
                        (*p) -= 0x08;
                    else if ((*p >= 0xa8)
                        && (*p <= 0xaf))
                        (*p) -= 0x08;
                    else if ((*p >= 0xb8)
                        && (*p <= 0xb9))
                        (*p) -= 0x08;
                    else if ((*p >= 0xba)
                        && (*p <= 0xbb)) {
                        *(p - 1) = 0xbd;
                        (*p) -= 0x0a;
                    }
                    else if (*p == 0xbc)
                        (*p) -= 0x09;
                    break;
                case 0xbf: /* Greek ex */
                    if ((*p >= 0x88)
                        && (*p <= 0x8b)) {
                        *(p - 1) = 0xbd;
                        (*p) += 0x2a;
                    }
                    else if (*p == 0x8c)
                        (*p) -= 0x09;
                    else if ((*p >= 0x98)
                        && (*p <= 0x99))
                        (*p) -= 0x08;
                    else if ((*p >= 0x9a)
                        && (*p <= 0x9b)) {
                        *(p - 1) = 0xbd;
                        (*p) += 0x1c;
                    }
                    else if ((*p >= 0xa8)
                        && (*p <= 0xa9))
                        (*p) -= 0x08;
                    else if ((*p >= 0xaa)
                        && (*p <= 0xab)) {
                        *(p - 1) = 0xbd;
                        (*p) += 0x10;
                    }
                    else if (*p == 0xac)
                        (*p) -= 0x07;
                    else if ((*p >= 0xb8)
                        && (*p <= 0xb9)) {
                        *(p - 1) = 0xbd;
                    }
                    else if ((*p >= 0xba)
                        && (*p <= 0xbb)) {
                        *(p - 1) = 0xbd;
                        (*p) += 0x02;
                    }
                    else if (*p == 0xbc)
                        (*p) -= 0x09;
                    break;
                default:
                    break;
                }
                break;
            case 0xe2: /* Three byte code */
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0xb0: /* Glagolitic */
                    if ((*p >= 0x80)
                        && (*p <= 0x8f)) {
                        (*p) += 0x30;
                    }
                    else if ((*p >= 0x90)
                        && (*p <= 0xae)) {
                        *pExtChar = 0xb1;
                        (*p) -= 0x10;
                    }
                    break;
                case 0xb1: /* Latin ext */
                    switch (*p) {
                    case 0xa0:
                    case 0xa7:
                    case 0xa9:
                    case 0xab:
                    case 0xb2:
                    case 0xb5:
                        (*p)++; /* Next char is lwr */
                        break;
                    case 0xa2: /* Two byte small 0xc9 0xab */
                    case 0xa4: /* Two byte small 0xc9 0xbd */
                    case 0xad: /* Two byte small 0xc9 0x91 */
                    case 0xae: /* Two byte small 0xc9 0xb1 */
                    case 0xaf: /* Two byte small 0xc9 0x90 */
                    case 0xb0: /* Two byte small 0xc9 0x92 */
                    case 0xbe: /* Two byte small 0xc8 0xbf */
                    case 0xbf: /* Two byte small 0xc9 0x80 */
                        break;
                    case 0xa3:
                        *(p - 2) = 0xe1;
                        *(p - 1) = 0xb5;
                        *(p) = 0xbd;
                        break;
                    default:
                        break;
                    }
                    break;
                case 0xb2: /* Coptic */
                    if ((*p >= 0x80)
                        && (*p <= 0xbf)
                        && (!(*p % 2))) /* Even */
                        (*p)++; /* Next char is lwr */
                    break;
                case 0xb3: /* Coptic */
                    if (((*p >= 0x80)
                        && (*p <= 0xa3)
                        && (!(*p % 2))) /* Even */
                        || (*p == 0xab)
                        || (*p == 0xad)
                        || (*p == 0xb2))
                        (*p)++; /* Next char is lwr */
                    break;
                case 0xb4: /* Georgian nuskhuri */
                    if (((*p >= 0x80)
                        && (*p <= 0xa5))
                        || (*p == 0xa7)
                        || (*p == 0xad)) {
                        *(p - 2) = 0xe1;
                        *(p - 1) = 0x83;
                        (*p) += 0x10;
                    }
                    break;
                default:
                    break;
                }
                break;
            case 0xea: /* Three byte code */
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0x99: /* Cyrillic */
                    if ((*p >= 0x80)
                        && (*p <= 0xad)
                        && (!(*p % 2))) /* Even */
                        (*p)++; /* Next char is lwr */
                    break;
                case 0x9a: /* Cyrillic */
                    if ((*p >= 0x80)
                        && (*p <= 0x9b)
                        && (!(*p % 2))) /* Even */
                        (*p)++; /* Next char is lwr */
                    break;
                case 0x9c: /* Latin ext */
                    if ((((*p >= 0xa2)
                        && (*p <= 0xaf))
                        || ((*p >= 0xb2)
                            && (*p <= 0xbf)))
                        && (!(*p % 2))) /* Even */
                        (*p)++; /* Next char is lwr */
                    break;
                case 0x9d: /* Latin ext */
                    if ((((*p >= 0x80)
                        && (*p <= 0xaf))
                        && (!(*p % 2))) /* Even */
                        || (*p == 0xb9)
                        || (*p == 0xbb)
                        || (*p == 0xbe))
                        (*p)++; /* Next char is lwr */
                    else if (*p == 0xbd) {
                        *(p - 2) = 0xe1;
                        *(p - 1) = 0xb5;
                        *(p) = 0xb9;
                    }
                    break;
                case 0x9e: /* Latin ext */
                    if (((((*p >= 0x80)
                        && (*p <= 0x87))
                        || ((*p >= 0x96)
                            && (*p <= 0xa9))
                        || ((*p >= 0xb4)
                            && (*p <= 0xbf)))
                        && (!(*p % 2))) /* Even */
                        || (*p == 0x8b)
                        || (*p == 0x90)
                        || (*p == 0x92))
                        (*p)++; /* Next char is lwr */
                    else if (*p == 0xb3) {
                        *(p - 2) = 0xea;
                        *(p - 1) = 0xad;
                        *(p) = 0x93;
                    }
                    /* case 0x8d: // Two byte small 0xc9 0xa5 */
                    /* case 0xaa: // Two byte small 0xc9 0xa6 */
                    /* case 0xab: // Two byte small 0xc9 0x9c */
                    /* case 0xac: // Two byte small 0xc9 0xa1 */
                    /* case 0xad: // Two byte small 0xc9 0xac */
                    /* case 0xae: // Two byte small 0xc9 0xaa */
                    /* case 0xb0: // Two byte small 0xca 0x9e */
                    /* case 0xb1: // Two byte small 0xca 0x87 */
                    /* case 0xb2: // Two byte small 0xca 0x9d */
                    break;
                case 0x9f: /* Latin ext */
                    if ((*p == 0x82)
                        || (*p == 0x87)
                        || (*p == 0x89)
                        || (*p == 0xb5))
                        (*p)++; /* Next char is lwr */
                    else if (*p == 0x84) {
                        *(p - 2) = 0xea;
                        *(p - 1) = 0x9e;
                        *(p) = 0x94;
                    }
                    else if (*p == 0x86) {
                        *(p - 2) = 0xe1;
                        *(p - 1) = 0xb6;
                        *(p) = 0x8e;
                    }
                    /* case 0x85: // Two byte small 0xca 0x82 */
                    break;
                default:
                    break;
                }
                break;
            case 0xef: /* Three byte code */
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0xbc: /* Latin fullwidth */
                    if ((*p >= 0xa1)
                        && (*p <= 0xba)) {
                        *pExtChar = 0xbd;
                        (*p) -= 0x20;
                    }
                    break;
                default:
                    break;
                }
                break;
            case 0xf0: /* Four byte code */
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0x90:
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0x90: /* Deseret */
                        if ((*p >= 0x80)
                            && (*p <= 0x97)) {
                            (*p) += 0x28;
                        }
                        else if ((*p >= 0x98)
                            && (*p <= 0xa7)) {
                            *pExtChar = 0x91;
                            (*p) -= 0x18;
                        }
                        break;
                    case 0x92: /* Osage  */
                        if ((*p >= 0xb0)
                            && (*p <= 0xbf)) {
                            *pExtChar = 0x93;
                            (*p) -= 0x18;
                        }
                        break;
                    case 0x93: /* Osage  */
                        if ((*p >= 0x80)
                            && (*p <= 0x93))
                            (*p) += 0x28;
                        break;
                    case 0xb2: /* Old hungarian */
                        if ((*p >= 0x80)
                            && (*p <= 0xb2))
                            *pExtChar = 0xb3;
                        break;
                    default:
                        break;
                    }
                    break;
                case 0x91:
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0xa2: /* Warang citi */
                        if ((*p >= 0xa0)
                            && (*p <= 0xbf)) {
                            *pExtChar = 0xa3;
                            (*p) -= 0x20;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case 0x96:
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0xb9: /* Medefaidrin */
                        if ((*p >= 0x80)
                            && (*p <= 0x9f)) {
                            (*p) += 0x20;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case 0x9E:
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0xA4: /* Adlam */
                        if ((*p >= 0x80)
                            && (*p <= 0x9d))
                            (*p) += 0x22;
                        else if ((*p >= 0x9e)
                            && (*p <= 0xa1)) {
                            *(pExtChar) = 0xa5;
                            (*p) -= 0x1e;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                default:
                    break;
                }
                break;
            default:
                break;
            }
            pExtChar = 0;
        }
        p++;
    }
    
    return result;
}

string_t string_to_upper_utf8(char* buf, size_t capacity, const char* str, size_t length)
{
    string_t result = string_copy(buf, capacity, str, length);

    unsigned char* pExtChar = 0;
    unsigned char* p = (unsigned char*)result.str;

    if (str == nullptr || length == 0 || str[0] == 0)
        return result;

    while (*p && p < (unsigned char*)(result.str + result.length))
    {
        if ((*p >= 0x61) && (*p <= 0x7a)) /* US ASCII */
            (*p) -= 0x20;
        else if (*p > 0xc0) {
            pExtChar = p;
            p++;
            switch (*pExtChar) {
            case 0xc3: /* Latin 1 */
                /* 0x9f Three byte capital 0xe1 0xba 0x9e */
                if ((*p >= 0xa0)
                    && (*p <= 0xbe)
                    && (*p != 0xb7))
                    (*p) -= 0x20; /* US ASCII shift */
                else if (*p == 0xbf) {
                    *pExtChar = 0xc5;
                    (*p) = 0xb8;
                }
                break;
            case 0xc4: /* Latin ext */
                if (((*p >= 0x80)
                    && (*p <= 0xb7)
                    && (*p != 0xb1))
                    && (*p % 2)) /* Odd */
                    (*p)--; /* Prev char is upr */
                else if ((*p >= 0xb9)
                    && (*p <= 0xbe)
                    && (!(*p % 2))) /* Even */
                    (*p)--; /* Prev char is upr */
                break;
            case 0xc5: /* Latin ext */
                if (*p == 0x80) {
                    *pExtChar = 0xc4;
                    (*p) = 0xbf;
                }
                else if ((*p >= 0x81)
                    && (*p <= 0x88)
                    && (!(*p % 2))) /* Even */
                    (*p)--; /* Prev char is upr */
                else if ((*p >= 0x8a)
                    && (*p <= 0xb7)
                    && (*p % 2)) /* Odd */
                    (*p)--; /* Prev char is upr */
                else if (*p == 0xb8) {
                    *pExtChar = 0xc5;
                    (*p) = 0xb8;
                }
                else if ((*p >= 0xb9)
                    && (*p <= 0xbe)
                    && (!(*p % 2))) /* Even */
                    (*p)--; /* Prev char is upr */
                break;
            case 0xc6: /* Latin ext */
                switch (*p) {
                case 0x83:
                case 0x85:
                case 0x88:
                case 0x8c:
                case 0x92:
                case 0x99:
                case 0xa1:
                case 0xa3:
                case 0xa5:
                case 0xa8:
                case 0xad:
                case 0xb0:
                case 0xb4:
                case 0xb6:
                case 0xb9:
                case 0xbd:
                    (*p)--; /* Prev char is upr */
                    break;
                case 0x80:
                    *pExtChar = 0xc9;
                    (*p) = 0x83;
                    break;
                case 0x95:
                    *pExtChar = 0xc7;
                    (*p) = 0xb6;
                    break;
                case 0x9a:
                    *pExtChar = 0xc8;
                    (*p) = 0xbd;
                    break;
                case 0x9e:
                    *pExtChar = 0xc8;
                    (*p) = 0xa0;
                    break;
                case 0xbf:
                    *pExtChar = 0xc7;
                    (*p) = 0xb7;
                    break;
                default:
                    break;
                }
                break;
            case 0xc7: /* Latin ext */
                if (*p == 0x85)
                    (*p)--; /* Prev char is upr */
                else if (*p == 0x86)
                    (*p) = 0x84;
                else if (*p == 0x88)
                    (*p)--; /* Prev char is upr */
                else if (*p == 0x89)
                    (*p) = 0x87;
                else if (*p == 0x8b)
                    (*p)--; /* Prev char is upr */
                else if (*p == 0x8c)
                    (*p) = 0x8a;
                else if ((*p >= 0x8d)
                    && (*p <= 0x9c)
                    && (!(*p % 2))) /* Even */
                    (*p)--; /* Prev char is upr */
                else if ((*p >= 0x9e)
                    && (*p <= 0xaf)
                    && (*p % 2)) /* Odd */
                    (*p)--; /* Prev char is upr */
                else if (*p == 0xb2)
                    (*p)--; /* Prev char is upr */
                else if (*p == 0xb3)
                    (*p) = 0xb1;
                else if (*p == 0xb5)
                    (*p)--; /* Prev char is upr */
                else if ((*p >= 0xb9)
                    && (*p <= 0xbf)
                    && (*p % 2)) /* Odd */
                    (*p)--; /* Prev char is upr */
                break;
            case 0xc8: /* Latin ext */
                if ((*p >= 0x80)
                    && (*p <= 0x9f)
                    && (*p % 2)) /* Odd */
                    (*p)--; /* Prev char is upr */
                else if ((*p >= 0xa2)
                    && (*p <= 0xb3)
                    && (*p % 2)) /* Odd */
                    (*p)--; /* Prev char is upr */
                else if (*p == 0xbc)
                    (*p)--; /* Prev char is upr */
                /* 0xbf Three byte capital 0xe2 0xb1 0xbe */
                break;
            case 0xc9: /* Latin ext */
                switch (*p) {
                case 0x80: /* Three byte capital 0xe2 0xb1 0xbf */
                case 0x90: /* Three byte capital 0xe2 0xb1 0xaf */
                case 0x91: /* Three byte capital 0xe2 0xb1 0xad */
                case 0x92: /* Three byte capital 0xe2 0xb1 0xb0 */
                case 0x9c: /* Three byte capital 0xea 0x9e 0xab */
                case 0xa1: /* Three byte capital 0xea 0x9e 0xac */
                case 0xa5: /* Three byte capital 0xea 0x9e 0x8d */
                case 0xa6: /* Three byte capital 0xea 0x9e 0xaa */
                case 0xab: /* Three byte capital 0xe2 0xb1 0xa2 */
                case 0xac: /* Three byte capital 0xea 0x9e 0xad */
                case 0xb1: /* Three byte capital 0xe2 0xb1 0xae */
                case 0xbd: /* Three byte capital 0xe2 0xb1 0xa4 */
                    break;
                case 0x82:
                    (*p)--; /* Prev char is upr */
                    break;
                case 0x93:
                    *pExtChar = 0xc6;
                    (*p) = 0x81;
                    break;
                case 0x94:
                    *pExtChar = 0xc6;
                    (*p) = 0x86;
                    break;
                case 0x96:
                    *pExtChar = 0xc6;
                    (*p) = 0x89;
                    break;
                case 0x97:
                    *pExtChar = 0xc6;
                    (*p) = 0x8a;
                    break;
                case 0x98:
                    *pExtChar = 0xc6;
                    (*p) = 0x8e;
                    break;
                case 0x99:
                    *pExtChar = 0xc6;
                    (*p) = 0x8f;
                    break;
                case 0x9b:
                    *pExtChar = 0xc6;
                    (*p) = 0x90;
                    break;
                case 0xa0:
                    *pExtChar = 0xc6;
                    (*p) = 0x93;
                    break;
                case 0xa3:
                    *pExtChar = 0xc6;
                    (*p) = 0x94;
                    break;
                case 0xa8:
                    *pExtChar = 0xc6;
                    (*p) = 0x97;
                    break;
                case 0xa9:
                    *pExtChar = 0xc6;
                    (*p) = 0x96;
                    break;
                case 0xaf:
                    *pExtChar = 0xc6;
                    (*p) = 0x9c;
                    break;
                case 0xb2:
                    *pExtChar = 0xc6;
                    (*p) = 0x9d;
                    break;
                case 0xb5:
                    *pExtChar = 0xc6;
                    (*p) = 0x9f;
                    break;
                default:
                    if ((*p >= 0x87)
                        && (*p <= 0x8f)
                        && (*p % 2)) /* Odd */
                        (*p)--; /* Prev char is upr */
                    break;
                }
                break;

            case 0xca: /* Latin ext */
                switch (*p) {
                case 0x82: /* Three byte capital 0xea 0x9f 0x85 */
                case 0x87: /* Three byte capital 0xea 0x9e 0xb1 */
                case 0x9d: /* Three byte capital 0xea 0x9e 0xb2 */
                case 0x9e: /* Three byte capital 0xea 0x9e 0xb0 */
                    break;
                case 0x83:
                    *pExtChar = 0xc6;
                    (*p) = 0xa9;
                    break;
                case 0x88:
                    *pExtChar = 0xc6;
                    (*p) = 0xae;
                    break;
                case 0x89:
                    *pExtChar = 0xc9;
                    (*p) = 0x84;
                    break;
                case 0x8a:
                    *pExtChar = 0xc6;
                    (*p) = 0xb1;
                    break;
                case 0x8b:
                    *pExtChar = 0xc6;
                    (*p) = 0xb2;
                    break;
                case 0x8c:
                    *pExtChar = 0xc9;
                    (*p) = 0x85;
                    break;
                case 0x92:
                    *pExtChar = 0xc6;
                    (*p) = 0xb7;
                    break;
                default:
                    break;
                }
                break;
            case 0xcd: /* Greek & Coptic */
                switch (*p) {
                case 0xb1:
                case 0xb3:
                case 0xb7:
                    (*p)--; /* Prev char is upr */
                    break;
                case 0xbb:
                    *pExtChar = 0xcf;
                    (*p) = 0xbd;
                    break;
                case 0xbc:
                    *pExtChar = 0xcf;
                    (*p) = 0xbe;
                    break;
                case 0xbd:
                    *pExtChar = 0xcf;
                    (*p) = 0xbf;
                    break;
                default:
                    break;
                }
                break;
            case 0xce: /* Greek & Coptic */
                if (*p == 0xac)
                    (*p) = 0x86;
                else if (*p == 0xad)
                    (*p) = 0x88;
                else if (*p == 0xae)
                    (*p) = 0x89;
                else if (*p == 0xaf)
                    (*p) = 0x8a;
                else if ((*p >= 0xb1)
                    && (*p <= 0xbf))
                    (*p) -= 0x20; /* US ASCII shift */
                break;
            case 0xcf: /* Greek & Coptic */
                if (*p == 0x82) {
                    *pExtChar = 0xce;
                    (*p) = 0xa3;
                }
                else if ((*p >= 0x80)
                    && (*p <= 0x8b)) {
                    *pExtChar = 0xce;
                    (*p) += 0x20;
                }
                else if (*p == 0x8c) {
                    *pExtChar = 0xce;
                    (*p) = 0x8c;
                }
                else if (*p == 0x8d) {
                    *pExtChar = 0xce;
                    (*p) = 0x8e;
                }
                else if (*p == 0x8e) {
                    *pExtChar = 0xce;
                    (*p) = 0x8f;
                }
                else if (*p == 0x91)
                    (*p) = 0xb4;
                else if (*p == 0x97)
                    (*p) = 0x8f;
                else if ((*p >= 0x98)
                    && (*p <= 0xaf)
                    && (*p % 2)) /* Odd */
                    (*p)--; /* Prev char is upr */
                else if (*p == 0xb2)
                    (*p) = 0xb9;
                else if (*p == 0xb3) {
                    *pExtChar = 0xcd;
                    (*p) = 0xbf;
                }
                else if (*p == 0xb8)
                    (*p)--; /* Prev char is upr */
                else if (*p == 0xbb)
                    (*p)--; /* Prev char is upr */
                break;
            case 0xd0: /* Cyrillic */
                if ((*p >= 0xb0)
                    && (*p <= 0xbf))
                    (*p) -= 0x20; /* US ASCII shift */
                break;
            case 0xd1: /* Cyrillic supplement */
                if ((*p >= 0x80)
                    && (*p <= 0x8f)) {
                    *pExtChar = 0xd0;
                    (*p) += 0x20;
                }
                else if ((*p >= 0x90)
                    && (*p <= 0x9f)) {
                    *pExtChar = 0xd0;
                    (*p) -= 0x10;
                }
                else if ((*p >= 0xa0)
                    && (*p <= 0xbf)
                    && (*p % 2)) /* Odd */
                    (*p)--; /* Prev char is upr */
                break;
            case 0xd2: /* Cyrillic supplement */
                if (*p == 0x81)
                    (*p)--; /* Prev char is upr */
                else if ((*p >= 0x8a)
                    && (*p <= 0xbf)
                    && (*p % 2)) /* Odd */
                    (*p)--; /* Prev char is upr */
                break;
            case 0xd3: /* Cyrillic supplement */
                if ((*p >= 0x81)
                    && (*p <= 0x8e)
                    && (!(*p % 2))) /* Even */
                    (*p)--; /* Prev char is upr */
                else if (*p == 0x8f)
                    (*p) = 0x80;
                else if ((*p >= 0x90)
                    && (*p <= 0xbf)
                    && (*p % 2)) /* Odd */
                    (*p)--; /* Prev char is upr */
                break;
            case 0xd4: /* Cyrillic supplement & Armenian */
                if ((*p >= 0x80)
                    && (*p <= 0xaf)
                    && (*p % 2)) /* Odd */
                    (*p)--; /* Prev char is upr */
                break;
            case 0xd5: /* Armenian */
                if ((*p >= 0xa1)
                    && (*p <= 0xaf)) {
                    *pExtChar = 0xd4;
                    (*p) += 0x10;
                }
                else if ((*p >= 0xb0)
                    && (*p <= 0xbf)) {
                    (*p) -= 0x30;
                }
                break;
            case 0xd6: /* Armenian */
                if ((*p >= 0x80)
                    && (*p <= 0x86)) {
                    *pExtChar = 0xd5;
                    (*p) += 0x10;
                }
                break;
            case 0xe1: /* Three byte code */
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0x82: /* Georgian Asomtavruli  */
                    if ((*p >= 0xa0)
                        && (*p <= 0xbf)) {
                        *pExtChar = 0xb2;
                        (*p) -= 0x10;
                    }
                    break;
                case 0x83: /* Georgian */
                    /* Georgian Asomtavruli  */
                    if (((*p >= 0x80)
                        && (*p <= 0x85))
                        || (*p == 0x87)
                        || (*p == 0x8d)) {
                        *pExtChar = 0xb2;
                        (*p) += 0x30;
                    }
                    /* Georgian mkhedruli */
                    else if (((*p >= 0x90)
                        && (*p <= 0xba))
                        || (*p == 0xbd)
                        || (*p == 0xbe)
                        || (*p == 0xbf)) {
                        *pExtChar = 0xb2;
                    }
                    break;
                case 0x8f: /* Cherokee */
                    if ((*p >= 0xb8)
                        && (*p <= 0xbd)) {
                        (*p) -= 0x08;
                    }
                    break;
                case 0xb5: /* Latin ext */
                    if (*p == 0xb9) {
                        *(p - 2) = 0xea;
                        *(p - 1) = 0x9d;
                        (*p) = 0xbd;
                    }
                    else if (*p == 0xbd) {
                        *(p - 2) = 0xe2;
                        *(p - 1) = 0xb1;
                        (*p) = 0xa3;
                    }
                    break;
                case 0xb6: /* Latin ext */
                    if (*p == 0x8e) {
                        *(p - 2) = 0xea;
                        *(p - 1) = 0x9f;
                        (*p) = 0x86;
                    }
                    break;
                case 0xb8: /* Latin ext */
                    if ((*p >= 0x80)
                        && (*p <= 0xbf)
                        && (*p % 2)) /* Odd */
                        (*p)--; /* Prev char is upr */
                    break;
                case 0xb9: /* Latin ext */
                    if ((*p >= 0x80)
                        && (*p <= 0xbf)
                        && (*p % 2)) /* Odd */
                        (*p)--; /* Prev char is upr */
                    break;
                case 0xba: /* Latin ext */
                    if ((*p >= 0x80)
                        && (*p <= 0x95)
                        && (*p % 2)) /* Odd */
                        (*p)--; /* Prev char is upr */
                    else if ((*p >= 0xa0)
                        && (*p <= 0xbf)
                        && (*p % 2)) /* Odd */
                        (*p)--; /* Prev char is upr */
                    break;
                case 0xbb: /* Latin ext */
                    if ((*p >= 0x80)
                        && (*p <= 0xbf)
                        && (*p % 2)) /* Odd */
                        (*p)--; /* Prev char is upr */
                    break;
                case 0xbc: /* Greek ext */
                    if ((*p >= 0x80)
                        && (*p <= 0x87))
                        (*p) += 0x08;
                    else if ((*p >= 0x90)
                        && (*p <= 0x95))
                        (*p) += 0x08;
                    else if ((*p >= 0xa0)
                        && (*p <= 0xa7))
                        (*p) += 0x08;
                    else if ((*p >= 0xb0)
                        && (*p <= 0xb7))
                        (*p) += 0x08;
                    break;
                case 0xbd: /* Greek ext */
                    if ((*p >= 0x80)
                        && (*p <= 0x85))
                        (*p) += 0x08;
                    else if ((*p == 0x91)
                        || (*p == 0x93)
                        || (*p == 0x95)
                        || (*p == 0x97))
                        (*p) += 0x08;
                    else if ((*p >= 0xa0)
                        && (*p <= 0xa7))
                        (*p) += 0x08;
                    else if ((*p >= 0xb0)
                        && (*p <= 0xb1)) {
                        *(p - 1) = 0xbe;
                        (*p) += 0x0a;
                    }
                    else if ((*p >= 0xb2)
                        && (*p <= 0xb5)) {
                        *(p - 1) = 0xbf;
                        (*p) -= 0x2a;
                    }
                    else if ((*p >= 0xb6)
                        && (*p <= 0xb7)) {
                        *(p - 1) = 0xbf;
                        (*p) -= 0x1c;
                    }
                    else if ((*p >= 0xb8)
                        && (*p <= 0xb9)) {
                        *(p - 1) = 0xbf;
                    }
                    else if ((*p >= 0xba)
                        && (*p <= 0xbb)) {
                        *(p - 1) = 0xbf;
                        (*p) -= 0x10;
                    }
                    else if ((*p >= 0xbc)
                        && (*p <= 0xbd)) {
                        *(p - 1) = 0xbf;
                        (*p) -= 0x02;
                    }
                    break;
                case 0xbe: /* Greek ext */
                    if ((*p >= 0x80)
                        && (*p <= 0x87))
                        (*p) += 0x08;
                    else if ((*p >= 0x90)
                        && (*p <= 0x97))
                        (*p) += 0x08;
                    else if ((*p >= 0xa0)
                        && (*p <= 0xa7))
                        (*p) += 0x08;
                    else if ((*p >= 0xb0)
                        && (*p <= 0xb1))
                        (*p) += 0x08;
                    else if (*p == 0xb3)
                        (*p) += 0x09;
                    break;
                case 0xbf: /* Greek ext */
                    if (*p == 0x83)
                        (*p) += 0x09;
                    else if ((*p >= 0x90)
                        && (*p <= 0x91))
                        *p += 0x08;
                    else if ((*p >= 0xa0)
                        && (*p <= 0xa1))
                        (*p) += 0x08;
                    else if (*p == 0xa5)
                        (*p) += 0x07;
                    else if (*p == 0xb3)
                        (*p) += 0x09;
                    break;
                default:
                    break;
                }
                break;
            case 0xe2: /* Three byte code */
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0xb0: /* Glagolitic  */
                    if ((*p >= 0xb0)
                        && (*p <= 0xbf)) {
                        (*p) -= 0x30;
                    }
                    break;
                case 0xb1: /* Glagolitic */
                    if ((*p >= 0x80)
                        && (*p <= 0x9e)) {
                        *pExtChar = 0xb0;
                        (*p) += 0x10;
                    }
                    else { /* Latin ext */
                        switch (*p) {
                        case 0xa1:
                        case 0xa8:
                        case 0xaa:
                        case 0xac:
                        case 0xb3:
                        case 0xb6:
                            (*p)--; /* Prev char is upr */
                            break;
                        case 0xa5: /* Two byte capital  0xc8 0xba */
                        case 0xa6: /* Two byte capital  0xc8 0xbe */
                            break;
                        default:
                            break;
                        }
                    }
                    break;
                case 0xb2: /* Coptic */
                    if ((*p >= 0x80)
                        && (*p <= 0xbf)
                        && (*p % 2)) /* Odd */
                        (*p)--; /* Prev char is upr */
                    break;
                case 0xb3: /* Coptic */
                    if (((*p >= 0x80)
                        && (*p <= 0xa3)
                        && (*p % 2)) /* Odd */
                        || (*p == 0xac)
                        || (*p == 0xae)
                        || (*p == 0xb3))
                        (*p)--; /* Prev char is upr */
                    break;
                case 0xb4: /* Georgian */
                    if (((*p >= 0x80)
                        && (*p <= 0xa5))
                        || (*p == 0xa7)
                        || (*p == 0xad)) {
                        *(p - 2) = 0xe1;
                        *(p - 1) = 0xb2;
                        *(p) += 0x10;
                    }
                    break;
                default:
                    break;
                }
                break;
            case 0xea: /* Three byte code */
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0x99: /* Cyrillic */
                    if ((*p >= 0x80)
                        && (*p <= 0xad)
                        && (*p % 2)) /* Odd */
                        (*p)--; /* Prev char is upr */
                    break;
                case 0x9a: /* Cyrillic */
                    if ((*p >= 0x80)
                        && (*p <= 0x9b)
                        && (*p % 2)) /* Odd */
                        (*p)--; /* Prev char is upr */
                    break;
                case 0x9c: /* Latin ext */
                    if ((((*p >= 0xa2)
                        && (*p <= 0xaf))
                        || ((*p >= 0xb2)
                            && (*p <= 0xbf)))
                        && (*p % 2)) /* Odd */
                        (*p)--; /* Prev char is upr */
                    break;
                case 0x9d: /* Latin ext */
                    if (((*p >= 0x80)
                        && (*p <= 0xaf)
                        && (*p % 2)) /* Odd */
                        || (*p == 0xba)
                        || (*p == 0xbc)
                        || (*p == 0xbf))
                        (*p)--; /* Prev char is upr */
                    break;
                case 0x9e: /* Latin ext */
                    if (((((*p >= 0x80)
                        && (*p <= 0x87))
                        || ((*p >= 0x96)
                            && (*p <= 0xa9))
                        || ((*p >= 0xb4)
                            && (*p <= 0xbf)))
                        && (*p % 2)) /* Odd */
                        || (*p == 0x8c)
                        || (*p == 0x91)
                        || (*p == 0x93))
                        (*p)--; /* Prev char is upr */
                    else if (*p == 0x94) {
                        *(p - 2) = 0xea;
                        *(p - 1) = 0x9f;
                        *(p) = 0x84;
                    }
                    break;
                case 0x9f: /* Latin ext */
                    if ((*p == 0x83)
                        || (*p == 0x88)
                        || (*p == 0x8a)
                        || (*p == 0xb6))
                        (*p)--; /* Prev char is upr */
                    break;
                case 0xad:
                    /* Latin ext */
                    if (*p == 0x93) {
                        *pExtChar = 0x9e;
                        (*p) = 0xb3;
                    }
                    /* Cherokee */
                    else if ((*p >= 0xb0)
                        && (*p <= 0xbf)) {
                        *(p - 2) = 0xe1;
                        *pExtChar = 0x8e;
                        (*p) -= 0x10;
                    }
                    break;
                case 0xae: /* Cherokee */
                    if ((*p >= 0x80)
                        && (*p <= 0x8f)) {
                        *(p - 2) = 0xe1;
                        *pExtChar = 0x8e;
                        (*p) += 0x30;
                    }
                    else if ((*p >= 0x90)
                        && (*p <= 0xbf)) {
                        *(p - 2) = 0xe1;
                        *pExtChar = 0x8f;
                        (*p) -= 0x10;
                    }
                    break;
                default:
                    break;
                }
                break;
            case 0xef: /* Three byte code */
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0xbd: /* Latin fullwidth */
                    if ((*p >= 0x81)
                        && (*p <= 0x9a)) {
                        *pExtChar = 0xbc;
                        (*p) += 0x20;
                    }
                    break;
                default:
                    break;
                }
                break;
            case 0xf0: /* Four byte code */
                pExtChar = p;
                p++;
                switch (*pExtChar) {
                case 0x90:
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0x90: /* Deseret */
                        if ((*p >= 0xa8)
                            && (*p <= 0xbf)) {
                            (*p) -= 0x28;
                        }
                        break;
                    case 0x91: /* Deseret */
                        if ((*p >= 0x80)
                            && (*p <= 0x8f)) {
                            *pExtChar = 0x90;
                            (*p) += 0x18;
                        }
                        break;
                    case 0x93: /* Osage  */
                        if ((*p >= 0x98)
                            && (*p <= 0xa7)) {
                            *pExtChar = 0x92;
                            (*p) += 0x18;
                        }
                        else if ((*p >= 0xa8)
                            && (*p <= 0xbb))
                            (*p) -= 0x28;
                        break;
                    case 0xb3: /* Old hungarian */
                        if ((*p >= 0x80)
                            && (*p <= 0xb2))
                            *pExtChar = 0xb2;
                        break;
                    default:
                        break;
                    }
                    break;
                case 0x91:
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0xa3: /* Warang citi */
                        if ((*p >= 0x80)
                            && (*p <= 0x9f)) {
                            *pExtChar = 0xa2;
                            (*p) += 0x20;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                case 0x96:
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0xb9: /* Medefaidrin */
                        if ((*p >= 0xa0)
                            && (*p <= 0xbf))
                            (*p) -= 0x20;
                        break;
                    default:
                        break;
                    }
                    break;
                case 0x9E:
                    pExtChar = p;
                    p++;
                    switch (*pExtChar) {
                    case 0xA4: /* Adlam */
                        if ((*p >= 0xa2)
                            && (*p <= 0xbf))
                            (*p) -= 0x22;
                        break;
                    case 0xA5: /* Adlam */
                        if ((*p >= 0x80)
                            && (*p <= 0x83)) {
                            *(pExtChar) = 0xa4;
                            (*p) += 0x1e;
                        }
                        break;
                    default:
                        break;
                    }
                    break;
                }
                break;
            default:
                break;
            }
            pExtChar = 0;
        }
        p++;
    }

    return result;
}

bool string_try_convert_number(const char* str, size_t length, double& out_value)
{
    char* end = (char*)str + length;
    out_value = strtod(str, &end);
    return end == str + length;
}

string_t string_remove_character(char* buf, size_t size, size_t capacity, char char_to_remove)
{
    size_t i = 0;

    size_t pos = string_find(buf, size, char_to_remove, i);
    if (pos == STRING_NPOS)
        return {buf, size};

    string_t result{ buf, 0 };
    while (i < size && result.length < capacity)
    {
        if (pos != STRING_NPOS)
        {
            size_t copy_size = pos - i;
            memcpy(result.str + result.length, buf + i, copy_size);
            result.length += copy_size;
            i = pos + 1;
        }
        else
        {
            size_t copy_size = size - i;
            memcpy(result.str + result.length, buf + i, copy_size);
            result.length += copy_size;
            i = size;
        }

        pos = string_find(buf, size, char_to_remove, i);
    }
    return result;
}

string_t string_remove_line_returns(const char* str, size_t length)
{
    if (string_find(str, length, '\n', 0) == STRING_NPOS)
        return {};

    bool space_injected = false;
    string_t result = string_allocate(0, length + 1);
    for (size_t i = 0; i < length; ++i)
    {
        const char tok = str[i];
        if (tok <= ' ')
        {
            if (!space_injected)
            {
                result.str[result.length++] = ' ';
                space_injected = true;
            }
        }
        else
        {
            result.str[result.length++] = tok;
            space_injected = false;
        }
    }

    result.str[result.length] = '\0';
    return result;
}

string_const_t random_string(char* buf, size_t capacity)
{
    static const char* const strings[] = {
        "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s",
        "t", "u", "v", "w", "x", "y", "z", "0", "1", "2", "3", "4", "5", "6",
        "7", "8", "9", "!", "@", "#", "$", "%", "^", "&", "*", "(", ")",
        "-", "_", "+", "=", "[", "]", "{", "}", ";", ":", "'", "\"", ",", ".",
        "<", ">", "/", "?", "|", "\\", "`", "~" };
    const size_t num_chars = capacity - 1;

    string_t random_string { buf, 0 };
    for (size_t i = 0; i < num_chars; ++i)
    {
        const size_t string_index = random32_range(0, ARRAY_COUNT(strings));
        random_string = string_concat(buf, capacity,  STRING_ARGS(random_string), strings[string_index], string_length(strings[string_index]));
    }
    buf[num_chars] = '\0';
    return { buf, num_chars };
}

void string_deallocate(string_t& str)
{
    if (!str.str)
        return;
    string_deallocate(str.str);
    str.str = nullptr;
    str.length = 0;
}