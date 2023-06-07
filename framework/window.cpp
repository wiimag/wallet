/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "window.h"

#if BUILD_APPLICATION

#include <framework/app.h>
#include <framework/glfw.h>
#include <framework/bgfx.h>
#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/module.h>
#include <framework/session.h>
#include <framework/dispatcher.h>
#include <framework/math.h>
#include <framework/string.h>
#include <framework/system.h>

#include <foundation/hash.h>

#include <imgui/fs_imgui.bin.h>
#include <imgui/vs_imgui.bin.h>

#include <bgfx/platform.h>
#include <bgfx/embedded_shader.h>

#if FOUNDATION_PLATFORM_WINDOWS
#include <GLFW/glfw3native.h>
#endif

#include <foundation/exception.h>

#ifndef ENABLE_DIALOG_NO_WINDOW_DECORATION
    #define ENABLE_DIALOG_NO_WINDOW_DECORATION 0
#endif

#define HASH_WINDOW static_hash_string("window", 6, 0xa9008b1c524585c4ULL)

static const bgfx::EmbeddedShader _bgfx_imgui_embedded_shaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui), BGFX_EMBEDDED_SHADER_END()
};

/*! Window data structure to hold window resources */
struct window_t
{
    /*! Window handle to manage the window resources
     *  This is basically the window index in the module global array + 1 
     */
    object_t handle{ 0 };

    /*! Window flags */
    window_flags_t flags{ WindowFlags::None };

    /*! Window state */
    double time{ 0.0 };
    bool prepared{ false };
    double last_valid_mouse_pos[2]{ 0, 0 };
    int frame_width{ 0 }, frame_height{ 0 };
    float scale{ 1.0f };
    bool opened{ true };

    /*! GLFW window handle */
    GLFWwindow* glfw_window{ nullptr };
    GLFWcursor* glfw_mouse_cursors[ImGuiMouseCursor_COUNT] = { nullptr };

    /*! BGFX resources */
    uint8_t                 bgfx_view{ 255 };
    bgfx::VertexLayout      bgfx_imgui_vertex_layout;
    bgfx::TextureHandle     bgfx_imgui_font_texture{ bgfx::kInvalidHandle };
    bgfx::ProgramHandle     bgfx_imgui_shader_handle{ bgfx::kInvalidHandle };
    bgfx::UniformHandle     bgfx_imgui_attrib_location_tex{ bgfx::kInvalidHandle };
    bgfx::FrameBufferHandle bgfx_imgui_frame_buffer_handle{ bgfx::kInvalidHandle };
    
    /*! ImGui window context */
    ImGuiContext* imgui_context{ nullptr };
    ImPlotContext* implot_context{ nullptr };

    /*! Window event handlers */
    window_event_handler_t open{ nullptr };
    window_event_handler_t close{ nullptr };
    window_event_handler_t render{ nullptr };
    window_resize_callback_t resize{ nullptr };
    window_event_handler_t menu{ nullptr };
    
    /*! Window user data */
    string_t id{};
    string_t title{};
    void* user_data{ nullptr };
    config_handle_t config{ nullptr };
};

struct WindowContext
{
    window_t* window;

    ImGuiContext* prev_imgui_context;
    ImPlotContext* prev_implot_context;

    WindowContext(window_t* win)
        : window(win)
        , prev_imgui_context(ImGui::GetCurrentContext())
        , prev_implot_context(ImPlot::GetCurrentContext())
    {
        FOUNDATION_ASSERT(window);
        FOUNDATION_ASSERT(window->imgui_context);
        FOUNDATION_ASSERT(window->implot_context);

        ImGui::SetCurrentContext((ImGuiContext*)window->imgui_context);
        ImPlot::SetCurrentContext((ImPlotContext*)window->implot_context);
    }

    WindowContext(GLFWwindow* glfw_window)
        : WindowContext((window_t*)glfwGetWindowUserPointer(glfw_window))
    {
        FOUNDATION_ASSERT(window->glfw_window == glfw_window);
    }

    ~WindowContext()
    {
        ImGui::SetCurrentContext(prev_imgui_context);
        ImPlot::SetCurrentContext(prev_implot_context);

        signal_thread();
    }
};

/*! Global Window module */
static struct WINDOW_MODULE {

    window_t** windows{ nullptr };

    window_handle_t current_window{ 0 };

    config_handle_t configs;
    
} *_window_module;

// 
// # PRIVATE
//

FOUNDATION_STATIC unsigned int window_index(window_handle_t window_handle)
{
    FOUNDATION_ASSERT(window_handle >= 1 && window_handle <= array_size(_window_module->windows));
    return window_handle - 1;
}

FOUNDATION_STATIC window_t* window_handle_lookup(window_handle_t window_handle)
{
    if (window_handle == 0)
        return nullptr;
    if (window_handle > array_size(_window_module->windows))
        return nullptr;
    auto index = window_index(window_handle);
    return _window_module->windows[index];
}

FOUNDATION_STATIC window_t* window_allocate(GLFWwindow* glfw_window, window_flags_t flags)
{
    FOUNDATION_ASSERT(glfw_window);

    // Allocate window object
    window_t* new_window = MEM_NEW(HASH_WINDOW, window_t);
    new_window->handle = array_size(_window_module->windows) + 1;
    new_window->glfw_window = glfw_window;

    // Set the window user pointer
    glfwSetWindowUserPointer(glfw_window, new_window);

    // Create the IMGUI context
    new_window->imgui_context = ImGui::CreateContext();
    new_window->implot_context = ImPlot::CreateContext();

    // Set the window flags
    new_window->flags = flags;

    // Store the window object
    array_push(_window_module->windows, new_window);

    return new_window;
}

