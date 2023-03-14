/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "query.h"

#include <framework/array.h>
#include <framework/common.h>

#include <foundation/path.h>

#if ENABLE_QUERY_MOCKING

typedef struct QueryMockRequestResponse {
    string_t request{};
    string_t response{};
    query_format_t format{ FORMAT_UNDEFINED };

    hash_t request_hash{};
} query_mock_request_response_t;

static bool _query_mock_enabled = false;

// We assume mocks are registered at startup and do not change afterward, 
// therefore we can safely read them in any thread.
static query_mock_request_response_t* _query_mocks = nullptr;

void query_mock_register_request_response(
    const char* query, size_t query_size,
    const char* response, size_t response_length,
    query_format_t format /*= FORMAT_UNDEFINED*/)
{
    FOUNDATION_ASSERT(_query_mock_enabled);

    QueryMockRequestResponse m;
    m.format = format;
    m.request = string_clone(query, query_size);
    m.response = string_clone(response, response_length);
    m.request_hash = string_hash(STRING_ARGS(m.request));

    array_push_memcpy(_query_mocks, &m);
}

bool query_mock_is_enabled(const char* query, bool* mock_success, string_t* result)
{
    if (!_query_mock_enabled || query == nullptr || _query_mocks == nullptr)
        return false;

    const size_t query_size = string_length(query);
    if (query_size == 0)
        return false;

    string_const_t uri_path = path_strip_protocol(query, query_size);
    while (uri_path.length > 0 && uri_path.str[0] == '/')
    {
        uri_path.str++;
        uri_path.length--;
    }
    if (uri_path.length == 0)
        return false;

    size_t pos = string_find(STRING_ARGS(uri_path), '/', 1);
    if (pos == STRING_NPOS)
        return false;

    uri_path = string_substr(STRING_ARGS(uri_path), pos + 1, uri_path.length - pos - 1);
    pos = string_find(STRING_ARGS(uri_path), '?', 0);
    if (pos != STRING_NPOS)
        uri_path = string_substr(STRING_ARGS(uri_path), 0, pos);
    if (uri_path.length == 0)
        return false;

    const hash_t uri_hash = string_hash(uri_path.str, uri_path.length);
    foreach(e, _query_mocks)
    {
        if (uri_hash == e->request_hash)
        {
            if (result)
                *result = string_clone(STRING_ARGS(e->response));

            return true;
        }
    }

    return false;
}

void query_mock_initialize()
{
    _query_mock_enabled = main_is_running_tests();
}

void query_mock_shutdown() 
{
    _query_mock_enabled = false;

    foreach(e, _query_mocks)
    {
        string_deallocate(e->request.str);
        string_deallocate(e->response.str);
    }
    array_deallocate(_query_mocks);
}

#endif
