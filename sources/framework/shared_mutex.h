/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/platform.h>
#include <foundation/time.h>

#if FOUNDATION_PLATFORM_WINDOWS
#include <foundation/windows.h>
#undef THREAD_PRIORITY_NORMAL
#else
#include <foundation/posix.h>
#endif

#include <foundation/thread.h>

class shared_mutex 
{
public:
    FOUNDATION_FORCEINLINE shared_mutex()
    {
        #if FOUNDATION_PLATFORM_WINDOWS
        InitializeSRWLock(&mutex_);
        #else
        pthread_rwlock_init(&mutex_, NULL);
        #endif
    }

    // Move constructor
    FOUNDATION_FORCEINLINE shared_mutex(shared_mutex&& other)
    {
        #ifdef _WIN32
        mutex_ = other.mutex_;
        #else
        pthread_rwlock_init(&mutex_, NULL);
        pthread_rwlock_wrlock(&other.mutex_);
        pthread_rwlock_unlock(&other.mutex_);
        pthread_rwlock_destroy(&other.mutex_);
        other.mutex_ = mutex_;
        mutex_ = pthread_rwlock_t();
        #endif
    }

    FOUNDATION_FORCEINLINE ~shared_mutex()
    {
        #if FOUNDATION_PLATFORM_WINDOWS
        (void)mutex_;  // The SRWLOCK type does not need to be destroyed
        #else
        pthread_rwlock_destroy(&mutex_);
        #endif
    }

    FOUNDATION_FORCEINLINE bool shared_lock() const
    {
        #if FOUNDATION_PLATFORM_WINDOWS
        SetLastError(ERROR_SUCCESS);
        AcquireSRWLockShared(&mutex_);
        return GetLastError() == ERROR_SUCCESS;
        #else
        return pthread_rwlock_rdlock(&mutex_) == 0;
        #endif
    }

    FOUNDATION_FORCEINLINE bool shared_unlock() const
    {
        #if FOUNDATION_PLATFORM_WINDOWS
        SetLastError(ERROR_SUCCESS);
        ReleaseSRWLockShared(&mutex_);
        return GetLastError() == ERROR_SUCCESS;
        #else
        return pthread_rwlock_unlock(&mutex_) == 0;
        #endif
    }

    FOUNDATION_FORCEINLINE bool exclusive_lock()
    {
        #if FOUNDATION_PLATFORM_WINDOWS
        SetLastError(ERROR_SUCCESS);
        AcquireSRWLockExclusive(&mutex_);
        return GetLastError() == ERROR_SUCCESS;
        #else
        return pthread_rwlock_wrlock(&mutex_) == 0;
        #endif
    }

    FOUNDATION_FORCEINLINE bool exclusive_unlock()
    {
        #if FOUNDATION_PLATFORM_WINDOWS
        SetLastError(ERROR_SUCCESS);
        ReleaseSRWLockExclusive(&mutex_);
        return GetLastError() == ERROR_SUCCESS;
        #else
        return pthread_rwlock_unlock(&mutex_) == 0;
        #endif
    }

private:
    #if FOUNDATION_PLATFORM_WINDOWS
    mutable SRWLOCK mutex_;
    #else
    mutable pthread_rwlock_t mutex_;
    #endif
};

struct shared_mutex_read_lock
{
    const bool locked;
    shared_mutex& _mutex;

    FOUNDATION_FORCEINLINE shared_mutex_read_lock(shared_mutex& mutex)
        : _mutex(mutex)
        , locked(mutex.shared_lock())
    {
    }

    FOUNDATION_FORCEINLINE ~shared_mutex_read_lock()
    {
        if (locked)
            _mutex.shared_unlock();
    }

    FOUNDATION_FORCEINLINE operator bool() const
    {
        return locked;
    }
};

struct shared_mutex_write_lock
{
    const bool locked;
    shared_mutex& _mutex;

    FOUNDATION_FORCEINLINE shared_mutex_write_lock(shared_mutex& mutex)
        : _mutex(mutex)
        , locked(mutex.exclusive_lock())
    {
    }

    FOUNDATION_FORCEINLINE ~shared_mutex_write_lock()
    {
        if (locked)
            _mutex.exclusive_unlock();
    }

    FOUNDATION_FORCEINLINE operator bool() const
    {
        return locked;
    }
};

#define SHARED_READ_LOCK_COUNTER_EXPAND(COUNTER, mutex) shared_mutex_read_lock __var_shared_read_lock__##COUNTER(mutex)
#define SHARED_READ_LOCK_COUNTER(COUNTER, mutex) SHARED_READ_LOCK_COUNTER_EXPAND(COUNTER, mutex)
#define SHARED_READ_LOCK(mutex) SHARED_READ_LOCK_COUNTER(__LINE__, mutex)

