/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/function.h>

 /*! Window user handle. */
typedef object_t window_handle_t;

/*! Window application callback. */
typedef function<void(window_handle_t)> window_handler_t;

/*! Create and open a new window.
 *
 *  @param title           The title of the window. The title string gets copied into managed memory.
 *  @param render_callback The callback to be called when the window is rendered.
 * 
 *  @return              The handle of the window.
 */
window_handle_t window_open(const char* title, const window_handler_t& render_callback);

/*! Returns the title string of the window.
 *
 *  @param window The handle of the window.
 *
 *  @return       The title string of the window.
 */
const char* window_title(window_handle_t window);
