/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "string_table.h"

#include <framework/common.h>
#include <framework/profiler.h>
#include <framework/shared_mutex.h>
#include <framework/scoped_string.h>
#include <framework/string.h>
#include <framework/array.h>

#include <foundation/hash.h>
#include <foundation/memory.h>
#include <foundation/assert.h>

#define HASH_STRING_TABLE static_hash_string("string_table", 12, 0xf026bfe3a9500e3cLL)

#define HASH_FACTOR (2.0f)

struct string_table_free_slot_t
{
    string_table_symbol_t symbol;
    size_t                length;
};

/*! Global string table lock mutex. */
static shared_mutex _string_table_lock;

/// <summary>
/// Global string table shared by all systems of the application.
/// Only allocated on demand.
/// </summary>
static string_table_t* GLOBAL_STRING_TABLE = nullptr;

/// <summary>
/// Contains the hash key of string stored in a string table.
/// </summary>
struct string_table_hash_length_t
{
    const char* s;
    uint32_t hash;
    int length;
};

string_table_symbol_t string_table_encode(string_const_t value)
{
    return string_table_encode(STRING_ARGS(value));
}

string_table_symbol_t string_table_encode(string_t value)
{
    return string_table_encode(STRING_ARGS(value));
}

string_table_symbol_t string_table_encode_unescape(string_const_t value)
{
    if (value.length == 0)
        return STRING_TABLE_NULL_SYMBOL;

    if (string_find(STRING_ARGS(value), '\\', 0) == STRING_NPOS)
        return string_table_encode(STRING_ARGS(value));

    scoped_string_t utf8 = string_utf8_unescape(STRING_ARGS(value));
    if (utf8.value.str)
        return string_table_encode(STRING_ARGS(utf8.value));

    return string_table_encode(STRING_ARGS(value));
}

string_table_symbol_t string_table_encode(const char* s, size_t length)
{
    if (s == nullptr || length == 0)
        return STRING_TABLE_NULL_SYMBOL;

    SHARED_WRITE_LOCK(_string_table_lock);

    string_table_symbol_t symbol = string_table_to_symbol(GLOBAL_STRING_TABLE, s, length);
    while (symbol == STRING_TABLE_FULL)
    {
        string_table_grow(&GLOBAL_STRING_TABLE, (int)(GLOBAL_STRING_TABLE->allocated_bytes * HASH_FACTOR));
        symbol = string_table_to_symbol(GLOBAL_STRING_TABLE, s, length);
    }

    return symbol;
}

const char* string_table_decode(string_table_symbol_t symbol)
{
    SHARED_READ_LOCK(_string_table_lock);
    return string_table_to_string(GLOBAL_STRING_TABLE, symbol);
}

string_t string_table_decode(char* buffer, size_t capacity, string_table_symbol_t symbol)
{
    SHARED_READ_LOCK(_string_table_lock);
    string_const_t str = string_table_to_string_const(GLOBAL_STRING_TABLE, symbol);
    return string_copy(buffer, capacity, str.str, str.length);
}

string_const_t string_table_decode_const(string_table_symbol_t symbol)
{
    SHARED_READ_LOCK(_string_table_lock);
    return string_table_to_string_const(GLOBAL_STRING_TABLE, symbol);
}

void string_table_compress()
{
    SHARED_WRITE_LOCK(_string_table_lock);
    string_table_pack(&GLOBAL_STRING_TABLE);
}

void string_table_initialize()
{
    SHARED_WRITE_LOCK(_string_table_lock);
    
    if (GLOBAL_STRING_TABLE == nullptr)
        GLOBAL_STRING_TABLE = string_table_allocate(32 * 1024, 16);
}

void string_table_shutdown()
{   
    if (GLOBAL_STRING_TABLE)
    {
        SHARED_WRITE_LOCK(_string_table_lock);
        log_debugf(HASH_STRING_TABLE, STRING_CONST("String table size: %.3g kb (average string length: %" PRIsize ")"), 
            GLOBAL_STRING_TABLE->allocated_bytes / 1024.0, string_table_average_string_length(GLOBAL_STRING_TABLE));
        string_table_deallocate(GLOBAL_STRING_TABLE);
        GLOBAL_STRING_TABLE = nullptr;
    }
}

string_table_t* string_table_allocate(int bytes, int average_string_size)
{
    bytes = max<int>(STRING_TABLE_MIN_SIZE, bytes);
    string_table_t* st = (string_table_t*)memory_allocate(HASH_STRING_TABLE, bytes, 4, MEMORY_PERSISTENT);
    string_table_init(st, bytes, average_string_size);
    return st;
}

void string_table_deallocate(string_table_t* st)
{
    array_deallocate(st->free_slots);
    memory_deallocate(st);
}

