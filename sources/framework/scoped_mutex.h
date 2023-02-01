/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/assert.h>
#include <foundation/mutex.h>

struct scoped_mutex_t
{
    mutex_t* _mutex;
    bool _error = false;

    scoped_mutex_t(mutex_t* mutex)
        : _mutex(mutex)
        , _error(false)
    {
        if (mutex)
        {
            _error = !mutex_lock(mutex);
            FOUNDATION_ASSERT(!_error && "Failed to lock mutex");
        }
    }

    ~scoped_mutex_t()
    {
        if (!_error && _mutex)
            _error = !mutex_unlock(_mutex);
    }

    scoped_mutex_t(scoped_mutex_t&& src) noexcept
        : _mutex(src._mutex)
        , _error(src._error)
    {
        src._mutex = nullptr;
        src._error = true;
    }

    scoped_mutex_t& operator=(scoped_mutex_t&& src) noexcept
    {
        _mutex = src._mutex;
        _error = src._error;

        src._mutex = nullptr;
        src._error = true;

        return *this;
    }

    operator bool() const
    {
        return _mutex && !_error;
    }
};
