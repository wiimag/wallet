/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/config.h>
#include <framework/function.h>

#include <foundation/hash.h>
#include <foundation/atomic.h>
#include <foundation/thread.h>

typedef struct GLFWwindow GLFWwindow; // Required by #dispatcher_poll

 /*! Represents an invalid event listener id, usually returned by a call to #dispatcher_add_event_listener. */
constexpr uint32_t INVALID_DISPATCHER_EVENT_LISTENER_ID = (0U);

/*! Hashed dispatcher event name. */
typedef hash_t dispatcher_event_name_t;

/*! Event listener id type. */
typedef uint32_t dispatcher_event_listener_id_t;

/*! Dispatcher thread handle type. */
typedef object_t dispatcher_thread_handle_t;

/*! Dispatcher event options. */
typedef enum DispatcherEventOption : uint32_t
{
    /*! No options. */
    DISPATCHER_EVENT_OPTION_NONE = 0,

    /*! Copy the data payload. The memory is managed by the dispatcher. */
    DISPATCHER_EVENT_OPTION_COPY_DATA = 1 << 0,

    /*! The data payload is a config object. The memory is managed by the dispatcher. */
    DISPATCHER_EVENT_OPTION_CONFIG_DATA = 1 << 1,
} dispatcher_event_option_t;
typedef uint32_t dispatcher_event_options_t;

/*! Dispatcher event arguments. */
struct dispatcher_event_args_t
{
    /*! Size of the argument payload */
    size_t                     size;
    
    /*! Argument payload, memory is managed by the dispatcher */
    uint8_t*                   data;
    
    /*! Event options */
    dispatcher_event_options_t options;

    /*! User data. This pointer is passed back to registered listeners. */
    void*                      user_data;

    /*! Cast the data payload to a specific type. */
    template<typename T> const T* cast() const
    {
        FOUNDATION_ASSERT(sizeof(T) == size);
        return (const T*)data;
    }

    /*! Cast the data payload to a const char string. */
    const char* c_str() const
    {
        return (const char*)data;
    }
};

/*! Dispatcher event callback signature. 
 * 
 * @param evt Event arguments.
 */
typedef bool (*dispatcher_event_callback_t)(const dispatcher_event_args_t& evt);

/*! Dispatcher event void callback signature.
 *
 * @param evt Event arguments.
 * @return Listener must return true if the event was processed, false otherwise.
 */
typedef function<bool(const dispatcher_event_args_t& args)> dispatcher_event_handler_t;

/*! Dispatcher event void callback signature.
 *
 * @param evt Event arguments.
 */
typedef function<void(const dispatcher_event_args_t& args)> dispatcher_event_void_handler_t;

/*! Dispatcher event config callback signature.
 *
 * @param args Config value argument.
 * 
 * @return Listener must return true if the event was processed, false otherwise.
 */
typedef function<bool(const config_handle_t& args)> dispatcher_event_config_handler_t;

/*! Wake up main thread so it resumes immediately. */
void signal_thread();

/*! Initialize the dispatcher system. */
void dispatcher_initialize();

/*! Shutdown the dispatcher system. */
void dispatcher_shutdown();

/*! Dispatch a call to be executed on the main thread. 
 * 
 *  @param callback Callback to be executed.
 *  @param delay_milliseconds Delay in milliseconds before executing the call.
 * 
 *  @return True if the call was dispatched, false otherwise.
 */
bool dispatch(const function<void()>& callback, uint32_t delay_milliseconds = 0);

/*! Dispatch a call to be executed on the main thread for a given object.
 * 
 *  @param self     Object to call the callback on.
 *                  This object instance must remain valid until the callback is executed.
 *  @param callback Callback to be executed on the object instance.
 * 
 *  @return True if the call was dispatched, false otherwise.
 */
template<typename T, typename Callback>
bool dispatch_self(T* self, Callback callback, uint32_t delay_milliseconds = 0)
{
    FOUNDATION_ASSERT(self);
    FOUNDATION_ASSERT(callback);

    return dispatch([self, callback]()
    { 
        FOUNDATION_ASSERT(self);
        (self->*callback)(); 
    }, delay_milliseconds);
}

/*! Process events and run dispatched calls. */
void dispatcher_update();

/*! Poll events and run dispatched calls. 
 * 
 *  @param window Window handle.
 */
void dispatcher_poll(GLFWwindow* window);

/*! Post an event from any thread to be invoked on the main thread when the dispatcher polls event the next time.
 *
 *  @remark The data is only copied on the heap if the options parameter contains #DISPATCHER_EVENT_OPTION_COPY_DATA.
 * 
 *  @param name         Name of the event, name must be hashed with #string_hash before calling this function.
 *  @param payload      Data to be (copied and) passed to listeners
 *  @param payload_size Size of the data payload
 *  @param options      Post and execution options
 *
 *  @return True if the event was posted, false otherwise.
 */