void string_table_init(string_table_t* st, int bytes, int average_strlen)
{
    FOUNDATION_ASSERT(bytes >= STRING_TABLE_MIN_SIZE);

    st->count = 0;
    st->free_slots = nullptr;
    st->allocated_bytes = bytes;

    float bytes_per_string = average_strlen + 1 + sizeof(uint16_t) * HASH_FACTOR;
    float num_strings = (bytes - sizeof(*st)) / bytes_per_string;
    st->num_hash_slots = max<int>(num_strings * HASH_FACTOR, 1);

    int bytes_for_strings_16 = bytes - sizeof(*st) - sizeof(uint16_t) * st->num_hash_slots;
    int bytes_for_strings_32 = bytes - sizeof(*st) - sizeof(uint32_t) * st->num_hash_slots;
    st->uses_16_bit_hash_slots = bytes_for_strings_32 <= 64 * 1024;

    memset(st->h16(), 0, st->num_hash_slots * st->hsize());

    // Empty string is stored at index 0. This way, we can use 0 as a marker for
    // empty hash slots.
    st->strings()[0] = 0;
    st->string_bytes = 1;
}

/// <summary>
/// The hash function is borrowed from Lua.
///
/// Since we need to walk the entire string anyway for finding the
/// length, this is a decent hash function.
/// </summary>
/// <param name="start"></param>
/// <returns></returns>
FOUNDATION_STATIC FOUNDATION_FORCEINLINE string_table_hash_length_t string_table_calc_hash_and_length(const char* start)
{
    uint32_t h = 0;
    const char* s = start;
    for (; *s; ++s)
        h = h ^ ((h << 5) + (h >> 2) + (unsigned char)*s);

    return string_table_hash_length_t { start, h, (int)(s - start) };
}

FOUNDATION_STATIC FOUNDATION_FORCEINLINE string_table_hash_length_t string_table_calc_hash_and_length(const char* start, size_t length)
{
    if (length == 0)
        return string_table_calc_hash_and_length(start);

    uint32_t h = 0;
    const char* s = start;
    for (size_t i = 0; i < length && *s; ++i, ++s)
        h = h ^ ((h << 5) + (h >> 2) + (unsigned char)*s);

    return string_table_hash_length_t { start, h, (int)(s - start) };
}

template<typename T> 
FOUNDATION_STATIC void string_table_rebuild_hash_table(const string_table_t* st, T* const ht)
{
    const char* strs = st->strings();
    const char* s = strs + 1;

    while (s < strs + st->string_bytes) 
    {
        const string_table_hash_length_t& hl = string_table_calc_hash_and_length(s);
        int i = hl.hash % st->num_hash_slots;
        int itr = 0;
        while (ht[i])
        {
            i = (i + 1) % st->num_hash_slots;
            FOUNDATION_ASSERT_MSG(++itr <= st->num_hash_slots, "String table hash table full, unable to add string");
        }
        ht[i] = (T)(s - strs);
        s = s + hl.length + 1;
    }
}

FOUNDATION_STATIC void string_table_rebuild_hash_table(string_table_t* st)
{
    const char* strs = st->strings();
    const char* s = strs + 1;

    memset(st->h16(), 0, st->num_hash_slots * st->hsize());

    if (st->uses_16_bit_hash_slots)
        string_table_rebuild_hash_table(st, st->h16());
    else
        string_table_rebuild_hash_table(st, st->h32());
}

void string_table_grow(string_table_t* st, int bytes)
{
    FOUNDATION_ASSERT((size_t)bytes >= st->allocated_bytes);

    const char* const old_strings = st->strings();
    st->allocated_bytes = bytes;

    float average_strlen = st->count > 0 ? string_table_average_string_length(st) : 15.0f;
    float bytes_per_string = average_strlen + 1 + sizeof(uint16_t) * HASH_FACTOR;
    float num_strings = (bytes - sizeof(*st)) / bytes_per_string;
    st->num_hash_slots = max<int>(num_strings * HASH_FACTOR, st->num_hash_slots);

    //int bytes_for_strings_16 = bytes - sizeof(*st) - sizeof(uint16_t) * st->num_hash_slots;
    int bytes_for_strings_32 = bytes - sizeof(*st) - sizeof(uint32_t) * st->num_hash_slots;
    st->uses_16_bit_hash_slots = bytes_for_strings_32 <= 64 * 1024;

    // Delete free slots
    array_deallocate(st->free_slots);

    char* const new_strings = st->strings();
    memmove(new_strings, old_strings, st->string_bytes);
    string_table_rebuild_hash_table(st);
}