FOUNDATION_STATIC void window_bgfx_invalidate_device_objects(window_t* win)
{
    if (bgfx::isValid(win->bgfx_imgui_shader_handle))
        bgfx::destroy(win->bgfx_imgui_shader_handle);

    if (bgfx::isValid(win->bgfx_imgui_attrib_location_tex))
        bgfx::destroy(win->bgfx_imgui_attrib_location_tex);

    if (bgfx::isValid(win->bgfx_imgui_font_texture))
    {
        bgfx::destroy(win->bgfx_imgui_font_texture);
        win->bgfx_imgui_font_texture.idx = bgfx::kInvalidHandle;
    }

    if (bgfx::isValid(win->bgfx_imgui_frame_buffer_handle))
    {
        bgfx::destroy(win->bgfx_imgui_frame_buffer_handle);
        win->bgfx_imgui_frame_buffer_handle.idx = bgfx::kInvalidHandle;
    }
}

FOUNDATION_STATIC void window_restore_settings(window_t* win, config_handle_t config)
{
    win->scale = (float)config["scale"].as_number(1.0);
    win->config = config;
}

FOUNDATION_STATIC string_const_t window_get_imgui_save_path(window_t* win)
{
    string_t normalized_window_id = path_normalize_name(SHARED_BUFFER(BUILD_MAX_PATHLEN), STRING_ARGS(win->id));
    string_const_t window_imgui_save_path = session_get_user_file_path(
        STRING_ARGS(normalized_window_id),
        STRING_CONST("imgui"),
        STRING_CONST("ini"), true);
    return window_imgui_save_path;
}

FOUNDATION_STATIC void window_save_settings(window_t* win)
{
    FOUNDATION_ASSERT(win && win->glfw_window);

    if (!win->config)
        return;

    int window_x = 0, window_y = 0;
    int window_width = 0, window_height = 0;
    glfwGetWindowPos(win->glfw_window, &window_x, &window_y);
    glfwGetWindowSize(win->glfw_window, &window_width, &window_height);
    const int window_maximized = glfwGetWindowAttrib(win->glfw_window, GLFW_MAXIMIZED);

    config_set(win->config, "x", (double)window_x);
    config_set(win->config, "y", (double)window_y);
    config_set(win->config, "width", (double)window_width);
    config_set(win->config, "height", (double)window_height);
    config_set(win->config, "maximized", window_maximized == GLFW_TRUE);
    config_set(win->config, "scale", (double)win->scale);
    
    WindowContext ctx(win);
    string_const_t window_imgui_save_path = window_get_imgui_save_path(win);
    ImGui::SaveIniSettingsToDisk(window_imgui_save_path.str);
}

FOUNDATION_STATIC void window_deallocate(window_t* win)
{
    if (!win)
        return;

    // Let the user do anything before closing the window
    win->close.invoke(win->handle);

    // Close application dialogs owned by this window
    app_close_dialogs(win->handle);

    // Save the window settings
    window_save_settings(win);

    const unsigned window_count = array_size(_window_module->windows);
    const unsigned window_index = ::window_index(win->handle);
    if (_window_module->windows[window_count - 1] == win)
    {
        // We can safely delete the last window as the window handle id shouldn't be reused.
        array_erase_ordered_safe(_window_module->windows, window_index);
    }
    else
    {
        // We can't delete the last window as the window handle id might be reused.
        _window_module->windows[window_index] = nullptr;
    }

    // Destroy the IMGUI context
    ImPlot::DestroyContext(win->implot_context);
    ImGui::DestroyContext(win->imgui_context);

    // Destroy bgfx resources
    window_bgfx_invalidate_device_objects(win);
        
    // Destroy the GLFW window
    if (win->glfw_window)
    {
        // Destroy GLFW mouse cursors
        for (auto& cursor : win->glfw_mouse_cursors)
            glfwDestroyCursor(cursor);
        memset(win->glfw_mouse_cursors, 0, sizeof(win->glfw_mouse_cursors));
        
        glfwDestroyWindow(win->glfw_window);
    }

    string_deallocate(win->id.str);
    string_deallocate(win->title.str);
        
    MEM_DELETE(win);
}

FOUNDATION_STATIC void window_glfw_set_clipboard_text(void* user_data, const char* text)
{
    glfwSetClipboardString((GLFWwindow*)user_data, text);
}

FOUNDATION_STATIC const char* window_glfw_get_clipboard_text(void* user_data)
{
    return glfwGetClipboardString((GLFWwindow*)user_data);
}

FOUNDATION_STATIC bool window_bgfx_create_fonts_texture(window_t* win)
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();

    float xscale = 1.0f, yscale = 1.0f;
    GLFWmonitor* monitor = glfw_find_window_monitor(win->glfw_window);
    glfwGetMonitorContentScale(monitor, &xscale, &yscale);

    xscale *= session_get_float("font_scaling", 1.0f);

    ImFont* main_font = imgui_load_main_font(xscale);
    if (main_font)
    {
        // Merge in icons from Google Material Design
        imgui_load_material_design_font(xscale);
    }
    else
    {
        ImFontConfig config;
        config.SizePixels = 16.0f * xscale;
        io.Fonts->AddFontDefault(&config);
    }

    // Build texture atlas
    int width, height;
    unsigned char* pixels;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // Upload texture to graphics system
    win->bgfx_imgui_font_texture = bgfx::createTexture2D(
        (uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::BGRA8,
        0, bgfx::copy(pixels, width * height * 4));

    // Store our identifier
    io.Fonts->SetTexID((void*)(intptr_t)win->bgfx_imgui_font_texture.idx);

    return true;
}

FOUNDATION_STATIC bool window_bgfx_create_device_objects(window_t* win)
{
    bgfx::RendererType::Enum type = bgfx::getRendererType();
    win->bgfx_imgui_shader_handle = bgfx::createProgram(
        bgfx::createEmbeddedShader(_bgfx_imgui_embedded_shaders, type, "vs_ocornut_imgui"),
        bgfx::createEmbeddedShader(_bgfx_imgui_embedded_shaders, type, "fs_ocornut_imgui"),
        true);

    win->bgfx_imgui_vertex_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    win->bgfx_imgui_attrib_location_tex =
        bgfx::createUniform("g_AttribLocationTex", bgfx::UniformType::Sampler);

    return window_bgfx_create_fonts_texture(win);
}

