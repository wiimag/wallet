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
#include <framework/string_table.h>

#define HASH_SEARCH static_hash_string("search", 6, 0xc9d4e54fbae76425ULL)

typedef enum class SearchIndexType : uint32_t
{
    Undefined   = 0,
    Word        = (1 << 0),
    Variation   = (1 << 1),
    Number      = (1 << 2),
    Property    = (1 << 3)
} search_index_type_t;
DEFINE_ENUM_FLAGS(SearchIndexType);

typedef enum class SearchIndexingFlags
{
    None                = 0,
    TrimWord            = 1 << 0,
    RemovePonctuations  = 1 << 1,
    Lowercase           = 1 << 2,
    Variations          = 1 << 3,

    //Default = TrimWord | Lowercase
    
} search_indexing_flags_t;
DEFINE_ENUM_FLAGS(SearchIndexingFlags);

typedef enum class SearchDocumentType
{
    Unused  = 0,
    Default = 1 << 0,
    Root    = 1 << 1,
    Removed = 1 << 2,

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
    shared_mutex            mutex;
    search_index_t*         indexes{ nullptr };
    search_document_t*      documents{ nullptr };
    uint32_t                document_count{ 0 };
    string_table_t*         strings{ nullptr };
    search_database_flags_t options{ SearchDatabaseFlags::Default };
};

//
// # PRIVATE
//

FOUNDATION_STATIC string_const_t search_database_format_word(const char* word, size_t& word_length, search_indexing_flags_t flags)
{
    FOUNDATION_ASSERT(word && word_length > 0);

    if (flags == SearchIndexingFlags::None)
        return string_const_t(word, word_length);

    const bool trim_word = (flags & SearchIndexingFlags::TrimWord) != 0;
    const bool lower_case_word = (flags & SearchIndexingFlags::Lowercase) != 0;
    const bool remove_ponctuations = (flags & SearchIndexingFlags::RemovePonctuations) != 0;
    
    if (trim_word && word_length >= 4)
    {
        // By default ignore s or es word ending
        if ((word[word_length - 1] == 's') || (word[word_length - 1] == 'S'))
        {
            --word_length;
            if ((word[word_length - 1] == 'e') || (word[word_length - 1] == 'E'))
                --word_length;
        }
    }

    if (word_length >= 32)
        log_warnf(HASH_SEARCH, WARNING_INVALID_VALUE, STRING_CONST("Word too long, truncating to 32 characters: %.*s"), (int)word_length, word);

    static thread_local char word_lower_buffer[32];
    string_t word_lower = lower_case_word ? 
        string_to_lower_utf8(STRING_CONST_BUFFER(word_lower_buffer), word, word_length) :
        string_copy(STRING_CONST_BUFFER(word_lower_buffer), word, word_length);

    if (remove_ponctuations)
    {
        // Remove some chars
        word_lower = string_remove_character(STRING_ARGS(word_lower), sizeof(word_lower_buffer), '.');
        word_lower = string_remove_character(STRING_ARGS(word_lower), sizeof(word_lower_buffer), ',');
        word_lower = string_remove_character(STRING_ARGS(word_lower), sizeof(word_lower_buffer), ';');
    }

    // Always trim whitespaces
    return string_trim(string_to_const(word_lower), ' ');
}

FOUNDATION_STATIC int search_database_find_index(search_database_t* db, const search_index_key_t& key)
{
    return array_binary_search_compare(db->indexes, key, search_index_compare);
}

FOUNDATION_STATIC int search_database_insert_index(search_database_t* db, search_document_handle_t doc, const search_index_key_t& key)
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

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL string_const_t search_database_clean_up_text(const char* text, size_t text_length)
{
    return string_trim(string_trim(string_trim(string_const(text, text_length), '"'), '\''), ' ');
}

FOUNDATION_STATIC hash_t search_database_string_to_symbol(search_database_t* db, const char* str, size_t length)
{
    hash_t symbol = string_table_to_symbol(db->strings, str, length);
    while (symbol == STRING_TABLE_FULL)
    {
        const int grow_size = (int)math_align_up(db->strings->allocated_bytes * 1.5f, 8);
        log_warnf(HASH_SEARCH, WARNING_PERFORMANCE, STRING_CONST("Search database string table full, growing to %d bytes"), grow_size);
        string_table_grow(&db->strings, grow_size);
        symbol = string_table_to_symbol(db->strings, str, length);
    }

    return symbol;
}

FOUNDATION_STATIC int32_t search_database_string_to_key(search_database_t* db, const char* str, size_t length, search_index_key_t& key)
{
    key.hash = string_hash(str, length);
    key.crc = search_database_string_to_symbol(db, str, length);
    return -to_int(length);
}

