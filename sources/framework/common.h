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

// ## MACROS

constexpr double DNAN = __builtin_nan("0");
#define CTEXT(str) string_const(STRING_CONST(str))
#define ARRAY_COUNT(ARR) (sizeof(ARR) / sizeof(ARR[0]))

#define DEFINE_ENUM_FLAGS(T) \
    FOUNDATION_FORCEINLINE const T operator~ (T a) { return (T)~(unsigned int)a; } \
    FOUNDATION_FORCEINLINE const bool operator== (const T a, const int b) { return (unsigned int)a == b; } \
    FOUNDATION_FORCEINLINE const bool operator&& (const T a, const T b) { return (unsigned int)a != 0 && (unsigned int)b != 0; } \
    FOUNDATION_FORCEINLINE const bool operator&& (const T a, const bool b) { return (unsigned int)a != 0 && b; } \
    FOUNDATION_FORCEINLINE const T operator| (const T a, const T b) { return (T)((unsigned int)a | (unsigned int)b); } \
    FOUNDATION_FORCEINLINE const T operator& (const T a, const T b) { return (T)((unsigned int)a & (unsigned int)b); } \
    FOUNDATION_FORCEINLINE const T operator^ (const T a, const T b) { return (T)((unsigned int)a ^ (unsigned int)b); } \
    FOUNDATION_FORCEINLINE T& operator|= (T& a, const T b) { return (T&)((unsigned int&)a |= (unsigned int)b); } \
    FOUNDATION_FORCEINLINE T& operator&= (T& a, const T b) { return (T&)((unsigned int&)a &= (unsigned int)b); } \
    FOUNDATION_FORCEINLINE T& operator^= (T& a, const T b) { return (T&)((unsigned int&)a ^= (unsigned int)b); }

template<typename T> T min(T a, T b) { return (((a) < (b)) ? (a) : (b)); }
template<typename T> T max(T a, T b) { return (((a) > (b)) ? (a) : (b)); }
template<typename R, typename T, typename U> R max(T a, U b) { return (((R)(a) > (R)(b)) ? (R)(a) : (R)(b)); }

typedef struct GLFWwindow GLFWwindow;
typedef function<void(GLFWwindow* window)> app_update_handler_t;
typedef function<void(GLFWwindow* window, int frame_width, int frame_height)> app_render_handler_t;

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

// ## STRINGS

size_t string_occurence(const char* str, size_t len, char c);
size_t string_line_count(const char* str, size_t len);
lines_t string_split_lines(const char* str, size_t len);
string_t* string_split(string_const_t str, string_const_t sep);
void string_lines_finalize(lines_t& lines);
bool string_contains_nocase(const char* lhs, size_t lhs_length, const char* rhs, size_t rhs_length);
bool string_equal_ignore_whitespace(const char* lhs, size_t lhs_length, const char* rhs, size_t rhs_length);
string_t string_utf8_unescape(const char* s, size_t length);
string_const_t string_from_date(const tm& tm);
string_const_t string_from_date(time_t at);
string_t string_static_buffer(size_t required_length = 64, bool clear_memory = false);
string_const_t string_from_currency(double value, const char* fmt = nullptr, size_t fmt_length = 0);
const char* string_format_static_const(const char fmt[], ...);
string_const_t string_format_static(const char* fmt, size_t fmt_length, ...);
time_t string_to_date(const char* date_str, size_t date_str_length, tm* out_tm = nullptr);
string_const_t string_to_const(const char* str);
string_const_t string_trim(string_const_t str, char c = ' ');
bool string_compare_less(const char* str1, const char* str2);
bool string_compare_less(const char* str1, size_t str1_length, const char* str2, size_t str2_length);
string_const_t string_remove_line_returns(char* buffer, size_t capacity, const char* str, size_t length);
string_t string_remove_line_returns(const char* str, size_t length);

FOUNDATION_FORCEINLINE bool is_whitespace(char c)
{
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        return true;
    return false;
}

template <size_t N> FOUNDATION_FORCEINLINE constexpr string_const_t string_const(const char(&s)[N]) 
{ 
    return string_const(s, min(N-1, string_length(s)));
}

FOUNDATION_FORCEINLINE bool string_is_null(string_const_t s)
{
    return s.str == nullptr || s.length == 0;
}

FOUNDATION_FORCEINLINE bool string_equal(string_const_t lhs, string_const_t rhs)
{
    return string_equal(STRING_ARGS(lhs), STRING_ARGS(rhs));
}

FOUNDATION_FORCEINLINE bool string_equal(string_t lhs, string_const_t rhs)
{
    return string_equal(STRING_ARGS(lhs), STRING_ARGS(rhs));
}

FOUNDATION_FORCEINLINE bool string_equal(string_const_t lhs, string_t rhs)
{
    return string_equal(STRING_ARGS(lhs), STRING_ARGS(rhs));
}

FOUNDATION_FORCEINLINE bool string_is_null(string_t s)
{
    return s.str == nullptr || s.length == 0;
}

FOUNDATION_FORCEINLINE bool string_starts_with(const char* str, size_t str_length, const char* prefix, size_t prefix_length)
{
    if (str_length < prefix_length)
        return false;
    return string_equal(str, prefix_length, prefix, prefix_length);
}

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
//decltype(declval<typename Iter>().operator*())
template<typename Iter>
string_const_t string_join(Iter begin, Iter end, const function<string_const_t(const typename Iter::type& e)>& iter,
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

template<typename T>
string_const_t string_join(T* const list, size_t count, const function<string_const_t(T& e)>& iter,
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

template<typename T>
string_const_t string_join(T* const list, const function<string_const_t(T& e)>& iter,
    string_const_t sep = { ", ", 2 },
    string_const_t open_token = { nullptr, 0 },
    string_const_t close_token = { nullptr, 0 })
{
    return string_join(list, array_size(list), iter, sep, open_token, close_token);
}

// ## Array

#define foreach(_VAR_NAME, _ARR) \
    decltype(_ARR) _VAR_NAME = array_size(_ARR) > 0 ? &_ARR[0] : nullptr; \
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

#if NOT_WORKING
template<typename T>
void array_sort(T* arr, const function<bool(const T& a, const T& b)>& less_comparer)
{
    qsort_r(arr, array_size(arr), sizeof(T), (void*)&less_comparer, [](void* context, const void* va, const void* vb)
    {
        const function<int(const T&, const T&)>& less_comparer = *(function<int(const T&, const T&)>*)context;
        const T& a = *(const T*)va;
        const T& b = *(const T*)vb;
        
        if (less_comparer(a, b))
            return -1;
        
        return memcmp(va, vb, sizeof(T));
    });
}
#else
#define array_sort(ARRAY, EXPRESSION) std::sort(ARRAY, ARRAY + array_size(ARRAY), [=](const auto& a, const auto& b){ return EXPRESSION; });
#endif

template<typename T, typename U>
bool array_contains(const T* arr, const function<bool(const T& a, const U& b)>& compare_equal, const U& v)
{
    for (unsigned i = 0, end = array_size(arr); i < end; ++i)
    {
        if (compare_equal(arr[i], v))
            return true;
    }

    return false;
}

template<typename T, typename V>
FOUNDATION_STATIC int array_binary_search(const T* array, uint32_t _num, const V& _key)
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

extern bool main_is_batch_mode();
extern bool main_is_graphical_mode();
extern bool main_is_interactive_mode();
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
            if (elapsed_time < 1.0)
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

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL constexpr uint32_t to_uint(size_t v)
{
    FOUNDATION_ASSERT(v <= UINT_MAX);
    return (uint32_t)v;
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
