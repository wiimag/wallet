/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#pragma once

#include <foundation/string.h>

#define STRING_TABLE_FULL (-1)
#define STRING_TABLE_NOT_FOUND (-2)
#define STRING_TABLE_NULL_SYMBOL (0)

#define SYMBOL_CSTR(s) string_table_decode(s)
#define SYMBOL_CONST(s) string_table_decode_const(s)
#define SYMBOL_ARGS(s) STRING_ARGS(string_table_decode_const(s))
#define SYMBOL_CAPACITY(s) STRING_ARGS_CAPACITY(string_table_decode_const(s))
#define SYMBOL_FORMAT(s) STRING_FORMAT(string_table_decode_const(s))

/// <summary>
/// Handle to a string content in a string table.
/// </summary>
typedef int string_table_symbol_t;

/// <summary>
/// Structure representing a string table. The data for the table is stored
/// directly after this header in memory and consists of a hash table
/// followed by a string data block.
/// </summary>
struct string_table_t
{
    // The total size of the allocated data, including this header.
    size_t allocated_bytes;

    // The number of strings in the table.
    int count;

    // Does the hash table use 16 bit slots
    int uses_16_bit_hash_slots;

    // Total number of slots in the hash table.
    int num_hash_slots;

    // The current number of bytes used for string data.
    size_t string_bytes;

    inline uint16_t* h16() const { return (uint16_t*)(this + 1); }
    inline uint32_t* h32() const { return (uint32_t*)(this + 1); }
    inline const uint16_t& h16(size_t index) const { return h16()[index]; }
    inline const uint32_t& h32(size_t index) const { return h32()[index]; }
    inline const uint16_t& h16(size_t index, uint16_t v) const { return h16()[index] = v; }
    inline const uint32_t& h32(size_t index, uint32_t v) const { return h32()[index] = v; }
    inline size_t hsize() const { return uses_16_bit_hash_slots ? sizeof(uint16_t) : sizeof(uint32_t); }

    /// <summary>
    /// Returns a pointer to the string table strings content buffer.
    /// </summary>
    /// <param name="index"></param>
    /// <returns></returns>
    inline char* strings(size_t index = 0) const
    {
        return (uses_16_bit_hash_slots ?
            (char*)(h16() + num_hash_slots) :
            (char*)(h32() + num_hash_slots)) + index;
    }
};

// We must have room for at least one hash slot and one string
#define STRING_TABLE_MIN_SIZE (sizeof(string_table_t) + 1*(uint32_t) + 4)

/// <summary>
/// Allocate a string table and initialize it. 
/// Memory allocation is done within this function.
/// You must call @string_table_deallocate to release the table.
/// </summary>
/// <param name="bytes"></param>
/// <param name="average_string_size"></param>
/// <returns></returns>
string_table_t* string_table_allocate(int bytes = STRING_TABLE_MIN_SIZE, int average_string_size = 16);

/// <summary>
/// Grow a string table storage in-place by reallocating the memory.
/// </summary>
/// <param name="out_st"></param>
/// <param name="bytes"></param>
/// <returns></returns>
string_table_t* string_table_grow(string_table_t** out_st, int bytes = 0);

/// <summary>
/// Compact a string table in-place so it takes less space.
/// </summary>
/// <param name="out_st"></param>
/// <returns></returns>
string_table_t* string_table_pack(string_table_t** out_st);

/// <summary>
/// Release the resources consumed by a string table.
/// </summary>
/// <param name="st"></param>
void string_table_deallocate(string_table_t* st);

/// <summary>
/// Initializes an empty string table in the specified memory area. `bytes` is
/// the total ammount of memory allocated at the pointer and `average_strlen` is
/// the expected average length of the strings that will be added.
/// </summary>
/// <param name="st"></param>
/// <param name="bytes"></param>
/// <param name="average_string_size"></param>
void string_table_init(string_table_t* st, int bytes, int average_string_size);

/// <summary>
/// Grows the string table to size `bytes`. You must make sure that this many
/// bytes are available in the pointer `st` (typically by calling realloc before
/// calling this function).
/// </summary>
/// <param name="st"></param>
/// <param name="bytes"></param>
void string_table_grow(string_table_t* st, int bytes);

/// <summary>
/// Packs the string table so that it uses as little memory as possible while
/// still preserving the content. Updates st->allocated_bytes and returns the
/// new value. You can use that to shrink the buffer with realloc() if so desired.
/// </summary>
/// <param name="st"></param>
/// <returns></returns>
size_t string_table_pack(string_table_t* st);

/// <summary>
/// Returns the symbol for the string `s`. If `s` is not already in the table,
/// it is added. If `s` can't be added because the table is full, the function
/// returns `STRING_TABLE_FULL`.
///
/// The empty string is guaranteed to have the symbol `0`.
/// </summary>
/// <param name="st"></param>
/// <param name="s"></param>
/// <param name="length"></param>
/// <returns></returns>
string_table_symbol_t string_table_to_symbol(string_table_t* st, const char* s, size_t length = 0);

/// <summary>
/// As string_table_to_symbol(), but never adds the string to the table.
/// If the string doesn't exist in the table STRING_TABLE_FULL is returned.
/// </summary>
/// <param name="st"></param>
/// <param name="s"></param>
/// <param name="length"></param>
/// <returns></returns>
string_table_symbol_t string_table_find_symbol(const string_table_t* st, const char* s, size_t length = 0);

/// <summary>
/// Returns the string corresponding to the `symbol`. Calling this with a
/// value which is not a symbol returned by `string_table_to_symbol()` results in
/// undefined behavior.
/// </summary>
/// <param name=""></param>
/// <param name="symbol"></param>
/// <returns></returns>
const char* string_table_to_string(string_table_t* st, string_table_symbol_t symbol);

/// <summary>
/// Returns the const string representation of the string symbol.
/// </summary>
/// <param name=""></param>
/// <param name="symbol"></param>
/// <returns></returns>
string_const_t string_table_to_string_const(string_table_t* st, string_table_symbol_t symbol);

/// # Global string table access

/// <summary>
/// Store a string in the global string table shared by all systems in the application.
/// </summary>
/// <param name="s"></param>
/// <param name="length"></param>
/// <returns></returns>
string_table_symbol_t string_table_encode(const char* s, size_t length = 0);

string_table_symbol_t string_table_encode(string_t value);
string_table_symbol_t string_table_encode(string_const_t value);
string_table_symbol_t string_table_encode_unescape(string_const_t value);

/// <summary>
/// Returns the string content for a given symbol in the global application string table.
/// </summary>
/// <notes>
/// If this method is never called, the global string table will never be instantiated.
/// </notes>
/// <param name="symbol"></param>
/// <returns></returns>
const char* string_table_decode(string_table_symbol_t symbol);
string_const_t string_table_decode_const(string_table_symbol_t symbol);

/// <summary>
/// Compact the global string table.
/// </summary>
void string_table_compress();

/// <summary>
/// Initialize the shared global string table.
/// </summary>
void string_table_initialize();

/// <summary>
/// Release the memory used by the global string table.
/// </summary>
void string_table_shutdown();