FOUNDATION_STATIC void window_bgfx_init(window_t* win)
{
    FOUNDATION_ASSERT(win);
    
    win->bgfx_view = (uint8_t)(win->handle & 0xff) + 10;

    if (window_bgfx_create_device_objects(win))
    {
        bgfx::setViewClear(win->bgfx_view, BGFX_CLEAR_COLOR);
        bgfx::setViewRect(win->bgfx_view, 0, 0, bgfx::BackbufferRatio::Equal);
    }
}

FOUNDATION_STATIC void window_glfw_update_key_modifiers(int mods)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, (mods & GLFW_MOD_CONTROL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (mods & GLFW_MOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (mods & GLFW_MOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (mods & GLFW_MOD_SUPER) != 0);
}

FOUNDATION_STATIC void window_glfw_mouse_button_callback(GLFWwindow* glfw_window, int button, int action, int mods)
{
    WindowContext ctx(glfw_window);
    
    window_glfw_update_key_modifiers(mods);

    ImGuiIO& io = ImGui::GetIO();
    if (button >= 0 && button < ImGuiMouseButton_COUNT)
        io.AddMouseButtonEvent(button, action == GLFW_PRESS);
}

FOUNDATION_STATIC void window_glfw_scroll_callback(GLFWwindow* glfw_window, double xoffset, double yoffset)
{
    WindowContext ctx(glfw_window);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent((float)xoffset, (float)yoffset);
}

FOUNDATION_STATIC void window_glfw_key_callback(GLFWwindow* glfw_window, int keycode, int scancode, int action, int mods)
{
    WindowContext ctx(glfw_window);

    if (keycode == -1)
        return;

    if (action != GLFW_PRESS && action != GLFW_RELEASE)
        return;

    // Workaround: X11 does not include current pressed/released modifier key in 'mods' flags. https://github.com/glfw/glfw/issues/1630
    if (int keycode_to_mod = glfw_key_to_modifier(keycode))
        mods = (action == GLFW_PRESS) ? (mods | keycode_to_mod) : (mods & ~keycode_to_mod);
    window_glfw_update_key_modifiers(mods);

    keycode = glfw_translate_untranslated_key(keycode, scancode);

    ImGuiIO& io = ImGui::GetIO();
    const ImGuiKey imgui_key = imgui_key_from_glfw_key(keycode);
    io.AddKeyEvent(imgui_key, (action == GLFW_PRESS));
    io.SetKeyEventNativeData(imgui_key, keycode, scancode); // To support legacy indexing (<1.87 user code)
}

FOUNDATION_STATIC void window_imgui_update_mouse_cursor(window_t* win)
{
    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) || glfwGetInputMode(win->glfw_window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
        return;

    ImGuiMouseCursor imgui_cursor = ImGui::GetMouseCursor();
    if (imgui_cursor == ImGuiMouseCursor_None || io.MouseDrawCursor)
    {
        // Hide OS mouse cursor if imgui is drawing it or if it wants no cursor
        glfwSetInputMode(win->glfw_window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
    }
    else
    {
        // Show OS mouse cursor
        // FIXME-PLATFORM: Unfocused windows seems to fail changing the mouse cursor with GLFW 3.2, but 3.3 works here.
        glfwSetCursor(win->glfw_window, win->glfw_mouse_cursors[imgui_cursor] ? win->glfw_mouse_cursors[imgui_cursor] : win->glfw_mouse_cursors[ImGuiMouseCursor_Arrow]);
        glfwSetInputMode(win->glfw_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

FOUNDATION_STATIC void window_imgui_update_cursor_pos(GLFWwindow* window, double& x, double& y)
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

FOUNDATION_STATIC void window_glfw_set_cursor_pos_callback(GLFWwindow* glfw_window, double x, double y)
{
    WindowContext ctx(glfw_window);
    
    window_imgui_update_cursor_pos(glfw_window, x, y);
    window_imgui_update_mouse_cursor(ctx.window);
}

FOUNDATION_STATIC void window_glfw_char_callback(GLFWwindow* glfw_window, unsigned int c)
{
    WindowContext ctx(glfw_window);
    
    ImGuiIO& io = ImGui::GetIO();
    if (c > 0 && c < 0x10000)
        io.AddInputCharacter((unsigned short)c);
}

FOUNDATION_STATIC void window_glfw_focus_callback(GLFWwindow* glfw_window, int focused)
{
    WindowContext ctx(glfw_window);
    
    ImGuiIO& io = ImGui::GetIO();
    io.AddFocusEvent(focused != 0);
}

FOUNDATION_STATIC void window_glfw_cursor_enter_callback(GLFWwindow* glfw_window, int entered)
{
    WindowContext ctx(glfw_window);
    
    if (glfwGetInputMode(glfw_window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
        return;

    ImGuiIO& io = ImGui::GetIO();
    if (entered)
    {
        window_imgui_update_cursor_pos(glfw_window, ctx.window->last_valid_mouse_pos[0], ctx.window->last_valid_mouse_pos[1]);
    }
    else if (!entered)
    {
        ctx.window->last_valid_mouse_pos[0] = io.MousePos.x;
        ctx.window->last_valid_mouse_pos[1] = io.MousePos.y;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }
}

FOUNDATION_STATIC bool window_imgui_init(window_t* win)
{
    FOUNDATION_ASSERT(win);
    FOUNDATION_ASSERT(win->glfw_window);
    
    win->time = 0.0;
    
    // Setup back-end capabilities flags
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;   // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;    // We can honor io.WantSetMousePos requests (optional, rarely used)

    io.IniFilename = NULL;
    io.WantSaveIniSettings = false;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ClipboardUserData = win->glfw_window;
    io.SetClipboardTextFn = window_glfw_set_clipboard_text;
    io.GetClipboardTextFn = window_glfw_get_clipboard_text;

//     if (test(win->flags, WindowFlags::Dialog))
//     {
//         io.ConfigWindowsMoveFromTitleBarOnly = true;
//     }
    
    glfwSetMouseButtonCallback(win->glfw_window, window_glfw_mouse_button_callback);
    glfwSetScrollCallback(win->glfw_window, window_glfw_scroll_callback);
    glfwSetKeyCallback(win->glfw_window, window_glfw_key_callback);
    glfwSetCharCallback(win->glfw_window, window_glfw_char_callback);
    glfwSetWindowFocusCallback(win->glfw_window, window_glfw_focus_callback);
    glfwSetCursorPosCallback(win->glfw_window, window_glfw_set_cursor_pos_callback);
    glfwSetCursorEnterCallback(win->glfw_window, window_glfw_cursor_enter_callback);

    // Load cursors
    // FIXME: GLFW doesn't expose suitable cursors for ResizeAll, ResizeNESW, ResizeNWSE. We revert to arrow cursor for those.
    win->glfw_mouse_cursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    win->glfw_mouse_cursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);

    // Create mouse cursors
    // (By design, on X11 cursors are user configurable and some cursors may be missing. When a cursor doesn't exist,
    // GLFW will emit an error which will often be printed by the app, so we temporarily disable error reporting.
    // Missing cursors will return nullptr and our _UpdateMouseCursor() function will use the Arrow cursor instead.)
    GLFWerrorfun prev_error_callback = glfwSetErrorCallback(nullptr);
    win->glfw_mouse_cursors[ImGuiMouseCursor_Arrow] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    win->glfw_mouse_cursors[ImGuiMouseCursor_TextInput] = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
    win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeNS] = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
    win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeEW] = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
    win->glfw_mouse_cursors[ImGuiMouseCursor_Hand] = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
    #if GLFW_HAS_NEW_CURSORS
        win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_RESIZE_ALL_CURSOR);
        win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
        win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
        win->glfw_mouse_cursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
    #else
        win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeAll] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeNESW] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        win->glfw_mouse_cursors[ImGuiMouseCursor_ResizeNWSE] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
        win->glfw_mouse_cursors[ImGuiMouseCursor_NotAllowed] = glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
    #endif

    #if (GLFW_VERSION_COMBINED >= 3300) // Eat errors (see #5785)
        (void)glfwGetError(NULL);
    #endif
    glfwSetErrorCallback(prev_error_callback);

    ImGui::StyleColorsDark();

    return true;
}

FOUNDATION_STATIC void window_resize(window_t* win, int frame_width, int frame_height)
{
    win->frame_width = frame_width;
    win->frame_height = frame_height;
    
    void* window_handle = glfw_platform_window_handle(win->glfw_window);

    // When window changes size or native window handle changed
    // frame buffer must be recreated.
    if (bgfx::isValid(win->bgfx_imgui_frame_buffer_handle))
    {
        bgfx::destroy(win->bgfx_imgui_frame_buffer_handle);
        win->bgfx_imgui_frame_buffer_handle.idx = bgfx::kInvalidHandle;
    }

    win->bgfx_imgui_frame_buffer_handle = bgfx::createFrameBuffer(
        window_handle, (uint16_t)frame_width, (uint16_t)frame_height);

    win->resize.invoke(win->handle, frame_width, frame_height);
}

FOUNDATION_STATIC void window_prepare(window_t* win)
{
    FOUNDATION_ASSERT(win);

    // Make the window context current
    ImGui::SetCurrentContext(win->imgui_context);
    ImPlot::SetCurrentContext(win->implot_context);
    
    if (!win->prepared)
    {
        window_bgfx_init(win);
        window_imgui_init(win);

        // Load IMGUI settings
        if (none(win->flags, WindowFlags::Transient))
        {
            string_const_t window_imgui_save_path = window_get_imgui_save_path(win);
            if (fs_is_file(STRING_ARGS(window_imgui_save_path)))
                ImGui::LoadIniSettingsFromDisk(window_imgui_save_path.str);
        }

        win->prepared = true;
    }

    int frame_width = win->frame_width, frame_height = win->frame_height;
    glfwGetFramebufferSize(win->glfw_window, &frame_width, &frame_height);
    if (frame_width != win->frame_width || frame_height != win->frame_height)
        window_resize(win, frame_width, frame_height); 
}

FOUNDATION_STATIC void window_bgfx_new_frame(window_t* win)
{
    bgfx::setViewClear(win->bgfx_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH);
    bgfx::touch(win->bgfx_view);
}

FOUNDATION_STATIC void window_imgui_new_frame(window_t* win)
{
    ImGuiIO& io = ImGui::GetIO();

    // We assume the framebuffer is always of the good size.
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DisplaySize = ImVec2((float)win->frame_width, (float)win->frame_height);

    // Setup time step
    const double current_time = glfwGetTime();
    io.DeltaTime = win->time > 0.0 ? (float)(current_time - win->time) : (float)(1.0f / 60.0f);
    win->time = current_time;

    ImGui::NewFrame();
}

FOUNDATION_STATIC void window_bgfx_render_draw_lists(window_t* win, ImDrawData* draw_data)
{
    if (win->frame_width <= 0 || win->frame_height <= 0)
        return;

    ImGuiIO& io = ImGui::GetIO();

    // Setup render state: alpha-blending enabled, no face culling,
    // no depth testing, scissor enabled
    uint64_t state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
        BGFX_STATE_BLEND_FUNC(
            BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

    bgfx::setViewName(win->bgfx_view, "Window");
    bgfx::setViewMode(win->bgfx_view, bgfx::ViewMode::Sequential);
    bgfx::setViewFrameBuffer(win->bgfx_view, win->bgfx_imgui_frame_buffer_handle);

    // Setup viewport, orthographic projection matrix
    float ortho[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    bx::mtxOrtho(ortho, 0.0f, (float)win->frame_width, (float)win->frame_height, 0.0f, -1.0f, 1000.0f, 0.0f, caps->homogeneousDepth);
    bgfx::setViewTransform(win->bgfx_view, NULL, ortho);
    bgfx::setViewRect(win->bgfx_view, 0, 0, win->frame_width, win->frame_height);
    
    // Render command lists
    for (int n = 0; n < draw_data->CmdListsCount; n++) 
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;

        uint32_t numVertices = (uint32_t)cmd_list->VtxBuffer.size();
        uint32_t numIndices = (uint32_t)cmd_list->IdxBuffer.size();

        if (numIndices != 0 && numVertices != 0)
        {
            if ((numVertices != bgfx::getAvailTransientVertexBuffer(
                numVertices, win->bgfx_imgui_vertex_layout)) ||
                (numIndices != bgfx::getAvailTransientIndexBuffer(numIndices))) {
                // not enough space in transient buffer, quit drawing the rest...
                break;
            }

            bgfx::allocTransientVertexBuffer(&tvb, numVertices, win->bgfx_imgui_vertex_layout);
            bgfx::allocTransientIndexBuffer(&tib, numIndices);

            ImDrawVert* verts = (ImDrawVert*)tvb.data;
            memcpy(verts, cmd_list->VtxBuffer.begin(), numVertices * sizeof(ImDrawVert));

            ImDrawIdx* indices = (ImDrawIdx*)tib.data;
            memcpy(indices, cmd_list->IdxBuffer.begin(), numIndices * sizeof(ImDrawIdx));
        }

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];

            if (pcmd->UserCallback)
            {
                pcmd->UserCallback(cmd_list, pcmd);
            }
            else if (numIndices != 0 && numVertices != 0)
            {
                const uint16_t xx = (uint16_t)bx::max(pcmd->ClipRect.x, 0.0f);
                const uint16_t yy = (uint16_t)bx::max(pcmd->ClipRect.y, 0.0f);
                bgfx::setScissor(
                    xx, yy, (uint16_t)bx::min(pcmd->ClipRect.z, 65535.0f) - xx,
                    (uint16_t)bx::min(pcmd->ClipRect.w, 65535.0f) - yy);

                bgfx::setState(state);
                bgfx::TextureHandle texture = { (uint16_t)((intptr_t)pcmd->TextureId & 0xffff) };
                bgfx::setTexture(0, win->bgfx_imgui_attrib_location_tex, texture);
                bgfx::setVertexBuffer(0, &tvb, 0, numVertices);
                bgfx::setIndexBuffer(&tib, pcmd->IdxOffset, pcmd->ElemCount);
                bgfx::submit(win->bgfx_view, win->bgfx_imgui_shader_handle);
            }
        }
    }
}

