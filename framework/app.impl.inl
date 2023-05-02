/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * Provide a default application implementation
 */

#pragma once

#include "version.h"

#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/jobs.h>
#include <framework/query.h>
#include <framework/session.h>
#include <framework/module.h>
#include <framework/progress.h>
#include <framework/dispatcher.h>
#include <framework/tabs.h>

#include <foundation/foundation.h>

extern const char* app_title()
{
    return PRODUCT_NAME;
}

extern void app_exception_handler(void* args, const char* dump_file, size_t length)
{
    FOUNDATION_UNUSED(dump_file);
    FOUNDATION_UNUSED(length);
    log_error(0, ERROR_EXCEPTION, STRING_CONST("Unhandled exception"));
    process_exit(-1);
}

extern void app_configure(foundation_config_t& config, application_t& application)
{
    application.flags = APPLICATION_GUI;
    application.name = string_const(PRODUCT_NAME, string_length(PRODUCT_NAME));
    application.short_name = string_const(PRODUCT_CODE_NAME, string_length(PRODUCT_CODE_NAME));
    application.company = string_const(STRING_CONST(PRODUCT_COMPANY));
    application.version = version_make(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD, 0);
    application.exception_handler = app_exception_handler;
}

extern int app_initialize(GLFWwindow* window)
{
    // Framework systems
    string_table_initialize();
    progress_initialize();
    jobs_initialize();
    query_initialize();

    session_setup(nullptr);

    // App systems;
    module_initialize();

    #if defined(FRAMEWORK_APP_EXTENDED_INITIALIZE)
    extern void FRAMEWORK_APP_EXTENDED_INITIALIZE(GLFWwindow * window);
    FRAMEWORK_APP_EXTENDED_INITIALIZE(window);
    #endif

    return 0;
}

extern void app_shutdown()
{
    dispatcher_update();
    dispatcher_poll(nullptr);

    #if defined(FRAMEWORK_APP_EXTENDED_SHUTDOWN)
    extern void FRAMEWORK_APP_EXTENDED_SHUTDOWN();
    FRAMEWORK_APP_EXTENDED_SHUTDOWN();
    #else
    tabs_shutdown();
    #endif

    // Lets make sure all requests are finished 
    // before exiting shutting down other services.
    jobs_shutdown();
    query_shutdown();

    // Framework systems
    module_shutdown();
    progress_finalize();
    session_shutdown();
    string_table_shutdown();
}

extern void app_update(GLFWwindow* window)
{
    module_update();
}

#if !defined(FRAMEWORK_APP_CUSTOM_RENDER_IMPLEMENTATION)
extern void app_render(GLFWwindow* window, int frame_width, int frame_height)
{
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)frame_width, (float)frame_height));

    if (ImGui::Begin(app_title(), nullptr,
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar))
    {
        dispatcher_update();
        module_foreach_window();
    } ImGui::End();
}
#endif

extern void app_render_3rdparty_libs()
{
    
}

#if BUILD_TESTS
extern int main_tests(void* _context, GLFWwindow* window)
{
    FOUNDATION_ASSERT_FAIL("Not implemented");
    return -1;
}
#endif
