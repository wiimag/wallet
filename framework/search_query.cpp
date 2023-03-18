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
#include <framework/array.h>
#include <framework/string.h>

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

//
// # PRIVATE
//

FOUNDATION_STATIC search_query_node_t* search_query_allocate_node(search_query_node_type_t type)
{
    search_query_node_t* node = (search_query_node_t*)memory_allocate(0, sizeof(search_query_node_t), 0, MEMORY_PERSISTENT);
    node->type = type;
    node->left = nullptr;
    node->right = nullptr;
    node->token = nullptr;
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
        const size_t length = tend - tok - 1;
        if (tend < end && length > 0)
        {
            // Found end of string
            search_query_token_t token{ SearchQueryTokenType::Literal };
            token.value = string_const(tok + 1, length);

            token.identifier = string_const(tok, tend - tok);
            array_push(tokens, token);
            return tend + 1;
        }
        else
        {
            search_query_deallocate_tokens(tokens);
            throw SearchQueryException(SearchQueryError::UnexpectedQuoteEnd, string_const(tok, end - tok), "Unexpected end of quoted string");
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
        { STRING_CONST(":") },
        { STRING_CONST("!=") },
        { STRING_CONST(">=") },
        { STRING_CONST("<=") },
        { STRING_CONST("=") },
        { STRING_CONST("<") },
        { STRING_CONST(">") },
    };
    for (size_t i = 0; i < ARRAY_COUNT(property_operators); ++i)
    {
        const string_const_t& op = property_operators[i];
        if (variable_length >= op.length)
        {
            const size_t property_operator_pos = string_find_string(tok, variable_length, op.str, op.length, 1);
            if (property_operator_pos != STRING_NPOS)
            {
                // Found property
                search_query_token_t property{ SearchQueryTokenType::Property };
                property.name = string_const(tok, property_operator_pos);

                // Now the property value can be a word, literal or function, basically another variable
                const char* start_value_pos = tok + property_operator_pos + op.length;

                // Skip whitespace
                while (start_value_pos < end && isspace(*start_value_pos))
                    ++start_value_pos;

                if (start_value_pos == end)
                {
                    search_query_deallocate_tokens(tokens);
                    throw SearchQueryException(SearchQueryError::MissingPropertyValue, string_const(tok, end - tok), "Unexpected end of property value");
                }

                try
                {
                    const char* end_value_pos = search_query_parse_variable(start_value_pos, end, property.children);
                    if (end_value_pos == start_value_pos)
                    {
                        search_query_deallocate_tokens(tokens);
                        search_query_deallocate_tokens(property.children);
                        throw SearchQueryException(SearchQueryError::MissingPropertyValue, string_const(tok, end - tok), "Unexpected end of property value");
                    }

                    property.identifier = string_const(tok, end_value_pos - tok);

                    if (*start_value_pos == '"' && *(end_value_pos - 1) == '"')
                    {
                        property.value = string_const(start_value_pos + 1, end_value_pos - start_value_pos - 2);
                    }
                    else
                    {
                        property.value = string_const(start_value_pos, end_value_pos - start_value_pos);
                    }


                    array_push(tokens, property);
                    return end_value_pos;
                }
                catch (SearchQueryException ex)
                {
                    search_query_deallocate_tokens(tokens);
                    throw ex;
                }
            }
        }
    }

    // Check if function
    const size_t function_paran_pos = string_find(tok, variable_length, '(', 2);
    if (function_paran_pos != STRING_NPOS)
    {
        search_query_token_t function{ SearchQueryTokenType::Function };
        function.name = string_const(tok, function_paran_pos);

        try
        {
            // Now make sure we can parse the function (group)
            const char* start_group_pos = tok + function_paran_pos;
            const char* end_group_pos = search_query_parse_block(start_group_pos, end, function.children);
            if (end_group_pos == start_group_pos)
            {
                search_query_deallocate_tokens(tokens);
                search_query_deallocate_tokens(function.children);
                throw SearchQueryException(SearchQueryError::MissingFunctionGroup, string_const(tok, end - tok), "Unexpected end of function group");
            }

            function.identifier = string_const(tok, end_group_pos - tok);
            function.value = string_const(start_group_pos + 1, end_group_pos - start_group_pos - 2);
            array_push(tokens, function);
            return end_group_pos;
        }
        catch (SearchQueryException err)
        {
            search_query_deallocate_tokens(tokens);
            search_query_deallocate_tokens(function.children);
            throw err;
        }
    }

    // Finally lets assume we have a word!?
    search_query_token_t word{ SearchQueryTokenType::Word };
    word.value = word.identifier = string_const(tok, variable_length);
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
        token.identifier = string_const(tok, 3);
        array_push(tokens, token);
        return tok + 3;
    }
    
    if (tok + 1 < end && (tok[0] == 'o' || tok[0] == 'O') && (tok[1] == 'r' || tok[1] == 'R'))
    {
        search_query_token_t token{};
        token.type = SearchQueryTokenType::Or;
        token.identifier = string_const(tok, 2);
        array_push(tokens, token);
        return tok + 2;
    }
    
    if (tok + 2 < end && (tok[0] == 'n' || tok[0] == 'N') && (tok[1] == 'o' || tok[1] == 'O') && (tok[2] == 't' || tok[2] == 'T'))
    {
        search_query_token_t token{ SearchQueryTokenType::Not };

        const char* negate_token_pos = tok + 3;
        while (negate_token_pos < end && isspace(*negate_token_pos))
            ++negate_token_pos;

        try
        {
            const char* next_tok = search_query_parse_block(negate_token_pos, end, token.children);
            if (next_tok > negate_token_pos)
            {
                token.identifier = string_const(negate_token_pos, next_tok - negate_token_pos);
                array_push(tokens, token);
                return next_tok;
            }

            next_tok = search_query_parse_variable(negate_token_pos, end, token.children);
            if (next_tok > negate_token_pos)
            {
                token.identifier = string_const(negate_token_pos, next_tok - negate_token_pos);
                array_push(tokens, token);
            }
            else
            {
                throw SearchQueryException(SearchQueryError::UnexpectedToken, string_const(tok, 1), "Unexpected token");
            }

            return next_tok;
        }
        catch (SearchQueryException err)
        {
            search_query_deallocate_tokens(tokens);
            search_query_deallocate_tokens(token.children);
            throw err;
        }
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
            block.identifier = string_const(tok + 1, tend - tok - 1);

            try
            {
                block.children = search_query_parse_tokens(STRING_ARGS(block.identifier));
            }
            catch (SearchQueryException err)
            {
                search_query_deallocate_tokens(tokens);
                throw err;
            }
            
            array_push(tokens, block);
            return tend + 1;
        }
        else
        {
            search_query_deallocate_tokens(tokens);
            throw SearchQueryException(SearchQueryError::UnexpectedGroupEnd, string_const(tok, end - tok), "Unexpected end of group");
        }
    }

    if (*tok == ')')
    {
        search_query_deallocate_tokens(tokens);
        throw SearchQueryException(SearchQueryError::UnexpectedGroupEnd, string_const(tok, end - tok), "Unexpected ')'");
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
        log_debugf(0, STRING_CONST("%*s%s: %s-%s"), level * 2, "", search_query_node_type_name(node->type), node->left ? "L" : "", node->right ? "R" : "");
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
            log_debugf(0, STRING_CONST("%*s%s: %s-%s | %.*s"), level * 2, "", search_query_node_type_name(node->type),
                node->left ? "L" : "", node->right ? "R" : "", STRING_FORMAT(node->token->identifier));
        }
        else
            log_debugf(0, STRING_CONST("%*s%s: %s-%s"), level * 2, "", search_query_node_type_name(node->type), node->left ? "L" : "", node->right ? "R" : "");
    }
}

