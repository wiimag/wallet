/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 *
 * Config value module.
 * 
 * This module provides a dynamic and simple JSON-like config value structure.
 * It is designed to be used as a simple key/value store, but can also be used
 * as a JSON parser and writer.
 *
 *
 * It also support the SJSON, Simplified-JSON, format which is a superset JSON which
 * is more compact and easier to read and write. It also much better with merge conflicts
 * as it is line based.
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

/*! Config value primitive types. */
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

/*! Loading and saving config value options */
typedef enum : uint32_t {
    CONFIG_OPTION_NONE = 0,
    CONFIG_OPTION_PRESERVE_INSERTION_ORDER = 1 << 0,
    CONFIG_OPTION_SORT_OBJECT_FIELDS = 1 << 1,
    CONFIG_OPTION_PACK_STRING_TABLE = 1 << 2,
    CONFIG_OPTION_PARSE_UNICODE_UTF8 = 1 << 3,
    CONFIG_OPTION_ALLOCATE_TEMPORARY = 1 << 4,

    // Output/Write options
    CONFIG_OPTION_WRITE_JSON = 1 << 19,
    CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS = 1 << 20,
    CONFIG_OPTION_WRITE_SKIP_NULL = 1 << 21,
    CONFIG_OPTION_WRITE_SKIP_DOUBLE_COMMA_FIELDS = 1 << 22,
    CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES = 1 << 23,
    CONFIG_OPTION_WRITE_TRUNCATE_NUMBERS = 1 << 24,
    CONFIG_OPTION_WRITE_NO_SAVE_ON_DATA_EQUAL = 1 << 25,
    CONFIG_OPTION_WRITE_ESCAPE_UTF8 = 1 << 26,

} config_option_t;
typedef uint32_t config_option_flags_t;

/*! Config field tag structure 
 *
 *  Using tags on a config value can speed up the linear field search of objects.
 */
struct config_tag_t
{
    int symbol;
};

/*! Config value handle.
 *
 *  This structure is the principale token to use to manipulate config values.
 */
struct config_handle_t
{
    config_t* config{ nullptr };
    config_index_t index{ 0 };
    
    /*! Creates a null config value */
    FOUNDATION_FORCEINLINE config_handle_t(std::nullptr_t = nullptr) noexcept
        : config(nullptr)
        , index((config_index_t)-1)
    {
    }

    /*! Internal usage: Create a config value from internal structs */
    FOUNDATION_FORCEINLINE config_handle_t(config_t* config, config_index_t index) noexcept
        : config(config)
        , index(index)
    {
    }

    FOUNDATION_FORCEINLINE config_handle_t(config_handle_t&& other) noexcept
        : config(other.config)
        , index(other.index)
    {
        other.config = nullptr;
        other.index = (config_index_t)-1;
    }

    FOUNDATION_FORCEINLINE config_handle_t(const config_handle_t& other) noexcept
        : config(other.config)
        , index(other.index)
    {
    }

    FOUNDATION_FORCEINLINE config_handle_t& operator=(config_handle_t&& other) noexcept
    {
        config = other.config;
        index = other.index;
        other.config = nullptr;
        other.index = (config_index_t)-1;
        return *this;
    }

    FOUNDATION_FORCEINLINE config_handle_t& operator=(const config_handle_t& other) noexcept
    {
        config = other.config;
        index = other.index;
        return *this;
    }

    operator config_value_t* () const;
    config_handle_t operator[] (const char* key) const;
    config_handle_t operator[] (config_index_t index) const;
    config_handle_t operator[] (const config_tag_t& tag) const;
    config_handle_t operator[] (const string_const_t& tag) const;

    struct iterator
    {
        config_t* config;
        config_index_t index;

        FOUNDATION_FORCEINLINE bool operator!=(const iterator& other) const
        {
            if (!config || index == (config_index_t)(-1))
                return false;
            return config != other.config || index != other.index;
        }