bool dispatcher_post_event(
    dispatcher_event_name_t name, 
    void* payload = nullptr, size_t payload_size = 0, 
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE);

/*! Post an event from any thread to be invoked on the main thread when the dispatcher polls event the next time.
 * 
 *  @remark The data is only copied on the heap if the options parameter contains #DISPATCHER_EVENT_OPTION_COPY_DATA.
 * 
 *  @param event_name        Name of the event, must be a static string literal, i.e. "RENDER_FRAME"
 *  @param event_name_length Length of the event name
 *  @param payload           Data to be (copied and) passed to listeners
 *  @param payload_size      Size of the data payload
 *  @param options           Post and execution options
 *
 *  @return True if the event was posted, false otherwise.
 */
bool dispatcher_post_event(
    const char* event_name, size_t event_name_length,
    void* payload = nullptr, size_t payload_size = 0,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE);

/*! Post an event from any thread to be invoked on the main thread when the dispatcher polls event the next time.
 *
 *  @remark The data is only copied on the heap if the options parameter contains #DISPATCHER_EVENT_OPTION_COPY_DATA.
 *
 *  @param event_name   Name of the event, must be a static string literal, i.e. "RENDER_FRAME"
 *  @param payload      Data to be passed to listeners
 *  @param payload_size Size of the data payload
 *  @param options      Post and execution options
 *
 *  @return True if the event was posted, false otherwise.
 */
template <size_t N> FOUNDATION_FORCEINLINE
bool dispatcher_post_event(
    const char(&name)[N],
    void* payload = nullptr, size_t payload_size = 0,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE)
{
    FOUNDATION_ASSERT(name[N-1] == '\0');
    return dispatcher_post_event(name, N-1, payload, payload_size, options);
}

/*! Register an event listener that will be invoked when an event is posted.
 *
 *  @param name      Name of the event, name must be hashed with #string_hash before calling this function.
 *  @param callback  Callback to be invoked when the event is triggered.
 *  @param options   Registration and event execution options
 *  @param user_data User data to be passed to the callback
 * 
 *  @return New registered event listen ID or #INVALID_DISPATCHER_EVENT_LISTENER_ID if something failed.
 */
dispatcher_event_listener_id_t dispatcher_register_event_listener(
    dispatcher_event_name_t name, 
    const dispatcher_event_handler_t& callback,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE,
    void* user_data = nullptr);

/*! Register an event listener that will be invoked when an event is posted.
 * 
 *  @param event_name        Name of the event, must be a static string literal, i.e. "RENDER_FRAME"
 *  @param event_name_length Length of the event name
 *  @param callback          Callback to be invoked when the event is triggered.
 *  @param options           Registration and event execution options
 *  @param user_data         User data to be passed to the callback when invoked.
 *  
 *  @return New registered event listen ID or #INVALID_DISPATCHER_EVENT_LISTENER_ID if something failed.
 */
dispatcher_event_listener_id_t dispatcher_register_event_listener(
    const char* event_name, size_t event_name_length,
    const dispatcher_event_handler_t& callback,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE,
    void* user_data = nullptr);

/*! Register an event listener that will be invoked when an event is posted.
 * 
 *  @param event_name   Name of the event, must be a static string literal, i.e. "RENDER_FRAME"
 *  @param callback     Callback to be invoked when the event is triggered.
 *  @param options      Registration and event execution options
 *  @param user_data    User data to be passed to the callback when invoked.
 *  
 *  @return New registered event listen ID or #INVALID_DISPATCHER_EVENT_LISTENER_ID if something failed.
 */
template <size_t N> FOUNDATION_FORCEINLINE
dispatcher_event_listener_id_t dispatcher_register_event_listener(
    const char(&name)[N], 
    const dispatcher_event_handler_t& callback,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE,
    void* user_data = nullptr)
{
    FOUNDATION_ASSERT(name[N - 1] == '\0');
    return dispatcher_register_event_listener(name, N - 1, callback, options, user_data);
}

/*! Register an event listener that will be invoked when an event is posted with a #config_handle_t data payload.
 *
 *  @param event_name        Name of the event, must be a static string literal, i.e. "RENDER_FRAME"
 *  @param event_name_length Length of the event name
 *  @param callback          Callback to be invoked when the event is triggered. 
 *                           See #dispatcher_event_config_handler_t for the callback signature requirements.
 *  @param options           Registration and event execution options
 *  @param user_data         User data to be passed to the callback when invoked.
 *
 *  @return New registered event listen ID or #INVALID_DISPATCHER_EVENT_LISTENER_ID if something failed.
 */
