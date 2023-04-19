/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * This module contains application framework specific code. 
 * It is expected that the project sources also includes an app.cpp and defines the following functions:
 *  extern const char* app_title()
 *  extern void app_exception_handler(const char* dump_file, size_t length)
 *  extern void app_initialize()
 *  extern void app_shutdown()
 *  extern void app_update()
 *  extern void app_render()
 */

#include "app.h"
#include "version.h"

#include <framework/imgui.h>
#include <framework/module.h>
#include <framework/string.h>
#include <framework/console.h>
#include <framework/profiler.h>
#include <framework/array.h>
#include <framework/common.h>

#include <foundation/memory.h>
#include <foundation/version.h>
#include <foundation/stacktrace.h>
#include <foundation/hashstrings.h>

#define HASH_APP static_hash_string("app", 3, 0x6ced59ff7a1fae4bULL)

struct app_dialog_t
{
    char title[128]{ 0 };
    bool opened{ true };
    bool can_resize{ true };
    bool window_opened_once{ false };
    uint32_t width{ 480 }, height{ 400 };
    app_dialog_handler_t handler{};
    app_dialog_close_handler_t close_handler{};
    void* user_data{ nullptr };
};

struct app_menu_t
{
    hash_t context;
    char path[128]{ 0 };
    char shortcut[16]{ 0 };
    string_t* paths{ nullptr };

    app_menu_flags_t flags{ AppMenuFlags::None };

    /*! Shortcut key for the menu item */
    ImGuiKeyChord shortcut_key{ 0 };

    /*! Appended menu will usually be added after all other menu items */
    bool append_menu{ false };

    app_event_handler_t handler;
    void* user_data{ nullptr };
};

static app_menu_t* _menus = nullptr;
static app_dialog_t* _dialogs = nullptr;

//
// # PRIVATE
// 

FOUNDATION_STATIC void app_dialogs_shutdown()
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

FOUNDATION_STATIC void app_menus_shutdown()
{
    for (unsigned i = 0, end = array_size(_menus); i < end; ++i)
    {
        app_menu_t& menu = _menus[i];
        menu.handler.~function();
        string_array_deallocate(menu.paths);
    }

    array_deallocate(_menus);
}

FOUNDATION_STATIC bool app_menu_handle_shortcuts(GLFWwindow* window)
{
    foreach(menu, _menus)
    {
        const bool has_shortcut = menu->shortcut_key != 0;
        if (!has_shortcut)
            continue;

        if (ImGui::Shortcut(menu->shortcut_key, 0U, ImGuiInputFlags_RouteGlobalLow))
        {
            menu->handler(menu->user_data);
            return true;
        }
    }

    return false;
}

FOUNDATION_STATIC void app_menu(bool appended)
{
    if (!ImGui::BeginMenuBar())
        return;

    // Render register menus
    foreach(menu, _menus)
    {
        FOUNDATION_ASSERT(array_size(menu->paths) > 1);

        if (menu->append_menu != appended)
            continue;
        
        int menu_to_close_count = 0;
        for (unsigned i = 0, end = array_size(menu->paths); i < end; ++i)
        {
            const string_t& path = menu->paths[i];
           
           const char* path_tr = tr(STRING_ARGS(path), false).str;
            if (i == end - 1)
            {
                const char* shortcut = menu->shortcut[0] ? menu->shortcut : nullptr;
                if (ImGui::MenuItem(path_tr, shortcut, false, true))
                    menu->handler(menu->user_data);
            }
            else
            {
                if (!ImGui::BeginMenu(path_tr, true))
                    break;
                ++menu_to_close_count;
            }
        }

        for (int i = 0; i < menu_to_close_count; ++i)
            ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

FOUNDATION_STATIC ImGuiKeyChord app_string_to_shortcut_key_coord(const char* str, size_t str_length)
{
    ImGuiKeyChord key{ 0 };
    if (!str || !str_length)
        return key;

    const char* end = str + str_length;
    const char* p = str;
    while (p < end)
    {
        const char* next = p;
        while (next < end && *next != '+')
            ++next;

        string_const_t key_str{ p, (size_t)(next - p) };
        key_str = string_trim(key_str, ' ');

        if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Ctrl")) ||
            string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Command")))
        {
            #if FOUNDATION_PLATFORM_MACOS
            key |= ImGuiMod_Shortcut;
            #else
            key |= ImGuiMod_Ctrl;
            #endif
        }
        else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Control")))
            key |= ImGuiMod_Ctrl;
        else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Shift")))
            key |= ImGuiMod_Shift;
        else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Alt")))
            key |= ImGuiMod_Alt;
        else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Super")))
            key |= ImGuiMod_Super;
        else
        {
            if (key_str.length == 1)
            {
                key |= (uint32_t)key_str.str[0];
            }
            else
            {
                if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Space")))
                    key |= (uint32_t)' ';
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Enter")))
                    key |= (uint32_t)ImGuiKey_Enter;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Tab")))
                    key |= (uint32_t)ImGuiKey_Tab;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Backspace")))
                    key |= (uint32_t)ImGuiKey_Backspace;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Delete")))
                    key |= (uint32_t)ImGuiKey_Delete;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Insert")))
                    key |= (uint32_t)ImGuiKey_Insert;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Up")))
                    key |= (uint32_t)ImGuiKey_UpArrow;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Down")))
                    key |= (uint32_t)ImGuiKey_DownArrow;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Left")))
                    key |= (uint32_t)ImGuiKey_LeftArrow;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Right")))
                    key |= (uint32_t)ImGuiKey_RightArrow;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("PageUp")))
                    key |= (uint32_t)ImGuiKey_PageUp;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("PageDown")))
                    key |= (uint32_t)ImGuiKey_PageDown;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Home")))
                    key |= (uint32_t)ImGuiKey_Home;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("End")))
                    key |= (uint32_t)ImGuiKey_End;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Escape")))
                    key |= (uint32_t)ImGuiKey_Escape;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("Backspace")))
                    key |= (uint32_t)ImGuiKey_Backspace;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F1")))
                    key |= (uint32_t)ImGuiKey_F1;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F2")))
                    key |= (uint32_t)ImGuiKey_F2;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F3")))
                    key |= (uint32_t)ImGuiKey_F3;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F4")))
                    key |= (uint32_t)ImGuiKey_F4;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F5")))
                    key |= (uint32_t)ImGuiKey_F5;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F6")))
                    key |= (uint32_t)ImGuiKey_F6;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F7")))
                    key |= (uint32_t)ImGuiKey_F7;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F8")))
                    key |= (uint32_t)ImGuiKey_F8;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F9")))
                    key |= (uint32_t)ImGuiKey_F9;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F10")))
                    key |= (uint32_t)ImGuiKey_F10;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F11")))
                    key |= (uint32_t)ImGuiKey_F11;
                else if (string_equal_nocase(key_str.str, key_str.length, STRING_CONST("F12")))
                    key |= (uint32_t)ImGuiKey_F12;
                else
                {
                    log_warnf(0, WARNING_INVALID_VALUE, STRING_CONST("Unknown key %.*s"), (int)key_str.length, key_str.str);
                }
            }
        }

        p = next+1;
    }

    return key;
}

