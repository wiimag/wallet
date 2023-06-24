/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "glfw.h"

#if FOUNDATION_PLATFORM_WINDOWS

    #include <framework/resource.h>

    #pragma comment( lib, "glfw3.lib" )

    #include <GLFW/glfw3native.h>

#elif FOUNDATION_PLATFORM_MACOS

    #include <foundation/apple.h>

#endif

#include <framework/string.h>
#include <framework/common.h>
#include <framework/session.h>

#include <foundation/environment.h>

/*! Main window platform handle */
void* _window_handle = nullptr;

// GLFW data
// TODO: Replace with new window_t*
GLFWwindow* _glfw_main_window = nullptr;
GLFWcursor* _glfw_default_cursor = nullptr;


bool glfw_get_window_monitor_size(int window_x, int window_y, int* monitor_width, int* monitor_height)
{
    FOUNDATION_ASSERT(monitor_width);
    FOUNDATION_ASSERT(monitor_height);
    
    // Get the list of monitors
    int monitors_length;
    GLFWmonitor** monitors = glfwGetMonitors(&monitors_length);

    if (monitors == NULL)
        return false;

    // Figure out which monitor the window is in
    for (int i = 0; i < monitors_length; i++)
    {
        // Get the monitor position
        int monitor_x, monitor_y;
        glfwGetMonitorPos(monitors[i], &monitor_x, &monitor_y);

        GLFWvidmode* monitor_vidmode = (GLFWvidmode*)glfwGetVideoMode(monitors[i]);

        if (monitor_vidmode == NULL)
            continue; // Video mode is required for width and height, so skip this monitor

        *monitor_width = monitor_vidmode->width;
        *monitor_height = monitor_vidmode->height;

        if (window_x == INT_MAX || window_y == INT_MAX)
            return true; // Default position, take the first monitor for the specs

        // Set the owner to this monitor if the center of the window is within its bounding box
        if ((window_x > monitor_x && window_x < (monitor_x + *monitor_width)) && (window_y > monitor_y && window_y < (monitor_y + *monitor_height)))
            return true;
    }

    return false;
}

bool glfw_get_window_monitor_size(GLFWwindow* window, int* monitor_width, int* monitor_height)
{
    FOUNDATION_ASSERT(monitor_width);
    FOUNDATION_ASSERT(monitor_height);

    // Get the list of monitors
    int monitors_length;
    GLFWmonitor** monitors = glfwGetMonitors(&monitors_length);

    if (monitors == NULL)
        return false;
        
    int window_x, window_y;
    glfwGetWindowPos(window, &window_x, &window_y);

    // Figure out which monitor the window is in
    for (int i = 0; i < monitors_length; i++) 
    {
        // Get the monitor position
        int monitor_x, monitor_y;
        glfwGetMonitorPos(monitors[i], &monitor_x, &monitor_y);

        GLFWvidmode* monitor_vidmode = (GLFWvidmode*)glfwGetVideoMode(monitors[i]);

        if (monitor_vidmode == NULL)
            continue; // Video mode is required for width and height, so skip this monitor

        *monitor_width = monitor_vidmode->width;
        *monitor_height = monitor_vidmode->height;

        // Set the owner to this monitor if the center of the window is within its bounding box
        if ((window_x > monitor_x && window_x < (monitor_x + *monitor_width)) && (window_y > monitor_y && window_y < (monitor_y + *monitor_height))) 
            return true;
    }

    return false;
}

