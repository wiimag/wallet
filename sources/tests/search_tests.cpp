/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT
 
#include "test_utils.h"

#include <search.h>

#include <framework/common.h>

#include <doctest/doctest.h>

TEST_SUITE("Search")
{
    TEST_CASE("UTF-8")
    {
        const char mel[] = { 0x4d, (char)0xc3, (char)0x89, 0x4c, 0x61, 0x6e, 0x49, 0x45, 0 };

        char to_lower_buffer[32];
        char to_upper_buffer[32];

        string_t to_lower = string_to_lower_utf8(STRING_CONST_BUFFER(to_lower_buffer), STRING_CONST(mel));
        string_t to_upper = string_to_upper_utf8(STRING_CONST_BUFFER(to_upper_buffer), STRING_ARGS(to_lower));

        log_debugf(0, STRING_CONST("Original: %s -> Upper: %.*s -> Lower: %.*s "), 
            mel,
            STRING_FORMAT(to_upper),
            STRING_FORMAT(to_lower));
    }

    
    TEST_CASE("Create")
    {
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);

        auto doc1 = search_database_add_document(db, STRING_CONST("HDOC.TO"));
        REQUIRE_NE(doc1, 0);
        
        search_database_index_word(db, doc1, STRING_CONST("hello"));
        search_database_index_word(db, doc1, STRING_CONST("world"));
        search_database_index_word(db, doc1, STRING_CONST("hello"));

        CHECK_EQ(search_database_index_count(db), 6);
        CHECK_EQ(search_database_document_count(db), 1);

        search_database_deallocate(db);
    }

    TEST_CASE("Words")
    {
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);

        auto doc1 = search_database_add_document(db, STRING_CONST("U.US"));
        auto doc2 = search_database_add_document(db, STRING_CONST("ADSK.US"));
        
        search_database_index_word(db, doc1, STRING_CONST("hello"));
        search_database_index_word(db, doc2, STRING_CONST("world"));
        search_database_index_word(db, doc1, STRING_CONST("hell"));
        search_database_index_word(db, doc2, STRING_CONST("hell"));
        search_database_index_word(db, doc1, STRING_CONST("zone"));
        search_database_index_word(db, doc2, STRING_CONST("bold"));
        search_database_index_word(db, doc2, STRING_CONST("worst"));

        CHECK_EQ(search_database_index_count(db), 13);
        CHECK_EQ(search_database_document_count(db), 2);

        search_database_deallocate(db);
    }

    TEST_CASE("Word Exclusion")
    {
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);
        
        auto doc1 = search_database_add_document(db, STRING_CONST("Words 1"));
        auto doc2 = search_database_add_document(db, STRING_CONST("Words 2"));

        search_database_index_word(db, doc1, STRING_CONST("hello"));
        CHECK_EQ(search_database_index_count(db), 3);
        
        search_database_index_word(db, doc1, STRING_CONST("HELLO"));// Indexed words are case insensitive.
        search_database_index_word(db, doc2, STRING_CONST("HELLO"));// Indexed words are case insensitive.
        CHECK_EQ(search_database_index_count(db), 3);
        
        // Exact words are always reindexed and usually have a better score.
        search_database_index_word(db, doc1, STRING_CONST("HELL"));// A variation is already indexed for this.. Only add new full word
        CHECK_EQ(search_database_index_count(db), 4);
        
        search_database_index_word(db, doc2, STRING_CONST("HEL")); // A variation is already indexed for this. Only add new full word
        CHECK_EQ(search_database_index_count(db), 5);
        
        search_database_index_word(db, doc1, STRING_CONST("HE")); // Too short
        search_database_index_word(db, doc2, STRING_CONST("H")); // Too short
        CHECK_EQ(search_database_index_count(db), 5);

        // We have a few heuristics were we don't index words that ends with es or s (TODO: Make this an option eventually)
        search_database_index_word(db, doc1, STRING_CONST("CAR"));
        CHECK_EQ(search_database_index_count(db), 6);
        search_database_index_word(db, doc2, STRING_CONST("CARS"));
        CHECK_EQ(search_database_index_count(db), 6);

        search_database_index_word(db, doc1, STRING_CONST("puppy"));
        search_database_index_word(db, doc2, STRING_CONST("PUPPIES"));
        
        search_database_deallocate(db);
    }

    TEST_CASE("Document Lists")
    {
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);

        string_const_t super_word = CTEXT("SUPER COOL!");
        auto doc1 = search_database_add_document(db, STRING_CONST("doc1"));
        auto doc2 = search_database_add_document(db, STRING_CONST("doc2"));
        auto doc3 = search_database_add_document(db, STRING_CONST("doc3"));
        auto doc4 = search_database_add_document(db, STRING_CONST("doc4"));

        CHECK_EQ(search_database_word_document_count(db, STRING_ARGS(super_word)), 0);

        search_database_index_word(db, doc1, STRING_ARGS(super_word));
        CHECK_EQ(search_database_index_count(db), 9);
        CHECK_EQ(search_database_document_count(db), 4);
        CHECK_EQ(search_database_word_document_count(db, STRING_ARGS(super_word)), 1);
        
        search_database_index_word(db, doc2, STRING_ARGS(super_word));
        CHECK_EQ(search_database_index_count(db), 9);
        CHECK_EQ(search_database_document_count(db), 4);
        CHECK_EQ(search_database_word_document_count(db, STRING_ARGS(super_word)), 2);
        
        search_database_index_word(db, doc3, STRING_ARGS(super_word));
        CHECK_EQ(search_database_index_count(db), 9);
        CHECK_EQ(search_database_document_count(db), 4);
        CHECK_EQ(search_database_word_document_count(db, STRING_ARGS(super_word)), 3);

        search_database_index_word(db, doc3, STRING_CONST("SUPER KOOL"));
        CHECK_EQ(search_database_index_count(db), 13);
        CHECK_EQ(search_database_document_count(db), 4);

        search_database_index_word(db, doc4, STRING_ARGS(super_word));
        CHECK_EQ(search_database_index_count(db), 13);
        CHECK_EQ(search_database_document_count(db), 4);
        CHECK_EQ(search_database_word_document_count(db, STRING_ARGS(super_word)), 4);

        auto doc5 = search_database_add_document(db, STRING_CONST("doc5"));

        search_database_index_word(db, doc5, STRING_CONST("SUPER"));
        CHECK_EQ(search_database_index_count(db), 14);
        CHECK_EQ(search_database_document_count(db), 5);
        CHECK_EQ(search_database_word_document_count(db, STRING_CONST("SUPER"), false), 1);
        CHECK_EQ(search_database_word_document_count(db, STRING_CONST("SUPER"), true), 5);

        CHECK_EQ(search_database_word_document_count(db, STRING_CONST("SUPER COOL"), false), 0);
        CHECK_EQ(search_database_word_document_count(db, STRING_CONST("SUPER COOL"), true), 4);

        search_database_deallocate(db);
    }
}

#endif // BUILD_DEVELOPMENT
