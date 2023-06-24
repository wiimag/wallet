/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include <foundation/string.h>

typedef struct GLFWwindow GLFWwindow;
typedef struct string_const_t string_const_t;

/*! Setup the user session around the specified file path. 
 * 
 *  @param root_path The path of the file to open.
 */
void session_setup(const char* root_path = nullptr);

/*! Save to disk system and user configuration. 
 * 
 *  @return True if the settings were saved.
 */
void session_save();

/*! Cleanup any resources used by the current user session. */
void session_shutdown();

/*! Returns the current working directory (dir. path of the current file) 
 * 
 *  @return The current working directory.
 */
const char* session_working_dir();

/*! Returns the session root file if any. 
 * 
 *  @return The session root file.
 */
string_const_t session_get_file_path();

/*! Returns the user root directory where settings can be saved on disk. 
 * 
 *  @return The user root directory.
 */
string_const_t session_get_user_dir();

/*! Returns the full path of a user configuration file. 
 * 
 *  @param filename         The name of the file to retrieve.
 *  @param length           The length of the file name.
 *  @param prefix           The prefix to add to the file name.
 *  @param prefix_length    The length of the prefix.
 *  @param extension        The extension to add to the file name.
 *  @param extension_length The length of the extension.
 *  @param mkdir            True if the directory should be created if it does not exist.
 * 
 *  @return The full path of the user configuration file.
 */
string_const_t session_get_user_file_path(
    const char* filename, size_t length,
    const char* prefix = nullptr, size_t prefix_length = 0,
    const char* extension = nullptr, size_t extension_length = 0,
    bool mkdir = false);

/*! Returns the full path of a user configuration file. 
 *  The path is written to the buffer and the buffer is returned.
 * 
 *  @param buffer           The buffer to write the path to.
 *  @param capacity         The capacity of the buffer.
 *  @param filename         The name of the file to retrieve.
 *  @param length           The length of the file name.
 *  @param prefix           The prefix to add to the file name.
 *  @param prefix_length    The length of the prefix.
 *  @param extension        The extension to add to the file name.
 *  @param extension_length The length of the extension.
 *  @param mkdir            True if the directory should be created if it does not exist.
 * 
 *  @return The full path of the user configuration file.
 */
string_t session_get_user_file_path(
    char* buffer, size_t capacity,
    const char* filename, size_t length,
    const char* prefix, size_t prefix_length,
    const char* extension, size_t extension_length,
    bool mkdir);

/*! Checks if a user settings key exists. 
 * 
 *  @param keyname The name of the setting to check.
 * 
 *  @return True if the setting exists.
 */
bool session_key_exists(const char* keyname);

/*! Returns the boolean value of user setting. 
 * 
 *  @param keyname        The name of the setting to retrieve.
 *  @param default_value  The default value to return if the setting does not exist.
 * 
 *  @return The boolean value of the setting.
 */
bool session_get_bool(const char* keyname, bool default_value = false);

/*! Returns the integer value of user setting. 
 * 
 *  @param keyname        The name of the setting to retrieve.
 *  @param default_value  The default value to return if the setting does not exist.
 * 
 *  @return The integer value of the setting.
 */
int session_get_integer(const char* keyname, int default_value = 0);

/*! Returns the floating point value of user setting. 
 * 
 *  @param keyname        The name of the setting to retrieve.
 *  @param default_value  The default value to return if the setting does not exist.
 * 
 *  @return The floating point value of the setting.
 */
float session_get_float(const char* keyname, float default_value = 0.0f);

/*! Returns the string value of a user setting. 
 * 
 *  @param keyname        The name of the setting to retrieve.
 *  @param default_value  The default value to return if the setting does not exist.
 * 
 *  @return The string value of the setting.
 */
string_const_t session_get_string(const char* keyname, const char* default_value = nullptr);

/*! Returns the string value of a user setting. 
 * 
 *  @param keyname        The name of the setting to retrieve.
 *  @param buf            The buffer to store the string value.
 *  @param size           The size of the buffer.
 *  @param default_value  The default value to return if the setting does not exist.
 * 
 *  @return The string value of the setting.
 */
string_t session_get_string(const char* keyname, char* buf, size_t size, const char* default_value = nullptr);

/*! Sets the boolean value of a user setting. 
 * 
 *  @param keyname The name of the setting to set.
 *  @param value   The value to set.
 * 
 *  @return True if the setting was set.
 */
bool session_set_bool(const char* keyname, bool value);

/*! Sets the integer value of a user setting. 
 * 
 *  @param keyname The name of the setting to set.
 *  @param value   The value to set.
 * 
 *  @return True if the setting was set.
 */
bool session_set_integer(const char* keyname, int value);

/*! Sets the floating point value of a user setting. 
 * 
 *  @param keyname The name of the setting to set.
 *  @param value   The value to set.
 * 
 *  @return True if the setting was set.
 */
bool session_set_float(const char* keyname, float value);

/*! Sets the floating point value of a user setting. 
 * 
 *  @param keyname      The name of the setting to set.
 *  @param value        The value to set.
 *  @param value_length The length of the value.
 * 
 *  @return True if the setting was set.
 */
bool session_set_string(const char* keyname, const char* value, size_t value_length = 0);

/*! Clear a given user setting. 
 * 
 *  @param keyname The name of the setting to clear.
 * 
 *  @return True if the setting was cleared.
 */
bool session_clear_value(const char* keyname);

/*! Clear all user settings. 
 * 
 *  @return True if the settings were cleared.
 */
bool session_clear_all_values();

/*! Returns the list of string values of a user setting.
 * 
 *  The returned list needs to be freed by the caller using #string_array_deallocate
 * 
 *  @param keyname         The name of the setting to retrieve.
 * 
 *  @return The list of string values of the setting.
 */
string_t* session_get_string_list(const char* keyname);

/*! Sets the list of string values of a user setting. 
 * 
 *  @param keyname The name of the setting to set.
 *  @param strings The list of string values to set.
 * 
 *  @return True if the setting was set.
 */
bool session_set_string_list(const char* keyname, string_t* strings);
