/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/string.h>

struct search_query_node_t;

typedef enum class SearchQueryTokenType : unsigned char
{
    Undefined,
    Word,
    Literal,
    Property,
    Function,
    Group,

    // Keywords
    Or,
    And,
    Not,
} search_query_token_type_t;

struct search_query_t
{
    string_t text{};
    uint32_t document_count{ 0 };
    //search_document_handle_t* documents{ nullptr };
    bool completed{ false };

    search_query_node_t* root{ nullptr };
};

struct search_query_token_t
{
    search_query_token_type_t type{ SearchQueryTokenType::Undefined };
    string_const_t            token{};
    search_query_token_t*     children{ nullptr };

    string_const_t            name{}; // Used by function and property token
};

search_query_t* search_query_allocate(const char* text, size_t length);

void search_query_deallocate(search_query_t*& query);

search_query_token_t* search_query_parse_tokens(const char* text, size_t length);

void search_query_deallocate_tokens(search_query_token_t*& tokens);

const char* search_query_parse_block(const char* const tok, const char* const end, search_query_token_t*& tokens);

search_query_node_t* search_query_scan_operator_node(search_query_token_t* tokens);