template <size_t N> FOUNDATION_FORCEINLINE
dispatcher_event_listener_id_t dispatcher_register_event_listener_config(
    const char(&name)[N],
    const dispatcher_event_config_handler_t& callback,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_CONFIG_DATA,
    void* user_data = nullptr)
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
    return dispatcher_register_event_listener(name, N - 1, handler, options, user_data);
}

/*! Register an event listener that will be invoked when an event is posted.
 * 
 *  @remark The callback doesn't need to return true of false to indicate that it has done something, we assume it always do.
 *
 *  @param event_name        Name of the event, must be a static string literal, i.e. "RENDER_FRAME"
 *  @param event_name_length Length of the event name
 *  @param callback          Callback to be invoked when the event is triggered.
 *                           See #dispatcher_event_void_handler_t for the callback signature requirements.
 *  @param options           Registration and event execution options
 *  @param user_data         User data to be passed to the callback when invoked.
 *
 *  @return New registered event listen ID or #INVALID_DISPATCHER_EVENT_LISTENER_ID if something failed.
 */
template <size_t N> FOUNDATION_FORCEINLINE
dispatcher_event_listener_id_t dispatcher_register_event_listener_easy(
    const char(&name)[N], const dispatcher_event_void_handler_t& callback,
    dispatcher_event_options_t options = DISPATCHER_EVENT_OPTION_NONE,
    void* user_data = nullptr)
{
    auto handler = [callback](const dispatcher_event_args_t& args)->bool
    {
        callback.invoke(args);
        return true;
    };

    return dispatcher_register_event_listener(name, N - 1, handler, options, user_data);
}

/*! Unregister an event listener.
 *
 *  @param event_listener_id Event listener ID to unregister.
 *
 *  @return True if the listener was found and unregistered, false otherwise.
 */
bool dispatcher_unregister_event_listener(dispatcher_event_listener_id_t event_listener_id);

/*! Unregister an event listener. We use the name and the callback signature to find the previously registered listener.
 * 
 *  @remark #callback must be a statically declared function in order to be able to match any previously registered listener handler.
 *
 *  @param event_name        Name of the event, name must be hashed with #string_hash before calling this function.
 *  @param callback          Static callback used to register the listener
 *
 *  @return True if the listener was found and unregistered, false otherwise (i.e. it was never registered).
 */
bool dispatcher_unregister_event_listener(dispatcher_event_name_t name, dispatcher_event_callback_t callback);

/*! Unregister an event listener. We use the name and the callback signature to find the previously registered listener.
 *
 *  @remark #callback must be a statically declared function in order to be able to match any previously registered listener handler.
 *
 *  @param event_name        Name of the event, must be a static string literal, i.e. "RENDER_FRAME"
 *  @param event_name_length Length of the event name
 *  @param callback          Static callback used to register the listener
 *
 *  @return True if the listener was found and unregistered, false otherwise (i.e. it was never registered).
 */
bool dispatcher_unregister_event_listener(
    const char* event_name, size_t event_name_length,
    dispatcher_event_callback_t callback);

/*! Unregister an event listener.
 *
 *  @param event_name        Name of the event, must be a static string literal, i.e. "RENDER_FRAME"
 *  @param callback          Static callback used to register the listener
 *  @return True if the listener was found and unregistered, false otherwise (i.e. it was never registered).
 */
template <size_t N> FOUNDATION_FORCEINLINE
bool dispatcher_unregister_event_listener(
    const char(&name)[N], 
    dispatcher_event_callback_t callback)
{
    FOUNDATION_ASSERT(name[N - 1] == '\0');
    return dispatcher_unregister_event_listener(name, N-1, callback);
}

/*! Post an event to the dispatcher with a #config_handle_t data payload.
 * 
 *  @remark The event will only be posted to listener registered with #DISPATCHER_EVENT_OPTION_CONFIG_DATA.
 *
 *  @param name        Name of the event, name will be hashed with #string_hash
 *  @param args        Arguments to be passed serialized in a config data object and passed to the event handlers
 *
 *  @return True if the event was posted, false otherwise.
 */
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

/*! Sends a signal to have the main thread wake up in case 
 *  it was waiting with a call to #dispatcher_wait_for_wakeup_main_thread.
 */
void dispatcher_wakeup_main_thread();

/*! Waits for the main thread to be woken up by a call to #dispatcher_wakeup_main_thread.
 *
 *  @param timeout_ms Timeout in milliseconds to wait for the main thread to be woken up.
 *
 *  @return True if the main thread was woken up, false otherwise (i.e. the timeout was reached).
 */
bool dispatcher_wait_for_wakeup_main_thread(int timeout_ms = 100);

