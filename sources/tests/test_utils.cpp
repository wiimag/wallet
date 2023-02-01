/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT

#include "test_utils.h"

#include <framework/imgui.h>
#include <framework/service.h>

#include <foundation/hashstrings.h>

#include <doctest/doctest.h>

struct ImGuiTestItem
{
    ImGuiID id;
    ImRect bb;
    string_t label;
    ImGuiItemStatusFlags flags;
};

ImGuiTestItem* _test_items = nullptr;

/// <summary>
/// Called by IMGUI to register an item that was drawn.
/// </summary>
/// <param name="ctx">IMGUI context</param>
/// <param name="bb">Bounding box of the item being drawn</param>
/// <param name="id">Unique ID of the item being drawn</param>
extern void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, const ImRect& bb, ImGuiID id)
{
    ImGuiTestItem ti;
    ti.id = id;
    ti.bb = bb;
    ti.label = {};
    ti.flags = 0;
    array_push_memcpy(_test_items, &ti);
}

/// <summary>
/// Invoked by IMGUI to register additional information about an item being rendered.
/// </summary>
/// <param name="ctx">IMGUI context</param>
/// <param name="id">Unique item ID</param>
/// <param name="label">Diaply label of the item</param>
/// <param name="flags">Item status flags</param>
extern void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id,
                                         const char* label, ImGuiItemStatusFlags flags)
{
    foreach(ti, _test_items)
    {
        if (ti->id == id)
        {
            string_deallocate(ti->label.str);
            ti->label = string_clone(label, string_length(label));
            ti->flags = flags;
            return;
        }
    }

    FOUNDATION_ASSERT_FAIL("Cannot find item");
}

/// <summary>
/// Called IMGUI to log additional information about an item. Mostly used for debugging.
/// </summary>
/// <remark>Not currently used</remark>
/// <param name="ctx">IMGUI context</param>
/// <param name="fmt">Printf format string</param>
/// <param name="...">Printf arguments</param>
extern void ImGuiTestEngineHook_Log(ImGuiContext* ctx, const char* fmt, ...)
{
    va_list list;
    va_start(list, fmt);  
    string_t msg = string_allocate_vformat(fmt, string_length(fmt), list);
    log_info(HASH_TEST, STRING_ARGS(msg));
    string_deallocate(msg.str);
    va_end(list);
}

extern const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* ctx, ImGuiID id)
{
    foreach(ti, _test_items)
    {
        if (ti->id == id)
            return ti->label.str;
    }
    return nullptr;
}

FOUNDATION_FORCEINLINE ImGuiID ImGuiTestEngine_GetID(ImGuiContext* ctx, const char* label)
{
    FOUNDATION_ASSERT(ctx && ctx->Initialized);

    ImGuiWindow* window = ctx->CurrentWindow ? ctx->CurrentWindow : ctx->Windows[0];
    FOUNDATION_ASSERT(window);

    return window->GetID(label);
}

FOUNDATION_FORCEINLINE ImGuiTestItem* ImGuiTestEngine_FindItemByLabel(ImGuiContext* ctx, const char* label)
{
    FOUNDATION_ASSERT(ctx && ctx->Initialized);

    const size_t label_length = string_length(label);
    foreach(ti, _test_items)
    {
        if (string_equal(STRING_ARGS(ti->label), label, label_length))
            return ti;
    }

    return nullptr;
}

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
        main_process(test_window, [p_render_callback, p_test_event_callback](GLFWwindow* window, int frame_width, int frame_height)
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

DEFINE_SERVICE(TEST, test_utils_initialize, test_utils_shutdown, SERVICE_PRIORITY_TESTS-1);

#else

#include <framework/imgui.h>

extern void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, const ImRect& bb, ImGuiID id)
{
}

extern void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags)
{
}

extern void ImGuiTestEngineHook_Log(ImGuiContext* ctx, const char* fmt, ...)
{
}

extern const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* ctx, ImGuiID id)
{
    return nullptr;
}

#endif
