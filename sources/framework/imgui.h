/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include "function.h"

#include <foundation/platform.h>

#undef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_date_chooser.h>
#include <imgui/implot.h>

enum ImGuiSplitterOrientation
{
    IMGUI_SPLITTER_VERTICAL,
    IMGUI_SPLITTER_HORIZONTAL,
};

typedef function<void(const ImRect& rect)> imgui_frame_render_callback_t;

static const ImU32 TEXT_GOOD_COLOR = ImColor::HSV(140 / 360.0f, 0.83f, 0.95f);
static const ImU32 TEXT_WARN_COLOR = ImColor::HSV(65 / 360.0f, 0.50f, 0.98f);
static const ImU32 TEXT_WARN2_COLOR = ImColor::HSV(5 / 360.0f, 0.55f, 0.95f); //hsv(0, 100%, 85%)
static const ImU32 TEXT_BAD_COLOR = ImColor::HSV(355 / 360.0f, 0.85f, 0.95f);
static const ImU32 TEXT_COLOR_LIGHT = ImColor::HSV(0 / 360.0f, 0.00f, 1.00f);
static const ImU32 TEXT_COLOR_DARK = ImColor::HSV(0 / 360.0f, 0.00f, 0.00f);
static const ImU32 TOOLTIP_TEXT_COLOR = ImColor::HSV(40 / 360.0f, 0.05f, 1.0f);
static const ImU32 BACKGROUND_CRITITAL_COLOR = ImColor::HSV(10 / 360.0f, 0.95f, 0.78f);
static const ImU32 BACKGROUND_SOLD_COLOR = ImColor::HSV(226 / 360.0f, 0.45f, 0.53f); //hsv(226, 25%, 53%)
static const ImU32 BACKGROUND_INDX_COLOR = ImColor::HSV(220 / 360.0f, 0.20f, 0.51f);
static const ImU32 BACKGROUND_LIGHT_TEXT_COLOR = ImColor::HSV(40 / 360.0f, 0.05f, 0.10f);
static const ImU32 BACKGROUND_DARK_TEXT_COLOR = ImColor::HSV(40 / 360.0f, 0.05f, 1.0f);
static const ImU32 BACKGROUND_HIGHLIGHT_COLOR = ImColor::HSV(227 / 360.0f, 0.20f, 0.51f);
static const ImU32 BACKGROUND_GOOD_COLOR = ImColor::HSV(100 / 360.0f, 0.99f, 0.70f); // hsv(99, 91%, 69%)
static const ImU32 BACKGROUND_WARN_COLOR = ImColor::HSV(13 / 360.0f, 0.89f, 0.51f); // hsv(13, 89%, 51%)
static const ImU32 BACKGROUND_BAD_COLOR = ImColor::HSV(358 / 360.0f, 0.99f, 0.70f); // hsv(358, 91%, 69%)

bool shortcut_executed(bool ctrl, bool alt, bool shift, bool super, int key);

FOUNDATION_FORCEINLINE bool shortcut_executed(bool ctrl, bool alt, int key) { return shortcut_executed(ctrl, alt, false, false, key); }
FOUNDATION_FORCEINLINE bool shortcut_executed(bool ctrl, int key) { return shortcut_executed(ctrl, false, false, false, key); }
FOUNDATION_FORCEINLINE bool shortcut_executed(int key) { return shortcut_executed(false, false, false, false, key); }

ImVec4 imgui_color_highlight(ImVec4 c, float intensity);

ImColor imgui_color_text_for_background(const ImColor& bg);
ImColor imgui_color_contrast_background(const ImColor& color);

bool imgui_draw_splitter(const char* id, float* splitter_pos,
    const imgui_frame_render_callback_t& left_callback,
    const imgui_frame_render_callback_t& right_callback,
    ImGuiSplitterOrientation orientation,
    ImGuiWindowFlags frame_flags = ImGuiWindowFlags_None,
    bool preserve_proportion = false);

bool imgui_draw_splitter(const char* id,
    const imgui_frame_render_callback_t& left_callback,
    const imgui_frame_render_callback_t& right_callback,
    ImGuiSplitterOrientation orientation,
    ImGuiWindowFlags frame_flags = ImGuiWindowFlags_None,
    float initial_propertion = 0.0f,
    bool preserve_proportion = false);

ImRect imgui_draw_rect(const ImVec2& offset, const ImVec2& size, 
    const ImColor& border_color = 0U, const ImColor& background_color = 0U);

bool imgui_right_aligned_button(const char* label, bool same_line = false, float in_space_left = -1.0f);

void imgui_right_aligned_label(const char* label, bool same_line = false);

void imgui_centered_aligned_label(const char* label, bool same_line = false);

float imgui_get_font_ui_scale(float value = 1.0f);

void imgui_set_font_ui_scale(float scale);

ImRect imgui_get_available_rect();

namespace ImGui 
{

    FOUNDATION_FORCEINLINE ImVec2 MoveCursor(float x, float y, bool same_line = false)
    {
        if (same_line)
            ImGui::SameLine();
        auto cpos = ImGui::GetCursorPos();
        cpos.x += x;
        cpos.y += y;
        ImGui::SetCursorPos(cpos);
        return cpos;
    }

    FOUNDATION_FORCEINLINE bool ButtonRightAligned(const char* label, bool same_line = false, float in_space_left = -1.0f)
    {
        return imgui_right_aligned_button(label, same_line, in_space_left);
    }

    bool TextURL(const char* name, const char* name_end, const char* URL, size_t URL_length, uint8_t SameLineBefore_ = 0, uint8_t SameLineAfter_ = 0);

    FOUNDATION_FORCEINLINE void TextUnformatted(string_const_t text, bool same_line = false)
    {
        if (text.length == 0)
            return;
        if (same_line)
            ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        TextUnformatted(text.str, text.str + text.length);
    }

    // Make the UI compact because there are so many fields
    void PushStyleCompact();
    void PopStyleCompact();
    void PushStyleTight();
    void PopStyleTight();
}
