/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * Query language is a simple boolean expression of words, with optional
 * grouping and negation. The query is parsed into a tree of nodes, where
 * each node is either a word, a property=value pair, a group or a negation. The tree is then
 * traversed to build a list of words to search for.
 */

#include "search_query.h"

#include <framework/common.h>

#include <foundation/array.h>
#include <foundation/memory.h>

#include <ctype.h>

struct search_query_node_t;

typedef enum class SearchQueryNodeType : unsigned char
{
    None,
    
    // Leaf nodes
    Word,       // joe, "joe", "s p a c e s", (will)
    Property,   // property=value, property="value", property=(value), property:value
                // property>number, property<number, property>=number, property<=number, property!=number
    Function,   // function(arg1, arg2, .., argN)=value, function(arg1, arg2, .., argN):value
                // function(arg1, arg2, .., argN)>number, function(arg1, arg2, .., argN)<number
                // function(arg1, arg2, .., argN)>=number, function(arg1, arg2, .., argN)<=number
                // function(arg1, arg2, .., argN)!=number

    // Boolean operators
    And,    // joe and bob, joe and "bob" and sam     (and is implicit when not specified)
    Or,     // joe or sam, joe or "bob" or sam

    // Unary operator
    Not,    // -word, -"word", -property=value, not(joe or bob), -(smith and will)
    Root,   // Root node, not used in query
} search_query_node_type_t;

struct search_query_node_t
{
    search_query_node_type_t type{ SearchQueryNodeType::None };
    search_query_node_t*     left{ nullptr };
    search_query_node_t*     right{ nullptr };

    search_query_token_t*    token{ nullptr };
};

typedef enum class SearchParserError : unsigned char
{
    None,
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
    InvalidOperator
} search_parser_error_t;

struct SearchParserException
{
    search_parser_error_t error{ SearchParserError::None };
    string_const_t        token{};
    char                  msg[256];

    SearchParserException(search_parser_error_t err, string_const_t token, const char* fmt = nullptr, ...)
        : error(err)
        , token(token)
    {
        va_list list;
        va_start(list, fmt);
        string_vformat(STRING_CONST_BUFFER(msg), fmt, string_length(msg), list);
        va_end(list);
    }
};


//
// # PRIVATE
//

FOUNDATION_STATIC search_query_node_t* search_query_allocate_node(search_query_node_type_t type)
{
    search_query_node_t* node = (search_query_node_t*)memory_allocate(0, sizeof(search_query_node_t), 0, MEMORY_PERSISTENT);
    node->type = type;
    node->left = nullptr;
    node->right = nullptr;
    return node;
}

FOUNDATION_STATIC void search_query_deallocate_node(search_query_node_t*& node)
{
    if (node == nullptr)
        return;
    search_query_deallocate_node(node->left);
    search_query_deallocate_node(node->right);

    if (node->type == SearchQueryNodeType::Root)
        search_query_deallocate_tokens(node->token);

    memory_deallocate(node);
    node = nullptr;
}

FOUNDATION_STATIC const char* search_parse_find_end_quote(const char* tok, const char* end, char quote)
{
    const char* pos = tok;
    while (pos < end)
    {
        if (*pos == '\\' && (pos + 1) < end)
            ++pos;
        else if (*pos == quote)
            return pos;
        ++pos;
    }
    return end;
}

FOUNDATION_STATIC const char* search_parse_find_end_group(const char* tok, const char* end, char start_group_symbol = '(', char end_group_symbol = ')')
{
    const char* pos = tok;
    int group_count = 1;
    while (pos < end)
    {
        if (*pos == '\\' && (pos + 1) < end)
            ++pos;
        else if (*pos == start_group_symbol)
            ++group_count;
        else if (*pos == end_group_symbol)
        {
            --group_count;
            if (group_count == 0)
                return pos;
        }
        ++pos;
    }
    return end;
}

FOUNDATION_STATIC const char* search_query_parse_literal(const char* const tok, const char* const end, search_query_token_t*& tokens)
{
    // Check for quoted string
    if (*tok == '"' || *tok == '\'')
    {
        const char* tend = search_parse_find_end_quote(tok + 1, end, *tok);
        if (tend < end)
        {
            // Found end of string
            search_query_token_t token{ SearchQueryTokenType::Literal };
            token.token = string_const(tok + 1, tend - tok - 1);
            array_push(tokens, token);
            return tend + 1;
        }
        else
        {
            search_query_deallocate_tokens(tokens);
            throw SearchParserException(SearchParserError::UnexpectedQuoteEnd, string_const(tok, end - tok), "Unexpected end of quoted string");
        }
    }

    return tok;
}

