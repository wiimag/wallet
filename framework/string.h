/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2023 Wiimag inc. All rights reserved.
 */

#pragma once

#include <framework/common.h>
#include <framework/function.h>
 
#include <foundation/assert.h>
#include <foundation/string.h>

#include <framework/string_template.inl.h>
 
struct tm;

// ## MACROS

#define CTEXT(str) string_const_t{ (const char*)(str), (sizeof((str)) - 1) }

#define TEXT_UTF8(str) (const char*)(u8 ## str)
#define CTEXT_UTF8(str) string_const_t{ (const char*)(u8 ## str), (sizeof((str)) - 1) }

// ## STRUCTURES

struct lines_t
{
    size_t count{};
    string_const_t* items{};

    const string_const_t& operator[](size_t index)
    {
        FOUNDATION_ASSERT(index < count);
        return items[index];
    }
};

// ### Shared buffer ###

/*! A ring buffer shared between threads.
 *
 * @tparam BUF_SIZE The size of the buffer (ideally must be a power of 2).
 */
template<size_t BUF_SIZE>
struct SharedBuffer
{
    static const size_t RING_COUNT = 6;
    static thread_local size_t index;
    static thread_local char buffer[RING_COUNT][BUF_SIZE];
};

template<size_t BUF_SIZE> thread_local size_t SharedBuffer<BUF_SIZE>::index = 0;
template<size_t BUF_SIZE> thread_local char SharedBuffer<BUF_SIZE>::buffer[RING_COUNT][BUF_SIZE] = {};

/*! @def SHARED_BUFFER
 * @brief A macro to declare a thread local shared buffer.
 *
 * @param capacity The capacity of the buffer. The capacity will be capped to the next power of 2.
 */
#define SHARED_BUFFER(capacity) \
    SharedBuffer<CPOW2(capacity)>::buffer[(SharedBuffer<CPOW2(capacity)>::index++) % SharedBuffer<CPOW2(capacity)>::RING_COUNT], ((size_t)CPOW2(capacity))

/*! Count the occurrences of a character in a string.
 *
 * @param str The string to search.
 * @param len The length of the string.
 * @param c The character to search for.
 * @return The number of occurrences of the character in the string.
 */
size_t string_occurence(const char* str, size_t len, char c);

/*! Count the number of lines in a string.
 *
 * @param str The string to search.
 * @param len The length of the string.
 * @return The number of lines in the string.
 */
size_t string_line_count(const char* str, size_t len);

/*! Split a string in multiple lines and return the lines.
 *
 * @param str The string to split.
 * @param len The length of the string.
 * @return The lines.
 */
lines_t string_split_lines(const char* str, size_t len);

/*! Release the memory allocated by #string_split_lines.
 *
 * @param lines The lines to release.
 */
void string_lines_finalize(lines_t& lines);

/*! Split a string in multiple lines and return the lines.
 *
 * @param str The string to split.
 * @return The lines.
 */
string_t* string_split(string_const_t str, string_const_t sep);

/*! Split a string in multiple lines and return the lines.
 *
 * @param str The string to split.
 * @param str_length The length of the string to split.
 * @param sep The separator.
 * @param sep_length The length of the separator.
 * @return The lines.
 */
FOUNDATION_FORCEINLINE string_t* string_split(const char* str, size_t str_length, const char* sep, size_t sep_length)
{
    return string_split(string_const(str, str_length), string_const(sep, sep_length));
}

/*! Checks if a string is contained in another string by ignoring the case.
 *
 * @param lhs The string to search.
 * @param lhs_length The length of the string to search.
 * @param rhs The string to search for.
 * @param rhs_length The length of the string to search for.
 * @return True if the string is contained in the other string, false otherwise.
 */
bool string_contains_nocase(const char* lhs, size_t lhs_length, const char* rhs, size_t rhs_length);

/*! Checks if two string are equal by ignoring whitespaces.
 *
 * @param lhs The first string.
 * @param lhs_length The length of the first string.
 * @param rhs The second string.
 * @param rhs_length The length of the second string.
 * @return True if the two strings are equal, false otherwise.
 */
bool string_equal_ignore_whitespace(const char* lhs, size_t lhs_length, const char* rhs, size_t rhs_length);

