/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/common.h>
#include <framework/function.h>

 /*! Window user handle. */
typedef object_t window_handle_t;

/*! Window application callback. */
typedef function<void(window_handle_t)> window_handler_t;

typedef enum class WindowFlags {
    None = 0,
    
} window_flags_t;
DEFINE_ENUM_FLAGS(WindowFlags);

/*! Create and open a new window.
 *
 *  @param title           The title of the window. The title string gets copied into managed memory.
 *  @param render_callback The callback to be called when the window is rendered.
 *  @param flags           The window flags used to create and show the window.
 * 
 *  @return              The handle of the window.
 */
window_handle_t window_open(const char* title, const window_handler_t& render_callback, window_flags_t flags = WindowFlags::None);

/*! Returns the title string of the window.
 *
 *  @param window The handle of the window.
 *
 *  @return       The title string of the window.
 */
const char* window_title(window_handle_t window);
