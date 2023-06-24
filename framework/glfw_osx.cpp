/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include <foundation/platform.h>

#if FOUNDATION_PLATFORM_MACOS

#define GLFW_EXPOSE_NATIVE_COCOA

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

void* glfw_platform_window_handle(GLFWwindow* window)
{
    return (void*)glfwGetCocoaWindow(window);
}

#endif
