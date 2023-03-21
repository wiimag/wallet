/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <framework/app.h>
#include <framework/glfw.h>
#include <framework/bgfx.h>
#include <framework/imgui.h>

#include <framework/common.h>
#include <framework/dispatcher.h>
#include <framework/profiler.h>
#include <framework/math.h>

// Include modules that might not be used but need to be initialized implicitly
#include <framework/about.h>

#include <foundation/foundation.h>

#if BUILD_DEVELOPMENT
static bool _run_tests = false;
#endif
#if BUILD_ENABLE_PROFILE
static double _smooth_elapsed_time_ms = 0.0f;
#endif

// Indicates if the application is running in daemon/batch mode which usually run headless.
static bool _batch_mode = false;
static bool _process_should_exit = false;

/*! Handles the debug break command line argument.
 * 
 *  If the debugger is attached, it will raise a debug break exception.
 *  If the debugger is not attached, it will wait for the debugger to attach.
 */
FOUNDATION_STATIC void main_handle_debug_break()
{
    if (!environment_command_line_arg("debug-break"))
        return;
    
    if (system_debugger_attached())
        exception_raise_debug_break();
    else
    {
        log_warn(0, WARNING_STANDARD, STRING_CONST("Waiting for debugger to attach..."));
        
        static bool debug_break_continue = false;
        static dispatcher_thread_handle_t wait_thread_handle{ 0 };
        if (main_is_graphical_mode())
        {
            wait_thread_handle = dispatch_fire([]()
            {
                system_message_box(STRING_CONST("Attach Debugger (Debug Break)"),
                    STRING_CONST("You can attach debugger now and press OK to continue..."), false);
                wait_thread_handle = {};
                debug_break_continue = true;
            });
        }

        while (!system_debugger_attached() && !_process_should_exit && !debug_break_continue)
            thread_sleep(1000);
        if (wait_thread_handle)
            dispatcher_thread_stop(wait_thread_handle);
    }
}

/*! This function checks if the framework can handle startup command line arguments */
FOUNDATION_STATIC int main_process_command_line(foundation_config_t& config, application_t& application)
{
    LOG_PREFIX(false);

    if (environment_command_line_arg("version"))
    {
        string_const_t version_string = string_from_version_static(application.version);
        fprintf(stdout, "%.*s\n", STRING_FORMAT(version_string));
        process_exit(0);        
        return 0;
    }

    return 0;
}

/*! Function invoked by foundation_lib to initialize the application.
 *
 *  @return 0 if successful, otherwise an error code.
 */
