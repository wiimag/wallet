/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#pragma once

#include "framework/common.h"

typedef function<bool()> app_dialog_handler_t;

void app_open_dialog(const char* title, app_dialog_handler_t&& handler, uint32_t width = 0, uint32_t height = 0, bool can_resize = true);

void app_render_dialogs();
