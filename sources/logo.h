/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#if BUILD_APPLICATION

#include <framework/imgui.h>

struct logo_t;

/*! Render a squared icon of the symbol. 
 * 
 *  @param symbol               The symbol to render.
 *  @param symbol_length        The length of the symbol.
 *  @param size                 The size of the logo.
 *  @param background           If true, the logo will be rendered with a background.
 *  @param show_tooltip         If true, the symbol will be shown as a tooltip.
 *  @param fill_rect            If not null, the logo will be rendered inside this rectangle.
 * 
 *  @returns Returns true if the logo was rendered.
 */
bool logo_render_icon(const char* symbol, size_t symbol_length, ImVec2& size, bool background = false, bool show_tooltip = true, ImRect* fill_rect = nullptr);

/*! Render a symbol logo using IMGUI.
 * 
 *  @param symbol               The symbol to render.
 *  @param symbol_length        The length of the symbol.
 *  @param size                 The size of the logo.
 *  @param background           If true, the logo will be rendered with a background.
 *  @param show_tooltip         If true, the symbol will be shown as a tooltip.
 *  @param fill_rect            If not null, the logo will be rendered inside this rectangle.
 * 
 *  @returns Returns true if the logo was rendered.
 */
bool logo_render_banner(const char* symbol, size_t symbol_length, ImVec2& size, bool background = false, bool show_tooltip = true, ImRect* fill_rect = nullptr);

/*! Render a symbol logo banner using IMGUI.
 * 
 *  @param symbol               The symbol to render.
 *  @param symbol_length        The length of the symbol.
 *  @param size                 The size of the logo.
 *  @param background           If true, the logo will be rendered with a background.
 *  @param show_tooltip         If true, the symbol will be shown as a tooltip.
 *  @param fill_rect            If not null, the logo will be rendered inside this rectangle.
 *  @param suggested_text_color If not null, the logo will be rendered with this color.
 *  
 *  @returns Returns true if the logo was rendered.
 */
bool logo_render_banner(const char* symbol, size_t symbol_length, const ImRect& rect, ImU32* suggested_text_color = nullptr);

/*! Check if logo has a banner version.
 *
 *  @param symbol           The symbol to render.
 *  @param symbol_length    The length of the symbol.
 *  @param banner_width     The width of the banner.
 *  @param banner_height    The height of the banner.
 *  @param banner_channels  The number of channels of the banner.
 *  @param image_bg_color   The background color of the banner.
 *  @param fill_color       The fill color of the banner.
 *
 *  @returns Returns true if the logo has a banner version.
 */
bool logo_has_banner(
    const char* symbol, size_t symbol_length, 
    int& banner_width, int& banner_height, int& banner_channels, 
    ImU32& image_bg_color, ImU32& fill_color);
 
#endif // BUILD_APPLICATION
