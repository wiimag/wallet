/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "config.h"

#include <framework/common.h>
#include <framework/scoped_string.h>
#include <framework/string_table.h>
#include <framework/string.h>

#include <foundation/fs.h>
#include <foundation/array.h>
#include <foundation/stream.h>

#include <stdexcept>
#include <algorithm>

struct config_value_t;

static config_handle_t NIL { nullptr, (config_index_t)(-1) };

struct config_t
{
    config_option_flags_t options;
    config_value_t* values;
    string_table_t* st;
};

struct config_value_t
{
    string_table_symbol_t name;
    config_value_type_t type;
    config_index_t index;
    config_index_t child;
    config_index_t sibling;
    
    // Primitive data
    union {
        // Boolean, Number
        double number;

        // String
        string_table_symbol_t str;

        // Object, Array
        uint32_t child_count;

        // Used for volatile raw pointers
        const void* data;
    };
};

config_handle_t::operator config_value_t* () const
{
    if (config == nullptr || index == -1)
        return nullptr;
    if (index >= array_size(config->values))
        return nullptr;
    return &(config->values[index]);
}

config_handle_t config_handle_t::operator[] (config_index_t at) const
{
    return config_element_at(*this, at);
}

config_handle_t::iterator config_handle_t::begin(size_t at /*= 0*/) const
{
    config_value_t* cv = *this;
    if (!cv)
        return iterator { nullptr, 0 };
    if (index == 0)
        return iterator{ config, cv->child };

    auto element = config_element_at(*this, at);
    return iterator{ config, element.index };
}

config_handle_t::iterator& config_handle_t::iterator::operator++()
{
    if (config)
        index = config->values[index].sibling;
    return *this;
}

config_handle_t::iterator config_handle_t::end() const
{
    return iterator{ config, 0 };
}

bool config_handle_t::as_boolean(bool default_value) const
{
    return config_value_as_boolean(*this, default_value);
}

string_const_t config_handle_t::name() const
{
    return config_name(*this);
}

config_value_type_t config_handle_t::type() const
{
    return config_value_type(*this);
}

double config_handle_t::as_number(double default_value /*= NAN*/) const
{
    return config_value_as_number(*this, default_value);
}

string_const_t config_handle_t::as_string(const char* default_string /*= nullptr*/, size_t default_string_length /*= 0*/, const char* fmt /*= nullptr*/) const
{
    string_const_t string_data = config_value_as_string(*this, fmt);
    if (!string_is_null(string_data))
        return string_data;
    return string_const(default_string, default_string_length);
}

time_t config_handle_t::as_time(time_t default_value /*= 0*/) const
{
    auto type = config_value_type(*this);
    if (type == CONFIG_VALUE_STRING)
    {
        string_const_t string_data = config_value_as_string(*this);
        return string_to_date(STRING_ARGS(string_data));
    }
    else if (type == CONFIG_VALUE_NUMBER)
        return (time_t)config_value_as_number(*this);

    return default_value;
}

FOUNDATION_STATIC string_table_symbol_t config_add_symbol(config_t* root, const char* s, size_t length)
{
    if (root == nullptr || s == nullptr || length == 0)
        return STRING_TABLE_NULL_SYMBOL;
    string_table_symbol_t symbol = STRING_TABLE_NULL_SYMBOL;
    while ((symbol = string_table_to_symbol(root->st, s, length)) == STRING_TABLE_FULL)
        string_table_grow(&root->st);
    return symbol;
}

FOUNDATION_STATIC void config_value_initialize(config_t* config, config_value_t& value, config_value_type_t type, unsigned int index, string_table_symbol_t name_symbol)
{
    value.name = name_symbol;
    value.index = index;
    value.type = type;
    value.child = 0;
    value.sibling = 0;
    value.number = 0;
    value.child_count = 0;
    value.data = nullptr;
}

config_handle_t config_null()
{
    return NIL;
}

config_handle_t config_allocate(config_value_type_t type /*= CONFIG_VALUE_OBJECT*/, config_option_flags_t options /*= CONFIG_OPTION_NONE*/)
{
    config_t* config = (config_t*)memory_allocate(0, sizeof(config_t), 0, (options & CONFIG_OPTION_ALLOCATE_TEMPORARY) ? MEMORY_PERSISTENT : MEMORY_TEMPORARY);
    config->options = options;
    config->st = string_table_allocate(256, 10);
    config->values = nullptr;
    array_resize(config->values, 1);

    //config->guard = mutex_allocate(STRING_CONST("CV"));
    
    string_table_symbol_t root_symbol = config_add_symbol(config, STRING_CONST("<root>"));
    config_value_initialize(config, config->values[0], type, 0, root_symbol);
    return config_handle_t{ config, 0 };
}

void config_deallocate(config_handle_t& root)
{
    if (root.config == nullptr)
        return;
    config_t* config = root.config;
    string_table_deallocate(config->st);
    array_deallocate(config->values);
    memory_deallocate(config);

    root.config = nullptr;
    root.index = 0;
}

config_tag_t config_tag(const config_handle_t& h, const char* tag, size_t tag_length)
{
    return config_tag_t { config_add_symbol(h.config, tag, tag_length) };
}

config_option_flags_t config_get_options(const config_handle_t& root)
{
    if (!root || !root.config)
        return CONFIG_OPTION_NONE;

    return root.config->options;
}

config_option_flags_t config_set_options(const config_handle_t& root, config_option_flags_t options)
{
    if (!root || !root.config)
        return CONFIG_OPTION_NONE;

    config_option_flags_t old_options = root.config->options;
    root.config->options = options;
    return old_options;
}

config_handle_t config_element_at(const config_handle_t& h, size_t index)
{
    if (h.config == nullptr)
        return NIL;

    config_value_t& v = h.config->values[h.index];
    if(v.child == 0)
        return NIL; // no child elements

    config_value_t* p = &h.config->values[v.child];
    while (index-- > 0 && p)
    {
        if (p->sibling == 0)
            return NIL; // invalid chain
        p = &h.config->values[p->sibling];
    }

    return config_handle_t { h.config, p->index };
}

FOUNDATION_STATIC config_handle_t config_find(const config_handle_t& obj, string_table_symbol_t symbol)
{
    const config_value_t* v = obj;
    if (v == nullptr || symbol <= 0)
        return NIL;
    
    const config_value_t* values = obj.config->values;
    const config_value_t* p = &values[v->child];
    while (p && p->name != symbol)
    {
        if (p->sibling == 0)
            return NIL; // invalid chain
        p = &values[p->sibling];
    }

    return config_handle_t{ obj.config, p->index };
}

config_handle_t config_find(const config_handle_t& obj, const config_tag_t& tag)
{
    return config_find(obj, tag.symbol);
}

config_handle_t config_find(const config_handle_t& h, const char* key, size_t key_length)
{
    if (h.config == nullptr || key == nullptr || key_length == 0)
        return NIL;

    const config_value_t& v = h.config->values[h.index];
    if (v.child == 0)
        return NIL; // no child elements

    if (v.type == CONFIG_VALUE_OBJECT)
    {
        string_table_symbol_t key_symbol = string_table_find_symbol(h.config->st, key, key_length);
        if (key_symbol > 0)
            return config_find(h, key_symbol);
    }
    
    return NIL;
}