FOUNDATION_STATIC bool search_database_index_word(
    search_database_t* db, search_document_handle_t doc, 
    const char* _word, size_t _word_length, 
    SearchIndexingFlags flags)
{
    if (!search_database_is_document_valid(db, doc))
        return false;
        
    FOUNDATION_ASSERT(_word && _word_length > 0);
    if (_word_length < 3)
        return false;

    string_const_t word = search_database_format_word(_word, _word_length, flags);

    // Insert exact word
    search_index_key_t key;
    key.type = SearchIndexType::Word;
    key.score = search_database_string_to_key(db, STRING_ARGS(word), key);
    search_database_insert_index(db, doc, key);

    const bool include_variations = (flags & SearchIndexingFlags::Variations) != SearchIndexingFlags::None;
    if (!include_variations)
        return true;

    word.length--;
    if (word.length < 3)
        return true;

    key.type = SearchIndexType::Variation;
    for (; word.length > 2; --word.length, ++key.score)
    {
        // Skip spaces at the end
        if (word.str[word.length - 1] == ' ')
            continue;
            
        search_database_string_to_key(db, word.str, word.length, key);
        search_database_insert_index(db, doc, key);
    }

    return true;
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL SearchIndexingFlags search_database_case_indexing_flag(search_database_t* db)
{
    return (db->options & SearchDatabaseFlags::CaseSensitive) != 0 ? SearchIndexingFlags::None : SearchIndexingFlags::Lowercase;
}

//
// # PUBLIC API
//

search_database_t* search_database_allocate(search_database_flags_t flags /*= SearchDatabaseFlags::None*/)
{
    search_database_t* db = MEM_NEW(HASH_SEARCH, search_database_t);
    if (flags != SearchDatabaseFlags::None)
        db->options = flags;

    db->strings = string_table_allocate(1024, 10);
    
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
            string_deallocate(doc.name);
            string_deallocate(doc.source);
        }

        for (unsigned i = 0, end = array_size(database->indexes); i < end; ++i)
        {
            search_index_t& index = database->indexes[i];
            if (index.document_count > ARRAY_COUNT(index.docs))
                array_deallocate(index.docs_list);
        }

        array_deallocate(database->indexes);
        array_deallocate(database->documents);

        string_table_deallocate(database->strings);
    
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
    db->document_count++;
    array_push_memcpy(db->documents, &document);
    return array_size(db->documents) - 1;
}

bool search_database_index_text(search_database_t* db, search_document_handle_t doc, const char* text, size_t text_length, bool include_variations /*= true*/)
{
    if (!search_database_is_document_valid(db, doc))
        return false;
        
    SearchIndexingFlags flags = search_database_case_indexing_flag(db) | SearchIndexingFlags::RemovePonctuations;
    if (include_variations)
        flags |= SearchIndexingFlags::Variations;
    string_const_t expression, r = search_database_clean_up_text(text, text_length);
    do
    {
        // Split words by space
        string_split(STRING_ARGS(r), STRING_CONST(","), &expression, &r, false);
        if (expression.length)
        {
            // Split words by double colon
            string_const_t kvp, rr = search_database_clean_up_text(STRING_ARGS(expression));
            do
            {
                // Split words by :
                string_split(STRING_ARGS(rr), STRING_CONST(":"), &kvp, &rr, false);
                if (kvp.length)
                {
                    // Split words by space
                    string_const_t word, rrr = search_database_clean_up_text(STRING_ARGS(kvp));
                    do
                    {
                        // Split words by :
                        string_split(STRING_ARGS(rrr), STRING_CONST(" "), &word, &rrr, false);
                        if (word.length)
                        {
                            search_database_index_word(db, doc, STRING_ARGS(word), flags);
                        }
                    } while (rrr.length > 0);
                }
            } while (rr.length > 0);
        }
    } while (r.length > 0);

    return true;
}

bool search_database_index_exact_match(search_database_t* db, search_document_handle_t document, const char* _word, size_t _word_length, bool case_sensitive /*= false*/)
{
    FOUNDATION_ASSERT(_word && _word_length > 0);
    
    if (!search_database_is_document_valid(db, document))
        return false;

    const search_indexing_flags_t flags = case_sensitive ? SearchIndexingFlags::None : SearchIndexingFlags::Lowercase;
    string_const_t word = search_database_format_word(_word, _word_length, flags);
    
    search_index_key_t key;
    key.type = SearchIndexType::Word;
    key.score = INT_MIN - search_database_string_to_key(db, STRING_ARGS(word), key);
    return search_database_insert_index(db, document, key) >= 0;
}

bool search_database_index_word(search_database_t* db, search_document_handle_t doc, const char* word, size_t word_length, bool include_variations /*= true*/)
{
    search_indexing_flags_t flags = search_database_case_indexing_flag(db) | SearchIndexingFlags::TrimWord;
    if (include_variations)
        flags |= SearchIndexingFlags::Variations;
    return search_database_index_word(db, doc, word, word_length, flags);
}

uint32_t search_database_index_count(search_database_t* database)
{
    return array_size(database->indexes);
}

uint32_t search_database_document_count(search_database_t* database)
{
    return database->document_count;
}

