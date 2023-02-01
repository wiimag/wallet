/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include <foundation/platform.h>

void progress_initialize();

void progress_stop();

void progress_finalize();

void progress_set(size_t current, size_t total);
