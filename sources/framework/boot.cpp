/*
 * Copyright 2022-2023 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#include <framework/glfw.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/dispatcher.h>
#include <framework/profiler.h>
#include <framework/math.h>

#include <foundation/foundation.h>

#include <imgui/imgui.h>
#include <imgui/implot.h>
#include <imgui/fs_imgui.bin.h>
#include <imgui/vs_imgui.bin.h>

#include <bx/math.h>
#include <bx/allocator.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bgfx/embedded_shader.h>

#if FOUNDATION_PLATFORM_WINDOWS

    HWND _window_handle = nullptr;
    
#elif FOUNDATION_PLATFORM_MACOS

    void* _window_handle = nullptr;

#endif

#include <GLFW/glfw3native.h>

#define HASH_BGFX static_hash_string("bgfx", 4, 0x14900654424ff61bULL)
#define HASH_IMGUI static_hash_string("imgui", 5, 0x9803803300f77bbfULL)

// GLFW data
static double       _time = 0.0;
static GLFWcursor*  _mouse_cursors[ImGuiMouseCursor_COUNT] = { nullptr };
GLFWwindow*         _glfw_window = nullptr;

// BGFX data
static uint8_t              _bgfx_imgui_view = 255;
static bgfx::TextureHandle  _bgfx_imgui_font_texture = BGFX_INVALID_HANDLE;
static bgfx::ProgramHandle  _bgfx_imgui_shader_handle = BGFX_INVALID_HANDLE;
static bgfx::UniformHandle  _bgfx_imgui_attrib_location_tex = BGFX_INVALID_HANDLE;
static bgfx::VertexLayout   _bgfx_imgui_vertex_layout;

static const bgfx::EmbeddedShader _bgfx_imgui_embedded_shaders[] = {
    BGFX_EMBEDDED_SHADER(vs_ocornut_imgui),
    BGFX_EMBEDDED_SHADER(fs_ocornut_imgui), BGFX_EMBEDDED_SHADER_END() 
};

#if BUILD_DEVELOPMENT
static bool _run_tests = false;
static bool _show_stats = false;
#endif
#if BUILD_ENABLE_PROFILE
static double _smooth_elapsed_time_ms = 0.0f;
#endif

// Indicates if the application is running in daemon/batch mode which usually run headless.
static bool _batch_mode = false;
static bool _process_should_exit = false;

FOUNDATION_STATIC const char* glfw_get_clipboard_text(void* user_data)
{
    return glfwGetClipboardString((GLFWwindow*)user_data);
}

FOUNDATION_STATIC void glfw_set_clipboard_text(void* user_data, const char* text)
{
    glfwSetClipboardString((GLFWwindow*)user_data, text);
}

FOUNDATION_STATIC void glfw_update_key_modifiers(int mods)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, (mods & GLFW_MOD_CONTROL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (mods & GLFW_MOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (mods & GLFW_MOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (mods & GLFW_MOD_SUPER) != 0);
}

FOUNDATION_STATIC void glfw_mouse_button_callback(GLFWwindow*, int button, int action, int mods)
{
    glfw_update_key_modifiers(mods);

    ImGuiIO& io = ImGui::GetIO();
    if (button >= 0 && button < ImGuiMouseButton_COUNT)
        io.AddMouseButtonEvent(button, action == GLFW_PRESS);

    signal_thread();
}

FOUNDATION_STATIC void glfw_scroll_callback(GLFWwindow*, double xoffset, double yoffset)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent((float)xoffset, (float)yoffset);
    signal_thread();
}

FOUNDATION_STATIC int glfw_key_to_modifier(int key)
{
    if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL)
        return GLFW_MOD_CONTROL;
    if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT)
        return GLFW_MOD_SHIFT;
    if (key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT)
        return GLFW_MOD_ALT;
    if (key == GLFW_KEY_LEFT_SUPER || key == GLFW_KEY_RIGHT_SUPER)
        return GLFW_MOD_SUPER;
    return 0;
}

FOUNDATION_STATIC int glfw_translate_untranslated_key(int key, int scancode)
{
    #if GLFW_HAS_GETKEYNAME && !defined(__EMSCRIPTEN__)
        // GLFW 3.1+ attempts to "untranslated" keys, which goes the opposite of what every other framework does, making using lettered shortcuts difficult.
        // (It had reasons to do so: namely GLFW is/was more likely to be used for WASD-type game controls rather than lettered shortcuts, but IHMO the 3.1 change could have been done differently)
        // See https://github.com/glfw/glfw/issues/1502 for details.
        // Adding a workaround to undo this (so our keys are translated->untranslated->translated, likely a lossy process).
        // This won't cover edge cases but this is at least going to cover common cases.
        if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_EQUAL)
            return key;
        const char* key_name = glfwGetKeyName(key, scancode);
        if (key_name && key_name[0] != 0 && key_name[1] == 0)
        {
            const char char_names[] = "`-=[]\\,;\'./";
            const int char_keys[] = { GLFW_KEY_GRAVE_ACCENT, GLFW_KEY_MINUS, GLFW_KEY_EQUAL, GLFW_KEY_LEFT_BRACKET, GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_BACKSLASH, GLFW_KEY_COMMA, GLFW_KEY_SEMICOLON, GLFW_KEY_APOSTROPHE, GLFW_KEY_PERIOD, GLFW_KEY_SLASH, 0 };
            IM_ASSERT(IM_ARRAYSIZE(char_names) == IM_ARRAYSIZE(char_keys));
            if (key_name[0] >= '0' && key_name[0] <= '9') { key = GLFW_KEY_0 + (key_name[0] - '0'); }
            else if (key_name[0] >= 'A' && key_name[0] <= 'Z') { key = GLFW_KEY_A + (key_name[0] - 'A'); }
            else if (key_name[0] >= 'a' && key_name[0] <= 'z') { key = GLFW_KEY_A + (key_name[0] - 'a'); }
            else if (const char* p = strchr(char_names, key_name[0])) { key = char_keys[p - char_names]; }
        }
        // if (action == GLFW_PRESS) printf("key %d scancode %d name '%s'\n", key, scancode, key_name);
    #else
        IM_UNUSED(scancode);
    #endif
    return key;
}

FOUNDATION_STATIC ImGuiKey glfw_key_to_imgui_key(int key)
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

FOUNDATION_STATIC void glfw_key_callback(GLFWwindow*, int keycode, int scancode, int action, int mods)
{
    signal_thread();

    if (keycode == -1)
        return;

    if (action != GLFW_PRESS && action != GLFW_RELEASE)
        return;

    // Workaround: X11 does not include current pressed/released modifier key in 'mods' flags. https://github.com/glfw/glfw/issues/1630
    if (int keycode_to_mod = glfw_key_to_modifier(keycode))
        mods = (action == GLFW_PRESS) ? (mods | keycode_to_mod) : (mods & ~keycode_to_mod);
    glfw_update_key_modifiers(mods);

    keycode = glfw_translate_untranslated_key(keycode, scancode);

    ImGuiIO& io = ImGui::GetIO();
    ImGuiKey imgui_key = glfw_key_to_imgui_key(keycode);
    io.AddKeyEvent(imgui_key, (action == GLFW_PRESS));
    io.SetKeyEventNativeData(imgui_key, keycode, scancode); // To support legacy indexing (<1.87 user code)

    #if BUILD_DEVELOPMENT
    if (keycode == GLFW_KEY_F1 && action == GLFW_RELEASE)
        _show_stats = !_show_stats;
    #endif
}

FOUNDATION_STATIC void glfw_char_callback(GLFWwindow*, unsigned int c)
{
    ImGuiIO& io = ImGui::GetIO();
    if (c > 0 && c < 0x10000)
        io.AddInputCharacter((unsigned short)c);

    signal_thread();
}

FOUNDATION_STATIC void glfw_window_focus_callback(GLFWwindow* window, int focused)
{
    ImGuiIO& io = ImGui::GetIO();
    io.AddFocusEvent(focused != 0);
    signal_thread();

    //log_debugf(0, STRING_CONST("Application %s (%d)"), focused == 0 ? "unfocused" : "focused", focused);
}

FOUNDATION_STATIC void glfw_update_cursor_pos(GLFWwindow* window, double& x, double& y)
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

FOUNDATION_STATIC void glfw_set_cursor_pos_callback(GLFWwindow* window, double x, double y)
{
    glfw_update_cursor_pos(window, x, y);
    signal_thread();
}

FOUNDATION_STATIC void glfw_cursor_enter_callback(GLFWwindow* window, int entered)
{
    if (glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
        return;

    static double s_last_valid_mouse_pos[2] = {0, 0};

    ImGuiIO& io = ImGui::GetIO();
    if (entered)
    {
        glfw_update_cursor_pos(window, s_last_valid_mouse_pos[0], s_last_valid_mouse_pos[1]);
    }
    else if (!entered)
    {
        s_last_valid_mouse_pos[0] = io.MousePos.x;
        s_last_valid_mouse_pos[1] = io.MousePos.y;
        io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    }

    signal_thread();
}

FOUNDATION_STATIC void glfw_install_callbacks(GLFWwindow* window)
{
    glfwSetMouseButtonCallback(window, glfw_mouse_button_callback);
    glfwSetScrollCallback(window, glfw_scroll_callback);
    glfwSetKeyCallback(window, glfw_key_callback);
    glfwSetCharCallback(window, glfw_char_callback);
    glfwSetWindowFocusCallback(window, glfw_window_focus_callback);
    glfwSetCursorPosCallback(window, glfw_set_cursor_pos_callback);
    glfwSetCursorEnterCallback(window, glfw_cursor_enter_callback);
}

FOUNDATION_STATIC void glfw_shutdown()
{
    // Destroy GLFW mouse cursors
    for (auto& g_MouseCursor : _mouse_cursors)
        glfwDestroyCursor(g_MouseCursor);
    memset(_mouse_cursors, 0, sizeof(_mouse_cursors));

    glfwDestroyWindow(_glfw_window);
    glfwTerminate();
    
    _glfw_window = nullptr;
}

FOUNDATION_STATIC bool imgui_glfw_init(GLFWwindow* window, bool install_callbacks)
{
    _time = 0.0;
    _glfw_window = window;
    
    #if FOUNDATION_PLATFORM_WINDOWS
        _window_handle = glfwGetWin32Window(window);
    #elif FOUNDATION_PLATFORM_MACOS
        _window_handle = (void*)glfwGetCocoaWindow(window);
    #endif

    // Setup back-end capabilities flags
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;   // We can honor GetMouseCursor() values (optional)
    io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;    // We can honor io.WantSetMousePos requests (optional, rarely used)

    io.ClipboardUserData = window;
    io.SetClipboardTextFn = glfw_set_clipboard_text;
    io.GetClipboardTextFn = glfw_get_clipboard_text;
    ImGui::GetMainViewport()->PlatformHandleRaw = (void*)_window_handle;

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
        glfw_install_callbacks(window);

    return true;
}

FOUNDATION_STATIC void* imgui_allocate(size_t sz, void* user_data)
{
    return memory_allocate(HASH_IMGUI, sz, sizeof(float), MEMORY_PERSISTENT);
}

FOUNDATION_STATIC void imgui_deallocate(void* ptr, void* user_data)
{
    memory_deallocate(ptr);
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

            io.Fonts->AddFontFromMemoryTTF(
                bytes, dwSize,
                size_pixels,
                font_cfg,
                glyph_ranges
            );

            UnlockResource(hMemory);
            return true;
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
        bool success = io.Fonts->AddFontFromMemoryTTF(
            bytes, (int)file_size,
            size_pixels,
            font_cfg,
            glyph_ranges
        );
        
        if (success)
            return true;
    #endif

    return false;
}

FOUNDATION_STATIC void imgui_update_mouse_cursor(GLFWwindow* window)
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
            glfw_update_cursor_pos(window, mouse_x, mouse_y);

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

FOUNDATION_STATIC void imgui_new_frame(GLFWwindow* window, int width, int height)
{
    PERFORMANCE_TRACKER("imgui_new_frame");

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

FOUNDATION_STATIC bool imgui_load_main_font(float xscale = 1.0f)
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

FOUNDATION_STATIC bool imgui_load_material_design_font(float xscale = 1.0f)
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

FOUNDATION_STATIC void imgui_shutdown()
{
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

// This is the main rendering function that you have to implement and call after
// ImGui::Render(). Pass ImGui::GetDrawData() to this function.
// Note: If text or lines are blurry when integrating ImGui into your engine,
// in your Render function, try translating your projection matrix by
// (0.5f,0.5f) or (0.375f,0.375f)
FOUNDATION_STATIC void bgfx_render_draw_lists(ImDrawData* draw_data, int fb_width, int fb_height)
{
    if (fb_width <= 0 || fb_height <= 0)
        return;

    ImGuiIO& io = ImGui::GetIO();

    // Setup render state: alpha-blending enabled, no face culling,
    // no depth testing, scissor enabled
    uint64_t state =
        BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_MSAA |
        BGFX_STATE_BLEND_FUNC(
            BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

    bgfx::setViewName(_bgfx_imgui_view, "UI");
    bgfx::setViewMode(_bgfx_imgui_view, bgfx::ViewMode::Sequential);

    // Setup viewport, orthographic projection matrix
    float ortho[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    bx::mtxOrtho(ortho, 0.0f, (float)fb_width, (float)fb_height, 0.0f, -1.0f, 1000.0f, 0.0f, caps->homogeneousDepth);
    bgfx::setViewTransform(_bgfx_imgui_view, NULL, ortho);
    bgfx::setViewRect(_bgfx_imgui_view, 0, 0, fb_width, fb_height);

    // Render command lists
    for (int n = 0; n < draw_data->CmdListsCount; n++) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;

        uint32_t numVertices = (uint32_t)cmd_list->VtxBuffer.size();
        uint32_t numIndices = (uint32_t)cmd_list->IdxBuffer.size();

        if (numIndices != 0 && numVertices != 0)
        {
            if ((numVertices != bgfx::getAvailTransientVertexBuffer(
                numVertices, _bgfx_imgui_vertex_layout)) ||
                (numIndices != bgfx::getAvailTransientIndexBuffer(numIndices))) {
                // not enough space in transient buffer, quit drawing the rest...
                break;
            }

            bgfx::allocTransientVertexBuffer(&tvb, numVertices, _bgfx_imgui_vertex_layout);
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
                bgfx::setTexture(0, _bgfx_imgui_attrib_location_tex, texture);
                bgfx::setVertexBuffer(0, &tvb, 0, numVertices);
                bgfx::setIndexBuffer(&tib, pcmd->IdxOffset, pcmd->ElemCount);
                bgfx::submit(_bgfx_imgui_view, _bgfx_imgui_shader_handle);
            }
        }
    }
}

FOUNDATION_STATIC bool bgfx_create_fonts_texture(GLFWwindow* window)
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();

    float xscale = 1.0f, yscale = 1.0f;
    GLFWmonitor* monitor = glfw_find_window_monitor(window);
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
    _bgfx_imgui_font_texture = bgfx::createTexture2D(
        (uint16_t)width, (uint16_t)height, false, 1, bgfx::TextureFormat::BGRA8,
        0, bgfx::copy(pixels, width * height * 4));

    // Store our identifier
    io.Fonts->SetTexID((void*)(intptr_t)_bgfx_imgui_font_texture.idx);

    return true;
}

FOUNDATION_STATIC bool bgfx_create_device_objects(GLFWwindow* window)
{
    bgfx::RendererType::Enum type = bgfx::getRendererType();
    _bgfx_imgui_shader_handle = bgfx::createProgram(
        bgfx::createEmbeddedShader(_bgfx_imgui_embedded_shaders, type, "vs_ocornut_imgui"),
        bgfx::createEmbeddedShader(_bgfx_imgui_embedded_shaders, type, "fs_ocornut_imgui"),
        true);

    _bgfx_imgui_vertex_layout.begin()
        .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();

    _bgfx_imgui_attrib_location_tex =
        bgfx::createUniform("g_AttribLocationTex", bgfx::UniformType::Sampler);

    return bgfx_create_fonts_texture(window);
}

FOUNDATION_STATIC void bgfx_invalidate_device_objects()
{
    if (bgfx::isValid(_bgfx_imgui_attrib_location_tex))
        bgfx::destroy(_bgfx_imgui_attrib_location_tex);
    if (bgfx::isValid(_bgfx_imgui_shader_handle))
        bgfx::destroy(_bgfx_imgui_shader_handle);

    if (bgfx::isValid(_bgfx_imgui_font_texture))
    {
        bgfx::destroy(_bgfx_imgui_font_texture);
        ImGui::GetIO().Fonts->TexID = 0;
        _bgfx_imgui_font_texture.idx = bgfx::kInvalidHandle;
    }
}

FOUNDATION_STATIC void bgfx_init_view(int imgui_view)
{
    _bgfx_imgui_view = (uint8_t)(imgui_view & 0xff);

    // Set view 0 to the same dimensions as the window and to clear the color buffer.
    const bgfx::ViewId kClearView = 0;
    bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR);
    bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);
}

FOUNDATION_STATIC void bgfx_shutdown()
{
    bgfx_invalidate_device_objects();
    bgfx::shutdown();
}

FOUNDATION_STATIC void bgfx_new_frame(GLFWwindow* window, int width, int height)
{
    PERFORMANCE_TRACKER("bgfx_new_frame");
    if (!isValid(_bgfx_imgui_font_texture)) {
        bgfx_create_device_objects(window);
    }

    const bgfx::ViewId kClearView = 0;
    static uint32_t bWidth = 0, bHeight = 0;
    if (width != bWidth || height != bHeight)
    {
        bWidth = width; bHeight = height;
        bgfx::reset(bWidth, bHeight, 
        #if BUILD_DEVELOPMENT
            _run_tests ? BGFX_RESET_NONE : 
        #endif
            (BGFX_RESET_VSYNC | BGFX_RESET_HIDPI));
    }

    bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH);
    bgfx::setViewClear(_bgfx_imgui_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH);

    bgfx::touch(kClearView);
    bgfx::touch(_bgfx_imgui_view);
    
    #if BUILD_DEVELOPMENT
    if (BUILD_DEBUG && !_run_tests)
    {
        bgfx::dbgTextClear();
        bgfx::setDebug(!_show_stats ? BGFX_DEBUG_NONE : BGFX_DEBUG_STATS);
    }
    #endif
}

FOUNDATION_STATIC void setup_bgfx(GLFWwindow* window)
{
    if (!environment_command_line_arg("render-thread"))
    {
        // Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
        // Most graphics APIs must be used on the same thread that created the window.
        bgfx::renderFrame();
    }
    
    bgfx::Init bgfxInit;
    bgfxInit.type = bgfx::RendererType::Count; // Automatically choose a renderer.
    
    #if BUILD_ENABLE_MEMORY_TRACKER
    struct BgfxAllocatorkHandler : bx::AllocatorI
    {
        BgfxAllocatorkHandler()
        {
        }
        
        virtual ~BgfxAllocatorkHandler() {};

        virtual void* realloc(void* _ptr, size_t _size, size_t _align, const char* _file, uint32_t _line) override
        {
            if (_ptr)
            {
                if (_size != 0)
                {
                    memory_context_push(HASH_BGFX);
                    size_t oldsize = memory_size(_ptr);
                    void* bgfx_mem = memory_reallocate(_ptr, _size, to_uint(_align), oldsize, MEMORY_PERSISTENT);
                    memory_context_pop();
                    return bgfx_mem;
                }
                else
                {
                    memory_deallocate(_ptr);
                    return nullptr;
                }
            }

            if (_size == 0)
                return nullptr;
            
            return memory_allocate(HASH_BGFX, _size, to_uint(_align), MEMORY_PERSISTENT);
        }
    };
    static BgfxAllocatorkHandler bgfx_allocator_handler{};
    bgfxInit.allocator = &bgfx_allocator_handler;
    #endif
    
    #if BUILD_DEVELOPMENT
    struct BgfxCallbackHandler : bgfx::CallbackI
    {
        bool ignore_logs = true;
        BgfxCallbackHandler()
        {
            if (environment_command_line_arg("verbose"))
            {
                ignore_logs = false;
                ignore_logs = environment_command_line_arg("bgfx-ignore-logs");
            }
        }
        virtual ~BgfxCallbackHandler(){};

        virtual void fatal(const char* _filePath, uint16_t _line, bgfx::Fatal::Enum _code, const char* _str) override
        {
            log_errorf(HASH_BGFX, ERROR_INTERNAL_FAILURE, STRING_CONST("BGFX Failure (%d): %s\n\t%s(%hu)"), _code, _str, _filePath, _line);
            FOUNDATION_ASSERT_FAIL(_str);
            process_exit(_code);
        }

        virtual void traceVargs(const char* _filePath, uint16_t _line, const char* _format, va_list _argList) override
        {
            if (ignore_logs)
                return;
            string_t trace_msg = string_allocate_vformat(_format, string_length(_format), _argList);
            log_infof(HASH_BGFX, STRING_CONST("%.*s"), (int)trace_msg.length-1, trace_msg.str);
            string_deallocate(trace_msg.str);
        }

        virtual void profilerBegin(const char* _name, uint32_t _abgr, const char* _filePath, uint16_t _line) override
        {
            FOUNDATION_ASSERT_FAIL("TODO: Profiler region begin");
        }

        virtual void profilerBeginLiteral(const char* _name, uint32_t _abgr, const char* _filePath, uint16_t _line) override
        {
            FOUNDATION_ASSERT_FAIL("TODO: Profiler region begin with string literal name");
        }

        virtual void profilerEnd() override
        {
            FOUNDATION_ASSERT_FAIL("Profiler region end");
        }

        virtual uint32_t cacheReadSize(uint64_t _id) override
        {
            FOUNDATION_ASSERT_FAIL("TODO: cacheReadSize");
            return 0;
        }

        virtual bool cacheRead(uint64_t _id, void* _data, uint32_t _size) override
        {
            FOUNDATION_ASSERT_FAIL("TODO: cacheRead");
            return false;
        }

        virtual void cacheWrite(uint64_t _id, const void* _data, uint32_t _size) override
        {
            FOUNDATION_ASSERT_FAIL("TODO: cacheWrite");
        }

        virtual void screenShot(const char* _filePath, uint32_t _width, uint32_t _height, uint32_t _pitch, const void* _data, uint32_t _size, bool _yflip) override
        {
            FOUNDATION_ASSERT_FAIL("TODO: screenShot");
        }

        virtual void captureBegin(uint32_t _width, uint32_t _height, uint32_t _pitch, bgfx::TextureFormat::Enum _format, bool _yflip) override
        {
            FOUNDATION_ASSERT_FAIL("TODO: captureBegin");
        }

        virtual void captureEnd() override
        {
            FOUNDATION_ASSERT_FAIL("TODO: captureEnd");
        }

        virtual void captureFrame(const void* _data, uint32_t _size) override
        {
            FOUNDATION_ASSERT_FAIL("TODO: captureFrame");
        }
    };
    static BgfxCallbackHandler bgfx_callback_handler{};
    bgfxInit.callback = &bgfx_callback_handler;
    #endif

    #if FOUNDATION_PLATFORM_LINUX
        bgfxInit.platformData.ndt = glfwGetX11Display();
        bgfxInit.platformData.nwh = (void*)(uintptr_t)glfwGetX11Window(window);
    #elif FOUNDATION_PLATFORM_MACOS
        bgfxInit.type = bgfx::RendererType::Metal;
        bgfxInit.platformData.nwh = glfwGetCocoaWindow(window);
    #elif FOUNDATION_PLATFORM_WINDOWS
        bgfxInit.platformData.nwh = glfwGetWin32Window(window);
    #endif

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    bgfxInit.resolution.width = (uint32_t)width;
    bgfxInit.resolution.height = (uint32_t)height;
    bgfxInit.resolution.reset = 
    #if BUILD_DEVELOPMENT
        _run_tests ? BGFX_RESET_NONE : 
    #endif
        (BGFX_RESET_VSYNC | BGFX_RESET_HIDPI);
    
    log_infof(HASH_BGFX, STRING_CONST("Initializing BGFX (%d)..."), (int)bgfxInit.type);
    if (!bgfx::init(bgfxInit))
        log_errorf(HASH_BGFX, ERROR_EXCEPTION, STRING_CONST("Failed to initialize BGFX"));

    bgfx_init_view(1);
}

FOUNDATION_STATIC void setup_imgui(GLFWwindow* window)
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

FOUNDATION_STATIC void glfw_log_error(int error, const char* description)
{
    log_errorf(0, ERROR_EXCEPTION, STRING_CONST("GLFW Error %d: %s"), error, description);
}

FOUNDATION_STATIC GLFWwindow* setup_main_window()
{
    extern const char* app_title();

    // Setup window
    glfwSetErrorCallback(glfw_log_error);
    if (!glfwInit())
        return nullptr;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    
    #if FOUNDATION_PLATFORM_MACOS
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_FALSE);
        glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);
    #else
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    #endif

    GLFWwindow* window = glfw_create_window_geometry(app_title());
    if (window == nullptr)
        return nullptr;

    const application_t* app = environment_application();
    string_const_t version_string = string_from_version_static(app->version);
    glfwSetWindowTitle(window, string_format_static_const("%s v.%.*s", app_title(), STRING_FORMAT(version_string)));
    
    return window;
}

FOUNDATION_STATIC void setup_main_window_icon(GLFWwindow* window)
{
    #if FOUNDATION_PLATFORM_WINDOWS
        HWND window_handle = glfwGetWin32Window(window);
        HINSTANCE module_handle = ::GetModuleHandle(nullptr);
        HANDLE big_icon = LoadImageA(module_handle, MAKEINTRESOURCEA(GLFW_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
        HANDLE small_icon = LoadImageA(module_handle, MAKEINTRESOURCEA(GLFW_ICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        if (big_icon)
            SendMessage(window_handle, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big_icon));
        if (small_icon)
            SendMessage(window_handle, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
    #endif
}

FOUNDATION_STATIC void main_handle_debug_break()
{
    if (!environment_command_line_arg("debug-break"))
        return;
    
    if (system_debugger_attached())
        exception_raise_debug_break();
    else
    {
        log_warn(0, WARNING_STANDARD, STRING_CONST("Waiting for debugger to attach..."));
        
        static bool debug_break_continue = false;
        static dispatcher_thread_id wait_thread_id = 0;
        if (main_is_graphical_mode())
        {
            wait_thread_id = dispatch_fire([]()
            {
                system_message_box(STRING_CONST("Attach Debugger (Debug Break)"),
                    STRING_CONST("You can attach debugger now and press OK to continue..."), false);
                wait_thread_id = 0;
                debug_break_continue = true;
            });
        }

        while (!system_debugger_attached() && !_process_should_exit && !debug_break_continue)
            thread_sleep(1000);
        if (wait_thread_id)
            dispatcher_thread_stop(wait_thread_id);
    }
}

extern int main_initialize()
{
    extern int app_initialize(GLFWwindow * window);
    extern void app_configure(foundation_config_t & config, application_t & application);

    // Use default values for foundation config
    application_t application;
    foundation_config_t config;
    memset(&config, 0, sizeof config);
    memset(&application, 0, sizeof application);

    #if BUILD_ENABLE_MEMORY_TRACKER
        memory_set_tracker(memory_tracker_local());
    #endif
    
    app_configure(config, application);

    int init_result = foundation_initialize(memory_system_malloc(), application, config);
    if (init_result)
        return init_result;

    #if FOUNDATION_PLATFORM_WINDOWS
        log_enable_stdout(process_redirect_io_to_console() || environment_command_line_arg("build-machine"));

        // Always set executable directory as the initial working directory.
        //string_const_t exe_dir = environment_executable_directory();
        //environment_set_current_working_directory(STRING_ARGS(exe_dir));
    #endif

    const bool run_eval_mode = environment_command_line_arg("eval");
    
    #if BUILD_DEVELOPMENT
    _run_tests = environment_command_line_arg("run-tests");
    #endif

    if (environment_command_line_arg("debug") || environment_command_line_arg("verbose"))
        log_set_suppress(0, ERRORLEVEL_NONE);
    else
    {
        log_set_suppress(0, ERRORLEVEL_DEBUG);
     
        if (environment_command_line_arg("X"))
        {
            log_enable_prefix(false);
            log_enable_stdout(true);
        }
    }

    // Check if running batch mode (which is incompatible with running tests)
    _batch_mode = !_run_tests && (environment_command_line_arg("batch-mode") || run_eval_mode);

    dispatcher_initialize();
    main_handle_debug_break();

    GLFWwindow* window = nullptr;
    if (main_is_graphical_mode())
    {
        GLFWwindow* window = setup_main_window();
        if (!window)
        {
            log_error(0, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Fail to create main window context."));
            return ERROR_SYSTEM_CALL_FAIL;
        }

        setup_main_window_icon(window);
        setup_bgfx(window);
        setup_imgui(window);
    }

    return app_initialize(window);
}

extern GLFWwindow* main_window()
{
    return _glfw_window;
}

extern bool main_is_batch_mode()
{
    return _batch_mode;
}

extern bool main_is_graphical_mode()
{
    return !_batch_mode;
}

extern bool main_is_interactive_mode(bool exclude_debugger /*= false*/)
{
    if (_batch_mode)
        return false;
    if (_run_tests)
        return false;
    if (exclude_debugger && system_debugger_attached())
        return false;
    return true;
}

