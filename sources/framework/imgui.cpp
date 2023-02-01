/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "imgui.h"

#include "common.h"
#include "session.h"

static float _global_font_scaling = 0.0f;

namespace ImGui
{
    inline void AddUnderLine(ImColor col_)
    {
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        min.y = max.y;
        ImGui::GetWindowDrawList()->AddLine(min, max, col_, 1.0f);
    }

    void TextURL(const char* name, const char* name_end, const char* URL, size_t URL_length, uint8_t SameLineBefore_ /*= 0*/, uint8_t SameLineAfter_ /*= 0*/)
    {
        if (1 == SameLineBefore_) { ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x); }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
        ImGui::TextUnformatted(name, name_end);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
        {
            if (ImGui::IsMouseClicked(0))
                open_in_shell(URL);
            ImGui::AddUnderLine(ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
            ImGui::SetTooltip(/*ICON_FA_LINK*/ "  %.*s", (int)URL_length, URL);
        }
        else
        {
            AddUnderLine(ImGui::GetStyle().Colors[ImGuiCol_Button]);
        }
        if (1 == SameLineAfter_) { ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x); }
    }

    void PushStyleCompact()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, (float)(int)(style.FramePadding.y * 0.60f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, (float)(int)(style.ItemSpacing.y * 0.60f)));
    }

    void PopStyleCompact()
    {
        ImGui::PopStyleVar(2);
    }

    void PushStyleTight()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    }

    void PopStyleTight()
    {
        ImGui::PopStyleVar(6);
    }
}

bool shortcut_executed(bool ctrl, bool alt, bool shift, bool super, int key)
{
    ImGuiIO& io = ImGui::GetIO();

    if (io.WantTextInput)
        return false;

    ImGuiKeyData* key_data = ImGui::GetKeyData((ImGuiKey)key);
    return ((!ctrl || io.KeyCtrl) && (!alt || io.KeyAlt) && (!shift || io.KeyShift) && (!super || io.KeySuper) &&
        io.KeysDown[key] && key_data->DownDuration == 0.0f);
}

ImVec4 imgui_color_highlight(ImVec4 c, float intensity)
{
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(c.x, c.y, c.z, h, s, v);
    v += intensity;
    ImGui::ColorConvertHSVtoRGB(h, s, v, c.x, c.y, c.z);
    return c;
}

ImColor imgui_color_text_for_background(const ImColor& bg)
{
    if ((bg.Value.x * 0.299f + bg.Value.y * 0.587f + bg.Value.z * 0.114f) * 255.0f * bg.Value.w > 116.0f)
        return TEXT_COLOR_DARK;
    return TEXT_COLOR_LIGHT;
}

FOUNDATION_STATIC void imgui_draw_frame(const char* id, const ImVec2& view_size, const imgui_frame_render_callback_t& render_callback, ImGuiWindowFlags frame_flags)
{
    ImVec2 screen_position = ImGui::GetCursorScreenPos();
    if (ImGui::BeginChild(id, view_size, true, frame_flags))
    {
        const ImRect view_rect = ImRect(screen_position, ImVec2(screen_position.x + view_size.y, screen_position.x + view_size.y));
        render_callback(view_rect);
    }
    ImGui::EndChild();
}

bool imgui_draw_splitter(const char* id,
    const imgui_frame_render_callback_t& left_callback,
    const imgui_frame_render_callback_t& right_callback,
    ImGuiSplitterOrientation orientation,
    ImGuiWindowFlags frame_flags,
    float initial_propertion,
    bool preserve_proportion)
{
    char splitter_session_key[256];
    ImVec2 space = ImGui::GetContentRegionAvail();
    const float space_left = (orientation == IMGUI_SPLITTER_HORIZONTAL ? space.x : space.y);

    string_format(STRING_CONST_CAPACITY(splitter_session_key), STRING_CONST("%s_splitter_pos"), id);
    float splitter_pos = session_get_float(splitter_session_key, initial_propertion > 0.0f ? space_left * initial_propertion : space_left / 2.0f);
    if (!imgui_draw_splitter(id, &splitter_pos, left_callback, right_callback, orientation, frame_flags, preserve_proportion))
        return false;

    session_set_float(splitter_session_key, splitter_pos);
    return true;
}