FOUNDATION_STATIC void search_query_print_tokens(search_query_token_t* tokens, int level = 0)
{
    foreach(t, tokens)
    {
        log_debugf(0, STRING_CONST("%*s%s: %.*s"), level * 2, "", search_query_token_type_name(t->type), STRING_FORMAT(t->identifier));
        
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
    
    throw SearchQueryException(SearchQueryError::InvalidLeafNode, token->identifier, "Invalid leaf node");
}

FOUNDATION_STATIC search_result_t* search_query_merge_sets(search_result_t*& lhs, search_result_t*& rhs)
{
    if (!lhs)
        return rhs;
    if (!rhs)
        return lhs;

    search_result_t* results = nullptr;
    array_copy(results, lhs);
    array_deallocate(lhs);

    foreach(e, rhs)
    {
        bool exists = false;
        foreach(r, results)
        {
            if (r->id == e->id)
            {
                exists = true;
                break;
            }
        }

        if (!exists)
            array_push_memcpy(results, e);
    }

    array_deallocate(rhs);
    return results;
}

FOUNDATION_STATIC search_result_t* search_query_evaluate_node(
    search_query_node_t* node, 
    const search_query_eval_handler_t& handler, 
    search_result_t* and_set, bool exclude, void* user_data)
{
    if (!node)
        return nullptr;

    SearchQueryEvalFlags eval_flags = exclude ? SearchQueryEvalFlags::Exclude : SearchQueryEvalFlags::None;

    if (node->type == SearchQueryNodeType::Word)
    {
        FOUNDATION_ASSERT(node->token->type == SearchQueryTokenType::Word || node->token->type == SearchQueryTokenType::Literal);
        FOUNDATION_ASSERT(node->token->value.str && node->token->value.length > 0);
        
        FOUNDATION_ASSERT(node->left == nullptr);
        FOUNDATION_ASSERT(node->right == nullptr);
        FOUNDATION_ASSERT(node->token->children == nullptr);
        
        eval_flags |= SearchQueryEvalFlags::Word;

        if (node->token->type == SearchQueryTokenType::Literal)
            eval_flags |= SearchQueryEvalFlags::OpEqual;
        else
            eval_flags |= SearchQueryEvalFlags::OpContains;

        return handler(node->token->name, node->token->value, eval_flags, and_set, user_data);
    }
    else if (node->type == SearchQueryNodeType::Property)
    {
        FOUNDATION_ASSERT(node->token->type == SearchQueryTokenType::Property);
        FOUNDATION_ASSERT(node->token->name.str && node->token->name.length > 0);
        FOUNDATION_ASSERT(node->token->value.str && node->token->value.length > 0);
        FOUNDATION_ASSERT(node->token->identifier.str && node->token->identifier.length > 0);

        if (array_size(node->token->children) == 0)
        {
            throw SearchQueryException(SearchQueryError::InvalidPropertyDeclaration, node->token->identifier,
                "A property must have a value to evaluate after the operator (i.e. property>=value)");
        }

        FOUNDATION_ASSERT(node->left == nullptr);
        FOUNDATION_ASSERT(node->right == nullptr);
        if (node->token->children[0].type != SearchQueryTokenType::Word &&
            node->token->children[0].type != SearchQueryTokenType::Literal)
        {
            throw SearchQueryException(SearchQueryError::InvalidPropertyDeclaration, node->token->identifier, 
                "Invalid property declaration, property only support word or literal as the right hand side");
        }
        
        string_const_t op_token = { 
            node->token->identifier.str + node->token->name.length, 
            to_size(node->token->value.str - (node->token->name.str + node->token->name.length))
        };

        if (op_token.str[op_token.length-1] == '"')
            op_token.length--;

        eval_flags |= SearchQueryEvalFlags::Property;
        if (string_equal(STRING_ARGS(op_token), STRING_CONST("=")))
            eval_flags |= SearchQueryEvalFlags::OpEqual;
        else if (string_equal(STRING_ARGS(op_token), STRING_CONST(":")))
            eval_flags |= SearchQueryEvalFlags::OpContains;
        else if (string_equal(STRING_ARGS(op_token), STRING_CONST("!=")))
            eval_flags |= SearchQueryEvalFlags::OpNotEq;
        else if (string_equal(STRING_ARGS(op_token), STRING_CONST(">")))
            eval_flags |= SearchQueryEvalFlags::OpGreater;
        else if (string_equal(STRING_ARGS(op_token), STRING_CONST(">=")))
            eval_flags |= SearchQueryEvalFlags::OpGreaterEq;
        else if (string_equal(STRING_ARGS(op_token), STRING_CONST("<")))
            eval_flags |= SearchQueryEvalFlags::OpLess;
        else if (string_equal(STRING_ARGS(op_token), STRING_CONST("<=")))
            eval_flags |= SearchQueryEvalFlags::OpLessEq;
        else
            throw SearchQueryException(SearchQueryError::InvalidOperator, op_token, "Invalid operator");
            
        return handler(node->token->name, node->token->value, eval_flags, and_set, user_data);
    }
    else if (node->type == SearchQueryNodeType::Function)
    {
        FOUNDATION_ASSERT(node->left == nullptr);
        FOUNDATION_ASSERT(node->right == nullptr);
        FOUNDATION_ASSERT(node->token->children && node->token->children[0].type == SearchQueryTokenType::Group);

        if (node->token->value.length == 0)
            return nullptr; // TODO: Validate this?
        
        FOUNDATION_ASSERT(node->token->type == SearchQueryTokenType::Function);
        FOUNDATION_ASSERT(node->token->name.str && node->token->name.length > 0);
        FOUNDATION_ASSERT(node->token->value.str && node->token->value.length > 0);
        FOUNDATION_ASSERT(node->token->identifier.str && node->token->identifier.length > 0);

        eval_flags |= SearchQueryEvalFlags::Function | SearchQueryEvalFlags::OpEval;
        return handler(node->token->name, node->token->value, eval_flags, and_set, user_data);
    }
    else if (node->type == SearchQueryNodeType::Not)
    {
        FOUNDATION_ASSERT(node->right == nullptr);

        if (and_set)
        {
            // Remove from the and set the left results that are negated
            search_result_t* results = nullptr;
            search_result_t* left = search_query_evaluate_node(node->left, handler, nullptr, false, user_data);
            
            foreach(e, and_set)
            {
                if (!array_contains(left, *e))
                    array_push_memcpy(results, e);
            }

            array_deallocate(left);
            return results;
        }

        return search_query_evaluate_node(node->left, handler, nullptr, true, user_data);
    }
    else if (node->type == SearchQueryNodeType::And)
    {        
        search_result_t* left = search_query_evaluate_node(node->left, handler, and_set, exclude, user_data);
        search_result_t* right = search_query_evaluate_node(node->right, handler, left, exclude, user_data);
        array_deallocate(left);
        return right;
    }
    else if (node->type == SearchQueryNodeType::Or)
    {
        search_result_t* left = search_query_evaluate_node(node->left, handler, and_set, exclude, user_data);
        search_result_t* right = search_query_evaluate_node(node->right, handler, and_set, exclude, user_data);
        return search_query_merge_sets(left, right);
    }
    else if (node->type == SearchQueryNodeType::Root)
    {
        FOUNDATION_ASSERT(and_set == nullptr);
        FOUNDATION_ASSERT(exclude == false);
        return search_query_evaluate_node(node->left, handler, nullptr, false, user_data);
    }

    FOUNDATION_ASSERT_FAIL("Node type evaluation not implemented");
    return nullptr;
}

const char* search_query_eval_flags_to_string(search_query_eval_flags_t flags)
{
    if (flags == SearchQueryEvalFlags::None)
        return "None";

    static thread_local char buffer[256]; buffer[0] = 0;
    string_t str{ buffer, 0 };

    if (any(flags, SearchQueryEvalFlags::Exclude))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("Exclude | "));

    FOUNDATION_ASSERT(one(flags, SearchQueryEvalFlags::Word | SearchQueryEvalFlags::Property | SearchQueryEvalFlags::Function));

    if (any(flags, SearchQueryEvalFlags::Word))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("Word | "));
    else if (any(flags, SearchQueryEvalFlags::Property))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("Property | "));
    else if (any(flags, SearchQueryEvalFlags::Function))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("Function | "));

    FOUNDATION_ASSERT(one(flags, 
        SearchQueryEvalFlags::OpContains | SearchQueryEvalFlags::OpEqual | SearchQueryEvalFlags::OpNotEq | 
        SearchQueryEvalFlags::OpLess | SearchQueryEvalFlags::OpGreater | SearchQueryEvalFlags::OpLessEq | 
        SearchQueryEvalFlags::OpGreaterEq | SearchQueryEvalFlags::OpEval));

    if (any(flags, SearchQueryEvalFlags::OpContains))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("Contains"));
    else if (any(flags, SearchQueryEvalFlags::OpEqual))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("Equals"));
    else if (any(flags, SearchQueryEvalFlags::OpNotEq))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("Not Equal"));
    else if (any(flags, SearchQueryEvalFlags::OpLess))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("Less"));
    else if (any(flags, SearchQueryEvalFlags::OpLessEq))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("LessOrEqual"));
    else if (any(flags, SearchQueryEvalFlags::OpGreater))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("Greater"));
    else if (any(flags, SearchQueryEvalFlags::OpGreaterEq))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("GreaterOrEqual"));
    else if (any(flags, SearchQueryEvalFlags::OpEval))
        str = string_concat(STRING_BUFFER(buffer), STRING_ARGS(str), STRING_CONST("Eval"));
    else 
        FOUNDATION_ASSERT_FAIL("Unknown operator");

    return str.str;
}

