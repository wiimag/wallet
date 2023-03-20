/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "search_database.h"

#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/service.h>
#include <framework/database.h>
#include <framework/dispatcher.h>
#include <framework/string_table.h>
#include <framework/string.h>
#include <framework/array.h>

#include <foundation/stream.h>

 /*! Search database version */
constexpr uint8_t SEARCH_DATABASE_VERSION = 7;

/*! Search database index entry types */
typedef enum class SearchIndexType : uint32_t
{
    Undefined   = 0,
    Word        = (1 << 0),
    Variation   = (1 << 1),
    Number      = (1 << 2),
    Property    = (1 << 3)
} search_index_type_t;
DEFINE_ENUM_FLAGS(SearchIndexType);

/*! Search database indexing flags */
typedef enum class SearchIndexingFlags
{
    None                = 0,
    TrimWord            = 1 << 0,
    RemovePonctuations  = 1 << 1,
    Lowercase           = 1 << 2,
    Variations          = 1 << 3,
    Exclude             = 1 << 4,
    
} search_indexing_flags_t;
DEFINE_ENUM_FLAGS(SearchIndexingFlags);

/*! Search database document types */
typedef enum class SearchDocumentType : unsigned char
{
    Unused  = 0,
    Default = 1 << 0,
    Root    = 1 << 1,
    Removed = 1 << 2,

} search_document_type_t;
DEFINE_ENUM_FLAGS(SearchDocumentType);

/*! Search database index key entry */
FOUNDATION_ALIGNED_STRUCT(search_index_key_t, 8)
{
    /*! Type of the index entry */
    search_index_type_t type{ SearchIndexType::Undefined };

    /*! Value correction code (can be length, property key hash, etc.) */
    uint64_t crc{ 0 };

    /*! Key of the index entry. This key is usually hashed from the value content. */
    union {
        hash_t hash{ 0 };
        double number;
    };

    /*! Score of the entry (used for sorting results only) */
    int32_t score{ 0 };
};

/*! Search database index entry */
FOUNDATION_ALIGNED_STRUCT(search_index_t, 8)
{
    search_index_key_t  key;
    
    uint32_t document_count{ 0 };
    union {
        search_document_handle_t  doc;
        search_document_handle_t  docs[6]; // Ideally align this padding with 8 bytes
        search_document_handle_t* docs_list{ nullptr };
    };
};

/*! Search database document entry */
FOUNDATION_ALIGNED_STRUCT(search_document_t, 8) 
{
    search_document_type_t  type{ SearchDocumentType::Unused };
    string_t                name{};
    time_t                  timestamp{ 0 };
};

/*! Search database structure
 * 
 * The search database is thread safe and use a shared mutex to allow multiple reads concurrently.
 */
FOUNDATION_ALIGNED_STRUCT(search_database_t, 8)
{
    shared_mutex            mutex;
    search_index_t*         indexes{ nullptr };
    search_document_t*      documents{ nullptr };
    uint32_t                document_count{ 0 };
    string_table_t*         strings{ nullptr };
    search_database_flags_t options{ SearchDatabaseFlags::Default };
    bool                    dirty{ false };

    search_query_t**         queries{ nullptr };
};

/*! Search database header */
FOUNDATION_ALIGNED_STRUCT(search_database_header_t, 8) {
    char magic[4] = { 0 };
    uint8_t version = 0;
    uint8_t index_struct_size = 0;
    uint8_t index_key_struct_size = 0;
    uint8_t document_struct_size = 0;
    uint8_t db_struct_size = 0;
    uint8_t string_table_size = 0;
} SEARCH_DATABASE_HEADER{
    { 'S', 'E', 'A', 'R' }, SEARCH_DATABASE_VERSION, 
    sizeof(search_index_t), sizeof(search_index_key_t),
    sizeof(search_document_t), sizeof(search_database_t),
    sizeof(string_table_t)
};

//
// # PRIVATE
//

