/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "common.h"

#include <framework/generics.h>
#include <framework/string.h>

#include <foundation/hash.h>
#include <foundation/process.h>
#include <foundation/stream.h>
#include <foundation/path.h>
#include <foundation/environment.h>

#include <stack> 

#if FOUNDATION_PLATFORM_WINDOWS
    #include <foundation/windows.h>
    #include <Commdlg.h>

    #include <iostream>

    extern HWND _window_handle;
#endif

#define HASH_COMMON (static_hash_string("common", 6, 14370257353172364778ULL))

void execute_tool(const string_const_t& name, string_const_t* argv, size_t argc, const char* working_dir, size_t working_dir_length)
{
    process_t* tool = process_allocate();
    process_set_executable_path(tool, STRING_ARGS(name));
    if (working_dir && working_dir_length > 0)
        process_set_working_directory(tool, working_dir, working_dir_length);
    process_set_arguments(tool, argv, argc);
    process_set_flags(tool, PROCESS_DETACHED | PROCESS_HIDE_WINDOW);
    process_spawn(tool);
    process_deallocate(tool);
}

void on_thread_exit(function<void()> func)
{
    class ThreadExiter
    {
        std::stack<function<void()>> exit_funcs;
    public:
        ThreadExiter() = default;
        ThreadExiter(ThreadExiter const&) = delete;
        void operator=(ThreadExiter const&) = delete;
        ~ThreadExiter()
        {
            while (!exit_funcs.empty())
            {
                exit_funcs.top()();
                exit_funcs.pop();
            }
        }
        void add(function<void()> func)
        {
            exit_funcs.push(std::move(func));
        }
    };

    static thread_local ThreadExiter exiter;
    exiter.add(std::move(func));
}

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
        if (*pstr < 0 || isalnum(*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~')
            *pbuf++ = *pstr;
        else if (*pstr == ' ')
            *pbuf++ = '%', * pbuf++ = '2', * pbuf++ = '0';
        else
            *pbuf++ = '%', * pbuf++ = to_hex(*pstr >> 4), * pbuf++ = to_hex(*pstr & 15);
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

void open_in_shell(const char* path)
{
    #ifdef _WIN32
        ::ShellExecuteA(NULL, "open", path, NULL, NULL, SW_SHOWDEFAULT);
    #else
        #if __APPLE__
            const char* open_executable = "open";
        #else
            const char* open_executable = "xdg-open";
        #endif
        char command[2048];
        snprintf(command, 2048, "%s \"%s\"", open_executable, path);
        system(command);
    #endif
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

#if FOUNDATION_PLATFORM_WINDOWS
    bool open_file_dialog(const char* dialog_title, const char* extension, const char* current_file_path, function<bool(string_const_t)> selected_file_callback)
    {
        string_t file_path_buffer = string_static_buffer(1024, true);
        if (current_file_path != nullptr)
        {
            string_t file_path = string_format(STRING_ARGS(file_path_buffer), STRING_CONST("%s"), current_file_path);
            file_path = path_clean(STRING_ARGS(file_path), file_path_buffer.length);
            file_path = string_replace(STRING_ARGS(file_path), file_path_buffer.length,
                STRING_CONST("/"), STRING_CONST("\\"), true);
        }

        OPENFILENAMEA ofn;
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = _window_handle;
        ofn.lpstrFile = file_path_buffer.str;
        ofn.nMaxFile = (DWORD)file_path_buffer.length;
        if (extension == nullptr)
        {
            ofn.lpstrFilter = "All\0*.*\0";
        }
        else
        {
            char file_extensions_buffer[1024] = { '\0' };
            string_t extension_filters = string_format(STRING_BUFFER(file_extensions_buffer),
                STRING_CONST("%s|All Files (*.*)|*.*"), extension);
            extension_filters = string_replace(STRING_ARGS(extension_filters), sizeof(file_extensions_buffer),
                STRING_CONST("|"), "\0", 1, true);
            extension_filters.str[extension_filters.length+1] = '\0';
            ofn.lpstrFilter = extension_filters.str;
        }
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrTitle = dialog_title;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        if (GetOpenFileNameA(&ofn))
        {
            selected_file_callback(string_const(file_path_buffer.str, string_length(file_path_buffer.str)));
            return true;
        }
        
        return false;
    }
#elif FOUNDATION_PLATFORM_MACOS
    // See common.m
#else
    #error "Not implemented"
#endif

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

bool environment_command_line_arg(string_const_t name, string_const_t* value /*= nullptr*/)
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

    return false;
}

bool environment_command_line_arg(const char* name, string_const_t* value)
{
    return environment_command_line_arg(string_const(name, string_length(name)), value);
}

bool environment_command_line_arg(const char* name, size_t name_length, string_const_t* value)
{
    return environment_command_line_arg(string_const(name, name_length), value);
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

void process_debug_output(const char* output, size_t output_length /*= 0*/)
{
    #if BUILD_DEVELOPMENT
        #if FOUNDATION_PLATFORM_WINDOWS
            OutputDebugStringA(output);
        #else
            fprintf(stdout, "%.*s", output_length ? (int)output_length : (int)string_length(output), output);
        #endif
    #endif
}

bool process_redirect_io_to_console()
{
#if FOUNDATION_PLATFORM_WINDOWS
    bool result = true;
    FILE* fp;

    if (IsDebuggerPresent())
        return false;

    if (AttachConsole(ATTACH_PARENT_PROCESS) == 0)
        return false;

    // Redirect STDIN if the console has an input handle
    if (GetStdHandle(STD_INPUT_HANDLE) != INVALID_HANDLE_VALUE)
        if (freopen_s(&fp, "CONIN$", "r", stdin) != 0)
            result = false;
        else
            setvbuf(stdin, NULL, _IONBF, 0);

    // Redirect STDOUT if the console has an output handle
    if (GetStdHandle(STD_OUTPUT_HANDLE) != INVALID_HANDLE_VALUE)
        if (freopen_s(&fp, "CONOUT$", "w", stdout) != 0)
            result = false;
        else
            setvbuf(stdout, NULL, _IONBF, 0);

    // Redirect STDERR if the console has an error handle
    if (GetStdHandle(STD_ERROR_HANDLE) != INVALID_HANDLE_VALUE)
        if (freopen_s(&fp, "CONOUT$", "w", stderr) != 0)
            result = false;
        else
            setvbuf(stderr, NULL, _IONBF, 0);

    // Make C++ standard streams point to console as well.
    std::ios::sync_with_stdio(true);

    // Clear the error state for each of the C++ standard streams.
    std::wcout.clear();
    std::cout.clear();
    std::wcerr.clear();
    std::cerr.clear();
    std::wcin.clear();
    std::cin.clear();

    return result;
#else
    return false;
#endif
}

bool process_release_console()
{
#if FOUNDATION_PLATFORM_WINDOWS
    bool result = true;
    FILE* fp;

    // Just to be safe, redirect standard IO to NUL before releasing.

    // Redirect STDIN to NUL
    if (freopen_s(&fp, "NUL:", "r", stdin) != 0)
        result = false;
    else
        setvbuf(stdin, NULL, _IONBF, 0);

    // Redirect STDOUT to NUL
    if (freopen_s(&fp, "NUL:", "w", stdout) != 0)
        result = false;
    else
        setvbuf(stdout, NULL, _IONBF, 0);

    // Redirect STDERR to NUL
    if (freopen_s(&fp, "NUL:", "w", stderr) != 0)
        result = false;
    else
        setvbuf(stderr, NULL, _IONBF, 0);

    // Detach from console
    if (!FreeConsole())
        result = false;

    return result;
#else
    return false;
#endif
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