void glfw_set_window_center(GLFWwindow* window)
{
    // Get window position and size
    int window_x, window_y;
    glfwGetWindowPos(window, &window_x, &window_y);

    int window_width, window_height;
    glfwGetWindowSize(window, &window_width, &window_height);

    // Halve the window size and use it to adjust the window position to the center of the window
    window_width = int(window_width * 0.5f);
    window_height = int(window_height * 0.5f);

    window_x += window_width;
    window_y += window_height;

    // Get the list of monitors
    int monitors_length;
    GLFWmonitor** monitors = glfwGetMonitors(&monitors_length);

    if (monitors == NULL) {
        // Got no monitors back
        return;
    }

    // Figure out which monitor the window is in
    GLFWmonitor* owner = NULL;
    int owner_x = 0, owner_y = 0, owner_width = 1, owner_height = 1;

    for (int i = 0; i < monitors_length; i++) {
        // Get the monitor position
        int monitor_x, monitor_y;
        glfwGetMonitorPos(monitors[i], &monitor_x, &monitor_y);

        GLFWvidmode* monitor_vidmode = (GLFWvidmode*)glfwGetVideoMode(monitors[i]);

        if (monitor_vidmode == NULL) {
            // Video mode is required for width and height, so skip this monitor
            continue;

        }

        int monitor_width = monitor_vidmode->width;
        int monitor_height = monitor_vidmode->height;

        // Set the owner to this monitor if the center of the window is within its bounding box
        if ((window_x > monitor_x && window_x < (monitor_x + monitor_width)) && (window_y > monitor_y && window_y < (monitor_y + monitor_height))) {
            owner = monitors[i];

            owner_x = monitor_x;
            owner_y = monitor_y;

            owner_width = monitor_width;
            owner_height = monitor_height;
        }
    }

    if (owner != NULL) {
        // Set the window position to the center of the owner monitor
        glfwSetWindowPos(window, owner_x + int(owner_width * 0.5f) - window_width, owner_y + int(owner_height * 0.5f) - window_height);
    }
}

GLFWwindow* glfw_create_window_geometry(const char* window_title)
{
    bool main_window_maximized = session_get_bool("main_window_maximized");
    int window_x = session_get_integer("main_window_x", INT_MAX);
    int window_y = session_get_integer("main_window_y", INT_MAX);
    int window_width = max(640, session_get_integer("main_window_width", 1600));
    int window_height = max(480, session_get_integer("main_window_height", 900));

    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);

    GLFWmonitor* monitor = glfw_find_window_monitor(window_x, window_y);
    if (monitor == glfwGetPrimaryMonitor())
        glfwWindowHint(GLFW_MAXIMIZED, main_window_maximized ? GLFW_TRUE : GLFW_FALSE);

    float scale_x = 1.0f, scale_y = 1.0f;
    #if FOUNDATION_PLATFORM_WINDOWS
    glfwGetMonitorContentScale(monitor, &scale_x, &scale_y);
    #endif

    GLFWwindow* window = glfwCreateWindow((int)(window_width / scale_x), (int)(window_height / scale_y), window_title, nullptr, nullptr);
    if (window == nullptr)
        return nullptr;

    bool has_position = session_key_exists("main_window_x");
    if (has_position)
    {
        if (window_x != INT_MAX && window_y != INT_MAX)
        {
            glfwSetWindowPos(window, window_x, window_y);
        }

        if (main_window_maximized)
            glfwMaximizeWindow(window);
    }
    else
    {
        glfw_set_window_center(window);
    }

    //glfwShowWindow(window);

    return window;
}

void glfw_save_window_geometry(GLFWwindow* window)
{
    int main_window_maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED);
    session_set_bool("main_window_maximized", main_window_maximized == GLFW_TRUE);

    float xs = 1.0f, ys = 1.0f;
    int window_x = 0, window_y = 0;
    int window_width = 0, window_height = 0;
    glfwGetWindowPos(window, &window_x, &window_y);
    glfwGetWindowSize(window, &window_width, &window_height);

    session_set_integer("main_window_width", window_width);
    session_set_integer("main_window_height", window_height);
    session_set_integer("main_window_x", window_x);
    session_set_integer("main_window_y", window_y);
}

GLFWmonitor* glfw_find_window_monitor(GLFWwindow* window)
{
    GLFWmonitor* monitor = glfwGetWindowMonitor(window);
    if (monitor == nullptr)
    {
        int window_x = 0, window_y = 0;
        glfwGetWindowPos(window, &window_x, &window_y);
        return glfw_find_window_monitor(window_x, window_y);
    }

    return monitor;
}