        FOUNDATION_FORCEINLINE bool operator==(const iterator& other) const
        {
            return !operator!=(other);
        }

        iterator& operator++();

        FOUNDATION_FORCEINLINE config_handle_t operator*() const
        {
            return config_handle_t{ config, index };
        }
    };

    /*! Config object and array value iterator. 
     *
     *  @param index Starting index.
     */
    iterator begin(size_t index = 0) const;

    /*! Config value object end iterator. */
    iterator end() const;

    /*! Returns the object id if any. */
    string_const_t name() const;

    /*! Returns the config value type. */
    config_value_type_t type() const;

    /*! Converts the config value to a boolean value. */
    bool as_boolean(bool default_value = false) const;

    /*! Converts the config value to a number. */
    double as_number(double default_value = __builtin_nan("0")) const;

    /*! Converts the config value to a string, or the JSON string value if anything else than a primitive. */
    string_const_t as_string(const char* default_string = nullptr, size_t default_string_length = 0, const char* fmt = nullptr) const;

    /*! Converts the config value to a integer value if possible. */
    template<typename T = int> FOUNDATION_FORCEINLINE T as_integer(int default_value = 0) const 
    { 
        return (T)math_trunc(as_number((double)default_value)); 
    }

    /*! Converts the config value number to a timestamp. */
    time_t as_time(time_t default_value = 0) const;
};

/*! Returns the default static null value. 
 *
 *  @return Null config value.
 */
config_handle_t config_null();

/*! Allocates a new config value.
 *
 *  @remark The config value must be deallocated using #config_deallocate.
 *
 *  @param type    Config value type.
 *  @param options Config value options.
 *
 *  @return Config value handle.
 */
config_handle_t config_allocate(config_value_type_t type = CONFIG_VALUE_OBJECT, config_option_flags_t options = CONFIG_OPTION_NONE);

/*! Deallocates a config value. 
 *
 *  @param root Config value handle.
 */
void config_deallocate(config_handle_t& root);

/*! Preload the config value field tag for quicker subsequent accesses. 
 *
 *  @param root Config value handle.
 *  @param tag  Field tag name.
 *  @param tag_length Field tag name length.
 *
 *  @return Field tag handle.
 */
config_tag_t config_tag(const config_handle_t& h, const char* tag, size_t tag_length);

/*! Get initialization options of the config value. 
 *
 *  @param root Config value handle.
 *
 *  @return Config value options.
 */
config_option_flags_t config_get_options(const config_handle_t& root);

/*! Set initialization options of the config value. 
 *
 *  @param root    Config value handle.
 *  @param options Config value options.
 *
 *  @return Config value options.
 */
config_option_flags_t config_set_options(const config_handle_t& root, config_option_flags_t options);

/*! Returns the child element value at the given index. 
 *
 *  @param array_or_obj Config value handle.
 *  @param index        Child index.
 *
 *  @return Child config value handle. NIL if not found or invalid.
 */
config_handle_t config_element_at(const config_handle_t& array_or_obj, size_t index);

/*! Returns the child element value with the given key. 
 *
 *  @param obj Config value handle.
 *  @param key Child field key tag (obtained with #config_tag).
 *
 *  @return Child config value handle. NIL if not found or invalid.
 */
config_handle_t config_find(const config_handle_t& obj, const config_tag_t& tag);

/*! Returns the child element value with the given key. 
 *
 *  @param obj        Config value handle.
 *  @param key        Child field key.
 *  @param key_length Child field key length.
 *
 *  @return Child config value handle. NIL if not found or invalid.
 */
config_handle_t config_find(const config_handle_t& obj, const char* key, size_t key_length);

/*! Returns the config value raw pointer value. 
 *
 *  @remark This function is unsafe and should only be used when the config value type is known.
 *
 *  @param value Config value handle.
 *
 *  @return Config value raw pointer value.
 */
