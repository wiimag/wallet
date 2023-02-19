/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/hash.h>

struct search_index_t;
struct search_database_t;

typedef uint32_t search_document_handle_t;

search_database_t* search_database_allocate();

void search_database_deallocate(search_database_t*& database);

search_document_handle_t search_database_add_document(search_database_t* database, const char* name, size_t name_length);

void search_database_index_word(search_database_t* database, search_document_handle_t document, const char* word, size_t word_length);

uint32_t search_database_index_count(search_database_t* database);

uint32_t search_database_document_count(search_database_t* database);

uint32_t search_database_word_document_count(search_database_t* database, const char* word, size_t word_length, bool include_variations = false);
