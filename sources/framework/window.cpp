/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "window.h"

#include <framework/glfw.h>
#include <framework/bgfx.h>
#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/service.h>
#include <framework/session.h>
#include <framework/dispatcher.h>

#include <imgui/fs_imgui.bin.h>
#include <imgui/vs_imgui.bin.h>

#include <bx/math.h>
#include <bgfx/platform.h>
#include <bgfx/embedded_shader.h>

#include <GLFW/glfw3native.h>

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

    double time{ 0.0 };
    bool prepared{ false };

    int frame_width{ 0 }, frame_height{ 0 };
    double last_valid_mouse_pos[2]{ 0, 0 };

    /*! GLFW window handle */
    GLFWwindow* glfw_window{ nullptr };
    GLFWcursor* glfw_mouse_cursors[ImGuiMouseCursor_COUNT] = { nullptr };

    uint8_t                 bgfx_view{ 255 };
    bgfx::VertexLayout      bgfx_imgui_vertex_layout;
    bgfx::TextureHandle     bgfx_imgui_font_texture{ BGFX_INVALID_HANDLE };
    bgfx::ProgramHandle     bgfx_imgui_shader_handle{ BGFX_INVALID_HANDLE };
    bgfx::UniformHandle     bgfx_imgui_attrib_location_tex{ BGFX_INVALID_HANDLE };
    bgfx::FrameBufferHandle bgfx_imgui_frame_buffer_handle{ BGFX_INVALID_HANDLE };
    
    /*! ImGui window context */
    ImGuiContext* imgui_context{ nullptr };
    ImPlotContext* implot_context{ nullptr };
};

struct WindowContext
{
    window_t* window;

    ImGuiContext* prev_imgui_context;
    ImPlotContext* prev_implot_context;

    WindowContext(GLFWwindow* glfw_window)
        : window((window_t*)glfwGetWindowUserPointer(glfw_window))
        , prev_imgui_context(ImGui::GetCurrentContext())
        , prev_implot_context(ImPlot::GetCurrentContext())
    {
        FOUNDATION_ASSERT(window);
        FOUNDATION_ASSERT(window->imgui_context);
        FOUNDATION_ASSERT(window->implot_context);
        FOUNDATION_ASSERT(window->glfw_window == glfw_window);

        ImGui::SetCurrentContext((ImGuiContext*)window->imgui_context);
        ImPlot::SetCurrentContext((ImPlotContext*)window->implot_context);
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
    
} *_window_module;

// 
// # PRIVATE
//

FOUNDATION_STATIC window_t* window_allocate(GLFWwindow* glfw_window)
{
    FOUNDATION_ASSERT(glfw_window);

    // Allocate window object
    window_t* new_window = MEM_NEW(HASH_WINDOW, window_t);
    new_window->handle = array_size(_window_module->windows) + 1;
    new_window->glfw_window = glfw_window;

    // Create the IMGUI context
    new_window->imgui_context = ImGui::CreateContext();
    new_window->implot_context = ImPlot::CreateContext();

    // Store the window object
    array_push(_window_module->windows, new_window);

    return new_window;
}

FOUNDATION_STATIC void window_bgfx_invalidate_device_objects(window_t* win)
{
    if (bgfx::isValid(win->bgfx_imgui_attrib_location_tex))
        bgfx::destroy(win->bgfx_imgui_attrib_location_tex);
    if (bgfx::isValid(win->bgfx_imgui_shader_handle))
        bgfx::destroy(win->bgfx_imgui_shader_handle);

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

FOUNDATION_STATIC void window_deallocate(window_t* win)
{
    if (!win)
        return;

    const unsigned window_count = array_size(_window_module->windows);
    FOUNDATION_ASSERT(window_count > 0);
    FOUNDATION_ASSERT(win->handle != OBJECT_INVALID);
    FOUNDATION_ASSERT(win->handle > 0 && win->handle <= window_count);
    
    const unsigned window_index = win->handle - 1;
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

    if (imgui_load_main_font(xscale))
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

    glfwSetWindowUserPointer(win->glfw_window, win);
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
}

FOUNDATION_STATIC void window_prepare(window_t* win)
{
    FOUNDATION_ASSERT(win);

    // Make the window context current
    //glfwMakeContextCurrent(win->glfw_window);
    ImGui::SetCurrentContext(win->imgui_context);
    ImPlot::SetCurrentContext(win->implot_context);
    
    if (!win->prepared)
    {
        window_bgfx_init(win);
        window_imgui_init(win);

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

    //imgui_update_mouse_data(window);
    //imgui_update_mouse_cursor(window);

    ImGui::NewFrame();
}

// This is the main rendering function that you have to implement and call after
// ImGui::Render(). Pass ImGui::GetDrawData() to this function.
// Note: If text or lines are blurry when integrating ImGui into your engine,
// in your Render function, try translating your projection matrix by
// (0.5f,0.5f) or (0.375f,0.375f)
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

FOUNDATION_STATIC void window_render(window_t* win)
{        
    const bool graphical_mode = !main_is_batch_mode();

    // Prepare next frame
    window_bgfx_new_frame(win);
    window_imgui_new_frame(win);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)win->frame_width , (float)win->frame_height));