const void* config_value_as_pointer_unsafe(const config_handle_t& value);

/*! Returns the config value boolean value. 
 *
 *  @param boolean_value Config value handle.
 *  @param default_value Default value if the config value is not a boolean.
 *
 *  @return Config value boolean value.
 */
bool config_value_as_boolean(const config_handle_t& boolean_value, bool default_value = false);

/*! Returns the config value number value. 
 *
 *  @param number_value Config value handle.
 *  @param default_value Default value if the config value is not a number.
 *
 *  @return Config value number value.
 */
double config_value_as_number(const config_handle_t& number_value, double default_value = __builtin_nan("0"));

/*! Returns the config value string value. 
 *
 *  @param string_value Config value handle.
 *  @param fmt          Optional format string to format the string value if 
 *                      the config value is not a string or the default value 
 *                      if the config value is a string.
 *
 *  @return Config value string value.
 */
string_const_t config_value_as_string(const config_handle_t& string_value, const char* fmt = nullptr);

/*! Returns the config value holding type (object, array, string, number, boolean, null). 
 *
 *  @param v Config value handle.
 *
 *  @return Config value type.
 */
config_value_type_t config_value_type(const config_handle_t& v);

/*! Returns the config value holding type (object, array, string, number, boolean, null). 
 *
 *  @param v Config value handle.
 *
 *  @return Config value type.
 */
FOUNDATION_FORCEINLINE config_value_type_t config_type(const config_handle_t& v)
{
    return config_value_type(v);
}

/*! Add a new child element to the config value.
 *
 *  If the config value is not an object, it will be converted to an object.
 *  The added child element will be undefined initially. You can use #config_set
 *  if you want to add and set a child element in one call.
 *
 *  @param v          Config value handle.
 *  @param key        Child field name
 *  @param key_length Child field name length
 *
 *  @return Child config value handle.
 */
config_handle_t config_add(const config_handle_t& v, const char* key, size_t key_length);

/*! Add a new child element to the config value.
 *
 *  @param v   Config value handle.
 *  @param key Child field name
 *
 *  @return Child config value handle.
 */
template<size_t N>
FOUNDATION_FORCEINLINE config_handle_t config_add(const config_handle_t& v, const char(&key)[N]) 
{
    return config_add(v, key, N - 1);
}

/*! Remove a child element from the config value. 
 *
 *  @param obj_or_array Config value handle.
 *  @param v            Child config value handle.
 *
 *  @return True if the child config value was removed.
 */
bool config_remove(const config_handle_t& obj_or_array, const config_handle_t& v);

/*! Remove a child element from the config value. 
 *
 *  @param obj_or_array Config value handle.
 *  @param key          Child field key name.
 *  @param key_length   Child field key name length.
 *
 *  @return True if the child config value was removed.
 */
bool config_remove(const config_handle_t& v, const char* key, size_t key_length);

/*! Remove a child element. 
 *
 *  @param obj_or_array Config value handle.
 *  @param key          Child field key name.
 *
 *  @return True if the child config value was removed.
 */
template<size_t N>
FOUNDATION_FORCEINLINE bool config_remove(const config_handle_t& v, const char(&key)[N])
{
    return config_remove(v, key, N - 1);
}

/*! Get a child element or creates it if it does not exist. 
 *
 *  @param v          Config value handle.
 *  @param tag        Child field key tag (obtained with #config_tag).
 *
 *  @return Child config value handle.
 */
config_handle_t config_get_or_create(const config_handle_t& v, const config_tag_t& tag);

/*! Get a child element or creates it if it does not exist. 
 *
 *  @param v          Config value handle.
 *  @param key        Child field key name.
 *  @param key_length Child field key name length.
 *
 *  @return Child config value handle.
 */
config_handle_t config_get_or_create(const config_handle_t& v, const char* key, size_t key_length);

/*! Sets or change the config value to a boolean value. 
 *
 *  @param v     Config value handle.
 *  @param value Boolean value.
 *
 *  @return Config value handle.
 */
