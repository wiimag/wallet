/*
 * Copyright 2023 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 * 
 * The render pad is used to test various rendering code.
 */

#define ENABLE_RENDER_PAD 0

#if ENABLE_RENDER_PAD && !BUILD_DEPLOY

#include "render_pad.h"

#include "butils.h"

#include "framework/common.h"
#include "framework/service.h"
#include "framework/imgui_utils.h"
#include "framework/tabs.h"
#include "framework/session.h"

#include <bx/math.h>
#include <bgfx/bgfx.h>

#include <imgui/imgui.h>
#include <imgui/ImGuizmo.h>

#if FOUNDATION_PLATFORM_WINDOWS
#include <omp.h>
#endif

#define HASH_RENDER_PAD static_hash_string("render_pad", 10, 0xa3bded1790392649ULL)

static struct render_pad_case_t {
    

} _test_case{};

FOUNDATION_STATIC void render_pad_setup_case(render_pad_case_t* pad, ...)
{
    // ...
}

FOUNDATION_STATIC void render_pad_case_toolbar(render_pad_case_t* pad, float width)
{
    // ...
}

FOUNDATION_STATIC bool render_pad_case_selector(render_pad_case_t* pad, float space_left)
{
    // ...
    return false;
}

FOUNDATION_STATIC void render_pad_case(render_pad_case_t* pad)
{
    const float space_left = ImGui::GetContentRegionAvail().x;
    render_pad_case_selector(pad, space_left);
    render_pad_case_toolbar(pad, space_left);
    
    // ...
}

FOUNDATION_STATIC void render_pad_tab()
{
    render_pad_case(&_test_case);
}

FOUNDATION_STATIC void render_pad()
{
    tab_draw(ICON_MD_GAMEPAD " Render Pad ", nullptr, ImGuiTabItemFlags_Trailing, render_pad_tab);
}

FOUNDATION_STATIC void render_pad_initialize()
{
    service_register_tabs(HASH_RENDER_PAD, render_pad);
}

FOUNDATION_STATIC void render_pad_shutdown()
{
}

DEFINE_SERVICE(RENDER_PAD, render_pad_initialize, render_pad_shutdown, SERVICE_PRIORITY_TESTS);

#endif
