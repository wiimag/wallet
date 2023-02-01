/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/string.h>

struct scoped_string_t
{
    scoped_string_t(const char* s)
        : value(string_clone(s, string_length(s)))
    {
    }

    scoped_string_t(const string_const_t& s)
        : value(string_clone(s.str, s.length))
    {
    }

    scoped_string_t(string_t&& o)
    {
        string_t temp = value;
        value.str = o.str;
        value.length = o.length;
        string_deallocate(temp.str);
    }

    scoped_string_t& operator=(string_t&& o)
    {
        string_t temp = value;
        value.str = o.str;
        value.length = o.length;
        string_deallocate(temp.str);
        return *this;
    }

    scoped_string_t(const scoped_string_t& other)
    : value(string_clone(other.value.str, other.value.length))
    {
    }

    scoped_string_t(scoped_string_t&& o) noexcept 
    {
        value.str = o.value.str;
        value.length = o.value.length;
        o.value.str = nullptr;
        o.value.length = 0;
    }

    ~scoped_string_t()
    {
        string_deallocate(value.str);
    }

    operator string_t&()
    {
        return value;
    }

    operator const char*()
    {
        return value.str;
    }

    size_t length() const
    {
        return value.length;
    }

    string_t value{};
};