config_handle_t config_handle_t::operator[] (const char* key) const
{
    return config_find(*this, key, string_length(key));
}

config_handle_t config_handle_t::operator[] (const config_tag_t& tag) const
{
    return config_find(*this, tag.symbol);
}

config_handle_t config_handle_t::operator[] (const string_const_t& tag) const
{
    return config_find(*this, STRING_ARGS(tag));
}

const void* config_value_as_pointer_unsafe(const config_handle_t& value)
{
    const config_value_t* cv = value;
    FOUNDATION_ASSERT_MSG(cv, "Config value is undefined and it is unsafe to fetch its raw value.");
    if (cv && cv->type == CONFIG_VALUE_RAW_DATA)
        return cv->data;
    return nullptr;
}

bool config_value_as_boolean(const config_handle_t& h, bool default_value /*= false*/)
{
    const config_value_t* cv = h;
    if (cv == nullptr)
        return default_value;

    if (cv->type == CONFIG_VALUE_NIL)
        return false;

    if (cv->type == CONFIG_VALUE_TRUE)
        return true;

    if (cv->type == CONFIG_VALUE_FALSE)
        return false;

    if (cv->type == CONFIG_VALUE_NUMBER)
        return !math_real_is_zero(cv->number);

    if (cv->type == CONFIG_VALUE_RAW_DATA)
        return cv->data;

    if (cv->type == CONFIG_VALUE_ARRAY || cv->type == CONFIG_VALUE_OBJECT)
        return cv->child > 0 && cv->child_count > 0;

    if (cv->type == CONFIG_VALUE_STRING)
    {
        string_const_t str = string_table_to_string_const(h.config->st, cv->str);
        if (string_equal_nocase(STRING_ARGS(str), STRING_CONST("true")))
            return true;
        if (string_equal_nocase(STRING_ARGS(str), STRING_CONST("false")))
            return false;
    }

    return default_value;
}

double config_value_as_number(const config_handle_t& h, double default_value /*= NAN*/)
{
    const config_value_t* cv = h;

    if (cv == nullptr)
        return default_value;

    if (cv->type == CONFIG_VALUE_NUMBER)
        return cv->number;

    if (cv->type == CONFIG_VALUE_TRUE)
        return 1.0;

    if (cv->type == CONFIG_VALUE_NIL || cv->type == CONFIG_VALUE_FALSE)
        return 0;

    if (cv->type == CONFIG_VALUE_RAW_DATA)
        return (double)(uint64_t)cv->data;

    if (cv->type == CONFIG_VALUE_ARRAY)
        return (double)cv->child_count;

    if (cv->type == CONFIG_VALUE_STRING)
    {
        string_const_t number_string = string_table_to_string_const(h.config->st, cv->str);
        return string_to_real(STRING_ARGS(number_string));
    }

    return default_value;
}

string_const_t config_value_as_string(const config_handle_t& h, const char* fmt)
{
    if (config_is_null(h))
        return string_null();

    const config_value_t& v = h.config->values[h.index];
    if (v.type == CONFIG_VALUE_STRING)
        return string_table_to_string_const(h.config->st, v.str);

    if (v.type == CONFIG_VALUE_NUMBER)
    {
        if (math_real_is_nan(v.number))
            return CTEXT("null");

        if (fmt != nullptr)
            return string_to_const(string_format(SHARED_BUFFER(64), fmt, string_length(fmt), v.number));
            
        if (h.config->options & CONFIG_OPTION_WRITE_TRUNCATE_NUMBERS)
        {
            if (v.number < 0.1)
                return string_to_const(string_format(SHARED_BUFFER(64), STRING_CONST("%.4lf"), v.number));

            if (v.number < 1.0)
                return string_to_const(string_format(SHARED_BUFFER(64), STRING_CONST("%.3lf"), v.number));
            
            return string_to_const(string_format(SHARED_BUFFER(64), STRING_CONST("%.2lf"), v.number));
        }
        
        return string_from_real_static(v.number, 0, 0, 0);
    }

    if (v.type == CONFIG_VALUE_TRUE)
        return string_const(STRING_CONST("true"));

    if (v.type == CONFIG_VALUE_FALSE)
        return string_const(STRING_CONST("false"));

    if (v.type == CONFIG_VALUE_RAW_DATA)
    {
        double n = v.type == CONFIG_VALUE_NUMBER ? v.number : (double)(uint64_t)v.data;
        string_t data_string = string_format(SHARED_BUFFER(32), STRING_CONST("0x%016x"), v.data);
        return string_const(STRING_ARGS(data_string));
    }

    return string_null();
}

config_value_type_t config_value_type(const config_handle_t& h)
{
    if (h.config == nullptr)
        return CONFIG_VALUE_UNDEFINED;
    const config_value_t& v = h.config->values[h.index];
    return v.type;
}

FOUNDATION_STATIC config_handle_t config_add(const config_handle_t& obj_handle, string_table_symbol_t symbol)
{
    config_value_t* obj = obj_handle;
    if (!obj || symbol == STRING_TABLE_NULL_SYMBOL)
        return NIL;

    if (obj->type != CONFIG_VALUE_OBJECT)
    {
        obj->type = CONFIG_VALUE_OBJECT;
        obj->child = 0;
        obj->child_count = 0;
    }

    //auto lock = scoped_mutex_t(obj_handle.config->guard);
    config_value_t* values = obj_handle.config->values;
    obj_handle.config->values = values = array_push(values, config_value_t{});
    const unsigned int new_field_index = array_size(values) - 1;
    config_value_t& new_field_value = values[new_field_index];
    config_value_initialize(obj_handle.config, new_field_value, CONFIG_VALUE_UNDEFINED, new_field_index, symbol);

    obj = obj_handle;
    obj->child_count++;

    if (obj->child == 0)
    {
        obj->child = new_field_index;
    }
    else if (obj_handle.config->options & CONFIG_OPTION_PRESERVE_INSERTION_ORDER)
    {
        config_value_t* p = &values[obj->child];
        while (p && p->sibling != 0)
            p = &values[p->sibling];

        if (p)
            p->sibling = new_field_index;
    }
    else
    {
        new_field_value.sibling = obj->child;
        obj->child = new_field_index;
    }
    
    return config_handle_t{ obj_handle.config, new_field_index };
}

config_handle_t config_add(const config_handle_t& obj_handle, const char* key, size_t key_length)
{
    string_table_symbol_t name_symbol = config_add_symbol(obj_handle.config, key, key_length);
    return config_add(obj_handle, name_symbol);
}

bool config_remove(const config_handle_t& h, const config_handle_t& to_remove_handle)
{
    config_value_t* cv = h;
    if (!cv || cv->child == 0)
        return false;

    if (to_remove_handle.config == nullptr)
        return false;

    config_value_t* values = h.config->values;
    if (cv->child == to_remove_handle.index)
    {
        cv->child = values[to_remove_handle.index].sibling;
        cv->child_count--;
        return true;
    }

    config_value_t* p = &values[cv->child];
    while (p && p->sibling != 0 && p->sibling != to_remove_handle.index)
    {
        if (p->sibling == 0)
            return false; // invalid chain
        p = &values[p->sibling];
    }

    if (p)
    {
        cv->child_count--;
        p->sibling = values[to_remove_handle.index].sibling;
        return true;
    }

    return false;
}

