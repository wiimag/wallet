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
{
    char name[256]{0};
    char text[64 * 1024]{0};

    time_t last_modified{ 0 };
    time_t last_executed{ 0 };
    bool show_console{ false };
    bool function_library{ false };
    hash_t function_registered{ 0 };

    bool is_new{ false };
};

/*! Render the IMGUI menu items for the scripts module that can be executed for a pattern */
void scripts_render_pattern_menu_items();

/*! Register new script library function */
bool scripts_register_function(const char* name, size_t name_length, const char* expression_code, size_t expression_code_length);

/*! Unregister script library function */
bool scripts_unregister_function(const char* name, size_t name_length);