GLFWmonitor* glfw_find_window_monitor(int window_x, int window_y)
{
    GLFWmonitor* monitor = nullptr;

    // Find on which monitor our window is.
    int monitor_count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitor_count);
    for (int i = 0; i < monitor_count; ++i)
    {
        int mx, my, mw, mh;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);

        if (monitor == nullptr || (window_x >= mx && window_y >= my && window_x <= mx + mw && window_y <= my + mh))
            monitor = monitors[i];
    }

    return monitor;
}

bool glfw_is_window_focused(GLFWwindow* window)
{
    #ifdef __EMSCRIPTEN__
        return true;
    #else
        return glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
    #endif
}

bool glfw_is_any_mouse_button_down(GLFWwindow* window)
{
    for (int i = GLFW_MOUSE_BUTTON_1; i <= GLFW_MOUSE_BUTTON_LAST; ++i)
    {
        if (glfwGetMouseButton(window, i) == GLFW_PRESS)
            return true;
    }

    return false;
}

int glfw_translate_untranslated_key(int key, int scancode)
{
    #if GLFW_HAS_GETKEYNAME && !defined(__EMSCRIPTEN__)
        // GLFW 3.1+ attempts to "untranslated" keys, which goes the opposite of what every other framework does, making using lettered shortcuts difficult.
        // (It had reasons to do so: namely GLFW is/was more likely to be used for WASD-type game controls rather than lettered shortcuts, but IHMO the 3.1 change could have been done differently)
        // See https://github.com/glfw/glfw/issues/1502 for details.
        // Adding a workaround to undo this (so our keys are translated->untranslated->translated, likely a lossy process).
        // This won't cover edge cases but this is at least going to cover common cases.
        if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL)
            return key;
        const char* key_name = glfwGetKeyName(key, scancode);
        if (key_name && key_name[0] != 0 && key_name[1] == 0)
        {
            const char char_names[] = "`-=[]\\,;\'./";
            const int char_keys[] = { 
                GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET, 
                GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_COMMA, GLFW_KEY_SEMICOLON, 
                GLFW_KEY_APOSTROPHE, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, 0 };
            FOUNDATION_ASSERT(ARRAY_COUNT(char_names) == ARRAY_COUNT(char_keys));
            if (key_name[0] >= '0' && key_name[0] <= '9') { key = GLFW_KEY_0 + (key_name[0] - '0'); }
            else if (key_name[0] >= 'A' && key_name[0] <= 'Z') { key = GLFW_KEY_A + (key_name[0] - 'A'); }
            else if (key_name[0] >= 'a' && key_name[0] <= 'z') { key = GLFW_KEY_A + (key_name[0] - 'a'); }
            else if (const char* p = strchr(char_names, key_name[0])) { key = char_keys[p - char_names]; }
        }
        // if (action == GLFW_PRESS) printf("key %d scancode %d name '%s'\n", key, scancode, key_name);
    #else
        FOUNDATION_UNUSED(scancode);
    #endif
    return key;
}

int glfw_key_to_modifier(int key)
{
    if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL)
        return GLFW_MOD_CONTROL;
    if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT)
        return GLFW_MOD_SHIFT;
    if (key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT)
        return GLFW_MOD_ALT;
    if (key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER)
        return GLFW_MOD_SUPER;
    return 0;
}

void glfw_log_error(int error, const char* description)
{
    log_errorf(0, ERROR_EXCEPTION, STRING_CONST("GLFW Error %d: %s"), error, description);
}

