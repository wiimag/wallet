/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "app.h"
#include "version.h"
#include "settings.h"

#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/service.h>
#include <framework/profiler.h>
#include <framework/tabs.h>
#include <framework/string_table.h>
#include <framework/session.h>
#include <framework/progress.h>
#include <framework/jobs.h>
#include <framework/query.h>
#include <framework/console.h>
#include <framework/dispatcher.h>
#include <framework/string.h>

#include <foundation/version.h>
#include <foundation/hashstrings.h>
#include <foundation/stacktrace.h>
#include <foundation/process.h>
#include <foundation/hashtable.h>

#include <algorithm>

struct app_dialog_t
{
    char title[128]{ 0 };
    bool opened{ true };
    bool can_resize{ true };
    bool window_opened_once{ false };
    uint32_t width{ 480 }, height{ 400 };
    app_dialog_handler_t handler;
    app_dialog_close_handler_t close_handler;
    void* user_data{ nullptr };
};

static app_dialog_t* _dialogs = nullptr;

FOUNDATION_STATIC void app_main_menu_begin(GLFWwindow* window)
{
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::BeginMenu("Create"))
        {
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Open"))
        {
            ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem(ICON_MD_EXIT_TO_APP " Exit", "Alt+F4"))
            glfwSetWindowShouldClose(window, 1);
            
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

FOUNDATION_STATIC void app_main_menu_end(GLFWwindow* window)
{    
    service_foreach_menu();

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Windows"))
            ImGui::EndMenu();
            
        if (ImGui::BeginMenu("Help"))
        {
            #if BUILD_DEVELOPMENT
            if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) && ImGui::BeginMenu("BUILD"))
            {
                #if BUILD_DEBUG
                ImGui::MenuItem("BUILD_DEBUG");
                #endif
                #if BUILD_RELEASE
                ImGui::MenuItem("BUILD_RELEASE");
                #endif
                #if BUILD_DEPLOY
                ImGui::MenuItem("BUILD_DEPLOY");
                #endif
                #if BUILD_DEVELOPMENT
                ImGui::MenuItem("BUILD_DEVELOPMENT");
                #endif
                #if BUILD_ENABLE_LOG
                ImGui::MenuItem("BUILD_ENABLE_LOG");
                #endif
                #if BUILD_ENABLE_ASSERT
                ImGui::MenuItem("BUILD_ENABLE_ASSERT");
                #endif
                #if BUILD_ENABLE_ERROR_CONTEXT
                ImGui::MenuItem("BUILD_ENABLE_ERROR_CONTEXT");
                #endif
                #if BUILD_ENABLE_DEBUG_LOG
                ImGui::MenuItem("BUILD_ENABLE_DEBUG_LOG");
                #endif
                #if BUILD_ENABLE_PROFILE
                ImGui::MenuItem("BUILD_ENABLE_PROFILE");
                #endif
                #if BUILD_ENABLE_MEMORY_CONTEXT
                ImGui::MenuItem("BUILD_ENABLE_MEMORY_CONTEXT");
                #endif
                #if BUILD_ENABLE_MEMORY_TRACKER
                ImGui::MenuItem("BUILD_ENABLE_MEMORY_TRACKER");
                #endif
                #if BUILD_ENABLE_MEMORY_GUARD
                ImGui::MenuItem("BUILD_ENABLE_MEMORY_GUARD");
                #endif
                #if BUILD_ENABLE_MEMORY_STATISTICS
                ImGui::MenuItem("BUILD_ENABLE_MEMORY_STATISTICS");
                #endif
                #if BUILD_ENABLE_STATIC_HASH_DEBUG
                ImGui::MenuItem("BUILD_ENABLE_STATIC_HASH_DEBUG");
                #endif
                ImGui::EndMenu();
            }
            #endif

            #if BUILD_ENABLE_DEBUG_LOG
            bool show_debug_log = log_suppress(HASH_DEBUG) == ERRORLEVEL_NONE;
            if (ImGui::MenuItem("Show Debug Logs", nullptr, &show_debug_log))
            {
                if (show_debug_log)
                {
                    console_show();
                    log_set_suppress(0, ERRORLEVEL_NONE);
                    log_set_suppress(HASH_DEBUG, ERRORLEVEL_NONE);
                }
                else
                {
                    log_set_suppress(0, ERRORLEVEL_DEBUG);
                    log_set_suppress(HASH_DEBUG, ERRORLEVEL_DEBUG);
                }
            }
            #endif

            #if BUILD_ENABLE_MEMORY_STATISTICS && BUILD_ENABLE_MEMORY_TRACKER
            if (ImGui::MenuItem("Show Memory Stats"))
            {
                MEMORY_TRACKER(HASH_MEMORY);
                console_show();
                auto mem_stats = memory_statistics();
                log_infof(HASH_MEMORY, STRING_CONST("Memory stats: \n"
                    "\t Current: %.4g mb (%llu)\n"
                    "\t Total: %.4g mb (%llu)"),
                    mem_stats.allocated_current / 1024.0f / 1024.0f, mem_stats.allocations_current,
                    mem_stats.allocated_total / 1024.0f / 1024.0f, mem_stats.allocations_total);

                #if BUILD_ENABLE_MEMORY_TRACKER && BUILD_ENABLE_MEMORY_CONTEXT
                    struct memory_context_stats_t {
                        hash_t context;
                        uint64_t allocated_mem;
                    };
                    static memory_context_stats_t* memory_contexts = nullptr;
                    memory_tracker_dump([](hash_t context, const void* addr, size_t size, void* const* trace, size_t depth)->int
                    {
                        context = context ? context : HASH_DEFAULT;
                        foreach(c, memory_contexts)
                        {
                            if (c->context == context)
                            { 
                                c->allocated_mem += size;
                                return 0;
                            }
                        }

                        memory_context_stats_t mc{ context, size };
                        array_push(memory_contexts, mc);
                        return 0;
                    });

                    array_sort(memory_contexts, a.allocated_mem > b.allocated_mem);

                    foreach(c, memory_contexts)
                    {
                        string_const_t context_name = hash_to_string(c->context);
                        if (context_name.length == 0)
                            context_name = CTEXT("other");
                        if (c->allocated_mem > 512 * 1024 * 1024)
                            log_warnf(HASH_MEMORY, WARNING_MEMORY, STRING_CONST("%16.*s : %5.3g gb"), STRING_FORMAT(context_name), c->allocated_mem / 1024.0f / 1024.0f / 1024.0f);
                        else if (c->allocated_mem > 512 * 1024)
                            log_warnf(HASH_MEMORY, WARNING_MEMORY, STRING_CONST("%16.*s : %5.3g mb"), STRING_FORMAT(context_name), c->allocated_mem / 1024.0f / 1024.0f);
                        else 
                            log_infof(HASH_MEMORY, STRING_CONST("%34.*s : %5.3g kb"), STRING_FORMAT(context_name), c->allocated_mem / 1024.0f);
                    }

                    array_deallocate(memory_contexts);
                #endif
            }
            #endif

            #if BUILD_ENABLE_MEMORY_TRACKER && BUILD_ENABLE_MEMORY_CONTEXT
            if (ImGui::MenuItem("Show Memory Usages"))
            {
                console_show();
                const bool prefix_enaled = log_is_prefix_enabled();
                log_enable_prefix(false);
                log_enable_auto_newline(true);
                memory_tracker_dump([](hash_t context, const void* addr, size_t size, void* const* trace, size_t depth)->int
                {
                    context = context ? context : HASH_DEFAULT;
                    string_const_t context_name = hash_to_string(context);
                    if (context_name.length == 0)
                        context_name = CTEXT("other");
                    char current_frame_stack_buffer[512];
                    string_t stf = stacktrace_resolve(STRING_BUFFER(current_frame_stack_buffer), trace, min((size_t)3, depth), 0);
                    if (size > 256 * 1024)
                    {
                        log_warnf(HASH_MEMORY, WARNING_MEMORY, STRING_CONST("%.*s: 0x%p, %.3g mb [%.*s]\n%.*s"), STRING_FORMAT(context_name), addr, size / 1024.0f / 1024.0f,
                            min(32, (int)size), (const char*)addr, STRING_FORMAT(stf));
                    }
                    else
                    {
                        log_infof(HASH_MEMORY, STRING_CONST("%.*s: 0x%p, %.4g kb [%.*s]\n%.*s"), STRING_FORMAT(context_name), addr, size / 1024.0f,
                            min(32, (int)size), (const char*)addr, STRING_FORMAT(stf));
                    }
                    return 0;
                });
                log_enable_prefix(prefix_enaled);
            }
            #endif

            #if BUILD_ENABLE_DEBUG_LOG || BUILD_ENABLE_MEMORY_STATISTICS || (BUILD_ENABLE_MEMORY_TRACKER && BUILD_ENABLE_MEMORY_CONTEXT)
            ImGui::Separator();
            #endif

            string_const_t version_string = string_from_version_static(version_make(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD, 0));
            if (ImGui::MenuItem(string_format_static_const("Version %.*s.%s (%s)", STRING_FORMAT(version_string), GIT_SHORT_HASH, __DATE__)))
            {
                // TODO: Check new version available?
            }

            ImGui::EndMenu();
        }

        // Update special application menu status.
        // Usually controls are displayed at the far right of the menu.
        profiler_menu_timer();
        service_foreach_menu_status();

        ImGui::EndMenuBar();
    }
}