search_result_t* search_query_evaluate(search_query_t* query, const search_query_eval_handler_t& handler, void* user_data)
{
    if (!query || !query->root)
        return nullptr;

    FOUNDATION_ASSERT(query->root && query->root->right == nullptr);
    return search_query_evaluate_node(query->root->left, handler, nullptr, false, user_data);
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
                throw SearchQueryException(SearchQueryError::UnexpectedOperator, token->identifier, "Unexpected operator");

            if (left_token == nullptr && node == nullptr)
                throw SearchQueryException(SearchQueryError::MissingLeftOperand, token->identifier, "Missing left operand");
                
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
                throw SearchQueryException(SearchQueryError::UnexpectedOperand, token->identifier, "Unexpected operand");
            }
        }
        else
        {
            throw SearchQueryException(SearchQueryError::UnexpectedToken, token->identifier, "Unexpected token");
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
                throw SearchQueryException(SearchQueryError::InvalidOperator, op_token->identifier, "Invalid operator");
            }
            
            node->left = prev ? prev : search_query_allocate_node(left_token);
            if (right_token)
                node->right = search_query_allocate_node(right_token);

            op_token = nullptr;
            left_token = nullptr;
            right_token = nullptr;
        }        
    }

    //FOUNDATION_ASSERT(node && "Failed to scan boolean node");
    return node;
}