config_handle_t config_set(const config_handle_t& v, bool value);

/*! Sets or change the config value to a number value. 
 *
 *  @param v     Config value handle.
 *  @param value Number value.
 *
 *  @return Config value handle.
 */
config_handle_t config_set(const config_handle_t& v, double number);

/*! Sets or change the config value to a raw pointer value.
 *
 *  @remark This function is unsafe and should only be used when the config value type is known.
 *
 *  @param v            Config value handle.
 *  @param string_value String value.
 *
 *  @return Config value handle.
 */
config_handle_t config_set(const config_handle_t& v, const void* data);

/*! Sets or change the config value to a string value. 
 *
 *  @param v            Config value handle.
 *  @param string_value String value.
 *
 *  @return Config value handle.
 */
config_handle_t config_set(const config_handle_t& v, const char* string_value, size_t string_length);

/*! Sets or change the config value to a string value. 
 *
 *  @param v            Config value handle.
 *  @param string_value String value.
 *
 *  @return Config value handle.
 */
template<size_t N>
FOUNDATION_FORCEINLINE config_handle_t config_set(const config_handle_t& v, const char(&string_value)[N]) 
{
    return config_set(v, string_value, N - 1);
}

/*! Sets or change the child config value to a boolean value.
 *
 *  @param v            Config value handle.
 *  @param key          Child field key name.
 *  @param key_length   Child field key name length.
 *  @param value        Boolean value.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set(const config_handle_t& v, const char* key, size_t key_length, bool value);

/*! Sets or change the child config value to a number value.
 *
 *  @param v            Config value handle.
 *  @param key          Child field key name.
 *  @param key_length   Child field key name length.
 *  @param value        Number value.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set(const config_handle_t& v, const char* key, size_t key_length, double number);

/*! Sets or change the child config value to a raw pointer value.
 *
 *  @remark This function is unsafe and should only be used when the config value type is known.
 *
 *  @param v            Config value handle.
 *  @param key          Child field key name.
 *  @param key_length   Child field key name length.
 *  @param data         Raw pointer value.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set(const config_handle_t& v, const char* key, size_t key_length, const void* data);

/*! Sets or change the child config value to a string value.
 *
 *  @param v            Config value handle.
 *  @param key          Child field key name.
 *  @param key_length   Child field key name length.
 *  @param str          String value.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set(const config_handle_t& v, const char* key, size_t key_length, string_const_t str);

/*! Sets or change the child config value to a string value.
 *
 *  @param v            Config value handle.
 *  @param key          Child field key name.
 *  @param key_length   Child field key name length.
 *  @param str          String value.
 *  @param length       String value length.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set(const config_handle_t& v, const char* key, size_t key_length, const char* str, size_t length);

/*! Sets or change the child config value to a string value.
 *
 *  @param v             Config value handle.
 *  @param key           Child field key name.
 *  @param key_length    Child field key name length.
 *  @param string_value  String value.
 *  @param string_length String value length.
 *
 *  @return Modified config value handle.
 */
FOUNDATION_FORCEINLINE config_handle_t config_set_string(const config_handle_t& obj, const char* key, size_t key_length, const char* string_value, size_t string_length)
{
    return config_set(obj, key, key_length, string_value, string_length);
}

/*! Sets or change the child config value to a boolean value. 
 *
 *  @param v     Config value handle.
 *  @param key   Child field key name.
 *  @param value Boolean value.
 *
 *  @return Modified config value handle.
 */
template <size_t N> FOUNDATION_FORCEINLINE config_handle_t config_set(const config_handle_t& v, const char(&key)[N], bool value) 
{ 
    return config_set(v, key, N - 1, value); 
}

/*! Sets or change the child config value to an integer value. 
 *
 *  @remark The integer value is converted to a double.
 *
 *  @param v     Config value handle.
 *  @param key   Child field key name.
 *  @param value Number value.
 *
 *  @return Modified config value handle.
 */
