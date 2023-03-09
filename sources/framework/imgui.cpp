/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "imgui.h"

#include <framework/glfw.h>

#include <framework/common.h>
#include <framework/string.h>
#include <framework/session.h>
#include <framework/profiler.h>
#include <framework/dispatcher.h>

#include <foundation/memory.h>

#if FOUNDATION_PLATFORM_WINDOWS

    #include <resource.h>
    #include <foundation/windows.h>
    
#elif FOUNDATION_PLATFORM_MACOS

    #include <foundation/apple.h>

#endif

#include <GLFW/glfw3native.h>

#define HASH_IMGUI static_hash_string("imgui", 5, 0x9803803300f77bbfULL)

static double _time = 0.0;
static float _global_font_scaling = 0.0f;
static GLFWcursor* _mouse_cursors[ImGuiMouseCursor_COUNT] = { nullptr };

namespace ImGui
{
    inline void AddUnderLine(ImColor col_)
    {
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        min.y = max.y;
        ImGui::GetWindowDrawList()->AddLine(min, max, col_, 1.0f);
    }

    bool TextURL(const char* name, const char* name_end, const char* URL, size_t URL_length, uint8_t SameLineBefore_ /*= 0*/, uint8_t SameLineAfter_ /*= 0*/)
    {
        if (1 == SameLineBefore_) { ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x); }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
        ImGui::TextUnformatted(name, name_end);
        ImGui::PopStyleColor();
        bool clicked = false;
        if (ImGui::IsItemHovered())
        {
            if (ImGui::IsMouseClicked(0))
            {
                open_in_shell(URL);
                clicked = true;
            }
            ImGui::AddUnderLine(ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
            ImGui::SetTooltip(/*ICON_FA_LINK*/ "  %.*s", (int)URL_length, URL);
        }
        else
        {
            AddUnderLine(ImGui::GetStyle().Colors[ImGuiCol_Button]);
        }
        if (1 == SameLineAfter_) { ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x); }

