/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include <foundation/platform.h>

#if BUILD_APPLICATION

#if FOUNDATION_COMPILER_MSVC
 // Disable C26495 and C26498 warnings for this file
 // https://docs.microsoft.com/en-us/cpp/code-quality/c26495?view=msvc-160
#pragma warning(push)
#pragma warning(disable: 26495)
#pragma warning(disable: 26498)
#pragma warning(disable: 26451)
#endif

#include <bgfx/bgfx.h>
#include <bgfx/src/version.h>
#include <bx/allocator.h>

struct ImDrawData;
typedef struct GLFWwindow GLFWwindow;

/*! Initialize the BGFX library 
 * 
 *  @param window The GLFW main window to use for rendering
 */
void bgfx_initialize(GLFWwindow* window);

/*! Shutdown the BGFX library */
void bgfx_shutdown();

/*! Initialize a view for the IMGUI rendering */
void bgfx_init_view(int imgui_view);

/*! Render a new frame 
 * 
 *  @param window The GLFW main window to use for rendering
 *  @param width  The width of the window
 *  @param height The height of the window
 */
void bgfx_new_frame(GLFWwindow* window, int width, int height);

/*! Render the IMGUI draw lists 
 * 
 *  @param draw_data The IMGUI draw data
 *  @param fb_width  The width of the framebuffer
 *  @param fb_height The height of the framebuffer
 */
void bgfx_render_draw_lists(ImDrawData* draw_data, int frame_width, int frame_height);

/*! Get the framework custom BGFX allocator */
bx::AllocatorI* bgfx_system_allocator();

/*! Get the framework custom BGFX callback handler to log traces using #log_infof */
bgfx::CallbackI* bgfx_system_callback_handler();

#if FOUNDATION_COMPILER_MSVC
#pragma warning(pop)
#endif

#endif // BUILD_APPLICATION