template <size_t N> FOUNDATION_FORCEINLINE config_handle_t config_set(const config_handle_t& v, const char(&key)[N], int32_t number) 
{ 
    return config_set(v, key, N -1, (double)number); 
}

/*! Sets or change the child config value to an integer value. 
 *
 *  @remark The integer value is converted to a double.
 *
 *  @param v     Config value handle.
 *  @param key   Child field key name.
 *  @param value Number value.
 *
 *  @return Modified config value handle.
 */
template <size_t N> FOUNDATION_FORCEINLINE config_handle_t config_set(const config_handle_t& v, const char(&key)[N], int64_t number) 
{ 
    return config_set(v, key, N -1, (double)number); 
}

/*! Sets or change the child config value to a number value. 
 *
 *  @param v     Config value handle.
 *  @param key   Child field key name.
 *  @param value Number value.
 *
 *  @return Modified config value handle.
 */
template <size_t N> FOUNDATION_FORCEINLINE config_handle_t config_set(const config_handle_t& v, const char(&key)[N], double number) 
{ 
    return config_set(v, key, N - 1, number); 
}

#if FOUNDATION_PLATFORM_MACOS
/*! Sets or change the child config value to a time number value.
 *
 *  @param v     Config value handle.
 *  @param key   Child field key name.
 *  @param time Time value converted to double
 *
 *  @return Modified config value handle.
 */
template <size_t N> FOUNDATION_FORCEINLINE config_handle_t config_set(const config_handle_t& v, const char(&key)[N], time_t time)
{
    return config_set(v, key, N - 1, (double)time);
}
#endif

/*! Sets or change the child config value to a number value. 
 *
 *  @param v     Config value handle.
 *  @param key   Child field key name.
 *  @param value Number value.
 *
 *  @return Modified config value handle.
 */
template <size_t N> FOUNDATION_FORCEINLINE config_handle_t config_set(const config_handle_t& v, const char(&key)[N], float number) 
{ 
    return config_set(v, key, N - 1, (double)number); 
}

/*! Sets or change the child config value to a raw pointer value. 
 *
 *  @remark This function is unsafe and should only be used when the config value type is known.
 *
 *  @param v     Config value handle.
 *  @param key   Child field key name.
 *  @param value Raw pointer value.
 *
 *  @return Modified config value handle.
 */
template <size_t N> FOUNDATION_FORCEINLINE config_handle_t config_set(const config_handle_t& v, const char(&key)[N], const void* data) 
{ 
    return config_set(v, key, N - 1, data); 
}

/*! Sets or change the child config value to a string value. 
 *
 *  @template String String type. Must be convertible to #string_const_t or #string_t 
 *                   which is compatible with the #STRING_ARGS macro.
 *
 *  @param v            Config value handle.
 *  @param key          Child field key name.
 *  @param string_value String value.
 *
 *  @return Modified config value handle.
 */
template <size_t N, class String> 
FOUNDATION_FORCEINLINE config_handle_t config_set(const config_handle_t& v, const char(&key)[N], const String& str)
{ 
    return config_set(v, key, N - 1, STRING_ARGS(str)); 
}

/*! Sets or change the child config value to a string value. 
 *
 *  @param v      Config value handle.
 *  @param key    Child field key name.
 *  @param str    String value.
 *  @param length String value length.
 *
 *  @return Modified config value handle.
 */
template <size_t N> FOUNDATION_FORCEINLINE config_handle_t config_set(const config_handle_t& v, const char(&key)[N], const char* str, size_t length) 
{
    return config_set(v, key, N - 1, str, length); 
}