FOUNDATION_STATIC void window_capture_framebuffer_to_png(window_t* win)
{
    FOUNDATION_ASSERT(win);
    FOUNDATION_ASSERT(win->glfw_window);

    const int width = win->frame_width;
    const int height = win->frame_height;

    char name_buffer[BUILD_MAX_PATHLEN];
    string_t name = path_normalize_name(STRING_BUFFER(name_buffer), STRING_ARGS(win->id));

    // Append the current date
    string_const_t today_string = string_from_date(time_now());
    name = string_concat(STRING_BUFFER(name_buffer), STRING_ARGS(name), STRING_CONST(" ["));
    name = string_concat(STRING_BUFFER(name_buffer), STRING_ARGS(name), STRING_ARGS(today_string));
    name = string_concat(STRING_BUFFER(name_buffer), STRING_ARGS(name), STRING_CONST("]"));

    string_const_t window_imgui_save_path = session_get_user_file_path(
        STRING_ARGS(name),
        STRING_CONST("shots"),
        STRING_CONST("png"), true);
    bgfx::requestScreenShot(win->bgfx_imgui_frame_buffer_handle, window_imgui_save_path.str);

    system_browse_to_file(STRING_ARGS(window_imgui_save_path));
}

FOUNDATION_STATIC void window_handle_global_shortcuts(window_t* win)
{
    if (test(win->flags, WindowFlags::Dialog))
    {
        if (ImGui::IsWindowFocused() && shortcut_executed(ImGuiKey_Escape))
            window_close(win->handle);
    }

    if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiMod_Ctrl | ImGuiKey_Minus))
    {
        win->scale = max(0.2f, win->scale - 0.1f);
    }
    else if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiMod_Ctrl | ImGuiKey_Equal))
    {
        win->scale = min(win->scale + 0.1f, 4.0f);
    }
    else if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiMod_Ctrl | ImGuiKey_0))
    {
        win->scale = 1.0f;
    }

    if (ImGui::Shortcut(ImGuiKey_F11))
    {
        window_capture_framebuffer_to_png(win);
    }

    ImGui::GetIO().FontGlobalScale = win->scale;
}