/*! Creates a thread that will be managed by the dispatcher system and started immediately.
 *  The thread will be joined when the dispatcher is shut down.
 * 
 *  @remark The Thread can be stopped or aborted with a call to #dispatcher_thread_stop.
 *
 *  @param name         Name of the thread (Useful when displayed in a debugger)
 *  @param name_length  Length of the thread name
 *  @param thread_fn    Function to be called in the thread
 *  @param completed_fn Function to be called when the thread has completed.
 *                      Always dispatched to the main thread.
 *  @param user_data    User data to be passed to the thread function
 *
 *  @return Handle to the thread, or #INVALID_DISPATCHER_THREAD_HANDLE if something failed.
 */
dispatcher_thread_handle_t dispatch_thread(
    const char* name, size_t name_length, 
    const function<void*(void*)>& thread_fn, 
    const function<void(void)> completed_fn, 
    void* user_data = nullptr);

/*! Creates a thread that will be managed by the dispatcher system and started immediately.
 * 
 *  @remark The Thread can be stopped or aborted with a call to #dispatcher_thread_stop.
 * 
 *  @param name         Name of the thread (Useful when displayed in a debugger)
 *  @param thread_fn    Function to be called in the thread
 * 
 *  @return Handle to the thread, or #INVALID_DISPATCHER_THREAD_HANDLE if something failed.
 */
template<size_t N>
FOUNDATION_FORCEINLINE dispatcher_thread_handle_t dispatch_thread(
    const char(&name)[N], const function<void* (void*)>& thread_fn)
{
    return dispatch_thread(name, N - 1, thread_fn, nullptr, nullptr);
}

/*! Creates a thread that will be managed by the dispatcher system and started immediately.
 *
 *  @remark The Thread can be stopped or aborted with a call to #dispatcher_thread_stop.
 *
 *  @param thread_fn    Function to be called in the thread
 *  @param completed_fn Function to be called when the thread has completed.
 *                      Always dispatched to the main thread.
 *
 *  @return Handle to the thread, or #INVALID_DISPATCHER_THREAD_HANDLE if something failed.
 */
FOUNDATION_FORCEINLINE dispatcher_thread_handle_t dispatch_thread(
    const function<void*(void*)>& thread_fn,
    const function<void(void)> completed_fn,
    void* user_data)
{
    return dispatch_thread(STRING_CONST("Dispatcher Thread"), thread_fn, completed_fn, user_data);
}

/*! Creates a thread that will be managed by the dispatcher system and started immediately.
 *
 *  @remark The Thread can be stopped or aborted with a call to #dispatcher_thread_stop.
 *
 *  @param thread_fn    Function to be called in the thread
 *
 *  @return Handle to the thread, or #INVALID_DISPATCHER_THREAD_HANDLE if something failed.
 */
FOUNDATION_FORCEINLINE dispatcher_thread_handle_t dispatch_thread(const function<void*(void*)>& thread_fn)
{
    return dispatch_thread(thread_fn, nullptr, nullptr);
}

/*! Creates a thread that will be managed by the dispatcher system and started immediately.
 *
 *  @remark The Thread can be stopped or aborted with a call to #dispatcher_thread_stop.
 *
 *  @param thread_fn    Function to be called in the thread
 *  @param completed_fn Function to be called when the thread has completed.
 *                      Always dispatched to the main thread.
 *
 *  @return Handle to the thread, or #INVALID_DISPATCHER_THREAD_HANDLE if something failed.
 */
FOUNDATION_FORCEINLINE dispatcher_thread_handle_t dispatch_thread(
    const function<void*(void*)>& thread_fn,
    const function<void(void)> completed_fn)
{
    return dispatch_thread(thread_fn, completed_fn, nullptr);
}

/*! Creates a thread that will be managed by the dispatcher system and started immediately.
 *
 *  @remark The utility of this function is that the thread function signature is simplified.
 *          Fire and forget style syntax :D
 *
 *  @param thread_fn    Function to be called in the thread
 *
 *  @return Handle to the thread, or #INVALID_DISPATCHER_THREAD_HANDLE if something failed.
 */
FOUNDATION_FORCEINLINE dispatcher_thread_handle_t dispatch_fire(function<void(void)>&& thread_fn)
{
    return dispatch_thread([thread_fn](void*)->void* { thread_fn(); return nullptr; }, nullptr);
}

/*! Makes a request to stop a dispatched thread. Care must be taken with this function as the 
 *  thread can be aborted which might not be what you want and create memory leaks of objects 
 *  that were released from the stack.
 * 
 *  @param thread_id        Handle to the thread to stop
 *  @param timeout_seconds  Timeout in seconds to wait for the thread to stop.
 * 
 *  @return True if the thread was stopped successfully, false if the thread was aborted.
 */
bool dispatcher_thread_stop(dispatcher_thread_handle_t thread_id, double timeout_seconds = 30.0);

/*! Checks if the dispatcher thread is still running. 
 * 
 *  @param thread_id Handle to the thread to check
 * 
 *  @return True if the thread is still running, false otherwise.
 */
bool dispatcher_thread_is_running(dispatcher_thread_handle_t thread_handle);
