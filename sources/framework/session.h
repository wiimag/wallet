/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include <stddef.h>

typedef struct GLFWwindow GLFWwindow;
typedef struct string_const_t string_const_t;

/// Setup the user session around the specified file path
void session_setup(const char* root_path = nullptr);

/// <summary>
/// Periodically update the current session data
/// </summary>
void session_update();

/// <summary>
/// Save to disk system and user configuration.
/// </summary>
void session_save();

/// Cleanup any resources used by the current user session
void session_shutdown();

/// Returns the current working directory (dir. path of the current file)
const char* session_working_dir();

/// Returns the session root file if any
string_const_t session_get_file_path();

/// <summary>
/// Returns the user root directory where settings can be saved on disk.
/// </summary>
/// <returns></returns>
string_const_t session_get_user_dir();

/// <summary>
/// Returns the full path of a user configuration file.
/// </summary>
/// <param name="filename"></param>
/// <param name="length"></param>
/// <returns></returns>
string_const_t session_get_user_file_path(
    const char* filename, size_t length,
    const char* prefix = nullptr, size_t prefix_length = 0,
    const char* extension = nullptr, size_t extension_length = 0,
    bool mkdir = false);

/// <summary>
/// Checks if a user settings key exists.
/// </summary>
/// <param name="keyname"></param>
/// <returns></returns>
bool session_key_exists(const char* keyname);

/// <summary>
/// Returns the boolean value of user setting.
/// </summary>
/// <param name="keyname"></param>
/// <param name="default_value"></param>
/// <returns></returns>
bool session_get_bool(const char* keyname, bool default_value = false);

/// <summary>
/// Returns the integer value of user setting.
/// </summary>
/// <param name="keyname"></param>
/// <param name="default_value"></param>
/// <returns></returns>
int session_get_integer(const char* keyname, int default_value = 0);

/// <summary>
/// Returns the floating point value of user setting.
/// </summary>
/// <param name="keyname"></param>
/// <param name="default_value"></param>
/// <returns></returns>
float session_get_float(const char* keyname, float default_value = 0.0f);

/// <summary>
/// Returns the string value of a user setting.
/// </summary>
/// <param name="keyname"></param>
/// <param name="default_value"></param>
/// <returns></returns>
string_const_t session_get_string(const char* keyname, const char* default_value = NULL);

/// <summary>
/// Sets the boolean value of a user setting.
/// </summary>
/// <param name="keyname"></param>
/// <param name="value"></param>
/// <returns></returns>
bool session_set_bool(const char* keyname, bool value);

/// <summary>
/// Sets the integer value of a user setting.
/// </summary>
/// <param name="keyname"></param>
/// <param name="value"></param>
/// <returns></returns>
bool session_set_integer(const char* keyname, int value);

/// <summary>
/// Sets the floating point value of a user setting.
/// </summary>
/// <param name="keyname"></param>
/// <param name="value"></param>
/// <returns></returns>
bool session_set_float(const char* keyname, float value);

/// <summary>
/// Sets the floating point value of a user setting.
/// </summary>
/// <param name="keyname"></param>
/// <param name="value"></param>
/// <returns></returns>
bool session_set_string(const char* keyname, const char* value, size_t value_length = 0);

/// <summary>
/// Clear a given user setting.
/// </summary>
/// <param name="keyname"></param>
/// <returns></returns>
bool session_clear_value(const char* keyname);

/// <summary>
/// Clear all user settings.
/// </summary>
/// <returns></returns>
bool session_clear_all_values();
