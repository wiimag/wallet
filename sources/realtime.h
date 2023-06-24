/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */
 
#pragma once

#include <foundation/time.h>

bool realtime_render_graph(const char* code, size_t code_length, time_t since = 0, float width = -1.0f, float height = -1.0f);
