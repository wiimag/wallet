/*
 * Copyright 2023 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#include "dispatcher.h"

#include "common.h"
#include "session.h"
#include "glfw.h"
#include "shared_mutex.h"
#include "profiler.h"

#include <foundation/mutex.h>
#include <foundation/array.h>
#include <foundation/semaphore.h>
#include <foundation/event.h>
#include <foundation/hashstrings.h>
#include <foundation/thread.h>

struct event_listener_t
{
    dispatcher_event_listener_id_t id;
    dispatcher_event_name_t event_name;
    dispatcher_event_handler_t callback;
    dispatcher_event_options_t options;
};

struct dispatcher_event_t
{
    dispatcher_event_name_t event_name;
    dispatcher_event_options_t options;

    void* data{ nullptr };
    size_t data_size{ 0 };
};

typedef enum DispatcherEventId : int32_t {
    DISPATCHER_NOEVENT = 0,
    DISPATCHER_POST_EVENT,
} dispatcher_event_id_t;

typedef function<void()> dispatcher_handler_t;

static int _wait_frame_throttling = 0;
static event_handle _wait_active_signal;

static mutex_t* _dispatcher_lock = nullptr;
static dispatcher_handler_t* _dispatcher_actions = nullptr;

static dispatcher_event_listener_id_t _next_listener_id = 1;
static event_stream_t* _event_stream = nullptr;
static event_listener_t* _event_listeners = nullptr;

//
// # PRIVATE
//

FOUNDATION_STATIC bool dispatcher_process_events()
{
    PERFORMANCE_TRACKER("dispatcher_process_events");

    event_block_t* eb = event_stream_process(_event_stream);
    event_t* ev = eb ? event_next(eb, nullptr) : nullptr;
    if (ev == nullptr || !mutex_lock(_dispatcher_lock))
        return false;
    
    while (ev)
    {
        if (ev->id == DISPATCHER_POST_EVENT)
        {
            FOUNDATION_ASSERT(ev->size >= sizeof(dispatcher_event_t));
            dispatcher_event_t* de = (dispatcher_event_t*)ev->payload;
            foreach(e, _event_listeners)
            {
                if (e->event_name == de->event_name)
                {
                    dispatcher_event_args_t args{};
                    args.data = (uint8_t*)de->data;
                    args.size = de->data_size;
                    args.options = de->options;
                    e->callback.invoke(args);
                }
            }

            if (de->options & DISPATCHER_EVENT_OPTION_CONFIG_DATA)
            {
                config_handle_t& cv = *(config_handle_t*)de->data;
                config_deallocate(cv);
            }

            if (de->options & DISPATCHER_EVENT_OPTION_COPY_DATA)
            {
                memory_deallocate(de->data);
            }
        }

        ev = event_next(eb, ev);
    }

    mutex_unlock(_dispatcher_lock);
    return true;
}

//
// # PUBLIC
//

void signal_thread()
{
    _wait_frame_throttling = max(0, _wait_frame_throttling - 10);
    _wait_active_signal.signal();
}

void dispatcher_poll(GLFWwindow* window)
{
    PERFORMANCE_TRACKER("dispatcher_poll");

    static int startup_frame_throttling = main_is_running_tests() ? 0 : session_get_integer("frame_throttling", 16);

    if (!dispatcher_process_events())
    {
        const bool fully_active = window == nullptr || (glfw_is_window_focused(window) && glfw_is_any_mouse_button_down(window));
        if (!fully_active)
        {
            // Throttle the rendering in order render less frequently and preserve more battery.
            _wait_frame_throttling = min(_wait_frame_throttling + 1, startup_frame_throttling);
            if (_wait_frame_throttling > 0 && _wait_active_signal.wait(_wait_frame_throttling))
            {
                // Frame was signaled/resumed
            }
        }
    }
}

bool dispatch(const function<void()>& callback)
{
    signal_thread();
    if (mutex_lock(_dispatcher_lock))
    {
        array_push_memcpy(_dispatcher_actions, &callback);
        mutex_unlock(_dispatcher_lock);
        return true;
    }

    return false;
}

void dispatcher_update()
{
    PERFORMANCE_TRACKER("dispatcher_update");
    if (!mutex_try_lock(_dispatcher_lock))
        return;
    for (unsigned i = 0, end = array_size(_dispatcher_actions); i < end; ++i)
        _dispatcher_actions[i]();
    array_clear(_dispatcher_actions);
    mutex_unlock(_dispatcher_lock);
}

bool dispatcher_post_event(
    dispatcher_event_name_t name,
    void* payload, size_t payload_size,
    dispatcher_event_options_t options /*= DISPATCHER_EVENT_OPTION_NONE*/)
{
    dispatcher_event_t de;
    de.event_name = name;
    de.options = options;
    if (options & DISPATCHER_EVENT_OPTION_COPY_DATA)
    {
        de.data = memory_allocate(HASH_EVENT, payload_size, 0, MEMORY_PERSISTENT);
        memcpy(de.data, payload, payload_size);
        de.data_size = payload_size;
    }
    else
    {
        de.data = payload;
        de.data_size = payload_size;
    }
    event_post(_event_stream, DISPATCHER_POST_EVENT, 0, 0, &de, sizeof(dispatcher_event_t));
    return true;
}