extern bool main_is_running_tests()
{
#if BUILD_DEVELOPMENT
    return _run_tests;
#else
    return false;
#endif
}

extern double main_tick_elapsed_time_ms()
{
#if BUILD_ENABLE_PROFILE
    return _smooth_elapsed_time_ms;
#else
    return 0;
#endif
}

FOUNDATION_STATIC void main_process_system_events(GLFWwindow* window)
{
    system_process_events();

    // Process all pending events in the event stream
    event_t* event = nullptr;
    event_stream_t* stream = system_event_stream();
    event_block_t* block = event_stream_process(stream);
    while ((event = event_next(block, event))) 
    {
        switch (event->id) 
        {
        case FOUNDATIONEVENT_START:
            break;

        case FOUNDATIONEVENT_TERMINATE:
            _process_should_exit = true;
            if (window)
                glfwSetWindowShouldClose(window, 1);
            break;

        case FOUNDATIONEVENT_FOCUS_GAIN:
        case FOUNDATIONEVENT_FOCUS_LOST:
            break;

        default:
            break;
        }
    }
}

extern void main_update(GLFWwindow* window, const app_update_handler_t& update)
{
    PERFORMANCE_TRACKER("main_update");

    session_update();
    dispatcher_update();

    main_process_system_events(window);

    // Update application
    if (update)
        update(window);
}