FOUNDATION_STATIC const char* search_query_parse_variable(const char* tok, const char* const end, search_query_token_t*& tokens)
{
    // Find next space
    const char* pos = tok;
    while (pos < end && !isspace(*pos))
        ++pos;

    const size_t variable_length = (pos - tok);
    if (variable_length == 0)
        return end;

    // Check if word literal
    const char* next_pos = search_query_parse_literal(tok, end, tokens);
    if (next_pos > tok)
        return next_pos;

    // Check if property
    constexpr string_const_t property_operators[] = {
        { STRING_CONST("=") },
        { STRING_CONST(":") },
        { STRING_CONST("<") },
        { STRING_CONST(">") },
        { STRING_CONST("!=") },
        { STRING_CONST(">=") },
        { STRING_CONST("<=") },
    };
    for (size_t i = 0; i < ARRAY_COUNT(property_operators); ++i)
    {
        const string_const_t& op = property_operators[i];
        if (variable_length >= op.length)
        {
            const size_t property_operator_pos = string_find_first_of(tok, variable_length, op.str, op.length, 1);
            if (property_operator_pos != STRING_NPOS)
            {
                // Found property
                search_query_token_t property{ SearchQueryTokenType::Property };
                property.name = string_const(tok, property_operator_pos - 1);

                // Now the property value can be a word, literal or function, basically another variable
                const char* start_value_pos = tok + property_operator_pos + op.length;

                // Skip whitespace
                while (start_value_pos < end && isspace(*start_value_pos))
                    ++start_value_pos;

                if (start_value_pos == end)
                {
                    search_query_deallocate_tokens(tokens);
                    throw SearchParserException(SearchParserError::MissingPropertyValue, string_const(tok, end - tok), "Unexpected end of property value");
                }

                const char* end_value_pos = search_query_parse_variable(start_value_pos, end, property.children);
                if (end_value_pos == start_value_pos)
                {
                    search_query_deallocate_tokens(tokens);
                    search_query_deallocate_tokens(property.children);
                    throw SearchParserException(SearchParserError::MissingPropertyValue, string_const(tok, end - tok), "Unexpected end of property value");
                }

                property.token = string_const(tok, end_value_pos - tok);
                array_push(tokens, property);
                return end_value_pos;
            }
        }
    }

    // Check if function
    const size_t function_paran_pos = string_find(tok, variable_length, '(', 2);
    if (function_paran_pos != STRING_NPOS)
    {
        search_query_token_t function{};
        function.type = SearchQueryTokenType::Function;
        function.name = string_const(tok, function_paran_pos);

        // Now make sure we can parse the function (group)
        const char* start_group_pos = tok + function_paran_pos;
        const char* end_group_pos = search_query_parse_block(start_group_pos, end, function.children);
        if (end_group_pos == start_group_pos)
        {
            search_query_deallocate_tokens(tokens);
            search_query_deallocate_tokens(function.children);
            throw SearchParserException(SearchParserError::MissingFunctionGroup, string_const(tok, end - tok), "Unexpected end of function group");
        }

        function.token = string_const(tok, end_group_pos - tok);
        array_push(tokens, function);
        return end_group_pos;
    }

    // Finally lets assume we have a word!?
    search_query_token_t word{};
    word.type = SearchQueryTokenType::Word;
    word.token = string_const(tok, variable_length);
    array_push(tokens, word);
    FOUNDATION_ASSERT(pos > tok);
    return pos;
}