bool dispatcher_post_event(
    const char* event_name, size_t event_name_length,
    void* payload /*= nullptr*/, size_t payload_size /*= 0*/,
    dispatcher_event_options_t options /*= DISPATCHER_EVENT_OPTION_NONE*/)
{
    return dispatcher_post_event(string_hash(event_name, event_name_length), payload, payload_size, options);
}

dispatcher_event_listener_id_t dispatcher_register_event_listener(
    dispatcher_event_name_t name,
    const dispatcher_event_handler_t& callback,
    dispatcher_event_options_t options /*= DISPATCHER_EVENT_OPTION_NONE*/)
{
    FOUNDATION_ASSERT(name != HASH_EMPTY_STRING);

    if (!mutex_lock(_dispatcher_lock))
    {
        log_errorf(0, ERROR_EXCEPTION, STRING_CONST("Failed to register event listener for %llu (0x%08x)"), name, options);
        return INVALID_DISPATCHER_EVENT_LISTENER_ID;
    }
    
    event_listener_t elistener;
    elistener.id = _next_listener_id++;
    elistener.event_name = name;
    elistener.options = options;
    array_push_memcpy(_event_listeners, &elistener);

    /// The callback is assigned here, because we do not want the copy to be destroyed 
    /// with event_listener_t{} getting out of scope since we inserted the listener 
    //  using #array_push_memcpy
    array_last(_event_listeners)->callback = callback;

    mutex_unlock(_dispatcher_lock);
    return elistener.id;
}

dispatcher_event_listener_id_t dispatcher_register_event_listener(
    const char* event_name, size_t event_name_length,
    const dispatcher_event_handler_t& callback,
    dispatcher_event_options_t options /*= DISPATCHER_EVENT_OPTION_NONE*/)
{
    return dispatcher_register_event_listener(string_hash(event_name, event_name_length), callback, options);
}

bool dispatcher_unregister_event_listener(dispatcher_event_listener_id_t event_listener_id)
{
    bool unregistered = false;
    if (mutex_lock(_dispatcher_lock))
    {
        foreach(e, _event_listeners)
        {
            if (e->id == event_listener_id)
            {
                e->callback.~function();
                array_erase_memcpy_safe(_event_listeners, i);
                unregistered = true;
                break;
            }
        }

        mutex_unlock(_dispatcher_lock);
    }

    return unregistered;
}

bool dispatcher_unregister_event_listener(
    dispatcher_event_name_t name,
    dispatcher_event_callback_t callback)
{
    FOUNDATION_ASSERT(name != HASH_EMPTY_STRING);

    bool unregistered = false;
    if (mutex_lock(_dispatcher_lock))
    {
        foreach(e, _event_listeners)
        {
            if (e->event_name == name && (void*)e->callback.handler == callback)
            {
                e->callback.~function();
                array_erase_memcpy_safe(_event_listeners, i);
                unregistered = true;
                break;
            }
        }

        mutex_unlock(_dispatcher_lock);
    }

    return unregistered;
}

bool dispatcher_unregister_event_listener(
    const char* event_name, size_t event_name_length,
    dispatcher_event_callback_t callback)
{
    return dispatcher_unregister_event_listener(string_hash(event_name, event_name_length), callback);
}

static event_handle _main_thread_wake_up_event;
void dispatcher_wakeup_main_thread()
{
    _main_thread_wake_up_event.signal();
}

bool dispatcher_wait_for_wakeup_main_thread(int timeout_ms)
{
    return _main_thread_wake_up_event.wait(timeout_ms) == 0;
}

//
// # SYSTEM
//

void dispatcher_initialize()
{
    _event_stream = event_stream_allocate(256);
    _dispatcher_lock = mutex_allocate(STRING_CONST("Dispatcher"));
}