/*! Converts UTF-8 character to their respective code. This function is not
 * 100% compliant with the UTF-8 standard, but it is good enough for our
 * purposes.
 *
 * @param str The string to convert.
 * @param len The length of the string.
 * @param out_code The code of the character.
 * @return The number of bytes read from the string.
 */
string_t string_utf8_unescape(const char* s, size_t length);

/*! Converts UTF-8 character to their respective code. This function is not
 * 100% compliant with the UTF-8 standard, but it is good enough for our
 * purposes.
 *
 * @param buffer The buffer to write the string to.
 * @param capacity The capacity of the buffer.
 * @param str The string to convert.
 * @param len The length of the string.
 * @return The number of bytes read from the string.
 */
string_t string_utf8_unescape(char* buffer, size_t capacity, const char* s, size_t length);

/*! Converts a time structure a string. The string is allocated 
 *  in a temporary static buffer and most be used as soon as possible. 
 * 
 * @param tm The time structure to convert.
 * 
 * @return The string representation of the time structure.
 */
string_const_t string_from_date(const tm& tm);

/*! Converts a date to a string. The string is allocated
 *  in a temporary static buffer and most be used as soon as possible.
 *
 * @param at The date to convert.
 *
 * @return The string representation of the date.
 */
string_const_t string_from_date(time_t at);

/*! Converts a date to a string buffer.
 *
 * @param at The date to convert.
 *
 * @return The string representation of the date.
 */
string_t string_from_date(char* buffer, size_t capacity, time_t at);

/*! Returns a string buffer allocated from a volatile memory pool.
 * 
 * @remark The buffer is allocated from a volatile memory pool and must be used as soon as possible. 
 *
 * @param required_length The required length of the string.
 * @param clear_memory True to clear the memory, false otherwise.
 * @return The string buffer.
 */
string_t string_static_buffer(size_t required_length = 64, bool clear_memory = false);

/*! Converts a string to a double formatted as a dollar/currency value.
 *
 * @param str The string to convert.
 * @param str_length The length of the string.
 * @return The double value.
 */
string_const_t string_from_currency(double value, const char* fmt = nullptr, size_t fmt_length = 0);

/*! Converts a string to a double formatted as a dollar/currency value.
 *
 * @param str The string to convert.
 * @param str_length The length of the string.
 *
 * @return The double value.
 */
string_t string_from_currency(char* buffer, size_t capacity, double value, const char* fmt = nullptr, size_t fmt_length = 0);

/*! Format a string using a variable argument list using a temporary memory pool. 
 *
 * @param fmt The format string.
 * @param fmt_length The length of the format string.
 * @param args The variable argument list.
 * @return The formatted string pointer.
 */
const char* string_format_static_const(const char fmt[], ...);

/*! Format a string using a variable argument list using a temporary memory pool.
 *
 * @param fmt The format string.
 * @param fmt_length The length of the format string.
 * @param args The variable argument list.
 * @return The formatted constant string.
 */
string_const_t string_format_static(const char* fmt, size_t fmt_length, ...);

/*! Parse a string into a time date value.
 *
 * @param date_str The string to parse.
 * @param date_str_length The length of the string to parse.
 * @param out_tm The time structure to fill.
 * @return The time value.
 */
time_t string_to_date(const char* date_str, size_t date_str_length, tm* out_tm = nullptr);

/*! Create a string_const_t from a string buffer by computing its length.
 *
 * @param str The string buffer.
 * @return The string_const_t.
 */
string_const_t string_to_const(const char* str);

/*! Trims a string from the left and the right.
 *
 * @param str The string to trim.
 * @param c The character to trim.
 * @return The trimmed string.
 */
string_const_t string_trim(string_const_t str, char c = ' ');

/*! Trims a string from the left and the right.
 *
 * @param str The string to trim.
 * @param length The length of the string to trim.
 * @param c The character to trim.
 * @return The trimmed string.
 */
string_const_t string_trim(const char* str, size_t length, char c = ' ');

/*! Checks if #str1 is less than #str2.
 *
 * @remark Usually used as a functor.
 * 
 * @param str1 The first string.
 * @param str2 The second string.
 * @return True if #str1 is less than #str2, false otherwise.
 */
bool string_compare_less(const char* str1, const char* str2);