#if ENABLE_DIALOG_NO_WINDOW_DECORATION
FOUNDATION_STATIC void window_imgui_resize_callback(ImGuiSizeCallbackData* args)
{
    window_handle_t window_handle = (window_handle_t)(uintptr_t)args->UserData;
    window_t* win = window_handle_lookup(window_handle);
    if (win == nullptr)
        return;

    // Check if the window was resized
    const auto* cw = ImGui::GetCurrentWindowRead();
    if (cw && cw == cw->RootWindow && (args->CurrentSize.x != args->DesiredSize.x || args->CurrentSize.y != args->DesiredSize.y))
    {
        //const ImVec2 size_delta = args->DesiredSize - args->CurrentSize;
        int size_x = math_floor(args->DesiredSize.x);
        int size_y = math_floor(args->DesiredSize.y);

        int position[2];
        glfwGetWindowPos(win->glfw_window, &position[0], &position[1]);

        position[0] += math_floor(args->Pos.x);
        position[1] += math_floor(args->Pos.y);

        glfwSetWindowPos(win->glfw_window, position[0], position[1]);
        glfwSetWindowSize(win->glfw_window, size_x, size_y);
        signal_thread();
    }
    else if (cw == nullptr)
    {
        // Check if the window was moved
        const ImVec2 move_delta = args->Pos;
        if (move_delta.x != 0.0f || move_delta.y != 0.0f)
        {
            int position[2];
            glfwGetWindowPos(win->glfw_window, &position[0], &position[1]);

            float scale_x = 1.0f, scale_y = 1.0f;
            GLFWmonitor* monitor = glfw_find_window_monitor(win->glfw_window);
            if (monitor)
            {
                #if FOUNDATION_PLATFORM_WINDOWS
                glfwGetMonitorContentScale(monitor, &scale_x, &scale_y);
                #endif
            }

            position[0] += math_floor(move_delta.x / scale_x);
            position[1] += math_floor(move_delta.y / scale_y);

            glfwSetWindowPos(win->glfw_window, position[0], position[1]);
            signal_thread();
        }
    }
}
#endif