bool config_remove(const config_handle_t& h, const char* key, size_t key_length)
{
    config_value_t* cv = h;
    if (!cv || cv->child == 0)
        return false;

    config_handle_t to_remove_handle = config_find(h, key, key_length);
    return config_remove(h, to_remove_handle);
}

FOUNDATION_STATIC config_handle_t config_set(const config_handle_t& h, config_value_t* cv, bool value)
{
    if (cv)
    {
        cv->type = value ? CONFIG_VALUE_TRUE : CONFIG_VALUE_FALSE;
        cv->number = value ? 1.0 : 0;
        cv->child = 0;
        return config_handle_t{ h.config, cv->index };
    }

    return h;
}

FOUNDATION_STATIC config_handle_t config_set(const config_handle_t& h, config_value_t* cv, double number)
{
    if (cv)
    {
        cv->type = CONFIG_VALUE_NUMBER;
        cv->number = number;
        cv->child = 0;
        return config_handle_t{ h.config, cv->index };
    }

    return h;
}

FOUNDATION_STATIC config_handle_t config_set(const config_handle_t& h, config_value_t* cv, const void* data)
{
    if (cv)
    {
        cv->type = data == nullptr ? CONFIG_VALUE_NIL : CONFIG_VALUE_RAW_DATA;
        cv->data = data;
        cv->child = 0;
        return config_handle_t{ h.config, cv->index };
    }

    return h;
}

FOUNDATION_STATIC config_handle_t config_set(const config_handle_t& obj_handle, config_value_t* cv, const char* value, size_t value_length)
{
    if (cv)
    {
        cv->type = CONFIG_VALUE_STRING;
        cv->str = config_add_symbol(obj_handle.config, value, value_length);
        cv->child = 0;
        return config_handle_t{ obj_handle.config, cv->index };
    }

    return obj_handle;
}

FOUNDATION_STATIC config_handle_t config_get_or_create(const config_handle_t& h, string_table_symbol_t symbol)
{
    config_handle_t cv = config_find(h, symbol);
    if (!cv)
        cv = config_add(h, symbol);
    return cv;
}

config_handle_t config_set(const config_handle_t& h, const config_tag_t& tag, bool value)
{
    config_value_t* cv = config_get_or_create(h, tag.symbol);
    return config_set(h, cv, value);
}

config_handle_t config_set(const config_handle_t& h, const config_tag_t& tag, double number)
{
    config_value_t* cv = config_get_or_create(h, tag.symbol);
    return config_set(h, cv, number);
}

config_handle_t config_set(const config_handle_t& h, const config_tag_t& tag, const void* data)
{
    config_value_t* cv = config_get_or_create(h, tag.symbol);
    return config_set(h, cv, data);
}

config_handle_t config_set(const config_handle_t& h, const config_tag_t& tag, const char* string_value, size_t string_length)
{
    config_value_t* cv = config_get_or_create(h, tag.symbol);
    return config_set(h, cv, string_value, string_length);
}

config_handle_t config_get_or_create(const config_handle_t& h, const config_tag_t& tag)
{
    return config_get_or_create(h, tag.symbol);
}

config_handle_t config_get_or_create(const config_handle_t& h, const char* key, size_t key_length)
{
    config_handle_t cv = config_find(h, key, key_length);
    if (!cv)
        cv = config_add(h, key, key_length);
    return cv;
}

config_handle_t config_set(const config_handle_t& h, const char* key, size_t key_length, bool value)
{
    config_value_t* cv = key != nullptr ? config_get_or_create(h, key, key_length) : &h.config->values[h.index];
    return config_set(h, cv, value);
}

config_handle_t config_set(const config_handle_t& v, bool value)
{
    return config_set(v, nullptr, 0, value);
}

config_handle_t config_set(const config_handle_t& h, const char* key, size_t key_length, double number)
{
    config_value_t* cv = key != nullptr ? config_get_or_create(h, key, key_length) : h;
    return config_set(h, cv, number);
}

config_handle_t config_set(const config_handle_t& v, double number)
{
    return config_set(v, nullptr, 0, number);
}

FOUNDATION_STATIC void config_set_null(config_value_t* cv)
{
    FOUNDATION_ASSERT(cv);

    cv->type = CONFIG_VALUE_NIL;
    cv->str = 0;
    cv->child = 0;
}

config_handle_t config_set(const config_handle_t& h, const char* key, size_t key_length, const void* data)
{
    config_value_t* cv = key != nullptr ? config_get_or_create(h, key, key_length) : h;
    if (data == nullptr)
    {
        config_set_null(cv);
        return config_handle_t{ h.config, cv->index };
    }

    return config_set(h, cv, data);
}

config_handle_t config_set(const config_handle_t& v, const void* data)
{
    return config_set(v, nullptr, 0, data);
}

config_handle_t config_set(const config_handle_t& v, const char* key, size_t key_length, string_const_t string_value)
{
    return config_set(v, key, key_length, STRING_ARGS(string_value));
}

config_handle_t config_set(const config_handle_t& obj_handle, const char* key, size_t key_length, const char* string_value, size_t string_length)
{
    config_value_t* cv = key != nullptr ? config_get_or_create(obj_handle, key, key_length) : obj_handle;
    return config_set(obj_handle, cv, string_value, string_length);
}

config_handle_t config_set(const config_handle_t& obj_handle, const char* value, size_t value_length)
{
    return config_set(obj_handle, nullptr, 0, value, value_length);
}

config_handle_t config_set_object(const config_handle_t& h, const char* key, size_t key_length)
{
    config_value_t* cv = key != nullptr ? config_get_or_create(h, key, key_length) : h;
    if (!cv)
        return h;

    if (cv->type != CONFIG_VALUE_OBJECT)
    {
        cv->type = CONFIG_VALUE_OBJECT;
        cv->child_count = 0;
        cv->child = 0;
    }

    return config_handle_t{ h.config, cv->index };
}

config_handle_t config_set_array(const config_handle_t& h, const char* key, size_t key_length)
{
    config_value_t* cv = key != nullptr ? config_get_or_create(h, key, key_length) : h;
    if (cv)
    {
        if (cv->type != CONFIG_VALUE_ARRAY)
        {
            cv->type = CONFIG_VALUE_ARRAY;
            cv->child_count = 0;
            cv->child = 0;
        }
        return config_handle_t{ h.config, cv->index };
    }

    return h;
}

void config_set_null(const config_handle_t& handle)
{
    config_value_t* value = handle;
    if (!value)
        return;

    config_set_null(value);
}

config_handle_t config_set_null(const config_handle_t& h, const char* key, size_t key_length)
{
    config_value_t* cv = key != nullptr ? config_get_or_create(h, key, key_length) : h;
    if (!cv)
        return h;
    config_set_null(cv);
    return config_handle_t{ h.config, cv->index };
}