FOUNDATION_STATIC search_query_node_t* search_query_parse_node(const char* text, size_t length)
{
    FOUNDATION_ASSERT(text);
    FOUNDATION_ASSERT(length > 0);

    // Parse query into tree
    search_query_token_t* tokens = nullptr;
    search_query_node_t* root = search_query_allocate_node(SearchQueryNodeType::Root);
    
    try
    {
        tokens = search_query_parse_tokens(text, length);
    }
    catch (SearchQueryException err)
    {
        search_query_deallocate_node(root);
        log_warnf(0, WARNING_INVALID_VALUE, STRING_CONST("Failed to parse query `%.*s`: %s at %.*s"), to_int(length), text, err.msg, STRING_FORMAT(err.token));
        throw err;
    }
    
    #if BUILD_DEBUG
    {
        LOG_PREFIX(false);
        search_query_print_tokens(tokens);
    }
    #endif

    root->token = tokens;
    root->right = nullptr;

    // Now we have a list of tokens, lets parse them into a tree
    root->left = search_query_scan_operator_node(tokens);

    #if BUILD_DEBUG
    {
        LOG_PREFIX(false);
        search_query_print_evaluation_order(root->left);
    }
    #endif

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
                nott.identifier = string_const(negate_token_pos, next_tok - negate_token_pos);
                array_push(tokens, nott);
                tok = next_tok;
                continue;
            }
            
            next_tok = search_query_parse_variable(negate_token_pos, end, nott.children);
            if (next_tok > negate_token_pos)
            {
                nott.identifier = string_const(negate_token_pos, next_tok - negate_token_pos);
                array_push(tokens, nott);
                tok = next_tok;
                continue;
            }
            else
            {
                search_query_deallocate_tokens(tokens);
                throw SearchQueryException(SearchQueryError::UnexpectedToken, string_const(tok, 1), "Unexpected token");
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
    // Try to parse query. This might throw an exception that must be catched by user code.
    search_query_node_t* root = search_query_parse_node(text, length);
    FOUNDATION_ASSERT(root);
    
    search_query_t* query = (search_query_t*)memory_allocate(0, sizeof(search_query_t), 8, MEMORY_PERSISTENT);
    query->text = string_clone(text, length);
    
    query->completed = false;
    query->results = nullptr;
    
    FOUNDATION_ASSERT(root);
    query->root = root;
    
    return query;
}

void search_query_deallocate(search_query_t*& query)
{
    if (!query)
        return;

    array_deallocate(query->results);
    search_query_deallocate_node(query->root);
    string_deallocate(query->text);
    memory_deallocate(query);
    query = nullptr;
}
