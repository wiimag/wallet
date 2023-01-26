/*
 * Copyright 2023 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#include "test_utils.h"

#include <framework/imgui.h>
#include <framework/config.h>
#include <framework/common.h>

#include <doctest/doctest.h>

#include <bgfx/bgfx.h>

TEST_SUITE("IMGUI")
{
    TEST_CASE("BGFX Setup")
    {
        auto renderer_type = bgfx::getRendererType();
        MESSAGE("Renderer ", doctest::String(bgfx::getRendererName(renderer_type)));
        #if FOUNDATION_PLATFORM_WINDOWS
            REQUIRE_GE(renderer_type, bgfx::RendererType::Direct3D11);
            REQUIRE_LE(renderer_type, bgfx::RendererType::Direct3D12);
        #elif FOUNDATION_PLATFORM_MACOS
            REQUIRE_EQ(renderer_type, bgfx::RendererType::Metal);
        #endif
    }

    TEST_CASE("Button Clicked" * doctest::may_fail(true))
    {
        bool clicked = false;
        TEST_RENDER_FRAME([&clicked]()
        {
            if (ImGui::Button("Test Me"))
                clicked = true;
        }, []()
        {
            CLICK_UI("Test Me");
        });

        REQUIRE_UI("Test Me");
        CHECK(clicked);
    }

    TEST_CASE("Button Not Clicked" * doctest::may_fail(true))
    {
        bool clicked = false;
        TEST_RENDER_FRAME([&clicked]()
        {
            if (ImGui::Button("Do not test me"))
                clicked = true;
        });

        REQUIRE_UI("Do not test me");
        REQUIRE_UI_FALSE("Test Me");
        CHECK_FALSE(clicked);
    }
}
