/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include "function.h"

#include <foundation/math.h>
#include <foundation/string.h>

struct config_t;
struct config_value_t;

typedef char* config_sjson_t;
typedef const char* config_sjson_const_t;
typedef unsigned int config_index_t;

typedef enum : uint8_t {
    CONFIG_VALUE_UNDEFINED = (uint8_t)-1,
    CONFIG_VALUE_NIL = 0,
    CONFIG_VALUE_TRUE,
    CONFIG_VALUE_FALSE,
    CONFIG_VALUE_NUMBER,
    CONFIG_VALUE_STRING,
    CONFIG_VALUE_ARRAY,
    CONFIG_VALUE_OBJECT,
    CONFIG_VALUE_RAW_DATA
} config_value_type_t;

typedef enum : uint32_t {
    CONFIG_OPTION_NONE = 0,
    CONFIG_OPTION_PRESERVE_INSERTION_ORDER = 1 << 0,
    CONFIG_OPTION_SORT_OBJECT_FIELDS = 1 << 1,
    CONFIG_OPTION_PACK_STRING_TABLE = 1 << 2,
    CONFIG_OPTION_PARSE_UNICODE_UTF8 = 1 << 3,
    CONFIG_OPTION_ALLOCATE_TEMPORARY = 1 << 4,

    CONFIG_OPTION_ALL = CONFIG_OPTION_PRESERVE_INSERTION_ORDER | CONFIG_OPTION_PACK_STRING_TABLE,

    // Output/Write options
    CONFIG_OPTION_WRITE_JSON = 1 << 19,
    CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS = 1 << 20,
    CONFIG_OPTION_WRITE_SKIP_NULL = 1 << 21,
    CONFIG_OPTION_WRITE_SKIP_DOUBLE_COMMA_FIELDS = 1 << 22,
    CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES = 1 << 23,
    CONFIG_OPTION_WRITE_TRUNCATE_NUMBERS = 1 << 24,
    CONFIG_OPTION_WRITE_NO_SAVE_ON_DATA_EQUAL = 1 << 25

} config_option_t;
typedef uint32_t config_option_flags_t;

struct config_tag_t
{
    int symbol;
};

struct config_handle_t
{
    config_t* config;
    config_index_t index;

    operator config_value_t* () const;
    config_handle_t operator[] (config_index_t index) const;
    config_handle_t operator[] (const char* key) const;
    config_handle_t operator[] (const config_tag_t& tag) const;
    config_handle_t operator[] (const string_const_t& tag) const;

    config_handle_t operator= (bool b) const;
    config_handle_t operator= (const char* value) const;
    config_handle_t operator= (string_const_t value) const;
    config_handle_t operator= (double number) const;

    struct iterator
    {
        config_t* config;
        config_index_t index;

        bool operator!=(const iterator& other) const
        {
            if (!config || index == (config_index_t)(-1))
                return false;
            return config != other.config || index != other.index;
        }

        bool operator==(const iterator& other) const
        {
            return !operator!=(other);
        }

        iterator& operator++();

        config_handle_t operator*() const
        {
            return config_handle_t{ config, index };
        }
    };

    iterator begin(size_t index = 0) const;
    iterator end() const;

    string_const_t name() const;

    bool as_boolean(bool default_value = false) const;
    double as_number(double default_value = NAN) const;
    string_const_t as_string(const char* default_string = nullptr, size_t default_string_length = 0, const char* fmt = nullptr) const;
    template<typename T = int> T as_integer() const { return (T)as_number(); }
};

config_handle_t config_null();
config_handle_t config_allocate(config_value_type_t type = CONFIG_VALUE_OBJECT, config_option_flags_t options = CONFIG_OPTION_NONE);
void config_deallocate(config_handle_t& root);

config_tag_t config_get_tag(config_handle_t h, const char* tag, size_t tag_length);

config_option_flags_t config_get_options(config_handle_t root);
config_option_flags_t config_set_options(config_handle_t root, config_option_flags_t options);

config_handle_t config_element_at(config_handle_t array_or_obj, size_t index);
config_handle_t config_find(config_handle_t obj, int symbol);
config_handle_t config_find(config_handle_t obj, const config_tag_t& tag);
config_handle_t config_find(config_handle_t obj, const char* key, size_t key_length);

const void* config_value_as_pointer_unsafe(config_handle_t value);
bool config_value_as_boolean(config_handle_t boolean_value, bool default_value = false);
double config_value_as_number(config_handle_t number_value, double default_value = NAN);
string_const_t config_value_as_string(config_handle_t string_value, const char* fmt = nullptr);

config_value_type_t config_value_type(config_handle_t v);
config_handle_t config_add(config_handle_t v, int symbol);
config_handle_t config_add(config_handle_t v, const char* key, size_t key_length);
bool config_remove(config_handle_t obj_or_array, config_handle_t v);
bool config_remove(config_handle_t v, const char* key, size_t key_length);
config_handle_t config_get_or_create(config_handle_t v, int symbol);
config_handle_t config_get_or_create(config_handle_t v, const config_tag_t& tag);
config_handle_t config_get_or_create(config_handle_t v, const char* key, size_t key_length);

