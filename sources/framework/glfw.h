/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
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
#include <GLFW/glfw3native.h>

// We gather version tests as define in order to easily see which features are version-dependent.
#define GLFW_VERSION_COMBINED           (GLFW_VERSION_MAJOR * 1000 + GLFW_VERSION_MINOR * 100 + GLFW_VERSION_REVISION)
#ifdef GLFW_RESIZE_NESW_CURSOR          // Let's be nice to people who pulled GLFW between 2019-04-16 (3.4 define) and 2019-11-29 (cursors defines) // FIXME: Remove when GLFW 3.4 is released?
    #define GLFW_HAS_NEW_CURSORS            (GLFW_VERSION_COMBINED >= 3400) // 3.4+ GLFW_RESIZE_ALL_CURSOR, GLFW_RESIZE_NESW_CURSOR, GLFW_RESIZE_NWSE_CURSOR, GLFW_NOT_ALLOWED_CURSOR
#else
    #define GLFW_HAS_NEW_CURSORS            (0)
#endif
#define GLFW_HAS_GAMEPAD_API            (GLFW_VERSION_COMBINED >= 3300) // 3.3+ glfwGetGamepadState() new api
#define GLFW_HAS_GETKEYNAME             (GLFW_VERSION_COMBINED >= 3200) // 3.2+ glfwGetKeyName()

void glfw_set_window_center(GLFWwindow* window);

GLFWwindow* glfw_create_window_geometry(const char* window_title);

void glfw_save_window_geometry(GLFWwindow* window);

GLFWmonitor* glfw_find_window_monitor(GLFWwindow* window);

GLFWmonitor* glfw_find_window_monitor(int window_x, int window_y);

bool glfw_is_window_focused(GLFWwindow* window);

bool glfw_is_any_mouse_button_down(GLFWwindow* window);