/*! Checks if #str1 is less than #str2.
 *
 * @remark Usually used as a functor.
 *
 * @param str1 The first string.
 * @param str1_length The length of the first string.
 * @param str2 The second string.
 * @param str2_length The length of the second string.
 * @return True if #str1 is less than #str2, false otherwise.
 */
bool string_compare_less(const char* str1, size_t str1_length, const char* str2, size_t str2_length);

/*! Remove all line returns in a string buffer.
 *
 * @param buffer The buffer to write the string to.
 * @param capacity The capacity of the buffer.
 * @param str The string to remove the line returns from.
 * @param length The length of the string.
 * @return The string without line returns.
 */
string_const_t string_remove_line_returns(char* buffer, size_t capacity, const char* str, size_t length);

/*! Remove all line returns in a string. The string is allocated and must be freed.
 *
 * If the returned string is null, i.e. {}, then it means the input string 
 * must be used instead and no allocate was done.
 *
 * @param str The string to remove the line returns from.
 * @param length The length of the string.
 * @return The string without line returns.
 */
string_t string_remove_line_returns(const char* str, size_t length);

/*! Lowercase a string buffer. It only works with ASCII characters.
 *
 * @param str The string to lowercase.
 * @param length The length of the string.
 * @return The lowercase string.
 */
string_t string_to_lower_ascii(char* buf, size_t capacity, const char* str, size_t length);

/*! Uppercase a string buffer. It only works with ASCII characters.
 *
 * @param str The string to uppercase.
 * @param length The length of the string.
 * @return The uppercase string.
 */
string_t string_to_upper_ascii(char* buf, size_t capacity, const char* str, size_t length);

/*! Lowercase a string buffer. It works with UTF-8 characters.
 *
 * @param str The string to lowercase.
 * @param length The length of the string.
 * @return The lowercase string.
 */
string_t string_to_lower_utf8(char* buf, size_t capacity, const char* str, size_t length);

/*! Uppercase a string buffer. It works with UTF-8 characters.
 *
 * @param str The string to uppercase.
 * @param length The length of the string.
 * @return The uppercase string.
 */
string_t string_to_upper_utf8(char* buf, size_t capacity, const char* str, size_t length);

/*! Remove all character occurrence from a string buffer.
 *
 * @param buf The buffer to write the string to.
 * @param size The size of the string.
 * @param capacity The capacity of the buffer.
 * @param char_to_remove The character to remove.
 * @return The string without the character.
 */
string_t string_remove_character(char* buf, size_t size, size_t capacity, char char_to_remove);

/*! Try to convert a string to a real. It will return false if the string is not a valid number.
 *
 * @param str The string to convert.
 * @param length The length of the string.
 * @param out_value The value to write to.
 * @return True if the string is a valid number, false otherwise.
 */
bool string_try_convert_number(const char* str, size_t length, double& out_value);

/*! Try to convert a string to an integer. It will return false if the string is not a valid number.
 *
 * @param str The string to convert.
 * @param length The length of the string.
 * @param out_value The value to write to.
 * @param radix The radix of the number.
 * @return True if the string is a valid number, false otherwise.
 */
bool string_try_convert_number(const char* str, size_t length, int& out_value, const int radix = 10);

/*! Try to convert a string to a date. It will return false if the string is not a valid date.
 * 
 * It only support format such as YYYY-MM-DD for now.
 *
 * @param str    The string to convert.
 * @param length The length of the string.
 * @param date   The date to write to.
 * 
 * @return True if the string is a valid date, false otherwise.
 */
bool string_try_convert_date(const char* str, size_t length, time_t& date);

/*! Helper function to deallocate a string and set its member to null.
 *
 * @param str The string to deallocate.
 */
void string_deallocate(string_t& str);

/*! Checks if the character is a common whitespace. 
 *  It is either a space, a tab, a new line or a carriage return.
 *
 * @param c The character to check.
 * @return True if the character is a whitespace, false otherwise.
 */
FOUNDATION_FORCEINLINE bool is_whitespace(char c)
{
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        return true;
    return false;
}

/*! Converts a fixed string to a #string_const_t.
 *  The string must be null terminated.
 *
 * @param s The string to convert.
 * @return The string_const_t.
 */
