/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include <foundation/platform.h>
#include <foundation/hash.h>
#include <foundation/memory.h>
#include <foundation/time.h>

#if BUILD_ENABLE_PROFILE

#include <foundation/string.h>
#include <foundation/profile.h>

struct TrackerScope
{
    FOUNDATION_FORCEINLINE TrackerScope(const char* name, size_t name_length)
    {
        profile_begin_block(name, name_length);
    }

    template <size_t N> FOUNDATION_FORCEINLINE
    TrackerScope(const char(&name)[N])
        :TrackerScope(name, N-1)
    {
    }

    FOUNDATION_FORCEINLINE TrackerScope(int counter, const char* fmt, ...)
    {   
        FOUNDATION_UNUSED(counter);
        va_list list;
        va_start(list, fmt);
        char vname_buffer[64];
        string_t vname = string_vformat(STRING_BUFFER(vname_buffer), fmt, string_length(fmt), list);
        va_end(list);
        profile_begin_block(STRING_ARGS(vname));
    }

    FOUNDATION_FORCEINLINE ~TrackerScope()
    {
        profile_end_block();
    }
};

#define PERFORMANCE_TRACKER_NAME_COUNTER_EXPAND(NAME, COUNTER) TrackerScope __var_tracker__##COUNTER (NAME)
#define PERFORMANCE_TRACKER_NAME_COUNTER(NAME, COUNTER) PERFORMANCE_TRACKER_NAME_COUNTER_EXPAND(NAME, COUNTER)
#define PERFORMANCE_TRACKER(NAME) PERFORMANCE_TRACKER_NAME_COUNTER(NAME, __LINE__)

#define PERFORMANCE_TRACKER_FORMAT_COUNTER_EXPAND(COUNTER, FMT, ...) TrackerScope __var_tracker__##COUNTER(COUNTER, FMT, __VA_ARGS__)
#define PERFORMANCE_TRACKER_FORMAT_COUNTER(COUNTER, FMT, ...) PERFORMANCE_TRACKER_FORMAT_COUNTER_EXPAND(COUNTER, FMT, __VA_ARGS__)
#define PERFORMANCE_TRACKER_FORMAT(FMT, ...) PERFORMANCE_TRACKER_FORMAT_COUNTER(__LINE__, FMT, __VA_ARGS__)

void profiler_menu_timer();

#else

#define PERFORMANCE_TRACKER(NAME) (void)0;
#define PERFORMANCE_TRACKER_FORMAT(FMT, ...) (void)0;

FOUNDATION_FORCEINLINE void profiler_menu_timer() {}

#endif

#if BUILD_DEBUG && BUILD_ENABLE_PROFILE
struct TimeMarkerScope
{
    char label[128];
    hash_t context;
    tick_t start_time;
    const double less_ignored_elapsed_time = 0.0009;

    FOUNDATION_FORCEINLINE TimeMarkerScope(double max_time, hash_t _context, const char* fmt, ...)
        : context(_context)
        , less_ignored_elapsed_time(max_time)
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_BUFFER(label), fmt, string_length(fmt), list);
        va_end(list);

        start_time = time_current();
    }

    FOUNDATION_FORCEINLINE TimeMarkerScope(hash_t _context, const char* fmt, ...)
        : context(_context)
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_BUFFER(label), fmt, string_length(fmt), list);
        va_end(list);

        start_time = time_current();
    }

    template <size_t N> FOUNDATION_FORCEINLINE
        TimeMarkerScope(const char(&name)[N])
        : context(memory_context())
    {
        string_copy(STRING_BUFFER(label), name, N - 1);
        start_time = time_current();
    }

    FOUNDATION_FORCEINLINE TimeMarkerScope(const char* FOUNDATION_RESTRICT fmt, ...)
        : context(memory_context())
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_BUFFER(label), fmt, string_length(fmt), list);
        va_end(list);

        start_time = time_current();
    }

    FOUNDATION_FORCEINLINE TimeMarkerScope(double max_time, const char* FOUNDATION_RESTRICT fmt, ...)
        : context(memory_context())
        , less_ignored_elapsed_time(max_time)
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_BUFFER(label), fmt, string_length(fmt), list);
        va_end(list);

        start_time = time_current();
    }

    FOUNDATION_FORCEINLINE ~TimeMarkerScope()
    {
        const double elapsed_time = time_elapsed(start_time);
        if (elapsed_time > less_ignored_elapsed_time)
        {
            if (elapsed_time < 0.1)
                log_debugf(context, STRING_CONST("%s took %.3lg ms"), label, elapsed_time * 1000.0);
            else if (elapsed_time < 1.0)
                log_infof(context, STRING_CONST("%s took %.3lg ms"), label, elapsed_time * 1000.0);
            else
                log_warnf(context, WARNING_PERFORMANCE, STRING_CONST("%s took %.3lg seconds <<<"), label, elapsed_time);
        }
    }
};

#define TIME_TRACKER_NAME_COUNTER_EXPAND(COUNTER, ...) TimeMarkerScope __var_time_tracker__##COUNTER (__VA_ARGS__)
#define TIME_TRACKER_NAME_COUNTER(COUNTER, ...) TIME_TRACKER_NAME_COUNTER_EXPAND(COUNTER, __VA_ARGS__)
#define TIME_TRACKER(...) TIME_TRACKER_NAME_COUNTER(__LINE__, __VA_ARGS__)

#else

#define TIME_TRACKER(...)                       \
    do {                                        \
        FOUNDATION_UNUSED_VARARGS(__VA_ARGS__); \
    } while (0)

#endif