/*! Sets or change the child config value to a boolean value using a preloaded tag handle.
 *
 *  @param v             Config value handle.
 *  @param tag           Child field tag.
 *  @param value         Boolean value.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set(const config_handle_t& v, const config_tag_t& tag, bool value);

/*! Sets or change the child config value to an integer value using a preloaded tag handle.
 *
 *  @remark The integer value is converted to a double.
 *
 *  @param v             Config value handle.
 *  @param tag           Child field tag.
 *  @param value         Number value.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set(const config_handle_t& v, const config_tag_t& tag, double number);

/*! Sets or change the child config value to a raw pointer value using a preloaded tag handle.
 *
 *  @remark This function is unsafe and should only be used when the config value type is known.
 *
 *  @param v             Config value handle.
 *  @param tag           Child field tag.
 *  @param data          Raw pointer value.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set(const config_handle_t& v, const config_tag_t& tag, const void* data);

/*! Sets or change the child config value to a string value using a preloaded tag handle.
 *
 *  @param v             Config value handle.
 *  @param tag           Child field tag.
 *  @param str           String value.
 *  @param length        String value length.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set(const config_handle_t& v, const config_tag_t& tag, const char* str, size_t length);

/*! Creates or change the child element to an object value. 
 *
 *  This function provide the main way to create sub objects.
 *
 *  @param v          Config value handle.
 *  @param key        Child field key name.
 *  @param key_length Length of key name.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set_object(const config_handle_t& v, const char* key, size_t key_length);

/*! Creates or change the child element to an object value.
 *
 *  @param v   Config value handle.
 *  @param key Child field key name.
 *
 *  @return Modified config value handle.
 */
template<size_t N>
FOUNDATION_FORCEINLINE config_handle_t config_set_object(const config_handle_t& v, const char(&key)[N])
{
        return config_set_object(v, key, N - 1);
}

/*! Creates or change the child element to an array value. 
 *
 *  This function provide the main way to create sub arrays.
 *
 *  @param v          Config value handle.
 *  @param key        Child field key name.
 *  @param key_length Length of key name.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set_array(const config_handle_t& v, const char* key, size_t key_length);

/*! Creates or change the child element to an array value.
 *
 *  @param v   Config value handle.
 *  @param key Child field key name.
 *
 *  @return Modified config value handle.
 */
template<size_t N>
FOUNDATION_FORCEINLINE config_handle_t config_set_array(const config_handle_t& v, const char(&key)[N])
{
    return config_set_array(v, key, N - 1);
}

/*! Nullify a child element. 
 *
 *  This function provide the main way to nullify sub elements.
 *
 *  @param v          Config value handle.
 *  @param key        Child field key name.
 *  @param key_length Length of key name.
 *
 *  @return Modified config value handle.
 */
config_handle_t config_set_null(const config_handle_t& v, const char* key, size_t key_length);

/*! Nullify the config value. 
 *
 *  This function provide the main way to nullify a config value.
 *
 *  @param v Config value handle.
 */
void config_set_null(const config_handle_t& v);

/*! Returns the value name/id if any. 
 *
 *  @remark This function is mainly useful on child elements of an object.
 *
 *  @param v Config value handle.
 *
 *  @return Value name/id.
 */
string_const_t config_name(const config_handle_t& obj);

/*! Returns the element count of an array or object. 
 *
 *  @remark This function is mainly useful on child elements of an array or object.
 *
 *  @param v Config value handle.
 *
 *  @return Element count, 0 if the value is not an array or object.
 */
size_t config_size(const config_handle_t& obj);

/*! Remove all child elements of an array
 *
 *  @remark This function is mainly useful on child elements of an array or object.
 *
 *  @param v Config value handle.
 *
 *  @return The cleared array value handle or null if the value is not an array.
 */
config_handle_t config_array_clear(const config_handle_t& v);

/*! Push a new element to an array with and initial type and a default value.
 *
 *  @remark The new element is "empty" and must be set afterward using config_set().
 *
 *  @param v           Config value handle.
 *  @param type        Type of the new element.
 *  @param name        Name of the new element. Note that usually setting a name of a new array element is not needed or used.
 *  @param name_length Length of name.
 *
 *  @return Newly added element handle.
 */