template <size_t N> FOUNDATION_FORCEINLINE constexpr string_const_t string_const(const char(&s)[N]) 
{ 
    return string_const(s, min(N-1, string_length(s)));
}

/*! Checks if the string is null.
 *
 * @param s The string to check.
 * @return True if the string is null, false otherwise.
 */
FOUNDATION_FORCEINLINE FOUNDATION_CONSTEXPR bool string_is_null(string_const_t s)
{
    return s.str == nullptr || s.length == 0;
}

/*! Compares two strings for equality.
 *
 * @param lhs The first string.
 * @param rhs The second string.
 * @return True if the strings are equal, false otherwise.
 */
FOUNDATION_FORCEINLINE bool string_equal(string_const_t lhs, string_const_t rhs)
{
    return string_equal(STRING_ARGS(lhs), STRING_ARGS(rhs));
}

/*! Compares two strings for equality.
 *
 * @param lhs The first string.
 * @param rhs The second string.
 * @return True if the strings are equal, false otherwise.
 */
FOUNDATION_FORCEINLINE bool string_equal(string_t lhs, string_const_t rhs)
{
    return string_equal(STRING_ARGS(lhs), STRING_ARGS(rhs));
}

/*! Compares two strings for equality.
 *
 * @param lhs The first string.
 * @param rhs The second string.
 * @return True if the strings are equal, false otherwise.
 */
FOUNDATION_FORCEINLINE bool string_equal(string_const_t lhs, string_t rhs)
{
    return string_equal(STRING_ARGS(lhs), STRING_ARGS(rhs));
}

/*! Checks if a non-const string is null.
 *
 * @param s The string to check.
 * @return True if the string is null, false otherwise.
 */
FOUNDATION_FORCEINLINE FOUNDATION_CONSTEXPR bool string_is_null(string_t s)
{
    return s.str == nullptr || s.length == 0;
}

/*! Checks if a string starts with a prefix.
 *
 * @param str The string to check.
 * @param prefix The prefix to check.
 * @return True if the string starts with the prefix, false otherwise.
 */
FOUNDATION_FORCEINLINE bool string_starts_with(const char* str, size_t str_length, const char* prefix, size_t prefix_length)
{
    if (str_length < prefix_length)
        return false;
    return string_equal(str, prefix_length, prefix, prefix_length);
}

/*! Checks if a character is an hex digit.
 *
 * @param c The character to check.
 * @return True if the character is a hex digit, false otherwise.
 */
FOUNDATION_FORCEINLINE bool is_char_alpha_num_hex(char c)
{
    if (c >= '0' && c <= '9')
        return true;
    if (c >= 'a' && c <= 'f')
        return true;
    if (c >= 'A' && c <= 'F')
        return true;
    return false;
}

/*! Converts a character to its hex value.
 *
 * @param c The character to convert.
 * @return The hex value of the character.
 */
FOUNDATION_FORCEINLINE uint8_t hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return 9 - ('9' - c);
    if (c >= 'a' && c <= 'f')
        return 15 - ('f' - c);
    if (c >= 'A' && c <= 'F')
        return 15 - ('F' - c);
    return -1;
}

/*! Join a list of strings into a single string.
 *
 * @param begin The begin iterator.
 * @param end The end iterator.
 * @param iter The iterator function to get the string from the element.
 * @param sep The separator to use between elements.
 * @param open_token The token to open the list with.
 * @param close_token The token to close the list with.
 * @return The joined string.
 */
