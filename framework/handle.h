/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/assert.h>
#include <foundation/atomic.h>

#define HANDLE_INVALID (SIZE_MAX)
#define HANDLE_RESOLVING (SIZE_MAX - 1ULL)

typedef enum {
    STATUS_OK = 0,

    STATUS_UNDEFINED = INT_MAX,
    STATUS_INITIALIZED = (1),
    STATUS_RESOLVING = (2),
    STATUS_AVAILABLE = (3),

    STATUS_ERROR = (-1),
    STATUS_UNRESOLVED = (-2),
    STATUS_ERROR_NULL_REFERENCE = (-11),
    STATUS_ERROR_INVALID_HANDLE = (-12),
    STATUS_ERROR_DB_ACCESS = (-13),
    STATUS_ERROR_MUTEX_UNLOCK = (-14),
    STATUS_ERROR_HASH_TABLE_NOT_LARGE_ENOUGH = (-15),
    STATUS_ERROR_FAILED_CREATE_JOB = (-16),
    STATUS_ERROR_INVALID_REQUEST = (-17),
    STATUS_ERROR_INVALID_STREAM = (-18),
    STATUS_ERROR_LOAD_FAILURE = (-19),
    STATUS_ERROR_NOT_AVAILABLE = (-20),
} status_t;

struct HandleKey
{
    size_t index{ HANDLE_INVALID };
    hash_t hash{ 0 };
    //void* user_data{ nullptr };
};

#define HANDLE_NIL (HandleKey{HANDLE_INVALID, 0})

template<typename T, T*(*GETTER)(HandleKey key), HandleKey(*GET_HANDLE)(T* ptr) = nullptr>
struct Handle
{
    HandleKey key{ HANDLE_NIL };

    FOUNDATION_FORCEINLINE Handle(HandleKey key)
        : key(key)
    {
    }

    FOUNDATION_FORCEINLINE Handle(const T* ptr)
        : key(HANDLE_NIL)
    {
        if (ptr)
        {
            FOUNDATION_ASSERT(GET_HANDLE);
            key = GET_HANDLE((T*)ptr);
        }
    }

    FOUNDATION_FORCEINLINE ~Handle()
    {
        key = {};
    }

    FOUNDATION_FORCEINLINE Handle(const Handle& o)
        : key(o.key)
    {
    }

    FOUNDATION_FORCEINLINE Handle(Handle&& o)
        : key(o.key)
    {
        o.key = HANDLE_NIL;
    }

    FOUNDATION_FORCEINLINE Handle& operator=(Handle&& o)
    {
        this->key = o.key;
        o.key = HANDLE_NIL;
        return *this;
    }

    FOUNDATION_FORCEINLINE Handle& operator=(const Handle& o)
    {
        this->key = o.key;
        return *this;
    }

    FOUNDATION_FORCEINLINE T* resolve() const
    {
        if (key.index == SIZE_MAX)
            return nullptr;
        return (T*)GETTER(key);
    }

    FOUNDATION_FORCEINLINE operator T*()
    {
        return resolve();
    }

    FOUNDATION_FORCEINLINE operator bool() const
    {
        if (key.index == SIZE_MAX)
            return false;
        return true;
    }

    FOUNDATION_FORCEINLINE operator const T*() const
    {
        return resolve();
    }

    FOUNDATION_FORCEINLINE T* operator->()
    {
        FOUNDATION_ASSERT_MSG(key.index != SIZE_MAX, "Invalid handle");
        return GETTER(key);
    }

    FOUNDATION_FORCEINLINE const T* operator->() const
    {
        FOUNDATION_ASSERT_MSG(key.index != SIZE_MAX, "Invalid handle");
        return GETTER(key);
    }
};

template<typename T = int32_t>
struct atom32
{
    FOUNDATION_FORCEINLINE atom32(T val)
    {
        store(val);
    }

    template<typename U = T>
    FOUNDATION_FORCEINLINE atom32(atom32<U>&& o)
        : atom(o.atom)
    {
        o.atom = 0;
    }

    FOUNDATION_FORCEINLINE atom32<T>& operator=(atom32<T>&& o)
    {
        this->atom = o.atom;
        o.atom = 0;
        return *this;
    }

    FOUNDATION_FORCEINLINE T load(memory_order mo = memory_order_acquire) const // memory_order_consume, memory_order_relaxed
    {
        return (T)atomic_load32(&atom, mo); 
    }

    FOUNDATION_FORCEINLINE void store(T val, memory_order mo = memory_order_release) // memory_order_relaxed
    {
        atomic_store32(&atom, (int32_t)val, mo); 
    }

    FOUNDATION_FORCEINLINE operator T() const
    {
        return load();
    }