        return clicked;
    }

    void PushStyleCompact()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, (float)(int)(style.FramePadding.y * 0.60f)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, (float)(int)(style.ItemSpacing.y * 0.60f)));
        ImGui::BeginGroup();
    }

    void PopStyleCompact()
    {
        ImGui::EndGroup();
        ImGui::PopStyleVar(2);
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

ImColor imgui_color_contrast_background(const ImColor& color)
{
    float h, s, v;
    ImGui::ColorConvertRGBtoHSV(color.Value.x, color.Value.y, color.Value.z, h, s, v);

    bool mr = color.Value.x > 0.9f;
    bool mg = color.Value.y > 0.95f;
    bool mb = color.Value.z > 0.90f;

    return ImColor(0.9f + (mr ? 0.1f : 0.0f), 0.9f + (mg ? 0.1f : 0.0f), 0.9f + (mb ? 0.1f : 0.0f));
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
    ImVec2 space = ImGui::GetContentRegionAvail();
    if (space.x <= 0 || space.y <= 0)
        return false;

    char splitter_session_key[256];
    const float space_left = (orientation == IMGUI_SPLITTER_HORIZONTAL ? space.x : space.y);
    string_format(STRING_BUFFER(splitter_session_key), STRING_CONST("%s_splitter_pos"), id);
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
        const ImRect view_rect = ImRect(screen_position, ImVec2(screen_position.x + space.x, screen_position.y + space.y));
        left_callback(view_rect);
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

    static const float min_pixel_size = ImGui::CalcTextSize("W").y + ImGui::GetStyle().WindowPadding.y * 2.0f;
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
    return _global_font_scaling * value * ImGui::GetIO().FontGlobalScale;
}

void imgui_set_font_ui_scale(float scale)
{
    ImGui::GetIO().FontGlobalScale = 1.0f + (scale - _global_font_scaling);
    session_set_float("font_scaling", scale);
}

ImRect imgui_get_available_rect()
{
    const ImVec2 space = ImGui::GetContentRegionAvail();
    const ImVec2 screen_position = ImGui::GetCursorScreenPos();
    return ImRect(screen_position, ImVec2(screen_position.x + space.x, screen_position.y + space.y));
}

FOUNDATION_STATIC void imgui_add_thin_space_glyph(ImFont* font, float size_pixels)
{
    ImGuiIO& io = ImGui::GetIO();

    int rect_id = io.Fonts->AddCustomRectFontGlyph(font, 0x2009, size_pixels / 7, size_pixels, size_pixels / 7); // "\xe2\x80\x89"

    // Build atlas
    io.Fonts->Build();

    // Retrieve texture in RGBA format
    int tex_width, tex_height;
    unsigned char* tex_pixels = NULL;
    io.Fonts->GetTexDataAsRGBA32(&tex_pixels, &tex_width, &tex_height);
    if (const ImFontAtlasCustomRect* rect = io.Fonts->GetCustomRectByIndex(rect_id))
    {
        for (int y = 0; y < rect->Height; y++)
        {
            ImU32* p = (ImU32*)tex_pixels + (rect->Y + y) * tex_width + (rect->X);
            for (int x = rect->Width; x > 0; x--)
                *p++ = IM_COL32(0, 0, 0, 0);
        }
    }
}

FOUNDATION_STATIC bool imgui_load_font(unsigned int font_res_id, const char* res_type, float size_pixels, const ImFontConfig* font_cfg = NULL, const ImWchar* glyph_ranges = NULL)
{
    ImGuiIO& io = ImGui::GetIO();
    #if FOUNDATION_PLATFORM_WINDOWS
        HMODULE hModule = GetModuleHandle(NULL);
        HRSRC hResource = FindResourceA(hModule, MAKEINTRESOURCEA(font_res_id), res_type);
        if (hResource)
        {
            HGLOBAL hMemory = LoadResource(hModule, hResource);
            DWORD dwSize = SizeofResource(hModule, hResource);
            LPVOID lpAddress = LockResource(hMemory);

            char* bytes = (char*)imgui_allocate(dwSize, nullptr);
            memcpy(bytes, lpAddress, dwSize);

            auto font = io.Fonts->AddFontFromMemoryTTF(
                bytes, dwSize,
                size_pixels,
                font_cfg,
                glyph_ranges
            );
            
            UnlockResource(hMemory);
            
            if (font)
            {
                imgui_add_thin_space_glyph(font, size_pixels);
                return true;
            }
        }
    #elif FOUNDATION_PLATFORM_MACOS

        FILE* file = fopen(res_type, "rb");
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        char* bytes = (char*)imgui_allocate(file_size, nullptr);
        fread(bytes, 1, file_size, file);
        fclose(file);
        
        // bytes will be owned by imgui
        auto font = io.Fonts->AddFontFromMemoryTTF(
            bytes, (int)file_size,
            size_pixels,
            font_cfg,
            glyph_ranges
        );
        
        if (font)
        {
            imgui_add_thin_space_glyph(font, size_pixels);
            return true;
        }
    #endif

    return false;
}

bool imgui_load_main_font(float xscale /*= 1.0f*/)
{
    #if FOUNDATION_PLATFORM_WINDOWS
        return imgui_load_font(IDR_MAIN_FONT, MAKEINTRESOURCEA(8), 16.0f * xscale);
    #elif FOUNDATION_PLATFORM_MACOS
        return imgui_load_font(0, "../Resources/JetBrainsMono-ExtraLight.ttf", 16.0f * xscale);
    #else
        #error not supported
        return false;
    #endif
}

bool imgui_load_material_design_font(float xscale /*= 1.0f*/)
{
    static const ImWchar icons_ranges[] = { ICON_MIN_MD, ICON_MAX_16_MD, 0 };
    ImFontConfig icons_config; icons_config.MergeMode = true; icons_config.PixelSnapH = true; icons_config.GlyphOffset.y += 4.0f;

    #if FOUNDATION_PLATFORM_WINDOWS
        return imgui_load_font(IDR_MD_FONT, MAKEINTRESOURCEA(10), 14.0f * xscale, &icons_config, icons_ranges);
    #elif FOUNDATION_PLATFORM_MACOS
        return imgui_load_font(0, "../Resources/MaterialIcons-Regular.ttf", 14.0f * xscale, &icons_config, icons_ranges);
    #else
        return false;
    #endif
}

FOUNDATION_STATIC void imgui_update_cursor_pos(GLFWwindow* window, double& x, double& y)
{
    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
        return;

    ImGuiIO& io = ImGui::GetIO();
    float xscale = 1.0f, yscale = 1.0f;
    #if FOUNDATION_PLATFORM_MACOS
        glfwGetMonitorContentScale(glfw_find_window_monitor(window), &xscale, &yscale);
    #endif
    x *= xscale; y *= yscale;
    io.AddMousePosEvent((float)x, (float)y);
}


FOUNDATION_STATIC void imgui_update_mouse_data(GLFWwindow* window)
{
    ImGuiIO& io = ImGui::GetIO();

    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
        return io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);

    // Setup inputs
    // (we already got mouse wheel, keyboard keys & characters from glfw callbacks polled in glfwPollEvents())
    if (glfw_is_window_focused(window))
    {
        // Set OS mouse position if requested (only used when ImGuiConfigFlags_NavEnableSetMousePos is enabled by user)
        if (io.WantSetMousePos)
        {
            glfwSetCursorPos(window, (double)io.MousePos.x, (double)io.MousePos.y);
        }
        else
        {
            double mouse_x, mouse_y;
            glfwGetCursorPos(window, &mouse_x, &mouse_y);
            imgui_update_cursor_pos(window, mouse_x, mouse_y);

            static double prev_mouse_x = 0, prev_mouse_y = 0;
            if (prev_mouse_x != mouse_x || prev_mouse_y != mouse_y)
            {
                prev_mouse_x = mouse_x;
                prev_mouse_y = mouse_y;
                signal_thread();
            }
        }
    }
}

