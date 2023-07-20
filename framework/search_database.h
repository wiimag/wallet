/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2023 Wiimag inc. All rights reserved.
 */

#pragma once

#include <framework/common.h>
#include <framework/search_query.h>

#include <foundation/hash.h>

struct stream_t;
struct search_index_t;
struct search_database_t;

typedef uint32_t search_query_handle_t;
typedef uint32_t search_document_handle_t;

constexpr uint32_t SEARCH_QUERY_INVALID_ID = (0);
constexpr uint32_t SEARCH_DOCUMENT_INVALID_ID = (0);
constexpr uint8_t SEARCH_DOCUMENT_MAX_NAME_LENGTH = (64);
constexpr uint8_t SEARCH_INDEX_WORD_MAX_LENGTH = (64);

typedef enum class SearchDatabaseFlags : uint32_t {
    
    None                    = 0,
    CaseSensitive           = 1 << 0,
    DoNotIndexVariations    = 1 << 1, // TODO
    IndexDocumentName       = 1 << 2, // TODO
    IndexDocumentSource     = 1 << 3, // TODO

    SkipCommonWords         = 1 << 4,

    Default = None

} search_database_flags_t;
DEFINE_ENUM_FLAGS(SearchDatabaseFlags);

FOUNDATION_FORCEINLINE bool operator==(const search_result_t& result, const search_document_handle_t& id)
{
    return result.id == id;
}

search_database_t* search_database_allocate(search_database_flags_t flags = SearchDatabaseFlags::None);

void search_database_deallocate(search_database_t*& database);

search_document_handle_t search_database_add_document(
    search_database_t* database, const char* name, size_t name_length);

search_document_handle_t search_database_get_or_add_document(
    search_database_t* database, const char* name, size_t name_length);

search_document_handle_t search_database_find_document(
    search_database_t* database, const char* name, size_t name_length);

bool search_database_is_dirty(search_database_t* database);

time_t search_database_document_timestamp(search_database_t* database, search_document_handle_t document);

bool search_database_document_update_timestamp(search_database_t* database, search_document_handle_t document, time_t timestamp = 0);

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

string_const_t search_database_document_name(search_database_t* database, search_document_handle_t document);

uint32_t search_database_word_document_count(
    search_database_t* database, 
    const char* word, size_t word_length, 
    bool include_variations = false);

uint32_t search_database_word_count(search_database_t* database);

bool search_database_remove_document(search_database_t* database, search_document_handle_t document);

bool search_database_is_document_valid(search_database_t* database, search_document_handle_t document);

bool search_database_contains_word(search_database_t* database, const char* word, size_t word_length);

search_query_handle_t search_database_query(search_database_t* database, const char* query, size_t query_length);

bool search_database_query_is_completed(search_database_t* database, search_query_handle_t query);

const search_result_t* search_database_query_results(search_database_t* database, search_query_handle_t query);

bool search_database_query_dispose(search_database_t* database, search_query_handle_t query);

bool search_database_load(search_database_t* database, stream_t* stream);

bool search_database_save(search_database_t* database, stream_t* stream);

string_t* search_database_property_keywords(search_database_t* database);

/*! Print statistics about the search database to the console.
 *
 *  @param database The search database to print statistics for.
 */
void search_database_print_stats(search_database_t* database);

/*! Remove database documents that are older than #reference.
 *
 *  @param database The search database to remove old documents from.
 *  @param reference The reference time to compare document timestamps against.
 *  @param timeout_seconds The timeout in seconds after which we stop removing documents. If 0.0 then we remove all documents.
 *
 *  @return True if any documents were removed and the timeout was not reached, false otherwise.
 */
bool search_database_remove_old_documents(search_database_t* database, time_t reference, double timeout_seconds = 0.0);

/*! Remove dangling documents from the database.
 *
 *  @param database The search database to remove dangling documents from.
 *
 *  @return True if any documents were removed, false otherwise.
 */
void search_database_cleanup_up(search_database_t* database);