FOUNDATION_STATIC const char* search_query_parse_logical_opeartors(const char* const tok, const char* const end, search_query_token_t*& tokens)
{
    // Check for binary logical operators
    if (tok + 2 < end && (tok[0] == 'a' || tok[0] == 'A') && (tok[1] == 'n' || tok[1] == 'N') && (tok[2] == 'd' || tok[2] == 'D'))
    {
        search_query_token_t token{};
        token.type = SearchQueryTokenType::And;
        token.token = string_const(tok, 3);
        array_push(tokens, token);
        return tok + 3;
    }
    
    if (tok + 1 < end && (tok[0] == 'o' || tok[0] == 'O') && (tok[1] == 'r' || tok[1] == 'R'))
    {
        search_query_token_t token{};
        token.type = SearchQueryTokenType::Or;
        token.token = string_const(tok, 2);
        array_push(tokens, token);
        return tok + 2;
    }
    
    if (tok + 2 < end && (tok[0] == 'n' || tok[0] == 'N') && (tok[1] == 'o' || tok[1] == 'O') && (tok[2] == 't' || tok[2] == 'T'))
    {
        search_query_token_t token{ SearchQueryTokenType::Not };

        const char* negate_token_pos = tok + 3;
        while (negate_token_pos < end && isspace(*negate_token_pos))
            ++negate_token_pos;

        const char* next_tok = search_query_parse_block(negate_token_pos, end, token.children);
        if (next_tok > negate_token_pos)
        {
            token.token = string_const(negate_token_pos, next_tok - negate_token_pos);
            array_push(tokens, token);
            return next_tok;
        }

        next_tok = search_query_parse_variable(negate_token_pos, end, token.children);
        if (next_tok > negate_token_pos)
        {
            token.token = string_const(negate_token_pos, next_tok - negate_token_pos);
            array_push(tokens, token);
        }
        else
        {
            throw SearchParserException(SearchParserError::UnexpectedToken, string_const(tok, 1), "Unexpected token");
        }

        return next_tok;
    }

    return tok;
}

const char* search_query_parse_block(const char* const tok, const char* const end, search_query_token_t*& tokens)
{
    // Check for group
    if (*tok == '(')
    {
        const char* tend = search_parse_find_end_group(tok + 1, end, *tok, ')');
        if (tend < end)
        {
            // Found end of string
            search_query_token_t block{};
            block.type = SearchQueryTokenType::Group;
            block.token = string_const(tok + 1, tend - tok - 1);
            block.children = search_query_parse_tokens(STRING_ARGS(block.token));
            array_push(tokens, block);
            return tend + 1;
        }
        else
        {
            search_query_deallocate_tokens(tokens);
            throw SearchParserException(SearchParserError::UnexpectedGroupEnd, string_const(tok, end - tok), "Unexpected end of group");
        }
    }

    if (*tok == ')')
    {
        search_query_deallocate_tokens(tokens);
        throw SearchParserException(SearchParserError::UnexpectedGroupEnd, string_const(tok, end - tok), "Unexpected ')'");
    }

    // Check for quoted string
    return search_query_parse_literal(tok, end, tokens);
}

FOUNDATION_STATIC const char* search_query_node_type_name(SearchQueryNodeType type)
{
    switch (type)
    {   
        case SearchQueryNodeType::And: return "And";
        case SearchQueryNodeType::Or: return "Or";
        case SearchQueryNodeType::Not: return "Not";
        case SearchQueryNodeType::Word: return "Word";
        case SearchQueryNodeType::Property: return "Property";
        case SearchQueryNodeType::Function: return "Function";
        case SearchQueryNodeType::Root: return "Root";
        
        default: 
            return "Unknown";
    }
}

FOUNDATION_STATIC const char* search_query_token_type_name(SearchQueryTokenType type)
{
    switch (type)
    {   
        case SearchQueryTokenType::Undefined: return "Undefined";
        case SearchQueryTokenType::And: return "And";
        case SearchQueryTokenType::Or: return "Or";
        case SearchQueryTokenType::Not: return "Not";
        case SearchQueryTokenType::Word: return "Word";
        case SearchQueryTokenType::Literal: return "Literal";
        case SearchQueryTokenType::Property: return "Property";
        case SearchQueryTokenType::Function: return "Function";
        case SearchQueryTokenType::Group: return "Group";
        
        default: return "Unknown";
    }
}

FOUNDATION_STATIC void search_query_print_evaluation_order(search_query_node_t* node, int level = 0)
{
    if (!node)
        return;

    bool printed = false;
    if (node->type == SearchQueryNodeType::Or || node->type == SearchQueryNodeType::And || node->type == SearchQueryNodeType::Not)
    {
        log_infof(0, STRING_CONST("%*s%s: %s-%s"), level * 2, "", search_query_node_type_name(node->type), node->left ? "L" : "", node->right ? "R" : "");
        printed = true;
    }

    if (node->left)
        search_query_print_evaluation_order(node->left, level + 1);

    if (node->right)
        search_query_print_evaluation_order(node->right, level + 1);
    
    if (!printed)
    {
        if (node->token)
        {
            log_infof(0, STRING_CONST("%*s%s: %s-%s | %.*s"), level * 2, "", search_query_node_type_name(node->type),
                node->left ? "L" : "", node->right ? "R" : "", STRING_FORMAT(node->token->token));
        }
        else
            log_infof(0, STRING_CONST("%*s%s: %s-%s"), level * 2, "", search_query_node_type_name(node->type), node->left ? "L" : "", node->right ? "R" : "");
    }
}

