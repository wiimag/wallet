/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/platform.h>

#if FOUNDATION_PLATFORM_WINDOWS

    #include <foundation/windows.h>

    #include <resource.h>

    #define GLFW_EXPOSE_NATIVE_WIN32
    #define GLFW_EXPOSE_NATIVE_WGL
    #undef THREAD_PRIORITY_NORMAL
    #undef APIENTRY

#elif FOUNDATION_PLATFORM_MACOS

    #define GLFW_EXPOSE_NATIVE_COCOA

#endif

#include <GLFW/glfw3.h>

// We gather version tests as define in order to easily see which features are version-dependent.
#define GLFW_VERSION_COMBINED           (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)
#ifdef GLFW_RESIZE_NESW_CURSOR          // Let's be nice to people who pulled GLFW between 2019-04-16 (3.4 define) and 2019-11-29 (cursors defines) // FIXME: Remove when GLFW 3.4 is released?
    #define GLFW_HAS_NEW_CURSORS            (GLFW_VERSION_COMBINED >= 3400) // 3.4+ GLFW_RESIZE_ALL_CURSOR, GLFW_RESIZE_NESW_CURSOR, GLFW_RESIZE_NWSE_CURSOR, GLFW_NOT_ALLOWED_CURSOR
#else
    #define GLFW_HAS_NEW_CURSORS            (0)
#endif
#define GLFW_HAS_GAMEPAD_API            (GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetGamepadState() new api
#define GLFW_HAS_GETKEYNAME             (GLFW_VERSION_COMBINED >= 3200) // 3.2+ glfwGetKeyName()

/*! @brief  Returns the main window.
 *  @return The main window.
 */
extern GLFWwindow* main_window();

/*! @brief Center the window on the user main monitor.
 * @param window The window to center.
 */
void glfw_set_window_center(GLFWwindow* window);

/*! @brief Create a new window and restore its previous geometry.
 *  @param window_title The window title.
 *  @return The new window.
 */
GLFWwindow* glfw_create_window_geometry(const char* window_title);

/*! @brief Save the window geometry.
 *  @param window The window.
 */
void glfw_save_window_geometry(GLFWwindow* window);

/*! @brief Find the monitor on which the window is located.
 *  @param window The window to find the monitor for.
 *  @return The monitor on which the window is located.
 */
GLFWmonitor* glfw_find_window_monitor(GLFWwindow* window);

/*! @brief Find the monitor on which the window is located given its top-left coordinates.
 *  @param window_x The window x position.
 *  @param window_y The window y position.
 *  @return The monitor on which the window is located.
 */
GLFWmonitor* glfw_find_window_monitor(int window_x, int window_y);

/*! @brief Checks if the window is currently focused
 * @param window The window to check.
 * @return True if the window is focused, false otherwise.
 */
bool glfw_is_window_focused(GLFWwindow* window);

/*! @brief Checks if the mouse is currently over the window.
 * @param window The window to check.
 * @return True if the mouse is over the window, false otherwise.
 */
bool glfw_is_any_mouse_button_down(GLFWwindow* window);