config_handle_t config_set(config_handle_t v, bool value);
config_handle_t config_set(config_handle_t v, double number);
config_handle_t config_set(config_handle_t v, const void* data);
config_handle_t config_set(config_handle_t v, const char* string_value, size_t string_length);

config_handle_t config_set(config_handle_t v, const char* key, size_t key_length, bool value);
config_handle_t config_set(config_handle_t v, const char* key, size_t key_length, double number);
config_handle_t config_set(config_handle_t v, const char* key, size_t key_length, const void* data);
config_handle_t config_set(config_handle_t v, const char* key, size_t key_length, string_const_t string_value);
config_handle_t config_set(config_handle_t v, const char* key, size_t key_length, const char* string_value, size_t string_length);

template <size_t N> FOUNDATION_FORCEINLINE constexpr config_handle_t config_set(config_handle_t v, const char(&key)[N], bool value) { return config_set(v, key, sizeof(key)-1, value); }
template <size_t N> FOUNDATION_FORCEINLINE constexpr config_handle_t config_set(config_handle_t v, const char(&key)[N], double number) { return config_set(v, key, sizeof(key)-1, number); }
template <size_t N> FOUNDATION_FORCEINLINE constexpr config_handle_t config_set(config_handle_t v, const char(&key)[N], const void* data) { return config_set(v, key, sizeof(key)-1, data); }
template <size_t N> FOUNDATION_FORCEINLINE constexpr config_handle_t config_set(config_handle_t v, const char(&key)[N], string_const_t string_value) { return config_set(v, key, sizeof(key)-1, string_value); }
template <size_t N> FOUNDATION_FORCEINLINE constexpr config_handle_t config_set(config_handle_t v, const char(&key)[N], const char* string_value, size_t string_length) { 	return config_set(v, key, sizeof(key)-1, string_value, string_length); }

config_handle_t config_set(config_handle_t v, const config_tag_t& tag, bool value);
config_handle_t config_set(config_handle_t v, const config_tag_t& tag, double number);
config_handle_t config_set(config_handle_t v, const config_tag_t& tag, const void* data);
config_handle_t config_set(config_handle_t v, const config_tag_t& tag, const char* string_value, size_t string_length);

config_handle_t config_set_object(config_handle_t v, const char* key, size_t key_length);
config_handle_t config_set_array(config_handle_t v, const char* key, size_t key_length);
config_handle_t config_set_null(config_handle_t v, const char* key, size_t key_length);

string_const_t config_name(config_handle_t obj);
size_t config_size(config_handle_t obj);

config_handle_t config_array_push(config_handle_t v, config_value_type_t type = CONFIG_VALUE_NIL, const char* name = nullptr, size_t name_length = 0);
config_handle_t config_array_push(config_handle_t v, bool value);
config_handle_t config_array_push(config_handle_t v, double number);
config_handle_t config_array_push(config_handle_t v, const char* value, size_t value_length);
config_handle_t config_array_insert(config_handle_t v, size_t index, config_value_type_t type = CONFIG_VALUE_NIL, const char* name = nullptr, size_t name_length = 0);
config_handle_t config_array_insert(config_handle_t v, size_t index, bool value);
config_handle_t config_array_insert(config_handle_t v, size_t index, double number);
config_handle_t config_array_insert(config_handle_t v, size_t index, const char* value, size_t value_length);
bool config_array_pop(config_handle_t v);
void config_array_sort(config_handle_t array_handle, function<bool(const config_handle_t& a, const config_handle_t& b)> sort_fn);

void config_pack(config_handle_t value);
void config_clear(config_handle_t value);

bool config_is_valid(config_handle_t v, const char* key = nullptr, size_t key_length = 0);
bool config_exists(config_handle_t v, const char* key, size_t key_length);
bool config_is_null(config_handle_t v, const char* key = nullptr, size_t key_length = 0);
bool config_is_undefined(config_handle_t v, const char* key = nullptr, size_t key_length = 0);

bool config_write_file(string_const_t file_path, config_handle_t data, config_option_flags_t write_json_flags = CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS | CONFIG_OPTION_WRITE_SKIP_NULL);
bool config_write_file(
    string_const_t file_path,
    function<bool(config_handle_t data)> write_callback,
    config_value_type_t value_type = CONFIG_VALUE_OBJECT,
    config_option_flags_t write_json_flags = CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS | CONFIG_OPTION_WRITE_SKIP_NULL);
config_handle_t config_parse_file(const char* file_path, size_t file_path_length, config_option_flags_t options = CONFIG_OPTION_NONE);
config_handle_t config_parse(const char* json, size_t json_length, config_option_flags_t options = CONFIG_OPTION_NONE);
config_sjson_const_t config_sjson(config_handle_t value, config_option_flags_t options = CONFIG_OPTION_NONE);
string_const_t config_sjson_to_string(config_sjson_const_t sjson);
void config_sjson_deallocate(config_sjson_const_t sjson);