void dispatcher_shutdown()
{
    foreach(e, _event_listeners)
        e->callback.~function();
    array_deallocate(_event_listeners);
    event_stream_deallocate(_event_stream);
    _event_stream = nullptr;

    mutex_deallocate(_dispatcher_lock);
    array_deallocate(_dispatcher_actions);
    _dispatcher_lock = nullptr;
    _dispatcher_actions = nullptr;
}

#if BUILD_DEVELOPMENT

//
// # TESTS
//

#include <tests/test_utils.h>

#include <doctest/doctest.h>

TEST_SUITE("Dispatcher")
{
    TEST_CASE("Register")
    {
        SUBCASE("Default")
        {
            auto event_listener_id = dispatcher_register_event_listener("TEST_1", [](const auto& _1) { return false; });
            CHECK_NE(event_listener_id, INVALID_DISPATCHER_EVENT_LISTENER_ID);
            REQUIRE(dispatcher_unregister_event_listener(event_listener_id));
        }

        SUBCASE("Easy (No return value for handler)")
        {
            auto event_listener_id = dispatcher_register_event_listener_easy("EASY_1", [](const auto& _2) {});
            CHECK_NE(event_listener_id, INVALID_DISPATCHER_EVENT_LISTENER_ID);
            REQUIRE(dispatcher_unregister_event_listener(event_listener_id));
        }
    }

    TEST_CASE("Post Event")
    {
        SUBCASE("Default")
        {
            bool posted = false;
            auto event_listener_id = dispatcher_register_event_listener(S("POSTED_1"), [&posted](const auto& args)
            { 
                return (posted = true);
            });
            CHECK_NE(event_listener_id, INVALID_DISPATCHER_EVENT_LISTENER_ID);

            dispatcher_post_event("POSTED_1", nullptr, 0);
            dispatcher_process_events();

            REQUIRE(posted);
            REQUIRE(dispatcher_unregister_event_listener(event_listener_id));
        }

        SUBCASE("Easy (No return value for handler)")
        {
            bool posted = false;
            auto event_listener_id = dispatcher_register_event_listener_easy("EASY_33", [&posted](const auto& args)
            {
                posted = true;
            });
            CHECK_NE(event_listener_id, INVALID_DISPATCHER_EVENT_LISTENER_ID);
            
            dispatcher_post_event("EASY_33", nullptr, 0);
            dispatcher_process_events();

            REQUIRE(posted);
            REQUIRE(dispatcher_unregister_event_listener(event_listener_id));
        }

        SUBCASE("Post Event With Payload")
        {
            #define EVENT_POST_42_HASH static_hash_string("POST_42", 7, 0x981017af1d50240bULL)

            hash_t posted = HASH_EMPTY_STRING;
            auto postee = [&posted](const dispatcher_event_args_t& args)
            {
                return (posted = string_hash(args.c_str(), args.size));
            };

            auto event_listener_id = dispatcher_register_event_listener(EVENT_POST_42_HASH, postee);
            CHECK_NE(event_listener_id, INVALID_DISPATCHER_EVENT_LISTENER_ID);

            static char answer[42] = "life, the universe, and everything";
            dispatcher_post_event(EVENT_POST_42_HASH, STRING_CONST_CAPACITY(answer));
            dispatcher_process_events();

            REQUIRE_EQ(posted, string_hash(STRING_CONST_CAPACITY(answer)));
            REQUIRE(dispatcher_unregister_event_listener(event_listener_id));
        }
    }

    TEST_CASE("Main Thread Dispatch")
    {
        static bool main_thread_dispatched = false;

        TEST_RENDER_FRAME([]()
        {
            if (ImGui::SmallButton("DispatchCheck"))
                dispatch([](){ main_thread_dispatched = true; });
        }, L0(CLICK_UI("DispatchCheck")));

        REQUIRE_UI("DispatchCheck");
        TEST_RENDER_FRAME([]() { /* tick in order for main thread to dispatch last frame events */});

        REQUIRE(main_thread_dispatched);
    }

    TEST_CASE("Button Event Trigger" * doctest::may_fail(true))
    {
        static bool event_sent = false;
        auto eid = dispatcher_register_event_listener("UI_EVENT", L1(event_sent = true));

        TEST_RENDER_FRAME([]()
        {
            if (ImGui::Button("Post Event"))
                dispatcher_post_event("UI_EVENT");
        }, L0(CLICK_UI("Post Event")));

        REQUIRE_UI("Post Event");

        TEST_RENDER_FRAME([](){ /* tick in order for ui event to be dispatched */});
        CHECK(dispatcher_unregister_event_listener(eid));

        REQUIRE(event_sent);
    }
}

#endif
