/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/platform.h>

#if FOUNDATION_PLATFORM_WINDOWS

    //#include <resource.h>
    #include <foundation/windows.h>
    
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

/*! Create or return the GLFW main window.
 * 
 *  @param window_title The window title when creating the main window.
 * 
 *  @return The main window.
 */
GLFWwindow* glfw_main_window(const char* window_title = nullptr);

/*! @brief Releases GLFW global resources. 
 * 
 *  The main window will be destroyed if still alive.
 */
void glfw_shutdown();

/*! @brief Center the window on the user main monitor.
 * 
 *  @param window The window to center.
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

/*! Translate key code from GLFW to our own.
 * @param key The key code to translate.
 * @param scancode The scancode of the key.
 * @return The translated key code.
 */
int glfw_translate_untranslated_key(int key, int scancode);

/*! Converts a GLFW key to a modifier.
 * @param key The key to convert.
 * @return The modifier.
 */
int glfw_key_to_modifier(int key);

/*! Log GLFW errors.
 * 
 * @param error The error code.
 * @param description The error description.
 */
void glfw_log_error(int error, const char* description);

/*! @brief Set the main icon of the window.
 *  @param window The window to set the icon for.
 */
void glfw_set_window_main_icon(GLFWwindow* window);

/*! Return the GLFW platform specific window handle.
 * 
 *  @param window The window to get the handle for.
 *  @return The platform specific window handle.
 */
void* glfw_platform_window_handle(GLFWwindow* window);

/*! Get the monitor size on which the window is located.
 *
 *  @param window The window to get the monitor size for.
 *  @param monitor_width The monitor width.
 *  @param monitor_height The monitor height.
 *  @return True if the monitor size was found, false otherwise.
 */
bool glfw_get_window_monitor_size(GLFWwindow* window, int* monitor_width, int* monitor_height);

/*! Get the monitor size on which the window is located given its top-left coordinates.
 *
 *  @param window_x The window x position.
 *  @param window_y The window y position.
 *  @param monitor_width The monitor width.
 *  @param monitor_height The monitor height.
 *  @return True if the monitor size was found, false otherwise.
 */
bool glfw_get_window_monitor_size(int window_x, int window_y, int* monitor_width, int* monitor_height);

/*! Show the waiting/loading cursor.
 * 
 *  @todo Only works for Windows as of now
 *  
 *  @param window The window to show the cursor for.
 */
void glfw_show_wait_cursor(GLFWwindow* window);

/*! Show the normal cursor.
 *  
 *  @param window The window to show the cursor for.
 */
void glfw_show_normal_cursor(GLFWwindow* window);

/*! Structure used to scope and display a wait cursor. 
 * 
 *  @remark Prefer to use the macro #GLFW_WAIT_CURSOR
 */
struct GLFWWaitCursorScope
{
    GLFWwindow* window;
    GLFWWaitCursorScope(GLFWwindow* _window)
        : window(_window)
    {
        glfw_show_wait_cursor(window);
    }

    GLFWWaitCursorScope()
        : GLFWWaitCursorScope(glfw_main_window())
    {
    }

    ~GLFWWaitCursorScope()
    {
        glfw_show_normal_cursor(window);
    }
};

/*! @def GLFW_WAIT_CURSOR(window)
 *  @brief Scope a wait cursor.
 *  @param window The window to show the cursor for.
 *  @remark Prefer to use this macro instead of #GLFWWaitCursorScope directly.
 */
#define WAIT_CURSOR GLFWWaitCursorScope FOUNDATION_PREPROCESSOR_JOIN(_wait_cursor_scope_, __COUNTER__)
#define GLFW_WAIT_CURSOR(window) GLFWWaitCursorScope FOUNDATION_PREPROCESSOR_JOIN(_wait_cursor_scope_, __COUNTER__)(window);
