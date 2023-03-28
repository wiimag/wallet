/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 *
 * Windows system function implementations
 */

#include "system.h"

#include <framework/string.h>

#include <foundation/path.h>

#if FOUNDATION_PLATFORM_WINDOWS
#include <foundation/windows.h>
#include <Commdlg.h>

#include <iostream>

extern void* _window_handle;
#endif

#include <stack> 

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

bool system_execute_command(const char* command)
{
    #ifdef _WIN32
        return (uint64_t)::ShellExecuteA(NULL, "open", command, NULL, NULL, SW_SHOWDEFAULT) > 32;
    #else
        #if __APPLE__
            const char* open_executable = "open";
        #else
            const char* open_executable = "xdg-open";
        #endif
        char command[2048];
        snprintf(command, 2048, "%s \"%s\"", open_executable, path);
        system(command);

        return true;
    #endif
}

void system_browse_to_file(const char* path, size_t path_length, bool dir /*= false*/)
{
    const size_t capacity = path_length + 8 + 1;
    string_t url = string_allocate(0, capacity);
    url = string_copy(url.str, capacity, "file:///", 8);

    string_const_t path_dir = dir ? string_const(path, path_length) : path_directory_name(path, path_length);
    url = string_concat(url.str, capacity, STRING_ARGS(url), STRING_ARGS(path_dir));
    system_execute_command(STRING_ARGS(url));
    string_deallocate(url);
}

string_const_t system_app_data_local_path()
{
    static thread_local char _app_data_local_path_buffer[BUILD_MAX_PATHLEN] = {0};
    static thread_local string_t _app_data_local_path{};
    if (string_is_null(_app_data_local_path))
    {
        #if FOUNDATION_PLATFORM_WINDOWS
            
            // Get the AppData/Local path
            wchar_t wpath[BUILD_MAX_PATHLEN];
            SHGetFolderPathW(0, CSIDL_LOCAL_APPDATA, 0, 0, wpath);
            _app_data_local_path = string_convert_utf16(_app_data_local_path_buffer, BUILD_MAX_PATHLEN, (const uint16_t*)wpath, wstring_length(wpath));

        #elif FOUNDATION_PLATFORM_APPLE
            
            // Get the HOME/Applications path
            const char* home = getenv("HOME");
            _app_data_local_path = string_copy(STRING_BUFFER(_app_data_local_path_buffer), home, string_length(home));
            _app_data_local_path = string_concat(STRING_BUFFER(_app_data_local_path_buffer), STRING_ARGS(_app_data_local_path), STRING_CONST("/Applications"));

            // Clean the path
            _app_data_local_path = path_clean(STRING_ARGS(_app_data_local_path), BUILD_MAX_PATHLEN);

        #else
            #error Not implemented
        #endif
    }

    return string_to_const(_app_data_local_path);
}

const char* system_platform_name(platform_t platform)
{
    switch (platform)
    {
        case PLATFORM_WINDOWS: return "Windows";
        case PLATFORM_LINUX: return "Linux";
        case PLATFORM_MACOS: return "MacOS";
        case PLATFORM_ANDROID: return "Android";
        case PLATFORM_IOS: return "iOS";
        case PLATFORM_RASPBERRYPI: return "Raspberry Pi";
        case PLATFORM_BSD: return "BSD";
        case PLATFORM_TIZEN: return "Tizen";

        default: 
            return "Unknown";
    }
}

#if FOUNDATION_PLATFORM_WINDOWS
bool system_open_file_dialog(const char* dialog_title, const char* extension, const char* current_file_path, function<bool(string_const_t)> selected_file_callback)
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
    ofn.hwndOwner = (HWND)_window_handle;
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
        extension_filters.str[extension_filters.length + 1] = '\0';
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
// See system.mm
#else
#error "Not implemented"
#endif

void system_process_debug_output(const char* output, size_t output_length /*= 0*/)
{
    #if BUILD_DEVELOPMENT
        #if FOUNDATION_PLATFORM_WINDOWS
            OutputDebugStringA(output);
        #else
            fprintf(stdout, "%.*s", output_length ? (int)output_length : (int)string_length(output), output);
        #endif
    #endif
}

bool system_process_redirect_io_to_console()
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

bool system_process_release_console()
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

void system_execute_tool(const string_const_t& name, string_const_t* argv, size_t argc, const char* working_dir, size_t working_dir_length)
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

void system_thread_on_exit(function<void()>&& func)
{
    static thread_local ThreadExiter exiter;
    exiter.add(std::move(func));
}
