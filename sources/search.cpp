/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "search.h"

#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/service.h>
#include <framework/database.h>
#include <framework/dispatcher.h>

#define HASH_SEARCH static_hash_string("search", 6, 0xc9d4e54fbae76425ULL)

#define SEARCH_DOCUMENT_INVALID_ID (0)

typedef enum class SearchIndexType : uint32_t
{
    Undefined   = 0,
    Word        = 1 << 0,
    Variation   = 1 << 1,
    Number      = 1 << 2,
    Property    = 1 << 3
} search_index_type_t;
DEFINE_ENUM_FLAGS(SearchIndexType);

typedef enum class SearchDocumentType : uint32_t
{
    Unused = 0,
    Default = 1 << 0,
    Root = 1 << 1,

} search_document_type_t;

FOUNDATION_ALIGNED_STRUCT(search_index_key_t, 8)
{
    /*! Type of the index entry */
    search_index_type_t type{ SearchIndexType::Undefined };

    /*! Value correction code (can be length, property key hash, etc.) */
    hash_t crc{ 0 };

    /*! Key of the index entry. This key is usually hashed from the value content. */
    union {
        hash_t hash{ 0 };
        double number;
    };

    /*! Score of the entry (used for sorting results only) */
    int32_t score{ 0 };
};

FOUNDATION_ALIGNED_STRUCT(search_index_t, 8)
{
    search_index_key_t  key;
    
    uint32_t document_count{ 0 };
    union {
        search_document_handle_t  doc;
        search_document_handle_t  docs[3];
        search_document_handle_t* docs_list{ nullptr };
    };
};

