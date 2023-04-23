/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * Various memory utilities and helpers.
 */

#pragma once

#include <foundation/assert.h>
#include <foundation/memory.h>

/*! Used for #MEM_DELETE and #MEM_DELETE_ARRAY. */
template<typename T> using alias = T;

/*! @def MEM_NEW
 *  @brief Allocate memory for a new object and invoking the constructor.
 *  @param context The memory context to allocate from.
 *  @param type The type of the object to allocate.
 *  @param ... The arguments to pass to the constructor.
 */
#define MEM_NEW(context, type, ...) new (memory_allocate(context, sizeof(type), alignof(type), MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED)) type(__VA_ARGS__);

/*! @def MEM_NEW_ARRAY 
 *  @brief Allocate memory for an array of objects and invoking the constructor.
 *  @param context The memory context to allocate from.
 *  @param type The type of the object to allocate.
 *  @param count The number of objects to allocate.
 *  @param ... The arguments to pass to the constructor.
 */
 #define MEM_NEW_ARRAY(context, type, count, ...) new (memory_allocate(context, sizeof(type) * count, alignof(type), MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED)) type[count]{ __VA_ARGS__ };

/*! @def MEM_DELETE 
 *  @brief Deallocate memory for an object and invoking the destructor.
 *  @param ptr The pointer to the object to deallocate.
 */
#define MEM_DELETE(ptr) {                                           \
    if (ptr) {                                                      \
        ptr->~alias<std::remove_reference<decltype(*ptr)>::type>(); \
        memory_deallocate(ptr);                                     \
        ptr = nullptr;                                              \
    }                                                               \
}

/*! @def MEM_DELETE_ARRAY 
 *  @brief Deallocate memory for an array of objects and invoking the destructor.
 *  @param ptr The pointer to the array of objects to deallocate.
 */
 #define MEM_DELETE_ARRAY(ptr) {                                        \
    for (size_t i = 0; i < sizeof(ptr) / sizeof(ptr[0]); ++i)  {        \
        ptr[i].~alias<std::remove_reference<decltype(ptr[i])>::type>(); \
    }                                                                   \
    memory_deallocate(ptr);                                             \
    ptr = nullptr;                                                      \
}

/*! Structure used to scope the memory context tracker. */
struct MemoryScope
{
    FOUNDATION_FORCEINLINE MemoryScope(const hash_t& context)
    {
        memory_context_push(context);
    }

    FOUNDATION_FORCEINLINE ~MemoryScope()
    {
        memory_context_pop();
    }
};

#define MEMORY_TRACKER_NAME_COUNTER_EXPAND(HASH, COUNTER) MemoryScope __var_memory_tracker__##COUNTER (HASH)
#define MEMORY_TRACKER_NAME_COUNTER(HASH, COUNTER) MEMORY_TRACKER_NAME_COUNTER_EXPAND(HASH, COUNTER)

/*! @def MEMORY_TRACKER
 *  @brief Scope the memory context tracker.
 *  @param HASH The hash of the memory context to track.
 */
#define MEMORY_TRACKER(HASH) MEMORY_TRACKER_NAME_COUNTER(HASH, __LINE__)

/*! Template helper to allocate memory for a type.
 *
 *  @template T The type to allocate memory for.
 *
 *  @param context The memory context to allocate from.
 *  @param alignment The alignment of the memory to allocate.
 *  @param flags The flags to pass to the allocator.
 *
 *  @return A pointer to the allocated memory.
 */
template<typename T>
FOUNDATION_FORCEINLINE T* memory_allocate(hash_t context = 0, uint32_t alignment = alignof(T), uint32_t flags = MEMORY_PERSISTENT)
{
    return (T*)memory_allocate(context, sizeof(T), alignment, flags);
}

/*! Template helper to allocate temporary memory for a type.
 *
 *  @template T The type to allocate memory for.
 *
 *  @param context The memory context to allocate from.
 *  @param alignment The alignment of the memory to allocate.
 *  @param flags The flags to pass to the allocator.
 *
 *  @return A pointer to the allocated memory.
 */
template<typename T>
FOUNDATION_FORCEINLINE T* memory_temporary(hash_t context = 0, uint32_t alignment = alignof(T), uint32_t flags = 0)
{
    FOUNDATION_ASSERT_MSG((flags & MEMORY_PERSISTENT) == 0, "Cannot allocate persistent memory as temporary memory.");
    return (T*)memory_allocate(context, sizeof(T), alignment, MEMORY_TEMPORARY | flags);
}

