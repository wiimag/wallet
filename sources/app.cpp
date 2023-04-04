/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "version.h"
#include "settings.h"

#include "alerts.h"

#include <framework/app.h>
#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/module.h>
#include <framework/profiler.h>
#include <framework/tabs.h>
#include <framework/string_table.h>
#include <framework/progress.h>
#include <framework/jobs.h>
#include <framework/query.h>
#include <framework/session.h>
#include <framework/dispatcher.h>
#include <framework/localization.h>

#include <foundation/version.h>
#include <foundation/process.h>

FOUNDATION_STATIC void app_main_menu_begin(GLFWwindow* window)
{
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::TrBeginMenu("File"))
    {
        if (ImGui::TrBeginMenu("Create"))
        {
            ImGui::EndMenu();
        }

        if (ImGui::TrBeginMenu("Open"))
        {
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::TrMenuItem(ICON_MD_EXIT_TO_APP " Exit", "Alt+F4"))
            glfw_request_close_window(window);
            
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();

    app_menu_begin(window);
}

FOUNDATION_STATIC void app_main_menu_end(GLFWwindow* window)
{
    #if BUILD_APPLICATION
    module_foreach_menu();

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::TrBeginMenu("Windows"))
            ImGui::EndMenu();
            
        app_menu_help(window);

        alerts_notification_menu();

        // Update special application menu status.
        // Usually controls are displayed at the far right of the menu.
        profiler_menu_timer();
        module_foreach_menu_status();

        ImGui::EndMenuBar();
    }

    app_menu_end(window);
    #endif // BUILD_APPLICATION
}

FOUNDATION_STATIC void app_tabs_content_filter()
{
    if (shortcut_executed(true, ImGuiKey_F))
        ImGui::SetKeyboardFocusHere();
    ImGui::InputTextEx("##SearchFilter", "Filter... " ICON_MD_FILTER_LIST_ALT, STRING_BUFFER(SETTINGS.search_filter),
        ImVec2(IM_SCALEF(150.0f), 0), ImGuiInputTextFlags_AutoSelectAll, 0, 0);
}

FOUNDATION_STATIC void app_tabs()
{   
    #if BUILD_APPLICATION
    static ImGuiTabBarFlags tabs_init_flags = ImGuiTabBarFlags_Reorderable;

    if (tabs_begin("Tabs", SETTINGS.current_tab, tabs_init_flags, app_tabs_content_filter))
    {
        module_foreach_tabs();

        tab_set_color(TAB_COLOR_SETTINGS);
        tab_draw(tr(ICON_MD_SETTINGS " Settings ##Settings"), nullptr, 
            ImGuiTabItemFlags_NoPushId | ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoReorder, settings_draw);

        tabs_end();
    }

    if ((tabs_init_flags & ImGuiTabBarFlags_AutoSelectNewTabs) == 0)
        tabs_init_flags |= ImGuiTabBarFlags_AutoSelectNewTabs;
    #endif
}

extern const char* app_title()
{
    return PRODUCT_NAME;
}

extern void app_exception_handler(const char* dump_file, size_t length)
{
    FOUNDATION_UNUSED(dump_file);
    FOUNDATION_UNUSED(length);
    log_error(0, ERROR_EXCEPTION, STRING_CONST("Unhandled exception"));
    process_exit(-1);
}

extern void app_configure(foundation_config_t& config, application_t& application)
{
    #if BUILD_APPLICATION
    application.flags = APPLICATION_GUI;
    #else
    application.flags = APPLICATION_DAEMON;
    #endif

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
    session_setup(nullptr);
    query_initialize();
    
    // App systems
    settings_initialize();
    module_initialize();

    return 0;
}

extern void app_shutdown()
{
    dispatcher_update();
    dispatcher_poll(nullptr);
        
    // Lets make sure all requests are finished 
    // before exiting shutting down other services.
    jobs_shutdown();
    query_shutdown();
    
    // App systems
    module_shutdown();
    settings_shutdown();
    
    // Framework systems
    tabs_shutdown();
    progress_finalize();
    session_shutdown();
    string_table_shutdown();
}

extern void app_update(GLFWwindow* window)
{
    module_update();
}

extern void app_render(GLFWwindow* window, int frame_width, int frame_height)
{
    #if BUILD_APPLICATION
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)frame_width, (float)frame_height));

    if (ImGui::Begin(app_title(), nullptr,
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_MenuBar))
    {
        app_main_menu_begin(window);

        app_tabs();
        app_main_menu_end(window);

        module_foreach_window();
    } ImGui::End();
    #endif
}

extern void app_render_3rdparty_libs()
{
    ImGui::TextURL("EOD Historical Data API", nullptr, STRING_CONST("https://eodhistoricaldata.com/financial-apis/"));
}
