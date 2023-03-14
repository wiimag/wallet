Application Framework
=====================

This folder contains the sources to build the common application framework used to develop small, efficient and graphical aplications.

## Module Overview

### [app.h/cpp](app.h)

The app module contains the main application loop and the application state. It also contains the main entry point for the application.

You can use FRAMEWORK_APP_IMPLEMENTATION to implement the app module in a single source file. Then you can use various FRAMEWORK_APP_CUSTOM_* macros to customize the application behavior.

### [array.h/cpp](array.h)

The array module adds function extensions on top of the foundation_lib array type.

Here's an example of how to use the array module to sort an array of integers:

```cpp

#include <framework/array.h>

void example()
{
    int* values = nullptr;

    array_push(values, 3);
    array_push(values, 1);
    array_push(values, 2);

    // We use `a - b` to sort the array in ascending order since the array_sort 
    // function expect a comparison function that returns a negative value 
    // if a < b, 0 if a == b and a positive value if a > b.
    array_sort(values, [](int a, int b) { return a - b; });

    // The array is now sorted in ascending order: [1, 2, 3]

    array_deallocate(values);
}

```

Here's another example to use the `foreach` macro to iterate over the array:

```cpp

#include <framework/array.h>

void example()
{
    int* values = nullptr;

    array_push(values, 3);
    array_push(values, 1);
    array_push(values, 2);

    // Sort the array in descending order
    array_sort(values, [](int a, int b) { return b - a; });

    foreach (n, values)
    {
        log_debugf(0, STRING_CONST("%u: %d"), i, values[n]);
    }

    // The output is:
    // 0: 3
    // 1: 2
    // 2: 1

    array_deallocate(values);
}

```

### [bgfx.h/cpp](bgfx.h)

The bgfx module contains the code to initialize and shutdown the bgfx library. It also contains utility code to interact with the bgfx library at a higher level.

### [config.h/cpp](config.h)

The config module contains the code to load and save the application configuration. It also contains the code to load and save the application state. Basically, it allows you read and write JSON or SJSON stream.

Here's an example of how to use the config module to load and save the application state:

```cpp

#include <framework/config.h>

void save()
{
    config_t* config = config_allocate();

    config_set(config, STRING_CONST("window.width"), 1280);
    config_set(config, STRING_CONST("window.height"), 720);

    config_write_file(CTEXT("window.json"), config);

    config_deallocate(config);
}

void load()
{
    config_t* config = config_parse_file(STRING_CONST("window.json"));

    const int width = math_trunc(config["window.width"].as_number(DEFAULT_WIDTH));
    const int height = math_trunc(config["window.height"].as_number(DEFAULT_HEIGHT));

    create_window("MyGame", width, height);

    config_deallocate(config);
}

```

### [common.h/cpp](common.h)

All you can eat buffet of useful functions and macros. My workflow is first to declare utility functions in that module and then move them to their own module when I have a set of functions that serve the same purpose.

**Please note that if you depend on a function in that module, it might move to another module in the future.**

As for now you can find utility functions that extends `fs_*`, `path_*`, `time_*`, `process_*` foundation_lib APIs.

### [dispatcher.h/cpp](dispatcher.h)

The dispatcher module contains the code to dispatch events to the various application modules.

Here's an example of how to use the dispatcher module to dispatch an event to the application modules:

```cpp

#include <framework/dispatcher.h>

constexpr const char EVENT_MOM_IS_CALLING[] = "MOM_IS_CALLING";

void children()
{
    dispatcher_register_event_handler(EVENT_MOM_IS_CALLING, [](const dispatcher_event_args_t& args) 
    {
        log_warnf(0, WARNING_SUSPICIOUS, STRING_CONST("Mom is calling!"));
        return true; // Ok it's time to talk, the event has been handled.
    });
}

void mom()
{
    if (!dispatcher_post_event(EVENT_MOM_IS_CALLING))
        log_warnf(0, WARNING_SYSTEM_CALL_FAIL, STRING_CONST("Something is preventing me to reach my children!"));
}

```

### [jobs.h/cpp](jobs.h)

The jobs module contains the code to create and manage jobs. The framework lauches a set of threads that will execute the jobs. The jobs are executed in a FIFO order.

Here's an example of how to use the jobs module to execute a job:

```cpp

#include <framework/jobs.h>

#include <foundation/thread.h>

FOUNDATION_STATIC int message_thread(void* user_data)
{
    const char* message = (const char*)user_data;
    log_debugf(0, STRING_CONST("Job: %s"), message);
    return 0;
}

void example()
{
    constexpr const char* message = "Hello World!";
    job_t* task = job_execute(message_thread, message, JOB_DEALLOCATE_AFTER_EXECUTION);
    
    // Wait for the job to finish
    while (!job_completed(task))
        thread_yield();
}

```

### [service.h/cpp](service.h)

The service module contains the code to manage the application services.

Here's an example to define a new service module in your application:

```cpp

#include <framework/common.h>
#include <framework/service.h>

#define HASH_LOBBY static_string_hash("lobby", 5, 0x0ULL /*TODO update this key*/)

struct MODULE_LOBBY
{
    int* clients{ nullptr };
} *_lobby_module{ nullptr };

FOUNDATION_STATIC void lobby_render_window(void)
{
    // TODO: Render the lobby window
}

FOUNDATION_STATIC void lobby_initialize(void)
{
    // Create the lobby module
    _lobby_module = MEM_NEW(HASH_LOBBY, MODULE_LOBBY);

    // Fetch the data from the server
    _lobby_module->clients = lobby_get_clients();

    // Register various service application callbacks (see service.h)
    service_register_window(HASH_LOBBY, lobby_render_window);
}

FOUNDATION_STATIC void lobby_finalize(void)
{
    // Destroy the lobby module
    MEM_DELETE(_lobby_module);
}

DEFINE_SERVICE(LOBBY, lobby_initialize, lobby_finalize, SERVICE_PRIORITY_MODULE);

```

### [window.h/cpp](window.h)

The window module contains the code to create and manage the application window.

Here's an example to create a window:

```cpp

#include <framework/imgui.h>
#include <framework/window.h>

FOUNDATION_STATIC void timeline_render_window(window_handle_t window_handle)
{
    FOUNDATION_ASSERT(window_handle);

    const char* title = window_title(window_handle);
    FOUNDATION_ASSERT(title);

    ImGui::TextWrapped("This is the timeline window and its title is '%s'.", title);
}

void example()
{
    window_handle_t* window = window_open("Timeline", timeline_render_window, WindowFlag::Maximized);
}

```

### TODO