FOUNDATION_STATIC void window_render(window_t* win)
{        
    const bool graphical_mode = !main_is_batch_mode();

    // Skip rendering if the window is iconified
    if (glfwGetWindowAttrib(win->glfw_window, GLFW_ICONIFIED))
        return;

    if (glfwGetWindowAttrib(win->glfw_window, GLFW_VISIBLE) == 0)
    {
        // Window is not visible, but not iconified either
        // This happens when the window is minimized on Windows

        log_warnf(0, WARNING_SUSPICIOUS, STRING_CONST("Window %.*s is not visible, but not iconified either"), STRING_FORMAT(win->id));
        return;
    }

    if (glfwWindowShouldClose(win->glfw_window))
        return;

    if (win->frame_width <= 0 || win->frame_height <= 0)
    {
        log_warnf(0, WARNING_SUSPICIOUS, STRING_CONST("Window %.*s has invalid frame size (%dx%d)"),
                         STRING_FORMAT(win->id), win->frame_width, win->frame_height);
        return;
    }

    // Prepare next frame
    window_bgfx_new_frame(win);
    window_imgui_new_frame(win);

    imgui_set_current_window_scale(glfw_get_window_scale(win->glfw_window));

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)win->frame_width , (float)win->frame_height), ImGuiCond_Always);

    const bool has_menu = win->menu.valid();
    const bool is_dialog_window = test(win->flags, WindowFlags::Dialog);

    ImGuiWindowFlags imgui_window_flags = 
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoCollapse;

    if (has_menu)
        imgui_window_flags |= ImGuiWindowFlags_MenuBar;

    #if ENABLE_DIALOG_NO_WINDOW_DECORATION
    ImGui::PushStyleVar(ImGuiStyleVar_WindowHoverPadding, is_dialog_window ? 7.0 : 4.0f);
    #endif

    if (is_dialog_window)
    {
        imgui_window_flags |= 
            ImGuiWindowFlags_NoNavInputs | 
            ImGuiWindowFlags_AlwaysUseWindowPadding;

        #if ENABLE_DIALOG_NO_WINDOW_DECORATION
        ImGui::SetNextWindowGeometryCallback(window_imgui_resize_callback, (void*)(uintptr_t)win->handle);
        #else
        imgui_window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize;
        #endif
    }
    else
    {
        imgui_window_flags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
    }
    
    // Render window context
    // TODO: Add options to personalize the window visual style
    if (ImGui::Begin(win->title.str, &win->opened, imgui_window_flags))
    {
        window_handle_global_shortcuts(win);

        if (has_menu)
        {
            if (ImGui::BeginMenuBar())
            {
                win->menu(win->handle);
                ImGui::EndMenuBar();
            }
        }

        win->render.invoke(win->handle);

        app_dialogs_render();

    } ImGui::End();

    #if ENABLE_DIALOG_NO_WINDOW_DECORATION
    ImGui::PopStyleVar();
    #endif

    if (!win->opened)
    {
        window_close(win->handle);
        return;
    }
        
    // Render IMGUI frame
    ImGui::Render();

    // Render everything
    window_bgfx_render_draw_lists(win, ImGui::GetDrawData());
    bgfx::frame();
}

bool window_valid(window_handle_t window_handle)
{
    return window_handle_lookup(window_handle) != nullptr;
}

window_handle_t window_current()
{
    return _window_module->current_window;
}

void window_update()
{
    const unsigned window_count = array_size(_window_module->windows);
    if (window_count == 0)
        return;

    glfwPollEvents();

    // Capture the current contexts
    ImGuiContext* current_imgui_context = ImGui::GetCurrentContext();
    ImPlotContext* current_implot_context = ImPlot::GetCurrentContext();

    // Tick all windows
    for (unsigned i = 0; i < window_count; ++i)
    {
        window_t* win = _window_module->windows[i];
        if (win == nullptr || win->glfw_window == nullptr)
            continue;

        window_prepare(win);

        _window_module->current_window = win->handle;

        exception_try([](void* args)
        {
            window_t* win = (window_t*)args;
            window_render(win);
            return 0;
        }, win, [](void* args, const char* file, size_t length)
        {
            window_t* win = (window_t*)args;
            log_errorf(HASH_WINDOW, ERROR_EXCEPTION, "Exception in window render: %.*s", (int)length, file);
            window_close(win->handle);
        }, STRING_CONST("window_dump"));

        _window_module->current_window = 0;
        
        // Check if the window should be closed
        GLFWwindow* glfw_window = win->glfw_window;
        if (glfwWindowShouldClose(glfw_window))
            window_deallocate(win);
    }

    // Restore the previous contexts
    ImPlot::SetCurrentContext(current_implot_context);
    ImGui::SetCurrentContext(current_imgui_context);
}

//
// # PUBLIC API
//

void* window_get_user_data(window_handle_t window_handle)
{
    window_t* window = window_handle_lookup(window_handle);
    if (window == nullptr)
        return nullptr;
    return window->user_data;
}

void window_set_user_data(window_handle_t window_handle, void* user_data)
{
    window_t* window = window_handle_lookup(window_handle);
    if (window == nullptr)
        return;
    window->user_data = user_data;
}

const char* window_title(window_handle_t window_handle)
{
    window_t* window = window_handle_lookup(window_handle);
    FOUNDATION_ASSERT(window);
    FOUNDATION_ASSERT(window->glfw_window);
    
    return window->title.str;
}

void window_set_title(window_handle_t window_handle, const char* title, size_t title_length)
{
    window_t* window = window_handle_lookup(window_handle);
    FOUNDATION_ASSERT(window);
    FOUNDATION_ASSERT(window->glfw_window);
    FOUNDATION_ASSERT(title && title_length);

    string_const_t title_str = string_const(title, title_length);

    string_deallocate(window->title.str);
    window->title = string_clone(title, title_length);
    glfwSetWindowTitle(window->glfw_window, window->title.str);
}

