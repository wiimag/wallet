/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "query_json.h"

#include <foundation/math.h>

json_object_t json_parse(const string_t& str)
{
    return json_object_t(str);
}

const json_token_t* json_find_token(const json_object_t& json, const char* key, size_t key_length /*= 0*/)
{
    if (json.root == nullptr)
        return nullptr;
    return json_find_token(json.buffer, json.tokens, *json.root, key, key_length);
}

const json_token_t* json_find_token(const char* json, const json_token_t* tokens, const json_token_t& obj, const char* key, size_t key_length /*= 0*/)
{
    if (!key || tokens == nullptr)
        return nullptr;

    if (obj.type == JSON_UNDEFINED || obj.type == JSON_STRING || obj.type == JSON_PRIMITIVE)
        return nullptr;

    if (obj.type == JSON_OBJECT)
    {
        unsigned int c = obj.child;
        if (key_length == 0)
            key_length = string_length(key);
        while (c != 0)
        {
            const json_token_t& t = tokens[c];
            string_const_t id = string_const(json + t.id, t.id_length);
            if (string_equal(STRING_ARGS(id), key, key_length))
                return &t;

            c = t.sibling;
        }
        return nullptr;
    }

    if (obj.type == JSON_ARRAY)
    {
        FOUNDATION_ASSERT_FAIL("NOT SUPPORTED");
        return nullptr;
    }

    return nullptr;
}

double json_read_number(const char* json, const json_token_t* tokens, const json_token_t& value, double default_value /*= NAN*/)
{
    FOUNDATION_UNUSED(tokens);

    if (value.type == JSON_UNDEFINED)
        return NAN;

    if (value.type == JSON_PRIMITIVE || value.type == JSON_STRING)
    {
        string_const_t str_n = string_const(json + value.value, value.value_length);
        double n = string_to_float64(STRING_ARGS(str_n));
        if (n == 0 && str_n.length > 0 && str_n.str[0] != '0')
            return default_value;
        return n;
    }

    return default_value;
}

double json_read_number(const char* json, const json_token_t* tokens, const json_token_t* value, double default_value /*= DNAN*/)
{
    if (value == nullptr)
        return default_value;
    return json_read_number(json, tokens, *value, default_value);
}

double json_read_number(const json_object_t& json, const char* field_name, size_t field_name_length)
{
    const json_token_t* field_value_token = json_find_token(json, field_name, field_name_length);
    return json_read_number(json.buffer, json.tokens, field_value_token);
}

double json_read_number(const json_object_t& json, const json_token_t* obj, const char* field_name, size_t field_name_length)
{
    if (obj == nullptr)
        return NAN;
    const json_token_t* field_value_token = json_find_token(json.buffer, json.tokens, *obj, field_name, field_name_length);
    return json_read_number(json.buffer, json.tokens, field_value_token);
}

double json_read_number(const char* json, const json_token_t* tokens, const json_token_t& obj, const char* field_name, size_t field_name_length)
{
    const json_token_t* field_value_token = json_find_token(json, tokens, obj, field_name, field_name_length);
    return json_read_number(json, tokens, field_value_token);
}

double json_read_number(const char* json, const json_token_t* tokens, const json_token_t& obj, const char* key, double& out_number)
{
    const json_token_t* prim = json_find_token(json, tokens, obj, key);
    if (!prim)
        return NAN;

    out_number = json_read_number(json, tokens, *prim);
    return out_number;
}

time_t json_read_time(const char* json, const json_token_t* tokens, const json_token_t& obj, const char* key, time_t& out_number)
{
    double t;
    json_read_number(json, tokens, obj, key, t);
    out_number = (uint64_t)t;
    return out_number;
}