template<typename Iter, typename FnIter>
string_const_t string_join(Iter begin, Iter end, const FnIter& iter,
    string_const_t sep = { ", ", 2 },
    string_const_t open_token = { nullptr, 0 },
    string_const_t close_token = { nullptr, 0 })
{
    string_t b_list{ nullptr, 0 };
    string_t join_list_string = { nullptr, 0 };

    if (!string_is_null(open_token))
        join_list_string = string_allocate_concat(nullptr, 0, STRING_ARGS(open_token));

    int added = 0;
    for (Iter it = begin; it != end; ++it)
    {
        string_const_t e_str = iter(*it);
        if (string_is_null(e_str))
            continue;

        b_list = join_list_string;
        if (added > 0)
        {
            join_list_string = string_allocate_concat(STRING_ARGS(join_list_string), STRING_ARGS(sep));
            string_deallocate(b_list.str);
        }

        b_list = join_list_string;
        join_list_string = string_allocate_concat(STRING_ARGS(join_list_string), STRING_ARGS(e_str));
        string_deallocate(b_list.str);
        added++;
    }

    if (!string_is_null(close_token))
    {
        b_list = join_list_string;
        join_list_string = string_allocate_concat(STRING_ARGS(join_list_string), STRING_ARGS(close_token));
        string_deallocate(b_list.str);
    }

    string_t static_buf = string_static_buffer(min(16384ULL, join_list_string.length + 1ULL));
    string_const_t res = string_to_const(string_copy(static_buf.str, static_buf.length, STRING_ARGS(join_list_string)));
    string_deallocate(join_list_string.str);
    return res;
}

/*! Join a list of strings into a single string.
 *
 * @param list The list of strings.
 * @param count The number of elements in the list.
 * @param iter The iterator function to get the string from the element.
 * @param sep The separator to use between elements.
 * @param open_token The token to open the list with.
 * @param close_token The token to close the list with.
 * @return The joined string.
 */
template<typename T, typename Iter>
string_const_t string_join(const T* list, size_t count, const Iter& iter,
    string_const_t sep = { ", ", 2 }, 
    string_const_t open_token = { nullptr, 0 }, 
    string_const_t close_token = { nullptr, 0 })
{
    string_t b_list{ nullptr, 0 };
    string_t join_list_string = {nullptr, 0};
    
    if (!string_is_null(open_token))
        join_list_string = string_allocate_concat(nullptr, 0, STRING_ARGS(open_token));

    for (size_t i = 0; i < count; ++i)
    {
        b_list = join_list_string;

        if (i > 0)
        {
            join_list_string = string_allocate_concat(STRING_ARGS(join_list_string), STRING_ARGS(sep));
            string_deallocate(b_list.str);
        }

        string_const_t e_str = iter(list[i]);
        b_list = join_list_string;
        join_list_string = string_allocate_concat(STRING_ARGS(join_list_string), STRING_ARGS(e_str));
        string_deallocate(b_list.str);
    }

    if (!string_is_null(close_token))
    {
        b_list = join_list_string;
        join_list_string = string_allocate_concat(STRING_ARGS(join_list_string), STRING_ARGS(close_token));
        string_deallocate(b_list.str);
    }

    string_t static_buf = string_static_buffer(min(16384ULL, join_list_string.length + 1ULL));
    string_const_t res = string_to_const(string_copy(static_buf.str, static_buf.length, STRING_ARGS(join_list_string)));
    string_deallocate(join_list_string.str);
    return res;
}

/*! Join a list of strings into a single string.
 *
 * @param list The list of strings.
 * @param iter The iterator function to get the string from the element.
 * @param sep The separator to use between elements.
 * @param open_token The token to open the list with.
 * @param close_token The token to close the list with.
 * @return The joined string.
 */
template<typename T, typename Iter>
string_const_t string_join(const T* list, const Iter& iter,
    string_const_t sep = { ", ", 2 },
    string_const_t open_token = { nullptr, 0 },
    string_const_t close_token = { nullptr, 0 })
{
    return string_join(list, array_size(list), iter, sep, open_token, close_token);
}

/*! Returns a random string of the given length.
 *
 * @param buf The buffer to write the string to.
 * @param capacity The capacity of the buffer.
 * @return The random string.
 */
string_t string_random(char* buf, size_t capacity);

/*! Compute levenstein distance between two strings.
 *
 * @param str1 The first string.
 * @param length1 The length of the first string.
 * @param str2 The second string.
 * @param length2 The length of the second string.
 * @return The levenstein distance.
 */
int string_levenstein_distance(const char* str1, size_t length1, const char* str2, size_t length2);

/*! Compute levenstein distance between two strings.
 *
 * @param str1 The first string.
 * @param str2 The second string.
 * @return The levenstein distance.
 */
int string_levenstein_distance(string_const_t str1, string_const_t str2);

/*! Remove string trailing whitespaces.
 *
 * @param str The string.
 * @param length The length of the string.
 * 
 * @return The string without trailing whitespaces.
 */