void window_set_render_callback(window_handle_t window_handle, const window_event_handler_t& callback)
{
    window_t* window = window_handle_lookup(window_handle);
    FOUNDATION_ASSERT(window);
    FOUNDATION_ASSERT(window->glfw_window);
    FOUNDATION_ASSERT(callback);

    window->render = callback;
}

void window_set_resize_callback(window_handle_t window_handle, const window_resize_callback_t& callback)
{
    window_t* window = window_handle_lookup(window_handle);
    FOUNDATION_ASSERT(window);
    FOUNDATION_ASSERT(window->glfw_window);
    FOUNDATION_ASSERT(callback);

    window->resize = callback;
}

void window_set_menu_render_callback(window_handle_t window_handle, const window_event_handler_t& callback)
{
    window_t* window = window_handle_lookup(window_handle);
    FOUNDATION_ASSERT(window);
    FOUNDATION_ASSERT(window->glfw_window);
    FOUNDATION_ASSERT(callback);

    window->menu = callback;
}

void window_set_close_callback(window_handle_t window_handle, const window_event_handler_t& callback)
{
    window_t* window = window_handle_lookup(window_handle);
    FOUNDATION_ASSERT(window);
    FOUNDATION_ASSERT(window->glfw_window);
    FOUNDATION_ASSERT(callback);

    window->close = callback;
}

FOUNDATION_STATIC GLFWwindow* window_create(const char* window_title, size_t window_title_length, config_handle_t config, window_flags_t flags)
{
    FOUNDATION_ASSERT(window_title);

    const bool user_requested_maximized = test(flags, WindowFlags::Maximized);
    const bool has_position = config_exists(config, STRING_CONST("x"));
    const bool window_maximized = config["maximized"].as_boolean(user_requested_maximized);

    int window_x = math_trunc(config["x"].as_number(INT_MAX));
    int window_y = math_trunc(config["y"].as_number(INT_MAX));

    GLFWmonitor* monitor = glfw_find_window_monitor(window_x, window_y);
    if (monitor == glfwGetPrimaryMonitor())
        glfwWindowHint(GLFW_MAXIMIZED, window_maximized ? GLFW_TRUE : GLFW_FALSE);

    // Make sure the window is not outside the monitor work area
    int mposx, mposy, mwidth, mheight;
    glfwGetMonitorWorkarea(monitor, &mposx, &mposy, &mwidth, &mheight);
    if (window_x < mposx || window_x > mposx + mwidth)
        window_x = mposx;
    if (window_y < mposy || window_y > mposy + mheight)
        window_y = mposy;

    float scale_x = 1.0f, scale_y = 1.0f;
    #if FOUNDATION_PLATFORM_WINDOWS
        glfwGetMonitorContentScale(monitor, &scale_x, &scale_y);
    #endif

    // If not window settings are passed, then we assume #InitialProportionalSize is the default.
    if (flags == WindowFlags::None)
        flags = WindowFlags::InitialProportionalSize;

    // Compute the best initialize size of the window if not was saved previously
    int initial_width = 1280, initial_height = 720;
    if (!user_requested_maximized && !has_position && test(flags, WindowFlags::InitialProportionalSize))
    {
        if (glfw_get_window_monitor_size(window_x, window_y, &initial_width, &initial_height))
        {
            initial_width = math_round(initial_width * 0.8);
            initial_height = math_round(initial_height * 0.85);
        }
    }

    int window_width = math_trunc(config["width"].as_number(initial_width));
    int window_height = math_trunc(config["height"].as_number(initial_height));

    if (window_height <= 0 || window_width <= 0)
    {
        window_x = INT_MAX;
        window_y = INT_MAX;
        window_width = initial_width;
        window_height = initial_height;
    }
    
    char window_title_null_terminated_buffer[512];
    string_copy(STRING_BUFFER(window_title_null_terminated_buffer), window_title, window_title_length);

    // Create GLFW window
    glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(
        (int)(window_width / scale_x), (int)(window_height / scale_y), 
        window_title_null_terminated_buffer, nullptr, nullptr);
    if (window == nullptr)
        return nullptr;

    // TODO: Add option to override the window icon
    glfw_set_window_main_icon(window);

    if (has_position)
    {
        if (window_x != INT_MAX && window_y != INT_MAX)
        {
            glfwSetWindowPos(window, window_x, window_y);
        }

        if (window_maximized)
            glfwMaximizeWindow(window);
    }
    else if (window_maximized)
    {
        glfwMaximizeWindow(window);
    }
    else
    {
        glfw_set_window_center(window);
    }

    if (test(flags, WindowFlags::Dialog))
    {
        //glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_TRUE);

        #if ENABLE_DIALOG_NO_WINDOW_DECORATION
        glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_TRUE);
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        #endif

        #if FOUNDATION_PLATFORM_WINDOWS
        glfwSetWindowAttrib(window, GLFW_AUTO_ICONIFY, GLFW_FALSE);

        // Get window handle and make it a child of the main window
        HWND hwnd_child = glfwGetWin32Window(window);
        HWND hwnd_main = glfwGetWin32Window(glfw_main_window());

        // Set the window as a child of the main window and hide it from the taskbar
        DWORD style = GetWindowLong(hwnd_child, GWL_EXSTYLE);
        SetWindowLong(hwnd_child, GWL_EXSTYLE, (style & ~WS_EX_APPWINDOW) | WS_EX_TOOLWINDOW);
        SetWindowLongPtr(hwnd_child, GWLP_HWNDPARENT, (LONG_PTR)hwnd_main);
        #endif
    }
    
    glfwShowWindow(window);
    glfwFocusWindow(window);

    return window;
}

FOUNDATION_STATIC window_t* window_find_by_id(string_const_t window_id)
{
    for (unsigned i = 0, end = array_size(_window_module->windows); i != end; ++i)
    {
        window_t* w = _window_module->windows[i];
        if (w != nullptr && string_equal(STRING_ARGS(window_id), STRING_ARGS(w->id)))
            return w;
    }

    return nullptr;
}

