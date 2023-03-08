/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "window.h"

#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/service.h>

#include <foundation/objectmap.h>

#define HASH_WINDOW static_hash_string("window", 6, 0xa9008b1c524585c4ULL)

struct window_t
{
    object_t    handle{ 0 };
    GLFWwindow* glfw_window{ nullptr };
};


static struct WINDOW_MODULE {

    objectmap_t* windows{ nullptr };
    
} *_window_module;

// 
// # PRIVATE
//

void window_main_menu()
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Windows"))
        {
            if (ImGui::MenuItem(ICON_MD_LOGO_DEV " Test"))
            {
                window_open();
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

FOUNDATION_STATIC window_t* window_allocate(GLFWwindow* glfw_window)
{
    FOUNDATION_ASSERT(glfw_window);

    // Create window object
    object_t window_object_id = objectmap_reserve(_window_module->windows);
    if (window_object_id == OBJECT_INVALID)
    {
        log_errorf(HASH_WINDOW, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to create window object"));
        return OBJECT_INVALID;
    }

    // Allocate window object
    window_t* new_window = MEM_NEW(HASH_WINDOW, window_t);
    new_window->handle = window_object_id;
    new_window->glfw_window = glfw_window;

    // Store the window object
    if (!objectmap_set(_window_module->windows, window_object_id, new_window))
    {
        MEM_DELETE(new_window);
        log_errorf(HASH_WINDOW, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to store window object"));
        return OBJECT_INVALID;
    }

    return new_window;
}

FOUNDATION_STATIC void window_deallocate(window_t* window)
{
    if (!window)
        return;

    if (window->handle != OBJECT_INVALID)
        objectmap_free(_window_module->windows, window->handle);
        
    if (window->glfw_window)
        glfwDestroyWindow(window->glfw_window);
    MEM_DELETE(window);
}

//
// # PUBLIC API
//

window_handle_t window_open()
{
    // Create GLFW window
    glfwWindowHint(GLFW_FOCUSED, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
    GLFWwindow* glfw_window = glfwCreateWindow(1280, 720, "Window", nullptr, nullptr/*main_window()*/);
    if (glfw_window == nullptr)
    {
        log_errorf(HASH_WINDOW, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to create GLFW window"));
        return OBJECT_INVALID;
    }
    
    window_t* new_window = window_allocate(glfw_window);
    if (new_window == nullptr)
    {
        glfwDestroyWindow(glfw_window);
        log_errorf(HASH_WINDOW, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to create window"));
        return OBJECT_INVALID;
    }

    return new_window->handle;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void window_initialize()
{
    _window_module = MEM_NEW(HASH_WINDOW, WINDOW_MODULE);
    
    _window_module->windows = objectmap_allocate(32);

    service_register_menu(HASH_WINDOW, window_main_menu);
}

FOUNDATION_STATIC void window_shutdown()
{
    // Delete all windows
    for (size_t i = 0, end = objectmap_size(_window_module->windows); i < end; ++i)
    {
        window_t* win = (window_t*)objectmap_raw_lookup(_window_module->windows, i);
        window_deallocate(win);
    }
    objectmap_deallocate(_window_module->windows);

    MEM_DELETE(_window_module);
}

DEFINE_SERVICE(WINDOW, window_initialize, window_shutdown, SERVICE_PRIORITY_UI);
