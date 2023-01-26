/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#pragma once

#include "framework/common.h"

typedef function<bool(void*)> app_dialog_handler_t;
typedef function<void(void*)> app_dialog_close_handler_t;

/// <summary>
/// Returns the application title.
/// </summary>
const char* app_title();

/// <summary>
/// Creates and open a dialog window.
/// </summary>
void app_open_dialog(
    const char* title, 
    app_dialog_handler_t&& handler, 
    uint32_t width = 0, uint32_t height = 0, bool can_resize = true, 
    app_dialog_close_handler_t&& close_handler = nullptr, void* user_data = nullptr);