FOUNDATION_STATIC void app_tabs_content_filter()
{
    if (shortcut_executed(true, ImGuiKey_F))
        ImGui::SetKeyboardFocusHere();
    ImGui::InputTextEx("##SearchFilter", "Filter... " ICON_MD_FILTER_LIST_ALT, STRING_BUFFER(SETTINGS.search_filter),
        ImVec2(imgui_get_font_ui_scale(300.0f), 0), ImGuiInputTextFlags_AutoSelectAll, 0, 0);
}

FOUNDATION_STATIC void app_tabs()
{   
    static ImGuiTabBarFlags tabs_init_flags = ImGuiTabBarFlags_Reorderable;

    if (tabs_begin("Tabs", SETTINGS.current_tab, tabs_init_flags, app_tabs_content_filter))
    {
        service_foreach_tabs();

        tab_set_color(TAB_COLOR_SETTINGS);
        tab_draw(ICON_MD_SETTINGS " Settings ", nullptr, ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoReorder, settings_draw);

        tabs_end();
    }

    if ((tabs_init_flags & ImGuiTabBarFlags_AutoSelectNewTabs) == 0)
        tabs_init_flags |= ImGuiTabBarFlags_AutoSelectNewTabs;
}

FOUNDATION_STATIC void app_render_dialogs()
{
    for (unsigned i = 0, end = array_size(_dialogs); i < end; ++i)
    {
        app_dialog_t& dlg = _dialogs[i];
        if (!dlg.window_opened_once)
        {
            const ImVec2 window_size = ImGui::GetWindowSize();
            ImGui::SetNextWindowPos(ImVec2((window_size.x - dlg.width) / 2.0f, (window_size.y - dlg.height) / 2.0f), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(ImVec2(dlg.width, dlg.height), ImVec2(INFINITY, INFINITY));
            ImGui::SetNextWindowFocus();
            dlg.window_opened_once = true;
        }

        if (ImGui::Begin(dlg.title, &dlg.opened, (ImGuiWindowFlags_NoCollapse) | (dlg.can_resize ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoResize)))
        {
            if (ImGui::IsWindowFocused() && shortcut_executed(ImGuiKey_Escape))
                dlg.opened = false;

            if (!dlg.opened || !dlg.handler(dlg.user_data))
            {
                dlg.handler.~function();
                if (dlg.close_handler)
                    dlg.close_handler(dlg.user_data);
                dlg.close_handler.~function();
                array_erase(_dialogs, i);
                ImGui::End();
                break;
            }
        }
        ImGui::End();
    }
}

FOUNDATION_STATIC void app_dialog_shutdown()
{
    for (unsigned i = 0, end = array_size(_dialogs); i < end; ++i)
    {
        app_dialog_t& dlg = _dialogs[i];
        if (dlg.close_handler)
            dlg.close_handler(dlg.user_data);
        dlg.close_handler.~function();
        dlg.handler.~function();
    }

    array_deallocate(_dialogs);
}

FOUNDATION_STATIC void app_main_window(GLFWwindow* window, const char* window_title, float window_width, float window_height)
{
    app_main_menu_begin(window);
    dispatcher_update();

    app_tabs();
    app_main_menu_end(window);

    app_render_dialogs();
    service_foreach_window();
}

// 
// # PUBLIC API
//

void app_open_dialog(const char* title, app_dialog_handler_t&& handler, uint32_t width, uint32_t height, bool can_resize, void* user_data, app_dialog_close_handler_t&& close_handler)
{
    FOUNDATION_ASSERT(handler);

    for (unsigned i = 0, end = array_size(_dialogs); i < end; ++i)
    {
        app_dialog_t& dlg = _dialogs[i];
        if (string_equal(title, string_length(title), dlg.title, string_length(dlg.title)))
        {
            log_warnf(0, WARNING_UI, STRING_CONST("Dialog %s is already opened"), dlg.title);
            return;
        }
    }

    app_dialog_t dlg{};
    dlg.can_resize = can_resize;
    if (width) dlg.width = width;
    if (height) dlg.height = height;
    dlg.handler = std::move(handler);
    dlg.close_handler = std::move(close_handler);
    dlg.user_data = user_data;
    string_copy(STRING_BUFFER(dlg.title), title, string_length(title));
    array_push_memcpy(_dialogs, &dlg);
}

const char* app_title()
{
    return PRODUCT_NAME;
}

//
// # SYSTEM (Usually invoked within boot.cpp)
//

extern void app_exception_handler(const char* dump_file, size_t length)
{
    FOUNDATION_UNUSED(dump_file);
    FOUNDATION_UNUSED(length);
    log_error(0, ERROR_EXCEPTION, STRING_CONST("Unhandled exception"));
    process_exit(-1);
}

extern void app_configure(foundation_config_t& config, application_t& application)
{
    #if BUILD_ENABLE_STATIC_HASH_DEBUG
    config.hash_store_size = 256;
    #endif
    application.name = string_const(PRODUCT_NAME, string_length(PRODUCT_NAME));
    application.short_name = string_const(PRODUCT_CODE_NAME, string_length(PRODUCT_CODE_NAME));
    application.company = string_const(STRING_CONST(PRODUCT_COMPANY));
    application.version = version_make(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD, 0);
    application.flags = APPLICATION_GUI;
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

    // App systems
    settings_initialize();
    service_initialize();

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
    service_shutdown();
    settings_shutdown();
    
    // Framework systems
    tabs_shutdown();
    app_dialog_shutdown();
    progress_finalize();
    session_shutdown();
    string_table_shutdown();
}

extern void app_update(GLFWwindow* window)
{
    service_update();
}

extern void app_render(GLFWwindow* window, int frame_width, int frame_height)
{
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
        app_main_window(window, app_title(), (float)frame_width, (float)frame_height);
    } ImGui::End();
}