#define SHARED_WRITE_LOCK_COUNTER_EXPAND(COUNTER, mutex) shared_mutex_write_lock __var_shared_write_lock__##COUNTER(mutex)
#define SHARED_WRITE_LOCK_COUNTER(COUNTER, mutex) SHARED_WRITE_LOCK_COUNTER_EXPAND(COUNTER, mutex)
#define SHARED_WRITE_LOCK(mutex) SHARED_WRITE_LOCK_COUNTER(__LINE__, mutex)

class event_handle
{
public:
    /// <summary>
    /// Initializes the specified signal instance, preparing it for use. A signal works like a flag, which can be waited on by
    /// one thread, until it is raised from another thread.
    /// </summary>
    event_handle()
    {
#if FOUNDATION_PLATFORM_WINDOWS

#if _WIN32_WINNT >= 0x0600
        InitializeCriticalSectionAndSpinCount(&mutex, 32);
        InitializeConditionVariable(&condition);
        value = 0;
#else 
        event = CreateEvent(NULL, FALSE, FALSE, NULL);
#endif 

#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_MACOS || FOUNDATION_PLATFORM_ANDROID

        pthread_mutex_init(&mutex, NULL);
        pthread_cond_init(&condition, NULL);
        value = 0;

#else 
#error Unknown platform.
#endif
    }

    ~event_handle()
    {
#if FOUNDATION_PLATFORM_WINDOWS

#if _WIN32_WINNT >= 0x0600
        DeleteCriticalSection(&mutex);
#else 
        CloseHandle(event);
#endif 

#elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_MACOS || FOUNDATION_PLATFORM_ANDROID

        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&condition);

#else 
#error Unknown platform.
#endif
    }

    void signal()
    {
        #if FOUNDATION_PLATFORM_WINDOWS

        #if _WIN32_WINNT >= 0x0600
                EnterCriticalSection(&mutex);
                value = 1;
                LeaveCriticalSection(&mutex);
                WakeConditionVariable(&condition);
        #else 
                SetEvent(event);
        #endif 

        #elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_MACOS || FOUNDATION_PLATFORM_ANDROID

                pthread_mutex_lock(&mutex);
                value = 1;
                pthread_mutex_unlock(&mutex);
                pthread_cond_signal(&condition);

        #else 
        #error Unknown platform.
        #endif

        thread_yield();
    }

    int wait(int timeout_ms = 0)
    {
        thread_yield();
        
        #if FOUNDATION_PLATFORM_WINDOWS

        #if _WIN32_WINNT >= 0x0600
                int timed_out = 0;
                EnterCriticalSection(&mutex);
                while (value == 0)
                {
                    BOOL res = SleepConditionVariableCS(&condition, &mutex, timeout_ms < 0 ? INFINITE : timeout_ms);
                    if (!res && GetLastError() == ERROR_TIMEOUT) { timed_out = 1; break; }
                }
                value = 0;
                LeaveCriticalSection(&mutex);
                return timed_out != 0;
        #else 
                int failed = WAIT_OBJECT_0 != WaitForSingleObject(event, timeout_ms < 0 ? INFINITE : timeout_ms);
                return !failed;
        #endif 

        #elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_MACOS || FOUNDATION_PLATFORM_ANDROID

                struct timespec ts;
                if (timeout_ms >= 0)
                {
                    struct timeval tv;
                    gettimeofday(&tv, NULL);
                    ts.tv_sec = time(NULL) + timeout_ms / 1000;
                    ts.tv_nsec = tv.tv_usec * 1000 + 1000 * 1000 * (timeout_ms % 1000);
                    ts.tv_sec += ts.tv_nsec / (1000 * 1000 * 1000);
                    ts.tv_nsec %= (1000 * 1000 * 1000);
                }

                int timed_out = 0;
                pthread_mutex_lock(&mutex);
                while (value == 0)
                {
                    if (timeout_ms < 0)
                        pthread_cond_wait(&condition, &mutex);
                    else if (pthread_cond_timedwait(&condition, &mutex, &ts) == ETIMEDOUT)
                    {
                        timed_out = 1;
                        break;
                    }

                }
                if (!timed_out) value = 0;
                pthread_mutex_unlock(&mutex);
                return timed_out != 0;

        #else 
        #error Unknown platform.
        #endif
    }

private:
    #if FOUNDATION_PLATFORM_WINDOWS

    #if _WIN32_WINNT >= 0x0600
        CRITICAL_SECTION mutex;
        CONDITION_VARIABLE condition;
        int value;
    #else 
        #pragma message( "Warning: _WIN32_WINNT < 0x0600 - condition variables not available" )
        HANDLE event;
    #endif 

    #elif FOUNDATION_PLATFORM_LINUX || FOUNDATION_PLATFORM_MACOS || FOUNDATION_PLATFORM_ANDROID

        pthread_mutex_t mutex;
        pthread_cond_t condition;
        int value;

    #else 
    #error Unknown platform.
    #endif
};