void imgui_update_mouse_cursor(GLFWwindow* window)
{
    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) || glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
        return;

    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
    {
        // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }
    else
    {
        // Show OS mouse cursor
        // FIXME-PLATFORM: Unfocused windows seems to fail changing the mouse cursor with GLFW 3.2, but 3.3 works here.
        glfwSetCursor(window, _mouse_cursors[imgui_cursor] ? _mouse_cursors[imgui_cursor] : _mouse_cursors[ImGuiMouseCursor_Arrow]);
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void imgui_new_frame(GLFWwindow* window, int width, int height)
{
    PERFORMANCE_TRACKER("imgui_new_frame");

    ImGui::SetCurrentContext((ImGuiContext*)glfwGetWindowUserPointer(window));

    ImGuiIO& io = ImGui::GetIO();

    // We assume the framebuffer is always of the good size.
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DisplaySize = ImVec2((float)width, (float)height);

    // Setup time step
    const double current_time = glfwGetTime();
    io.DeltaTime = _time > 0.0 ? (float)(current_time - _time) : (float)(1.0f / 60.0f);
    _time = current_time;

    imgui_update_mouse_data(window);
    imgui_update_mouse_cursor(window);

    ImGui::NewFrame();
}

FOUNDATION_STATIC void imgui_destroy_cursors()
{
    // Destroy GLFW mouse cursors
    for (auto& cursor : _mouse_cursors)
        glfwDestroyCursor(cursor);
    memset(_mouse_cursors, 0, sizeof(_mouse_cursors));
}

FOUNDATION_STATIC void imgui_glfw_set_clipboard_text(void* user_data, const char* text)
{
    glfwSetClipboardString((GLFWwindow*)user_data, text);
}

FOUNDATION_STATIC const char* imgui_glfw_get_clipboard_text(void* user_data)
{
    return glfwGetClipboardString((GLFWwindow*)user_data);
}

FOUNDATION_STATIC void imgui_glfw_update_key_modifiers(int mods)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, (mods & GLFW_MOD_CONTROL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (mods & GLFW_MOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (mods & GLFW_MOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (mods & GLFW_MOD_SUPER) != 0);
}

FOUNDATION_STATIC void imgui_glfw_mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    ImGui::SetCurrentContext((ImGuiContext*)glfwGetWindowUserPointer(window));
    imgui_glfw_update_key_modifiers(mods);

    ImGuiIO& io = ImGui::GetIO();
    if (button >= 0 && button < ImGuiMouseButton_COUNT)
        io.AddMouseButtonEvent(button, action == GLFW_PRESS);

    signal_thread();
}

FOUNDATION_STATIC void imgui_glfw_scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    ImGui::SetCurrentContext((ImGuiContext*)glfwGetWindowUserPointer(window));
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent((float)xoffset, (float)yoffset);
    signal_thread();
}

ImGuiKey imgui_key_from_glfw_key(int key)
{
    switch (key)
    {
    case GLFW_KEY_TAB: return ImGuiKey_Tab;
    case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
    case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
    case GLFW_KEY_UP: return ImGuiKey_UpArrow;
    case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
    case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
    case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
    case GLFW_KEY_HOME: return ImGuiKey_Home;
    case GLFW_KEY_END: return ImGuiKey_End;
    case GLFW_KEY_INSERT: return ImGuiKey_Insert;
    case GLFW_KEY_DELETE: return ImGuiKey_Delete;
    case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
    case GLFW_KEY_SPACE: return ImGuiKey_Space;
    case GLFW_KEY_ENTER: return ImGuiKey_Enter;
    case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
    case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
    case GLFW_KEY_COMMA: return ImGuiKey_Comma;
    case GLFW_KEY_MINUS: return ImGuiKey_Minus;
    case GLFW_KEY_PERIOD: return ImGuiKey_Period;
    case GLFW_KEY_SLASH: return ImGuiKey_Slash;
    case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
    case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
    case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
    case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
    case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
    case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
    case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
    case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
    case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
    case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
    case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
    case GLFW_KEY_KP_0: return ImGuiKey_Keypad0;
    case GLFW_KEY_KP_1: return ImGuiKey_Keypad1;
    case GLFW_KEY_KP_2: return ImGuiKey_Keypad2;
    case GLFW_KEY_KP_3: return ImGuiKey_Keypad3;
    case GLFW_KEY_KP_4: return ImGuiKey_Keypad4;
    case GLFW_KEY_KP_5: return ImGuiKey_Keypad5;
    case GLFW_KEY_KP_6: return ImGuiKey_Keypad6;
    case GLFW_KEY_KP_7: return ImGuiKey_Keypad7;
    case GLFW_KEY_KP_8: return ImGuiKey_Keypad8;
    case GLFW_KEY_KP_9: return ImGuiKey_Keypad9;
    case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
    case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
    case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
    case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
    case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
    case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
    case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
    case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
    case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
    case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
    case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
    case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
    case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
    case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
    case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
    case GLFW_KEY_MENU: return ImGuiKey_Menu;
    case GLFW_KEY_0: return ImGuiKey_0;
    case GLFW_KEY_1: return ImGuiKey_1;
    case GLFW_KEY_2: return ImGuiKey_2;
    case GLFW_KEY_3: return ImGuiKey_3;
    case GLFW_KEY_4: return ImGuiKey_4;
    case GLFW_KEY_5: return ImGuiKey_5;
    case GLFW_KEY_6: return ImGuiKey_6;
    case GLFW_KEY_7: return ImGuiKey_7;
    case GLFW_KEY_8: return ImGuiKey_8;
    case GLFW_KEY_9: return ImGuiKey_9;
    case GLFW_KEY_A: return ImGuiKey_A;
    case GLFW_KEY_B: return ImGuiKey_B;
    case GLFW_KEY_C: return ImGuiKey_C;
    case GLFW_KEY_D: return ImGuiKey_D;
    case GLFW_KEY_E: return ImGuiKey_E;
    case GLFW_KEY_F: return ImGuiKey_F;
    case GLFW_KEY_G: return ImGuiKey_G;
    case GLFW_KEY_H: return ImGuiKey_H;
    case GLFW_KEY_I: return ImGuiKey_I;
    case GLFW_KEY_J: return ImGuiKey_J;
    case GLFW_KEY_K: return ImGuiKey_K;
    case GLFW_KEY_L: return ImGuiKey_L;
    case GLFW_KEY_M: return ImGuiKey_M;
    case GLFW_KEY_N: return ImGuiKey_N;
    case GLFW_KEY_O: return ImGuiKey_O;
    case GLFW_KEY_P: return ImGuiKey_P;
    case GLFW_KEY_Q: return ImGuiKey_Q;
    case GLFW_KEY_R: return ImGuiKey_R;
    case GLFW_KEY_S: return ImGuiKey_S;
    case GLFW_KEY_T: return ImGuiKey_T;
    case GLFW_KEY_U: return ImGuiKey_U;
    case GLFW_KEY_V: return ImGuiKey_V;
    case GLFW_KEY_W: return ImGuiKey_W;
    case GLFW_KEY_X: return ImGuiKey_X;
    case GLFW_KEY_Y: return ImGuiKey_Y;
    case GLFW_KEY_Z: return ImGuiKey_Z;
    case GLFW_KEY_F1: return ImGuiKey_F1;
    case GLFW_KEY_F2: return ImGuiKey_F2;
    case GLFW_KEY_F3: return ImGuiKey_F3;
    case GLFW_KEY_F4: return ImGuiKey_F4;
    case GLFW_KEY_F5: return ImGuiKey_F5;
    case GLFW_KEY_F6: return ImGuiKey_F6;
    case GLFW_KEY_F7: return ImGuiKey_F7;
    case GLFW_KEY_F8: return ImGuiKey_F8;
    case GLFW_KEY_F9: return ImGuiKey_F9;
    case GLFW_KEY_F10: return ImGuiKey_F10;
    case GLFW_KEY_F11: return ImGuiKey_F11;
    case GLFW_KEY_F12: return ImGuiKey_F12;

    default:
        return ImGuiKey_None;
    }
}

FOUNDATION_STATIC void imgui_glfw_key_callback(GLFWwindow* window, int keycode, int scancode, int action, int mods)
{
    signal_thread();

    if (keycode == -1)
        return;

    if (action != GLFW_PRESS && action != GLFW_RELEASE)
        return;

    ImGui::SetCurrentContext((ImGuiContext*)glfwGetWindowUserPointer(window));

    // Workaround: X11 does not include current pressed/released modifier key in 'mods' flags. https://github.com/glfw/glfw/issues/1630
    if (int keycode_to_mod = glfw_key_to_modifier(keycode))
        mods = (action == GLFW_PRESS) ? (mods | keycode_to_mod) : (mods & ~keycode_to_mod);
    imgui_glfw_update_key_modifiers(mods);

    keycode = glfw_translate_untranslated_key(keycode, scancode);

    ImGuiIO& io = ImGui::GetIO();
    ImGuiKey imgui_key = imgui_key_from_glfw_key(keycode);
    io.AddKeyEvent(imgui_key, (action == GLFW_PRESS));
    io.SetKeyEventNativeData(imgui_key, keycode, scancode); // To support legacy indexing (<1.87 user code)
}

FOUNDATION_STATIC void imgui_glfw_char_callback(GLFWwindow* window, unsigned int c)
{
    ImGui::SetCurrentContext((ImGuiContext*)glfwGetWindowUserPointer(window));
    ImGuiIO& io = ImGui::GetIO();
    if (c > 0 && c < 0x10000)
        io.AddInputCharacter((unsigned short)c);

    signal_thread();
}

FOUNDATION_STATIC void imgui_glfw_window_focus_callback(GLFWwindow* window, int focused)
{
    ImGui::SetCurrentContext((ImGuiContext*)glfwGetWindowUserPointer(window));
    ImGuiIO& io = ImGui::GetIO();
    io.AddFocusEvent(focused != 0);
    signal_thread();
}

FOUNDATION_STATIC void imgui_glfw_set_cursor_pos_callback(GLFWwindow* window, double x, double y)
{
    ImGui::SetCurrentContext((ImGuiContext*)glfwGetWindowUserPointer(window));
    imgui_update_cursor_pos(window, x, y);
    signal_thread();
}

FOUNDATION_STATIC void imgui_glfw_cursor_enter_callback(GLFWwindow* window, int entered)
{
    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
        return;

    static double s_last_valid_mouse_pos[2] = { 0, 0 };

    ImGui::SetCurrentContext((ImGuiContext*)glfwGetWindowUserPointer(window));
    ImGuiIO& io = ImGui::GetIO();
    if (entered)
    {
        imgui_update_cursor_pos(window, s_last_valid_mouse_pos[0], s_last_valid_mouse_pos[1]);
    }
    else if (!entered)
    {
        s_last_valid_mouse_pos[0] = io.MousePos.x;
        s_last_valid_mouse_pos[1] = io.MousePos.y;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    signal_thread();
}

FOUNDATION_STATIC void imgui_glfw_install_callbacks(GLFWwindow* window)
{
    glfwSetWindowUserPointer(window, ImGui::GetCurrentContext());

    glfwSetMouseButtonCallback(window, imgui_glfw_mouse_button_callback);
    glfwSetScrollCallback(window, imgui_glfw_scroll_callback);
    glfwSetKeyCallback(window, imgui_glfw_key_callback);
    glfwSetCharCallback(window, imgui_glfw_char_callback);
    glfwSetWindowFocusCallback(window, imgui_glfw_window_focus_callback);
    glfwSetCursorPosCallback(window, imgui_glfw_set_cursor_pos_callback);
    glfwSetCursorEnterCallback(window, imgui_glfw_cursor_enter_callback);
}

bool imgui_glfw_init(GLFWwindow* window, bool install_callbacks)
{    
    _time = 0.0;

    // Setup back-end capabilities flags
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;   // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;    // We can honor io.WantSetMousePos requests (optional, rarely used)

    io.ClipboardUserData = window;
    io.SetClipboardTextFn = imgui_glfw_set_clipboard_text;
    io.GetClipboardTextFn = imgui_glfw_get_clipboard_text;
    ImGui::GetMainViewport()->PlatformHandleRaw = (void*)glfw_platform_window_handle(window);

    // Load cursors
    // FIXME: GLFW doesn't expose suitable cursors for ResizeAll, ResizeNESW, ResizeNWSE. We revert to arrow cursor for those.
    _mouse_cursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    _mouse_cursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    _mouse_cursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    _mouse_cursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    _mouse_cursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    _mouse_cursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    _mouse_cursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);

    // Create mouse cursors
    // (By design, on X11 cursors are user configurable and some cursors may be missing. When a cursor doesn't exist,
    // GLFW will emit an error which will often be printed by the app, so we temporarily disable error reporting.
    // Missing cursors will return nullptr and our _UpdateMouseCursor() function will use the Arrow cursor instead.)
    GLFWerrorfun prev_error_callback = glfwSetErrorCallback(nullptr);
    _mouse_cursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    _mouse_cursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    _mouse_cursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    _mouse_cursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    _mouse_cursors[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    #if GLFW_HAS_NEW_CURSORS
        _mouse_cursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
        _mouse_cursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
        _mouse_cursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
        _mouse_cursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
    #else
        _mouse_cursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        _mouse_cursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        _mouse_cursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        _mouse_cursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    #endif

    #if (GLFW_VERSION_COMBINED >= 3300) // Eat errors (see #5785)
        (void)glfwGetError(NULL);
    #endif
    glfwSetErrorCallback(prev_error_callback);

    if (install_callbacks)
        imgui_glfw_install_callbacks(window);

    return true;
}

void imgui_shutdown()
{
    imgui_destroy_cursors();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

void imgui_initiaize(GLFWwindow* window)
{
    log_info(HASH_IMGUI, STRING_CONST("Initializing IMGUI..."));
    
    ImGui::SetAllocatorFunctions(imgui_allocate, imgui_deallocate, nullptr);

    // Setup Dear ImGui binding
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.WantSaveIniSettings = false;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    
    imgui_glfw_init(window, true);

    float xscale = session_get_float("font_scaling1"), yscale = 1.0f;
    GLFWmonitor* monitor = glfw_find_window_monitor(window);
    glfwGetMonitorContentScale(monitor, &xscale, &yscale);
    if (xscale > 1)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.ScaleAllSizes(xscale);
    }

    // Setup style
    #if FOUNDATION_PLATFORM_MACOS
        //if (system_is_dark_theme())
            ImGui::StyleColorsDark();
        //else
          //  ImGui::StyleColorsLight();
    #else
        ImGui::StyleColorsDark();
    #endif
}

void* imgui_allocate(size_t sz, void* user_data)
{
    return memory_allocate(HASH_IMGUI, sz, sizeof(float), MEMORY_PERSISTENT);
}

void imgui_deallocate(void* ptr, void* user_data)
{
    memory_deallocate(ptr);
}

float imgui_calc_text_width(const char* text, size_t length, imgui_calc_text_flags_t flags /*= ImGuiCalcTextFlags::None*/)
{
    ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 label_size = ImGui::CalcTextSize(text, text + length);

    float item_width = label_size.x;
    if (test(flags, ImGuiCalcTextFlags::Padding))
        item_width += style.ItemInnerSpacing.x * 2.0f;
    return item_width;
}

