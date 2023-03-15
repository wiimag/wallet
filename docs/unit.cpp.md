File Structure (h/cpp)
======================

The following give an example of how most header and unit cpp files should be organized.

## Header (.h)

```cpp
/*
 * Copyright 2023 equals-forty-two.com. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

/* =======> Same level header inclusion <======== */

#include "function.h"

/* =======> Foundation header inclusion <======== */

#include <foundation/hash.h>

/* =======> Forward declarations <======== 
            - Use as most forward declaration as you instead of including other header files.
*/

typedef struct GLFWwindow GLFWwindow;

/* =======> Pratical typedefs <======== */

typedef hash_t dispatcher_event_name_t;
typedef uint32_t dispatcher_event_listener_id_t;

/* =======> Macros and constants <======== */
#define INVALID_DISPATCHER_EVENT_LISTENER_ID (0U)

/* =======> Enums and flags <======== */

typedef enum DispatcherEventOption : uint32_t
{
    DISPATCHER_EVENT_OPTION_NONE = 0,
} dispatcher_event_option_t;
typedef uint32_t dispatcher_event_options_t;

/* =======> Data structures <======== */

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

/* =======> Any additional structural declarations <======== */

typedef bool (*dispatcher_event_callback_t)(const dispatcher_event_args_t& evt);
typedef function<bool(const dispatcher_event_args_t& evt)> dispatcher_event_handler_t;
typedef function<void(const dispatcher_event_args_t& evt)> dispatcher_event_void_handler_t;

/* =======> Public API declarations <======== 
            - Most API declaration should contain a documentation header using XMLDOC
*/

...

/// <summary>
/// Dispatch a call to the main thread on the next update/tick.
/// </summary>
/// <param name="callback">Callback to execute on the main thread</param>
/// <returns>Returns true if the callback was properly dispatched.</returns>
bool dispatch(const function<void()>& callback);

/// <summary>
/// Post an event from any thread to be invoked on the main thread when the dispatcher polls event the next time.
/// </summary>
/// <param name="name">Name of the event</param>
/// <param name="payload">Data to be copied and passed to listeners</param>
/// <param name="payload_size">Size of the data payload</param>
/// <param name="options">Post and execution options</param>
/// <returns></returns>
bool dispatcher_post_event(
    dispatcher_event_name_t name, 
    void* payload = nullptr, size_t payload_size = 0, 
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE);

...

/// <summary>
/// Unregister an event listener.
/// </summary>
/// <param name="name">Event name to search for a matching listener.</param>
/// <param name="callback">Static callback used to register the listener</param>
/// <returns>Returns false if a corresponding listener cannot be found.</returns>
template <size_t N> FOUNDATION_FORCEINLINE
bool dispatcher_unregister_event_listener(
    const char(&name)[N], 
    dispatcher_event_callback_t callback)
{
    FOUNDATION_ASSERT(name[N - 1] == '\0');
    return dispatcher_unregister_event_listener(name, N-1, callback);
}

```

## Module (.cpp)

```cpp
/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * DESCRIPTION OF THE MODULE, i.e. Render pad to test rendering code.
 */

/* =======> Main header file inclusion <======== */

#include "render_pad.h"

/* =======> Same level header inclusions <======== */

#include "app.h"

/* =======> Application framework header inclusions <======== */

#include <framework/math.h>
#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/service.h>
#include <framework/imgui_utils.h>
#include <framework/tabs.h>

/* =======> External 3rdparty header inclusions <======== 
            - Grouped per library
*/

#include <bx/math.h>
#include <bgfx/bgfx.h>

#include <imgui/imgui.h>

/* =======> Any other standard library header inclusions <======== */

#include <algorithms>

/* =======> MACROS <======== */

#define HASH_RENDER_PAD static_hash_string("render_pad", 10, 0xa3bded1790392649ULL)

/* =======> Private data structures <======== */

static struct render_pad_case_dip_filter_t {
    dip::Image img;

    infineis_case_t* icase{ nullptr };
    infineis_viewport_t orig_viewport{};

    infineis_frame_t modified_frame;
    infineis_viewport_t dip_viewport{};
};

/* =======> Global module variables <======== */

static render_pad_case_dip_filter_t _test_dip{};

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
// # PUBLIC
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

DEFINE_SERVICE(RENDER_PAD, render_pad_initialize, render_pad_shutdown, SERVICE_PRIORITY_MODULE);

//
// # TESTS
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