config_handle_t config_array_push(const config_handle_t& v, config_value_type_t type = CONFIG_VALUE_NIL, const char* name = nullptr, size_t name_length = 0);

/*! Push a new element to an array with a boolean value.
 *
 *  @param v           Config value handle.
 *  @param value       Boolean value.
 *
 *  @return Newly added boolean array element handle.
 */
config_handle_t config_array_push(const config_handle_t& v, bool value);

/*! Push a new element to an array with a number value.
 *
 *  @param v           Config value handle.
 *  @param value       Number value.
 *
 *  @return Newly added number array element handle.
 */
config_handle_t config_array_push(const config_handle_t& v, double number);

/*! Push a new element to an array with a string value.
 *
 *  @param v           Config value handle.
 *  @param value       String value.
 *  @param length      String value length.
 *
 *  @return Newly added string array element handle.
 */
config_handle_t config_array_push(const config_handle_t& v, const char* value, size_t value_length);

/*! Insert a new element to an array with and initial type and a default value.
 *
 *  @remark The new element is "empty" and must be set afterward using config_set().
 *
 *  @param v           Config value handle.
 *  @param index       Index where to insert the new element.
 *  @param type        Type of the new element.
 *  @param name        Name of the new element. Note that usually setting a name of a new array element is not needed or used.
 *  @param name_length Length of name.
 *
 *  @return Newly added element handle.
 */
config_handle_t config_array_insert(const config_handle_t& v, size_t index, config_value_type_t type = CONFIG_VALUE_NIL, const char* name = nullptr, size_t name_length = 0);

/*! Insert a new element to an array with a boolean value.
 *
 *  @param v           Config value handle.
 *  @param index       Index where to insert the new element.
 *  @param value       Boolean value.
 *
 *  @return Newly added boolean array element handle.
 */
config_handle_t config_array_insert(const config_handle_t& v, size_t index, bool value);

/*! Insert a new element to an array with a number value.
 *
 *  @param v           Config value handle.
 *  @param index       Index where to insert the new element.
 *  @param value       Number value.
 *
 *  @return Newly added number array element handle.
 */
config_handle_t config_array_insert(const config_handle_t& v, size_t index, double number);

/*! Insert a new element to an array with a string value.
 *
 *  @param v           Config value handle.
 *  @param index       Index where to insert the new element.
 *  @param value       String value.
 *  @param length      String value length.
 *
 *  @return Newly added string array element handle.
 */
config_handle_t config_array_insert(const config_handle_t& v, size_t index, const char* value, size_t value_length);

/*! Pop the last element of an array.
 *
 *  @param v Config value handle.
 *
 *  @return True if the array was not empty.
 */
bool config_array_pop(const config_handle_t& v);

/*! Sorts the elements of an array using a custom sorting function.
 *
 *  @param array_handle Config value handle.
 *  @param sort_fn      Sorting function.
 */
void config_array_sort(const config_handle_t& array_handle, const function<bool(const config_handle_t& a, const config_handle_t& b)>& sort_fn);

/*! Compacts the config string table to save memory.
 *
 *  @param v Config value handle.
 */
void config_pack(const config_handle_t& value);

/*! Clears and empty a config value.
 *
 *  @param v Config value handle.
 */
void config_clear(const config_handle_t& value);

/*! Checks if the config value is valid, meaning that is it actually storing a value and not just a placeholder.
 *
 *  @param v Config value handle.
 *
 *  @return True if the config value is valid.
 */
bool config_is_valid(const config_handle_t& v, const char* key = nullptr, size_t key_length = 0);

/*! Checks if the child element with the field name exists.
 *
 *  @param v           Config value handle.
 *  @param key         Field name.
 *  @param key_length  Field name length.
 *
 *  @return True if the child element exists.
 */
