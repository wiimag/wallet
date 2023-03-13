/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/common.h>

typedef struct GLFWwindow GLFWwindow;

typedef function<void(void*)> app_event_handler_t;
typedef function<bool(void*)> app_dialog_handler_t;
typedef function<void(void*)> app_dialog_close_handler_t;

typedef enum class AppMenuFlags
{
    None = 0,

    /*! Insert the menu items after all other menu items, this helps preserve the system menu order. */
    Append = 1 << 0,

    /*! Menu item defines a shortcut */
    Shortcut = 1 << 1,
} app_menu_flags_t;
DEFINE_ENUM_FLAGS(AppMenuFlags);

/*! Returns the application title. */
const char* app_title();

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
    app_dialog_handler_t&& handler, 
    uint32_t width = 0, uint32_t height = 0, bool can_resize = true, 
    void* user_data = nullptr, app_dialog_close_handler_t&& close_handler = nullptr);

/*! Entry point to render application dialogs as IMGUI floating windows. */
void app_dialogs_render();

/*! Entry point to render application menus as IMGUI menus. */
void app_menu_begin(GLFWwindow* window);

/*! Entry point to render application menus as IMGUI menus. */
void app_menu_end(GLFWwindow* window);

/*! Register an application menu item. 
 *
 *  @param context       The context of the menu item.
 *  @param path          The path of the menu item.
 *  @param path_length   The length of the path.
 *  @param shortcut      The shortcut of the menu item.
 *  @param shortcut_length The length of the shortcut.
 *  @param flags         The flags of the menu item.
 *  @param handler       The handler to be called when the menu item is selected.
 *  @param user_data     The user data to be passed to the handler.
 */
void app_register_menu(
    hash_t context, 
    const char* path, size_t path_length,
    const char* shortcut, size_t shortcut_length,
    app_menu_flags_t flags, 
    app_event_handler_t&& handler, void* user_data = nullptr);

/*! Render common help menu items. 
 *
 * @param window The window to render the menu items for.
 * */
void app_menu_help(GLFWwindow* window);
