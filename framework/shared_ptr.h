/*
 * Copyright 2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 *
 * Basic shared_ptr implementation
 */

#pragma once

#include <foundation/memory.h>

struct shared_ptr_data_t
{
    atomicptr_t ptr;
    atomic32_t count;
    atomic64_t context;
};

template <typename T>
struct shared_ptr
{
    shared_ptr_data_t* _ref{ nullptr };

    FOUNDATION_FORCEINLINE shared_ptr()
    {
    }

    FOUNDATION_FORCEINLINE shared_ptr(T* ptr)
    {
        set(0, ptr);
    }

    FOUNDATION_FORCEINLINE shared_ptr(hash_t context, T* ptr)
    {
        set(context, ptr);
    }

    FOUNDATION_FORCEINLINE shared_ptr(shared_ptr const& other)
    {
        set(other._ref);
    }

    FOUNDATION_FORCEINLINE shared_ptr(shared_ptr&& other)
    {
        _ref = other._ref;
        other._ref = nullptr;
    }

    FOUNDATION_FORCEINLINE ~shared_ptr()
    {
        release();
    }

    FOUNDATION_FORCEINLINE shared_ptr& operator=(shared_ptr const& other)
    {
        set(other._ref);
        return *this;
    }

    FOUNDATION_FORCEINLINE shared_ptr& operator=(shared_ptr&& other)
    {
        _ref = other._ref;
        other._ref = nullptr;
        return *this;
    }

    FOUNDATION_FORCEINLINE static shared_ptr create(hash_t context = 0)
    {
        return shared_ptr(context, (T*)memory_allocate(context, sizeof(T), alignof(T), MEMORY_PERSISTENT));
    }

    FOUNDATION_FORCEINLINE void set(hash_t context, T* ptr)
    {
        release();
        if (ptr)
        {
            _ref = (shared_ptr_data_t*)memory_allocate(context, sizeof(shared_ptr_data_t), 0, MEMORY_PERSISTENT);
            atomic_store32(&_ref->count, 1, memory_order_release);
            atomic_store_ptr(&_ref->ptr, ptr, memory_order_release);
            atomic_store64(&_ref->context, context, memory_order_release);
        }
    }

    FOUNDATION_FORCEINLINE void set(shared_ptr_data_t* ref)
    {
        if (ref != _ref)
            release();
        if (ref)
        {
            if (atomic_incr32(&ref->count, memory_order_acq_rel) > 0)
                _ref = ref;
        }
    }

    FOUNDATION_FORCEINLINE void release()
    {
        if (!_ref)
            return;

        if (atomic_decr32(&_ref->count, memory_order_acq_rel) == 0)
        {
            T* ptr = (T*)atomic_load_ptr(&_ref->ptr, memory_order_acquire);
            if (ptr)
                memory_deallocate(ptr);
            memory_deallocate(_ref);
        }
        _ref = nullptr;
    }

    FOUNDATION_FORCEINLINE T* get() const
    {
        return _ref ? (T*)atomic_load_ptr(&_ref->ptr, memory_order_acquire) : nullptr;
    }

    FOUNDATION_FORCEINLINE T* operator->() const
    {
        return get();
    }

    FOUNDATION_FORCEINLINE operator bool() const
    {
        return _ref != nullptr;
    }

    FOUNDATION_FORCEINLINE bool operator==(shared_ptr const& other) const
    {
        return _ref == other._ref;
    }

    FOUNDATION_FORCEINLINE bool operator!=(shared_ptr const& other) const
    {
        return _ref != other._ref;
    }

    FOUNDATION_FORCEINLINE shared_ptr& operator=(T* ptr)
    {
        set(ptr);
        return *this;
    }
};