config_handle_t config_array_clear(const config_handle_t& v)
{
    config_value_t* obj = v;
    if (!obj)
        return NIL;

    if (obj->type != CONFIG_VALUE_ARRAY)
        return NIL;
        
    config_value_t* values = v.config->values;
    obj->child = 0;
    obj->child_count = 0;
    
    return v;
}

config_handle_t config_array_insert(const config_handle_t& array_handle, size_t index, config_value_type_t type /*= CONFIG_VALUE_NIL*/, const char* name /*= nullptr*/, size_t name_length /*= 0*/)
{
    config_value_t* obj = array_handle;
    if (!obj)
        return NIL;
        
    if (obj->type == CONFIG_VALUE_UNDEFINED || obj->type == CONFIG_VALUE_NIL)
    {
        obj->type = CONFIG_VALUE_ARRAY;
        obj->child = 0;
        obj->child_count = 0;
    }

    FOUNDATION_ASSERT(obj->type == CONFIG_VALUE_ARRAY);
    if (obj->type != CONFIG_VALUE_ARRAY)
        return NIL;

    config_value_t* values = array_handle.config->values;
    values = array_handle.config->values = array_push(values, config_value_t{});
    const unsigned int new_element_index = array_size(values) - 1;
    config_value_t& new_element = values[new_element_index];
    string_table_symbol_t name_symbol = config_add_symbol(array_handle.config, name, name_length);
    config_value_initialize(array_handle.config, new_element, type, new_element_index, name_symbol);

    config_value_t& arr = *array_handle;
    if (arr.child != 0)
    {
        arr.child_count++;

        if (index == 0)
        {
            new_element.sibling = arr.child;
            arr.child = new_element_index;
        }
        else
        {			
            config_value_t* p = &values[arr.child];
            while (--index > 0 && p && p->sibling != 0)
                p = &values[p->sibling];

            if (p)
            {
                new_element.sibling = p->sibling;
                p->sibling = new_element_index;
            }
        }
    }
    else
    {
        arr.type = CONFIG_VALUE_ARRAY;
        arr.child_count = 1;
        arr.child = new_element_index;
    }

    return config_handle_t{ array_handle.config, new_element_index };
}

config_handle_t config_array_push(const config_handle_t& v, config_value_type_t type /*= CONFIG_VALUE_NIL*/, const char* name /*= nullptr*/, size_t name_length /*= 0*/)
{
    return config_array_insert(v, UINT_MAX, type, name, name_length);
}

config_handle_t config_array_push(const config_handle_t& v, bool value)
{
    return config_set(config_array_push(v), value);
}

config_handle_t config_array_push(const config_handle_t& v, double number)
{
    return config_set(config_array_push(v), number);
}

config_handle_t config_array_push(const config_handle_t& v, const char* value, size_t value_length)
{
    return config_set(config_array_push(v), value, value_length);
}

config_handle_t config_array_insert(const config_handle_t& v, size_t index, bool value)
{
    return config_set(config_array_insert(v, index), value);
}

config_handle_t config_array_insert(const config_handle_t& v, size_t index, double number)
{
    return config_set(config_array_insert(v, index), number);
}

config_handle_t config_array_insert(const config_handle_t& v, size_t index, const char* value, size_t value_length)
{
    return config_set(config_array_insert(v, index), value, value_length);
}

string_const_t config_name(const config_handle_t& obj)
{
    config_value_t* cv = obj;
    if (!cv)
        return string_const_t{ nullptr, 0 };

    return string_table_to_string_const(obj.config->st, cv->name);
}

size_t config_size(const config_handle_t& obj)
{
    config_value_t* cv = obj;
    if (cv)
        return cv->child_count;
    return 0;
}

bool config_array_pop(const config_handle_t& array_handle)
{
    config_value_t* arr = array_handle;
    if (!arr)
        return false;

    // Find before last element
    if (arr->child == 0)
        return false;

    config_value_t* values = array_handle.config->values;
    config_value_t* p = &values[arr->child];

    if (p && p->sibling == 0)
    {
        // Mark item as deleted
        p->index = -1;

        arr->child = 0;
        arr->child_count--;
        return true;
    }

    config_value_t* next = &values[p->sibling];

    while (next && next->sibling != 0)
    {
        p = next;
        next = &values[p->sibling];
    }	

    if (p)
    {
        // Mark item as deleted
        if (next)
            next->index = -1;

        p->sibling = 0;
        arr->child_count--;
        return true;
    }

    return false;
}

void config_array_sort(const config_handle_t& array_handle, const function<bool(const config_handle_t& a, const config_handle_t& b)>& sort_fn)
{
    config_value_t* arr = array_handle;
    if (arr == nullptr || arr->child == 0 || !sort_fn)
        return; // Nothing to sort.

    // Get all element indexes into an array
    config_index_t* indexes = nullptr;
    array_reserve(indexes, arr->child_count);

    for (auto e : array_handle)
    {
        array_push(indexes, e.index);
    }

    size_t element_count = array_size(indexes);
    if (element_count > 0)
    {
        config_t* config = array_handle.config;

        std::sort(indexes, indexes + element_count, [config, sort_fn](auto ia, auto ib)
        {
            return sort_fn(config_handle_t{ config , ia }, config_handle_t{ config , ib });
        });

        arr->child = indexes[0];

        config_value_t* values = config->values;
        config_value_t* p = &values[indexes[0]];
        for (int i = 1; i < element_count; ++i)
        {
            config_index_t next = indexes[i];
            p->sibling = next;
            p = &values[next];
        }

        p->sibling = 0;
    }

    array_deallocate(indexes);
}

void config_pack(const config_handle_t& value)
{
    if (value.config == nullptr)
        return;

    string_table_pack(value.config->st);
}

void config_clear(const config_handle_t& value)
{
    config_value_t* cv = value;
    if (!cv)
        return;

    cv->child = 0;
    cv->child_count = 0;
    cv->data = nullptr;
}

bool config_is_valid(const config_handle_t& h, const char* key /*= nullptr*/, size_t key_length /*= 0*/)
{
    if (!h)
        return false;

    config_value_t& v = *h;
    if (key == nullptr)
        return v.type != CONFIG_VALUE_UNDEFINED;

    config_handle_t cv = config_get_or_create(h, key, key_length);
    if (cv != nullptr)
        return config_is_valid(cv);
    return false;
}

bool config_exists(const config_handle_t& v, const char* key, size_t key_length)
{
    if (key == nullptr)
        return config_is_valid(v);
    return config_find(v, key, key_length);
}

bool config_is_null(const config_handle_t& h, const char* key /*= nullptr*/, size_t key_length /*= 0*/)
{
    if (h.config == nullptr)
        return true;

    config_value_t& v = *h;
    if (key == nullptr)
        return v.type == CONFIG_VALUE_NIL;

    config_value_t* cv = config_get_or_create(h, key, key_length);
    if (cv != nullptr)
        return cv->type == CONFIG_VALUE_NIL;
    return true;
}

bool config_is_undefined(const config_handle_t& h, const char* key /*= nullptr*/, size_t key_length /*= 0*/)
{
    const config_value_t* cv = h;

    if (!cv)
        return true;
    
    if (key == nullptr)
        return cv->type == CONFIG_VALUE_UNDEFINED;

    cv = config_find(h, key, key_length);
    if (cv == nullptr)
        return true;
    return cv->type == CONFIG_VALUE_UNDEFINED;
}

