/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2022-2023 Wiimag inc. All rights reserved.
 */

#include "common.h"

#include <framework/generics.h>
#include <framework/string.h>

#include <foundation/hash.h>
#include <foundation/process.h>
#include <foundation/stream.h>
#include <foundation/path.h>
#include <foundation/environment.h>

#include <ctype.h>

#define HASH_COMMON (static_hash_string("common", 6, 14370257353172364778ULL))

time_t time_now()
{
    time_t now;
    return time(&now);
}

bool time_to_local(time_t at, tm* out_tm)
{
    #if FOUNDATION_PLATFORM_WINDOWS
    return localtime_s(out_tm, &at) == 0;
    #else
    return localtime_r(&at, out_tm) != 0;
    #endif
}

time_t time_add_days(time_t t, int days)
{
    return t + (time_one_day() * days);
}

time_t time_add_hours(time_t t, double hours)
{
    return t + (time_t)math_round(time_one_hour() * hours);
}

time_t time_work_day(time_t date, double rel)
{
    tm tm_date;

    date += math_round(time_one_day() * rel);
    time_to_local(date, &tm_date);
    while (tm_date.tm_wday == 0 || tm_date.tm_wday == 6)
    {
        date -= time_one_day();
        time_to_local(date, &tm_date);
    }

    return mktime(&tm_date);
}

bool time_date_equal(time_t da, time_t db)
{
    if (da == db)
        return true;
    tm ta, tb;
    time_to_local(da, &ta);
    time_to_local(db, &tb);
    return ta.tm_year == tb.tm_year && ta.tm_mon == tb.tm_mon && ta.tm_mday == tb.tm_mday;
}

bool time_date_before(time_t da, time_t db)
{
    if (da == db)
        return false;
    tm ta, tb;
    time_to_local(da, &ta);
    time_to_local(db, &tb);
    if (ta.tm_year < tb.tm_year)
        return true;
    if (ta.tm_year > tb.tm_year)
        return false;
    if (ta.tm_mon < tb.tm_mon)
        return true;
    if (ta.tm_mon > tb.tm_mon)
        return false;
    return ta.tm_mday < tb.tm_mday;
}

bool time_date_before_or_equal(time_t da, time_t db)
{
    if (da == db)
        return true;
    tm ta, tb;
    time_to_local(da, &ta);
    time_to_local(db, &tb);
    if (ta.tm_year < tb.tm_year)
        return true;
    if (ta.tm_year > tb.tm_year)
        return false;
    if (ta.tm_mon < tb.tm_mon)
        return true;
    if (ta.tm_mon > tb.tm_mon)
        return false;
    return ta.tm_mday <= tb.tm_mday;
}

double time_elapsed_days(time_t from, time_t to)
{
    double diff = difftime(to, from);
    return diff / time_one_day();
}

double time_elapsed_days_round(time_t from, time_t to)
{
    double diff = difftime(to, from);
    return math_round(diff / time_one_day());
}

char from_hex(char ch)
{
    return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10;
}

char to_hex(char code)
{
    static char hex[] = "0123456789abcdef";
    return hex[code & 15];
}

