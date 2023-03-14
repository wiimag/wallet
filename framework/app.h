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

/*! Set of flags used to customize the registration of a new menu item. */
typedef enum class AppMenuFlags
{
    None = 0,

    /*! Insert the menu items after all other menu items, this helps preserve the system menu order. */
    Append = 1 << 0,

    /*! Menu item defines a shortcut */
    Shortcut = 1 << 1,
} app_menu_flags_t;
DEFINE_ENUM_FLAGS(AppMenuFlags);

#if defined(FRAMEWORK_APP_IMPLEMENTATION)

#include <framework/app.impl.inl>

#else

/*! Returns the application title. */
extern const char* app_title();

/*! Handles exception at the application level.
 * 
 *  @param dump_file The path to the dump file.
 *  @param length    The length of the dump file.
 */
extern void app_exception_handler(const char* dump_file, size_t length);

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
    VoidHandler&& handler)
{
    app_open_dialog(title, [=](void* user_data)->bool
    {
        FOUNDATION_ASSERT(user_data == nullptr);
        handler();
        return true;
    }, width, height, can_resize, nullptr, nullptr);
}

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
 *  @param window The window to render the menu items for.
 */
void app_menu_help(GLFWwindow* window);