extern void main_render(GLFWwindow* window, const app_render_handler_t& render, const app_render_handler_t& begin, const app_render_handler_t& end)
{
    PERFORMANCE_TRACKER("main_render");

    int frame_width = 1, frame_height = 1;
    const bool graphical_mode = !main_is_batch_mode();
    
    if (window)
    {
        glfwGetFramebufferSize(window, &frame_width, &frame_height);

        // Prepare next frame
        bgfx_new_frame(window, frame_width, frame_height);
        imgui_new_frame(window, frame_width, frame_height);

        if (begin)
            begin(window, frame_width, frame_height);
    }

    // Render application
    if (window && render)
    {
        PERFORMANCE_TRACKER("app_render");
        render(window, frame_width, frame_height);
    }
    
    if (window)
    {
        PERFORMANCE_TRACKER("imgui_render");

        // Render IMGUI frame
        ImGui::Render();
    }

    if (window && end)
        end(window, frame_width, frame_height);

    // Render everything
    if (window)
    {
        PERFORMANCE_TRACKER("bgfx_render_draw_lists");
        bgfx_render_draw_lists(ImGui::GetDrawData(), frame_width, frame_height);
    }

    if (window)
    {
        PERFORMANCE_TRACKER("bgfx_frame");
        bgfx::frame();
    }
}

