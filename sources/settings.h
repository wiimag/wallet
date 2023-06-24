/*
 * Copyright 2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */
 
#pragma once

#include <foundation/string.h>

struct settings_t
{
    // UI settings
    int current_tab{ 0 };

    // Preferences
    bool show_logo_banners{ true };

    char search_terms[256]{ '\0' };
    char search_filter[256]{ '\0' };

    double good_dividends_ratio{ 0.04 };
    char preferred_currency[32] { '\0' };
};

void settings_draw();

void settings_initialize();
void settings_shutdown();

extern settings_t SETTINGS;