void window_close(window_handle_t window_handle)
{
    window_t* window = window_handle_lookup(window_handle);
    FOUNDATION_ASSERT(window);
    
    if (window->glfw_window)
    {
        dispatch([window_handle]()
        {
            window_t* window = window_handle_lookup(window_handle);
            if (window->glfw_window)
                glfw_request_close_window(window->glfw_window);
        });
    }
}

bool window_focus(window_handle_t window_handle)
{
    window_t* window = window_handle_lookup(window_handle);
    FOUNDATION_ASSERT(window);

    if (window->glfw_window == nullptr)
        return false;

    glfwFocusWindow(window->glfw_window);
    return glfwGetWindowAttrib(window->glfw_window, GLFW_FOCUSED) == 1;
}

window_handle_t window_open(
    const char* FOUNDATION_RESTRICT _window_id,
    const char* title, size_t title_length,
    const window_event_handler_t& render_callback,
    const window_event_handler_t& close_callback,
    void* user_data /*= nullptr*/, window_flags_t flags /*= WindowFlags::None*/)
{
    FOUNDATION_ASSERT(_window_id);
    FOUNDATION_ASSERT(title && title_length > 0);
    FOUNDATION_ASSERT(render_callback);

    string_const_t window_id = string_const(_window_id, string_length(_window_id));

    if (test(flags, WindowFlags::Singleton))
    {
        // Check if we already have an instance of the window by scanning window ids.
        window_t* existing_window = window_find_by_id(window_id);
        if (existing_window)
        {
            window_focus(existing_window->handle);
            return existing_window->handle;
        }
    }

    // Restore window settings
    config_handle_t config = none(flags, WindowFlags::Transient) ? config_set_object(_window_module->configs, STRING_ARGS(window_id)) : config_null();

    // Create GLFW window
    GLFWwindow* glfw_window = window_create(title, title_length, config, flags);
    if (glfw_window == nullptr)
    {
        log_errorf(HASH_WINDOW, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to create GLFW window"));
        return OBJECT_INVALID;
    }

    window_t* new_window = window_allocate(glfw_window, flags);
    if (new_window == nullptr)
    {
        glfwDestroyWindow(glfw_window);
        log_errorf(HASH_WINDOW, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to create window"));
        return OBJECT_INVALID;
    }

    // Set new window properties
    new_window->id = string_clone(STRING_ARGS(window_id));
    new_window->title = string_clone(title, title_length);
    new_window->render = render_callback;
    new_window->close = close_callback;
    new_window->user_data = user_data;

    window_restore_settings(new_window, config);

    return new_window->handle;
}

window_handle_t window_open(const char* window_title, const window_event_handler_t& render_callback, window_flags_t flags /*= WindowFlags::None*/)
{
    return window_open(window_title, window_title, string_length(window_title), render_callback, nullptr, nullptr, flags);
}

window_handle_t window_open(
    hash_t context,
    const char* title, size_t title_length,
    const window_event_handler_t& render_callback,
    const window_event_handler_t& close_callback,
    void* user_data /*= nullptr*/, window_flags_t flags /*= WindowFlags::None*/)
{
    string_const_t window_id = string_from_uint_static(context, true, 0, 0);
    return window_open(window_id.str, title, title_length, render_callback, close_callback, user_data, flags | WindowFlags::Singleton);
}

void window_menu()
{
    if (ImGui::TrBeginMenu("Windows"))
        ImGui::EndMenu();
}

//
// # SYSTEM
//

FOUNDATION_STATIC void window_initialize()
{
    _window_module = MEM_NEW(HASH_WINDOW, WINDOW_MODULE);

    if (!main_is_interactive_mode())
        return;

    string_const_t window_config_file_path = session_get_user_file_path(STRING_CONST("windows.json"));
    _window_module->configs = config_parse_file(STRING_ARGS(window_config_file_path), CONFIG_OPTION_PRESERVE_INSERTION_ORDER);
    if (_window_module->configs == nullptr)
        _window_module->configs = config_allocate(CONFIG_VALUE_OBJECT);
    
    module_register_update(HASH_WINDOW, window_update);
}

FOUNDATION_STATIC void window_shutdown()
{
    // Delete all windows
    for (size_t i = 0, end = array_size(_window_module->windows); i < end; ++i)
    {
        window_t* win = _window_module->windows[i];
        if (win == nullptr)
            continue;
        window_deallocate(win);
    }
    array_deallocate(_window_module->windows);

    // Save window configurations
    string_const_t window_config_file_path = session_get_user_file_path(STRING_CONST("windows.json"));
    if (!config_write_file(window_config_file_path, _window_module->configs))
        log_warnf(HASH_WINDOW, WARNING_RESOURCE, STRING_CONST("Failed to save window settings"));
    config_deallocate(_window_module->configs);

    MEM_DELETE(_window_module);
}

DEFINE_MODULE(WINDOW, window_initialize, window_shutdown, MODULE_PRIORITY_UI);

#else

window_handle_t window_open(
    const char* FOUNDATION_RESTRICT _window_id,
    const char* title, size_t title_length,
    const window_event_handler_t& render_callback,
    const window_event_handler_t& close_callback,
    void* user_data /*= nullptr*/, window_flags_t flags /*= WindowFlags::None*/)
{
    FOUNDATION_ASSERT_FAIL("No window support");
    return 0;
}

window_handle_t window_open(const char* window_title, const window_event_handler_t& render_callback, window_flags_t flags /*= WindowFlags::None*/)
{
    FOUNDATION_ASSERT_FAIL("No window support");
    return 0;
}

window_handle_t window_open(
    hash_t context,
    const char* title, size_t title_length,
    const window_event_handler_t& render_callback,
    const window_event_handler_t& close_callback,
    void* user_data /*= nullptr*/, window_flags_t flags /*= WindowFlags::None*/)
{
    FOUNDATION_ASSERT_FAIL("No window support");
    return 0;
}

void* window_get_user_data(window_handle_t window_handle)
{
    FOUNDATION_ASSERT_FAIL("No window support");
    return 0;
}

#endif // BUILD_APPLICATION
