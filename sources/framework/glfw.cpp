/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#include "glfw.h"

#include "session.h"
#include "common.h"

#if FOUNDATION_PLATFORM_WINDOWS

    #pragma comment( lib, "glfw3.lib" )

#elif FOUNDATION_PLATFORM_MACOS

#endif

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

    glfwShowWindow(window);

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
