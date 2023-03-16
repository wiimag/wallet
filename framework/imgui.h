/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/common.h>
#include <framework/function.h>

#include <imgui/imgui.h>
#include <imgui/implot.h>
#include <imgui/imgui_internal.h>
#include <imgui/imgui_date_chooser.h>

typedef struct GLFWwindow GLFWwindow;

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

typedef enum class ImGuiCalcTextFlags : int {
    None = 0,

    Padding = 1 << 0
} imgui_calc_text_flags_t;
DEFINE_ENUM_FLAGS(ImGuiCalcTextFlags);

#if IMGUI_ENABLE_TEST_ENGINE

struct ImGuiTestItem
{
    ImGuiID id;
    ImRect bb;
    string_t label;
    ImGuiItemStatusFlags flags;
};

ImGuiID ImGuiTestEngine_GetID(ImGuiContext* ctx, const char* label);

ImGuiTestItem* ImGuiTestEngine_FindItemByLabel(ImGuiContext* ctx, const char* label);

#endif

/*! @brief Returns true if the given key is pressed.
 *
 *  @param key The key to check.
 *  @return True if the key is pressed.
 */
bool shortcut_executed(bool ctrl, bool alt, bool shift, bool super, int key);
FOUNDATION_FORCEINLINE bool shortcut_executed(bool ctrl, bool alt, int key) { return shortcut_executed(ctrl, alt, false, false, key); }
FOUNDATION_FORCEINLINE bool shortcut_executed(bool ctrl, int key) { return shortcut_executed(ctrl, false, false, false, key); }
FOUNDATION_FORCEINLINE bool shortcut_executed(int key) { return shortcut_executed(false, false, false, false, key); }

/*! Allocates memory for IMGUI.
 *
 *  @param sz The size of the memory to allocate.
 *  @param user_data The user data.
 *  @return The allocated memory.
 */
void* imgui_allocate(size_t sz, void* user_data);

/*! Deallocates memory for IMGUI.
 *
 *  @param ptr The pointer to the memory to deallocate.
 *  @param user_data The user data.
 */
void imgui_deallocate(void* ptr, void* user_data);

/*! Makes a color more bright. 
 * 
 *  @param c The color to brighten.
 *  @param intensity The intensity of the brightening.
 * 
 *  @return The brightened color.
 */
ImVec4 imgui_color_highlight(ImVec4 c, float intensity);

/*! Makes a color more bright. 
 * 
 *  @param c The color to brighten.
 *  @param intensity The intensity of the brightening.
 * 
 *  @return The brightened color.
 */
 ImU32 imgui_color_highlight(const ImU32& c, float intensity);

/*! Returns a good color for text on the given background.
 *
 *  @param bg The background color.
 *
 *  @return The color for text on the given background.
 */
ImColor imgui_color_text_for_background(const ImColor& bg);

/*! Returns a good color for text on the given background.
 *
 *  @param bg The background color.
 *
 *  @return The color for text on the given background.
 */
ImColor imgui_color_contrast_background(const ImColor& color);

/*! Draw two view divided by a splitter handle.
 * 
 *  @param id The id of the splitter.
 *  @param splitter_pos The position of the splitter.
 *  @param left_callback The callback to render the left view.
 *  @param right_callback The callback to render the right view.
 *  @param orientation The orientation of the splitter.
 *  @param frame_flags The flags for the frame.
 *  @param preserve_proportion If true, the splitter will preserve the proportion of the views.
 *  
 *  @return True if the splitter was moved.
 */
bool imgui_draw_splitter(const char* id, float* splitter_pos,
    const imgui_frame_render_callback_t& left_callback,
    const imgui_frame_render_callback_t& right_callback,
    ImGuiSplitterOrientation orientation,
    ImGuiWindowFlags frame_flags = ImGuiWindowFlags_None,
    bool preserve_proportion = false);

/*! Draw two view divided by a splitter handle.
 * 
 *  @param id The id of the splitter.
 *  @param left_callback The callback to render the left view.
 *  @param right_callback The callback to render the right view.
 *  @param orientation The orientation of the splitter.
 *  @param frame_flags The flags for the frame.
 *  @param initial_propertion The initial proportion of the views.
 *  @param preserve_proportion If true, the splitter will preserve the proportion of the views.
 * 
 *  @return True if the splitter was moved.
 */
bool imgui_draw_splitter(const char* id,
    const imgui_frame_render_callback_t& left_callback,
    const imgui_frame_render_callback_t& right_callback,
    ImGuiSplitterOrientation orientation,
    ImGuiWindowFlags frame_flags = ImGuiWindowFlags_None,
    float initial_propertion = 0.0f,
    bool preserve_proportion = false);

/*! Draws a solid frame rect.
 * 
 *  @param rect The rect to draw.
 *  @param border_color The color of the border.
 *  @param background_color The color of the background.
 *  @param rounding The rounding of the corners.
 * 
 *  @return The rect of the frame.
 */
ImRect imgui_draw_rect(const ImVec2& offset, const ImVec2& size, 
    const ImColor& border_color = 0U, const ImColor& background_color = 0U);

/*! Draw a button aligned to the right.
 *
 *  @param label The label of the button.
 *  @param same_line If true, the button will be drawn on the same line.
 *  @param in_space_left The space left to draw the button.
 *
 *  @return True if the button was pressed.
 */
bool imgui_right_aligned_button(const char* label, bool same_line = false, float in_space_left = -1.0f);

