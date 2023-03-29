/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/platform.h>
#include <foundation/hash.h>
#include <foundation/memory.h>

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

struct MemoryScope
{
    FOUNDATION_FORCEINLINE MemoryScope(const hash_t& context)
    {
        memory_context_push(context);
    }

    FOUNDATION_FORCEINLINE ~MemoryScope()
    {
        memory_context_pop();
    }
};

#define MEMORY_TRACKER_NAME_COUNTER_EXPAND(HASH, COUNTER) MemoryScope __var_memory_tracker__##COUNTER (HASH)
#define MEMORY_TRACKER_NAME_COUNTER(HASH, COUNTER) MEMORY_TRACKER_NAME_COUNTER_EXPAND(HASH, COUNTER)
#define MEMORY_TRACKER(HASH) MEMORY_TRACKER_NAME_COUNTER(HASH, __LINE__)
