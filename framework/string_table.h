/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/string.h>

#define STRING_TABLE_FULL (-1)
#define STRING_TABLE_NOT_FOUND (-2)
#define STRING_TABLE_NULL_SYMBOL (0)

#define SYMBOL_CSTR(s) string_table_decode(s)
#define SYMBOL_CONST(s) string_table_decode_const(s)

struct string_table_free_slot_t;

/*! Handle to a string content in a string table. */
typedef int string_table_symbol_t;

/*! Structure representing a string table. The data for the table is stored
 *  directly after this header in memory and consists of a hash table
 *  followed by a string data block.
 * 
 *  Here's the layout of the string table in memory:
 *  +-----------------------+
 *  | string_table_t        |
 *  +-----------------------+
 *  | hash table            |
 *  +-----------------------+
 *  | string data           |
 *  +-----------------------+
 */
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

    /*! The current number of bytes used for string data. */
    size_t string_bytes;

    /*! List of free slots in the hash table. 
     *  A free slot is a slot that has been used before but is now empty.
     */
    string_table_free_slot_t* free_slots;

    FOUNDATION_FORCEINLINE uint16_t* h16() const { return (uint16_t*)(this + 1); }
    FOUNDATION_FORCEINLINE uint32_t* h32() const { return (uint32_t*)(this + 1); }
    FOUNDATION_FORCEINLINE const uint16_t& h16(size_t index) const { return h16()[index]; }
    FOUNDATION_FORCEINLINE const uint32_t& h32(size_t index) const { return h32()[index]; }
    FOUNDATION_FORCEINLINE void h16(size_t index, uint16_t v) const { h16()[index] = v; }
    FOUNDATION_FORCEINLINE void h32(size_t index, uint32_t v) const { h32()[index] = v; }
    FOUNDATION_FORCEINLINE size_t hsize() const { return uses_16_bit_hash_slots ? sizeof(uint16_t) : sizeof(uint32_t); }

    /*! Returns a pointer to the string table strings content buffer.
     *  The buffer is not null terminated.
     * 
     *  @param index The index of the string to return.
     *  
     *  @return A pointer to the string content.
     */
    FOUNDATION_FORCEINLINE char* strings(size_t index = 0) const
    {
        return (uses_16_bit_hash_slots ?
            (char*)(h16() + num_hash_slots) :
            (char*)(h32() + num_hash_slots)) + index;
    }
};

/*! @def STRING_TABLE_MIN_SIZE
 * 
 *  @brief The minimum size of a string table.
 * 
 *  We must have room for at least one hash slot and one string.
 */
#define STRING_TABLE_MIN_SIZE (sizeof(string_table_t) + 1*(uint32_t) + 4)

 /*! Allocate a string table and initialize it.
  * 
  *  Memory allocation is done within this function.
  *  You must call @string_table_deallocate to release the table.
  * 
  *  @param bytes               The total size of the string table in bytes.
  *  @param average_string_size The average length of the strings that will be added.
  * 
  *  @return A pointer to the string table.
  */
string_table_t* string_table_allocate(int bytes = STRING_TABLE_MIN_SIZE, int average_string_size = 16);

/*! Grow a string table storage in-place by reallocating the memory. 
 * 
 *  @param out_st A pointer to the string table pointer.
 *  @param bytes  The total size of the string table in bytes.
 * 
 *  @return A pointer to the string table.
 */
string_table_t* string_table_grow(string_table_t** out_st, int bytes = 0);

/*! Pack a string table storage in-place by reallocating the memory. 
 * 
 *  @param out_st A pointer to the string table pointer.
 * 
 *  @return A pointer to the string table.
 */
string_table_t* string_table_pack(string_table_t** out_st);

/*! Release the resources consumed by a string table.
 * 
 *  @param st A pointer to the string table.
 */
void string_table_deallocate(string_table_t* st);

/*! Initialize a string table.
 * 
 *  Memory allocation is not done within this function.
 *  You must call @string_table_deallocate to release the table.
 *
 *  You must make sure that the string table has enough memory allocated 
 *  to hold the data.
 * 
 *  @param st                  A pointer to the string table.
 *  @param bytes               The total size of the string table in bytes.
 *  @param average_string_size The average length of the strings that will be added.
 */
void string_table_init(string_table_t* st, int bytes, int average_string_size);

/*! Grow a string table storage in-place by reallocating the memory.
 *
 *  Grows the string table to size `bytes`. You must make sure that this many
 *  bytes are available in the pointer `st` (typically by calling realloc before
 *  calling this function). If `bytes` is less than the current size of the table,
 *  the function does nothing.
 * 
 *  @param st   A pointer to the string table. 
 *  @param bytes The total size of the string table in bytes.
 * 
 *  @return A pointer to the string table.
 */
void string_table_grow(string_table_t* st, int bytes);

/*! Pack the string table.
 * 
 *  Packs the string table so that it uses as little memory as possible while
 *  still preserving the content. Updates st->allocated_bytes and returns the
 *  new value. You can use that to shrink the buffer with realloc() if so desired.
 * 
 *  @param st A pointer to the string table.
 *  
 *  @return The new size of the string table.
 */
size_t string_table_pack(string_table_t* st);

/*! Adds and return a symbol for the string `s`.
 * 
 *  If the string already exists in the table, the existing symbol is returned.
 *  If the string doesn't exist in the table, it is added and a new symbol is
 *  returned.
 * 
 *  @param st    A pointer to the string table.
 *  @param s     The string to add.
 *  @param length The length of the string. If 0, the string is assumed to be null terminated.
 * 
 *  @return A symbol for the string.
 */
