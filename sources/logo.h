/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/imgui.h>

/// <summary>
/// Render a symbol logo using IMGUI.
/// </summary>
/// <returns>Returns true if the logo was clicked.</returns>
bool logo_render(const char* symbol, size_t symbol_length, const ImVec2& size = ImVec2(0, 0));

namespace ImGui {
    FOUNDATION_FORCEINLINE bool Logo(const char* symbol, size_t symbol_length, const ImVec2& size = ImVec2(0, 0))
    {
        return logo_render(symbol, symbol_length, size);
    }
 }
 