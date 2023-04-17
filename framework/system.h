/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 *
 * System function APIs
 */

#include <framework/function.h>

#include <foundation/string.h>
#include <foundation/system.h>
#include <foundation/process.h>

/*! Executes a system command line opening an application, file or URL.
 *
 *  @param command System command to execute
 * 
 *  @return True if the command was executed successfully, false otherwise.
 */
bool system_execute_command(const char* command);

/*! Executes a system command line opening an application, file or URL.
 *
 *  @param command          System command to execute
 *  @param command_length   Length of command
 */
FOUNDATION_FORCEINLINE bool system_execute_command(const char* command, size_t command_length)
{
    static thread_local char _command_buffer[2048];
    string_t commandstr = string_copy(STRING_BUFFER(_command_buffer), command, command_length);
    return system_execute_command(commandstr.str);
}

/*! @bried Open the the file on the system. This will open the default application for the file type.
 *
 *  We produce a file:/// url
 *
 *  @param path Path to file
 *  @param path_length Length of path
 *  @param dir True if the path is a directory
 */
void system_browse_to_file(const char* path, size_t path_length, bool dir = false);

/*! @brief Returns the name of the platform the application is running on.
 *
 *  @return Name of the platform the application is running on.
 */
const char* system_platform_name(platform_t platform);

/*! @brief Returns the path to the application data folder.
 *
 *  @remark This is the folder where the application can store data that is not user specific.
 *
 *  @return Path to the application data folder.
 */
string_const_t system_app_data_local_path();

/*! Open a native dialog window to select a file of given type.
 * 
 * @param dialog_title              Dialog window title label
 * @param extension                 Set of extensions used in the dialog window (i.e. "DICOM (*.dcm)|*.dcm")
 * @param current_file_path         Current file path to open the dialog window at.
 * @param selected_file_callback    Callback invoked when a file is selected.
 * 
 * @return Returns true if the dialog window opened successfully.
 */
bool system_open_file_dialog(
    const char* dialog_title,
    const char* extension,
    const char* current_file_path,
    function<bool(string_const_t)> selected_file_callback);

/*! Acquire console resources for the application.
 * 
 *  @remark This is useful for applications that want to redirect stdout and stderr to the console, specifically on Windows.
 *
 *  @return True if the console was acquired successfully, false otherwise.
 */
bool system_process_redirect_io_to_console();

/*! Release the application console resources acquired by #system_process_acquire_console.
 *
 *  @return True if the console was released successfully, false otherwise.
 */
bool system_process_release_console();

/*! @brief Output a string to the system debugging console.
 *
 *  @param output String to output
 *  @param output_length Length of output
 */
void system_process_debug_output(const char* output, size_t output_length = 0);

/*! Helper function to execute a system tool process with arguments.
 *
 *  @param name Name of the tool to execute
 *  @param argv Array of arguments to pass to the tool
 *  @param argc Number of arguments in argv
 *  @param working_dir Working directory to execute the tool in
 *  @param working_dir_length Length of working_dir
 */
void system_execute_tool(const string_const_t& name, string_const_t* argv, size_t argc, const char* working_dir = 0, size_t working_dir_length = 0);

/*! Register a function callback to be invoked when the application thread exits.
 * 
 *  @remark The callback might be executed after the foundation resources have been released.
 *
 *  @param func Function to invoke when the application exits.
 */
void system_thread_on_exit(function<void()>&& func);

/*! Push a system notification.
 * 
 *  @param title            Title of the notification
 *  @param title_length     Length of title
 *  @param message          Message of the notification
 *  @param message_length   Length of message
 *  
 *  @return True if the notification was pushed successfully, false otherwise.
 */
bool system_notification_push(const char* title, size_t title_length, const char* message, size_t message_length);

/*! @brief Returns the system window handle.
 *
 *  @return System window handle.
 */
void* system_window_handle();

/*! Return the last system error code. 
 * 
 *  @param error_string     String to store the error message in.
 *  
 *  @return Last system error code.
 */
uint32_t system_get_last_error(string_const_t* error_string = nullptr);

/*! Returns the last system error formatted message. 
 * 
 *  @param error Pointer to store the error code in.
 *  
 *  @return Last system error formatted message.
 */
string_const_t system_get_last_error_message(uint32_t* error = nullptr);

/*! Extract a resource from the system executable and write it to a file.
 * 
 *  @param resource_name    Name of the resource to extract
 *  @param resource_type    Type of the resource to extract
 *  
 *  @return Path to the extracted resource file.
 */
string_const_t system_executable_resource_to_file(const char* resource_name, const char* resource_type);
