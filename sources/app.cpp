/*
 * Copyright 2022-2023 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#include "app.h"
#include "version.h"
#include "settings.h"
#include "report.h"
#include "eod.h"
#include "symbols.h"
#include "pattern.h"
 
// Framework includes
#include <framework/session.h>
#include <framework/scoped_string.h>
#include <framework/query.h>
#include <framework/imgui.h>
#include <framework/progress.h>
#include <framework/service.h>
#include <framework/tabs.h>
#include <framework/jobs.h>
#include <framework/expr.h>
#include <framework/glfw.h>
 
#include <foundation/version.h>
#include <foundation/process.h>
#include <foundation/hashstrings.h>
#include <foundation/stacktrace.h>
 
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

const char* APP_TITLE = PRODUCT_NAME;
static app_dialog_t* _dialogs = nullptr;

static const ImVec4 TAB_COLOR_HISTORY(0.3f, 0.3f, 0.3f, 1.0f);
static const ImVec4 TAB_COLOR_REPORT(0.4f, 0.2f, 0.7f, 1.0f);
static const ImVec4 TAB_COLOR_PATTERN(0.2f, 0.4f, 0.5f, 1.0f);
static const ImVec4 TAB_COLOR_SYMBOLS(0.6f, 0.2f, 0.5f, 1.0f);

FOUNDATION_STATIC void app_update_window_title(GLFWwindow* window)
{
    eod_fetch_async("user", "", FORMAT_JSON, [window](const json_object_t& json)
    {
        const bool is_main_branch = string_equal(STRING_CONST(GIT_BRANCH), STRING_CONST("main")) ||
            string_equal(STRING_CONST(GIT_BRANCH), STRING_CONST("master"));

        string_const_t subscription = json["subscriptionType"].as_string();
        string_const_t branch_name{ subscription.str, subscription.length };
        if (!is_main_branch)
            branch_name = string_to_const(GIT_BRANCH);
        
        string_const_t license_name = json["name"].as_string();
        string_const_t version_string = string_from_version_static(version_make(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD, 0));

        char title[128] = PRODUCT_NAME;
        string_format(STRING_CONST_CAPACITY(title), STRING_CONST("%s (%.*s) [%.*s] v.%.*s"), 
            APP_TITLE, STRING_FORMAT(license_name), STRING_FORMAT(branch_name), STRING_FORMAT(version_string));

        glfwSetWindowTitle(window, title);
    });
}

FOUNDATION_STATIC void app_main_menu_begin(GLFWwindow* window)
{
    if (shortcut_executed(ImGuiKey_F2))
        SETTINGS.show_create_report_ui = true;

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::BeginMenu("Create"))
            {
                if (ImGui::MenuItem("Report", "F2", &SETTINGS.show_create_report_ui))
                    SETTINGS.show_create_report_ui = true;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Open"))
            {
                if (ImGui::MenuItem("Report...", nullptr, nullptr))
                    log_warnf(0, WARNING_UNSUPPORTED, STRING_CONST("TODO"));

                bool first_report_that_can_be_opened = true;
                size_t report_count = ::report_count();
                for (int handle = 0; handle < report_count; ++handle)
                {
                    report_t* report = report_get_at(handle);
                    if (!report->opened)
                    {
                        if (first_report_that_can_be_opened)
                        {
                            ImGui::Separator();
                            first_report_that_can_be_opened = false;
                        }
                        ImGui::MenuItem(
                            string_format_static_const("%s", string_table_decode(report->name)), 
                            nullptr, &report->opened);
                    }
                }

                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem(ICON_MD_EXIT_TO_APP " Exit", "Alt+F4"))
                glfwSetWindowShouldClose(window, 1);
            
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Symbols"))
        {
            ImGui::MenuItem("Indexes", nullptr, &SETTINGS.show_symbols_INDX);
            ImGui::MenuItem("Last Day", nullptr, &SETTINGS.show_bulk_eod);

            ImGui::Separator();
            ImGui::MenuItem("TO Symbols", nullptr, &SETTINGS.show_symbols_TO);
            ImGui::MenuItem("CVE Symbols", nullptr, &SETTINGS.show_symbols_CVE);
            ImGui::MenuItem("NEO Symbols", nullptr, &SETTINGS.show_symbols_NEO);
            ImGui::MenuItem("US Symbols", nullptr, &SETTINGS.show_symbols_US);
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

FOUNDATION_STATIC void app_main_menu_end(GLFWwindow* window)
{
    service_foreach_menu();

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Help"))
        {
            #if BUILD_DEBUG
                bool show_debug_log = log_suppress(HASH_DEBUG) == ERRORLEVEL_NONE;
                if (ImGui::MenuItem("Show Debug Logs", nullptr, &show_debug_log))
                {
                    if (show_debug_log)
                    {
                        log_set_suppress(0, ERRORLEVEL_NONE);
                        log_set_suppress(HASH_DEBUG, ERRORLEVEL_NONE);
                    }
                    else
                    {
                        log_set_suppress(0, ERRORLEVEL_DEBUG);
                        log_set_suppress(HASH_DEBUG, ERRORLEVEL_DEBUG);
                    }
                }

                if (ImGui::MenuItem("Show Memory Stats"))
                {
                    auto mem_stats = memory_statistics();
                    log_infof(HASH_MEMORY, STRING_CONST("Memory stats: \n"
                        "\t Current: %.3g mb (%llu)\n"
                        "\t Total: %.3g mb (%llu)"),
                        mem_stats.allocated_current / 1024.0f / 1024.0f, mem_stats.allocations_current,
                        mem_stats.allocated_total / 1024.0f / 1024.0f, mem_stats.allocations_total);
                }

                if (ImGui::MenuItem("Show Memory Usages"))
                {
                    memory_tracker_dump([](const void* addr, size_t size, void* const* trace, size_t depth)->int
                    {
                        char current_frame_stack_buffer[512];
                        string_t stf = stacktrace_resolve(STRING_CONST_CAPACITY(current_frame_stack_buffer), trace, min((size_t)3, depth), 0);
                        log_infof(HASH_MEMORY, STRING_CONST("0x%p, %.3g kb [%.*s]\n%.*s"), addr, size / 1024.0f,
                            min(32, (int)size), (const char*)addr, STRING_FORMAT(stf));
                        return 0;
                    });
                }
            #endif

            ImGui::Separator();

            string_const_t version_string = string_from_version_static(version_make(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD, 0));
            if (ImGui::MenuItem(string_format_static_const("Version %.*s.%s (%s)", STRING_FORMAT(version_string), GIT_SHORT_HASH, __DATE__)))
            {
                // TODO: Check new version available?
            }

            ImGui::EndMenu();
        }

        #if !BUILD_DEPLOY && BUILD_ENABLE_PROFILE
        {
            static tick_t last_frame_tick = time_current();
            tick_t elapsed_ticks = time_diff(last_frame_tick, time_current());

            static unsigned index = 0;
            static double elapsed_times[30] = { 0.0 };
            elapsed_times[index++ % ARRAY_COUNT(elapsed_times)] = time_ticks_to_milliseconds(elapsed_ticks);
            const double smooth_elapsed_time = math_average(elapsed_times, ARRAY_COUNT(elapsed_times));
            const double tick_elapsed_time = main_tick_elapsed_time_ms();

            char frame_time[16];
            if (tick_elapsed_time < smooth_elapsed_time - 1)
                string_format(STRING_CONST_CAPACITY(frame_time), S("%.0lf/%.0lf ms"), tick_elapsed_time, smooth_elapsed_time);
            else
                string_format(STRING_CONST_CAPACITY(frame_time), S("%.0lf ms"), tick_elapsed_time);

            ImGui::MenuItem(frame_time, nullptr, nullptr, false);
            last_frame_tick = time_current();
        }
        #endif

        {
            static bool connected = false;
            static char eod_status[128] = "";
            static char eod_menu_title[64] = "EOD";
            static tick_t eod_menu_title_last_update = 0;
            if (time_elapsed(eod_menu_title_last_update) > 60)
            {
                eod_menu_title_last_update = time_current();
                eod_fetch_async("user", "", FORMAT_JSON, [](const json_object_t& json)
                {
                    connected = true;
                    double api_calls = json["apiRequests"].as_number();
                    double api_calls_limit = json["dailyRateLimit"].as_number();
                    string_format(STRING_CONST_CAPACITY(eod_menu_title), STRING_CONST("EOD [API USAGE %.2lf %%]"), api_calls * 100 / api_calls_limit);

                    string_const_t name = json["name"].as_string();
                    string_const_t email = json["email"].as_string();
                    string_const_t subtype = json["subscriptionType"].as_string();
                    string_format(STRING_CONST_CAPACITY(eod_status), STRING_CONST("Name: %.*s\nEmail: %.*s\nSubscription: %.*s\nRequest: %lg/%lg"),
                        STRING_FORMAT(name), STRING_FORMAT(email), STRING_FORMAT(subtype), api_calls, api_calls_limit);

                    eod_menu_title_last_update = time_current();
                });
            }

            const ImGuiStyle& style = ImGui::GetStyle();
            const ImVec2 window_size = ImGui::GetWindowSize();
            const float space = ImGui::GetContentRegionAvail().x;
            const float content_width = ImGui::CalcTextSize(eod_menu_title).x + style.FramePadding.x * 2.0f;
            const ImVec2 status_box_size(38.0f, 38.0f);

            ImGui::MoveCursor(space - content_width - status_box_size.x - style.FramePadding.x * 2.0f, 0);
            ImGui::BeginGroup();
            if (ImGui::BeginMenu(eod_menu_title))
            {
                string_t eod_key = eod_get_key();

                if (ImGui::MenuItem("Refresh"))
                {
                    eod_menu_title_last_update = 0;
                    app_update_window_title(window);
                }

                ImGui::Separator();
                ImGui::TextURL("EOD API Key", nullptr, STRING_CONST("https://eodhistoricaldata.com"));
                if (ImGui::InputTextWithHint("##EODKey", "demo", eod_key.str, eod_key.length,
                    ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_Password))
                {
                    eod_save_key(eod_key);
                }

                ImGui::EndMenu();
            }

            ImGui::Dummy(status_box_size);
            if (ImGui::IsItemHovered())
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false))
                {
                    // TODO
                }
                else
                    ImGui::SetTooltip("%s", eod_status);
            }

            static const ImColor green = ImColor::HSV(150 / 360.0f, 0.4f, 0.6f);
            static const ImColor yellow = ImColor::HSV(61 / 360.0f, 0.95f, 0.98f); // hsv(61, 95%, 98%)
            static const ImColor red = ImColor::HSV(356 / 360.0f, 0.42f, 0.97f); // hsv(356, 42%, 97%)
            static const ImColor gray = ImColor::HSV(155 / 360.0f, 0.05f, 0.85f); // hsv(155, 6%, 84%)
            const ImRect connection_status_box(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            const ImVec2 connection_status_box_center = connection_status_box.GetCenter() + ImVec2(-4.0f, 4.0f);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddCircleFilled(connection_status_box_center, status_box_size.x / 2.0f, connected ? green : gray);

            ImGui::EndGroup();
        }

        ImGui::EndMenuBar();
    }
}

FOUNDATION_STATIC void app_tabs_content_filter()
{
    if (shortcut_executed(true, ImGuiKey_F))
        ImGui::SetKeyboardFocusHere();
    ImGui::InputTextEx("##SearchFilter", "Filter... " ICON_MD_FILTER_LIST_ALT, STRING_CONST_CAPACITY(SETTINGS.search_filter),
        ImVec2(imgui_get_font_ui_scale(300.0f), 0), ImGuiInputTextFlags_AutoSelectAll, 0, 0);
}

FOUNDATION_STATIC void app_tabs()
{   
    static ImGuiTabBarFlags tabs_init_flags = ImGuiTabBarFlags_Reorderable;

    if (tabs_begin("Tabs", SETTINGS.current_tab, tabs_init_flags, app_tabs_content_filter))
    {
        tab_set_color(TAB_COLOR_APP);
        tab_draw(ICON_MD_WALLET " Wallet ", nullptr, ImGuiTabItemFlags_Leading, wallet_history_draw, nullptr);

        tab_set_color(TAB_COLOR_REPORT);
        size_t report_count = ::report_count();
        for (int handle = 0; handle < report_count; ++handle)
        {
            report_t* report = report_get_at(handle);
            if (report->opened)
            {
                string_const_t id = string_from_uuid_static(report->id);
                string_const_t name = string_table_decode_const(report->name);
                string_const_t report_tab_id = string_format_static(STRING_CONST(ICON_MD_WALLET " %.*s###%.*s"), STRING_FORMAT(name), STRING_FORMAT(id));
                report->save_index = ImGui::GetTabItemVisibleIndex(report_tab_id.str);

                tab_draw(report_tab_id.str, &report->opened, (report->dirty ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None), 
                    L0(report_render(report)), L0(report_menu(report)));
            }
        }

        // Load all active patterns
        tab_set_color(TAB_COLOR_PATTERN);
        size_t pattern_count = ::pattern_count();
        for (int handle = 0; handle < pattern_count; ++handle)
        {
            pattern_t* pattern = pattern_get(handle);
            if (pattern->opened)
            {
                string_const_t code = string_table_decode_const(pattern->code);
                string_const_t tab_id = string_format_static(STRING_CONST(ICON_MD_INSIGHTS" %.*s"), STRING_FORMAT(code));
                tab_draw(tab_id.str, &(pattern->opened), L0(pattern_render(handle)), L0(pattern_menu(handle)));
            }
        }

        tab_set_color(TAB_COLOR_SYMBOLS);
        if (SETTINGS.show_symbols_TO) tab_draw(ICON_MD_CURRENCY_EXCHANGE " Symbols (TO)", &SETTINGS.show_symbols_TO, L0(symbols_render("TO")));
        if (SETTINGS.show_symbols_CVE) tab_draw(ICON_MD_CURRENCY_EXCHANGE " Symbols (CVE)", &SETTINGS.show_symbols_CVE, L0(symbols_render("V")));
        if (SETTINGS.show_symbols_NEO) tab_draw(ICON_MD_CURRENCY_EXCHANGE " Symbols (NEO)", &SETTINGS.show_symbols_NEO, L0(symbols_render("NEO")));
        if (SETTINGS.show_symbols_US) tab_draw(ICON_MD_CURRENCY_EXCHANGE " Symbols (US)", &SETTINGS.show_symbols_US, L0(symbols_render("US")));
        if (SETTINGS.show_symbols_INDX) tab_draw(ICON_MD_TRENDING_UP " Indexes", &SETTINGS.show_symbols_INDX, L0(symbols_render("INDX", false)));

        tab_set_color(TAB_COLOR_OTHER);
        if (SETTINGS.show_bulk_eod)
            tab_draw(ICON_MD_BATCH_PREDICTION " Last Day", &SETTINGS.show_bulk_eod, bulk_render);

        service_foreach_tabs();

        tab_draw(ICON_MD_MANAGE_SEARCH" Search ", nullptr, ImGuiTabItemFlags_Trailing, []() { symbols_render_search(nullptr); }, nullptr);

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
            dlg.window_opened_once = true;
        }

        if (ImGui::Begin(dlg.title, &dlg.opened, (ImGuiWindowFlags_NoCollapse) | (dlg.can_resize ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoResize)))
        {
            if (shortcut_executed(ImGuiKey_Escape))
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

FOUNDATION_STATIC void app_main_window(GLFWwindow* window, const char* window_title, float window_width, float window_height)
{
    ImGui::SetWindowPos(window_title, ImVec2(0, 0));
    ImGui::SetWindowSize(window_title, ImVec2(window_width, window_height));

    app_main_menu_begin(window);
    dispatcher_update();

    app_tabs();
    app_main_menu_end(window);

    app_render_dialogs();
    service_foreach_window();

    report_render_create_dialog(&SETTINGS.show_create_report_ui);
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

// 
// # PUBLIC API
//

void app_open_dialog(const char* title, app_dialog_handler_t&& handler, uint32_t width, uint32_t height, bool can_resize, app_dialog_close_handler_t&& close_handler, void* user_data)
{
    FOUNDATION_ASSERT(handler);

    for (unsigned i = 0, end = array_size(_dialogs); i < end; ++i)
    {
        app_dialog_t& dlg = _dialogs[i];
        if (string_equal(title, string_length(title), dlg.title, string_length(dlg.title)))
            return log_warnf(0, WARNING_UI, STRING_CONST("Dialog %s is already opened"), dlg.title);
    }

    app_dialog_t dlg{};
    dlg.can_resize = can_resize;
    if (width) dlg.width = width;
    if (height) dlg.height = height;
    dlg.handler = std::move(handler);
    dlg.close_handler = std::move(close_handler);
    dlg.user_data = user_data;
    string_copy(STRING_CONST_CAPACITY(dlg.title), title, string_length(title));
    array_push_memcpy(_dialogs, &dlg);
}

const char* app_title()
{
    return APP_TITLE;
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
    session_setup(nullptr);
    progress_initialize();
    
    jobs_initialize();
    query_initialize();
    dispatch(L0(app_update_window_title(window)));
    
    // App systems
    settings_initialize();
    service_initialize();

    return 0;
}

extern void app_shutdown()
{
    // Lets make sure all requests are finished 
    // before exiting shutting down other services.
    query_shutdown();
    
    // App systems
    service_shutdown();
    settings_shutdown();
    
    // Framework systems
    tabs_shutdown();
    app_dialog_shutdown();
    jobs_shutdown();
    progress_finalize();
    session_shutdown();
    string_table_shutdown();
}

extern void app_render(GLFWwindow* window, int frame_width, int frame_height)
{
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
