/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/imgui.h>

struct logo_image_t;

/// <summary>
/// Render a symbol logo using IMGUI.
/// </summary>
/// <returns>Returns true if the logo was rendered.</returns>
bool logo_render(const char* symbol, size_t symbol_length, const ImVec2& size = ImVec2(0, 0), bool background = false);

bool logo_is_banner(
    const char* symbol, size_t symbol_length, 
    int& banner_width, int& banner_height, int& banner_channels, 
    ImU32& image_bg_color, ImU32& fill_color);

namespace ImGui {
    FOUNDATION_FORCEINLINE bool Logo(const char* symbol, size_t symbol_length, const ImVec2& size = ImVec2(0, 0), bool background = false)
    {
        return logo_render(symbol, symbol_length, size, background);
    }
 }
 