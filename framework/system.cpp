/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 *
 * Windows system function implementations
 */

#include "system.h"
#include "version.h"
#include "resource.h"

#include <framework/string.h>
#include <framework/localization.h>

#include <foundation/fs.h>
#include <foundation/path.h>
#include <foundation/stream.h>

#if FOUNDATION_PLATFORM_WINDOWS
    #include <foundation/windows.h>
    #include <Commdlg.h>
    #include <CommCtrl.h>

    #include <iostream>

    #pragma comment( lib, "comctl32.lib" )
#endif

extern void* _window_handle;

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
        char command_buffer[2048];
        snprintf(command_buffer, 2048, "%s \"%s\"", open_executable, command);
        system(command_buffer);

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
bool system_open_file_dialog(const char* dialog_title, const char* extension, const char* current_file_path, const function<bool(string_const_t)>& selected_file_callback)
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
        ofn.lpstrFilter = tr("All Files\0*.*\0");
    }
    else
    {
        char file_extensions_buffer[1024] = { '\0' };
        string_const_t fmttr = RTEXT("%s|All Files (*.*)|*.*");
        string_t extension_filters = string_format(STRING_BUFFER(file_extensions_buffer), STRING_ARGS(fmttr), extension);
        extension_filters = string_replace(STRING_ARGS(extension_filters), sizeof(file_extensions_buffer), STRING_CONST("|"), "\0", 1, true);
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
        selected_file_callback(string_to_const(file_path_buffer.str));
        return true;
    }

    return false;
}

bool system_save_file_dialog(
    const char* dialog_title,
    const char* extension,
    const char* current_file_path,
    const function<bool(string_const_t)>& selected_file_callback)
{
    static thread_local wchar_t file_path_buffer[BUILD_MAX_PATHLEN] = {0};

    wstring_from_string(STRING_BUFFER(file_path_buffer), current_file_path, string_length(current_file_path));

    // Setup Windows save file dialog
    OPENFILENAME ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = (HWND)_window_handle;
    ofn.lpstrFile = file_path_buffer;
    ofn.nMaxFile = (DWORD)sizeof(file_path_buffer)/sizeof(file_path_buffer[0]);
    if (extension == nullptr)
    {
        ofn.lpstrFilter = L"All Files\0*.*\0";
    }
    else
    {
        char filters_buffer[BUILD_MAX_PATHLEN] = { '\0' };
        string_t extension_filters = string_copy(STRING_BUFFER(filters_buffer), extension, string_length(extension));
        extension_filters = string_replace(STRING_ARGS(extension_filters), sizeof(filters_buffer), STRING_CONST("|"), "\0", 1, true);
        extension_filters.str[extension_filters.length + 1] = '\0';

        wchar_t wchar_filters_buffer[BUILD_MAX_PATHLEN] = { '\0' };
        wstring_from_string(STRING_BUFFER(wchar_filters_buffer), STRING_ARGS(extension_filters));
        ofn.lpstrFilter = ofn.lpstrDefExt = wchar_filters_buffer;
    }

    string_const_t current_file_name = path_file_name(current_file_path, string_length(current_file_path));
    string_const_t current_file_dir = path_directory_name(current_file_path, string_length(current_file_path));

    static thread_local wchar_t current_file_dir_buffer[BUILD_MAX_PATHLEN] = {0};
    wstring_from_string(STRING_BUFFER(current_file_dir_buffer), STRING_ARGS(current_file_dir));
    
    static thread_local wchar_t file_name_buffer[BUILD_MAX_PATHLEN] = {0};
    wstring_from_string(STRING_BUFFER(file_name_buffer), STRING_ARGS(current_file_name));

    static thread_local wchar_t dialog_title_buffer[BUILD_MAX_PATHLEN] = {0};
    wstring_from_string(STRING_BUFFER(dialog_title_buffer), dialog_title, string_length(dialog_title));

    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = file_name_buffer;
    ofn.nMaxFileTitle = BUILD_MAX_PATHLEN;
    ofn.lpstrTitle = dialog_title_buffer;
    ofn.lpstrInitialDir = current_file_dir_buffer;
    
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOREADONLYRETURN;

    if (::GetSaveFileName(&ofn))
    {
        static thread_local char file_path_cstr_buffer[BUILD_MAX_PATHLEN] = {0};
        string_convert_utf16(STRING_BUFFER(file_path_cstr_buffer), (const uint16_t*)file_path_buffer, wstring_length(file_path_buffer));
        return selected_file_callback(string_to_const(file_path_cstr_buffer));
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

uint32_t system_get_last_error(string_const_t* out_error_string /*= nullptr*/)
{
#if FOUNDATION_PLATFORM_WINDOWS
    DWORD error = ::GetLastError();

    if (out_error_string)
    {
        // Get GetLastError error string
        static thread_local char win32_error_message_buffer[512] = { 0 };

        size_t size = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), STRING_BUFFER(win32_error_message_buffer), NULL);

        *out_error_string = string_const(win32_error_message_buffer, size);

        // Trim trailing whitespace
        *out_error_string = string_remove_trailing_whitespaces(STRING_ARGS(*out_error_string));
    }

    return error;
#else
    if (out_error_string)
        *out_error_string = string_null();
    return 0;
#endif
}

