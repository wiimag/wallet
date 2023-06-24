/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "string_builder.h"

struct string_builder_t
{
    char* text{ nullptr };
};

string_builder_t* string_builder_allocate(unsigned capacity /*= UINT32_C(2048)*/)
{
    string_builder_t* sb = (string_builder_t*)memory_allocate(0, sizeof(string_builder_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
    capacity = max(capacity, UINT32_C(4));
    array_reserve(sb->text, capacity);
    memset(sb->text, 0, capacity);
    return sb;
}

void string_builder_deallocate(string_builder_t*& sb)
{
    FOUNDATION_ASSERT(sb);
    array_deallocate(sb->text);
    memory_deallocate(sb);
    sb = nullptr;
}

unsigned string_builder_size(const string_builder_t* sb)
{
    FOUNDATION_ASSERT(sb);
    return (size_t)array_size(sb->text);
}

string_builder_t* string_builder_append(string_builder_t* sb, const char c)
{
    FOUNDATION_ASSERT(sb);
    array_push(sb->text, c);
    return sb;
}

string_builder_t* string_builder_append(string_builder_t* sb, const char* text, size_t length)
{
    FOUNDATION_ASSERT(sb);
    const size_t current_size = string_builder_size(sb);
    const size_t required_size = current_size + length;
    array_resize(sb->text, required_size);
    memcpy(sb->text + current_size, text, length);
    return sb;
}

string_builder_t* string_builder_append(string_builder_t* sb, const char* text)
{
    return string_builder_append(sb, text, string_length(text));
}

string_builder_t* string_builder_append(string_builder_t* sb, const string_t& text)
{
    return string_builder_append(sb, text.str, text.length);
}

string_builder_t* string_builder_append(string_builder_t* sb, const string_const_t& text)
{
    return string_builder_append(sb, text.str, text.length);
}

string_builder_t* string_builder_append(string_builder_t* sb, const string_builder_t* other)
{
    return string_builder_append(sb, other->text, string_builder_size(other));
}

string_builder_t* string_builder_append_new_line(string_builder_t* sb)
{
    return string_builder_append(sb, '\n');
}

string_builder_t* string_builder_append_indent(string_builder_t* sb, unsigned indent)
{
    for (unsigned i = 0; i < indent; ++i)
        string_builder_append(sb, ' ');
    return sb;
}

string_builder_t* string_builder_append_vformat(string_builder_t* sb, const char* format, va_list args)
{
    string_t str = string_allocate_vformat(format, string_length(format), args);
    string_builder_append(sb, str);
    string_deallocate(str.str);
    return sb;
}

string_builder_t* string_builder_append_format(string_builder_t* sb, const char* format, ...)
{
    va_list args;
    va_start(args, format);

    string_builder_append_vformat(sb, format, args);
    va_end(args);

    return sb;
}

string_const_t string_builder_text(const string_builder_t* sb)
{
    return string_const_t{ sb->text, string_builder_size(sb) };
}
