/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/function.h>

#include <foundation/fs.h>
#include <foundation/time.h>
#include <foundation/array.h>
 
#include <time.h>
#include <limits>

// ## MACROS

constexpr double DNAN = __builtin_nan("0");
#define ARRAY_COUNT(ARR) (sizeof(ARR) / sizeof(ARR[0]))

#define DEFINE_ENUM_FLAGS(T) \
    FOUNDATION_FORCEINLINE T operator~ (T a) { return static_cast<T>(~(std::underlying_type_t<T>)a); } \
    FOUNDATION_FORCEINLINE bool operator!= (const T a, const std::underlying_type_t<T> b) { return (std::underlying_type_t<T>)a != b; } \
    FOUNDATION_FORCEINLINE bool operator== (const T a, const std::underlying_type_t<T> b) { return (std::underlying_type_t<T>)a == b; } \
    FOUNDATION_FORCEINLINE bool operator&& (const T a, const T b) { return (std::underlying_type_t<T>)a != 0 && (std::underlying_type_t<T>)b != 0; } \
    FOUNDATION_FORCEINLINE bool operator&& (const T a, const bool b) { return (std::underlying_type_t<T>)a != 0 && b; } \
    FOUNDATION_FORCEINLINE T operator| (const T a, const T b) { return (T)((std::underlying_type_t<T>)a | (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE T operator& (const T a, const T b) { return (T)((std::underlying_type_t<T>)a & (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE T operator^ (const T a, const T b) { return (T)((std::underlying_type_t<T>)a ^ (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE T& operator|= (T& a, const T b) { return (T&)((std::underlying_type_t<T>&)a |= (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE T& operator&= (T& a, const T b) { return (T&)((std::underlying_type_t<T>&)a &= (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE T& operator^= (T& a, const T b) { return (T&)((std::underlying_type_t<T>&)a ^= (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE bool test(const T a, const T b) { return (a & b) == b; } \
    FOUNDATION_FORCEINLINE bool any(const T a, const T b) { return (a & b) != 0; } \
    FOUNDATION_FORCEINLINE bool none(const T a, const T b) { return (a & b) == 0; } \
    FOUNDATION_FORCEINLINE bool one(const T a, const T b) { const auto bits = ((std::underlying_type_t<T>)a & (std::underlying_type_t<T>)b); return bits && !(bits & (bits-1)); }

template<typename T> FOUNDATION_FORCEINLINE T min(T a, T b) { return (((a) < (b)) ? (a) : (b)); }
template<typename T> FOUNDATION_FORCEINLINE T max(T a, T b) { return (((a) > (b)) ? (a) : (b)); }

template<typename T>
struct range_view 
{
    FOUNDATION_FORCEINLINE range_view(T* data, std::size_t size)
        : m_data{ data }, m_size{ size } { }

    struct iterator
    {
        const T* ptr;

        typedef T type;
        typedef const T const_type;

        FOUNDATION_FORCEINLINE iterator(const T* ptr)
            : ptr(ptr)
        {
        }

        FOUNDATION_FORCEINLINE bool operator!=(const iterator& other) const
        {
            return ptr != other.ptr;
        }

        FOUNDATION_FORCEINLINE bool operator==(const iterator& other) const
        {
            return ptr == other.ptr;
        }

        FOUNDATION_FORCEINLINE iterator& operator++()
        {
            ptr++;
            return *this;
        }

        FOUNDATION_FORCEINLINE const T& operator*() const
        {
            return *ptr;
        }
    };

    iterator begin() { return iterator(m_data); }
    iterator end() { return iterator(m_data + m_size); }

    iterator begin() const { return iterator(m_data); }
    iterator end() const { return iterator(m_data + m_size); }

    T* m_data;
    size_t m_size;
};

// ## URLs

string_const_t url_encode(const char* str, size_t str_length = 0);
string_const_t url_decode(const char* str, size_t str_length = 0);

// ## Paths

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

// ##FS

/*! Returns all the text in a file.
 * 
 *  @param path Path to file
 *  @param path_length Length of path
 * 
 *  @return Text in file
 */
string_t fs_read_text(const char* path, size_t path_length);

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

hash_t fs_hash_file(string_t file_path);
hash_t fs_hash_file(string_const_t file_path);

FOUNDATION_FORCEINLINE bool fs_is_file(string_const_t file_path)
{
    return fs_is_file(STRING_ARGS(file_path));
}

FOUNDATION_FORCEINLINE bool fs_is_file(string_t file_path)
{
    return fs_is_file(STRING_ARGS(file_path));
}

// ## Time

time_t time_now();
time_t time_add_days(time_t t, int days);
time_t time_add_hours(time_t t, double hours);
double time_elapsed_days(time_t from, time_t to);
double time_elapsed_days_round(time_t from, time_t to);
time_t time_work_day(time_t date, double rel);
bool time_date_equal(time_t da, time_t db);
bool time_to_local(time_t t, tm* out_tm);
time_t time_make(int year, int month, int day, int hour = 0, int minute = 0, int second = 0, int millisecond = 0);

/*! @brief Checks if the current time in a weekend day. */
bool time_is_weekend();

/*! @brief Checks if the current time is a working hour. */
bool time_is_working_hours();

FOUNDATION_FORCEINLINE constexpr time_t const time_one_hour()
{
    constexpr const time_t one_day = 60 * 60;
    return one_day;
}

FOUNDATION_FORCEINLINE constexpr time_t const time_one_day()
{
    constexpr const time_t one_day = 24 * 60 * 60;
    return one_day;
}

// ## GENERICS

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

template<typename T, void (*DTOR)(T* ptr)>
struct ManagedPtr
{
    T* ptr{ nullptr };
    bool managed{ false };

    FOUNDATION_FORCEINLINE ManagedPtr(T* ptr)
        : ptr(ptr)
        , managed(true)
    {
    }

    FOUNDATION_FORCEINLINE ManagedPtr(T& ptr)
        : ptr(&ptr)
        , managed(true)
    {
    }

    FOUNDATION_FORCEINLINE ~ManagedPtr()
    {
        if (ptr && managed)
            DTOR(ptr);
        ptr = nullptr;
        managed = false;
    }

    FOUNDATION_FORCEINLINE ManagedPtr(const ManagedPtr& o)
        : ptr(o.ptr)
        , managed(false)
    {
    }

    FOUNDATION_FORCEINLINE ManagedPtr(ManagedPtr&& o)
        : ptr(o.ptr)
        , managed(o.managed)
    {
        o.ptr = nullptr;
        o.managed = false;
    }

    FOUNDATION_FORCEINLINE ManagedPtr& operator=(ManagedPtr&& o)
    {
        this->ptr = o.ptr;
        this->managed = o.managed;
        o.ptr = nullptr;
        o.managed = false;
        return *this;
    }

    FOUNDATION_FORCEINLINE ManagedPtr& operator=(const ManagedPtr& o)
    {
        this->ptr = o.ptr;
        this->managed = false;
        return *this;
    }

    FOUNDATION_FORCEINLINE operator T* () { return ptr; }
    FOUNDATION_FORCEINLINE operator const T* () const { return ptr; }

    FOUNDATION_FORCEINLINE T* operator->() { return ptr; }
    FOUNDATION_FORCEINLINE const T* operator->() const { return ptr; }
};

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


extern double main_tick_elapsed_time_ms();

bool environment_command_line_arg(const char* name, string_const_t* value = nullptr);
bool environment_command_line_arg(const char* name, size_t name_length, string_const_t* value = nullptr);
bool environment_command_line_arg(string_const_t name, string_const_t* value = nullptr);

#if BUILD_DEBUG
struct TimeMarkerScope
{
    char label[128];
    hash_t context;
    tick_t start_time;
    const double less_ignored_elapsed_time = 0.0009;

    FOUNDATION_FORCEINLINE TimeMarkerScope(double max_time, hash_t _context, const char* fmt, ...)
        : context(_context)
        , less_ignored_elapsed_time(max_time)
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_BUFFER(label), fmt, string_length(fmt), list);
        va_end(list);

        start_time = time_current();
    }

    FOUNDATION_FORCEINLINE TimeMarkerScope(hash_t _context, const char* fmt, ...)
        : context(_context)
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_BUFFER(label), fmt, string_length(fmt), list);
        va_end(list);   
        
        start_time = time_current();
    }

    template <size_t N> FOUNDATION_FORCEINLINE
        TimeMarkerScope(const char(&name)[N])
        : context(memory_context())
    {
        string_copy(STRING_BUFFER(label), name, N - 1);
        start_time = time_current();
    }

    FOUNDATION_FORCEINLINE TimeMarkerScope(const char* FOUNDATION_RESTRICT fmt, ...)
        : context(memory_context())
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_BUFFER(label), fmt, string_length(fmt), list);
        va_end(list);

        start_time = time_current();
    }

    FOUNDATION_FORCEINLINE TimeMarkerScope(double max_time, const char* FOUNDATION_RESTRICT fmt, ...)
        : context(memory_context())
        , less_ignored_elapsed_time(max_time)
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_BUFFER(label), fmt, string_length(fmt), list);
        va_end(list);

        start_time = time_current();
    }

    FOUNDATION_FORCEINLINE ~TimeMarkerScope()
    {
        const double elapsed_time = time_elapsed(start_time);
        if (elapsed_time > less_ignored_elapsed_time)
        {
            if (elapsed_time < 0.1)
                log_debugf(context, STRING_CONST("%s took %.3lg ms"), label, elapsed_time * 1000.0);
            else if (elapsed_time < 1.0)
                log_infof(context, STRING_CONST("%s took %.3lg ms"), label, elapsed_time * 1000.0);
            else
                log_warnf(context, WARNING_PERFORMANCE, STRING_CONST("%s took %.3lg seconds <<<"), label, elapsed_time);
        }
    }
};

#define TIME_TRACKER_NAME_COUNTER_EXPAND(COUNTER, ...) TimeMarkerScope __var_time_tracker__##COUNTER (__VA_ARGS__)
#define TIME_TRACKER_NAME_COUNTER(COUNTER, ...) TIME_TRACKER_NAME_COUNTER_EXPAND(COUNTER, __VA_ARGS__)
#define TIME_TRACKER(...) TIME_TRACKER_NAME_COUNTER(__LINE__, __VA_ARGS__)

#else

#define TIME_TRACKER(...)                       \
	do {                                        \
		FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); \
	} while (0)

#endif

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

template<typename T> using alias = T;
#define MEM_NEW(context, type, ...) new (memory_allocate(context, sizeof(type), alignof(type), MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED)) type(__VA_ARGS__);
#define MEM_DELETE(ptr) {   \
    ptr->~alias<std::remove_reference<decltype(*ptr)>::type>(); \
    memory_deallocate(ptr); \
    ptr = nullptr;          \
}

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

FOUNDATION_FORCEINLINE uint32_t rgb_to_abgr(const uint32_t v, const uint8_t alpha = 0xFF)
{
    const uint8_t r = (v & 0x00FF0000) >> 16;
    const uint8_t g = (v & 0x0000FF00) >> 8;
    const uint8_t b = (v & 0x000000FF);
    return (alpha << 24) | (b << 16) | (g << 8) | (r);
}

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


