/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include "function.h"

#include <foundation/fs.h>
#include <foundation/time.h>
#include <foundation/array.h>
 
#include <time.h>
#include <limits>

// ## MACROS

constexpr double DNAN = __builtin_nan("0");
#define ARRAY_COUNT(ARR) (sizeof(ARR) / sizeof(ARR[0]))

#define DEFINE_ENUM_FLAGS(T) \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const T operator~ (T a) { return static_cast<T>(~(std::underlying_type_t<T>)a); } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const bool operator!= (const T a, const std::underlying_type_t<T> b) { return (std::underlying_type_t<T>)a != b; } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const bool operator== (const T a, const std::underlying_type_t<T> b) { return (std::underlying_type_t<T>)a == b; } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const bool operator&& (const T a, const T b) { return (std::underlying_type_t<T>)a != 0 && (std::underlying_type_t<T>)b != 0; } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const bool operator&& (const T a, const bool b) { return (std::underlying_type_t<T>)a != 0 && b; } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const T operator| (const T a, const T b) { return (T)((std::underlying_type_t<T>)a | (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const T operator& (const T a, const T b) { return (T)((std::underlying_type_t<T>)a & (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const T operator^ (const T a, const T b) { return (T)((std::underlying_type_t<T>)a ^ (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL T& operator|= (T& a, const T b) { return (T&)((std::underlying_type_t<T>&)a |= (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL T& operator&= (T& a, const T b) { return (T&)((std::underlying_type_t<T>&)a &= (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL T& operator^= (T& a, const T b) { return (T&)((std::underlying_type_t<T>&)a ^= (std::underlying_type_t<T>)b); } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const bool test(const T a, const T b) { return (a & b) == b; } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const bool any(const T a, const T b) { return (a & b) != 0; } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const bool none(const T a, const T b) { return (a & b) == 0; } \
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr const bool one(const T a, const T b) { const auto bits = ((std::underlying_type_t<T>)a & (std::underlying_type_t<T>)b); return bits && !(bits & (bits-1)); }

template<typename T> FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr T min(T a, T b) { return (((a) < (b)) ? (a) : (b)); }
template<typename T> FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr T max(T a, T b) { return (((a) > (b)) ? (a) : (b)); }

typedef struct GLFWwindow GLFWwindow;
typedef function<void(GLFWwindow* window)> app_update_handler_t;
typedef function<void(GLFWwindow* window, int frame_width, int frame_height)> app_render_handler_t;

// ## Array

#define foreach(_VAR_NAME, _ARR) \
    std::remove_reference<decltype(_ARR)>::type _VAR_NAME = array_size(_ARR) > 0 ? &_ARR[0] : nullptr; \
    for (unsigned i = 0, end = array_size(_ARR); i < end; _VAR_NAME = &_ARR[++i])

template<typename T> T* array_last(T* arr)
{
    const size_t count = array_size(arr);
    if (count == 0)
        return nullptr;
    return &arr[count-1];
}

template<typename T> size_t array_offset(const T* arr, const T* element)
{
    return pointer_diff(element, arr) / sizeof(T);
}

#define array_sort(ARRAY, EXPRESSION) std::sort(ARRAY, ARRAY + array_size(ARRAY), [=](const auto& a, const auto& b){ return EXPRESSION; });

template<typename T>
T array_sort_by(T arr, const function<int(const typename std::remove_pointer<T>::type& a, const typename std::remove_pointer<T>::type& b)>& comparer)
{
#if FOUNDATION_PLATFORM_WINDOWS
    qsort_s(arr, array_size(arr), sizeof(typename std::remove_pointer<T>::type), [](void* context, const void* va, const void* vb)
#else
    qsort_r(arr, array_size(arr), sizeof(typename std::remove_pointer<T>::type), (void*)&comparer, [](void* context, const void* va, const void* vb)
#endif
    {
        const auto& comparer = *(function<int(const typename std::remove_pointer<T>::type& a, const typename std::remove_pointer<T>::type& b)>*)context;
        const typename std::remove_pointer<T>::type& a = *(const typename std::remove_pointer<T>::type*)va;
        const typename std::remove_pointer<T>::type& b = *(const typename std::remove_pointer<T>::type*)vb;
        return comparer(a, b);

#if FOUNDATION_PLATFORM_WINDOWS
    }, (void*)&comparer);
#else
    });
#endif

    return arr;
}

template<typename T, typename U, typename Compare>
bool array_contains(const T* arr, const U& v, const Compare& compare_equal)
{
    for (unsigned i = 0, end = array_size(arr); i < end; ++i)
    {
        if (compare_equal(arr[i], v))
            return true;
    }

    return false;
}

template<typename T, typename U>
bool array_contains(const T* arr, const U& v)
{
    return array_contains(arr, v, [](const T& a, const U& b) { return a == b; });
}

template<typename T, typename V>
int array_binary_search(const T* array, uint32_t _num, const V& _key)
{
    uint32_t offset = 0;
    for (uint32_t ll = _num; offset < ll;)
    {
        const uint32_t idx = (offset + ll) / 2;

        const T& mid_value = array[idx];
        if (mid_value > _key)
            ll = idx;
        else if (mid_value < _key)
            offset = idx + 1;
        else
            return idx;
    }

    return ~offset;
}