FOUNDATION_STATIC void search_database_deallocate_documents(search_database_t*& db)
{
    for (unsigned i = 0, end = array_size(db->documents); i < end; ++i)
    {
        search_document_t& doc = db->documents[i];
        string_deallocate(doc.name);
    }
    array_deallocate(db->documents);
}

FOUNDATION_STATIC void search_database_deallocate_indexes(search_database_t*& db)
{
    for (unsigned i = 0, end = array_size(db->indexes); i < end; ++i)
    {
        search_index_t& index = db->indexes[i];
        if (index.document_count > ARRAY_COUNT(index.docs))
            array_deallocate(index.docs_list);
    }
    array_deallocate(db->indexes);
}

FOUNDATION_STATIC string_const_t search_database_format_word(const char* word, size_t& word_length, search_indexing_flags_t flags)
{
    FOUNDATION_ASSERT(word && word_length > 0);

    if (flags == SearchIndexingFlags::None)
        return string_const(word, word_length);

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

    static thread_local char word_lower_buffer[SEARCH_INDEX_WORD_MAX_LENGTH];
    if (word_length >= ARRAY_COUNT(word_lower_buffer))
        log_warnf(0, WARNING_INVALID_VALUE, STRING_CONST("Word too long, truncating to %u characters: %.*s"), SEARCH_INDEX_WORD_MAX_LENGTH, (int)word_length, word);

    string_t word_lower = lower_case_word ? 
        string_to_lower_utf8(STRING_BUFFER(word_lower_buffer), word, word_length) :
        string_copy(STRING_BUFFER(word_lower_buffer), word, word_length);

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

FOUNDATION_FORCEINLINE int search_database_index_key_compare(const search_index_key_t& s, const search_index_key_t& key)
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

FOUNDATION_FORCEINLINE int search_database_index_compare(const search_index_t& s, const search_index_key_t& key)
{
    return search_database_index_key_compare(s.key, key);
}

FOUNDATION_STATIC int search_database_find_index(search_database_t* db, const search_index_key_t& key)
{
    return array_binary_search_compare(db->indexes, key, search_database_index_compare);
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
            db->dirty = true;
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
            db->dirty = true;
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
            db->dirty = true;
            array_push(docs, doc);
            index.docs_list = docs;
            index.document_count = array_size(docs);
            FOUNDATION_ASSERT(index.document_count > ARRAY_COUNT(index.docs));
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
            db->dirty = true;
            array_push(index.docs_list, doc);
            index.document_count = array_size(index.docs_list);
            FOUNDATION_ASSERT(index.document_count > ARRAY_COUNT(index.docs));
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
        db->dirty = true;
        array_insert_memcpy(db->indexes, insert_at, &index);
    }

    return insert_at;
}

FOUNDATION_FORCEINLINE string_const_t search_database_clean_up_text(const char* text, size_t text_length)
{
    return string_trim(string_trim(string_trim(string_const(text, text_length), '"'), '\''), ' ');
}