    FOUNDATION_FORCEINLINE atom32<T>& operator=(T val)
    {
        store(val);
        return *this;
    }

    FOUNDATION_FORCEINLINE atom32<T>& operator|=(T orval)
    {
        T ref = load();
        while (!atomic_cas32(&atom, (int32_t)(ref | orval), ref, memory_order_release, memory_order_acquire))
            ref = load();
        return *this;
    }

    FOUNDATION_FORCEINLINE atom32<T>& operator&=(T andval)
    {
        T ref = load();
        while (!atomic_cas32(&atom, (int32_t)(ref & andval), ref, memory_order_release, memory_order_acquire))
            ref = load();
        return *this;
    }

    FOUNDATION_FORCEINLINE atom32<T>& operator--()
    {
        atomic_decr32(&atom, memory_order_relaxed);
        return *this;
    }

    FOUNDATION_FORCEINLINE atom32<T>& operator++()
    {
        atomic_incr32(&atom, memory_order_relaxed);
        return *this;
    }

    FOUNDATION_FORCEINLINE atom32<T>& operator--(int)
    {
        atomic_add32(&atom, -1, memory_order_relaxed);
        return *this;
    }

    FOUNDATION_FORCEINLINE atom32<T>& operator++(int)
    {
        atomic_add32(&atom, 1, memory_order_relaxed);
        return *this;
    }

    template<typename U = T>
    FOUNDATION_FORCEINLINE atom32<T>& operator=(const atom32<U>& o)
    {
        const int32_t val = o.load();
        store(val);
        return *this;
    }

    atomic32_t atom;
};

template<typename T = int64_t>
struct atom64
{
    FOUNDATION_FORCEINLINE atom64(T val)
    {
        store(val);
    }

    template<typename U = T>
    FOUNDATION_FORCEINLINE atom64(atom64<U>&& o)
        : atom(o.atom)
    {
        o.atom = 0;
    }

    FOUNDATION_FORCEINLINE atom64<T>& operator=(atom64<T>&& o)
    {
        this->atom = o.atom;
        o.atom = 0;
        return *this;
    }

    FOUNDATION_FORCEINLINE T load(memory_order mo = memory_order_acquire) const // memory_order_consume, memory_order_relaxed
    {
        return (T)atomic_load64(&atom, mo);
    }

    FOUNDATION_FORCEINLINE void store(T val, memory_order mo = memory_order_release) // memory_order_relaxed
    {
        atomic_store64(&atom, (int64_t)val, mo);
    }

    FOUNDATION_FORCEINLINE operator T() const
    {
        return load();
    }

    FOUNDATION_FORCEINLINE atom64<T>& operator=(T val)
    {
        store(val);
        return *this;
    }

    FOUNDATION_FORCEINLINE atom64<T>& operator|=(T orval)
    {
        T ref = load();
        while (!atomic_cas64(&atom, (int64_t)(ref | orval), ref, memory_order_release, memory_order_acquire))
            ref = load();
        return *this;
    }

    FOUNDATION_FORCEINLINE atom64<T>& operator&=(T andval)
    {
        T ref = load();
        while (!atomic_cas64(&atom, (int64_t)(ref & andval), ref, memory_order_release, memory_order_acquire))
            ref = load();
        return *this;
    }

    template<typename U = T>
    FOUNDATION_FORCEINLINE atom64<T>& operator=(const atom64<U>& o)
    {
        const int64_t val = o.load();
        store(val);
        return *this;
    }

    atomic64_t atom;
};

template<typename T>
struct atomptr
{
    FOUNDATION_FORCEINLINE atomptr(T* val)
    {
        atomic_store_ptr(&atom, val, memory_order_release);
    }

    FOUNDATION_FORCEINLINE atomptr(atomptr&& o)
        : atom(o.atom)
    {
        o.atom = 0;
    }

    FOUNDATION_FORCEINLINE atomptr& operator=(atomptr&& o)
    {
        this->atom = o.atom;
        o.atom = 0;
        return *this;
    }

    FOUNDATION_FORCEINLINE operator const T*() const
    {
        return atomic_load_ptr(&atom, memory_order_acquire);
    }

    FOUNDATION_FORCEINLINE operator T*()
    {
        return atomic_load_ptr(&atom, memory_order_acquire);
    }

    FOUNDATION_FORCEINLINE atomptr& operator=(T* val)
    {
        T* ref = this->operator T*();
        while (!atomic_cas_ptr(&atom, val, ref, memory_order_release, memory_order_acquire))
            ref = this->operator T*();
        return *this;
    }

    atomicptr_t atom;
};

typedef atom32<int32_t> atom32_t;
typedef atom32<int64_t> atom64_t;

