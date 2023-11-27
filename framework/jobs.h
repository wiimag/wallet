/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include <framework/option.h>

struct payload_t{};

typedef function<int(payload_t* payload)> job_handler_t;

typedef enum job_enum_flag_t : uint32_t {
    JOB_FLAGS_NONE = 0,

    JOB_DEALLOCATE_AFTER_EXECUTION = 1 << 10
} job_flag_t;
typedef uint32_t job_flags_t;

struct job_t
{
    job_flags_t flags { JOB_FLAGS_NONE };
    job_handler_t handler { nullptr };
    void* payload { nullptr };
    size_t payload_size{  0};

    int status { 0 };
    volatile bool scheduled { false };
    volatile bool completed { false };
};

void jobs_initialize();

void jobs_shutdown();

job_t* job_allocate();

void job_deallocate(job_t*& job);

job_t* job_execute(const job_handler_t& handler, void* payload, size_t payload_size, job_flags_t flags);

FOUNDATION_FORCEINLINE job_t* job_execute(const job_handler_t& handler)
{
    return job_execute(handler, nullptr, 0, JOB_FLAGS_NONE);
}

FOUNDATION_FORCEINLINE job_t* job_execute(const job_handler_t& handler, void* payload)
{
    return job_execute(handler, payload, 0, JOB_FLAGS_NONE);
}

FOUNDATION_FORCEINLINE job_t* job_execute(const job_handler_t& handler, void* payload, job_flags_t flags)
{
    return job_execute(handler, payload, 0, flags);
}

bool job_completed(job_t* job);