FOUNDATION_STATIC FOUNDATION_FORCEINLINE void config_sjson_add_string(config_sjson_t& sjson, const char* str, size_t length)
{
    const char* s = str;
    for (int i = 0; i < length && *s; ++i, ++s)
        sjson = array_push(sjson, *s);
}

FOUNDATION_STATIC FOUNDATION_FORCEINLINE void config_sjson_add_char(config_sjson_t& sjson, char c)
{
    sjson = array_push(sjson, c);
}

FOUNDATION_STATIC void config_sjson_write_new_line(config_sjson_t& sjson, int indentation)
{
    sjson = array_push(sjson, '\n');
    for (int i = 0; i < indentation; ++i)
        sjson = array_push(sjson, '\t');
}

FOUNDATION_STATIC FOUNDATION_FORCEINLINE bool config_sjson_is_primitive_type(const config_value_t* o)
{
    FOUNDATION_ASSERT(o);

    if (o->type == CONFIG_VALUE_NIL ||
        o->type == CONFIG_VALUE_FALSE ||
        o->type == CONFIG_VALUE_TRUE ||
        o->type == CONFIG_VALUE_NUMBER ||
        o->type == CONFIG_VALUE_STRING ||
        o->type == CONFIG_VALUE_RAW_DATA)
    {
        return true;
    }

    return false;
}

FOUNDATION_STATIC void config_sjson_write_array(const config_handle_t& array_handle, config_sjson_t& sjson, int indentation);
FOUNDATION_STATIC void config_sjson_write_object(const config_handle_t& array_handle, config_sjson_t& sjson, int indentation);

FOUNDATION_STATIC void config_sjson_write_string(config_sjson_t& sjson, string_const_t value, config_option_flags_t options)
{
    constexpr char hexchar[] = "0123456789abcdef";

    const bool escape_utf8 = (options & CONFIG_OPTION_WRITE_ESCAPE_UTF8) == CONFIG_OPTION_WRITE_ESCAPE_UTF8;

    config_sjson_add_char(sjson, '"');
    const char* s = value.str;
    for (int i = 0; i < value.length && *s; ++i, ++s)
    {
        char c = *s;
        if (c == '"' || c == '\\')
            config_sjson_add_char(sjson, '\\');
        
        // Check if we need to escape the UTF-8 character
        if (c == '\n')
        {
            config_sjson_add_char(sjson, '\\');
            config_sjson_add_char(sjson, 'n');
        }
        else if (c == '\r')
        {
            config_sjson_add_char(sjson, '\\');
            config_sjson_add_char(sjson, 'r');
        }
        else if (c == '\t')
        {
            config_sjson_add_char(sjson, '\\');
            config_sjson_add_char(sjson, 't');
        }
        else if (c == '\b')
        {
            config_sjson_add_char(sjson, '\\');
            config_sjson_add_char(sjson, 'b');
        }
        else if (c == '\f')
        {
            config_sjson_add_char(sjson, '\\');
            config_sjson_add_char(sjson, 'f');
        }
        else if (escape_utf8)
        {
            if ((uint8_t)c >= 0x80)
            {
                // Escape the UTF-8 character as \xXX
                config_sjson_add_char(sjson, '\\');
                config_sjson_add_char(sjson, 'x');
                config_sjson_add_char(sjson, hexchar[(uint8_t)c >> 4]);
                config_sjson_add_char(sjson, hexchar[(uint8_t)c & 0x0F]);
            }
            else
            {
                config_sjson_add_char(sjson, c);
            }
        }
        else
        {
            config_sjson_add_char(sjson, c);
        }
    }
    config_sjson_add_char(sjson, '"');
}


FOUNDATION_STATIC void config_sjson_write(const config_handle_t& value_handle, config_sjson_t& sjson, int indentation /*= 4*/)
{
    const config_value_t* value = value_handle;
    if (value == nullptr || value->type == CONFIG_VALUE_NIL)
    {
        config_sjson_add_string(sjson, STRING_CONST("null"));
    }
    else if (config_sjson_is_primitive_type(value))
    {
        string_const_t value_string = config_value_as_string(value_handle);
        if (value->type == CONFIG_VALUE_STRING)
            config_sjson_write_string(sjson, value_string, value_handle.config->options);
        else
            config_sjson_add_string(sjson, value_string.str, value_string.length);
    }
    else if (value->type == CONFIG_VALUE_ARRAY)
    {
        config_sjson_write_array(value_handle, sjson, indentation);
    }
    else if (value->type == CONFIG_VALUE_OBJECT)
    {
        config_sjson_write_object(value_handle, sjson, indentation);
    }
    else
    {
        log_errorf(0, ERROR_INVALID_VALUE, STRING_CONST("Unknown object %d"), value->type);
    }
}

FOUNDATION_STATIC bool config_sjson_is_simple_identifier(string_const_t value)
{
    const char* s = value.str;
    for (int i = 0; i < value.length && *s; ++i, ++s)
    {
        char c = *s;
        if (c >= '0' && c <= '9')
            continue;
        if (c >= 'a' && c <= 'z')
            continue;
        if (c >= 'A' && c <= 'Z')
            continue;
        if (c == '_')
            continue;

        return false;
    }

    return true;
}

FOUNDATION_STATIC FOUNDATION_FORCEINLINE bool config_sjson_skip_element(const config_handle_t& h, const config_value_t* item)
{
    const auto options = h.config->options;
    const bool skip_nulls = options & CONFIG_OPTION_WRITE_SKIP_NULL;
    const bool skip_double_comma_fields = options & CONFIG_OPTION_WRITE_SKIP_DOUBLE_COMMA_FIELDS;
    if (item->name > 0 && skip_double_comma_fields)
    {
        string_const_t item_name = string_table_to_string_const(h.config->st, item->name);
        if (item_name.length >= 2 && item_name.str[0] == ':' && item_name.str[1] == ':')
            return true;
    }
    return !(item->type != CONFIG_VALUE_UNDEFINED && !(skip_nulls && item->type == CONFIG_VALUE_NIL) && item->type != CONFIG_VALUE_RAW_DATA);
}

