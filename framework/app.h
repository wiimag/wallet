/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include "version.h"

// Include most common application headers
#include <framework/jobs.h>
#include <framework/tabs.h>
#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/module.h>
#include <framework/session.h>
#include <framework/progress.h>
#include <framework/profiler.h>
#include <framework/dispatcher.h>
#include <framework/string_table.h>
#include <framework/localization.h>

typedef object_t window_handle_t;
typedef struct GLFWwindow GLFWwindow;

typedef function<void(void*)> app_event_handler_t;
typedef function<bool(void*)> app_dialog_handler_t;
typedef function<void(void*)> app_dialog_close_handler_t;

typedef function<void(GLFWwindow* window)> app_update_handler_t;
typedef function<void(GLFWwindow* window, int frame_width, int frame_height)> app_render_handler_t;

/*! Set of flags used to customize the registration of a new menu item. */
typedef enum class AppMenu
{
    None = 0,

    /*! Insert the menu items after all other menu items, this helps preserve the system menu order. */
    Append = 1 << 0,

    /*! Menu item defines a shortcut */
    Shortcut = 1 << 1,

    /*! Append a separator after the menu item. */
    Separator = 1 << 2,

    /*! The menu item name is dynamic and won't be translated. */
    DynamicName = 1 << 3,
} app_menu_flags_t;
typedef AppMenu AppMenuFlags;
DEFINE_ENUM_FLAGS(AppMenu);

#if defined(FRAMEWORK_APP_IMPLEMENTATION)

#include <framework/app.impl.inl>

#else

/*! Returns the application title. */
extern const char* app_title();

/*! Renders application 3rdparty libs using ImGui. */
extern void app_render_3rdparty_libs();

/*! Handles exception at the application level.
 * 
 *  @param dump_file The path to the dump file.
 *  @param length    The length of the dump file.
 */
extern void app_exception_handler(void* args, const char* dump_file, size_t length);

/*! Configure the application features and framework core services. 
 * 
 *  @param config      The configuration to be modified.
 *  @param application The application to be configured.
 */
extern void app_configure(foundation_config_t& config, application_t& application);

/*! Initialize the application features and framework core services. 
 * 
 *  @param window The main window used to render the application (can be null)
 * 
 *  @return 0 if initialization was successful, otherwise an error code.
 */
extern int app_initialize(GLFWwindow* window);

/*! Shutdown the application features and framework core services. */
extern void app_shutdown();

/*! Called each tick to update the application state (i.e. prior to rendering). 
 * 
 *  @param window The main window used to render the application (can be null)
 */
extern void app_update(GLFWwindow* window);

/*! Called each tick to render the application state. 
 * 
 *  @param window       The main window used to render the application (can be null)
 *  @param frame_width  The width of the frame to be rendered.
 *  @param frame_height The height of the frame to be rendered.
 */
extern void app_render(GLFWwindow* window, int frame_width, int frame_height);

#endif

/*! Creates and open a dialog window.
 *
 *  @param title         The title of the dialog. The title string gets copied into managed memory.
 *  @param handler       The handler to be called when the dialog is opened.
 *  @param width         The width of the dialog.
 *  @param height        The height of the dialog.
 *  @param can_resize    Whether the dialog can be resized by the user.
 *  @param user_data     The user data to be passed to the handlers.
 *  @param close_handler The handler to be called when the dialog is closed.
 */
void app_open_dialog(
    const char* title, 
    const app_dialog_handler_t& handler, 
    uint32_t width, uint32_t height, bool can_resize, 
    void* user_data, const app_dialog_close_handler_t& close_handler);

/*! Open a new dialog using a simplified interface.
 * 
 *  @template VoidHandler The simplified handler signature.
 *
 *  @param title         The title of the dialog. The title string gets copied into managed memory.
 *  @param width         The width of the dialog.
 *  @param height        The height of the dialog.
 *  @param can_resize    Whether the dialog can be resized by the user.
 *  @param handler       The handler to be called when the dialog is opened.
 */
template<typename VoidHandler>
FOUNDATION_FORCEINLINE void app_open_dialog(
    const char* title,
    uint32_t width, uint32_t height, bool can_resize, 
    const VoidHandler& handler)
{
    app_open_dialog(title, [handler](void* user_data)->bool
    {
        FOUNDATION_ASSERT(user_data == nullptr);
        handler();
        return true;
    }, width, height, can_resize, nullptr, nullptr);
}

/*! Close all dialogs owned by the specified window. */
void app_close_dialogs(window_handle_t owner);

/*! Render all active dialogs for the current window. */
void app_dialogs_render();

/*! Entry point to render application menus as IMGUI menus. */
void app_menu_begin(GLFWwindow* window);

/*! Entry point to render application menus as IMGUI menus. */
void app_menu_end(GLFWwindow* window);

/*! Register an application menu item. 
 *
 *  @param context       The context of the menu item.
 *  @param path          The path of the menu item.
 *  @param shortcut      The shortcut of the menu item.
 *  @param flags         The flags of the menu item.
 *  @param handler       The handler to be called when the menu item is selected.
 *  @param user_data     The user data to be passed to the handler.
 */
void app_register_menu(
    hash_t context, 
    STRING_PARAM(path),
    STRING_PARAM(shortcut),
    app_menu_flags_t flags, 
    app_event_handler_t&& handler, void* user_data = nullptr);

/*! Render common help menu items. 
 *
 *  @param window The window to render the menu items for.
 */
void app_menu_help(GLFWwindow* window);

/*! Opens and render an input dialog used to query the user for an input string.
 *
 *  This dialog is modal and mainly useful to get a quick user input. In example
 *  to rename a document or to get the name of a new document.
 *
 *  @param title         The title of the dialog.
 *  @param apply_label   The label of the apply button.
 *  @param initial_value The initial value of the input field.
 *  @param hint          The hint of the input field.
 *  @param callback      The callback to be called when the dialog is closed.
 */
void app_open_input_dialog(
    STRING_PARAM(title), 
    STRING_PARAM(apply_label), 
    STRING_PARAM(initial_value), 
    STRING_PARAM(hint), 
    const function<void(string_const_t value, bool canceled)>& callback);
