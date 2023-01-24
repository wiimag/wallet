/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#pragma once

#include "option.h"

struct payload_t{};

typedef function<int(payload_t* payload)> job_handler_t;

typedef enum job_enum_flag_t : unsigned int {
    JOB_FLAGS_NONE = 0,

    JOB_DEALLOCATE_AFTER_EXECUTION = 1 << 10
} job_flag_t;
typedef unsigned int job_flags_t;

struct job_t
{
    job_flags_t flags { JOB_FLAGS_NONE };
    job_handler_t handler { nullptr };
    void* payload { nullptr };

    int status { 0 };
    volatile bool scheduled { false };
    volatile bool completed { false };
};

void jobs_initialize();

void jobs_shutdown();

job_t* job_allocate();

void job_deallocate(job_t* job);

job_t* job_execute(job_handler_t handler, void* payload = nullptr, job_flags_t flags = JOB_FLAGS_NONE);

bool job_completed(job_t* job);

void job_async();
void job_sync();
