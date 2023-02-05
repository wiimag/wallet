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
    T* elements;
    size_t capacity;
    hashtable64_t* hashes;
    mutable shared_mutex mutex;

    static constexpr const hash_t INVALID_KEY{ 0 };

    database()
        : elements(nullptr)
        , capacity(16)
        , hashes(nullptr)
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
        {
            FOUNDATION_ASSERT_FAIL("Failed to get shared lock");
            return INVALID_KEY;
        }
            
        const uint64_t index = hashtable64_get(hashes, key);
        if (!mutex.shared_unlock() || index != 0)
            return INVALID_KEY;
        
        if (!mutex.exclusive_lock())
        {
            FOUNDATION_ASSERT_FAIL("Failed to get exclusive lock");
            return INVALID_KEY;
        }
            
        unsigned int element_index = array_size(elements) + 1; // 1 based

        while (!hashtable64_set(hashes, key, element_index))
            grow();

        array_push(elements, value);
        if (!mutex.exclusive_unlock())
        {
            FOUNDATION_ASSERT_FAIL("Failed to release exclusive lock");
        }
        
        return key;
    }
    
    constexpr hash_t update(const T& value)
    {
        const hash_t key = HASHER(value);

        if (!mutex.shared_lock())
        {
            FOUNDATION_ASSERT_FAIL("Failed to get shared lock");
            return INVALID_KEY;
        }

        const uint64_t index = hashtable64_get(hashes, key);
        if (index == 0)
        {
            if (!mutex.shared_unlock())
            {
                FOUNDATION_ASSERT_FAIL("Failed to release shared lock");
            }
            return INVALID_KEY;
        }

        elements[index - 1] = value;
        if (!mutex.shared_unlock())
        {
            FOUNDATION_ASSERT_FAIL("Failed to release shared lock");
            return INVALID_KEY;
        }

        return key;
    }

    constexpr hash_t put(const T& value)
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

        AutoLock(const AutoLock&);
        AutoLock& operator=(const AutoLock&);
        AutoLock& operator=(AutoLock&& o);

        FOUNDATION_FORCEINLINE AutoLock()
            : m(nullptr)
            , value(nullptr)
        {
        }

        FOUNDATION_FORCEINLINE AutoLock(AutoLock&& o)
            : m(o.m)
            , value(o.value)
        {
            o.m = nullptr;
            o.value = nullptr;
        }

        FOUNDATION_FORCEINLINE AutoLock(shared_mutex* mutex, T* value_ptr = nullptr)
            : m(mutex)
            , value(value_ptr)
        {
            if (m && !m->exclusive_lock())
            {
                FOUNDATION_ASSERT_FAIL("Failed to get exclusive lock");
                m = nullptr;
                value = nullptr;
            }
        }

        FOUNDATION_FORCEINLINE ~AutoLock()
        {
            if (m && !m->exclusive_unlock())
            {
                FOUNDATION_ASSERT_FAIL("Failed to release exclusive lock");
            }
        }

        FOUNDATION_FORCEINLINE operator bool() const
        {
            return m != nullptr && value != nullptr;
        }

        FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL T* operator->()
        {
            return value;
        }

        FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL const T* operator->() const
        {
            return value;
        }

        FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL operator const T* () const
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
        {
            FOUNDATION_ASSERT_FAIL("Failed to get shared lock");
            return {};
        }

        if (index > array_size(elements))
        {
            FOUNDATION_ASSERT_FAIL("Index is out of bound");
            mutex.shared_unlock();
            return {};
        }

        if (!mutex.shared_unlock())
        {
            FOUNDATION_ASSERT_FAIL("Failed to release shared lock");
            return {};
        }

        AutoLock locked_value(&mutex, &elements[index - 1]);
        if (HASHER(*locked_value.value) != key)
        { 
            FOUNDATION_ASSERT_FAIL("Element has been invalidated");
            return {};
        }
            
        return locked_value;
    }

    void clear()
    {
        if (!mutex.exclusive_lock())
        {
            FOUNDATION_ASSERT_FAIL("Failed to get exclusive lock");
            return;
        }

        array_clear(elements);
        hashtable64_clear(hashes);
        if (!mutex.exclusive_unlock())
        {
            FOUNDATION_ASSERT_FAIL("Failed to release exclusive lock");
        }
    }

    constexpr bool contains(hash_t key) const
    {
        return hashtable64_get(hashes, key) != 0;
    }

    bool contains(const T& value) const
    {
        const hash_t key = HASHER(value);
        return contains(key);
    }

    constexpr const T& get(hash_t key) const
    {
        const uint64_t index = hashtable64_get(hashes, key);
        if (index == 0)
        {
            static thread_local T NULL_VALUE{};
            return NULL_VALUE;
        }

        SHARED_READ_LOCK(mutex);
        FOUNDATION_ASSERT_MSG(index <= array_size(elements), "Index is out of bound");
        return elements[index - 1];
    }

    bool select(hash_t key, T& value) const
    {
        const uint64_t index = hashtable64_get(hashes, key);
        if (index == 0)
            return false;
        
        if (!mutex.shared_lock())
        {
            FOUNDATION_ASSERT_FAIL("Failed to get shared lock");
            return false;
        }

        if (index > array_size(elements))
        {
            FOUNDATION_ASSERT_FAIL("Index is out of bound");
            mutex.shared_unlock();
            return false;
        }

        value = elements[index - 1];
        if (HASHER(value) != key)
        {
            FOUNDATION_ASSERT_FAIL("Element has been invalidated");
            mutex.shared_unlock();
            return false;
        }
            
        return mutex.shared_unlock();
    }

    bool select(hash_t key, const function<void(T& value)>& selector, bool quick_and_unsafe = false) const
    {
        const uint64_t index = hashtable64_get(hashes, key);
        if (index == 0)
            return false;

        if (!mutex.shared_lock())
        {
            FOUNDATION_ASSERT_FAIL("Failed to get shared lock");
            return false;
        }

        if (index > array_size(elements))
        {
            FOUNDATION_ASSERT_FAIL("Index is out of bound");
            mutex.shared_unlock();
            return false;
        }
        
        T& value = elements[index - 1];
        if (!quick_and_unsafe && HASHER(value) != key)
        {
            FOUNDATION_ASSERT_FAIL("Element has been invalidated");
            mutex.shared_unlock();
            return false;
        }

        FOUNDATION_ASSERT(selector);
        selector(value);
        return mutex.shared_unlock();
    }

    bool remove(hash_t key, T* out_value = nullptr)
    {
        const uint64_t index = hashtable64_get(hashes, key);
        if (index == 0)
            return false;

        if (!mutex.exclusive_lock())
        {
            FOUNDATION_ASSERT_FAIL("Failed to get exclusive lock");
            return false;
        }

        if (index > array_size(elements))
        {
            FOUNDATION_ASSERT_FAIL("Index is out of bound");
            mutex.exclusive_unlock();
            return false;
        }

        const T& value = elements[index - 1];
        if (HASHER(value) != key)
        {
            FOUNDATION_ASSERT_FAIL("Element has been invalidated");
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
    
    constexpr AutoLock operator[](hash_t key)
    {
        return lock(key);
    }
};