    // Render window context
    if (ImGui::Begin("Hello World!", nullptr,
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_MenuBar))
    {
        static char input_buffer[256];
        ImGui::Text("Hello World!");
        ImGui::Button("Click me!");
        ImGui::InputTextMultiline("##input", input_buffer, sizeof(input_buffer));

        if (ImGui::IsKeyDown(ImGuiKey_O))
            ImGui::Text("Ooooooooo!");

    } ImGui::End();
        
    // Render IMGUI frame
    ImGui::Render();

    // Render everything
    window_bgfx_render_draw_lists(win, ImGui::GetDrawData());
    bgfx::frame();
}

FOUNDATION_STATIC void window_update()
{
    const unsigned window_count = array_size(_window_module->windows);
    if (window_count == 0)
        return;

    // Capture the current contexts
    GLFWwindow* current_glfw_window = glfwGetCurrentContext();
    ImGuiContext* current_imgui_context = ImGui::GetCurrentContext();
    ImPlotContext* current_implot_context = ImPlot::GetCurrentContext();

    // Tick all windows
    for (unsigned i = 0; i < window_count; ++i)
    {
        window_t* win = _window_module->windows[i];
        if (win == nullptr || win->glfw_window == nullptr)
            continue;

        window_prepare(win);
        window_render(win);
        
        // Check if the window should be closed
        GLFWwindow* glfw_window = win->glfw_window;
        if (glfwWindowShouldClose(glfw_window))
            window_deallocate(win);
    }

    // Restore the previous contexts
    glfwMakeContextCurrent(current_glfw_window);
    ImPlot::SetCurrentContext(current_implot_context);
    ImGui::SetCurrentContext(current_imgui_context);
}

//
// # PUBLIC API
//

window_handle_t window_open(const char* window_title)
{
    // Create GLFW window
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    GLFWwindow* glfw_window = glfwCreateWindow(1280, 720, window_title, nullptr, nullptr);
    if (glfw_window == nullptr)
    {
        log_errorf(HASH_WINDOW, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to create GLFW window"));
        return OBJECT_INVALID;
    }
    
    window_t* new_window = window_allocate(glfw_window);
    if (new_window == nullptr)
    {
        glfwDestroyWindow(glfw_window);
        log_errorf(HASH_WINDOW, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to create window"));
        return OBJECT_INVALID;
    }

    glfw_set_window_main_icon(glfw_window);

    return new_window->handle;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void window_initialize()
{
    _window_module = MEM_NEW(HASH_WINDOW, WINDOW_MODULE);
    
    service_register_menu(HASH_WINDOW, []()
    {
        if (!ImGui::BeginMenuBar())
            return;

        if (ImGui::BeginMenu("Windows"))
        {
            if (ImGui::MenuItem(ICON_MD_LOGO_DEV " Test"))
            {
                window_open("Test Window");
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    });
    service_register_update(HASH_WINDOW, window_update);
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

    MEM_DELETE(_window_module);
}

DEFINE_SERVICE(WINDOW, window_initialize, window_shutdown, SERVICE_PRIORITY_UI);
