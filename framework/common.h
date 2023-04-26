/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/memory.h>
#include <framework/function.h>

#include <foundation/fs.h>
#include <foundation/time.h>
#include <foundation/array.h>
 
#include <time.h>
#include <limits>

////////////////////////////////////////////////////////////////////////////
// ## Constants

constexpr double DNAN = __builtin_nan("0");

////////////////////////////////////////////////////////////////////////////
// ## MACROS

/*! @def ARRAY_COUNT 
 *
 *  @brief Returns the number of elements in an fixed array.
 */
#define ARRAY_COUNT(ARR) (sizeof(ARR) / sizeof(ARR[0]))

/*! @def SIZE_C
 * 
 *  @brief Make sure to cast the value to size_t.
 */
#define SIZE_C(val) (size_t)(UINT64_C(val))

/*! @def DEFINE_ENUM_FLAGS 
 *
 *  @brief Defines bitwise operators for an enum class.
 */
#define DEFINE_ENUM_FLAGS(T) \
    FOUNDATION_FORCEINLINE T operator~ (T a) { return static_cast<T>(~(std::underlying_type_t<T>)a); } \
    FOUNDATION_FORCEINLINE bool operator!= (const T a, const std::underlying_type_t<T> b) { return (std::underlying_type_t<T>)a != b; } \
    FOUNDATION_FORCEINLINE bool operator== (const T a, const std::underlying_type_t<T> b) { return (std::underlying_type_t<T>)a == b; } \
    FOUNDATION_FORCEINLINE bool operator&& (const T a, const T b) { return (std::underlying_type_t<T>)a != 0 && (std::underlying_type_t<T>)b != 0; } \
    FOUNDATION_FORCEINLINE bool operator&& (const T a, const bool b) { return (std::underlying_type_t<T>)a != 0 && b; } \
    FOUNDATION_FORCEINLINE constexpr T operator| (const T a, const T b) { return (T)((std::underlying_type_t<T>)a | (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE T operator& (const T a, const T b) { return (T)((std::underlying_type_t<T>)a & (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE T operator^ (const T a, const T b) { return (T)((std::underlying_type_t<T>)a ^ (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE T& operator|= (T& a, const T b) { return (T&)((std::underlying_type_t<T>&)a |= (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE T& operator&= (T& a, const T b) { return (T&)((std::underlying_type_t<T>&)a &= (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE T& operator^= (T& a, const T b) { return (T&)((std::underlying_type_t<T>&)a ^= (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE bool test(const T a, const T b) { return (a & b) == b; } \
    FOUNDATION_FORCEINLINE bool any(const T a, const T b) { return (a & b) != 0; } \
    FOUNDATION_FORCEINLINE bool none(const T a, const T b) { return (a & b) == 0; } \
    FOUNDATION_FORCEINLINE bool one(const T a, const T b) { const auto bits = ((std::underlying_type_t<T>)a & (std::underlying_type_t<T>)b); return bits && !(bits & (bits-1)); }

////////////////////////////////////////////////////////////////////////////
// ## Generics

/*! Returns the minimal value of two values.
 *
 *  @param a First value.
 *  @param b Second value.
 *
 *  @return The minimal value.
 */
template<typename T> FOUNDATION_FORCEINLINE T min(T a, T b) { return (((a) < (b)) ? (a) : (b)); }

/*! Returns the maximal value of two values.
 *
 *  @param a First value.
 *  @param b Second value.
 *
 *  @return The maximal value.
 */
template<typename T> FOUNDATION_FORCEINLINE T max(T a, T b) { return (((a) > (b)) ? (a) : (b)); }

////////////////////////////////////////////////////////////////////////////
// ## URLs

/*! Encode a string to be used in a URL.
 * 
 *  @note The functions used a static buffer, so the returned string is only valid until the next call to the function.
 * 
 *  @param str String to encode
 *  @param str_length Length of string
 * 
 *  @return Encoded string
 */
string_const_t url_encode(const char* str, size_t str_length = 0);

/*! Decode a string from a URL.
 * 
 *  @note The functions used a static buffer, so the returned string is only valid until the next call to the function.
 * 
 *  @param str String to decode
 *  @param str_length Length of string
 * 
 *  @return Decoded string
 */
string_const_t url_decode(const char* str, size_t str_length = 0);

////////////////////////////////////////////////////////////////////////////
// ## Path manipulation functions

/*! Compares two paths.
 * 
 *  @param a First path
 *  @param a_length Length of first path
 *  @param b Second path
 *  @param b_length Length of second path
 * 
 *  @return True if paths are equivalent, false otherwise.
 */
bool path_equals(const char* a, size_t a_length, const char* b, size_t b_length);

/*! Normalize path name, removing any redundant path components and by removing any illegal chars.
 * 
 *  @param buff Buffer to write normalized path to
 *  @param capacity Capacity of buffer
 *  @param path Path to normalize
 *  @param path_length Length of path
 *  @param replacement_char Character to replace illegal chars with
 * 
 *  @return Normalized path
 */
string_t path_normalize_name(char* buff, size_t capacity, const char* path, size_t path_length, const char replacement_char = '_');

////////////////////////////////////////////////////////////////////////////
// ## File system functions

/*! Returns all the text in a file.
 * 
 *  @param path Path to file
 *  @param path_length Length of path
 * 
 *  @return Text in file
 */
string_t fs_read_text(const char* path, size_t path_length);

/*! Get last modification time (last write) in milliseconds since the epoch (UNIX time)
 * 
 *  @param path   File path
 *
 *  @return       File modification time, 0 if not an existing file 
 */
template<typename T> FOUNDATION_FORCEINLINE tick_t fs_last_modified(const T& path)
{
    return fs_last_modified(STRING_ARGS(path));
}

/*! Remove a file from disk.
 *
 *  @param path   File path
 *
 *  @return       True if file was removed, false if not
 */
template<typename T> FOUNDATION_FORCEINLINE bool fs_remove_file(const T& path)
{
    return fs_remove_file(STRING_ARGS(path));
}

/*! Clean the file name, removing any illegal chars.
 * 
 *  @remark The returned string is a static buffer, so it will be overwritten on the next call.
 * 
 *  @param filename File name to clean
 *  @param filename_length Length of file name
 * 
 *  @return Cleaned file name
 */
string_const_t fs_clean_file_name(const char* filename, size_t filename_length);

/*! Returns an hash of the file contents.
 * 
 *  @param file_path Path to file
 * 
 *  @return Hash of file contents
 */
hash_t fs_hash_file(string_t file_path);

/*! Returns an hash of the file contents.
 * 
 *  @param file_path Path to file
 * 
 *  @return Hash of file contents
 */
hash_t fs_hash_file(string_const_t file_path);

/*! Checks if the file path is a file.
 * 
 *  @param file_path Path to file
 * 
 *  @return True if file exists, false otherwise
 */
FOUNDATION_FORCEINLINE bool fs_is_file(string_const_t file_path)
{
    return fs_is_file(STRING_ARGS(file_path));
}

/*! Checks if the file path is a file.
 * 
 *  @param file_path Path to file
 * 
 *  @return True if file exists, false otherwise
 */
FOUNDATION_FORCEINLINE bool fs_is_file(string_t file_path)
{
    return fs_is_file(STRING_ARGS(file_path));
}

////////////////////////////////////////////////////////////////////////////
// ## Time functions

/*! Returns the current time date. 
 *
 *  @remarks Compared to #time_current the returned value is equivalent to a Unix timestamp and counts seconds since 1970-01-01 00:00:00 UTC.
 *           As if #time_current returns milliseconds since 1970-01-01 00:00:00 UTC, the returned value is the same as #time_current divided by 1000.
 */
time_t time_now();

/*! Adds a number of days to a date. 
 *
 *  @param t    Date to add days to
 *  @param days Number of days to add
 *  
 *  @returns Date with days added
 */
time_t time_add_days(time_t t, int days);

/*! Adds a number of hours to a date. 
 *
 *  @param t     Date to add hours to
 *  @param hours Number of hours to add
 *  
 *  @returns Date with hours added
 */
time_t time_add_hours(time_t t, double hours);

/*! Returns the number of days between two dates. 
 * 
 *  @remarks The returned value can be fractional, so if the dates are on the same day the returned value will be 0.xyz.
 *
 *  @param from Start date
 *  @param to   End date
 *  
 *  @returns Number of days between dates
 */
double time_elapsed_days(time_t from, time_t to);

/*! Returns the number of days between two dates, rounded to the nearest integer. 
 *
 *  @param from Start date
 *  @param to   End date
 *  
 *  @returns Number of days between dates
 */
double time_elapsed_days_round(time_t from, time_t to);

/*! Returns the time work date nearest to a given date. 
 *
 *  @param date Date to find nearest work date to
 *  @param rel  Relative work day, 0 is today, -1 is yesterday, 1 is tomorrow, etc.
 *  
 *  @returns Nearest work date
 */
time_t time_work_day(time_t date, double rel);

/*! Checks if two date are equal (i.e. same day). 
 * 
 *  @remarks The hour, minutes, seconds, etc. parts of the date are ignored.
 *
 *  @param da First date
 *  @param db Second date
 *  
 *  @returns True if the dates are equal
 */
bool time_date_equal(time_t da, time_t db);

/*! Converts a time date to a local time date structure. 
 *
 *  @param t     Date to convert
 *  @param out_tm Pointer to tm struct to write converted date to
 *  
 *  @returns True if conversion was successful, false otherwise
 */
bool time_to_local(time_t t, tm* out_tm);

/*! Converts time date parts to a time date. 
 *
 *  @param year        Year
 *  @param month       Month
 *  @param day         Day
 *  @param hour        Hour
 *  @param minute      Minute
 *  @param second      Second
 *  @param millisecond Millisecond (not used on most systems, use #tick_t instead if you need millisecond precision)
 *  
 *  @returns Time date
 */
time_t time_make(int year, int month, int day, int hour = 0, int minute = 0, int second = 0, int millisecond = 0);

/*! Checks if two dates are on the same day. 
 *
 *  @param d1 First date
 *  @param d2 Second date
 *  
 *  @returns True if the dates are on the same day
 */
bool time_same_day(time_t d1, time_t d2);

/*! @brief Checks if the current time in a weekend day. */
bool time_is_weekend();

/*! @brief Checks if the current time is a working hour. */
bool time_is_working_hours();

/*! Returns the constant time that represents an hour. */
FOUNDATION_FORCEINLINE constexpr time_t const time_one_hour()
{
    constexpr const time_t one_day = 60 * 60;
    return one_day;
}

/*! Returns the constant time that represents a day. */
FOUNDATION_FORCEINLINE constexpr time_t const time_one_day()
{
    constexpr const time_t one_day = 24 * 60 * 60;
    return one_day;
}

/*! Convert a time date to a tick count. 
 *
 *  @param time Time date to convert
 *  
 *  @returns Tick count
 */
FOUNDATION_FORCEINLINE tick_t time_to_tick(time_t time)
{
    return (tick_t)time * 1000;
}

////////////////////////////////////////////////////////////////////////////
// ## Misc generic functions

/*! Returns the number of digits in a number. 
 *
 *  @param number Number to count digits in
 *  
 *  @returns Number of digits in number
 */
template <class T>
int num_digits(T number)
{
    int digits = 0;
    if (number < 0) digits = 1; // remove this line if '-' counts as a digit
    while (number) {
        number /= 10;
        digits++;
    }
    return digits;
}

////////////////////////////////////////////////////////////////////////////
// ## Environment functions

/*! Get the system application resources path.
 * 
 *  @remark This is the folder where the application can store data that is not user specific.
 * 
 *  @return Path to the application resources folder.
 */
string_const_t environment_get_resources_path();

/*! Get the system application build path.
 * 
 *  @remark This folder is the executable folder on Windows and the bundle folder on macOS.
 * 
 *  @return Path to the application bundle folder.
 */
string_const_t environment_get_build_path();

/*! Checks and returns the environment argument with the given name.
 *
 *  We first check the command line arguments, then the environment variables.
 *  For the command line arguments, we check the for --argument and -argument forms 
 *  and for the value with check: `--argument=value`, or `--argument value` .
 *  
 *  Then we check the environment variables, and for the value we check: `ARGUMENT=value`.
 *  For the environment variables, we check for the uppercase form of the argument name and replace `-` with an `_`.
 *  In example `environment_variable("openai-api-key")` will check for the environment variable `OPENAI_API_KEY`.
 *  
 *  We ignore argument names that are smaller than 4 characters.
 * 
 *  @param name Name of the argument to check.
 *  @param value If not null, will be set to the value of the argument.
 *  @param check_environment_variable If false, will skip checking the environment variables.
 * 
 *  @return True if the argument was found.
 */
bool environment_argument(string_const_t name, string_const_t* value, bool check_environment_variable);

/*! Checks and returns the environment argument with the given name.
 *
 *  @see environment_argument(string_const_t name, string_const_t* value, bool check_environment_variable)
 * 
 *  @param name        Name of the argument to check.
 *  @param name_length Length of the name of the argument to check.
 *  @param value       If not null, will be set to the value of the argument.
 * 
 *  @return True if the argument was found.
 */
FOUNDATION_FORCEINLINE bool environment_argument(const char* name, size_t name_length, string_const_t* value, bool check_environment_variable)
{
    string_const_t name_str = string_const(name, name_length);
    return environment_argument(name_str, value, check_environment_variable);
}

/*! Checks and returns the environment argument with the given name.
 *
 *  @see environment_argument(string_const_t name, string_const_t* value, bool check_environment_variable)
 * 
 *  @param name  Name of the argument to check.
 *  @param value If not null, will be set to the value of the argument.
 * 
 *  @return True if the argument was found.
 */
template<size_t N>
FOUNDATION_FORCEINLINE bool environment_argument(const char (&name)[N], string_const_t* value)
{
    string_const_t name_str = string_const(name, N - 1);
    return environment_argument(name_str, value, true);
}

/*! Checks and returns the environment argument with the given name.
 *
 *  @see environment_argument(string_const_t name, string_const_t* value, bool check_environment_variable)
 * 
 *  @param name  Name of the argument to check.
 *  @param value If not null, will be set to the value of the argument.
 *  @param check_environment_variable If false, will skip checking the environment variables.
 * 
 *  @return True if the argument was found.
 */
template<size_t N>
FOUNDATION_FORCEINLINE bool environment_argument(const char(&name)[N], string_const_t* value, bool check_environment_variable)
{
    string_const_t name_str = string_const(name, N - 1);
    return environment_argument(name_str, value, check_environment_variable);
}

/*! Checks if an command line argument is present.
 * 
 *  We do not check for an existing environment variable in this case, 
 *  use #environment_varibale or other #environment_argument overload for that.
 * 
 *  @param name  Name of the argument to check.
 * 
 *  @return True if the argument was found, false otherwise.
 */
template<size_t N>
FOUNDATION_FORCEINLINE bool environment_argument(const char(&name)[N])
{
    string_const_t name_str = string_const(name, N - 1);
    return environment_argument(name_str, nullptr, false);
}

////////////////////////////////////////////////////////////////////////////
// ## Main functions

/*! Returns the true if the application is running in daemon mode, meaning that it is either running as a service or as a background process.
 * 
 *  @remark Running tests is considered daemon mode, as it is a background process that does not require user interaction.
 * 
 *  @return True if the application is running in daemon mode.
 */
extern bool main_is_daemon_mode();

/*! Returns true if the application is running in batch mode, meaning that it is running without a graphical user interface.
 *
 *  @remark Running tests is considered batch mode, as it is a background process that does not require user interaction.
 *
 *  @return True if the application is running in batch mode.
 */
extern bool main_is_batch_mode();

/*! Returns true if the application is running in graphical mode, meaning that it is running with a graphical user interface.
 *
 *  @return True if the application is running in graphical mode.
 */
extern bool main_is_graphical_mode();

/*! Returns true if the application is running in interactive mode, meaning that it is running with a graphical user interface and the user is interacting with it.
 *
 *  @remark Running tests is considered interactive mode, as it is a background process that does not require user interaction.
 *
 *  @return True if the application is running in interactive mode.
 */
extern bool main_is_interactive_mode(bool exclude_debugger = false);

/*! Returns true if the application is running in test mode, meaning that it is running unit tests.
 *
 *  @return True if the application is running in test mode.
 */
extern bool main_is_running_tests();

/*! Returns the amount of time in milliseconds that has elapsed since the last application tick. 
 *
 *  @remark This is the time that has elapsed since the last call to #main_tick().
 *  
 *  @return Elapsed time in milliseconds.
 */
extern double main_tick_elapsed_time_ms();

////////////////////////////////////////////////////////////////////////////
// ## Logging

struct LogPrefixScope
{
    const bool previous_state;
    FOUNDATION_FORCEINLINE LogPrefixScope(bool enable = false)
        : previous_state(log_is_prefix_enabled())
    {
        log_enable_prefix(enable);
    }

    FOUNDATION_FORCEINLINE ~LogPrefixScope()
    {
        log_enable_prefix(previous_state);
    }
};

#define LOG_PREFIX(enable)  LogPrefixScope __var_log_prefix_scope__##COUNTER (enable) 

////////////////////////////////////////////////////////////////////////////
// ## Type conversion

FOUNDATION_FORCEINLINE int32_t to_int(size_t v)
{
    FOUNDATION_ASSERT_MSGFORMAT(v <= INT_MAX, "%" PRIsize " > %d", v, INT_MAX);
    return (int32_t)v;
}

FOUNDATION_FORCEINLINE uint32_t to_uint(int32_t v)
{
    FOUNDATION_ASSERT_MSGFORMAT(v >= 0, "%d<0", v);
    return (uint32_t)v;
}

FOUNDATION_FORCEINLINE size_t to_size(int64_t v)
{
    FOUNDATION_ASSERT_MSGFORMAT(v >= 0, "%lld<0", v);
    return (size_t)v;
}

FOUNDATION_FORCEINLINE uint32_t to_uint(size_t v)
{
    FOUNDATION_ASSERT(v <= UINT_MAX);
    return (uint32_t)v;
}

template<typename T = void> 
FOUNDATION_FORCEINLINE constexpr T* to_ptr(uint32_t v)
{
    return (T*)(uintptr_t)(v);
}

template<typename T>
FOUNDATION_FORCEINLINE T to_opaque(void* ptr)
{
    const auto v = (intptr_t)(ptr);
    FOUNDATION_ASSERT(std::numeric_limits<T>::min() <= v && v <= std::numeric_limits<T>::max());
    return (T)v;
}

////////////////////////////////////////////////////////////////////////////
// ## Color utility functions

FOUNDATION_FORCEINLINE uint32_t rgb_to_abgr(const uint32_t v, const uint8_t alpha = 0xFF)
{
    const uint8_t r = (v & 0x00FF0000) >> 16;
    const uint8_t g = (v & 0x0000FF00) >> 8;
    const uint8_t b = (v & 0x000000FF);
    return (alpha << 24) | (b << 16) | (g << 8) | (r);
}

////////////////////////////////////////////////////////////////////////////
// ## Hashing

FOUNDATION_FORCEINLINE hash_t hash_combine(hash_t h1, hash_t h2)
{
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
}

FOUNDATION_FORCEINLINE hash_t hash_combine(hash_t h1, hash_t h2, hash_t h3)
{
    return hash_combine(hash_combine(h1, h2), h3);
}

FOUNDATION_FORCEINLINE hash_t hash_combine(hash_t h1, hash_t h2, hash_t h3, hash_t h4)
{
    return hash_combine(hash_combine(h1, h2), hash_combine(h3, h4));
}


