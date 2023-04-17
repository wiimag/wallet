/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * String builder to build strings.
 */

#include <framework/string.h>

struct string_builder_t;

string_builder_t* string_builder_allocate(unsigned capacity = UINT32_C(2048));

void string_builder_deallocate(string_builder_t*& sb);

unsigned string_builder_size(const string_builder_t* sb);

string_builder_t* string_builder_append(string_builder_t* sb, const char c);

string_builder_t* string_builder_append(string_builder_t* sb, const char* text, size_t length);

string_builder_t* string_builder_append(string_builder_t* sb, const char* text);

string_builder_t* string_builder_append(string_builder_t* sb, const string_t& text);

string_builder_t* string_builder_append(string_builder_t* sb, const string_const_t& text);

string_builder_t* string_builder_append(string_builder_t* sb, const string_builder_t* other);

string_builder_t* string_builder_append_new_line(string_builder_t* sb);

string_builder_t* string_builder_append_indent(string_builder_t* sb, unsigned indent);

string_builder_t* string_builder_append_vformat(string_builder_t* sb, const char* format, va_list args);

string_builder_t* string_builder_append_format(string_builder_t* sb, const char* format, ...);

string_const_t string_builder_text(const string_builder_t* sb);