string_const_t string_remove_trailing_whitespaces(const char* str, size_t length);

/*! Escape all characters in the string that are not allowed in a url.
 *
 * @param buffer The buffer to write the escaped string to.
 * @param capacity The capacity of the buffer.
 * @param url The url to escape.
 * @param url_length The length of the url.
 * @return The escaped url.
 */
string_t string_encode_uri_component(char* buffer, size_t capacity, const char* url, size_t url_length);
    
/*! Parse version string with the format 0.0.0[.0] into a version_t struct.
 *
 * @param str The version string.
 * @param length The length of the version string.
 * @return The parsed version.
 */
version_t string_to_version_short(const char* str, size_t length);

/*! Convert Unicode code point to UTF-8 string.
 *
 * @param buffer The buffer to write the UTF-8 string to.
 * @param capacity The capacity of the buffer.
 * @param code_point The code point to convert.
 * @return The UTF-8 string.
 */
string_t string_utf8_from_code_point(char* buffer, size_t capacity, uint32_t code_point);

/*! Convert Unicode code point to UTF-8 string.
 *  The #code_point string is expected to be in the format U+XXXX. U+ is optional.
 *
 * @param buffer The buffer to write the UTF-8 string to.
 * @param capacity The capacity of the buffer.
 * @param code_point The code point to convert.
 * @param length The length of the code point string.
 * @return The UTF-8 string.
 */
string_t string_utf8_from_code_point(char* buffer, size_t capacity, const char* code_point, size_t length);

/*! Compare two strings, but skip any leading code points that are not letters or digits.
 *
 * @param lhs The left hand side string.
 * @param rhs The right hand side string.
 * @return The comparison result.
 */
int string_compare_skip_code_points(STRING_PARAM(lhs), STRING_PARAM(rhs));

/*! Remove the beginning of a string if matching the given begin string.
 *
 * @param str The string.
 * @param length The length of the string.
 * @param begin The characters to strip.
 * @param begin_length The length of the strip_chars string.
 * @return The stripped string.
 */
string_const_t string_strip_begin(const char* str, size_t length, const char* begin, size_t begin_length);

/*! Remove the ends of a string if matching the given end string.
 *
 * @param str The string.
 * @param length The length of the string.
 * @param end The characters to strip.
 * @param end_length The length of the strip_chars string.
 * @return The stripped string.
 */
string_const_t string_strip_end(const char* str, size_t length, const char* end, size_t end_length);

/*! Checks if the character is uppercase.
 *
 * @param c The character.
 * @return True if the character is uppercase, false if not.
 */
FOUNDATION_FORCEINLINE bool string_char_is_uppercase(char c)
{
    return (c >= 'A' && c <= 'Z');
}

/*! Checks if the character is lowercase.
 *
 * @param c The character.
 * @return True if the character is lowercase, false if not.
 */
FOUNDATION_FORCEINLINE bool string_char_is_lowercase(char c)
{
    return (c >= 'a' && c <= 'z');
}

/*! Checks if the character is a letter.
 *
 * @param c The character.
 * @return True if the character is a letter, false if not.
 */
FOUNDATION_FORCEINLINE bool string_char_is_letter(char c)
{
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

/*! Checks if the character is a digit.
 *
 * @param c The character.
 * @return True if the character is a digit, false if not.
 */
FOUNDATION_FORCEINLINE bool string_char_is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

/*! Checks if the character is a letter or digit.
 *
 * @param c The character.
 * @return True if the character is a letter or digit, false if not.
 */
FOUNDATION_FORCEINLINE bool string_char_is_alphanumeric(char c)
{
    return ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'));
}

/*! Checks if the character is a whitespace.
 *
 * @param c The character.
 * @return True if the character is a whitespace, false if not.
 */
FOUNDATION_FORCEINLINE bool string_char_is_whitespace(char c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}

/*! Transform a CamelCase string into a string with spaces between words.
 *
 * @param str The string.
 * @param length The length of the string.
 * @param capacity The capacity of the buffer.
 * @param split The string to insert between words.
 * @param split_length The length of the split string.
 * @return The transformed string.
 */
string_t string_camel_case_add_space(char* str, size_t length, size_t capacity, const char* split = " ", size_t split_length = 1);