void glfw_set_window_main_icon(GLFWwindow* window)
{
    #if FOUNDATION_PLATFORM_WINDOWS
        HWND window_handle = glfwGetWin32Window(window);
        HINSTANCE module_handle = ::GetModuleHandle(nullptr);
        HANDLE big_icon = LoadImageA(module_handle, MAKEINTRESOURCEA(GLFW_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
        HANDLE small_icon = LoadImageA(module_handle, MAKEINTRESOURCEA(GLFW_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        if (big_icon)
            SendMessage(window_handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big_icon));
        if (small_icon)
            SendMessage(window_handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
    #endif
}

#if !FOUNDATION_PLATFORM_MACOS
void* glfw_platform_window_handle(GLFWwindow* window)
{
    void* window_handle = nullptr;
    #if FOUNDATION_PLATFORM_LINUX
        window_handle = (void*)(uintptr_t)glfwGetX11Window(window);
    #elif FOUNDATION_PLATFORM_MACOS
        // See glfw_osx.cpp
    #elif FOUNDATION_PLATFORM_WINDOWS
        window_handle = glfwGetWin32Window(window);
    #else
        #error Not implemented
    #endif

    return window_handle;
}
#endif

void glfw_shutdown()
{
    if (_glfw_default_cursor)
        glfwDestroyCursor(_glfw_default_cursor);

    glfwDestroyWindow(_glfw_main_window);
    glfwTerminate();

    _glfw_main_window = nullptr;
}

GLFWwindow* glfw_main_window(const char* window_title /*= nullptr*/)
{
    if (window_title == nullptr)
        return _glfw_main_window;

    FOUNDATION_ASSERT(_glfw_main_window == nullptr);

    // Setup window
    glfwSetErrorCallback(glfw_log_error);
    if (!glfwInit())
        return nullptr;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    #if FOUNDATION_PLATFORM_MACOS
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
    #else
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    #endif

    GLFWwindow* window = glfw_create_window_geometry(window_title);
    if (window == nullptr)
        return nullptr;

    const application_t* app = environment_application();
    string_const_t version_string = string_from_version_static(app->version);
    glfwSetWindowTitle(window, string_format_static_const("%s v.%.*s", window_title, STRING_FORMAT(version_string)));
    glfw_set_window_main_icon(window);

    // Set global window handles if not set already.
    if (_glfw_main_window == nullptr)
        _glfw_main_window = window;

    if (_window_handle == 0)
        _window_handle = glfw_platform_window_handle(window);

    #if 1 //!BUILD_DEBUG
    // As soon as the request to close the window is initiate we hide it in order to 
    // give the impression that the application is already close, but in fact, the shutdown sequence
    // is still running.
    // FIXME: This is a hack, we should speed up the shutdown sequence in most cases
    glfwSetWindowCloseCallback(window, [](GLFWwindow* window)
    {
        if (glfwWindowShouldClose(window))
        {
            log_infof(0, STRING_CONST("Closing application..."));
            glfwHideWindow(window);
        }
    });
    #endif

    return window;
}


void glfw_show_wait_cursor(GLFWwindow* window)
{
    #if FOUNDATION_PLATFORM_WINDOWS
        HCURSOR cursor = LoadCursor(NULL, IDC_WAIT);
        SetClassLongPtr(window ? (HWND)glfw_platform_window_handle(window) : NULL, GCLP_HCURSOR, (LONG_PTR)cursor);
    #else
        FOUNDATION_UNUSED(window);
    #endif
}

void glfw_show_normal_cursor(GLFWwindow* window)
{
    if (_glfw_default_cursor == nullptr)
        _glfw_default_cursor = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);

    #if FOUNDATION_PLATFORM_WINDOWS
        HCURSOR cursor = LoadCursor(NULL, IDC_ARROW);
        SetClassLongPtr(window ? (HWND)glfw_platform_window_handle(window) : NULL, GCLP_HCURSOR, (LONG_PTR)cursor);
    #else
        if (window)
            glfwSetCursor(window, _glfw_default_cursor);
    #endif
}

void glfw_request_close_window(GLFWwindow* window)
{
    FOUNDATION_ASSERT(window);

    glfwSetWindowShouldClose(window, GLFW_TRUE);
    #if 1 //!BUILD_DEBUG
    if (glfwGetError(nullptr) == GLFW_NO_ERROR)
        glfwHideWindow(window);
    #endif
}

float glfw_get_window_scale(GLFWwindow* window)
{
    float scale = 1.0f;
    if (window)
    {
        GLFWmonitor* monitor = glfw_find_window_monitor(window);
        float scale_y = 1.0f;
        glfwGetMonitorContentScale(monitor, &scale, &scale_y);
    }
    return scale;
}

float glfw_current_window_scale()
{
    return glfw_get_window_scale(_glfw_main_window);
}