string_const_t system_get_last_error_message(uint32_t* out_error /*= nullptr*/)
{
    string_const_t error_message{};
    uint32_t error = system_get_last_error(&error_message);
    if (out_error)
        *out_error = error;
    return error_message;
}

string_const_t system_executable_resource_to_file(const char* resource_name, const char* resource_type)
{
#if FOUNDATION_PLATFORM_WINDOWS

    HMODULE hModule = GetModuleHandle(NULL);
    HRSRC hResource = FindResourceA(hModule, resource_name, resource_type);
    if (!hResource)
    {
        FOUNDATION_ASSERT_FAIL("Failed to find resource");
        return string_null();
    }
    HGLOBAL hMemory = LoadResource(hModule, hResource);
    if (hMemory == 0)
    {
        FOUNDATION_ASSERT_FAIL("Failed to load resource");
        return string_null();
    }
        
    DWORD dwSize = SizeofResource(hModule, hResource);
    LPVOID lpAddress = LockResource(hMemory);
    FOUNDATION_ASSERT(lpAddress);

    stream_t* resource_stream = fs_temporary_file();
    if (resource_stream == nullptr)
    {
        UnlockResource(hMemory);
        return string_null();
    }

    stream_write(resource_stream, lpAddress, dwSize);
        
    UnlockResource(hMemory);

    static thread_local char resource_path_buffer[BUILD_MAX_PATHLEN] = {0};
    string_const_t resource_stream_path = stream_path(resource_stream);

    string_t resource_paths_str = string_copy(STRING_BUFFER(resource_path_buffer), STRING_ARGS(resource_stream_path));
    stream_deallocate(resource_stream);

    // Strip protocol
    string_const_t resource_path = path_strip_protocol(STRING_ARGS(resource_paths_str));

    return resource_path;

#else

    return CTEXT("Not supported");

#endif    
}

bool system_notification_push(const char* title, size_t title_length, const char* message, size_t message_length)
{
#if FOUNDATION_PLATFORM_WINDOWS
    // Create a new notification.
    NOTIFYICONDATAA nid = { 0 };
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = (HWND)system_window_handle();
    nid.uID = 1;
    nid.uFlags = NIF_INFO;

    // Generate new GUID
    GUID guid;
    CoCreateGuid(&guid);
    nid.uFlags |= NIF_GUID;
    nid.guidItem = guid;

    // Set notification state
    nid.uFlags |= NIF_STATE;
    nid.dwState = NIS_HIDDEN;
    nid.dwStateMask = NIS_HIDDEN;

    // Load GLFW_ICON from resources.
    HINSTANCE hInstance = GetModuleHandle(NULL);
    LoadIconMetric(hInstance, MAKEINTRESOURCE(GLFW_ICON), LIM_SMALL, &(nid.hIcon));
    LoadIconMetric(hInstance, MAKEINTRESOURCE(GLFW_ICON), LIM_LARGE, &(nid.hBalloonIcon));
    nid.uFlags |= NIF_ICON | NIF_SHOWTIP;

    // Keep the notification active in the notification tray
    nid.uFlags |= NIF_REALTIME;
    nid.uTimeout = 10000;

    nid.dwInfoFlags = NIIF_RESPECT_QUIET_TIME | NIIF_USER | NIIF_LARGE_ICON;
    string_copy(nid.szInfoTitle, sizeof(nid.szInfoTitle), title, title_length);
    string_copy(nid.szInfo, sizeof(nid.szInfo), message, message_length);
    string_copy(nid.szTip, sizeof(nid.szTip), STRING_CONST(PRODUCT_NAME));

    // Push a notification to the Windows notification tray.
    BOOL success = Shell_NotifyIconA(NIM_ADD, &nid);

    // Print any errors
    if (!success)
    {
        string_const_t system_error_msg{};
        DWORD error = system_get_last_error(&system_error_msg);
        log_errorf(0, ERROR_SYSTEM_CALL_FAIL, 
            STRING_CONST("Failed to push notification (0x%8X): %.*s\n"), error, STRING_FORMAT(system_error_msg));
    }

    // Remove the notification icon from the system tray
    //Shell_NotifyIconA(NIM_DELETE, &nid);

    return success != 0;
#else
    log_warnf(0, WARNING_UNSUPPORTED, STRING_CONST("Notification push not supported on this platform"));
    return false;
#endif
}

void* system_window_handle()
{
    return _window_handle;
}