FOUNDATION_STATIC size_t config_sjson_write_object_fields(const config_handle_t& obj_handle, config_sjson_t& sjson, int indentation, bool skipFirstWhiteline, bool* out_wants_same_line = nullptr)
{
    config_value_t* obj = obj_handle;

    if (!obj || obj->child == 0)
        return 0;

    size_t fields_written = 0;
    const config_value_t* values = obj_handle.config->values;

    // Detect if we should write all primitives on the same line.
    bool wants_same_line = (obj_handle.config->options & CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES) && (obj_handle.config->options & CONFIG_OPTION_WRITE_JSON) == 0;
    if (wants_same_line)
    {
        // All elements need to be a primitive and at most 8 elements
        for (auto e : obj_handle)
        {
            config_value_t* item = e;
            if (!config_sjson_is_primitive_type(item))
            {
                wants_same_line = false;
                break;
            }
        }
    }

    int element_index = 0;
    const size_t element_count = config_size(obj_handle);
    for (auto e : obj_handle)
    {
        config_value_t* item = e;
        if (config_sjson_skip_element(e, item) || item->type == CONFIG_VALUE_UNDEFINED)
        {
            element_index++;
            continue;
        }

        string_const_t key = string_table_to_string_const(obj_handle.config->st, item->name);
        const bool simple_json = (obj_handle.config->options & CONFIG_OPTION_WRITE_JSON) == 0;
        const bool simple_identifier = simple_json && config_sjson_is_simple_identifier(key);

        if (!simple_identifier)
            wants_same_line = false;

        if (skipFirstWhiteline)
            skipFirstWhiteline = false;
        else if (indentation == 0 || !wants_same_line)
            config_sjson_write_new_line(sjson, indentation);
        else
            config_sjson_add_char(sjson, ' ');

        if (simple_identifier)
            config_sjson_add_string(sjson, key.str, key.length);
        else
        {
            config_sjson_write_string(sjson, key, obj_handle.config->options);
            wants_same_line = false;
        }

        if (simple_json)
            config_sjson_add_string(sjson, STRING_CONST(" = "));
        else
            config_sjson_add_string(sjson, STRING_CONST(": "));
        config_sjson_write(config_handle_t{ obj_handle.config, item->index }, sjson, indentation);

        if (!simple_json && element_index < element_count-1)
            config_sjson_add_string(sjson, STRING_CONST(", "));

        element_index++;
        fields_written++;
    }

    if (out_wants_same_line)
        *out_wants_same_line = wants_same_line;

    return fields_written;
}

FOUNDATION_STATIC void config_sjson_write_object(const config_handle_t& obj_handle, config_sjson_t& sjson, int indentation)
{
    const bool skip_first_brackets = (obj_handle.index == 0 && (obj_handle.config->options & CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS) != 0) 
                                        && (obj_handle.config->options & CONFIG_OPTION_WRITE_JSON) == 0;
    if (!skip_first_brackets)
        config_sjson_add_char(sjson, '{');

    bool wants_same_line = false;
    size_t fields_written = config_sjson_write_object_fields(obj_handle, sjson, indentation + (!skip_first_brackets ? 1 : 0), skip_first_brackets, &wants_same_line);
    if (fields_written > 0)
    {
        if (!wants_same_line)
            config_sjson_write_new_line(sjson, indentation);
        else
            config_sjson_add_char(sjson, ' ');
    }
    if (!skip_first_brackets)
        config_sjson_add_char(sjson, '}');
}

FOUNDATION_STATIC void config_sjson_write_array(const config_handle_t& array_handle, config_sjson_t& sjson, int indentation)
{
    config_sjson_add_char(sjson, '[');

    const config_value_t* arr = array_handle;
    bool is_last_item_primitive = arr->child == 0;
    if (arr->child != 0)
    {
        bool first_item = true;
        const bool simple_json = (array_handle.config->options & CONFIG_OPTION_WRITE_JSON) == 0;
        auto options = array_handle.config->options;

        int element_index = 0;
        const size_t element_count = config_size(array_handle);
        const config_value_t* values = array_handle.config->values;
        const config_value_t* item = &values[arr->child];
        while (item)
        {
            if (!config_sjson_skip_element(array_handle, item))
            {
                is_last_item_primitive = config_sjson_is_primitive_type(item);
                if (is_last_item_primitive)
                {
                    if (!first_item)
                        config_sjson_add_char(sjson, ' ');
                    else
                        first_item = false;
                }
                else
                    config_sjson_write_new_line(sjson, indentation + 1);
                config_sjson_write(config_handle_t{ array_handle.config, item->index }, sjson, indentation + 1);

                if (!simple_json && element_index < element_count - 1)
                    config_sjson_add_string(sjson, STRING_CONST(", "));
            }

            element_index++;
            item = item->sibling > 0 ? &values[item->sibling] : nullptr;
        }
    }

    if (!is_last_item_primitive)
        config_sjson_write_new_line(sjson, indentation);
    config_sjson_add_char(sjson, ']');
}

config_sjson_const_t config_sjson(const config_handle_t& value_handle, config_option_flags_t options /*= CONFIG_OPTION_NONE*/)
{
    const config_value_t* value = value_handle;
    if (value == nullptr)
        return nullptr;

    if (value->type == CONFIG_VALUE_UNDEFINED)
        return nullptr;

    char* sjson = nullptr;
    array_reserve(sjson, 64);
    sjson[0] = '\0';

    const config_option_flags_t existing_options = value_handle.config->options;
    value_handle.config->options |= options;

    config_sjson_write(value_handle, sjson, 0);
    config_sjson_add_char(sjson, '\0');

    value_handle.config->options = existing_options;	
    return sjson;
}

string_const_t config_sjson_to_string(config_sjson_const_t sjson)
{
    unsigned length = array_size(sjson);
    return string_const(sjson, length == 0 ? 0 : length-1);
}

void config_sjson_deallocate(config_sjson_const_t sjson)
{
    array_deallocate(sjson);
}

bool config_parse_at_end(string_const_t json, int index)
{
    return index >= json.length;
}

void config_parse_skip_BOM(string_const_t json, int& index)
{
    if (!config_parse_at_end(json, index + 2) && (unsigned char)json.str[index] == 0xEF && (unsigned char)json.str[index + 1] == 0xBB && (unsigned char)json.str[index + 2] == 0xBF)
        index += 3;
}

std::runtime_error config_parse_exception(string_const_t json, int index, const char* error)
{
// 	int lineNumber = -1;
// 	dynamic_array<int> lines(kMemTempAlloc);
// 
// 	for (size_t i = 0; i < sjson.size(); ++i)
// 	{
// 		if (i == index)
// 			lineNumber = lines.size();
// 		if (sjson[i] == 10)
// 			lines.push_back(i + 1);
// 	}
// 	lines.push_back(sjson.size());
// 	if (lineNumber == -1)
// 		lineNumber = lines.size() - 1;
// 
// 	dynamic_array<core::string> context(kMemTempAlloc);
// 	for (int i = lineNumber - 2; i <= lineNumber + 2; ++i)
// 	{
// 		if (i >= 0 && i + 1 < lines.size())
// 		{
// 			int start = lines[i], end = lines[i + 1];
// 			TempString line(end - start + 1, '\0', kMemTempAlloc);
// 			std::copy(sjson.begin() + start, sjson.begin() + end, line.begin());
// 			context.push_back(line);
// 		}
// 	}
// 
// 	return std::runtime_error(core::Format("SJSON Parse Error (memory) at line {0}: {1}\n\n{2}",
// 		lineNumber, error, JoinArray(context, "\n")).c_str());

    return std::runtime_error(error);
}

FOUNDATION_STATIC char config_parse_next(string_const_t json, int index)
{
    if (config_parse_at_end(json, index))
        throw config_parse_exception(json, index, "Unexpected end of data");
    return json.str[index];
}

