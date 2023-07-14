/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2023 Wiimag Inc. All rights reserved.
 */

#include "scripts.h"

#include <framework/app.h>
#include <framework/module.h>
#include <framework/memory.h>
#include <framework/session.h>
#include <framework/array.h>
#include <framework/window.h>
#include <framework/expr.h>
#include <framework/console.h>

struct SCRIPTS_MODULE
{
    script_t* scripts{};

} *_{ nullptr };

#define HASH_SCRIPTS static_hash_string("scripts", 7, 0xf71318a2c32e8e7eULL)

//
// PRIVATE
//

FOUNDATION_STATIC string_const_t scripts_config_path()
{
    return session_get_user_file_path(STRING_CONST("scripts.json"));
}

FOUNDATION_STATIC bool script_evaluate(script_t* script)
{
    if (script->show_console)
        console_show();

    string_const_t name = string_const(script->name, string_length(script->name));
    if (name.length >= 6 && name.str[0] == '\\' && name.str[1] == 'u')
        name = string_const(name.str + 6, name.length - 6);

    // Set global variables
    expr_set_global_var(STRING_CONST("$SCRIPT_NAME"), STRING_LENGTH(script->name));

    char formatted_name_buffer[256];
    string_t formatted_name = string_utf8_unescape(STRING_BUFFER(formatted_name_buffer), STRING_LENGTH(script->name));
    expr_set_global_var(STRING_CONST("$SCRIPT_NAME_FULL"), STRING_ARGS(formatted_name));

    // TODO: Guard against system exceptions.

    auto result = eval(STRING_LENGTH(script->text));
    if (EXPR_ERROR_CODE != EXPR_ERROR_NONE)
    {
        char formatted_name_buffer[256];
        string_t formatted_name = string_utf8_unescape(STRING_BUFFER(formatted_name_buffer), STRING_LENGTH(script->name));
        log_errorf(HASH_SCRIPTS, ERROR_SCRIPT, STRING_CONST("Failed to evaluate script '%.*s': %s"),
            STRING_FORMAT(formatted_name), EXPR_ERROR_MSG);
        console_show();
    }
    script->last_executed = time_now();
    array_sort(_->scripts, ARRAY_COMPARE_EXPRESSION(b.last_executed - a.last_executed));
    return result.as_boolean();
}

FOUNDATION_STATIC script_t* scripts_load()
{
    script_t* scripts = nullptr;

    config_handle_t cv;
    string_const_t scripts_config_file_path = scripts_config_path();
    if (fs_is_file(STRING_ARGS(scripts_config_file_path)))
        cv = config_parse_file(STRING_ARGS(scripts_config_file_path), CONFIG_OPTION_PRESERVE_INSERTION_ORDER);
    else
        cv = config_allocate(CONFIG_VALUE_ARRAY);

    for (auto e : cv)
    {
        if (config_type(e) != CONFIG_VALUE_OBJECT)
            continue;

        string_const_t name = e["name"].as_string();
        string_const_t text = e["text"].as_string();

        script_t script{};
        string_copy(STRING_BUFFER(script.name), STRING_ARGS(name));
        string_copy(STRING_BUFFER(script.text), STRING_ARGS(text));
        
        script.last_executed = e["last_executed"].as_time();
        script.last_modified = e["last_modified"].as_time();

        script.show_console = e["show_console"].as_boolean(false);
        script.load_on_startup = e["load_on_startup"].as_boolean(false);

        if (script.load_on_startup)
            script_evaluate(&script);

        array_push_memcpy(scripts, &script);
    }

    config_deallocate(cv);

    return array_sort(scripts, ARRAY_COMPARE_EXPRESSION(b.last_executed - a.last_executed));
}

FOUNDATION_STATIC void scripts_save(script_t* scripts)
{
    config_handle_t data = config_allocate(CONFIG_VALUE_ARRAY);

    for (unsigned i = 0, count = array_size(scripts); i< count; ++i)
    {
        script_t* script = scripts + i;
        
        config_handle_t cv = config_array_push(data, CONFIG_VALUE_OBJECT);

        config_set(cv, "name", STRING_LENGTH(script->name));
        config_set(cv, "text", STRING_LENGTH(script->text));

        config_set(cv, "last_executed", (double)script->last_executed);
        config_set(cv, "last_modified", (double)script->last_modified);

        config_set(cv, "show_console", script->show_console);
        config_set(cv, "load_on_startup", script->load_on_startup);
    }

    string_const_t scripts_config_file_path = scripts_config_path();
    config_write_file(scripts_config_file_path, data, CONFIG_OPTION_WRITE_NO_SAVE_ON_DATA_EQUAL);
    config_deallocate(data);
}

