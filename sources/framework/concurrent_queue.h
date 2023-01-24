/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#pragma once

#include <foundation/array.h>
#include <foundation/mutex.h>
#include <foundation/beacon.h>

#include "shared_mutex.h"

template<typename T>
class concurrent_queue
{
public: 
    void create()
    {
        FOUNDATION_ASSERT(wait_event == nullptr);
        wait_event = beacon_allocate();
    }

    void destroy()
    {
        FOUNDATION_ASSERT(wait_event);

        array_deallocate(elements);
        beacon_deallocate(wait_event);
    }

    size_t size() const
    {
        size_t count = 0;
        if (lock.shared_lock())
        {
            count = array_size(elements);
            lock.shared_unlock();
        }
        return count;
    }

    bool empty() const
    {
        return size() == 0;
    }

    bool push(const T& e)
    {
        FOUNDATION_ASSERT(wait_event);

        if (!lock.exclusive_lock())
            return false;
        array_push_memcpy(elements, &e);
        if (lock.exclusive_unlock())
            beacon_fire(wait_event);
        return true;
    }

    bool try_pop(T& e, unsigned int milliseconds = 0)
    {
        FOUNDATION_ASSERT(wait_event);

        beacon_try_wait(wait_event, milliseconds);
        if (!lock.exclusive_lock())
            return false;

        bool poped = false;
        if (array_size(elements) > 0)
        {
            e = std::move(*array_last(elements));
            array_pop_safe(elements);
            poped = true;
        }

        if (!lock.exclusive_unlock())
            return false;
        return poped;
    }

    void signal()
    {
        FOUNDATION_ASSERT(wait_event);
        beacon_fire(wait_event);
    }

private:

    T* elements{ nullptr };
    beacon_t* wait_event{ nullptr };
    shared_mutex lock;
};