uint32_t search_database_word_document_count(search_database_t* db, const char* _word, size_t _word_length, bool include_variations /*= false*/)
{    
    string_const_t word = search_database_format_word(_word, _word_length, search_database_case_indexing_flag(db) | SearchIndexingFlags::TrimWord);
    search_index_key_t key{ SearchIndexType::Word };
    search_database_string_to_key(db, STRING_ARGS(word), key);

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

bool search_database_index_property(search_database_t* db, search_document_handle_t doc, const char* name, size_t name_length, double value)
{
    if (!search_database_is_document_valid(db, doc))
        return false;
        
    const SearchIndexingFlags flags = search_database_case_indexing_flag(db);
    string_const_t property_name = search_database_format_word(name, name_length, flags);

    search_index_key_t key;
    key.type = SearchIndexType::Number;
    key.crc = search_database_string_to_symbol(db, STRING_ARGS(property_name));
    key.score = -to_int(name_length);
    key.number = value;

    return search_database_insert_index(db, doc, key) >= 0;
}

bool search_database_index_property(
    search_database_t* db, search_document_handle_t doc, 
    const char* name, size_t name_length, 
    const char* value, size_t value_length,
    bool include_variations /*= true*/)
{
    FOUNDATION_ASSERT(name && name_length > 0);
    FOUNDATION_ASSERT(value && value_length > 0);
    
    if (!search_database_is_document_valid(db, doc))
        return false;
        
    const SearchIndexingFlags case_indexing_flag = search_database_case_indexing_flag(db);
    string_const_t property_name = search_database_format_word(name, name_length, case_indexing_flag);

    search_index_key_t key;
    key.type = SearchIndexType::Property;
    key.score = -to_int(value_length);
    key.crc = search_database_string_to_symbol(db, STRING_ARGS(property_name));

    string_const_t property_value = search_database_format_word(value, value_length, case_indexing_flag);
    key.hash = search_database_string_to_symbol(db, STRING_ARGS(property_value));

    search_database_insert_index(db, doc, key);

    if (!include_variations)
        return true;

    property_value.length--;        
    for (; property_value.length > 2; --property_value.length, ++key.score)
    {
        // Skip spaces at the end
        if (property_value.str[property_value.length - 1] == ' ')
            continue;
        key.hash = search_database_string_to_symbol(db, property_value.str, property_value.length);
        search_database_insert_index(db, doc, key);
    }

    return true;
}

uint32_t search_database_word_count(search_database_t* database)
{
    return database->strings->count;
}

bool search_database_is_document_valid(search_database_t* database, search_document_handle_t document)
{
    FOUNDATION_ASSERT(database);

    if (document == 0)
        return false; // User cannot access root document
    
    if (document >= array_size(database->documents))
        return false;
        
    SHARED_READ_LOCK(database->mutex);
    return (database->documents[document].type == SearchDocumentType::Default);
}

bool search_database_remove_document(search_database_t* db, search_document_handle_t document)
{
    FOUNDATION_ASSERT(db);
    FOUNDATION_ASSERT(document < array_size(db->documents));
    FOUNDATION_ASSERT(document != 0);

    SHARED_WRITE_LOCK(db->mutex);
    
    bool document_removed = false;
    search_document_t* doc = &db->documents[document];
    if (doc->type == SearchDocumentType::Removed)
        return false;
    
    for (unsigned i = 0, end = array_size(db->indexes); i < end && !document_removed; ++i)
    {
        search_index_t& index = db->indexes[i];

        // Remove document from index
        if (index.document_count <= ARRAY_COUNT(index.docs))
        {
            for (unsigned j = 0, endj = ARRAY_COUNT(index.docs); j < endj; ++j)
            {
                if (index.docs[j] == document)
                {
                    --index.document_count;
                    if (index.document_count > 0)
                    {
                        // Remove element and memmove the rest
                        memmove(index.docs + j, index.docs + j + 1, sizeof(search_document_handle_t) * (ARRAY_COUNT(index.docs) - j - 1));
                    }
                    document_removed = true;
                    break;
                }
            }
        }
        else
        {
            for (unsigned j = 0, endj = array_size(index.docs_list); j < endj; ++j)
            {
                if (index.docs_list[j] == document)
                {
                    array_erase_ordered_safe(index.docs_list, j);
                    --index.document_count;

                    if (index.document_count <= ARRAY_COUNT(index.docs))
                    {
                        // Move all documents from list to array
                        search_document_handle_t static_docs[ARRAY_COUNT(index.docs)];
                        for (unsigned k = 0, endk = array_size(index.docs_list); k < endk; ++k)
                            static_docs[k] = index.docs_list[k];
                        array_deallocate(index.docs_list);
                        memcpy(&index.docs, &static_docs, sizeof(static_docs));
                    }

                    document_removed = true;
                    break;
                }
            }
        }
    }

    FOUNDATION_ASSERT(db->document_count > 0);
    db->document_count--;
    doc->type = SearchDocumentType::Removed;
    string_deallocate(doc->name);
    string_deallocate(doc->source);
    return document_removed;
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