FOUNDATION_STATIC void app_dialogs_render()
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

//
// # PUPLIC API
//

void app_open_dialog(const char* title, const app_dialog_handler_t& handler, uint32_t width, uint32_t height, bool can_resize, void* user_data, const app_dialog_close_handler_t& close_handler)
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
    dlg.handler = handler;
    dlg.close_handler = close_handler;
    dlg.user_data = user_data;
    string_copy(STRING_BUFFER(dlg.title), title, string_length(title));
    array_push_memcpy(_dialogs, &dlg);
}

void app_register_menu(
    hash_t context, 
    const char* path, size_t path_length, 
    const char* shortcut, size_t shortcut_length,
    app_menu_flags_t flags, 
    app_event_handler_t&& handler, void* user_data)
{
    FOUNDATION_ASSERT(handler);

    app_menu_t menu{};
    menu.context = context;
    menu.handler = std::move(handler);
    menu.flags = flags;
    menu.user_data = user_data;
    menu.append_menu = test(flags, AppMenuFlags::Append);
    string_t full_path = string_copy(STRING_BUFFER(menu.path), path, path_length);
    string_t shortcutstr = string_copy(STRING_BUFFER(menu.shortcut), shortcut, shortcut_length);

    menu.shortcut_key = app_string_to_shortcut_key_coord(STRING_ARGS(shortcutstr));
    if (menu.shortcut_key != 0)
        menu.flags |= AppMenuFlags::Shortcut;

    #if FOUNDATION_PLATFORM_MACOS
    shortcutstr = string_replace(STRING_ARGS(shortcutstr), sizeof(menu.shortcut),
        STRING_CONST("Ctrl"), STRING_CONST(ICON_MD_KEYBOARD_COMMAND_KEY), false);

    shortcutstr = string_replace(STRING_ARGS(shortcutstr), sizeof(menu.shortcut),
        STRING_CONST("Alt"), STRING_CONST(ICON_MD_KEYBOARD_OPTION_KEY), false);

    shortcutstr = string_replace(STRING_ARGS(shortcutstr), sizeof(menu.shortcut),
        STRING_CONST("Control"), STRING_CONST(ICON_MD_KEYBOARD_CONTROL_KEY), false);
    #endif

    // Split path in parts
    menu.paths = string_split(STRING_ARGS(full_path), STRING_CONST("/"));
    FOUNDATION_ASSERT_MSG(array_size(menu.paths) > 1, "Menu path must have at least 2 parts, i.e. File/Settings");

    array_push_memcpy(_menus, &menu);
}

void app_menu_begin(GLFWwindow* window)
{
    app_menu_handle_shortcuts(window);
    app_menu(false);
}

void app_menu_end(GLFWwindow* window)
{
    app_menu(true);
}

void app_menu_help(GLFWwindow* window)
{
    if (!ImGui::TrBeginMenu("Help"))
           return;

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
        #if BUILD_TESTS
        ImGui::MenuItem("BUILD_TESTS");
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
    if (ImGui::TrMenuItem("Show Debug Logs", nullptr, &show_debug_log))
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
    if (ImGui::TrMenuItem("Show Memory Stats"))
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

            array_sort(memory_contexts, ARRAY_GREATER_BY(allocated_mem));

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

    #if BUILD_DEBUG && BUILD_ENABLE_MEMORY_TRACKER && BUILD_ENABLE_MEMORY_CONTEXT
    if (ImGui::TrMenuItem("Show Memory Usages"))
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

    ImGui::EndMenu();
}

//
// # SERVICE
//

FOUNDATION_STATIC void app_framework_initialize()
{
    //system_add_menu_item("test");

    module_register_window(HASH_APP, app_dialogs_render);
}

FOUNDATION_STATIC void app_framework_shutdown()
{
    app_menus_shutdown();
    app_dialogs_shutdown();
}

DEFINE_MODULE(APP, app_framework_initialize, app_framework_shutdown, MODULE_PRIORITY_SYSTEM);
