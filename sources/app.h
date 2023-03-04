/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include "framework/common.h"

typedef function<bool(void*)> app_dialog_handler_t;
typedef function<void(void*)> app_dialog_close_handler_t;

/*! Returns the application title. */
const char* app_title();

/*! Creates and open a dialog window.
 *
 *  @param title         The title of the dialog. The title string gets copied into managed memory.
 *  @param handler       The handler to be called when the dialog is opened.
 *  @param width         The width of the dialog.
 *  @param height        The height of the dialog.
 *  @param can_resize    Whether the dialog can be resized by the user.
 *  @param user_data     The user data to be passed to the handlers.
 *  @param close_handler The handler to be called when the dialog is closed.
 */
void app_open_dialog(
    const char* title, 
    app_dialog_handler_t&& handler, 
    uint32_t width = 0, uint32_t height = 0, bool can_resize = true, 
    void* user_data = nullptr, app_dialog_close_handler_t&& close_handler = nullptr);