FOUNDATION_STATIC void search_query_print_tokens(search_query_token_t* tokens, int level = 0)
{
    foreach(t, tokens)
    {
        log_infof(0, STRING_CONST("%*s%s: %.*s"), level * 2, "", search_query_token_type_name(t->type), STRING_FORMAT(t->token));
        
        if (t->children)
            search_query_print_tokens(t->children, level + 1);
    }
}

FOUNDATION_STATIC search_query_node_t* search_query_allocate_node(search_query_token_t* token)
{
    if (token == nullptr)
        return nullptr;

    if (token->type == SearchQueryTokenType::Group)
        return search_query_scan_operator_node(token->children);
        
    search_query_node_t* node = (search_query_node_t*)memory_allocate(0, sizeof(search_query_node_t), 8, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
    
    if (token->type == SearchQueryTokenType::Word || token->type == SearchQueryTokenType::Literal)
    {
        node->type = SearchQueryNodeType::Word;
        node->token = token;
        node->left = nullptr;
        node->right = nullptr;
        return node;
    }
    
    if (token->type == SearchQueryTokenType::Property)
    {
        node->type = SearchQueryNodeType::Property;
        node->token = token;
        node->left = nullptr;
        node->right = nullptr;
        return node;
    }
    
    if (token->type == SearchQueryTokenType::Function)
    {
        node->type = SearchQueryNodeType::Function;
        node->token = token;
        node->left = nullptr;
        node->right = nullptr;
        return node;
    }

    if (token->type == SearchQueryTokenType::Not)
    {
        node->type = SearchQueryNodeType::Not;
        node->token = token;
        node->left = search_query_scan_operator_node(token->children);
        node->right = nullptr;
        return node;
    }
    
    throw SearchParserException(SearchParserError::InvalidLeafNode, token->token, "Invalid leaf node");
}

search_query_node_t* search_query_scan_operator_node(search_query_token_t* tokens)
{
    search_query_node_t* node = nullptr;
    search_query_token_t* op_token = nullptr;
    search_query_token_t* left_token = nullptr;
    search_query_token_t* right_token = nullptr;
    for (size_t i = 0, end = array_size(tokens); i < end; ++i)
    {
        search_query_token_t* token = &tokens[i];

        if (token->type == SearchQueryTokenType::And || token->type == SearchQueryTokenType::Or)
        {
            if (op_token)
                throw SearchParserException(SearchParserError::UnexpectedOperator, token->token, "Unexpected operator");

            if (left_token == nullptr && node == nullptr)
                throw SearchParserException(SearchParserError::MissingLeftOperand, token->token, "Missing left operand");
                
            op_token = token;
        }
        else if (token->type == SearchQueryTokenType::Not ||
                 token->type == SearchQueryTokenType::Word || 
                 token->type == SearchQueryTokenType::Literal || 
                 token->type == SearchQueryTokenType::Property || 
                 token->type == SearchQueryTokenType::Function || 
                 token->type == SearchQueryTokenType::Group)
        {
            if (node == nullptr && left_token == nullptr)
                left_token = token;
            else if (right_token == nullptr)
                right_token = token;
            else
            {
                throw SearchParserException(SearchParserError::UnexpectedOperand, token->token, "Unexpected operand");
            }
        }
        else
        {
            throw SearchParserException(SearchParserError::UnexpectedToken, token->token, "Unexpected token");
        }

        if ((node && right_token) ||
            (left_token && right_token) ||
            ((left_token || node) && i + 1 >= end))
        {       
            search_query_node_t* prev = node;
            
            if (op_token == nullptr && left_token && right_token == nullptr)
            {
                return search_query_allocate_node(left_token);
            }
            else if (op_token == nullptr)
            {
                // We have an implicit and logical operation
                node = search_query_allocate_node(SearchQueryNodeType::And);
                node->token = nullptr;
            }
            else if (op_token->type == SearchQueryTokenType::And)
            {
                node = search_query_allocate_node(SearchQueryNodeType::And);
                node->token = op_token;
            }
            else if (op_token->type == SearchQueryTokenType::Or)
            {
                node = search_query_allocate_node(SearchQueryNodeType::Or);
                node->token = op_token;
            }
            else
            {
                throw SearchParserException(SearchParserError::InvalidOperator, op_token->token, "Invalid operator");
            }
            
            node->left = prev ? prev : search_query_allocate_node(left_token);
            if (right_token)
                node->right = search_query_allocate_node(right_token);

            op_token = nullptr;
            left_token = nullptr;
            right_token = nullptr;
        }        
    }

    FOUNDATION_ASSERT(node && "Failed to scan boolean node");
    return node;
}

FOUNDATION_STATIC search_query_node_t* search_query_parse_node(const char* text, size_t length)
{
    FOUNDATION_ASSERT(text);
    FOUNDATION_ASSERT(length > 0);

    // Parse query into tree
    search_query_node_t* root = search_query_allocate_node(SearchQueryNodeType::Root);
    search_query_token_t* tokens = search_query_parse_tokens(text, length);

    {
        LOG_PREFIX(false);
        search_query_print_tokens(tokens);
    }

    root->token = tokens;
    root->right = nullptr;

    // Now we have a list of tokens, lets parse them into a tree
    root->left = search_query_scan_operator_node(tokens);

    {
        LOG_PREFIX(false);
        search_query_print_evaluation_order(root->left);
    }

    return root;
}

//
// # PUBLIC
//

void search_query_deallocate_tokens(search_query_token_t*& tokens)
{
    for (size_t i = 0, end = array_size(tokens); i < end; ++i)
    {
        search_query_token_t& token = tokens[i];
        if (token.children)
            search_query_deallocate_tokens(token.children);
    }
    array_deallocate(tokens);
}

search_query_token_t* search_query_parse_tokens(const char* text, size_t length)
{
    search_query_token_t* tokens = nullptr;

    const char* keywords[] = { "and", "or", "not", "(", ")", "-", "\"", "'", ":", "=", ">", "<", "!" };

    // Parse tokens
    const char* tok = text;
    const char* end = text + length;

    while (tok < end)
    {
        // Skip whitespace
        while (tok < end && isspace(*tok))
            ++tok;

        // Check for end of string
        if (tok >= end)
            break;

        // Check for negation
        if (*tok == '-')
        {
            search_query_token_t nott{ SearchQueryTokenType::Not };

            const char* negate_token_pos = tok + 1;
            while (negate_token_pos < end && isspace(*negate_token_pos))
                ++negate_token_pos;

            const char* next_tok = search_query_parse_block(negate_token_pos, end, nott.children);
            if (next_tok > negate_token_pos)
            {
                nott.token = string_const(negate_token_pos, next_tok - negate_token_pos);
                array_push(tokens, nott);
                tok = next_tok;
                continue;
            }
            
            next_tok = search_query_parse_variable(negate_token_pos, end, nott.children);
            if (next_tok > negate_token_pos)
            {
                nott.token = string_const(negate_token_pos, next_tok - negate_token_pos);
                array_push(tokens, nott);
                tok = next_tok;
                continue;
            }
            else
            {
                throw SearchParserException(SearchParserError::UnexpectedToken, string_const(tok, 1), "Unexpected token");
            }
        }

        const char* next_tok = search_query_parse_block(tok, end, tokens);
        if (next_tok > tok)
        {
            tok = next_tok;
            continue;
        }

        next_tok = search_query_parse_logical_opeartors(tok, end, tokens);
        if (next_tok > tok)
        {
            tok = next_tok;
            continue;
        }

        // What remains are either words, functions or properties.
        tok = search_query_parse_variable(tok, end, tokens);
    }

    return tokens;
}

search_query_t* search_query_allocate(const char* text, size_t length)
{
    search_query_t* query = (search_query_t*)memory_allocate(0, sizeof(search_query_t), 8, MEMORY_PERSISTENT);
    query->completed = false;
    query->document_count = 0;
    query->text = string_clone(text, length);
    
    // Parse query into tree
    query->root = search_query_parse_node(query->text.str, query->text.length); 
    
    return query;
}

void search_query_deallocate(search_query_t*& query)
{
    if (!query)
        return;

    search_query_deallocate_node(query->root);
    string_deallocate(query->text);
    memory_deallocate(query);
    query = nullptr;
}
