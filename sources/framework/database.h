/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#pragma once

#include <framework/function.h>
#include <framework/shared_mutex.h>

#include <foundation/hash.h>
#include <foundation/array.h>
#include <foundation/hashtable.h>

template<typename T>
FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL hash_t hash(const T& value)
{
    return hash(&value, FOUNDATION_ALIGNOF(T));
}

template<typename T, hash_t(*HASHER)(const T& v) = [](const T& v) { return hash(v); } >
struct database
{
    size_t capacity;
    shared_mutex mutex;
    hashtable64_t* hashes;
    T* elements;

    database()
        : capacity(16)
        , hashes(nullptr)
        , elements(nullptr)
    {
        hashes = hashtable64_allocate(capacity);
    }

    ~database()
    {
        array_deallocate(elements);
        hashtable64_deallocate(hashes);
    }

    void grow()
    {
        hashtable64_t* old_table = hashes;
        capacity *= size_t(2);
        hashtable64_t* new_hash_table = hashtable64_allocate(capacity);
        for (int i = 0, end = array_size(elements); i < end; ++i)
            hashtable64_set(new_hash_table, HASHER(elements[i]), i + 1); // 1 based

        hashes = new_hash_table;
        hashtable64_deallocate(old_table);
    }

    hash_t insert(const T& value)
    {
        const hash_t key = HASHER(value);

        if (!mutex.shared_lock())
            return 0;
            
        const uint64_t index = hashtable64_get(hashes, key);
        if (!mutex.shared_unlock() || index != 0)
            return 0;
        
        if (!mutex.exclusive_lock())
            return 0;
            
        unsigned int element_index = array_size(elements) + 1; // 1 based

        while (!hashtable64_set(hashes, key, element_index))
            grow();

        array_push(elements, value);
        mutex.exclusive_unlock();
        
        return key;
    }
    
    hash_t update(const T& value)
    {
        const hash_t key = HASHER(value);

        if (!mutex.shared_lock())
            return 0;

        const uint64_t index = hashtable64_get(hashes, key);
        if (index == 0)
        {
            mutex.shared_unlock();
            return 0;
        }

        elements[index - 1] = value;
        mutex.shared_unlock();
        return key;
    }

    hash_t put(const T& value)
    {
        const hash_t key = HASHER(value);
        const uint64_t index = hashtable64_get(hashes, key);
        if (index != 0)
            return update(value);
            
        return insert(value);
    }

    struct AutoLock
    {
        shared_mutex* m{ nullptr };
        T* value{ nullptr };

        AutoLock()
            : m(nullptr)
            , value(nullptr)
        {
        }

        AutoLock(const AutoLock&);
        AutoLock& operator=(const AutoLock&);

        AutoLock(AutoLock&& o)
            : m(o.m)
            , value(o.value)
        {
            o.m = nullptr;
            o.value = nullptr;
        }
        
        AutoLock& operator=(AutoLock&& o)
        {
            m = o.m;
            value = o.value;
            o.m = nullptr;
            o.value = nullptr;
        }

        AutoLock(shared_mutex* mutex, T* value_ptr = nullptr)
            : m(mutex)
            , value(value_ptr)
        {
            if (m)
                m->exclusive_lock();
        }

        ~AutoLock()
        {
            if (m)
                m->exclusive_unlock();
        }

        operator bool() const
        {
            return m != nullptr && value != nullptr;
        }

        T* operator->()
        {
            return value;
        }

        const T* operator->() const
        {
            return *value;
        }

        operator const T* () const
        {
            return value;
        }

        operator T* () const
        {
            return value;
        }
    };

    constexpr AutoLock lock(hash_t key)
    {            
        const uint64_t index = hashtable64_get(hashes, key);
        if (index == 0)
            return {};

        if (!mutex.shared_lock())
            return {};

        if (index > array_size(elements))
        {
            mutex.shared_unlock();
            return {};
        }
        mutex.shared_unlock();

        AutoLock l(&mutex, &elements[index - 1]);
        if (HASHER(*l.value) != key)
            return {};
            
        return l;
    }

    void clear()
    {
        if (!mutex.exclusive_lock())
            return;

        array_clear(elements);
        hashtable64_clear(hashes);
        mutex.exclusive_unlock();
    }

    bool contains(hash_t key) const
    {
        return hashtable64_get(hashes, key) != 0;
    }

    bool contains(const T& value) const
    {
        const hash_t key = HASHER(value);
        return contains(key);
    }

    bool select(hash_t key, T& value) const
    {
        const uint64_t index = hashtable64_get(hashes, key);
        if (index == 0)
            return false;
        
        if (!mutex.shared_lock())
            return false;

        if (index > array_size(elements))
        {
            mutex.shared_unlock();
            return false;
        }

        value = elements[index - 1];
        if (HASHER(value) != key)
        {
            mutex.shared_unlock();
            return false;
        }
            
        return mutex.shared_unlock();
    }

    bool select(hash_t key, const function<void(T& value)>& selector, bool quick_and_unsafe = false) const
    {
        FOUNDATION_ASSERT(selector);

        const uint64_t index = hashtable64_get(hashes, key);
        if (index == 0)
            return false;

        if (!mutex.shared_lock())
            return false;

        if (index > array_size(elements))
        {
            mutex.shared_unlock();
            return false;
        }
        
        T& value = elements[index - 1];
        if (!quick_and_unsafe && HASHER(value) != key)
        {
            mutex.shared_unlock();
            return false;
        }

        selector(value);
        return mutex.shared_unlock();
    }

    bool remove(hash_t key, T* out_value = nullptr)
    {
        const uint64_t index = hashtable64_get(hashes, key);
        if (index == 0)
            return false;

        if (!mutex.exclusive_lock())
            return false;

        if (index > array_size(elements))
        {
            mutex.exclusive_unlock();
            return false;
        }

        const T& value = elements[index - 1];
        if (HASHER(value) != key)
        {
            mutex.exclusive_unlock();
            return false;
        }

        if (out_value)
            *out_value = value;

        hashtable64_erase(hashes, key);
        array_erase_memcpy_safe(elements, index - 1);

        return mutex.exclusive_unlock();
    }

    size_t size() const
    {
        return hashtable64_size(hashes);
    }

    bool empty() const
    {
        return size() == 0;
    }

    constexpr T operator[](hash_t key) const
    {
        T v;
        select(key, v);
        v;
    }
    
    constexpr AutoLock operator[](hash_t key)
    {
        return lock(key);
    }
};
