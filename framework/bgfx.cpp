/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#if BUILD_APPLICATION

#include "bgfx.h"

#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/session.h>
#include <framework/profiler.h>
#include <framework/common.h>

#include <foundation/process.h>

#include <bx/math.h>
#include <bx/file.h>

#include <bgfx/platform.h>
#include <bgfx/embedded_shader.h>

#include <bimg/bimg.h>

#include <imgui/fs_imgui.bin.h>
#include <imgui/vs_imgui.bin.h>

#define HASH_BGFX static_hash_string("bgfx", 4, 0x14900654424ff61bULL)

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

struct BgfxCallbackHandler : bgfx::CallbackI
{
    bool ignore_logs = true;
    BgfxCallbackHandler()
    {
        if (environment_argument("verbose"))
        {
            ignore_logs = environment_argument("bgfx-ignore-logs");
        }
    }
    virtual ~BgfxCallbackHandler() {};

    virtual void fatal(const char* _filePath, uint16_t _line, bgfx::Fatal::Enum _code, const char* _str) override
    {
        log_panicf(HASH_BGFX, ERROR_INTERNAL_FAILURE, STRING_CONST("BGFX Failure (%d): %s\n\t%s(%hu)"), _code, _str, _filePath, _line);
    }

    virtual void traceVargs(const char* _filePath, uint16_t _line, const char* _format, va_list _argList) override
    {
        #if BUILD_DEVELOPMENT
        if (ignore_logs)
            return;
        static thread_local char trace_buffer[4096];
        const size_t fmt_length = string_length(_format);
        string_t trace_msg = string_vformat(STRING_BUFFER(trace_buffer), _format, fmt_length, _argList);
        if (trace_msg.length > 0 && trace_msg.str[trace_msg.length - 1] == '\n')
            --trace_msg.length;
        log_debug(HASH_BGFX, STRING_ARGS(trace_msg));
        #endif
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
        bx::FileWriter writer;
        if (bx::open(&writer, _filePath))
        {
            bimg::imageWritePng(&writer, _width, _height, _pitch, _data, bimg::TextureFormat::BGRA8, _yflip);
            bx::close(&writer);
        }
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

FOUNDATION_STATIC bool bgfx_create_fonts_texture(GLFWwindow* window)
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();

    float xscale = 1.0f, yscale = 1.0f;
    GLFWmonitor* monitor = glfw_find_window_monitor(window);
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

    _bgfx_imgui_attrib_location_tex = bgfx::createUniform("g_AttribLocationTex", bgfx::UniformType::Sampler);

    return bgfx_create_fonts_texture(window);
}

FOUNDATION_STATIC void bgfx_invalidate_device_objects()
{
    if (bgfx::isValid(_bgfx_imgui_shader_handle))
        bgfx::destroy(_bgfx_imgui_shader_handle);

    if (bgfx::isValid(_bgfx_imgui_attrib_location_tex))
        bgfx::destroy(_bgfx_imgui_attrib_location_tex);

    if (bgfx::isValid(_bgfx_imgui_font_texture))
    {
        ImGui::GetIO().Fonts->TexID = 0;
        bgfx::destroy(_bgfx_imgui_font_texture);
        _bgfx_imgui_font_texture.idx = bgfx::kInvalidHandle;
    }
}

void bgfx_init_view(int imgui_view)
{
    _bgfx_imgui_view = (uint8_t)(imgui_view & 0xff);

    // Set view 0 to the same dimensions as the window and to clear the color buffer.
    const bgfx::ViewId kClearView = 0;
    bgfx::setViewClear(kClearView, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH);
    bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);
}

void bgfx_shutdown()
{
    bgfx_invalidate_device_objects();
    bgfx::shutdown();
}

void bgfx_new_frame(GLFWwindow* window, int width, int height)
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
        bgfx::reset(bWidth, bHeight, BGFX_RESET_NONE);
    }

    bgfx::touch(kClearView);
}

// This is the main rendering function that you have to implement and call after
// ImGui::Render(). Pass ImGui::GetDrawData() to this function.
// Note: If text or lines are blurry when integrating ImGui into your engine,
// in your Render function, try translating your projection matrix by
// (0.5f,0.5f) or (0.375f,0.375f)
void bgfx_render_draw_lists(ImDrawData* draw_data, int fb_width, int fb_height)
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
    bgfx::setViewClear(_bgfx_imgui_view, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH);

    // Setup viewport, orthographic projection matrix
    float ortho[16];
    const bgfx::Caps* caps = bgfx::getCaps();
    bx::mtxOrtho(ortho, 0.0f, (float)fb_width, (float)fb_height, 0.0f, -1.0f, 1000.0f, 0.0f, caps->homogeneousDepth);
    bgfx::setViewTransform(_bgfx_imgui_view, NULL, ortho);
    bgfx::setViewRect(_bgfx_imgui_view, 0, 0, fb_width, fb_height);

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
            if ((numVertices != bgfx::getAvailTransientVertexBuffer(numVertices, _bgfx_imgui_vertex_layout)) ||
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

bx::AllocatorI* bgfx_system_allocator()
{
    static BgfxAllocatorkHandler bgfx_allocator_handler{};
    return &bgfx_allocator_handler;
}

bgfx::CallbackI* bgfx_system_callback_handler()
{
    static BgfxCallbackHandler bgfx_callback_handler{};
    return &bgfx_callback_handler;
}

void bgfx_initialize(GLFWwindow* window)
{
    if (!FOUNDATION_PLATFORM_WINDOWS || !environment_argument("render-thread"))
    {
        // Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
        // Most graphics APIs must be used on the same thread that created the window.
        bgfx::renderFrame();
    }
    
    bgfx::Init bgfxInit;
    bgfxInit.type = bgfx::RendererType::Count; // Automatically choose a renderer.
    
    bgfxInit.allocator = bgfx_system_allocator();
    bgfxInit.callback = bgfx_system_callback_handler();

    #if FOUNDATION_PLATFORM_LINUX
        bgfxInit.platformData.ndt = glfwGetX11Display();
    #elif FOUNDATION_PLATFORM_MACOS
        bgfxInit.type = bgfx::RendererType::Metal;
    #elif FOUNDATION_PLATFORM_WINDOWS
        bgfxInit.type = bgfx::RendererType::Direct3D11;
    #endif
    bgfxInit.platformData.nwh = glfw_platform_window_handle(window);

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);
    bgfxInit.resolution.width = (uint32_t)width;
    bgfxInit.resolution.height = (uint32_t)height;
    bgfxInit.resolution.reset = main_is_running_tests() ? BGFX_RESET_NONE : (BGFX_RESET_VSYNC | BGFX_RESET_HIDPI);
    bgfxInit.debug = false;
    bgfxInit.profile = false;
    
    log_infof(HASH_BGFX, STRING_CONST("Initializing BGFX (%d)..."), (int)bgfxInit.type);
    if (!bgfx::init(bgfxInit))
        log_errorf(HASH_BGFX, ERROR_EXCEPTION, STRING_CONST("Failed to initialize BGFX"));

    bgfx_init_view(1);
}

#endif // BUILD_APPLICATION
