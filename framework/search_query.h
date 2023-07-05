/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2023 Wiimag inc. All rights reserved.
 */

#pragma once

#include <framework/common.h>

#include <foundation/hash.h>
#include <foundation/string.h>

struct search_result_t;
struct search_query_node_t;

typedef enum class SearchQueryError : unsigned char
{
    None,

    // Parsing errors
    UnexpectedGroupEnd,
    UnexpectedQuoteEnd,
    MissingOrRightOperand,
    MissingAndRightOperand,
    MissingNotRightOperand,
    MissingPropertyValue,
    MissingFunctionGroup,
    UnexpectedOperator,
    MissingLeftOperand,
    MissingRightOperand,
    UnexpectedOperand,
    UnexpectedToken,
    InvalidLeafNode,
    InvalidOperator,

    // Evaluation errors
    InvalidPropertyDeclaration
} search_parser_error_t;

typedef enum class SearchQueryEvalFlags
{
    None,
    Exclude     = 1 << 0,
    
    Word        = 1 << 1,
    Property    = 1 << 2,
    Function    = 1 << 3,

    OpLess      = 1 << 13,
    OpLessEq    = 1 << 14,
    OpEqual     = 1 << 15,
    OpGreaterEq = 1 << 16,
    OpGreater   = 1 << 17,
    OpNotEq     = 1 << 18,    
    OpContains  = 1 << 19,
    OpEval      = 1 << 20
} search_query_eval_flags_t;
DEFINE_ENUM_FLAGS(SearchQueryEvalFlags);

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

struct SearchQueryException
{
    search_parser_error_t error{ SearchQueryError::None };
    string_const_t        token{};
    char                  msg[256];

    SearchQueryException(search_parser_error_t err, string_const_t token, const char* fmt = nullptr, ...)
        : error(err)
        , token(token)
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_BUFFER(msg), fmt, string_length(msg), list);
        va_end(list);
    }
};

struct search_query_t
{
    string_t text{};
    search_query_node_t* root{ nullptr };

    bool completed{ false };
    search_result_t* results{ nullptr };
};

struct search_query_token_t
{
    search_query_token_type_t type{ SearchQueryTokenType::Undefined };

    string_const_t            name{};
    string_const_t            value{};
    string_const_t            identifier{};

    search_query_token_t* children{ nullptr };

};

struct search_result_t
{
    hash_t id{ 0 };
    int32_t score{ 0 };
};

typedef function<search_result_t*(
    string_const_t name,
    string_const_t value,
    search_query_eval_flags_t flags,
    search_result_t* and_set,
    void* user_data)> search_query_eval_handler_t;

FOUNDATION_FORCEINLINE bool operator==(const search_result_t& a, const search_result_t& b)
{
    return a.id == b.id;
}

FOUNDATION_FORCEINLINE bool operator==(const search_result_t& a, const hash_t& b)
{
    return a.id == b;
}

search_query_t* search_query_allocate(const char* text, size_t length);

void search_query_deallocate(search_query_t*& query);

search_query_token_t* search_query_parse_tokens(const char* text, size_t length);

void search_query_deallocate_tokens(search_query_token_t*& tokens);

const char* search_query_parse_block(const char* const tok, const char* const end, search_query_token_t*& tokens);

search_query_node_t* search_query_scan_operator_node(search_query_token_t* tokens);

search_result_t* search_query_evaluate(search_query_t* query, const search_query_eval_handler_t& handler, void* user_data);

const char* search_query_eval_flags_to_string(search_query_eval_flags_t flags);