FOUNDATION_STATIC hash_t search_database_string_to_symbol(search_database_t* db, const char* str, size_t length)
{
    string_table_symbol_t symbol = string_table_to_symbol(db->strings, str, length);
    while (symbol == STRING_TABLE_FULL)
    {
        const int grow_size = (int)math_align_up(db->strings->allocated_bytes * 1.5f, 8);
        log_debugf(0, STRING_CONST("Search database string table full, growing to %d bytes"), grow_size);
        string_table_grow(&db->strings, grow_size);
        symbol = string_table_to_symbol(db->strings, str, length);
    }

    FOUNDATION_ASSERT(symbol > 0);
    return (hash_t)(symbol);
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
    FOUNDATION_ASSERT(db);

    if (_word == nullptr || _word_length < 3)
        return false;
        
    if (!search_database_is_document_valid(db, doc))
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

FOUNDATION_FORCEINLINE SearchIndexingFlags search_database_case_indexing_flag(search_database_t* db)
{
    return (db->options & SearchDatabaseFlags::CaseSensitive) != 0 ? SearchIndexingFlags::None : SearchIndexingFlags::Lowercase;
}

//
// # PUBLIC API
//

search_database_t* search_database_allocate(search_database_flags_t flags /*= SearchDatabaseFlags::None*/)
{
    FOUNDATION_ASSERT(sizeof(search_index_t) <= 64);

    search_database_t* db = MEM_NEW(0, search_database_t);
    if (flags != SearchDatabaseFlags::None)
        db->options = flags;

    db->strings = string_table_allocate(1024, 10);

    // Add a dummy query so the handle indexes start at 1
    array_push(db->queries, nullptr);
    
    // Add first "special" root document
    search_document_t root{};
    root.type = SearchDocumentType::Root;
    root.name = string_clone(STRING_CONST("<root>"));
    root.timestamp = time_now();
    array_push_memcpy(db->documents, &root);
    
    return db;
}

void search_database_deallocate(search_database_t*& db)
{
    if (db == nullptr)
        return;

    search_database_deallocate_indexes(db);
    search_database_deallocate_documents(db);
    
    string_table_deallocate(db->strings);

    for (unsigned i = 0, end = array_size(db->queries); i < end; ++i)
        search_query_deallocate(db->queries[i]);
    array_deallocate(db->queries);
    
    MEM_DELETE(db);
    db = nullptr;
}

bool search_database_is_dirty(search_database_t* database)
{
    FOUNDATION_ASSERT(database);
    return database->dirty;
}

bool search_database_document_update_timestamp(search_database_t* db, search_document_handle_t document, time_t timestamp /*= 0*/)
{
    if (!search_database_is_document_valid(db, document))
        return false;

    if (timestamp == 0)
        timestamp = time_now();

    SHARED_WRITE_LOCK(db->mutex);
    if (db->documents[document].timestamp == timestamp)
        return false;
    db->dirty = true;
    return (db->documents[document].timestamp = timestamp) > 0;
}

time_t search_database_document_timestamp(search_database_t* db, search_document_handle_t document)
{
    if (!search_database_is_document_valid(db, document))
        return 0;

    SHARED_READ_LOCK(db->mutex);
    return db->documents[document].timestamp;
}

search_document_handle_t search_database_find_document(search_database_t* db, const char* name, size_t name_length)
{
    FOUNDATION_ASSERT(db);

    if (name == nullptr || name_length == 0)
        return SEARCH_DOCUMENT_INVALID_ID;

    SHARED_READ_LOCK(db->mutex);
    for (unsigned doc_index = 1, end = array_size(db->documents); doc_index < end; ++doc_index)
    {
        search_document_t& doc = db->documents[doc_index];
        if (string_equal_nocase(STRING_ARGS(doc.name), name, name_length))
            return doc_index;
    }

    return SEARCH_DOCUMENT_INVALID_ID;
}

search_document_handle_t search_database_add_document(search_database_t* db, const char* name, size_t name_length)
{
    FOUNDATION_ASSERT(db);
    FOUNDATION_ASSERT(name && name_length > 0);
    
    search_document_t document{};
    document.type = SearchDocumentType::Default;
    document.name = string_clone(name, name_length);
    document.timestamp = time_now();

    SHARED_WRITE_LOCK(db->mutex);
    db->dirty = true;
    db->document_count++;
    array_push_memcpy(db->documents, &document);
    return array_size(db->documents) - 1;
}

search_document_handle_t search_database_get_or_add_document(search_database_t* db, const char* name, size_t name_length)
{
    FOUNDATION_ASSERT(db);
    FOUNDATION_ASSERT(name && name_length > 0);

    search_document_handle_t doc = search_database_find_document(db, name, name_length);
    if (doc == SEARCH_DOCUMENT_INVALID_ID)
        doc = search_database_add_document(db, name, name_length);
    return doc;
}

bool search_database_index_text(search_database_t* db, search_document_handle_t doc, const char* text, size_t text_length, bool include_variations /*= true*/)
{
    if (text == nullptr || text_length == 0)
        return false;
        
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
    FOUNDATION_ASSERT(db);

    if (!_word || _word_length == 0)
        return false;
    
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

string_const_t search_database_document_name(search_database_t* database, search_document_handle_t document)
{
    if (!search_database_is_document_valid(database, document))
        return string_null();

    SHARED_READ_LOCK(database->mutex);
    return string_to_const(database->documents[document].name);
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
    
    if (value == nullptr || value_length == 0)
        return false;
    
    if (!search_database_is_document_valid(db, doc))
        return false;
        
    const SearchIndexingFlags case_indexing_flag = search_database_case_indexing_flag(db);
    string_const_t property_name = search_database_format_word(name, name_length, case_indexing_flag);

    search_index_key_t key;
    key.type = SearchIndexType::Property;
    key.score = to_int(value_length);
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

    // TODO: Add support to split property text value with spaces and index each word as a property variation
    
    return true;
}

uint32_t search_database_word_count(search_database_t* database)
{
    return database->strings->count;
}

bool search_database_contains_word(search_database_t* db, const char* word, size_t word_length)
{
    FOUNDATION_ASSERT(db);

    if (word == nullptr || word_length == 0)
        return false;

    string_const_t formatted_word = search_database_format_word(word, word_length, search_database_case_indexing_flag(db));
    return string_table_find_symbol(db->strings, STRING_ARGS(formatted_word)) > 0;
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

FOUNDATION_FORCEINLINE search_document_handle_t search_database_get_indexed_document(const search_index_t& idx, uint32_t element_at)
{
    FOUNDATION_ASSERT(idx.document_count > 0 && element_at < idx.document_count);
        
    if (idx.document_count <= ARRAY_COUNT(idx.docs))
        return idx.docs[element_at];

    return idx.docs_list[element_at];
}

FOUNDATION_STATIC bool search_database_insert_result(search_result_t*& results, const search_result_t& new_entry)
{
    int ridx = array_binary_search_compare(results, new_entry, [](const search_result_t& lhs, const search_result_t& rhs) 
    {
        if (lhs.id < rhs.id)
            return -1;

        if (lhs.id > rhs.id)
            return 1;

        return 0;
    });

    if (ridx < 0)
    {
        ridx = ~ridx;
        array_insert_memcpy(results, ridx, &new_entry);
        return true;
    }

    search_result_t& entry = results[ridx];
    entry.score = min(new_entry.score, entry.score);
    return false;
}

FOUNDATION_STATIC search_result_t* search_database_get_index_document_results(search_database_t* db, const search_index_t& idx, const search_result_t* and_set, search_result_t*& results)
{
    search_result_t entry;
    search_result_t* matches = nullptr;
    for (uint32_t i = 0; i < idx.document_count; ++i)
    {
        entry.id = search_database_get_indexed_document(idx, i);

        if (and_set == nullptr || array_contains(and_set, entry.id))
        {
            entry.score = idx.key.score;
            if (search_database_insert_result(results, entry))
                matches = results;
        }
    }

    return matches;
}

FOUNDATION_STATIC search_result_t* search_database_get_key_document_results(search_database_t* db, const search_index_key_t& key, const search_result_t* and_set, search_result_t*& results)
{
    int index = search_database_find_index(db, key);
    if (index < 0)
        return nullptr;
     
    const search_index_t& idx = db->indexes[index];
    return search_database_get_index_document_results(db, idx, and_set, results);
}

FOUNDATION_STATIC search_result_t* search_database_exclude_documents(search_database_t* db, search_result_t*& results)
{
    // This operation is costly as we have to execute the query and then iterate over ALL the documents to exclude those found
    search_result_t* included_set = nullptr;
    const search_result_t* excluded_set = results;

    foreach(d, db->documents)
    {
        if (d->type != SearchDocumentType::Default)
            continue;

        const auto docid = (search_document_handle_t)i;
        if (array_contains(excluded_set, docid))
            continue;

        search_result_t entry;
        entry.id = docid;
        entry.score = 0;
        search_database_insert_result(included_set, entry);
    }

    array_deallocate(excluded_set);
    results = included_set;
    return included_set;
}

FOUNDATION_STATIC search_result_t* search_database_query_property_number(
    search_database_t* db, 
    search_query_eval_flags_t eval_flags, const search_index_key_t& key, 
    search_result_t* and_set, search_result_t*& results)
{
    // We expect the db to already be locked
    FOUNDATION_ASSERT(db->mutex.locked());
    
    // Find the range of numbers to match
    int start = search_database_find_index(db, key);
    if (start < 0)
        start = ~start;

    int end = start;

    // Try to fix the insertion point by one index
    if (db->indexes[start].key.type == key.type && db->indexes[start].key.crc != key.crc)
        start++;

    if (db->indexes[end].key.type == key.type && db->indexes[end].key.crc != key.crc)
        end--;

    if (db->indexes[start].key.crc != key.crc || db->indexes[end].key.crc != key.crc)
        return results; // Nothing to be found
    
    // Rewind to first index with same crc
    while (start > 0 && db->indexes[start - 1].key.crc == key.crc)
        --start;
    FOUNDATION_ASSERT(db->indexes[start].key.type == key.type && db->indexes[start].key.crc == key.crc);

    // Forward to the last index with same crc
    const int index_count = array_size(db->indexes);
    while (end < index_count - 1 && db->indexes[end + 1].key.crc == key.crc)
        ++end;
    FOUNDATION_ASSERT(db->indexes[end].key.type == key.type && db->indexes[end].key.crc == key.crc);

    if (any(eval_flags, SearchQueryEvalFlags::OpLess | SearchQueryEvalFlags::OpLessEq))
    {
        for (; start <= end; ++start)
        {
            const search_index_t& idx = db->indexes[start];
            if (idx.key.number >= key.number)
                break;
            search_database_get_index_document_results(db, idx, and_set, results);
        }
        
        if (test(eval_flags, SearchQueryEvalFlags::OpLessEq))
        {
            for (; start <= end; ++start)
            {
                const search_index_t& idx = db->indexes[start];
                if (idx.key.number > key.number)
                    break;
                search_database_get_index_document_results(db, idx, and_set, results);
            }
        }
    }
    else if (any(eval_flags, SearchQueryEvalFlags::OpGreater | SearchQueryEvalFlags::OpGreaterEq))
    {
        for (; end >= start; --end)
        {
            const search_index_t& idx = db->indexes[end];
            if (idx.key.number <= key.number)
                break;
            search_database_get_index_document_results(db, idx, and_set, results);
        }

        if (test(eval_flags, SearchQueryEvalFlags::OpGreaterEq))
        {
            for (; end >= start; --end)
            {
                const search_index_t& idx = db->indexes[end];
                if (idx.key.number < key.number)
                    break;
                search_database_get_index_document_results(db, idx, and_set, results);
            }
        }
    }
    else
    {
        FOUNDATION_ASSERT_FAIL("Invalid number query operator");
    }    

    return results;
}

FOUNDATION_STATIC search_result_t* search_database_query_property(
    search_database_t* db,
    string_const_t name,
    string_const_t value,
    search_result_t* and_set,
    search_query_eval_flags_t eval_flags,
    search_indexing_flags_t indexing_flags)
{
    if (value.length == 0)
        return nullptr;

    search_result_t* results = nullptr;
    search_index_key_t key{ SearchIndexType::Property };

    SHARED_READ_LOCK(db->mutex);

    string_const_t property_name = search_database_format_word(STRING_ARGS(name), indexing_flags);
    key.crc = string_table_find_symbol(db->strings, STRING_ARGS(property_name));
    if ((int64_t)key.crc <= 0)
        return nullptr;
        
    time_t date;
    string_const_t property_value = search_database_format_word(STRING_ARGS(value), indexing_flags);
    if (string_try_convert_number(STRING_ARGS(property_value), key.number))
    {
        key.type = SearchIndexType::Number;

        if (none(eval_flags, SearchQueryEvalFlags::OpEqual | SearchQueryEvalFlags::OpContains))
        {
            return search_database_query_property_number(db, eval_flags, key, and_set, results);
        }
    }
    else if (string_try_convert_date(STRING_ARGS(property_value), date))
    {
        key.number = (double)date;
        key.type = SearchIndexType::Number;
        return search_database_query_property_number(db, eval_flags, key, and_set, results);
    }
    else
    {
        key.hash = string_table_find_symbol(db->strings, STRING_ARGS(property_value));
        if ((int64_t)key.hash <= 0)
            return nullptr;
    }     
        
    return search_database_get_key_document_results(db, key, and_set, results);
}

FOUNDATION_STATIC search_result_t* search_database_query_word(
    search_database_t* db, 
    string_const_t value, 
    search_result_t* and_set,
    search_query_eval_flags_t eval_flags, 
    search_indexing_flags_t indexing_flags)
{    
    if (value.length < 2)
        return nullptr;

    search_result_t* results = nullptr;
    string_const_t word = search_database_format_word(STRING_ARGS(value), indexing_flags);

    search_index_key_t key{ SearchIndexType::Word };
    key.hash = string_hash(STRING_ARGS(word));

    SHARED_READ_LOCK(db->mutex);

    key.crc = string_table_find_symbol(db->strings, STRING_ARGS(word));
    if ((int64_t)key.crc <= 0)
        return nullptr;

    search_database_get_key_document_results(db, key, and_set, results);
    if (test(eval_flags, SearchQueryEvalFlags::OpContains))
    {
        if (any(indexing_flags, SearchIndexingFlags::Variations))
        {
            key.type = SearchIndexType::Variation;
            search_database_get_key_document_results(db, key, and_set, results);
        }
    }
    
    return results;
}

FOUNDATION_STATIC search_result_t* search_database_handle_query_evaluation(
    string_const_t name,
    string_const_t value,
    search_query_eval_flags_t eval_flags,
    search_result_t* and_set,
    void* user_data)
{
    search_database_t* db = (search_database_t*)user_data;
    FOUNDATION_ASSERT(db);

    if (array_size(db->indexes) == 0)
        return nullptr;
    
    SearchIndexingFlags indexing_flags = search_database_case_indexing_flag(db);
    if (any(eval_flags, SearchQueryEvalFlags::Exclude))
        indexing_flags |= SearchIndexingFlags::Exclude;
        
    if (!any(db->options, SearchDatabaseFlags::DoNotIndexVariations))
        indexing_flags |= SearchIndexingFlags::Variations;
    
    search_result_t* results = nullptr;
    if (any(eval_flags, SearchQueryEvalFlags::Word))
    {
        results = search_database_query_word(db, value, and_set, eval_flags, indexing_flags);
    }
    else if (any(eval_flags, SearchQueryEvalFlags::Property))
    {
        results = search_database_query_property(db, name, value, and_set, eval_flags, indexing_flags);
    }
    else if (any(eval_flags, SearchQueryEvalFlags::Function))
    {
        log_warnf(0, WARNING_UNSUPPORTED, STRING_CONST("Function support is not supported yet"));
        //FOUNDATION_ASSERT_FAIL("Not implemented");
    }
    else
    {
        FOUNDATION_ASSERT_FAIL("Not implemented");
    }

    if (any(eval_flags, SearchQueryEvalFlags::Exclude))
        return search_database_exclude_documents(db, results);

    return results;
}

search_query_handle_t search_database_query(search_database_t* db, const char* query_string, size_t query_string_length)
{
    FOUNDATION_ASSERT(db);

    if (query_string == nullptr || query_string_length == 0)
        return SEARCH_QUERY_INVALID_ID;

    // Create query
    search_query_t* query = search_query_allocate(query_string, query_string_length);
    FOUNDATION_ASSERT(query);
    
    // TODO: Use the job system to spawn a new query job.
    try
    {
        query->results = search_query_evaluate(query, search_database_handle_query_evaluation, db);
    }
    catch (SearchQueryException ex)
    {
        search_query_deallocate(query);
        throw ex;
    }
    
    query->completed = true;

    SHARED_WRITE_LOCK(db->mutex);
    array_push(db->queries, query);
    return (search_query_handle_t)array_size(db->queries) - 1;
}

bool search_database_query_is_completed(search_database_t* database, search_query_handle_t query)
{
    FOUNDATION_ASSERT(query > 0);
    if (query == 0 || query >= array_size(database->queries))
        return false;
    SHARED_READ_LOCK(database->mutex);
    return database->queries[query]->completed;
}

const search_result_t* search_database_query_results(search_database_t* database, search_query_handle_t query)
{
    FOUNDATION_ASSERT(query > 0);
    if (query == 0 || query >= array_size(database->queries))
        return nullptr;
    SHARED_READ_LOCK(database->mutex);
    return database->queries[query]->results;
}

bool search_database_query_dispose(search_database_t* database, search_query_handle_t query)
{
    FOUNDATION_ASSERT(query > 0);
    if (query >= array_size(database->queries))
        return false;
    SHARED_WRITE_LOCK(database->mutex);
    search_query_deallocate(database->queries[query]);
    return database->queries[query] == nullptr;
}

bool search_database_load(search_database_t* db, stream_t* stream)
{
    // Read database header
    search_database_header_t header;
    stream_read(stream, &header, sizeof(SEARCH_DATABASE_HEADER));
    if (memcmp(&header, &SEARCH_DATABASE_HEADER, sizeof(SEARCH_DATABASE_HEADER)) != 0)
        return false;
        
    // Read documents
    search_document_t* documents = nullptr;
    uint32_t document_count = stream_read_uint32(stream);
    array_resize(documents, document_count);
    for (uint32_t i = 0; i < document_count; ++i)
    {
        search_document_t* doc = documents + i;
        doc->type = (search_document_type_t)stream_read_uint8(stream);
        doc->name = stream_read_string(stream);
        doc->timestamp = stream_read_uint64(stream);
    }
    
    // Read string table
    string_table_t* strings = nullptr;
    int32_t string_count = stream_read_int32(stream);
    uint64_t average_string_length = stream_read_uint64(stream);
    uint64_t allocated_bytes = stream_read_uint64(stream);
    
    strings = (string_table_t*)memory_allocate(0, allocated_bytes, 4, MEMORY_PERSISTENT);
    FOUNDATION_ASSERT(strings);
    
    stream_read(stream, strings, allocated_bytes);
    strings->free_slots = nullptr;
    FOUNDATION_ASSERT(strings->count == string_count);
    FOUNDATION_ASSERT(strings->allocated_bytes == allocated_bytes);
    FOUNDATION_ASSERT(string_table_average_string_length(strings) == average_string_length);

    // Read indexes
    search_index_t* indexes = nullptr;
    const uint32_t index_count = stream_read_uint32(stream);
    array_resize(indexes, index_count);
    for (uint32_t i = 0; i < index_count; ++i)
    {
        search_index_t* index = indexes + i;
        stream_read(stream, &index->key, sizeof(index->key));
        index->document_count = stream_read_uint32(stream);
        if (index->document_count <= ARRAY_COUNT(index->docs))
        {
            stream_read(stream, index->docs, sizeof(uint32_t) * index->document_count);
        }
        else
        {
            index->docs_list = nullptr;
            array_resize(index->docs_list, index->document_count);
            stream_read(stream, index->docs_list, sizeof(uint32_t) * index->document_count);
        }
    }

    // So far so good, lets swap new entries.
    SHARED_WRITE_LOCK(db->mutex);
    search_database_deallocate_documents(db);
    db->dirty = false;
    db->documents = documents;
    db->document_count = document_count - 1; /* -1 to exclude the root document */

    search_database_deallocate_indexes(db);
    db->indexes = indexes;

    string_table_deallocate(db->strings);
    db->strings = strings;    
    
    return true;
}

string_t* search_database_property_keywords(search_database_t* database)
{
    string_t* keywords = nullptr;

    // Iterate all indexes with the type property
    SHARED_READ_LOCK(database->mutex);
    
    for (uint32_t i = 0; i < array_size(database->indexes); ++i)
    {
        const search_index_t* index = database->indexes + i;
        if (index->key.type == SearchIndexType::Property || index->key.type == SearchIndexType::Number)
        {
            string_const_t keyword = string_table_to_string_const(database->strings, (string_table_symbol_t)index->key.crc);
            if (keyword.length && !array_contains(keywords, keyword, LC2(string_equal(STRING_ARGS(_1), STRING_ARGS(_2)))))
                array_push(keywords, string_clone(STRING_ARGS(keyword)));
        }
    }

    return keywords;
}

bool search_database_save(search_database_t* db, stream_t* stream)
{
    SHARED_READ_LOCK(db->mutex);
    
    // Save database header
    {
        TIME_TRACKER("Write header");
        stream_write(stream, &SEARCH_DATABASE_HEADER, sizeof(SEARCH_DATABASE_HEADER));
    }

    // Save documents
    {
        TIME_TRACKER("Write document");
        stream_write_uint32(stream, array_size(db->documents));
        foreach(d, db->documents)
        {
            stream_write_uint8(stream, (uint32_t)d->type);
            stream_write_string(stream, STRING_ARGS(d->name));
            stream_write_uint64(stream, d->timestamp);
        }
    }

    // Save string table
    {
        TIME_TRACKER("Write string table");
        string_table_pack(db->strings);
        stream_write_int32(stream, db->strings->count);
        stream_write_uint64(stream, string_table_average_string_length(db->strings));
        stream_write_uint64(stream, db->strings->allocated_bytes);
        stream_write(stream, db->strings, db->strings->allocated_bytes);
    }

    // Save indexes
    {
        TIME_TRACKER("Write indexes");
        stream_write_uint32(stream, array_size(db->indexes));
        foreach(e, db->indexes)
        {
            stream_write(stream, &e->key, sizeof(e->key));

            stream_write_uint32(stream, e->document_count);

            if (e->document_count <= ARRAY_COUNT(e->docs))
            {
                stream_write(stream, e->docs, sizeof(uint32_t) * e->document_count);
            }
            else
            {
                stream_write(stream, e->docs_list, sizeof(uint32_t) * e->document_count);
            }
        }
    }

    db->dirty = false;
    return true;
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

    for (unsigned i = 0, end = array_size(db->indexes); i < end/* && !document_removed*/; ++i)
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
                    FOUNDATION_ASSERT(index.document_count == array_size(index.docs_list));

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

        if (index.document_count == 0)
        {
            const char* value = (int32_t)index.key.hash > 0 ? string_table_to_string(db->strings, (uint32_t)index.key.hash) : nullptr;
            log_debugf(0, STRING_CONST("Deleting index %u (%d) -> %s:%s(%.lf)"), 
                i, index.key.type, 
                string_table_to_string(db->strings, (int32_t)index.key.crc),
                value ? value : "NA", index.key.number);
            array_erase_ordered_safe(db->indexes, i);
            --i;
            --end;
            FOUNDATION_ASSERT(array_size(db->indexes) == end);
        }
    }

    FOUNDATION_ASSERT(db->document_count > 0);
    db->document_count--;
    doc->type = SearchDocumentType::Removed;
    db->dirty |= document_removed;
    string_deallocate(doc->name);
    return document_removed;
}