struct search_document_t
{
    search_document_type_t  type{ SearchDocumentType::Unused };
    string_t                name{};
    string_t                source{};
};

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL int search_index_key_compare(const search_index_key_t& s, const search_index_key_t& key)
{
    if (s.type < key.type)
        return -1;
        
    if (s.type > key.type)
        return 1;

    if (s.crc < key.crc)
        return -1;

    if (s.crc > key.crc)
        return 1;

    if (s.type == SearchIndexType::Number)
    {
        if (s.number < key.number)
            return -1;

        if (s.number > key.number)
            return 1;
    }
    else
    {
        if (s.hash < key.hash)
            return -1;

        if (s.hash > key.hash)
            return 1;
    }
    
    return 0;
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL int search_index_compare(const search_index_t& s, const search_index_key_t& key)
{
    return search_index_key_compare(s.key, key);
}

struct search_database_t
{
    shared_mutex        mutex;
    search_index_t*     indexes;
    search_document_t*  documents;   
};

//
// # PRIVATE
//

FOUNDATION_STATIC string_const_t search_database_format_word(const char* word, size_t& word_length)
{
    FOUNDATION_ASSERT(word && word_length > 0);

    // By default ignore s or es word ending
    // TODO: Make this an indexing option
    if (word_length >= 4)
    {
        if ((word[word_length - 1] == 's') || (word[word_length - 1] == 'S'))
        {
            --word_length;
            if ((word[word_length - 1] == 'e') || (word[word_length - 1] == 'E'))
                --word_length;
        }
    }

    static thread_local char word_lower_buffer[32];
    string_t word_lower = string_to_lower_utf8(STRING_CONST_BUFFER(word_lower_buffer), word, word_length);
    return string_to_const(word_lower);
}

FOUNDATION_STATIC int search_database_find_index(search_database_t* db, const search_index_key_t& key)
{
    return array_binary_search_compare(db->indexes, key, search_index_compare);
}

FOUNDATION_STATIC int search_database_add_document_index(search_database_t* db, search_document_handle_t doc, const search_index_key_t& key)
{
    SHARED_WRITE_LOCK(db->mutex);
    
    int insert_at = search_database_find_index(db, key);
    if (insert_at >= 0)
    {
        // Found existing index, add document to list
        search_index_t& index = db->indexes[insert_at];

        if (index.document_count == 0)
        {
            // This can happen if a document gets removed eventually.
            index.doc = doc;
            index.document_count = 1;
        }
        else if (index.document_count < ARRAY_COUNT(index.docs))
        {
            // Check if doc already exist
            for (unsigned i = 0; i < index.document_count; ++i)
            {
                if (index.docs[i] == doc)
                    return insert_at;
            }
            index.docs[index.document_count++] = doc;
        }
        else if (index.document_count == ARRAY_COUNT(index.docs))
        {
            // Check if doc already exist
            for (unsigned i = 0; i < index.document_count; ++i)
            {
                if (index.docs[i] == doc)
                    return insert_at;
            }
            
            // Create new list and copy existing docs
            search_document_handle_t* docs = nullptr;
            for (int i = 0; i < ARRAY_COUNT(index.docs); ++i)
                array_push(docs, index.docs[i]);
            array_push(docs, doc);
            index.docs_list = docs;
            index.document_count = array_size(docs);
        }
        else
        {
            // Check if doc already exist
            for (unsigned i = 0, end = array_size(index.docs_list); i < end; ++i)
            {
                if (index.docs_list[i] == doc)
                    return insert_at;
            }
            
            // Add to existing list
            array_push(index.docs_list, doc);
            index.document_count = array_size(index.docs_list);
        }
    }
    else
    {
        // Create new index
        insert_at = ~insert_at;
        search_index_t index{ key };
        index.docs[0] = doc;
        for (int i = 1; i < ARRAY_COUNT(index.docs); ++i)
            index.docs[i] = SEARCH_DOCUMENT_INVALID_ID;
        index.document_count = 1;
        array_insert_memcpy(db->indexes, insert_at, &index);
    }

    return insert_at;
}

//
// # PUBLIC API
//

search_database_t* search_database_allocate()
{
    search_database_t* db = MEM_NEW(HASH_SEARCH, search_database_t);
    
    // Add first "special" root document
    search_document_t root{};
    root.type = SearchDocumentType::Root;
    root.name = string_clone(STRING_CONST("<root>"));
    root.source = {};
    array_push_memcpy(db->documents, &root);
    
    return db;
}

void search_database_deallocate(search_database_t*& database)
{
    if (database)
    {
        for (unsigned i = 0, end = array_size(database->documents); i < end; ++i)
        {
            search_document_t& doc = database->documents[i];
            string_deallocate(doc.name.str);
            string_deallocate(doc.source.str);
        }

        for (unsigned i = 0, end = array_size(database->indexes); i < end; ++i)
        {
            search_index_t& index = database->indexes[i];
            if (index.document_count > ARRAY_COUNT(index.docs))
                array_deallocate(index.docs_list);
        }

        array_deallocate(database->indexes);
        array_deallocate(database->documents);
    
        MEM_DELETE(database);
        database = nullptr;
    }
}

search_document_handle_t search_database_add_document(search_database_t* db, const char* name, size_t name_length)
{
    FOUNDATION_ASSERT(db);
    FOUNDATION_ASSERT(name && name_length > 0);
    
    search_document_t document{};
    document.type = SearchDocumentType::Default;
    document.name = string_clone(name, name_length);
    document.source = {};

    SHARED_WRITE_LOCK(db->mutex);
    array_push_memcpy(db->documents, &document);
    return array_size(db->documents) - 1;
}

void search_database_index_word(search_database_t* db, search_document_handle_t doc, const char* word, size_t word_length)
{
    FOUNDATION_ASSERT(db);
    FOUNDATION_ASSERT(word && word_length > 0);
    FOUNDATION_ASSERT(doc < array_size(db->documents));

    if (word_length < 3)
        return;

    string_const_t word_lower = search_database_format_word(word, word_length);
    
    // Insert exact word
    {
        search_index_key_t key;
        key.type = SearchIndexType::Word;
        key.score = 0;
        key.crc = word_lower.length;
        key.hash = string_hash(STRING_ARGS(word_lower));
        
        search_database_add_document_index(db, doc, key);
    }

    word_length = word_lower.length - 1;
    if (word_length < 3)
        return;
        
    search_index_key_t key;
    key.score = 1;
    key.type = SearchIndexType::Variation;
    for (; word_length > 2; --word_length, ++key.score)
    {
        key.crc = word_length;
        key.hash = string_hash(word_lower.str, word_length);
        search_database_add_document_index(db, doc, key);
    }    
}

uint32_t search_database_index_count(search_database_t* database)
{
    return array_size(database->indexes);
}

uint32_t search_database_document_count(search_database_t* database)
{
    return array_size(database->documents) - 1; // Always exclude root document
}

uint32_t search_database_word_document_count(search_database_t* db, const char* _word, size_t _word_length, bool include_variations /*= false*/)
{    
    string_const_t word = search_database_format_word(_word, _word_length);
    search_index_key_t key;
    key.type = SearchIndexType::Word;
    key.crc = word.length;
    key.hash = string_hash(STRING_ARGS(word));

    int count = 0;
    SHARED_READ_LOCK(db->mutex);
    int index = search_database_find_index(db, key);
    if (index >= 0)
        count = db->indexes[index].document_count;

    if (include_variations)
    {
        key.type = SearchIndexType::Variation;
        index = search_database_find_index(db, key);
        if (index >= 0)
            count += db->indexes[index].document_count;
    }

    return count;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void search_initialize()
{
    // TODO: Load search database

    // TODO: Start indexing thread that query a stock exchange market and then 
    //       for each title query its fundamental values to build a search database.
}

FOUNDATION_STATIC void search_shutdown()
{    
    // TODO: Save search database on disk
}

DEFINE_SERVICE(SEARCH, search_initialize, search_shutdown, SERVICE_PRIORITY_MODULE);