/*! Draw a text label aligned to the right.
 *
 *  @param label The label of the button.
 *  @param same_line If true, the button will be drawn on the same line.
 */
void imgui_right_aligned_label(const char* label, bool same_line = false);

/*! Draw a label aligned to the center.
 *
 *  @param label The label of the button.
 *  @param same_line If true, the button will be drawn on the same line.
 *  @param in_space_left The space left to draw the button.
 *
 *  @return True if the button was pressed.
 */
void imgui_centered_aligned_label(const char* label, bool same_line = false);

/*! Returns the global UI scaling factor.
 * 
 *  @param value The value to scale.
 *  @return The scaled value.
 */
float imgui_get_font_ui_scale(float value = 1.0f);

/*! @def IM_SCALEF
 * 
 *  Returns the global UI scaling factor.
 * 
 *  @param value The value to scale.
 *  @return The scaled value.
 */
#define IM_SCALEF(value) imgui_get_font_ui_scale((float)(value))

/*! Sets the global UI scaling factor.
 *
 *  @param scale The scaling factor.
 */
void imgui_set_font_ui_scale(float scale);

/*! Returns the rect of the available space for the UI.
 *
 *  @return The rect of the available space for the UI.
 */
ImRect imgui_get_available_rect();

/*! Load the main font in the current IMGUI context.
 *
 *  @param xscale The scale factor for the font.
 *
 *  @return True if the font was loaded.
 */
ImFont* imgui_load_main_font(float xscale = 1.0f);

/*! Load the Google Material Design font in the current IMGUI context.
 *
 *  @param xscale The scale factor for the font.
 *
 *  @return True if the font was loaded.
 */
ImFont* imgui_load_material_design_font(float xscale = 1.0f);

/*! Prepare the next IMGUI frame.
 *
 *  @param window The window to draw the UI on.
 *  @param width The width of the window.
 *  @param height The height of the window.
 */
void imgui_new_frame(GLFWwindow* window, int width, int height);

/*! Initialize the main IMGUI context for the main window.
 *
 *  @param window The window to draw the UI on.
 *  @param install_callbacks If true, the callbacks will be installed.
 *
 *  @return True if the context was initialized.
 */
bool imgui_glfw_init(GLFWwindow* window, bool install_callbacks);

/*! Initialize the main IMGUI resources.
 *  
 *  @param window The main window to draw the UI on.
 */
void imgui_initiaize(GLFWwindow* window);

/*! Releases the main IMGUI resources. */
void imgui_shutdown();

/*! Converts a GLFW key to an IMGUI key.
 *
 *  @param key The GLFW key.
 *  @return The IMGUI key.
 */
ImGuiKey imgui_key_from_glfw_key(int key);

/*! Computes the text width using custom flags. 
 * 
 *  @param text The text to compute the width of.
 *  @param length The length of the text.
 *  @param flags The flags to use for the computation.
 *  @return The width of the text.
 */
float imgui_calc_text_width(const char* text, size_t length, imgui_calc_text_flags_t flags = ImGuiCalcTextFlags::None);

/*! Computes the literal string text width using custom flags.
 *
 *  @param text The text to compute the width of.
 *  @param flags The flags to use for the computation.
 *  @return The width of the text.
 */
template<size_t N>
float imgui_calc_text_width(const char(&text)[N], imgui_calc_text_flags_t flags = ImGuiCalcTextFlags::None)
{
    return imgui_calc_text_width(text, N - 1, flags);
}

/*! Draw a bullet item with a wrapped text label.
 *
 *  @param fmt The format string.
 *  @param ... The format arguments.
 */
void imgui_bullet_text_wrapped(const char* fmt, ...);

namespace ImGui 
{
    /*! Move the current cursor position.
     *
     *  @param x The x offset.
     *  @param y The y offset.
     *  @param same_line If true, the cursor will be moved on the same line.
     *
     *  @return The new cursor position.
     */
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

    /*! @see #imgui_right_aligned_button */
    FOUNDATION_FORCEINLINE bool ButtonRightAligned(const char* label, bool same_line = false, float in_space_left = -1.0f)
    {
        return imgui_right_aligned_button(label, same_line, in_space_left);
    }

    /*! Draws a text label presented as an URL hyperlink.
     * 
     *  @param name Text label.
     *  @param URL The URL.
     *  @param SameLineBefore_ If true, the text will be drawn on the same line.
     *  @param SameLineAfter_ If true, the text will be drawn on the same line.
     *  @return True if the URL was clicked.
     */
    bool TextURL(const char* name, const char* name_end, const char* URL, size_t URL_length, uint8_t SameLineBefore_ = 0, uint8_t SameLineAfter_ = 0);

    /*! Helper to draw unformatted on the same line as the previous control. 
     * 
     *  @param text The text to draw.
     *  @param same_line If true, the text will be drawn on the same line.
     */
    FOUNDATION_FORCEINLINE void TextUnformatted(string_const_t text, bool same_line = false)
    {
        if (text.length == 0)
            return;
        if (same_line)
            ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        TextUnformatted(text.str, text.str + text.length);
    }

    /*! Make the UI compact because there are so many fields */
    void PushStyleCompact();
    
    /*! Pops the compact style pushed by #PushStyleCompact */
    void PopStyleCompact();

    /*! Draw a table row with separators in its columns */
    void TableRowSeparator();

    /*! Draw a element by wrapping the text in a bullet */
    FOUNDATION_FORCEINLINE void BulletTextWrapped(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        imgui_bullet_text_wrapped(fmt, args);
        va_end(args);
    }
}