extern int main_initialize()
{
    WAIT_CURSOR;

    // Use default values for foundation config
    application_t application;
    foundation_config_t config;
    memset(&config, 0, sizeof config);
    memset(&application, 0, sizeof application);

    #if BUILD_ENABLE_MEMORY_TRACKER
        memory_set_tracker(memory_tracker_local());
    #endif
    
    #if BUILD_ENABLE_STATIC_HASH_DEBUG
        config.hash_store_size = 256;
    #endif
    app_configure(config, application);

    int init_result = foundation_initialize(memory_system_malloc(), application, config);
    if (init_result)
        return init_result;

    #if FOUNDATION_PLATFORM_WINDOWS
        log_enable_stdout(process_redirect_io_to_console() || environment_command_line_arg("build-machine"));
    #endif
    
    #if BUILD_DEVELOPMENT
    _run_tests = environment_command_line_arg("run-tests");
    #endif

    int command_line_result = main_process_command_line(config, application);
    if (command_line_result != 0 || _process_should_exit)
    {
        if (_process_should_exit)
            process_exit(command_line_result);
        return command_line_result;
    }

    if (environment_command_line_arg("debug") || environment_command_line_arg("verbose"))
        log_set_suppress(0, ERRORLEVEL_NONE);
    else
    {
        log_set_suppress(0, ERRORLEVEL_DEBUG);
     
        if (environment_command_line_arg("X"))
        {
            log_enable_prefix(false);
            log_enable_stdout(true);
        }
    }

    // Check if running batch mode (which is incompatible with running tests)
    const bool run_eval_mode = environment_command_line_arg("eval");
    _batch_mode = !main_is_running_tests() && (environment_command_line_arg("batch-mode") || run_eval_mode);

    dispatcher_initialize();
    main_handle_debug_break();

    GLFWwindow* window = nullptr;
    if (main_is_graphical_mode())
    {
        // This should create the main window not visible, 
        // then we show it after the initialization is done.
        window = glfw_main_window(app_title());
        if (!window)
        {
            log_error(0, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Fail to create main window context."));
            return ERROR_SYSTEM_CALL_FAIL;
        }

        bgfx_initialize(window);
        imgui_initiaize(window);
    }

    init_result = app_initialize(window);
    if (main_is_interactive_mode() && window)
    {
        // Show and focus the window once the main initialization is over.
        // We do this in order to prevent showing a stalling frame for too long (i.e. while loading assets).
        glfwShowWindow(window);
        glfwFocusWindow(window);
    }

    return init_result;
}

/*! Checks if the application is running in batch mode. 
 * 
 *  @todo Move to app.h
 */
extern bool main_is_batch_mode()
{
    return _batch_mode;
}

/*! Checks if the application is running in daemon mode.
 *
 *  @todo Move to app.h
 */
extern bool main_is_daemon_mode()
{
    return _batch_mode || main_is_running_tests();
}

/*! Checks if the application is running in graphical mode.
 *
 *  @remark Normally the graphical mode is used to open a window and render graphics.
 * 
 *  @todo Move to app.h
 */
extern bool main_is_graphical_mode()
{
    return !_batch_mode;
}

/*! Checks if the application is running in interactive mode.
 *
 *  @param exclude_debugger If true, running in the debugger will not be considered in interactive mode.
 *
 *  @todo Move to app.h
 * 
 *  @return True if the application is running in interactive mode, false otherwise.
 */
extern bool main_is_interactive_mode(bool exclude_debugger /*= false*/)
{
    if (_batch_mode)
        return false;
    #if BUILD_DEVELOPMENT
    if (_run_tests)
        return false;
    #endif
    if (exclude_debugger && system_debugger_attached())
        return false;
    return true;
}

/*! Checks if the application is running in test mode.
 *
 *  @todo Move to app.h
 */
extern bool main_is_running_tests()
{
    #if BUILD_DEVELOPMENT
        return _run_tests;
    #else
        return false;
    #endif
}

/*! Returns how much a batch of tick took time to execute in average.
 *
 *  @todo Move to app.h
 * 
 *  @return The average time in milliseconds.
 */
extern double main_tick_elapsed_time_ms()
{
    #if BUILD_ENABLE_PROFILE
        return _smooth_elapsed_time_ms;
    #else
        return 0;
    #endif
}

/*! Process system events that can affects the main application.
 *
 *  @param window The main window.
 */
FOUNDATION_STATIC void main_process_system_events(GLFWwindow* window)
{
    system_process_events();

    // Process all pending events in the event stream
    event_t* event = nullptr;
    event_stream_t* stream = system_event_stream();
    event_block_t* block = event_stream_process(stream);
    while ((event = event_next(block, event))) 
    {
        switch (event->id) 
        {
        case FOUNDATIONEVENT_START:
            break;

        case FOUNDATIONEVENT_TERMINATE:
            _process_should_exit = true;
            if (window)
                glfwSetWindowShouldClose(window, 1);
            break;

        case FOUNDATIONEVENT_FOCUS_GAIN:
        case FOUNDATIONEVENT_FOCUS_LOST:
            break;

        default:
            break;
        }
    }
}

/*! Main application update entry point.
 *
 *  @param window The main window.
 *  @param update The update handler.
 */
extern void main_update(GLFWwindow* window, const app_update_handler_t& update)
{
    PERFORMANCE_TRACKER("main_update");
    
    dispatcher_update();

    main_process_system_events(window);

    // Update application
    if (update)
        update(window);
}

/*! Main application render loop.
 *
 *  @param window The main window.
 *  @param render The render handler.
 *  @param begin The begin render handler.
 *  @param end The end render handler.
 */
extern void main_render(GLFWwindow* window, const app_render_handler_t& render, const app_render_handler_t& begin, const app_render_handler_t& end)
{
    PERFORMANCE_TRACKER("main_render");

    int frame_width = 1, frame_height = 1;
    const bool graphical_mode = !main_is_batch_mode();
    
    if (window)
    {
        glfwGetFramebufferSize(window, &frame_width, &frame_height);

        // Prepare next frame
        bgfx_new_frame(window, frame_width, frame_height);
        imgui_new_frame(window, frame_width, frame_height);

        if (begin)
            begin(window, frame_width, frame_height);
    }

    // Render application
    if (window && render)
    {
        PERFORMANCE_TRACKER("app_render");
        render(window, frame_width, frame_height);
    }
    
    if (window)
    {
        PERFORMANCE_TRACKER("imgui_render");

        // Render IMGUI frame
        ImGui::Render();
    }

    if (window && end)
        end(window, frame_width, frame_height);

    // Render everything
    if (window)
    {
        PERFORMANCE_TRACKER("bgfx_render_draw_lists");
        bgfx_render_draw_lists(ImGui::GetDrawData(), frame_width, frame_height);
    }

    if (window)
    {
        PERFORMANCE_TRACKER("bgfx_frame");
        bgfx::frame();
    }
}

/*! Main application loop.
 *
 *  @param window The main window. It can be null.
 */
extern void main_tick(GLFWwindow* window)
{
    PERFORMANCE_TRACKER("main_tick");

    main_update(window, app_update);

    if (window)
        main_render(window, app_render, nullptr, nullptr);
}

/*! Poll any windowing and dispatcher events that occurred since last tick.
 *
 *  @param window The main window. It can be null.
 *
 *  @return True if the application should continue running, false otherwise.
 */
extern bool main_poll(GLFWwindow* window)
{
    PERFORMANCE_TRACKER("main_poll");

    if (window)
        glfwPollEvents();
    dispatcher_poll(window);

    return window == nullptr || !glfwWindowShouldClose(window);
}

/*! Main application entry point invoked by the foundation platform.
 *
 *  @param context The application context.
 *
 *  @return The application exit code.
 */
extern int main_run(void* context)
{
    GLFWwindow* current_window = glfw_main_window();

    #if BUILD_DEVELOPMENT
    extern int main_tests(void* context, GLFWwindow* window);
    if (_run_tests)
        return main_tests(context, current_window);
    #endif

    _process_should_exit = environment_command_line_arg("exit");

    uint64_t frame_counter = 1;
    while (main_poll(current_window))
    {
        tick_t start_tick = time_current();
        main_tick(current_window);

        tick_t elapsed_ticks = time_diff(start_tick, time_current());

        #if BUILD_ENABLE_PROFILE
        static unsigned index = 0;
        static double elapsed_times[60] = { 0.0 };
        elapsed_times[index++ % ARRAY_COUNT(elapsed_times)] = time_ticks_to_milliseconds(elapsed_ticks);
        _smooth_elapsed_time_ms = math_average(elapsed_times, ARRAY_COUNT(elapsed_times));
        #endif
        
        if (_process_should_exit)
            return 0;

        profile_end_frame(frame_counter++);
    }

    return 0;
}

/*! Main application shutdown entry point. */
extern void main_finalize()
{    
    {
        WAIT_CURSOR;

        GLFWwindow* main_window = glfw_main_window();
        if (main_window && main_is_interactive_mode())
            glfw_save_window_geometry(main_window);

        app_shutdown();
        dispatcher_shutdown();

        if (main_is_graphical_mode())
        {
            bgfx_shutdown();
            imgui_shutdown();
        }

        if (log_stdout())
            process_release_console();
    }

    if (main_is_graphical_mode())
        glfw_shutdown();

    foundation_finalize();
}