string_table_symbol_t string_table_to_symbol(string_table_t* st, const char* s, size_t length);

/*! Add a string to the table.
 * 
 *  If the table is full, we grow it by 50% to ensure that we have enough room
 *  for the new string.
 * 
 *  @param st     A pointer to the string table.
 *  @param str    The string to add.
 *  @param length The length of the string. If 0, the string is assumed to be null terminated.
 * 
 *  @return A symbol for the string.
 */
string_table_symbol_t string_table_add_symbol(string_table_t*& st, const char* str, size_t length);

/*! Checks if a symbol is already stored. 
 * 
 *  Like #string_table_to_symbol(), but never adds the string to the table.
 *  If the string doesn't exist in the table a negative value is returned.
 * 
 *  @param st    A pointer to the string table.
 *  @param s     The string to check.
 *  @param length The length of the string. If 0, the string is assumed to be null terminated.
 * 
 *  @return An existing symbol for the string, or a negative value if the string is not in the table.
 */
string_table_symbol_t string_table_find_symbol(const string_table_t* st, const char* s, size_t length = 0);

/*! Returns the string representation of the string symbol.
 * 
 *  @param st    A pointer to the string table.
 *  @param symbol The symbol to return the string for.
 * 
 *  @return A pointer to the string content.
 */
const char* string_table_to_string(string_table_t* st, string_table_symbol_t symbol);

/*! Returns the string representation of the string symbol.
 * 
 *  @param st    A pointer to the string table.
 *  @param symbol The symbol to return the string for.
 * 
 *  @return A #string_const_t containing the string content.
 */
string_const_t string_table_to_string_const(string_table_t* st, string_table_symbol_t symbol);

/*! Returns the average string length of string already stored in the table.
 * 
 *  @remark This is an approximation and may not be accurate.
 *  @remark This function is not thread safe.
 * 
 *  @param st The string table to query.
 * 
 *  @return The average string length.
 */
size_t string_table_average_string_length(string_table_t* st);

//
// # Global string table access
//

/*! Store a string in the global string table shared by all systems in the application.
 * 
 *  @remark #s is not deallocated or modified. 
 * 
 *  @param s The string to store in the global string table.
 *  @param length The length of the string. If 0, the string is assumed to be null terminated.
 * 
 *  @return String symbol for the escaped encoded string.
 */
string_table_symbol_t string_table_encode(const char* s, size_t length);

/*! Store a string in the global string table shared by all systems in the application.
 * 
 *  @remark #value is not deallocated or modified.
 *
 *  @param s The string to store in the global string table.
 *  @return String symbol for the escaped encoded string.
 */
string_table_symbol_t string_table_encode(string_t value);

/*! Store a string in the global string table shared by all systems in the application.
 *  @param s The string to store in the global string table
 *  @return String symbol for the escaped encoded string.
 */
string_table_symbol_t string_table_encode(string_const_t value);

/*! Store a string in the global string table shared by all systems in the application. 
 *  @param s The string to store in the global string table
 *  @return String symbol for the unescaped encoded string.
 */
string_table_symbol_t string_table_encode_unescape(string_const_t value);

/*! Returns the string content for a given symbol in the global application string table. 
 * 
 *  If this method is never called, the global string table will never be instantiated.
 *
 *  @param symbol The symbol to resolve
 * 
 *  @return The string content for the symbol
 */
const char* string_table_decode(string_table_symbol_t symbol);

/*! Returns the string content for a given symbol in the global application string table. 
 * 
 *  @param symbol The symbol to resolve
 *  @return The string content for the symbol
 */
string_const_t string_table_decode_const(string_table_symbol_t symbol);

/*! Compact the global string table. */
void string_table_compress();

/*! Initialize the shared global string table. */
void string_table_initialize();

/*! Release the memory used by the global string table. */
void string_table_shutdown();

/*! Checks if symbols is equal to #str
 *  @param symbol       The symbol to resolve and check
 *  @param str          The string to compare to
 *  @param str_length   The length of the string to compare to
 *  @return True if the symbol resolves to the string, false otherwise.
 */
bool string_table_symbol_equal(string_table_symbol_t symbol, const char* str, size_t str_length);

 /*! Checks if symbols is equal to #str.
  *  @param symbol       The symbol to resolve and check
  *  @param str          The string to compare to
  *  @return True if the symbol resolves to the string, false otherwise.
  */
 bool string_table_symbol_equal(string_table_symbol_t symbol, string_const_t str);

 /*! Checks if the string table is valid.
  *
  *  @param st The string table to check
  * 
  *  @return True if the string table is valid, false otherwise.
  */
 bool string_table_is_valid(string_table_t* st);

 /*! Remove a symbol in the string table and mark the symbol space as free/re-usable.
  * 
  *  The string content is cleared and we mark the symbol as free.
  *  The symbol can be reused by a new string of the same length or less.
  *  The symbol might still resolve, but the string content is not guaranteed to be correct.
  *  You will usually get an empty string when the symbol is freed, but not re-used.
  * 
  *  @param st The string table to free the symbol from.
  *  @param symbol The symbol to free.
  * 
  *  @return True if the symbol was freed, false otherwise.
  */
bool string_table_remove_symbol(string_table_t* st, string_table_symbol_t symbol);
