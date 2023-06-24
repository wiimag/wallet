/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include <framework/imgui.h>

/*! Initialize and register plot expression functions */
void plot_expr_initialize();

/*! Shutdown and unregister plot expression functions */
void plot_expr_shutdown();