string_const_t url_encode(const char* str, size_t str_length)
{
    const char* pstr = str;
    string_t buf = string_static_buffer((str_length > 0 ? str_length : string_length(str)) * 3 + 1);
    char* pbuf = buf.str;
    while (*pstr) {
        char c = *pstr;
        if (c > 0 && (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~'))
            *pbuf++ = c;
        else if (c == ' ')
            *pbuf++ = '%', *pbuf++ = '2', * pbuf++ = '0';
        else
        {
            pbuf += string_format(pbuf, buf.length - pointer_diff(pbuf, buf.str), STRING_CONST("%%%02X"), (unsigned char)c).length;
        }
        pstr++;
    }
    *pbuf = '\0';
    buf.length = pointer_diff(pbuf, buf.str);
    return string_to_const(buf);
}

string_const_t url_decode(const char* str, size_t str_length)
{
    const char* pstr = str;
    string_t buf = string_static_buffer((str_length > 0 ? str_length : string_length(str)) + 1);
    char* pbuf = buf.str;
    while (*pstr) {
        if (*pstr == '%') {
            if (pstr[1] && pstr[2]) {
                *pbuf++ = from_hex(pstr[1]) << 4 | from_hex(pstr[2]);
                pstr += 2;
            }
        }
        else if (*pstr == '+') {
            *pbuf++ = ' ';
        }
        else {
            *pbuf++ = *pstr;
        }
        pstr++;
    }
    *pbuf = '\0';
    buf.length = pointer_diff(pbuf, buf.str);
    return string_to_const(buf);
}

string_t fs_read_text(const char* path, size_t path_length)
{
    if (!fs_is_file(path, path_length))
        return string_t{ nullptr, 0 };

    stream_t* text_stream = fs_open_file(path, path_length, STREAM_IN | STREAM_BINARY);
    if (text_stream == nullptr)
        return string_t{ nullptr, 0 };

    const size_t text_buffer_size = stream_size(text_stream);
    string_t text_buffer = string_allocate(text_buffer_size + 1, text_buffer_size + 2);
    string_t text = stream_read_string_buffer(text_stream, text_buffer.str, text_buffer.length);
    stream_deallocate(text_stream);
    return text;
}

string_const_t fs_clean_file_name(const char* filename, size_t filename_length)
{
    string_const_t illegal_chars = CTEXT("\\/:?\"<>|");
    string_t out_filename = string_static_buffer(filename_length);
    char* p = out_filename.str;
    for (auto c : generics::fixed_array(filename, filename_length))
    {
        if (string_find(STRING_ARGS(illegal_chars), c, 0) == STRING_NPOS)
            *p++ = c;
    }
    *p = '\0';
    return string_to_const(out_filename);
}

hash_t fs_hash_file(string_t file_path)
{
    return fs_hash_file(string_to_const(file_path));
}

hash_t fs_hash_file(string_const_t file_path)
{
    stream_t* fstream = fs_open_file(STRING_ARGS(file_path), STREAM_IN | STREAM_BINARY);
    if (fstream == nullptr)
        return 0;

    hash_t stream_hash = 0xbaadf00d;
    char hash_buffer[8192];
    size_t stream_size = stream_available_read(fstream), read_size = 0;
    while ((read_size = stream_read(fstream, hash_buffer, min(ARRAY_COUNT(hash_buffer), stream_size))))
    {
        stream_hash ^= hash(hash_buffer, read_size - (read_size % 8ULL));
        stream_size = stream_available_read(fstream);
    }

    stream_deallocate(fstream);

    return stream_hash;
}

FOUNDATION_STATIC string_const_t environment_command_line_trim_param(string_const_t name)
{
    for (int i = 0; i < 2; ++i)
    {
        if (name.length > 0 && name.str[0] == '-')
        {
            name.str++;
            name.length--;
        }
    }

    return name;
}

FOUNDATION_STATIC bool environment_command_line_read_value(
    const string_const_t* cmdline,
    string_const_t arg,
    size_t& arg_index,
    size_t arg_size,
    string_const_t param,
    string_const_t* value /*= nullptr*/)
{
    arg = environment_command_line_trim_param(arg);
    if (arg.length == 0 || param.length == 0)
        return false;

    const size_t arg_length = arg.length;
    const size_t value_assign_offset_sign = string_find(STRING_ARGS(arg), '=', param.length - 1);
    if (value_assign_offset_sign != STRING_NPOS)
        arg.length = value_assign_offset_sign;

    if (string_equal(arg.str, arg.length, STRING_ARGS(param)))
    {
        if (value_assign_offset_sign != STRING_NPOS)
        {
            if (value)
                *value = string_const(arg.str + value_assign_offset_sign + 1, arg_length - value_assign_offset_sign - 1);
        }
        else if (arg_index + 1 < arg_size)
        {
            string_const_t arg_value = cmdline[arg_index + 1];
            if (arg_value.length > 0 && arg_value.str[0] != '-')
            {
                if (value)
                    *value = arg_value;
                arg_index++;
            }
            else if (value)
                *value = string_null();
        }
        else if (value)
        {
            value->length = 0;
            value->str = nullptr;
        }

        return true;
    }

    return false;
}

string_const_t environment_username()
{
    static char username_buffer[32] = { 0 };

    if (username_buffer[0] != 0)
        return string_to_const(username_buffer);

    string_const_t username = environment_variable(STRING_CONST("USERNAME"));
    if (username.length)
    {
        string_copy(STRING_BUFFER(username_buffer), STRING_ARGS(username));
        return string_to_const(username_buffer);
    }
    
    string_const_t app_dir = environment_application_directory();
    #if FOUNDATION_PLATFORM_WINDOWS
    string_const_t user_dir = path_directory_name(STRING_ARGS(app_dir));
    user_dir = path_directory_name(STRING_ARGS(user_dir));
    user_dir = path_directory_name(STRING_ARGS(user_dir));
    user_dir = path_directory_name(STRING_ARGS(user_dir));
    username = path_base_file_name(STRING_ARGS(user_dir));

    string_copy(STRING_BUFFER(username_buffer), STRING_ARGS(username));
    #else
    string_copy(STRING_BUFFER(username_buffer), STRING_CONST("OSX"));
    #endif

    return string_to_const(username_buffer);
}

bool environment_argument(string_const_t name, string_const_t* value, bool check_environment_variable)
{
    name = environment_command_line_trim_param(name);
    const string_const_t* cmdline = environment_command_line();

    for (size_t iarg = 0, argsize = array_size(cmdline); iarg < argsize; ++iarg)
    {
        string_const_t arg = cmdline[iarg];

        if (arg.length == 0 || arg.str[0] != '-')
            continue;

        if (environment_command_line_read_value(cmdline, arg, iarg, argsize, name, value))
            return true;
    }

    // Check for the environment variable
    if (check_environment_variable && name.length >= 4)
    {
        char envname_buffer[32] = { 0 };
        string_t envname = string_copy(STRING_BUFFER(envname_buffer), STRING_ARGS(name));
        envname = string_to_upper_ascii(STRING_BUFFER(envname_buffer), STRING_ARGS(envname));
        envname = string_replace(STRING_ARGS(envname), sizeof(envname_buffer), STRING_CONST("-"), STRING_CONST("_"), true);

        string_const_t envval = environment_variable(STRING_ARGS(envname));
        if (envval.length)
        {
            if (value)
                *value = envval;
            return true;
        }
    }

    return false;
}

bool path_equals(const char* a, size_t a_length, const char* b, size_t b_length)
{
    string_const_t a_protocol = path_directory_name(a, a_length);
    string_const_t b_protocol = path_directory_name(b, b_length);
    if (!string_equal_nocase(STRING_ARGS(a_protocol), STRING_ARGS(b_protocol)))
        return false;

    string_const_t a_name = path_file_name(a, a_length);
    string_const_t b_name = path_file_name(b, b_length);
    if (!string_equal_nocase(STRING_ARGS(a_name), STRING_ARGS(b_name)))
        return false;

    string_const_t a_dir_name = path_directory_name(a, a_length);
    string_const_t b_dir_name = path_directory_name(b, b_length);
    if (!string_equal_nocase(STRING_ARGS(a_dir_name), STRING_ARGS(b_dir_name)))
        return false;

    return true;
}

bool time_same_day(time_t d1, time_t d2)
{
    tm tm1, tm2;
    if (!time_to_local(d1, &tm1) || !time_to_local(d2, &tm2))
        return false;
    return (tm1.tm_year == tm2.tm_year) && (tm1.tm_mon == tm2.tm_mon) && (tm1.tm_mday == tm2.tm_mday);
}

time_t time_make(int year, int month, int day, int hour, int minute, int second, int millisecond)
{
    struct tm t;
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    t.tm_isdst = -1;
    return mktime(&t);
}

bool time_is_weekend()
{
    tm tm_now;
    const time_t now = time_now();
    if (!time_to_local(now, &tm_now))
        return false;
    return (tm_now.tm_wday == 0) || (tm_now.tm_wday == 6);
}

bool time_is_working_hours()
{
    if (time_is_weekend())
        return false;

    tm tm_now;
    const time_t now = time_now();
    if (!time_to_local(now, &tm_now))
        return false;

    const int hour = tm_now.tm_hour;
    return (hour >= 9) && (hour < 17);
}

string_t path_normalize_name(char* buff, size_t capacity, const char* _path, size_t path_length, const char replacement_char /*= '_'*/)
{
    string_t path = string_copy(buff, capacity, _path, path_length);
    path = path_clean(STRING_ARGS(path), capacity);

    // Remove any file path illegal characters
    char* p = path.str;
    for (size_t i = 0, end = path.length; i < end; ++i)
    {
        if (p[i] == '<' || p[i] == '>' || p[i] == ':' || p[i] == '"' || p[i] == '/' || p[i] == '\\' || p[i] == '|' || p[i] == '?' || p[i] == '*')
            p[i] = replacement_char;
    }

    return path;
}

string_const_t environment_get_resources_path()
{
    string_const_t exe_path = environment_executable_path();
    string_const_t exe_dir = path_directory_name(STRING_ARGS(exe_path));

    #if FOUNDATION_PLATFORM_WINDOWS
        // On windows the resources path is the same as the executable dir
        return exe_dir;
    #elif FOUNDATION_PLATFORM_MACOS
        static thread_local char resources_path_buffer[BUILD_MAX_PATHLEN];

        // On MacOS the resources path is the executable dir + ../Resources 
        string_t resources_path = string_copy(STRING_BUFFER(resources_path_buffer), STRING_ARGS(exe_dir));
        resources_path = path_append(STRING_ARGS(resources_path), BUILD_MAX_PATHLEN, STRING_CONST("../Resources"));
        resources_path = path_clean(STRING_ARGS(resources_path), BUILD_MAX_PATHLEN);

        return string_to_const(resources_path);
    #else

        #error Not implemented

    #endif
}

string_const_t environment_get_build_path()
{
    string_const_t exe_path = environment_executable_path();
    string_const_t exe_dir = path_directory_name(STRING_ARGS(exe_path));

    #if FOUNDATION_PLATFORM_WINDOWS
        // On windows the resources path is the same as the executable dir
        return exe_dir;
    #elif FOUNDATION_PLATFORM_MACOS
        static thread_local char build_path_buffer[BUILD_MAX_PATHLEN];

        // On MacOS the resources path is the executable dir + ../../../
        string_t build_path = string_copy(STRING_BUFFER(build_path_buffer), STRING_ARGS(exe_dir));
        build_path = path_append(STRING_ARGS(build_path), BUILD_MAX_PATHLEN, STRING_CONST("../../../"));
        build_path = path_clean(STRING_ARGS(build_path), BUILD_MAX_PATHLEN);

        return string_to_const(build_path);
    #else

        #error Not implemented

    #endif
}

char stream_peek(stream_t* stream)
{
    const size_t skipped = stream_skip_whitespace(stream);
    const size_t abytes = stream_available_read(stream);
    if (abytes == 0)
        return 0;

    const size_t pos = stream_tell(stream);
    char token = 0;
    const ssize_t rb = stream_read(stream, &token, 1);
    stream_seek(stream, -rb, STREAM_SEEK_CURRENT);

    return token;
}

size_t stream_peek(stream_t* stream, char* buf, size_t capacity)
{
    const size_t skipped = stream_skip_whitespace(stream);
    const size_t abytes = stream_available_read(stream);
    if (abytes < capacity)
        return 0;

    const size_t pos = stream_tell(stream);
    const ssize_t rb = stream_read(stream, buf, capacity);
    stream_seek(stream, -rb, STREAM_SEEK_CURRENT);

    return rb;
}

size_t stream_skip_consume_until(stream_t* stream, char c)
{
    // Read stream until we find the character
    size_t read = 0;
    while (stream_available_read(stream) > 0)
    {
        char token = 0;
        const ssize_t rb = stream_read(stream, &token, 1);
        if (rb == 0)
            break;

        ++read;
        if (token == c)
            break;
    }

    return read;
}

string_t stream_read_consume_until(stream_t* stream, char c)
{
    size_t read = 0;
    size_t capacity = 32;
    string_t content = string_allocate(0, capacity);

    // Read stream until we find the character
    while (stream_available_read(stream) > 0)
    {
        char token = 0;
        const ssize_t rb = stream_read(stream, &token, 1);
        if (rb == 0)
            break;

        ++read;
        if (token == c)
            break;

        if (content.length >= capacity)
        {
            size_t new_capacity = capacity * 2;
            content.str = (char*)memory_reallocate(content.str, new_capacity, 0, capacity, 0);
            capacity = new_capacity;
        }

        content.str[content.length++] = token;
    }

    return content;
}
