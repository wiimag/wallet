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

/*! Indicates that a set of tabs will be drawn until #tabs_end is invoked. 
 * 
 *  @param tab_bar_name The name of the tab bar.
 *  @param active_tab The index of the active tab.
 *  @param flags The IMGUI tab bar flags (@ref ImGuiTabBarFlags).
 *  @param tools_callback The callback to render the tab tools.
 * 
 *  @return True if the tab bar is visible, false otherwise.
 */
bool tabs_begin(
    const char* FOUNDATION_RESTRICT tab_bar_name, 
    int& active_tab, 
    ImGuiTabBarFlags flags = ImGuiTabBarFlags_None,
    const function<void()>& tools_callback = nullptr);

/*! Ends the tab drawing sequence. 
 * 
 *  This call must be invoked after a call to #tabs_begin.
 */
void tabs_end();

/*! Draw all the tabs without any customization.
 * 
 *  This function will draw all tabs registered with #service_register_tabs one after the other.
 *  If you need to customize the tabs, use #tabs_begin and #tabs_end instead.
 */
void tabs_draw_all();

/*! Draw a tab with the given label, callback and a set of IMGUI tab item flags. 
 * 
 *  @param label The label of the tab.
 *  @param opened A pointer to a boolean that indicates if the tab is opened or not.
 *  @param tab_flags The IMGUI tab item flags (@ref ImGuiTabItemFlags).
 *  @param render_tab_callback The callback to render the tab content.
 *  @param tab_tools_callback The callback to render the tab tools.
 */
void tab_draw(
    const char* label,
    bool* opened,
    ImGuiTabItemFlags tab_flags,
    const function<void(void)>& render_tab_callback,
    const function<void(void)>& tab_tools_callback = nullptr);

/*! Draw a tab with the given label and callback. 
 * 
 *  @param label The label of the tab.
 *  @param opened A pointer to a boolean that indicates if the tab is opened or not.
 *  @param render_tab_callback The callback to render the tab content.
 *  @param tab_tools_callback The callback to render the tab tools.
 */
FOUNDATION_FORCEINLINE void tab_draw(
    const char* label,
    bool* opened,
    const function<void(void)>& render_tab_callback,
    const function<void(void)>& tab_tools_callback = nullptr)
{
    return tab_draw(label, opened, ImGuiTabItemFlags_None, render_tab_callback, tab_tools_callback);
}

/*! Set the color of the next tabs to be drawn. 
 * 
 *  This is useful to set the color of the tabs in the main menu bar.
 *  The color is set between the pair #tabs_begin and #tabs_end.
 * 
 *  @param c The color to set.
 */
void tab_set_color(const ImVec4& c);

/*! Pop the color set by #tab_set_color between the pair #tabs_begin and #tabs_end. */
void tab_pop_color();

/*! Release any resources allocated by the tabs system. */
void tabs_shutdown();
