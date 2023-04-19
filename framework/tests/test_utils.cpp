/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_TESTS

#include "test_utils.h"

#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/module.h>
#include <framework/dispatcher.h>
#include <framework/array.h>

#include <foundation/hashstrings.h>

#include <doctest/doctest.h>

extern ImGuiTestItem* _test_items;

void CLICK_UI(const char* label)
{
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    FOUNDATION_ASSERT(ctx && ctx->Initialized);

    const ImGuiID item_id = ImGuiTestEngine_GetID(ctx, label);
    ctx->NavActivateId = item_id;
    ctx->NavActivateDownId = item_id;
}

void REQUIRE_UI(const char* label)
{
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    const ImGuiTestItem* ti = ImGuiTestEngine_FindItemByLabel(ctx, label);

    if (ti)
    {
        CHECK_MESSAGE(ti->id, "UI Checking item `", doctest::String(label), "`");
    }
    else
    {
        FAIL("UI Item `", doctest::String(label), "` does not exists");
    }    
}

void REQUIRE_UI_FALSE(const char* label)
{
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    const ImGuiTestItem* ti = ImGuiTestEngine_FindItemByLabel(ctx, label);

    if (ti)
        FAIL("UI Item `", doctest::String(label), "` exists");
}

void REQUIRE_WAIT(bool* watch_var, const double timeout_seconds /*= 5.0*/)
{
    REQUIRE_NE(watch_var, nullptr);
    tick_t s = time_current();
    while (!*watch_var && time_elapsed(s) < timeout_seconds)
        signal_thread();
    CHECK_LE(time_elapsed(s), timeout_seconds);
    REQUIRE(*watch_var);
}

void TEST_CLEAR_FRAME()
{
    if (_test_items)
    {
        foreach(ti, _test_items)
            string_deallocate(ti->label.str);
        array_deallocate(_test_items);
    }
}

void TEST_RENDER_FRAME(const function<void()>& render_callback, const function<void()>& test_event_callback /*= nullptr*/)
{
    FOUNDATION_ASSERT(render_callback);

    memory_context_push(HASH_TEST);

    GLFWwindow* test_window = main_test_window();
    if (main_poll(test_window))
    {
        const function<void()>* p_render_callback = &render_callback;
        const function<void()>* p_test_event_callback = test_event_callback.valid() ? &test_event_callback : nullptr;

        glfwShowWindow(test_window);

        main_update(test_window, nullptr);

        main_render(test_window, [p_render_callback, p_test_event_callback](GLFWwindow* window, int frame_width, int frame_height)
        {
            FOUNDATION_UNUSED(window);

            ImGuiContext* ctx = ImGui::GetCurrentContext();
            FOUNDATION_ASSERT(ctx);

            ImGui::SetWindowPos(ctx->CurrentWindow, ImVec2(0, 0));
            ImGui::SetWindowSize(ctx->CurrentWindow, ImVec2(frame_width, frame_height));
            ImGui::PushClipRect(ImVec2(-FLT_MAX, -FLT_MAX), ImVec2 (+FLT_MAX, +FLT_MAX), false);
            ctx->TestEngineHookItems = true;
            if (p_test_event_callback)
                p_test_event_callback->invoke();
            p_render_callback->invoke();
            ctx->TestEngineHookItems = false;
            ImGui::PopClipRect();
        }, nullptr, nullptr);

        glfwHideWindow(test_window);
    }
    else
    {
        INFO("Failed to poll events for testing");
    }

    memory_context_pop();
}

FOUNDATION_STATIC void test_utils_initialize()
{
    
}

FOUNDATION_STATIC void test_utils_shutdown()
{
    TEST_CLEAR_FRAME();
}

DEFINE_MODULE(TEST, test_utils_initialize, test_utils_shutdown, MODULE_PRIORITY_TESTS-1);

#endif