void config_parse_skip_comment(string_const_t json, int& index)
{
    if (!config_parse_at_end(json, index + 1) && config_parse_next(json, index + 1) == '/')
    {
        while (!config_parse_at_end(json, index + 1) && json.str[index] != '\n')
            ++index;
        ++index;
    }
    else if (!config_parse_at_end(json, index + 1) && config_parse_next(json, index + 1) == '*')
    {
        while (!config_parse_at_end(json, index + 2) && (json.str[index] != '*' || json.str[index + 1] != '/'))
            ++index;
        index += 2;
    }
    else
        throw config_parse_exception(json, index, "Error in comment");
}

void config_parse_skip_whitespace(string_const_t json, int& index)
{
    while (!config_parse_at_end(json, index))
    {
        char c = config_parse_next(json, index);
        if (c == '/')
            config_parse_skip_comment(json, index);
        else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',')
            ++index;
        else
            break;
    }
}

FOUNDATION_STATIC bool config_parse_consume(string_const_t json, int& index, const char* consume, size_t consume_length, bool error = true)
{
    int end = index;
    config_parse_skip_whitespace(json, end);
    const char* t = consume;
    for (int i = 0; i < consume_length && *t; ++i, ++t)
    {
        if (config_parse_next(json, end) != *t)
        {
            if (error)
                throw config_parse_exception(json, end, "Error consuming: "/* + consume*/);
            return false;
        }
        ++end;
    }

    index = end;
    return true;
}

string_t config_parse_literal_string(string_const_t json, int& index)
{
    config_parse_consume(json, index, STRING_CONST("\"\"\""));
    int end = index;
    while (config_parse_next(json, end) != '"' || config_parse_next(json, end + 1) != '"' || config_parse_next(json, end + 2) != '"')
        ++end;
    int length = end - index;
    string_t res = string_clone(json.str + index, length);
    index = end;
    config_parse_consume(json, index, STRING_CONST("\"\"\""));
    return res;
}

string_t config_parse_string(string_const_t json, int& index, config_option_flags_t options)
{
    if (!config_parse_at_end(json, index + 2) && json.str[index + 1] == '"' && json.str[index + 2] == '"')
        return config_parse_literal_string(json, index);

    char* s = nullptr;
    array_reserve(s, 32);

    config_parse_consume(json, index, STRING_CONST("\""));
    while (true)
    {
        char c = config_parse_next(json, index);
        ++index;
        if (c == '"')
            break;
        if (c != '\\')
        {
            s = array_push(s, c);
        }
        else
        {
            char q = config_parse_next(json, index);
            ++index;
            if (q == '"' || q == '\\' || q == '/')
            {
                s = array_push(s, q);
            }
            else if (q == 'b') { s = array_push(s, '\b'); }
            else if (q == 'f') { s = array_push(s, '\f'); }
            else if (q == 'n') { s = array_push(s, '\n'); }
            else if (q == 'r') { s = array_push(s, '\r'); }
            else if (q == 't') { s = array_push(s, '\t'); }
            else if (q == 'u')
            {
                if (options & CONFIG_OPTION_PARSE_UNICODE_UTF8)
                {
                    scoped_string_t utf8 = string_utf8_unescape(json.str + index - 2, 6);
                    if (utf8.value.str == nullptr)
                        throw config_parse_exception(json, index, "Invalid Unicode character or sequence");

                    const char* utf8c = utf8.value.str;
                    for (int i = 0; i < utf8.value.length && *utf8c; ++i, ++utf8c)
                        s = array_push(s, *utf8c);
                    index += 4;
                }
                else
                {
                    s = array_push(s, '\\');
                    s = array_push(s, 'u');
                }
            }
            else if (q == 'x')
            {
                // Parse UTF-8 char
                if (options & CONFIG_OPTION_PARSE_UNICODE_UTF8)
                {
                    char b1 = config_parse_next(json, index);
                    char b2 = config_parse_next(json, index + 1);
                    if (b1 == '0' && b2 == '0')
                    {
                        s = array_push(s, '\0');
                    }
                    else
                    {
                        // Convert b1 and b2 to uint8_t
                        uint8_t b1v = 0;
                        uint8_t b2v = 0;
                        if (b1 >= '0' && b1 <= '9')
                            b1v = b1 - '0';
                        else if (b1 >= 'a' && b1 <= 'f')
                            b1v = b1 - 'a' + 10;
                        else if (b1 >= 'A' && b1 <= 'F')
                            b1v = b1 - 'A' + 10;
                        else
                            throw config_parse_exception(json, index, "Invalid hex character");

                        if (b2 >= '0' && b2 <= '9')
                            b2v = b2 - '0';
                        else if (b2 >= 'a' && b2 <= 'f')
                            b2v = b2 - 'a' + 10;
                        else if (b2 >= 'A' && b2 <= 'F')
                            b2v = b2 - 'A' + 10;
                        else
                            throw config_parse_exception(json, index, "Invalid hex character");

                        // Convert to UTF-8
                        uint8_t b = (b1v << 4) | b2v;

                        s = array_push(s, (char)b);
                    }
                }
                else
                {
                    s = array_push(s, '\\');
                    s = array_push(s, 'x');
                }

                index += 2;
            }
            else
                throw config_parse_exception(json, index, "Unknown escape code");
        }
    }

    string_t res = string_clone(s, array_size(s));
    array_deallocate(s);
    return res;
}

FOUNDATION_STATIC config_handle_t config_parse_string(string_const_t json, int& index, config_handle_t str_handle)
{
    string_t s = config_parse_string(json, index, str_handle.config->options);
    config_set(str_handle, STRING_ARGS(s));
    string_deallocate(s.str);
    return str_handle;
}

FOUNDATION_STATIC string_t config_parse_identifier(string_const_t json, int& index)
{
    config_parse_skip_whitespace(json, index);

    if (config_parse_next(json, index) == '"')
        return config_parse_string(json, index, CONFIG_OPTION_NONE);

    char* s = nullptr;
    array_reserve(s, 32);

    while (true)
    {
        char c = config_parse_next(json, index);
        if (c == ' ' || c == '\t' || c == '\n' || c == '=' || c == ':')
            break;
        s = array_push(s, c);
        ++index;
    }

    string_t res = string_clone(s, array_size(s));
    array_deallocate(s);
    return res;
}

config_handle_t config_parse_value(string_const_t json, int& index, config_handle_t value);

config_handle_t config_parse_object_field(string_const_t json, int& index, config_handle_t ht)
{
    string_t key = config_parse_identifier(json, index);
    config_parse_skip_whitespace(json, index);
    if (config_parse_next(json, index) == ':')
        config_parse_consume(json, index, STRING_CONST(":"));
    else
        config_parse_consume(json, index, STRING_CONST("="));

    config_handle_t value = config_add(ht, key.str, key.length);

    value = config_parse_value(json, index, value);
    string_deallocate(key.str);
    config_parse_skip_whitespace(json, index);
    return value;
}


config_handle_t config_parse_object(string_const_t json, int& index, config_handle_t ht)
{
    config_value_t* cv = ht;
    if (cv)
    {
        cv->type = CONFIG_VALUE_OBJECT;
        cv->child = 0;
    }

    config_parse_skip_BOM(json, index);
    config_parse_consume(json, index, STRING_CONST("{"));
    config_parse_skip_whitespace(json, index);

    while (config_parse_next(json, index) != '}')
        config_parse_object_field(json, index, ht);
    config_parse_consume(json, index, STRING_CONST("}"));
    return ht;
}

