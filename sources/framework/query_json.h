/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include <foundation/json.h>
#include <foundation/array.h>
#include <foundation/string.h>

struct json_object_t;

const json_token_t* json_find_token(const json_object_t& json, const char* key, size_t key_length = 0);

double json_read_number(const char* json, const json_token_t* tokens, const json_token_t* value, double default_value = NAN);

struct json_object_t
{
    bool child{ false }; // Child objects to not own the tokens allocation
    const char* buffer;
    size_t token_count;
    json_token_t* tokens;
    const json_token_t* root;
    long status_code{ 0 };
    long error_code{ 0 };
    string_const_t query;

    json_object_t()
        : buffer(nullptr)
        , token_count(0)
        , tokens(nullptr)
        , root(nullptr)
        , child(false)
    {

    }

    /// <summary>
    /// Construct a json object from a string constant literal.
    /// </summary>
    /// <param name="json_string"></param>
    json_object_t(string_const_t json_string)
        : buffer(json_string.str)
        , token_count(0)
        , tokens(nullptr)
        , root(nullptr)
    {
        token_count = json_parse(STRING_ARGS(json_string), nullptr, 0);
        if (token_count > 0)
        {
            array_reserve(tokens, token_count);
            token_count = json_parse(STRING_ARGS(json_string), tokens, token_count);
            root = &tokens[0];
        }
    }

    json_object_t(const string_t& buffer)
        : json_object_t(string_const(STRING_ARGS(buffer)))
    {
    }

    json_object_t(const json_object_t& json, const json_token_t* obj = nullptr)
        : child(true)
        , buffer(json.buffer)
        , token_count(json.token_count)
        , tokens(json.tokens)
        , root(obj ? obj : json.root)
        , status_code(json.status_code)
        , error_code(json.error_code)
        , query(json.query)
    {
    }

    json_object_t(json_object_t&& src) noexcept
        : child(src.child)
        , buffer(src.buffer)
        , token_count(src.token_count)
        , tokens(src.tokens)
        , root(src.root)
        , status_code(src.status_code)
        , error_code(src.error_code)
        , query(src.query)
    {
        src.child = true;
        src.buffer = nullptr;
        src.token_count = 0;
        src.tokens = nullptr;
        src.root = nullptr;
        src.query = {};
    }

    json_object_t& operator=(const json_object_t& src) noexcept
    {
        buffer = src.buffer;
        token_count = src.token_count;
        tokens = src.tokens;
        root = src.root;
        child = true;
        status_code = src.status_code;
        error_code = src.error_code;
        query = src.query;
        return *this;
    }

    json_object_t& operator=(json_object_t&& src) noexcept
    {
        buffer = src.buffer;
        token_count = src.token_count;
        tokens = src.tokens;
        root = src.root;
        status_code = src.status_code;
        error_code = src.error_code;
        query = src.query;

        src.buffer = nullptr;
        src.token_count = 0;
        src.tokens = nullptr;
        src.root = nullptr;
        query = {};

        return *this;
    }

    ~json_object_t()
    {
        if (!child)
            array_deallocate(tokens);
    }

    string_const_t id() const
    {
        if (buffer == nullptr || root == nullptr)
            return string_null();
        return string_const(buffer + root->id, root->id_length);
    }

    string_const_t to_string() const
    {
        if (buffer == nullptr || root == nullptr)
            return string_null();
        return string_const(buffer + root->value, root->value_length);
    }

    bool is_valid() const
    {
        return root && tokens;
    }

    bool is_null() const
    {
        if (root == nullptr || buffer == nullptr)
            return true;

        if (root->type == JSON_UNDEFINED)
            return true;

        if (root->type == JSON_PRIMITIVE)
            return string_equal(STRING_CONST("null"), STRING_ARGS(json_token_value(buffer, root)));

        return false;
    }

    bool resolved() const
    {
        return is_valid() && status_code < 400;
    }

    const json_object_t find(const char* path, size_t path_length, bool allow_null = true) const
    {
        const json_object_t& json = *this;
        string_const_t token, r;
        string_split(path, path_length, STRING_CONST("."), &token, &r, false);

        json_object_t ref = json;
        while (token.length > 0)
        {
            string_const_t subToken, orChoiceR;
            string_split(STRING_ARGS(token), STRING_CONST("|"), &subToken, &orChoiceR, false);
            bool foundSubPart = false;
            while (subToken.length > 0)
            {
                auto subRef = ref.get(STRING_ARGS(subToken));
                if (allow_null ? subRef.is_valid() : !subRef.is_null())
                {
                    ref = subRef;
                    if (r.length == 0)
                        return ref;

                    foundSubPart = true;
                    break;
                }

                string_split(STRING_ARGS(orChoiceR), STRING_CONST("|"), &subToken, &orChoiceR, false);
            }

            if (!foundSubPart)
                return json_object_t{};

            string_split(STRING_ARGS(r), STRING_CONST("."), &token, &r, false);
        }

        return json_object_t{};
    }

    const json_object_t get(size_t index) const
    {
        if (root->child == 0)
            return json_object_t{};

        const json_token_t* c = &tokens[root->child];
        while (index != 0)
        {
            index--;
            if (c->sibling == 0)
                break;
            c = &tokens[c->sibling];
        }
        return json_object_t(*this, c);
    }

    const json_object_t get(const char* field_name, size_t field_name_size) const
    {
        const json_token_t* field_token = json_find_token(*this, field_name, field_name_size);
        if (field_token == nullptr)
            return json_object_t{};
        return json_object_t(*this, field_token);
    }

    const json_object_t operator[](size_t index) const
    {
        return get(index);
    }

    const json_object_t operator[](const char* field_name) const
    {
        return get(field_name, 0U);
    }

    const json_object_t operator[](string_const_t field_name) const
    {
        return get(STRING_ARGS(field_name));
    }

    double as_number(double default_value = NAN) const
    {
        return json_read_number(buffer, tokens, root, default_value);
    }

    string_const_t as_string() const
    {
        if (root == nullptr)
            return string_const(STRING_CONST(""));
        return json_token_value(buffer, root);
    }

    bool operator!=(const json_object_t& other) const
    {
        return other.root != this->root;
    }

    bool operator==(const json_object_t& other) const
    {
        return !operator!=(other);
    }

    json_object_t& operator++()
    {
        if (root->sibling == 0)
        {
            this->root = nullptr;
            this->error_code = LONG_MAX;
            this->status_code = LONG_MAX;
        }
        else
        {
            this->root = &tokens[root->sibling];
        }

        return *this;
    }

    const json_object_t& operator*() const
    {
        return *this;
    }

    json_object_t begin(size_t index = 0) const
    {
        if (this->root == nullptr)
            return end();
        return get(index);
    }

    json_object_t end() const
    {
        return json_object_t{};
    }
};

json_object_t json_parse(const string_t& str);

const json_token_t* json_find_token(const char* json, const json_token_t* tokens, const json_token_t& root, const char* key, size_t key_length = 0);

double json_read_number(const char* json, const json_token_t* tokens, const json_token_t& value, double default_value = NAN);
double json_read_number(const json_object_t& json, const char* field_name, size_t field_name_length);
double json_read_number(const json_object_t& json, const json_token_t* value, const char* field_name, size_t field_name_length);

double json_read_number(const char* json, const json_token_t* tokens, const json_token_t& obj, const char* key, double& out_number);

time_t json_read_time(const char* json, const json_token_t* tokens, const json_token_t& obj, const char* key, time_t& out_number);