string_table_t* string_table_grow(string_table_t** out_st, int bytes /*= 0*/)
{
    MEMORY_TRACKER(HASH_STRING_TABLE);

    string_table_t* st = *out_st;
    bytes = max<int>(st->allocated_bytes * HASH_FACTOR, bytes);
    FOUNDATION_ASSERT(bytes >= (int)st->allocated_bytes);

    size_t old_string_bytes = st->string_bytes;
    st->string_bytes = 0;
    *out_st = st = (string_table_t*)memory_reallocate(*out_st, bytes, 4, st->allocated_bytes, MEMORY_PERSISTENT);
    st->string_bytes = old_string_bytes;
    string_table_grow(st, bytes);

    return st;
}

string_table_t* string_table_pack(string_table_t** out_st)
{
    MEMORY_TRACKER(HASH_STRING_TABLE);

    string_table_t* st = *out_st;
    size_t old_size = st->allocated_bytes;
    size_t new_size = string_table_pack(st);
    *out_st = st = (string_table_t*)memory_reallocate(*out_st, new_size, 4, old_size, MEMORY_PERSISTENT);
    return st;
}

size_t string_table_pack(string_table_t* st)
{
    const char* old_strings = st->strings();

    st->num_hash_slots = (int)(st->count * HASH_FACTOR);
    if (st->num_hash_slots < 1)
        st->num_hash_slots = 1;
    if (st->num_hash_slots < st->count + 1)
        st->num_hash_slots = st->count + 1;
    st->uses_16_bit_hash_slots = st->string_bytes <= 64 * 1024;

    char* const new_strings = st->strings();
    memmove(new_strings, old_strings, st->string_bytes);
    string_table_rebuild_hash_table(st);

    st->allocated_bytes = (new_strings + st->string_bytes) - (char*)st;
    return st->allocated_bytes;
}

FOUNDATION_FORCEINLINE int string_table_available_string_bytes(string_table_t* st)
{
    return (int)(st->allocated_bytes - sizeof(*st) - st->num_hash_slots * st->hsize());
}

template<typename T>
FOUNDATION_STATIC int string_table_find_slot_index(const string_table_t* st, T* const ht, const string_table_hash_length_t& key)
{
    char* const strs = st->strings();
    int i = key.hash % st->num_hash_slots;
    while (ht[i])
    {
        const char* str = strs + ht[i];
        const size_t strs_remaining_length = st->string_bytes - (str - strs);
        if ((size_t)key.length < strs_remaining_length && str[key.length] == '\0' && strncmp(key.s, str, key.length) == 0)
            return i;
        i = (i + 1) % st->num_hash_slots;
    }

    return ~i;
}

template<typename T> 
FOUNDATION_STATIC string_table_symbol_t string_table_find_symbol(const string_table_t* st, T* const ht, const string_table_hash_length_t& key)
{
    int slot_idx = string_table_find_slot_index(st, ht, key);
    if (slot_idx >= 0)
        return ht[slot_idx];
    return slot_idx;
}

FOUNDATION_FORCEINLINE int string_table_free_slot_binary_search_compare(const string_table_free_slot_t& slot, size_t length)
{
    if (slot.length < length)
        return -1;
    if (slot.length > length)
        return 1;
    return 0;
}

FOUNDATION_STATIC int string_table_insert_free_slot(string_table_t* st, const string_table_free_slot_t& slot)
{
    int slot_idx = array_binary_search_compare(st->free_slots, slot.length, string_table_free_slot_binary_search_compare);
    if (slot_idx < 0)
        slot_idx = ~slot_idx;
    array_insert_memcpy_safe(st->free_slots, slot_idx, &slot);
    return slot_idx;
}

FOUNDATION_STATIC string_table_symbol_t string_table_available_slot(string_table_t* st, size_t length)
{
    if (array_size(st->free_slots) > 0)
    {
        int slot_idx = array_binary_search_compare(st->free_slots, length, string_table_free_slot_binary_search_compare);
        if (slot_idx >= 0)
        {
            const string_table_free_slot_t* fs = st->free_slots + slot_idx;
            const string_table_symbol_t symbol = fs->symbol;
            array_erase_ordered_safe(st->free_slots, slot_idx);
            return symbol;
        }
        else
        {
            slot_idx = ~slot_idx;
            while (slot_idx < to_int(array_size(st->free_slots)))
            {
                const string_table_free_slot_t* fs = st->free_slots + slot_idx;
                if (fs->length >= length)
                {
                    string_table_free_slot_t cfs = *fs;
                    const string_table_symbol_t symbol = fs->symbol;
                    array_erase_ordered_safe(st->free_slots, slot_idx);

                    if (fs->length > length)
                    {
                        cfs.length -= (length + 1);
                        cfs.symbol += (string_table_symbol_t)(length + 1);
                        string_table_insert_free_slot(st, cfs);
                    }
                    
                    return symbol;
                }
                ++slot_idx;
            }
        }
    }

    return (string_table_symbol_t)st->string_bytes;
}