config_handle_t config_parse_array(string_const_t json, int& index, config_handle_t array_handle)
{
    config_value_t* cv = array_handle;
    if (cv)
    {
        cv->type = CONFIG_VALUE_ARRAY;
        cv->child = 0;
    }

    config_parse_consume(json, index, STRING_CONST("["));
    config_parse_skip_whitespace(json, index);

    while (config_parse_next(json, index) != ']')
    {
        config_handle_t element = config_array_push(array_handle);
        config_parse_value(json, index, element);
        config_parse_skip_whitespace(json, index);
    }
    config_parse_consume(json, index, STRING_CONST("]"));

    return array_handle;
}

config_handle_t config_parse_number(string_const_t json, int& index, config_handle_t value)
{
    int end = index;	
    while (!config_parse_at_end(json, end) && string_find_first_of(json.str + end, 1, STRING_CONST("0123456789abcdef+-.eE"), 0) != STRING_NPOS)
        ++end;
    int length = end - index;
    config_handle_t res;
    if (string_find_last_of(json.str + index, length, STRING_CONST("abcdef"), STRING_NPOS) != STRING_NPOS)
        res = config_set(value, (const void*)string_to_size(json.str + index, length, true));
    else 
        res = config_set(value, string_to_real(json.str + index, length));
    index = end;
    return res;
}

config_handle_t config_parse_value(string_const_t json, int& index, config_handle_t value)
{
    config_parse_skip_whitespace(json, index);

    char c = config_parse_next(json, index);

    if (c == '{')
        return config_parse_object(json, index, value);
    if (c == '[')
        return config_parse_array(json, index, value);
    if (c == '"')
        return config_parse_string(json, index, value);
    if (c == '-' || c == '.' || (c >= '0' && c <= '9'))
    {
        return config_parse_number(json, index, value);
    }
    if (c == 't')
    {
        if (config_parse_consume(json, index, STRING_CONST("true"), false))
            return config_set(value, true);
    }
    if (c == 'f')
    {
        if (config_parse_consume(json, index, STRING_CONST("false"), false))
            return config_set(value, false);
    }
    if (c == 'n')
    {
        if (config_parse_consume(json, index, STRING_CONST("null"), false))
            return config_set_null(value, nullptr, 0);
    }

    // Finally try to parse the value as an identifier or a raw data number.
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
    {
        config_handle_t res;
        scoped_string_t s = config_parse_identifier(json, index);
        res = config_set(value, STRING_ARGS(s.value));
        return res;
    }

    log_warnf(0, WARNING_INVALID_VALUE, STRING_CONST("Invalid value '%.*s'"), min((int)json.length - index, 32), json.str + index);
    throw config_parse_exception(json, index, "Unexpected character");
}

config_handle_t config_parse_root_object(string_const_t json, int& index, config_option_flags_t options)
{
    config_parse_skip_BOM(json, index);
    config_parse_skip_whitespace(json, index);

    if (config_parse_at_end(json, index))
        return NIL;

    config_handle_t root = config_allocate(CONFIG_VALUE_OBJECT, options);

    if (config_parse_next(json, index) == '{')
        return config_parse_object(json, index, root);

    if (config_parse_next(json, index) == '[')
        return config_parse_array(json, index, root);

    while (!config_parse_at_end(json, index))
        config_parse_object_field(json, index, root);
    
    return root;
}

config_handle_t config_parse(const char* json, size_t json_length, config_option_flags_t options /*= CONFIG_OPTION_NONE*/)
{
    int index = 0;
    config_handle_t root = config_parse_root_object(string_const(json, json_length), index, options);
    if (options & CONFIG_OPTION_PACK_STRING_TABLE)
    {
        string_table_pack(&root.config->st);
    }

    return root;
}

config_handle_t config_parse_file(const char* file_path, size_t file_path_length, config_option_flags_t options /*= CONFIG_OPTION_NONE*/)
{
    if (!fs_is_file(file_path, file_path_length))
        return NIL;

    stream_t* json_file_stream = fs_open_file(file_path, file_path_length, STREAM_IN | STREAM_BINARY);
    if (json_file_stream == nullptr)
        return NIL;

    const size_t json_buffer_size = stream_size(json_file_stream);

    string_t json_buffer = string_allocate(json_buffer_size + 1, json_buffer_size + 2);
    scoped_string_t json_string = stream_read_string_buffer(json_file_stream, json_buffer.str, json_buffer.length);

    auto json_root_handle = config_parse(STRING_ARGS(json_string.value), options);
    stream_deallocate(json_file_stream);
    return json_root_handle;
}

bool config_write_file(string_const_t _file_path, config_handle_t data, config_option_flags_t write_json_flags /*= CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS | CONFIG_OPTION_WRITE_SKIP_NULL*/)
{
    bool success = true;
    char local_copy_file_path_buffer[BUILD_MAX_PATHLEN];
    string_t file_path = string_copy(STRING_BUFFER(local_copy_file_path_buffer), STRING_ARGS(_file_path));
    config_sjson_const_t sjson = config_sjson(data, write_json_flags);
    size_t sjson_length = array_size(sjson);

    if (sjson_length == 0)
    {
        log_warnf(0, WARNING_INVALID_VALUE, STRING_CONST("No data to write to config file %.*s"), STRING_FORMAT(file_path));
        return false;
    }

    const bool no_write_on_data_equal = write_json_flags & CONFIG_OPTION_WRITE_NO_SAVE_ON_DATA_EQUAL;
    string_t current_text_buffer = no_write_on_data_equal ? fs_read_text(STRING_ARGS(file_path)) : string_t{nullptr, 0};
    if (!no_write_on_data_equal || !string_equal(STRING_ARGS(current_text_buffer), sjson, sjson_length - 1))
    {
        stream_t* sjson_file_stream = fs_open_file(STRING_ARGS(file_path), STREAM_CREATE | STREAM_OUT | STREAM_TRUNCATE);
        if (sjson_file_stream)
        {
            log_debugf(0, STRING_CONST("Writing config file %.*s"), STRING_FORMAT(file_path));
            stream_write_string(sjson_file_stream, sjson, sjson_length - 1);
            stream_deallocate(sjson_file_stream);
        }
        else
        {
            log_errorf(0, ERROR_ACCESS_DENIED, STRING_CONST("Failed to create SJSON stream for %.*s"), STRING_FORMAT(file_path));
            success = false;
        }
    }

    string_deallocate(current_text_buffer.str);
    config_sjson_deallocate(sjson);
    return success;
}

bool config_write_file(
    string_const_t output_file_path,
    function<bool(const config_handle_t& data)> write_callback,
    config_value_type_t value_type /*= CONFIG_VALUE_OBJECT*/,
    config_option_flags_t write_json_flags /*= CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS | CONFIG_OPTION_WRITE_SKIP_NULL*/)
{
    config_handle_t data = config_allocate(value_type, write_json_flags);

    bool success = true;
    if (write_callback(data))
    {
        success &= config_write_file(output_file_path, data, write_json_flags);
    }
    else
    {
        success = false;
    }

    config_deallocate(data);
    return success;
}
