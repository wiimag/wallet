/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "dispatcher.h"

#include <framework/glfw.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/shared_mutex.h>
#include <framework/profiler.h>
#include <framework/array.h>

#include <foundation/mutex.h>
#include <foundation/event.h>
#include <foundation/hashstrings.h>
#include <foundation/objectmap.h>

struct dispatcher_thread_t
{
    thread_t* thread{ nullptr };    
    void* payload{ nullptr };
    bool completed{ false };
    function<void* (void*)> thread_fn;
    function<void(void)> completed_fn;
};

struct dispatcher_event_listener_t
{
    dispatcher_event_listener_id_t id;
    dispatcher_event_name_t        event_name;
    dispatcher_event_handler_t     callback;
    dispatcher_event_options_t     options;
    void*                          user_data;
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

struct dispatcher_handler_t
{
    tick_t              trigger_at{ 0 };
    function<void()>    handler{ nullptr };
};

static int _wait_frame_throttling = 0;
static event_handle _wait_active_signal;

static mutex_t* _dispatcher_lock = nullptr;
static dispatcher_handler_t* _dispatcher_actions = nullptr;

static dispatcher_event_listener_id_t _next_listener_id = 1;
static event_stream_t* _event_stream = nullptr;
static dispatcher_event_listener_t* _event_listeners = nullptr;
static objectmap_t* _dispatcher_threads = nullptr;

//
// # PRIVATE
//

FOUNDATION_EXTERN bool dispatcher_process_events()
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
                    args.user_data = e->user_data;
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
        #if BUILD_APPLICATION
        const bool fully_active = window == nullptr || (glfw_is_window_focused(window) && glfw_is_any_mouse_button_down(window));
        if (!fully_active)
        #endif
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

bool dispatch(const function<void()>& callback, uint32_t delay_milliseconds /*= 0*/)
{
    static const tick_t ticks_per_milliseconds = time_ticks_per_second() / 1000LL;

    if (!mutex_lock(_dispatcher_lock))
    {
        log_errorf(0, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to lock dispatcher mutex"));
        return false;
    }
        
    dispatcher_handler_t d{};
    d.handler = callback;
    d.trigger_at = time_current() + ticks_per_milliseconds * delay_milliseconds;
    array_push_memcpy(_dispatcher_actions, &d);
    dispatcher_wakeup_main_thread();
    return mutex_unlock(_dispatcher_lock);
}

void dispatcher_update()
{
    PERFORMANCE_TRACKER("dispatcher_update");
    if (!mutex_try_lock(_dispatcher_lock))
        return;
    const tick_t now = time_current();
    const unsigned count = array_size(_dispatcher_actions);
    for (unsigned i = 0; i < count; ++i)
    {
        if (_dispatcher_actions[i].trigger_at <= now)
        {
            _dispatcher_actions[i].handler.invoke();
            _dispatcher_actions[i].handler.~function();
        }
        else
        {
            array_push_memcpy(_dispatcher_actions, &_dispatcher_actions[i]);
        }
    }

    // Delete all the actions that were executed this frame, 
    // those that were postponed were copied at the end of the array.
    array_erase_ordered_range_safe(_dispatcher_actions, 0, count);
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
    dispatcher_event_options_t options /*= DISPATCHER_EVENT_OPTION_NONE*/,
    void* user_data /*= nullptr*/)
{
    FOUNDATION_ASSERT(name != HASH_EMPTY_STRING);

    if (!mutex_lock(_dispatcher_lock))
    {
        log_errorf(0, ERROR_EXCEPTION, STRING_CONST("Failed to register event listener for %llu (0x%08x)"), name, options);
        return INVALID_DISPATCHER_EVENT_LISTENER_ID;
    }
    
    dispatcher_event_listener_t elistener;
    elistener.id = _next_listener_id++;
    elistener.event_name = name;
    elistener.options = options;
    elistener.user_data = user_data;
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
    dispatcher_event_options_t options /*= DISPATCHER_EVENT_OPTION_NONE*/,
    void* user_data /*= nullptr*/)
{
    return dispatcher_register_event_listener(string_hash(event_name, event_name_length), callback, options, user_data);
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
    else
    {
        log_errorf(0, ERROR_SYSTEM_CALL_FAIL, 
            STRING_CONST("Failed to lock dispatcher and unregister event listener %u"), event_listener_id);
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
    signal_thread();
    _main_thread_wake_up_event.signal();
}

bool dispatcher_wait_for_wakeup_main_thread(int timeout_ms)
{
    return _main_thread_wake_up_event.wait(timeout_ms) == 0;
}

FOUNDATION_STATIC void dispatch_execute_thread_completed(void* obj)
{
    dispatcher_thread_t* dt = (dispatcher_thread_t*)obj;
    if (dt == nullptr || dt->completed)
        return;
        
    FOUNDATION_ASSERT(dt->thread);
    FOUNDATION_ASSERT(!dt->completed);

    thread_signal(dt->thread);
    dt->completed = true;
    dt->thread_fn.~function();
    
    thread_t* thread = dt->thread;
    dispatch(dt->completed_fn);
    dispatch(L0(thread_deallocate(thread)));
    dt->thread = nullptr;
    
    memory_deallocate(dt);
}

dispatcher_thread_handle_t dispatch_thread(
    const char* name, size_t name_length, 
    const function<void*(void*)>& thread_fn, 
    const function<void(void)> completed_fn /*= nullptr*/,
    void* payload /*= nullptr*/)
{
    FOUNDATION_ASSERT(thread_fn);

    dispatcher_thread_handle_t thread_handle = objectmap_reserve(_dispatcher_threads);
    if (thread_handle == 0)
    {
        log_errorf(0, ERROR_OUT_OF_MEMORY, STRING_CONST("Failed to allocate thread handle"));
        return 0;
    }
    
    dispatcher_thread_t* dispatcher_thread = (dispatcher_thread_t*)memory_allocate(
        0, sizeof(dispatcher_thread_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);

    dispatcher_thread->payload = payload;
    dispatcher_thread->thread_fn = thread_fn;
    dispatcher_thread->completed_fn = completed_fn;
    dispatcher_thread->completed = false;

    if (!objectmap_set(_dispatcher_threads, thread_handle, dispatcher_thread))
    {
        log_errorf(0, ERROR_OUT_OF_MEMORY, STRING_CONST("Failed to store thread handle"));
        memory_deallocate(dispatcher_thread);
        objectmap_free(_dispatcher_threads, thread_handle);
        return 0;
    }

    dispatcher_thread->thread = thread_allocate([](void* thread_data)->void*
    {
        dispatcher_thread_handle_t dispatcher_thread_handle = to_opaque<dispatcher_thread_handle_t>(thread_data);
        dispatcher_thread_t* dt = (dispatcher_thread_t*)objectmap_acquire(_dispatcher_threads, dispatcher_thread_handle);
        if (dt == nullptr)
        {
            log_errorf(0, ERROR_INVALID_VALUE, STRING_CONST("Invalid thread handle or thread was already stopped"));
            return nullptr;
        }
        
        void* result_ptr = dt->thread_fn.invoke(dt->payload);
        
        if (objectmap_release(_dispatcher_threads, dispatcher_thread_handle, dispatch_execute_thread_completed))
        {
            dispatcher_wakeup_main_thread();

            if (objectmap_free(_dispatcher_threads, dispatcher_thread_handle))
                dispatch_execute_thread_completed(dt);
            else
                log_errorf(0, ERROR_INVALID_VALUE, STRING_CONST("Failed to free thread handle"));
        }

        return result_ptr;        
    }, to_ptr(thread_handle), name, name_length, THREAD_PRIORITY_NORMAL, 0);

    bool thread_started = thread_start(dispatcher_thread->thread);
    FOUNDATION_ASSERT(thread_started);
    
    return thread_handle;
}

bool dispatcher_thread_is_running(dispatcher_thread_handle_t thread_handle)
{
    dispatcher_thread_t* dt = (dispatcher_thread_t*)objectmap_acquire(_dispatcher_threads, thread_handle);
    if (!dt)
        return false;

    if (dt->completed)
    {
        objectmap_release(_dispatcher_threads, thread_handle, dispatch_execute_thread_completed);
        return false;
    }

    bool running = thread_is_running(dt->thread);
    objectmap_release(_dispatcher_threads, thread_handle, dispatch_execute_thread_completed);
    return running;
}

bool dispatcher_thread_stop(dispatcher_thread_handle_t thread_handle, double timeout_seconds /*= 30.0*/)
{
    bool thread_aborted = false;
    tick_t timeout = time_current();
    
    dispatcher_thread_t* dt = (dispatcher_thread_t*)objectmap_acquire(_dispatcher_threads, thread_handle);
    if (dt)
    {
        TIME_TRACKER(2.0, "Stopping dispatcher thread %.*s", STRING_FORMAT(dt->thread->name));
        
        while (!dt->completed && !thread_try_join(dt->thread, 100, nullptr) && time_elapsed(timeout) < timeout_seconds)
            dispatcher_wait_for_wakeup_main_thread(200);
        
        if (!dt->completed && thread_is_running(dt->thread))
        {
            log_warnf(0, WARNING_DEADLOCK, STRING_CONST("Thread %.*s did not stop in time (%.3lg), aborting..."),
                STRING_FORMAT(dt->thread->name), time_elapsed(timeout));

            thread_aborted = thread_kill(dt->thread);
        }

        if (objectmap_release(_dispatcher_threads, thread_handle, dispatch_execute_thread_completed))
        {
            dispatch_execute_thread_completed(dt);
            if (!objectmap_free(_dispatcher_threads, thread_handle))
                log_errorf(0, ERROR_INVALID_VALUE, STRING_CONST("Failed to free thread handle"));
        }
    }
    else
    {
        log_warnf(0, WARNING_INVALID_VALUE, STRING_CONST("Invalid thread handle or thread was already stopped"));
    }
    
    return !thread_aborted;
}

//
// # SYSTEM
//

void dispatcher_initialize()
{
    _event_stream = event_stream_allocate(256);
    _dispatcher_lock = mutex_allocate(STRING_CONST("Dispatcher"));
    _dispatcher_threads = objectmap_allocate(32);
}

void dispatcher_shutdown()
{
    foreach(e, _event_listeners)
        e->callback.~function();
    array_deallocate(_event_listeners);

    // Empty event queue by processing all remaining messages 
    // making sure any allocated memory is freed.
    dispatcher_update();
    dispatcher_process_events();

    event_stream_deallocate(_event_stream);
    _event_stream = nullptr;

    mutex_deallocate(_dispatcher_lock);
    array_deallocate(_dispatcher_actions);
    _dispatcher_lock = nullptr;
    _dispatcher_actions = nullptr;

    objectmap_deallocate(_dispatcher_threads);
    _dispatcher_threads = nullptr;
}
