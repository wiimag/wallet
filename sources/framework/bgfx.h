/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <bgfx/bgfx.h>
#include <bx/allocator.h>

struct ImDrawData;
typedef struct GLFWwindow GLFWwindow;

void bgfx_initialize(GLFWwindow* window);

void bgfx_shutdown();

void bgfx_init_view(int imgui_view);

void bgfx_new_frame(GLFWwindow* window, int width, int height);

void bgfx_render_draw_lists(ImDrawData* draw_data, int fb_width, int fb_height);

bx::AllocatorI* bgfx_system_allocator();

bgfx::CallbackI* bgfx_system_callback_handler();