bool config_exists(const config_handle_t& v, const char* key, size_t key_length);

/*! Checks if the config value is null.
 *
 *  @param v Config value handle.
 *
 *  @return True if the config value is null.
 */
bool config_is_null(const config_handle_t& v, const char* key = nullptr, size_t key_length = 0);

/*! Checks if the value was never defined. Setting a value to null will not make it undefined.
 *
 *  @param v Config value handle.
 *
 *  @return True if the value was never defined.
 */
bool config_is_undefined(const config_handle_t& v, const char* key = nullptr, size_t key_length = 0);

/*! Writes the config content to a file. The file will be overwritten if it already exists.
 *
 *  @param file_path        File path.
 *  @param data             Config value handle.
 *  @param write_json_flags Flags to control the JSON output.
 *
 *  @return True if the file was written successfully.
 */
bool config_write_file(
    string_const_t file_path, 
    config_handle_t data, 
    config_option_flags_t write_json_flags = CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS | CONFIG_OPTION_WRITE_SKIP_NULL);

/*! Writes the config content to a file. The file will be overwritten if it already exists.
 *
 *  @param file_path        File path.
 *  @param file_path_length File path length.
 *  @param data             Config value handle.
 *  @param write_json_flags Flags to control the JSON output.
 *
 *  @return True if the file was written successfully.
 */
FOUNDATION_FORCEINLINE bool config_write_file(
    const char* file_path, size_t file_path_length, 
    config_handle_t data, config_option_flags_t write_json_flags = CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS | CONFIG_OPTION_WRITE_SKIP_NULL)
{
    return config_write_file(string_const(file_path, file_path_length), data, write_json_flags);
}

/*! Writes the config content to a file. The file will be overwritten if it already exists.
 *
 *  @param file_path        File path.
 *  @param write_callback   Callback to write the config data before it is written to the file.
 *  @param value_type       Config value type.
 *  @param write_json_flags Flags to control the JSON output.
 *
 *  @return True if the file was written successfully.
 */
bool config_write_file(
    string_const_t file_path,
    function<bool(const config_handle_t& data)> write_callback,
    config_value_type_t value_type = CONFIG_VALUE_OBJECT,
    config_option_flags_t write_json_flags = CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS | CONFIG_OPTION_WRITE_SKIP_NULL);

/*! Parse a file on disk and creates a new config value.
 *
 *  @remark The config value needs to be deallocated with #config_deallocate by the caller.
 *
 *  @param file_path        File path.
 *  @param file_path_length File path length.
 *  @param options          Options to control the parsing.
 *
 *  @return Config value handle.
 */
config_handle_t config_parse_file(const char* file_path, size_t file_path_length, config_option_flags_t options = CONFIG_OPTION_NONE);

/*! Parse a string to a config value.
 *
 *  @remark The config value needs to be deallocated with #config_deallocate by the caller.
 *
 *  @param json         JSON string.
 *  @param json_length  JSON string length.
 *  @param options      Options to control the parsing.
 *
 *  @return Config value handle.
 */
config_handle_t config_parse(const char* json, size_t json_length, config_option_flags_t options = CONFIG_OPTION_NONE);

/*! Returns the JSON or SJSON string content of a config value.
 *
 *  @remark The string content must be deallocated with #config_sjson_deallocate by the caller.
 *
 *  @param value Config value handle.
 *  @param options Options to control string generation.
 *
 *  @return String content.
 */
config_sjson_const_t config_sjson(const config_handle_t& value, config_option_flags_t options = CONFIG_OPTION_NONE);

/*! Maps the JSON string content obtained by #config_sjson to a string object.
 *
 *  @param sjson String content.
 *
 *  @return String object.
 */
string_const_t config_sjson_to_string(config_sjson_const_t sjson);

/*! Deallocates the string content obtained by #config_sjson.
 *
 *  @param sjson String content.
 */
void config_sjson_deallocate(config_sjson_const_t sjson);