string_table_symbol_t string_table_to_symbol(string_table_t* st, const char* s, size_t length)
{
    int i = string_table_find_symbol(st, s, length);
    if (i >= 0)
        return i;

    if (st->count + 1 >= st->num_hash_slots)
        return STRING_TABLE_FULL;

    if ((float)st->num_hash_slots / (float)(st->count + 1) < HASH_FACTOR)
        return STRING_TABLE_FULL;

    const string_table_symbol_t symbol = string_table_available_slot(st, length);

    if ((size_t)symbol + length + 1 > (size_t)string_table_available_string_bytes(st))
        return STRING_TABLE_FULL;

    if (st->uses_16_bit_hash_slots) 
    {
        if (symbol > 64 * 1024)
            return STRING_TABLE_FULL;
        st->h16(~i, (uint16_t)symbol);
    }
    else 
    {
        st->h32(~i, (uint32_t)symbol);
    }

    char* const strs = st->strings();
    char* const dest = strs + symbol;
    memcpy(dest, s, length);
    dest[length] = '\0';

    st->count++;
    if (st->string_bytes < symbol + length + 1)
        st->string_bytes = symbol + length + 1;
    return symbol;
}

string_table_symbol_t string_table_find_symbol(const string_table_t* st, const char* s, size_t length)
{
    // "" maps to 0
    if (!*s) return 0;

    const string_table_hash_length_t& hl = string_table_calc_hash_and_length(s, length);

    if (st->uses_16_bit_hash_slots)
        return string_table_find_symbol(st, st->h16(), hl);
    return string_table_find_symbol(st, st->h32(), hl);
}

const char* string_table_to_string(string_table_t* st, string_table_symbol_t symbol)
{
    if (symbol > 0 && (size_t)symbol <= st->string_bytes)
        return st->strings(symbol);
    return nullptr;
}

string_const_t string_table_to_string_const(string_table_t* st, string_table_symbol_t symbol)
{
    if (st->string_bytes == 0)
        return {};
    const char* str = st->strings(symbol);
    return string_const(str, string_length(str));
}

bool string_table_symbol_equal(string_table_symbol_t symbol, const char* str, size_t str_length)
{
    if (symbol == STRING_TABLE_NULL_SYMBOL)
        return (str == nullptr || str_length == 0);

    string_const_t symbol_string = string_table_decode_const(symbol);
    return string_equal(STRING_ARGS(symbol_string), str, str_length);
}

size_t string_table_average_string_length(string_table_t* st)
{
    if (st->count == 0)
        return 0;
    return math_ceil(st->string_bytes / (double)st->count);
}

bool string_table_symbol_equal(string_table_symbol_t symbol, string_const_t str)
{
    return string_table_symbol_equal(symbol, str.str, str.length);
}

bool string_table_is_valid(string_table_t* st)
{
    if (st == nullptr)
        return false;

    if (st->count < 0)
        return false;

    if (st->string_bytes < 0)
        return false;

    if (st->allocated_bytes < STRING_TABLE_MIN_SIZE)
        return false;

    if (st->num_hash_slots < 1)
        return false;

    return true;
}

bool string_table_remove_symbol(string_table_t* st, string_table_symbol_t symbol)
{
    if (symbol == 0)
        return false;

    string_const_t str = string_table_to_string_const(st, symbol);
    if (str.length == 0)
        return false;

    // Find the symbol in the hash table and reset it.
    string_table_hash_length_t hl = string_table_calc_hash_and_length(STRING_ARGS(str));
    if (st->uses_16_bit_hash_slots)
    {
        uint16_t* ht = st->h16();
        int slot_idx = string_table_find_slot_index(st, ht, hl);
        FOUNDATION_ASSERT(slot_idx >= 0 && ht[slot_idx] == symbol);
        ht[slot_idx] = 0;
    }
    else
    {
        uint32_t* ht = st->h32();
        int slot_idx = string_table_find_slot_index(st, ht, hl);
        FOUNDATION_ASSERT(slot_idx >= 0);
        ht[slot_idx] = 0;
    }

    auto strs = st->strings();
    string_table_free_slot_t free_slot;
    free_slot.symbol = symbol;
    free_slot.length = str.length;
    string_table_insert_free_slot(st, free_slot);

    // Clear the string content
    memset((void*)str.str, 0, str.length);

    // Decrement the string count
    --st->count;

    return true;

}

string_table_symbol_t string_table_add_symbol(string_table_t*& st, const char* str, size_t length)
{
    string_table_symbol_t symbol = string_table_to_symbol(st, str, length);
    while (symbol == STRING_TABLE_FULL)
    {
        string_table_grow(&st, (int)(st->allocated_bytes * HASH_FACTOR));
        symbol = string_table_to_symbol(st, str, length);
    }

    return symbol;
}
