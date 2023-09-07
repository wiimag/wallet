/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2023 Wiimag Inc. All rights reserved.
 *
 * The script modules allow the user to create expression scripts that are easily executed from the UI.
 */

#pragma once

#include <framework/config.h>

typedef enum {

    SCRIPT_UNDEFINED,
    SCRIPT_EXPRESSION

} script_point_type_t;

struct script_t
{;
    char name[256]{0};
    char text[64 * 1024]{0};

    time_t last_modified{ 0 };
    time_t last_executed{ 0 };
    bool show_console{ false };
    bool function_library{ false };
    hash_t function_registered{ 0 };

    bool is_new{ false };
};
