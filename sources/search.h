/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/common.h>

#include <foundation/hash.h>

struct search_index_t;
struct search_database_t;
typedef uint32_t search_document_handle_t;
typedef uint32_t search_query_handle_t;

#define SEARCH_DOCUMENT_INVALID_ID (0)

typedef enum class SearchDatabaseFlags : uint32_t {
    
    None                    = 0,
    CaseSensitive           = 1 << 0,
    DoNotIndexVariations    = 1 << 1, // TODO
    IndexDocumentName       = 1 << 2, // TODO
    IndexDocumentSource     = 1 << 3, // TODO

    Default = None

} search_database_flags_t;
DEFINE_ENUM_FLAGS(SearchDatabaseFlags);

search_database_t* search_database_allocate(search_database_flags_t flags = SearchDatabaseFlags::None);

void search_database_deallocate(search_database_t*& database);

search_document_handle_t search_database_add_document(
    search_database_t* database, const char* name, size_t name_length);

bool search_database_index_word(
    search_database_t* database, 
    search_document_handle_t document, 
    const char* word, size_t word_length, 
    bool include_variations = true);

bool search_database_index_text(
    search_database_t* database, search_document_handle_t document, 
    const char* text, size_t text_length, 
    bool include_variations = true);

bool search_database_index_exact_match(
    search_database_t* database, search_document_handle_t document, 
    const char* word, size_t word_length, 
    bool case_sensitive = false);

bool search_database_index_property(
    search_database_t* database, search_document_handle_t document, 
    const char* name, size_t name_length, 
    double value);

bool search_database_index_property(
    search_database_t* database, search_document_handle_t document, 
    const char* name, size_t name_length, 
    const char* value, size_t value_length,
    bool include_variations = true);

uint32_t search_database_index_count(search_database_t* database);

uint32_t search_database_document_count(search_database_t* database);

uint32_t search_database_word_document_count(
    search_database_t* database, 
    const char* word, size_t word_length, 
    bool include_variations = false);

uint32_t search_database_word_count(search_database_t* database);

bool search_database_remove_document(search_database_t* database, search_document_handle_t document);

bool search_database_is_document_valid(search_database_t* database, search_document_handle_t document);

bool search_database_contains_word(search_database_t* database, const char* word, size_t word_length);

search_query_handle_t search_database_query(search_database_t* database, const char* query, size_t query_length);