template<typename T, typename V>
int array_binary_search(const T* array, const V& _key)
{
    return array_binary_search<T, V>(array, array_size(array), _key);
}

template<typename T, typename V, typename Comparer>
int array_binary_search_compare(const T array, const typename V& _key, Comparer compare)
{
    uint32_t offset = 0;
    for (uint32_t ll = array_size(array); offset < ll;)
    {
        const uint32_t idx = (offset + ll) / 2;

        const typename std::remove_pointer<T>::type& mid_value = array[idx];
        const int cmp = compare(mid_value, _key);
        if (cmp > 0)
            ll = idx;
        else if (cmp < 0)
            offset = idx + 1;
        else
            return idx;
    }

    return ~offset;
}

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

// ##FS

string_t fs_read_text(const char* path, size_t path_length);

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

/*! @brief Checks if the current time in a weekend day.
 */
bool time_is_weekend();

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

//
// ## PROCESS
//

void open_in_shell(const char* path);
void execute_tool(const string_const_t& name, string_const_t* argv, size_t argc, const char* working_dir = 0, size_t working_dir_length = 0);

void on_thread_exit(function<void()> func);

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

bool process_release_console();
bool process_redirect_io_to_console();
void process_debug_output(const char* output, size_t output_length = 0);

// ## OS

/*! Open a native dialog window to select a file of given type.
 \param dialog_title                        Dialog window title label
 \param extension                               Set of extensions used in the dialog window (i.e. "DICOM (*.dcm)|*.dcm")
 \param current_file_path             Current file path to open the dialog window at.
 \param selected_file_callback  Callback invoked when a file is selected.
 \return Returns true if the dialog window opened successfully.
 */
extern bool open_file_dialog(
    const char* dialog_title,
    const char* extension,
    const char* current_file_path,
    function<bool(string_const_t)> selected_file_callback);

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
    FOUNDATION_FORCEINLINE TimeMarkerScope(hash_t _context, const char* fmt, ...)
        : context(_context)
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_CONST_CAPACITY(label), fmt, string_length(fmt), list);
        va_end(list);   
        
        start_time = time_current();
    }

    template <size_t N> FOUNDATION_FORCEINLINE
        TimeMarkerScope(const char(&name)[N])
        : context(memory_context())
    {
        string_copy(STRING_CONST_CAPACITY(label), name, N);
        start_time = time_current();
    }

    FOUNDATION_FORCEINLINE TimeMarkerScope(const char* FOUNDATION_RESTRICT fmt, ...)
        : context(memory_context())
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_CONST_CAPACITY(label), fmt, string_length(fmt), list);
        va_end(list);

        start_time = time_current();
    }

    FOUNDATION_FORCEINLINE TimeMarkerScope(double max_time, const char* FOUNDATION_RESTRICT fmt, ...)
        : context(memory_context())
        , less_ignored_elapsed_time(max_time)
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_CONST_CAPACITY(label), fmt, string_length(fmt), list);
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

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr int32_t to_int(size_t v)
{
    FOUNDATION_ASSERT_MSGFORMAT(v <= INT_MAX, "%" PRIsize " > %d", v, INT_MAX);
    return (int32_t)v;
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr uint32_t to_uint(int32_t v)
{
    FOUNDATION_ASSERT_MSGFORMAT(v >= 0, "%d<0", v);
    return (uint32_t)v;
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr size_t to_size(int64_t v)
{
    FOUNDATION_ASSERT_MSGFORMAT(v >= 0, "%lld<0", v);
    return (size_t)v;
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr uint32_t to_uint(size_t v)
{
    FOUNDATION_ASSERT(v <= UINT_MAX);
    return (uint32_t)v;
}

template<typename T = void> 
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr T* to_ptr(uint32_t v)
{
    return (T*)(uintptr_t)(v);
}

template<typename T>
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr T to_opaque(void* ptr)
{
    const auto v = (intptr_t)(ptr);
    FOUNDATION_ASSERT(std::numeric_limits<T>::min() <= v && v <= std::numeric_limits<T>::max());
    return (T)v;
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr uint32_t rgb_to_abgr(const uint32_t v, const uint8_t alpha = 0xFF)
{
    const uint8_t r = (v & 0x00FF0000) >> 16;
    const uint8_t g = (v & 0x0000FF00) >> 8;
    const uint8_t b = (v & 0x000000FF);
    return (alpha << 24) | (b << 16) | (g << 8) | (r);
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr hash_t hash_combine(hash_t h1, hash_t h2)
{
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr hash_t hash_combine(hash_t h1, hash_t h2, hash_t h3)
{
    return hash_combine(hash_combine(h1, h2), h3);
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr hash_t hash_combine(hash_t h1, hash_t h2, hash_t h3, hash_t h4)
{
    return hash_combine(hash_combine(h1, h2), hash_combine(h3, h4));
}