FOUNDATION_STATIC void script_render_window(window_handle_t win)
{
    script_t* script = (script_t*)window_get_user_data(win);
    FOUNDATION_ASSERT(script != nullptr);

    ImGui::AlignTextToFramePadding();
    ImGui::TrTextUnformatted("Name");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
    if (ImGui::InputText("##Name", STRING_BUFFER(script->name)))
    {
        script->last_modified = time_now();
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Show console", &script->show_console))
    {
        script->last_modified = time_now();
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Run on startup", &script->load_on_startup))
    {
        script->last_modified = time_now();
    }

    ImGui::BeginDisabled(string_length(script->name) == 0 || string_length(script->text) == 0);
    if (script->is_new)
    {
        ImGui::SameLine();
        if (ImGui::Button(tr("Create")))
        {
            script->last_executed = time_now();
            script->last_modified = time_now();
            script->is_new = false;
            window_close(win);
        }
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Evaluate")))
    {
        script_evaluate(script);
    }
    ImGui::EndDisabled();

    if (!script->is_new)
    {
        ImGui::SameLine();
        if (ImGui::Button(tr("Delete")))
        {
            ImGui::OpenPopup(tr("Delete script?"));
        }

        if (ImGui::BeginPopupModal(tr("Delete script?"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            char formatted_name_buffer[256];
            string_t formatted_name = string_utf8_unescape(STRING_BUFFER(formatted_name_buffer), STRING_LENGTH(script->name));
            ImGui::TrText("Delete script %.*s?", STRING_FORMAT(formatted_name));
            ImGui::Separator();

            if (ImGui::Button(tr("Delete"), { ImGui::GetContentRegionAvailWidth() * 0.5f, 0.0f }))
            {
                ImGui::CloseCurrentPopup();
                array_erase_ordered_safe(_->scripts, (unsigned)(script - _->scripts));
                window_close(win);
            }

            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"), { ImGui::GetContentRegionAvailWidth(), 0.0f }))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    if (ImGui::InputTextMultiline("##Expression", STRING_BUFFER(script->text), ImGui::GetContentRegionAvail()))
    {
        script->last_modified = time_now();
    }
}

FOUNDATION_STATIC void script_close_new(window_handle_t win)
{
    script_t* new_script = (script_t*)window_get_user_data(win);
    if (!new_script->is_new)
    {
        array_push_memcpy(_->scripts, new_script);
    }

    if (new_script)
        MEM_DELETE(new_script);
}

FOUNDATION_STATIC void scripts_create_new()
{
    script_t* new_script = MEM_NEW(HASH_SCRIPTS, script_t);
    new_script->is_new = true;

    string_const_t title = RTEXT("New script");
    window_open(HASH_SCRIPTS, STRING_ARGS(title), script_render_window, script_close_new, new_script);
}

FOUNDATION_STATIC void scripts_menu()
{
    script_t* scripts = _->scripts;

    if(!ImGui::BeginMenuBar())
        return;

    if (!ImGui::TrBeginMenu("Scripts"))
        return ImGui::EndMenuBar();

    if (ImGui::TrMenuItem("Create..."))
        scripts_create_new();

    const unsigned scrit_count = array_size(scripts);

    if (scrit_count)
        ImGui::Separator();

    float max_label_width = 100.0f;
    for (unsigned i = 0, count = scrit_count; i < count; ++i)
    {
        script_t* script = scripts + i;
        max_label_width = math_max(max_label_width, ImGui::CalcTextSize(script->name).x);
    }
    
    for (unsigned i = 0, count = scrit_count; i < count; ++i)
    {
        script_t* script = scripts + i;

        char menu_name_buffer[256];
        string_t menu_name = string_utf8_unescape(STRING_BUFFER(menu_name_buffer), STRING_LENGTH(script->name));

        ImGui::PushID(script);
        ImGui::BeginGroup();
        if (ImGui::Selectable(menu_name.str, false, ImGuiSelectableFlags_AllowItemOverlap, {max_label_width, 0.0f}))
        {
            script_evaluate(script);
        }
        else if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::SetTooltip("%s", script->text);
        }

        ImGui::SameLine(max_label_width + IM_SCALEF(12));
        
        if (ImGui::SmallButton(ICON_MD_EDIT))
        {
            string_const_t name = string_const(script->name, string_length(script->name));
            if (name.length >= 6 && name.str[0] == '\\' && name.str[1] == 'u')
                name = string_const(name.str + 6, name.length - 6);

            string_t title = tr_format(SHARED_BUFFER(256), "{0} [Script]", name);
            window_open(HASH_SCRIPTS, STRING_ARGS(title), script_render_window, nullptr, script);
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_MD_DELETE_FOREVER))
        {
            ImGui::OpenPopup(tr("Delete script?"));
        }

        if (ImGui::BeginPopupModal(tr("Delete script?"), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            char formatted_name_buffer[256];
            string_t formatted_name = string_utf8_unescape(STRING_BUFFER(formatted_name_buffer), STRING_LENGTH(script->name));
            ImGui::TrText("Delete script %.*s?", STRING_FORMAT(formatted_name));
            ImGui::Separator();

            if (ImGui::Button(tr("Delete"), { ImGui::GetContentRegionAvailWidth() * 0.5f, 0.0f }))
            {
                array_erase_ordered_safe(_->scripts, i);
                i--;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"), { ImGui::GetContentRegionAvailWidth(), 0.0f }))
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::EndGroup();
        ImGui::PopID();
    }

    ImGui::EndMenu();
    ImGui::EndMenuBar();
}

//
// SYSTEM
//

FOUNDATION_STATIC void scripts_init()
{
    _ = MEM_NEW(HASH_SCRIPTS, SCRIPTS_MODULE);

    _->scripts = scripts_load();
    module_register_menu(HASH_SCRIPTS, scripts_menu);
}

FOUNDATION_STATIC void scripts_shutdown()
{
    scripts_save(_->scripts);
    array_deallocate(_->scripts);

    MEM_DELETE(_);
}

DEFINE_MODULE(SCRIPTS, scripts_init, scripts_shutdown, MODULE_PRIORITY_MODULE);
