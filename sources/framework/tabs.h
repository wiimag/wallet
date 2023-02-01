/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include "function.h"

#include <framework/imgui.h>

static const ImVec4 TAB_COLOR_APP(0.3f, 0.3f, 0.3f, 1.0f);
static const ImVec4 TAB_COLOR_APP_3D(0.6f, 0.25f, 0.25f, 1.0f);
static const ImVec4 TAB_COLOR_CASES(0.3f, 0.4f, 0.4f, 1.0f);
static const ImVec4 TAB_COLOR_OTHER(0.4f, 0.3f, 0.3f, 1.0f);
static const ImVec4 TAB_COLOR_SETTINGS(0.35f, 0.35f, 0.35f, 1.0f);

bool tabs_begin(
    const char* FOUNDATION_RESTRICT tab_bar_name, 
    int& active_tab, 
    ImGuiTabBarFlags flags = ImGuiTabBarFlags_None,
    const function<void()>& tools_callback = nullptr);

void tabs_end();

void tab_draw(
    const char* label,
    bool* opened,
    ImGuiTabItemFlags tab_flags,
    const function<void(void)>& render_tab_callback,
    const function<void(void)>& tab_tools_callback = nullptr);

FOUNDATION_FORCEINLINE void tab_draw(
    const char* label,
    bool* opened,
    const function<void(void)>& render_tab_callback,
    const function<void(void)>& tab_tools_callback = nullptr)
{
    return tab_draw(label, opened, ImGuiTabItemFlags_None, render_tab_callback, tab_tools_callback);
}

void tab_set_color(const ImVec4& c);

void tab_pop_color();

void tabs_shutdown();