extern void main_tick(GLFWwindow* window)
{
    PERFORMANCE_TRACKER("main_tick");
    extern void app_update(GLFWwindow* window);
    extern void app_render(GLFWwindow* window, int display_w, int display_h);

    main_update(window, app_update);

    if (window)
        main_render(window, app_render, nullptr, nullptr);
}

extern bool main_poll(GLFWwindow* window)
{
    PERFORMANCE_TRACKER("main_poll");

    if (window)
        glfwPollEvents();
    dispatcher_poll(window);

    return window == nullptr || !glfwWindowShouldClose(window);
}

extern int main_run(void* context)
{
    GLFWwindow* current_window = _glfw_window;

    #if BUILD_DEVELOPMENT
    extern int main_tests(void* context, GLFWwindow* window);
    if (_run_tests)
        return main_tests(context, current_window);
    #endif

    _process_should_exit = environment_command_line_arg("exit");

    uint64_t frame_counter = 1;
    while (main_poll(current_window))
    {
        tick_t start_tick = time_current();
        main_tick(current_window);

        tick_t elapsed_ticks = time_diff(start_tick, time_current());

        #if BUILD_ENABLE_PROFILE
        static unsigned index = 0;
        static double elapsed_times[60] = { 0.0 };
        elapsed_times[index++ % ARRAY_COUNT(elapsed_times)] = time_ticks_to_milliseconds(elapsed_ticks);
        _smooth_elapsed_time_ms = math_average(elapsed_times, ARRAY_COUNT(elapsed_times));
        #endif
        
        if (_process_should_exit)
            return 0;

        profile_end_frame(frame_counter++);
    }

    return 0;
}

extern void main_finalize()
{
    extern void app_shutdown();
    
    if (_glfw_window && main_is_interactive_mode())
        glfw_save_window_geometry(_glfw_window);

    app_shutdown();
    dispatcher_shutdown();

    if (main_is_graphical_mode())
    {
        bgfx_shutdown();
        imgui_shutdown();
        glfw_shutdown();
    }

    if (log_stdout())
        process_release_console();

    foundation_finalize();
}
