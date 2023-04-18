/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/common.h>
#include <framework/function.h>

 /*! Window user handle. */
typedef object_t window_handle_t;

/*! Window application callback. */
typedef function<void(window_handle_t)> window_event_handler_t;
typedef function<void(window_handle_t, int, int)> window_resize_callback_t;

typedef enum class WindowFlags {
    None = 0,

    /*! Make new window size proportional to the desktop monitor size it will open on. */
    InitialProportionalSize = 1 << 0,

    /*! A Transient window do not restore and save any user window settings. */
    Transient = 1 << 1,

    /*! The window will be opened maximized. */
    Maximized = 1 << 2,

    /*! Their can only be one instance of that window. */
    Singleton = 1 << 3,

    /*! This flags give a dialog behavior to the window. */
    Dialog = 1 << 4,
    
} window_flags_t;
DEFINE_ENUM_FLAGS(WindowFlags);

/*! Create and open a new window.
 *
 *  @param window_id       The unique identifier of the window. The window id string gets copied into managed memory.
 *                         It is best to keep window ID simple and to use constant literals
 *  @param title           The title of the window. The title string gets copied into managed memory.
 *  @param title_length    The length of the title string.
 *  @param render_callback The callback to be called when the window is rendered.
 *  @param close_callback  The callback to be called when the window is closed.
 *  @param user_data       The user data used to restore any user data through #window_get_user_pointer
 *  @param flags           The window flags used to create and show the window.
 *
 *  @return              The handle of the window.
 */
window_handle_t window_open(
    const char* FOUNDATION_RESTRICT window_id, 
    const char* title, size_t title_length, 
    const window_event_handler_t& render_callback, 
    const window_event_handler_t& close_callback,
    void* user_data = nullptr, window_flags_t flags = WindowFlags::None);

/*! Create and open a new window.
 *
 *  @param title           The title of the window. The title string gets copied into managed memory.
 *  @param render_callback The callback to be called when the window is rendered.
 *  @param flags           The window flags used to create and show the window.
 *
 *  @return              The handle of the window.
 */
window_handle_t window_open(const char* title, const window_event_handler_t& render_callback, window_flags_t flags = WindowFlags::None);

/*! Create and open a new module window that will act as a singleton.
 *
 *  @param context         The context of the window. The context is used to identify the window.
 *  @param title           The title of the window. The title string gets copied into managed memory.
 *  @param title_length    The length of the title string.
 *  @param render_callback The callback to be called when the window is rendered.
 *  @param close_callback  The callback to be called when the window is closed.
 *  @param user_data       The user data used to restore any user data through #window_get_user_pointer
 *  @param flags           The window flags used to create and show the window and we add the #WindowFlags::Singleton flag.
 *
 *  @return              The handle of the window.
 */
window_handle_t window_open(
    hash_t context,
    const char* title, size_t title_length,
    const window_event_handler_t& render_callback,
    const window_event_handler_t& close_callback,
    void* user_data = nullptr, window_flags_t flags = WindowFlags::None);

/*! Returns the title string of the window.
 *
 *  @param window The handle of the window.
 *
 *  @return       The title string of the window.
 */
const char* window_title(window_handle_t window);

/*! Returns any user data associated with the window.
 * 
 *  @note         The user data is set through #window_open.
 *  @important    The user data is NOT managed by the window system.
 *
 *  @param window The handle of the window.
 *
 *  @return       The user data associated with the window.
 */
void* window_get_user_data(window_handle_t window);

/*! Sets the window title. The title string gets copied into managed memory.
 *
 *  @param window_handle The handle of the window.
 *  @param title         The title of the window.
 */
void window_set_title(window_handle_t window_handle, const char* title, size_t title_length);

/*! Sets the window render callback. The callback is called when the window is rendered.
 *
 *  @param window_handle The handle of the window.
 *  @param callback      The callback to be called when the window is rendered.
 */
void window_set_render_callback(window_handle_t window_handle, const window_event_handler_t& callback);

/*! Sets the resize window callback. The callback is called each time the window is resized.
 *
 *  @param window_handle The handle of the window.
 *  @param callback      The callback to be called when the window is resized.
 */
void window_set_resize_callback(window_handle_t window_handle, const window_resize_callback_t& callback);

/*! Sets the window close callback. The callback is called when the window is closed or destroyed.
 *
 *  @param window_handle The handle of the window.
 *  @param callback      The callback to be called when the window is closed.
 */
void window_set_close_callback(window_handle_t window_handle, const window_event_handler_t& callback);

/*! Sets the window menu render callback. The callback is called when the window menu is rendered.
 *
 *  @param window_handle The handle of the window.
 *  @param callback      The callback to be called when the window menu is rendered.
 */
void window_set_menu_render_callback(window_handle_t window_handle, const window_event_handler_t& callback);

/*! Focus the window.
 *
 *  @param window_handle The handle of the window.
 * 
 *  @return              True if the window was focused, false otherwise.
 */
bool window_focus(window_handle_t window_handle);

/*! Request to close the window.
 * 
 *  @param window_handle The handle of the window to be closed.
 */
void window_close(window_handle_t window_handle);

/*! Handle the main Windows/ menu items. */
void window_menu();

/*! Update the window system. */
void window_update();

/*! Checks if the window is valid.
 *
 *  @param window_handle The handle of the window.
 *
 *  @return              True if the window is valid, false otherwise.
 */
bool window_valid(window_handle_t window_handle);
