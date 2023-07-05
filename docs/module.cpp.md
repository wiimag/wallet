File Structure (h/cpp)
======================

The following gives an example of how most header and module cpp files should be organized.

## Header (.h)

```cpp
/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2023 Wiimag inc. All rights reserved.
 */

#pragma once

#include <framework/common.h>

#include <foundation/hash.h>

// Forward declarations
typedef struct GLFWwindow GLFWwindow;

// Typedefs
typedef hash_t dispatcher_event_name_t;
typedef uint32_t dispatcher_event_listener_id_t;

// Macros and constants
#define INVALID_DISPATCHER_EVENT_LISTENER_ID (0U)

// Enums
typedef enum DispatcherEventOption : uint32_t
{
    DISPATCHER_EVENT_OPTION_NONE = 0,
} dispatcher_event_option_t;
typedef uint32_t dispatcher_event_options_t;

// Structs
struct dispatcher_event_args_t
{
    size_t size;
    uint8_t* data;

    template<typename T> const T* cast() const
    {
        FOUNDATION_ASSERT(sizeof(T) == size);
        return (const T*)data;
    }

    const char* c_str() const
    {
        return (const char*)data;
    }
};

// Any additional structural declarations

typedef bool (*dispatcher_event_callback_t)(const dispatcher_event_args_t& evt);
typedef function<bool(const dispatcher_event_args_t& evt)> dispatcher_event_handler_t;
typedef function<void(const dispatcher_event_args_t& evt)> dispatcher_event_void_handler_t;

// Public Function declarations

...

/*! Open a Tic-Tac-Toe window.
 *
 *  @param title Window title.
 *  @param width Window width.
 *  @param height Window height.
 *  @param out_window Pointer to a GLFWwindow* to be filled with the window handle.
 * 
 *  @return Returns true if the window was successfully opened.
 */
bool tic_tac_toe_open_window(const char* title, int width, int height, GLFWwindow** out_window);

...

```

## Module (.cpp)

```cpp
/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2022-2023 - All rights reserved.
 * 
 * DESCRIPTION OF THE MODULE, i.e. Render pad to test rendering code.
 */

#include "render_pad.h"

#include <framework/math.h>
#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/module.h>
#include <framework/imgui_utils.h>
#include <framework/tabs.h>

#include <algorithms>

#define HASH_RENDER_PAD static_hash_string("render_pad", 10, 0xa3bded1790392649ULL)

// Private structs and variables here

//
// # PRIVATE
//

/* =======> MOST PRIVATE FUNCTION SHOULD BE PREFIXED WITH FOUNDATION_STATIC <======== */

FOUNDATION_STATIC void render_pad_initialize_diplib()
{
    ...
}

FOUNDATION_STATIC void render_pad_render_viewport(const ImRect& rect, infineis_viewport_t* viewport)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    infineis_viewport_center_frame(viewport, rect);
    draw_list->AddCallback(infineis_viewport_render_command, viewport);
}

//
// # PUBLIC API
//

void render_pad()
{
    ...
}

//
// # SYSTEM
//

void render_pad_initialize()
{
    #if TEST_DIPLIB
        render_pad_initialize_diplib();
    #endif

    service_register_tabs(HASH_RENDER_PAD, render_pad);
}

void render_pad_shutdown()
{
    #if TEST_DIPLIB
        infineis_frame_unload(_test_dip.modified_frame);
    #endif
}

/* =======> MODULE SERVICE DEFINITION (IF ANY) <======== */

DEFINE_MODULE(RENDER_PAD, render_pad_initialize, render_pad_shutdown, SERVICE_PRIORITY_MODULE);

//
// # ANY PRIVATE TESTS (or use a separate test module under ./tests/render_pad_tests.cpp)
//

#include <doctest/doctest.h>

TEST_SUITE("RenderPad")
{
    TEST_CASE("Diplib")
    {
        SUBCASE("Case1")
        {
            ...
        }

        SUBCASE("Case2")
        {
            ...
        }

        SUBCASE("Case3")
        {
           ...
        }
    }
}

```