bool imgui_draw_splitter(const char* id, float* splitter_pos,
    const imgui_frame_render_callback_t& left_callback,
    const imgui_frame_render_callback_t& right_callback,
    ImGuiSplitterOrientation orientation,
    ImGuiWindowFlags frame_flags,
    bool preserve_proportion)
{
    FOUNDATION_ASSERT(splitter_pos);
    FOUNDATION_ASSERT(left_callback);

    const ImVec2 space = ImGui::GetContentRegionAvail();
    ImVec2 screen_position = ImGui::GetCursorScreenPos();

    if (!right_callback)
    {		
        if (ImGui::BeginChild(id, space, true, frame_flags))
        {
            const ImRect view_rect = ImRect(screen_position, ImVec2(screen_position.x + space.x, screen_position.y + space.y));
            left_callback(view_rect);
        }
        ImGui::EndChild();
        return false;
    }

    bool updated = false;
    const bool hv = orientation == IMGUI_SPLITTER_HORIZONTAL;
    const float space_left = hv ? space.x : space.y;
    const float space_expand = hv ? space.y : space.x;

    if (preserve_proportion)
    {
        static ImVec2 region_sizes[32]{};
        const unsigned int region_hash_index = ImGui::GetID(id) % ARRAY_COUNT(region_sizes);
        ImVec2& stored_region_size = region_sizes[region_hash_index];
        const ImVec2 current_region_size = ImGui::GetContentRegionAvail();
        if (stored_region_size.x != current_region_size.x || stored_region_size.y != current_region_size.y)
        {
            if (stored_region_size.x != 0.0f && stored_region_size.y != 0.0f)
            {
                // Window size has change, preserve proportion
                const float psz = (hv ? current_region_size.x / stored_region_size.x : current_region_size.y / stored_region_size.y);
                *splitter_pos *= psz;
            }

            region_sizes[region_hash_index] = current_region_size;
        }
    }

    const float min_pixel_size = imgui_get_font_ui_scale(55.0f);
    *splitter_pos = max(max(space_left * 0.05f, min_pixel_size), *splitter_pos);
    *splitter_pos = min(*splitter_pos, min(space_left * 0.95f, space_left - min_pixel_size));

    // Render left or top view
    const char* view_left_id = string_format_static_const("ViewLeft###%s_1", id);
    const ImVec2 left_view_size = ImVec2(hv ? *splitter_pos : space_expand, hv ? space_expand : *splitter_pos);
    if (ImGui::BeginChild(view_left_id, left_view_size, true, frame_flags))
    {
        const ImRect view_rect = ImRect(screen_position,
            ImVec2(screen_position.x + left_view_size.x, screen_position.y + left_view_size.y));
        left_callback(view_rect);
    }
    ImGui::EndChild();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

    // Render splitter handle
    if (hv)
        ImGui::SameLine();
    ImGui::InvisibleButton("MainViewHorizontalSplitter", ImVec2(hv ? 8.0f : -1.0f, hv ? -1.0f : 8.0f));
    const bool handle_active = ImGui::IsItemActive();
    const bool handle_hovered = ImGui::IsItemHovered();
    if (handle_active || handle_hovered)
        ImGui::SetMouseCursor(hv ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
    if (handle_active)
    {
        float delta = hv ? ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.y;
        *splitter_pos += delta;
        updated = !math_float_is_zero(delta);
    }

    // Render right or bottom view
    if (hv)
        ImGui::SameLine();

    ImGui::PopStyleVar();
        
    screen_position = ImGui::GetCursorScreenPos();
    const char* view_right_id = string_format_static_const("ViewRight###%s_2", id);
    const ImVec2 right_view_size = ImGui::GetContentRegionAvail();
    if (ImGui::BeginChild(view_right_id, right_view_size, true, frame_flags))
    {
        const ImRect view_rect = ImRect(screen_position,
            ImVec2(screen_position.x + right_view_size.x, screen_position.y + right_view_size.y));
        right_callback(view_rect);
    }
    ImGui::EndChild();

    return updated;
}

ImRect imgui_draw_rect(const ImVec2& offset, const ImVec2& size, const ImColor& border_color /*= 0U*/, const ImColor& background_color /*= 0U*/)
{
    if (offset.x != 0 && offset.y != 0)
        ImGui::MoveCursor(offset.x, offset.y, true);
    else
        ImGui::SameLine();
    ImGui::Dummy(size);
    ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    if (border_color != 0)
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        ImRect border_rect = rect;
        border_rect.Expand(1.0f);
        draw_list->AddRect(border_rect.Min, border_rect.Max, border_color);
    }

    if (background_color != 0)
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(rect.Min, rect.Max, background_color);
    }

    return rect;
}

bool imgui_right_aligned_button(const char* label, bool same_line /*= false*/, float in_space_left /*= -1.0f*/)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float space_left = (in_space_left > 0.0f ? in_space_left : ImGui::GetContentRegionAvail().x);
    const float button_width = ImGui::CalcTextSize(label, nullptr, true).x + style.FramePadding.x * 2.0f;
    
    if (!same_line)
        ImGui::Dummy(ImVec2(0,0));
    ImGui::SameLine(space_left - button_width, 0.0f);
    return ImGui::Button(label, ImVec2(button_width, 0.0f));
}

void imgui_right_aligned_label(const char* label, bool same_line /*= false*/)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float space_left = ImGui::GetContentRegionAvail().x;
    const float label_width = ImGui::CalcTextSize(label, nullptr, true).x;

    if (!same_line)
        ImGui::Dummy(ImVec2(0, 0));
    ImGui::SameLine(space_left - label_width /*+ style.FramePadding.x * 2.0f*/, 0.0f);
    ImGui::AlignTextToFramePadding();
    return ImGui::TextUnformatted(label);
}

void imgui_centered_aligned_label(const char* label, bool same_line /*= false*/)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    const float space_left = ImGui::GetContentRegionAvail().x;
    const float label_width = ImGui::CalcTextSize(label, nullptr, true).x;

    auto sx = ImGui::GetCursorPosX();
    ImVec2 cpos = ImGui::GetCursorPos();
    cpos.x += (space_left - label_width) / 2.0f;
    if (cpos.x > sx)
        ImGui::SetCursorPosX(cpos.x);
    ImGui::AlignTextToFramePadding();
    return ImGui::TextUnformatted(label);
}

float imgui_get_font_ui_scale(float value /*= 1.0f*/)
{
    if (math_float_is_zero(_global_font_scaling))
        _global_font_scaling = session_get_float("font_scaling", 1.0f);
    return _global_font_scaling * value;
}

void imgui_set_font_ui_scale(float scale)
{
    FOUNDATION_ASSERT(scale <= 0.1f);
    _global_font_scaling = scale;
    session_set_float("font_scaling", _global_font_scaling);
}

ImRect imgui_get_available_rect()
{
    const ImVec2 space = ImGui::GetContentRegionAvail();
    const ImVec2 screen_position = ImGui::GetCursorScreenPos();
    return ImRect(screen_position, ImVec2(screen_position.x + space.x, screen_position.y + space.y));
}
