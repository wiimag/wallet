/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include "config.h"
#include "function.h"

#include <foundation/hash.h>

#define INVALID_DISPATCHER_EVENT_LISTENER_ID (0U)

typedef struct GLFWwindow GLFWwindow;

typedef hash_t dispatcher_event_name_t;
typedef uint32_t dispatcher_event_listener_id_t;

typedef enum DispatcherEventOption : uint32_t
{
    DISPATCHER_EVENT_OPTION_NONE = 0,
    DISPATCHER_EVENT_OPTION_COPY_DATA = 1 << 0,
    DISPATCHER_EVENT_OPTION_CONFIG_DATA = 1 << 1,
} dispatcher_event_option_t;
typedef uint32_t dispatcher_event_options_t;

struct dispatcher_event_args_t
{
    size_t size;
    uint8_t* data;
    dispatcher_event_options_t options;

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

typedef bool (*dispatcher_event_callback_t)(const dispatcher_event_args_t& evt);
typedef function<bool(const dispatcher_event_args_t& args)> dispatcher_event_handler_t;
typedef function<void(const dispatcher_event_args_t& args)> dispatcher_event_void_handler_t;
typedef function<bool(const config_handle_t& args)> dispatcher_event_config_handler_t;

/// <summary>
/// Wake up main thread so it resumes immediately.
/// </summary>
void signal_thread();

/// <summary>
/// Initialize the dispatcher system.
/// </summary>
void dispatcher_initialize();

/// <summary>
/// Shutdown the dispatcher system.
/// </summary>
void dispatcher_shutdown();

/// <summary>
/// Dispatch a call to the main thread on the next update/tick.
/// </summary>
/// <param name="callback">Callback to execute on the main thread</param>
/// <returns>Returns true if the callback was properly dispatched.</returns>
bool dispatch(const function<void()>& callback);

/// <summary>
/// Process events and run dispatched calls.
/// </summary>
void dispatcher_update();

/// <summary>
/// Poll posted events and execute their callbacks.
/// </summary>
/// <param name="window"></param>
void dispatcher_poll(GLFWwindow* window);

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

/// <summary>
/// Same as above, but uses a string to hash the event name.
/// </summary>
/// <param name="event_name">Name of the event</param>
/// <param name="event_name_length">Length of the event name to be posted</param>
/// <param name="payload">Data to be copied and passed to listeners</param>
/// <param name="payload_size">Size of the data payload</param>
/// <param name="options">Post and execution options</param>
/// <returns></returns>
bool dispatcher_post_event(
    const char* event_name, size_t event_name_length,
    void* payload = nullptr, size_t payload_size = 0,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE);

/// <summary>
/// Post an event from any thread to be invoked on the main thread when the dispatcher polls event the next time.
/// </summary>
/// <param name="name">Name of the event, must be a static string literal, i.e. "RENDER_FRAME"</param>
/// <param name="payload">Data to be copied and passed to listeners</param>
/// <param name="payload_size">Size of the data payload</param>
/// <param name="options">Post and execution options</param>
/// <returns></returns>
template <size_t N> FOUNDATION_FORCEINLINE
bool dispatcher_post_event(
    const char(&name)[N],
    void* payload = nullptr, size_t payload_size = 0,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE)
{
    FOUNDATION_ASSERT(name[N-1] == '\0');
    return dispatcher_post_event(name, N-1, payload, payload_size, options);
}

/// <summary>
/// Register an event listener that will be invoked when an event is posted.
/// </summary>
/// <param name="name">Name of the event to listen too.</param>
/// <param name="callback">Callback to be invoked when the event is triggered.</param>
/// <param name="options">Registration and event execution options</param>
/// <returns>Returns the id of the event listener that can be used to unregister the listener later.</returns>
dispatcher_event_listener_id_t dispatcher_register_event_listener(
    dispatcher_event_name_t name, 
    const dispatcher_event_handler_t& callback,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE);

/// <summary>
/// Register an event listener that will be invoked when an event is posted.
/// </summary>
/// <param name="event_name">Name of the event to listen too. This name will be hashed to 64-bits</param>
/// <param name="event_name_length">Length of the event name</param>
/// <param name="callback">Callback to be invoked when the event is triggered.</param>
/// <param name="options">Registration and event execution options</param>
/// <returns>Returns the id of the event listener that can be used to query the listener state.</returns>
dispatcher_event_listener_id_t dispatcher_register_event_listener(
    const char* event_name, size_t event_name_length,
    const dispatcher_event_handler_t& callback,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE);

/// <summary>
/// Register an event listener that will be invoked when an event is posted.
/// </summary>
/// <param name="name">Name of the event to listen too. @name must be a constant string literal</param>
/// <param name="callback">Callback to be invoked when the event is triggered.</param>
/// <param name="options">Registration and event execution options</param>
/// <returns>Returns true if the listener was successfully registered.</returns>
template <size_t N> FOUNDATION_FORCEINLINE
dispatcher_event_listener_id_t dispatcher_register_event_listener(
    const char(&name)[N], 
    const dispatcher_event_handler_t& callback,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE)
{
    FOUNDATION_ASSERT(name[N - 1] == '\0');
    return dispatcher_register_event_listener(name, N - 1, callback, options);
}

template <size_t N> FOUNDATION_FORCEINLINE
dispatcher_event_listener_id_t dispatcher_register_event_listener_config(
    const char(&name)[N],
    const dispatcher_event_config_handler_t& callback,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_CONFIG_DATA)
{
    FOUNDATION_ASSERT(name[N - 1] == '\0');
    FOUNDATION_ASSERT(options & DISPATCHER_EVENT_OPTION_CONFIG_DATA);
    auto handler = [callback](const dispatcher_event_args_t& args)->bool
    {
        FOUNDATION_ASSERT(args.options & DISPATCHER_EVENT_OPTION_CONFIG_DATA);
        FOUNDATION_ASSERT(args.size == sizeof(config_handle_t));
        const config_handle_t& cv = *(config_handle_t*)args.data;
        return callback.invoke(cv);
    };
    return dispatcher_register_event_listener(name, N - 1, handler, options);
}

/// <summary>
/// Register an event listener that will be invoked when an event is posted.
/// The callback doesn't need to return true of false to indicate that it has done something, we assume it always do.
/// </summary>
/// <param name="name">Name of the event to listen too. @name must be a constant string literal</param>
/// <param name="callback">Callback to be invoked when the event is triggered.</param>
/// <returns>Returns true if the listener was successfully registered.</returns>
template <size_t N> FOUNDATION_FORCEINLINE
dispatcher_event_listener_id_t dispatcher_register_event_listener_easy(
    const char(&name)[N], const dispatcher_event_void_handler_t& callback,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE)
{
    auto handler = [callback](const dispatcher_event_args_t& args)->bool
    {
        callback.invoke(args);
        return true;
    };

    return dispatcher_register_event_listener(name, N - 1, handler, options);
}

/// <summary>
/// Unregister an event listener.
/// </summary>
/// <param name="dispatcher_event_listener_id_t">Unique ID of the listener to be removed.</param>
/// <returns>Returns true if the listener was successfully unregistered.</returns>
bool dispatcher_unregister_event_listener(dispatcher_event_listener_id_t event_listener_id);

/// <summary>
/// Unregister an event listener.
/// </summary>
/// <param name="name">Event name to search for a matching listener.</param>
/// <param name="callback">Static callback used to register the listener</param>
/// <returns>Returns false if a corresponding listener cannot be found.</returns>
bool dispatcher_unregister_event_listener(dispatcher_event_name_t name, dispatcher_event_callback_t callback);

/// <summary>
/// Unregister an event listener.
/// </summary>
/// <param name="event_name">Event name to search for a matching listener.</param>
/// <param name="event_name_length">Length of the event name to search for.</param>
/// <param name="callback">Static callback used to register the listener</param>
/// <returns>Returns false if a corresponding listener cannot be found.</returns>
bool dispatcher_unregister_event_listener(
    const char* event_name, size_t event_name_length,
    dispatcher_event_callback_t callback);

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

template <size_t N, typename... Args> FOUNDATION_FORCEINLINE
bool dispatcher_post_event_config(const char(&name)[N], Args&&... args)
{
    config_handle_t cv = config_allocate(CONFIG_VALUE_ARRAY);

    ([&]{
        config_handle_t e = config_array_push(cv, CONFIG_VALUE_NIL);
        e << args;
    } (), ...);

    return dispatcher_post_event(name, N-1, &cv, sizeof(config_handle_t), DISPATCHER_EVENT_OPTION_CONFIG_DATA | DISPATCHER_EVENT_OPTION_COPY_DATA);
}

void dispatcher_wakeup_main_thread();

bool dispatcher_wait_for_wakeup_main_thread(int timeout_ms = 100);
