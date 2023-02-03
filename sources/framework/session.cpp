/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "session.h"

#include "common.h"
#include "config.h"
#include "scoped_string.h"

#include <framework/imgui.h>
 
#include <foundation/environment.h>
#include <foundation/path.h>
#include <foundation/assert.h>
#include <foundation/fs.h>
#include <foundation/string.h>

static const char IMGUI_FILE_NAME[] = "imgui.ini";

static config_handle_t _session_config{};
static string_t _session_working_dir{};

static void session_cleanup()
{
    config_deallocate(_session_config);
    string_deallocate(_session_working_dir.str);
}

static void session_load_config()
{
    if (_session_config)
        return;
    
    string_const_t session_file_path = session_get_file_path();
    _session_config = config_parse_file(STRING_ARGS(session_file_path));
    if (!_session_config)
        _session_config = config_allocate();
}

void session_setup(const char* root_path)
{
    // Setup can be called multiple times, so cleaning up first.
    session_cleanup();
    
    _session_working_dir =  string_clone_string(environment_current_working_directory());

    // Make sure user dir is created
    const string_const_t& user_dir = session_get_user_dir();
    if (fs_make_directory(STRING_ARGS(user_dir)))
        ImGui::LoadIniSettingsFromDisk(session_get_user_file_path(STRING_CONST(IMGUI_FILE_NAME)).str);
    
    session_load_config();
}

void session_shutdown()
{
    session_save();
    session_cleanup();
}

void session_update()
{
    
}

string_const_t session_get_file_path()
{
   return session_get_user_file_path(STRING_CONST("session.json"));
}

const char* session_working_dir()
{
    return _session_working_dir.str;
}

void session_save()
{
    if (main_is_running_tests())
        return;

    const string_const_t& user_dir = session_get_user_dir();
    if (!fs_is_directory(STRING_ARGS(user_dir)))
        return;

    ImGui::SaveIniSettingsToDisk(session_get_user_file_path(STRING_CONST(IMGUI_FILE_NAME)).str);
    
    config_write_file(session_get_file_path(), _session_config,
          CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS |
          CONFIG_OPTION_WRITE_NO_SAVE_ON_DATA_EQUAL);
    
}

string_const_t session_get_user_dir()
{
    static thread_local char user_dir_buffer[BUILD_MAX_PATHLEN] = { '\0' };
    if (user_dir_buffer[0] != 0)
        return string_const(user_dir_buffer, string_length(user_dir_buffer));

    string_const_t app_dir = environment_application_directory();

    string_t unformatted_user_dir = string_copy(STRING_CONST_CAPACITY(user_dir_buffer), STRING_ARGS(app_dir));
    string_t user_dir = string_replace(STRING_ARGS(unformatted_user_dir), BUILD_MAX_PATHLEN, STRING_CONST("."), STRING_CONST(""), true);

    if (main_is_running_tests())
        user_dir = string_concat(STRING_CONST_CAPACITY(user_dir_buffer), STRING_ARGS(user_dir), STRING_CONST("_tests"));
    
    string_const_t profile_name{};
    if (environment_command_line_arg("session", &profile_name) && profile_name.length > 0)
    {
        user_dir = string_concat(STRING_CONST_CAPACITY(user_dir_buffer), STRING_ARGS(user_dir), STRING_CONST("_"));
        user_dir = string_concat(STRING_CONST_CAPACITY(user_dir_buffer), STRING_ARGS(user_dir), STRING_ARGS(profile_name));
    }
    user_dir = path_clean(STRING_ARGS(user_dir), BUILD_MAX_PATHLEN);

    return string_to_const(user_dir);
}

string_const_t session_get_user_file_path(const char* filename, size_t length, const char* prefix, size_t prefix_length, const char* extension, size_t extension_length, bool mkdir /*= false*/)
{
    string_t user_file_path_buffer = string_static_buffer(BUILD_MAX_PATHLEN);

    const string_const_t& user_dir = session_get_user_dir();

    string_t user_file_path = string_copy(STRING_ARGS(user_file_path_buffer), STRING_ARGS(user_dir));
    if (prefix != nullptr && prefix_length > 0)
    {
        user_file_path = path_concat(
            STRING_ARGS(user_file_path_buffer),
            STRING_ARGS(user_file_path),
            prefix, prefix_length);
    }

    user_file_path = path_concat(
        STRING_ARGS(user_file_path_buffer),
        STRING_ARGS(user_file_path),
        filename, length);

    if (extension != nullptr && extension_length > 0)
    {
        user_file_path = string_concat(STRING_ARGS(user_file_path_buffer), STRING_ARGS(user_file_path), STRING_CONST("."));
        user_file_path = string_concat(STRING_ARGS(user_file_path_buffer), STRING_ARGS(user_file_path), extension, extension_length);
    }

    user_file_path_buffer.str[user_file_path.length] = '\0';
    if (mkdir)
    {
        string_const_t dir_path = path_directory_name(STRING_ARGS(user_file_path));
        fs_make_directory(STRING_ARGS(dir_path));
    }
    return string_const(user_file_path.str, user_file_path.length);
}

bool session_key_exists(const char* keyname)
{
    session_load_config();
    return config_exists(_session_config, keyname, string_length(keyname));
}

bool session_get_bool(const char* keyname, bool default_value /*= false*/)
{
    session_load_config();
    return session_get_integer(keyname, default_value ? 1 : 0);
}

int session_get_integer(const char* keyname, int default_value /*= 0*/)
{
    session_load_config();
    return math_trunc(_session_config[keyname].as_number((double)default_value));
}

float session_get_float(const char* keyname, float default_value /*= 0.0f*/)
{
    session_load_config();
    return float(_session_config[keyname].as_number((double)default_value));
}

string_const_t session_get_string(const char* keyname, const char* default_value /*= NULL*/)
{
    session_load_config();
    return _session_config[keyname].as_string(default_value, string_length(default_value));
}

bool session_set_bool(const char* keyname, bool value)
{
    return session_set_integer(keyname, value ? 1 : 0);
}

bool session_set_integer(const char* keyname, int value)
{
    return config_set(_session_config, keyname, string_length(keyname), (double)value);
}

bool session_set_float(const char* keyname, float value)
{
    return config_set(_session_config, keyname, string_length(keyname), (double)value);
}

bool session_set_string(const char* keyname, const char* value, size_t value_length)
{
    return config_set(_session_config, keyname, string_length(keyname), value, value_length > 0 ? value_length : string_length(value));
}

bool session_clear_value(const char* keyname)
{
    return config_remove(_session_config, keyname, string_length(keyname));
}

bool session_clear_all_values()
{
    config_deallocate(_session_config);
    _session_config = config_allocate();
    return _session_config;
}
