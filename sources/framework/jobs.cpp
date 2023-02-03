/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "jobs.h"

#include "common.h"
#include "concurrent_queue.h"

#include <foundation/thread.h>
#include <foundation/semaphore.h>

#ifndef MAX_JOB_THREADS
#define MAX_JOB_THREADS 8
#endif

static thread_t* _job_threads[MAX_JOB_THREADS]{ nullptr };
static concurrent_queue<job_t*> _scheduled_jobs{};

static void* job_thread_fn(void* arg)
{
    job_t* job = nullptr;

    while (!thread_try_wait(1))
    {
        if (_scheduled_jobs.try_pop(job, 16))
        {
            job->status = job->handler((payload_t*)job->payload);
            job->completed = true;

            if (job->flags & JOB_DEALLOCATE_AFTER_EXECUTION)
                job_deallocate(job);
            signal_thread();
        }
    }

    // Empty jobs before exiting thread (prevent memory leaks)
    while (_scheduled_jobs.try_pop(job))
    {
        if (job->flags & JOB_DEALLOCATE_AFTER_EXECUTION)
            job_deallocate(job);
    }

    return 0;
}


void jobs_initialize()
{
    _scheduled_jobs.create();

    const size_t thread_count = ARRAY_COUNT(_job_threads);
    for (int i = 0; i < thread_count; ++i)
        _job_threads[i] = thread_allocate(job_thread_fn, nullptr, STRING_CONST("Jobber"), THREAD_PRIORITY_NORMAL, 0);

    for (int i = 0; i < thread_count; ++i)
        thread_start(_job_threads[i]);
}

void jobs_shutdown()
{
    const size_t thread_count = ARRAY_COUNT(_job_threads);
    for (size_t i = 0; i < thread_count; ++i)
    {
        while (thread_is_running(_job_threads[i]))
        {
            _scheduled_jobs.signal();
            thread_signal(_job_threads[i]);
        }
        thread_join(_job_threads[i]);
    }

    _scheduled_jobs.destroy();

    for (size_t i = 0; i < thread_count; ++i)
    {
        thread_deallocate(_job_threads[i]);
        _job_threads[i] = nullptr;
    }
}

job_t* job_allocate()
{
    void* job_mem = memory_allocate(0, sizeof(job_t), 8, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
    job_t* j = new (job_mem) job_t();
    return j;
}

void job_deallocate(job_t*& job)
{
    if (job == nullptr)
        return;
    if ((job->completed || !job->scheduled))
    {   
        job->~job_t();
        memory_deallocate(job);
        job = nullptr;
    }
    else
        job->flags |= JOB_DEALLOCATE_AFTER_EXECUTION;
}

job_t* job_execute(job_handler_t handler, void* payload /*= nullptr*/, job_flags_t flags /*= JOB_FLAGS_NONE*/)
{
    job_t* new_job = job_allocate();
    new_job->handler = handler;
    new_job->payload = payload;
    new_job->flags = flags;
    new_job->scheduled = true;
    _scheduled_jobs.push(new_job);
    signal_thread();
    return new_job;
}

bool job_completed(job_t* job)
{
    if (job == nullptr)
        return true;

    if (job->completed)
        return true;

    _scheduled_jobs.signal();
    return false;
}
