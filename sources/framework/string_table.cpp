/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "string_table.h"

#include <framework/common.h>
#include <framework/profiler.h>
#include <framework/shared_mutex.h>
#include <framework/scoped_string.h>
#include <framework/string.h>

#include <foundation/hash.h>
#include <foundation/memory.h>
#include <foundation/assert.h>

#define HASH_STRING_TABLE static_hash_string("string_table", 12, 0xf026bfe3a9500e3cLL)

#define HASH_FACTOR (2.0f)

/// <summary>
/// Global string table shared by all systems of the application.
/// Only allocated on demand.
/// </summary>
static string_table_t* GLOBAL_STRING_TABLE = nullptr;
static shared_mutex _string_table_lock;

/// <summary>
/// Contains the hash key of string stored in a string table.
/// </summary>
struct HashAndLength
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

string_table_symbol_t string_table_encode(const char* s, size_t length /* = 0*/)
{
    if (length == 0)
        length = string_length(s);

    if (length == 0)
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
    memory_deallocate(st);
}

void string_table_init(string_table_t* st, int bytes, int average_strlen)
{
    FOUNDATION_ASSERT(bytes >= STRING_TABLE_MIN_SIZE);

    st->allocated_bytes = bytes;
    st->count = 0;

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
static FOUNDATION_FORCEINLINE HashAndLength hash_and_length(const char* start)
{
    uint32_t h = 0;
    const char* s = start;
    for (; *s; ++s)
        h = h ^ ((h << 5) + (h >> 2) + (unsigned char)*s);

    return HashAndLength { start, h, (int)(s - start) };
}

static FOUNDATION_FORCEINLINE HashAndLength hash_and_length(const char* start, size_t length)
{
    if (length == 0)
        return hash_and_length(start);

    uint32_t h = 0;
    const char* s = start;
    for (int i = 0; i < length && *s; ++i, ++s)
        h = h ^ ((h << 5) + (h >> 2) + (unsigned char)*s);

    return HashAndLength { start, h, (int)(s - start) };
}

template<typename T> static void rebuild_hash_table(const string_table_t* st, T* const ht)
{
    const char* strs = st->strings();
    const char* s = strs + 1;

    while (s < strs + st->string_bytes) {
        const HashAndLength& hl = hash_and_length(s);
        int i = hl.hash % st->num_hash_slots;
        while (ht[i])
            i = (i + 1) % st->num_hash_slots;
        ht[i] = (T)(s - strs);
        s = s + hl.length + 1;
    }
}

static void rebuild_hash_table(string_table_t* st)
{
    const char* strs = st->strings();
    const char* s = strs + 1;

    memset(st->h16(), 0, st->num_hash_slots * st->hsize());

    if (st->uses_16_bit_hash_slots)
        rebuild_hash_table(st, st->h16());
    else
        rebuild_hash_table(st, st->h32());
}

void string_table_grow(string_table_t* st, int bytes)
{
    FOUNDATION_ASSERT(bytes >= st->allocated_bytes);

    const char* const old_strings = st->strings();
    st->allocated_bytes = bytes;

    float average_strlen = st->count > 0 ? string_table_average_string_length(st) : 15.0f;
    float bytes_per_string = average_strlen + 1 + sizeof(uint16_t) * HASH_FACTOR;
    float num_strings = (bytes - sizeof(*st)) / bytes_per_string;
    st->num_hash_slots = max<int>(num_strings * HASH_FACTOR, st->num_hash_slots);

    //int bytes_for_strings_16 = bytes - sizeof(*st) - sizeof(uint16_t) * st->num_hash_slots;
    int bytes_for_strings_32 = bytes - sizeof(*st) - sizeof(uint32_t) * st->num_hash_slots;
    st->uses_16_bit_hash_slots = bytes_for_strings_32 <= 64 * 1024;

    char* const new_strings = st->strings();
    memmove(new_strings, old_strings, st->string_bytes);
    rebuild_hash_table(st);
}

string_table_t* string_table_grow(string_table_t** out_st, int bytes /*= 0*/)
{
    MEMORY_TRACKER(HASH_STRING_TABLE);

    string_table_t* st = *out_st;
    bytes = max<int>(st->allocated_bytes * HASH_FACTOR, bytes);
    FOUNDATION_ASSERT(bytes >= st->allocated_bytes);

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
    rebuild_hash_table(st);

    st->allocated_bytes = (new_strings + st->string_bytes) - (char*)st;
    return st->allocated_bytes;
}

static inline int available_string_bytes(string_table_t* st)
{
    return (int)(st->allocated_bytes - sizeof(*st) - st->num_hash_slots * st->hsize());
}

template<typename T> static string_table_symbol_t find_symbol(const string_table_t* st, T* const ht, const HashAndLength& key)
{
    char* const strs = st->strings();
    int i = key.hash % st->num_hash_slots;
    while (ht[i]) {
        if (strncmp(key.s, strs + ht[i], key.length) == 0)
            return ht[i];
        i = (i + 1) % st->num_hash_slots;
    }

    return ~i;
}

string_table_symbol_t string_table_to_symbol(string_table_t* st, const char* s, size_t length /*= 0*/)
{
    int i = string_table_find_symbol(st, s, length);
    if (i >= 0)
        return i;

    if (st->count + 1 >= st->num_hash_slots)
        return STRING_TABLE_FULL;

    if ((float)st->num_hash_slots / (float)(st->count + 1) < HASH_FACTOR)
        return STRING_TABLE_FULL;

    if (st->string_bytes + length + 1 > available_string_bytes(st))
        return STRING_TABLE_FULL;

    const string_table_symbol_t symbol = (string_table_symbol_t)st->string_bytes;
    if (st->uses_16_bit_hash_slots) {
        if (symbol > 64 * 1024)
            return STRING_TABLE_FULL;
        st->h16(~i, (uint16_t)symbol);
    }
    else {
        st->h32(~i, (uint32_t)symbol);
    }
    st->count++;

    char* const strs = st->strings();
    char* const dest = strs + st->string_bytes;
    memcpy(dest, s, length + 1);
    dest[length] = '\0';

    st->string_bytes += length + 1;
    return symbol;
}

string_table_symbol_t string_table_find_symbol(const string_table_t* st, const char* s, size_t length /*= 0*/)
{
    // "" maps to 0
    if (!*s) return 0;

    const HashAndLength& hl = hash_and_length(s, length);

    if (st->uses_16_bit_hash_slots)
        return find_symbol(st, st->h16(), hl);
    return find_symbol(st, st->h32(), hl);
}

const char* string_table_to_string(string_table_t* st, string_table_symbol_t symbol)
{
    if (symbol > 0 && symbol <= st->string_bytes)
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
