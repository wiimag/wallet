/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT
 
#include "test_utils.h"

#include <eod.h>
#include <stock.h>

#include <framework/common.h>
#include <framework/search_query.h>
#include <framework/search_database.h>

#include <foundation/random.h>

#include <doctest/doctest.h>

TEST_SUITE("Search")
{
    TEST_CASE("UTF-8")
    {
        const char mel[] = { 0x4d, (char)0xc3, (char)0x89, 0x4c, 0x61, 0x6e, 0x49, 0x45, 0 };

        char to_lower_buffer[32];
        char to_upper_buffer[32];

        string_t to_lower = string_to_lower_utf8(STRING_BUFFER(to_lower_buffer), STRING_CONST(mel));
        string_t to_upper = string_to_upper_utf8(STRING_BUFFER(to_upper_buffer), STRING_ARGS(to_lower));

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
        CHECK_EQ(search_database_index_count(db), 8);
        CHECK_EQ(search_database_document_count(db), 4);
        CHECK_EQ(search_database_word_document_count(db, STRING_ARGS(super_word)), 1);
        
        search_database_index_word(db, doc2, STRING_ARGS(super_word));
        CHECK_EQ(search_database_index_count(db), 8);
        CHECK_EQ(search_database_document_count(db), 4);
        CHECK_EQ(search_database_word_document_count(db, STRING_ARGS(super_word)), 2);
        
        search_database_index_word(db, doc3, STRING_ARGS(super_word));
        CHECK_EQ(search_database_index_count(db), 8);
        CHECK_EQ(search_database_document_count(db), 4);
        CHECK_EQ(search_database_word_document_count(db, STRING_ARGS(super_word)), 3);

        search_database_index_word(db, doc3, STRING_CONST("SUPER KOOL"));
        CHECK_EQ(search_database_index_count(db), 12);
        CHECK_EQ(search_database_document_count(db), 4);

        CHECK(search_database_index_word(db, doc4, STRING_ARGS(super_word)));
        CHECK_EQ(search_database_index_count(db), 12);
        CHECK_EQ(search_database_document_count(db), 4);
        CHECK_EQ(search_database_word_document_count(db, STRING_ARGS(super_word)), 4);

        auto doc5 = search_database_add_document(db, STRING_CONST("doc5"));

        search_database_index_word(db, doc5, STRING_CONST("SUPER"));
        CHECK_EQ(search_database_index_count(db), 13);
        CHECK_EQ(search_database_document_count(db), 5);
        CHECK_EQ(search_database_word_document_count(db, STRING_CONST("SUPER"), false), 1);
        CHECK_EQ(search_database_word_document_count(db, STRING_CONST("SUPER"), true), 5);

        CHECK_EQ(search_database_word_document_count(db, STRING_CONST("SUPER COOL"), false), 0);
        CHECK_EQ(search_database_word_document_count(db, STRING_CONST("SUPER COOL"), true), 4);

        search_database_deallocate(db);
    }

    TEST_CASE("Index stock description" * doctest::timeout(30))
    {
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);

        stock_handle_t stock_handle = stock_request(STRING_CONST("SEAS.US"), FetchLevel::FUNDAMENTALS);
        REQUIRE_NE(stock_handle, nullptr);

        const stock_t* stock = stock_handle;
        REQUIRE_NE(stock, nullptr);

        while (!stock->has_resolve(FetchLevel::FUNDAMENTALS))
            dispatcher_wait_for_wakeup_main_thread(100);

        string_const_t name = SYMBOL_CONST(stock->name);
        string_const_t description = SYMBOL_CONST(stock->description.fetch());
        
        auto doc1 = search_database_add_document(db, STRING_CONST("SEAS.US"));

        CHECK(search_database_index_exact_match(db, doc1, STRING_ARGS(name)));
        CHECK_EQ(search_database_index_count(db), 1);
        
        string_t* words = string_split(description, CTEXT(" "));
        foreach(w, words)
        {
            log_debugf(0, STRING_CONST("WORD: %.*s"), STRING_FORMAT(*w));
            search_database_index_word(db, doc1, STRING_ARGS(*w));
        }
        string_array_deallocate(words);

        CHECK_GT(search_database_index_count(db), 250);
        CHECK_EQ(search_database_document_count(db), 1);
        CHECK_EQ(search_database_word_document_count(db, STRING_CONST("sexy"), true), 0);
        CHECK_EQ(search_database_word_document_count(db, STRING_CONST("theme")), 1);

        search_database_deallocate(db);
    }

    TEST_CASE("Index JSON query" * doctest::timeout(30))
    {
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);

        eod_fetch("fundamentals", "GFL.TO", FORMAT_JSON_CACHE, [db](const json_object_t& json)
        {
            auto doc1 = search_database_add_document(db, STRING_CONST("GFL.TO"));
                
            for (unsigned i = 0; i < json.token_count; ++i)
            {
                const json_token_t& token = json.tokens[i];

                string_const_t id = json_token_identifier(json.buffer, &token);
                search_database_index_text(db, doc1, STRING_ARGS(id), false);
                
                if (token.type == JSON_STRING || token.type == JSON_PRIMITIVE)
                {
                    string_const_t value = json_token_value(json.buffer, &token);
                    CHECK(search_database_index_text(db, doc1, STRING_ARGS(value), true));

                    log_debugf(0, STRING_CONST("id: %.*s, value: %.*s"), STRING_FORMAT(id), STRING_FORMAT(value));
                }
            }
        
        }, 5 * 60ULL);

        CHECK_EQ(search_database_document_count(db), 1);
        CHECK_GT(search_database_index_count(db), 1000);
        CHECK_EQ(search_database_word_document_count(db, STRING_CONST("environmental")), 1);

        search_database_deallocate(db);
    }

    TEST_CASE("Search fundamentals query" * doctest::timeout(30))
    {
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);

        eod_fetch("fundamentals", "GFL.TO", FORMAT_JSON_CACHE, [db](const json_object_t& json)
        {
            auto doc1 = search_database_add_document(db, STRING_CONST("GFL.TO"));

            for (unsigned i = 0; i < json.token_count; ++i)
            {
                const json_token_t& token = json.tokens[i];

                string_const_t id = json_token_identifier(json.buffer, &token);
                if (token.type == JSON_STRING || token.type == JSON_PRIMITIVE)
                {
                    string_const_t value = json_token_value(json.buffer, &token);
                    double number;
                    if (string_try_convert_number(STRING_ARGS(value), number))
                    {
                        search_database_index_property(db, doc1, STRING_ARGS(id), number);
                    }
                    else
                    {
                        search_database_index_property(db, doc1, STRING_ARGS(id), STRING_ARGS(value));
                    }
                }
            }
        }, 5 * 60ULL);

        {
            auto query = search_database_query(db, STRING_CONST("CurrencyCode=USD"));
            REQUIRE_NE(query, SEARCH_QUERY_INVALID_ID);
            while (!search_database_query_is_completed(db, query))
                dispatcher_wait_for_wakeup_main_thread(100);
            const search_result_t* results = search_database_query_results(db, query);
            CHECK_EQ(array_size(results), 0);
            search_database_query_dispose(db, query);
        }

        {
            auto query = search_database_query(db, STRING_CONST("isin=CA36168Q1046"));
            REQUIRE_NE(query, SEARCH_QUERY_INVALID_ID);
            while (!search_database_query_is_completed(db, query))
                dispatcher_wait_for_wakeup_main_thread(100);
            const search_result_t* results = search_database_query_results(db, query);
            CHECK_EQ(array_size(results), 1);
            search_database_query_dispose(db, query);
        }

        {
            auto query = search_database_query(db, STRING_CONST("MarketCapitalization>1e6"));
            REQUIRE_NE(query, SEARCH_QUERY_INVALID_ID);
            while (!search_database_query_is_completed(db, query))
                dispatcher_wait_for_wakeup_main_thread(100);
            const search_result_t* results = search_database_query_results(db, query);
            CHECK_EQ(array_size(results), 1);
            search_database_query_dispose(db, query);
        }

        {
            auto query = search_database_query(db, STRING_CONST("name:\"mr. patrick\""));
            REQUIRE_NE(query, SEARCH_QUERY_INVALID_ID);
            while (!search_database_query_is_completed(db, query))
                dispatcher_wait_for_wakeup_main_thread(100);
            const search_result_t* results = search_database_query_results(db, query);
            CHECK_EQ(array_size(results), 1);
            search_database_query_dispose(db, query);
        }
        
        search_database_deallocate(db);
    }

    TEST_CASE("Index properties")
    {
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);

        auto doc1 = search_database_add_document(db, STRING_CONST("doc1"));
        auto doc2 = search_database_add_document(db, STRING_CONST("doc2"));
        auto doc3 = search_database_add_document(db, STRING_CONST("doc3"));
        
        CHECK_EQ(search_database_index_count(db), 0);

        CHECK(search_database_index_word(db, doc1, STRING_CONST("SUPER")));
        CHECK_EQ(search_database_index_count(db), 3);

        search_database_index_word(db, doc2, STRING_CONST("COOL"));
        CHECK_EQ(search_database_index_count(db), 5);

        search_database_index_word(db, doc3, STRING_CONST("KOOL"));
        CHECK_EQ(search_database_index_count(db), 7);

        search_database_index_property(db, doc1, STRING_CONST("property1"), STRING_CONST("value1"));
        CHECK_EQ(search_database_index_count(db), 11);

        search_database_index_property(db, doc2, STRING_CONST("n1"), 14.5);
        CHECK_EQ(search_database_index_count(db), 12);

        search_database_index_property(db, doc1, STRING_CONST("number"), 24.5);
        CHECK_EQ(search_database_index_count(db), 13);

        search_database_index_property(db, doc2, STRING_CONST("number"), 24.5);
        CHECK_EQ(search_database_index_count(db), 13);
        
        search_database_index_property(db, doc2, STRING_CONST("s"), STRING_CONST("value2"));
        CHECK_EQ(search_database_index_count(db), 17);

        search_database_index_property(db, doc3, STRING_CONST("s"), STRING_CONST("value"));
        CHECK_EQ(search_database_index_count(db), 17);

        search_database_index_property(db, doc3, STRING_CONST("number"), 42.5);
        CHECK_EQ(search_database_index_count(db), 18);
        
        search_database_index_property(db, doc3, STRING_CONST("test_123"), STRING_CONST("v1"));
        CHECK_EQ(search_database_index_count(db), 19);

        search_database_index_property(db, doc3, STRING_CONST("test_123"), STRING_CONST("three"), true);
        CHECK_EQ(search_database_index_count(db), 22);

        search_database_index_property(db, doc1, STRING_CONST("test_123"), STRING_CONST("xmas"), false);
        CHECK_EQ(search_database_index_count(db), 23);
        
        search_database_index_property(db, doc3, STRING_CONST("test_123"), STRING_CONST("value "));
        CHECK_EQ(search_database_index_count(db), 26);

        search_database_index_property(db, doc2, STRING_CONST("test_123"), STRING_CONST("value third"));
        CHECK_EQ(search_database_index_count(db), 31);

        search_database_index_property(db, doc1, STRING_CONST("price"), 100042.5);
        CHECK_EQ(search_database_index_count(db), 32);
        
        CHECK_EQ(search_database_document_count(db), 3);
        search_database_deallocate(db);
    }

    TEST_CASE("Index many numbers")
    {
        auto db = search_database_allocate(SearchDatabaseFlags::CaseSensitive);
        REQUIRE_NE(db, nullptr);

        search_document_handle_t docs[] = {
            search_database_add_document(db, STRING_CONST("doc1")),
            search_database_add_document(db, STRING_CONST("doc2")),
            search_database_add_document(db, STRING_CONST("doc3")),
            search_database_add_document(db, STRING_CONST("doc4")),
            search_database_add_document(db, STRING_CONST("doc5")),
            search_database_add_document(db, STRING_CONST("doc6")),
            search_database_add_document(db, STRING_CONST("doc7")),
        };        

        CHECK_EQ(search_database_index_count(db), 0);

        // Generate 200 random unique numbers in an array
        double numbers[200];
        for (unsigned i = 0; i < ARRAY_COUNT(numbers); ++i)
        {
            numbers[i] = random_range(0.0, 1000.0);
            for (unsigned j = 0; j < i; ++j)
            {
                if (numbers[i] == numbers[j])
                {
                    --i;
                    break;
                }
            }
        }

        for (int t = 0; t < 100; ++t)
        {
            // Index the numbers
            for (unsigned i = 0, count = ARRAY_COUNT(numbers); i < count; ++i)
            {
                CHECK(search_database_index_property(db, docs[random32_range(0, ARRAY_COUNT(docs))], STRING_CONST("Number"), numbers[i]));
            }
        }
        
        CHECK_EQ(search_database_index_count(db), ARRAY_COUNT(numbers));
        CHECK_EQ(search_database_document_count(db), ARRAY_COUNT(docs));
        search_database_deallocate(db);
    }

    TEST_CASE("Add and remove many documents" * doctest::timeout(30))
    {
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);

        search_document_handle_t docs[1024];
        for (unsigned i = 0; i < ARRAY_COUNT(docs); ++i)
        {
            docs[i] = search_database_add_document(db, STRING_CONST("doc"));
        }

        CHECK_EQ(search_database_index_count(db), 0);
        CHECK_EQ(search_database_document_count(db), ARRAY_COUNT(docs));

        CHECK(search_database_index_exact_match(db, docs[0], STRING_CONST("word")));
        CHECK_EQ(search_database_index_count(db), 1);
        CHECK(search_database_remove_document(db, docs[0]));      
        CHECK_FALSE(search_database_index_exact_match(db, docs[0], STRING_CONST("word")));
        CHECK_EQ(search_database_document_count(db), ARRAY_COUNT(docs) - 1);

        // This should cover the case where an index now has document count of 0 and we re-add a new document to it.
        CHECK(search_database_index_exact_match(db, docs[1], STRING_CONST("word")));
        CHECK_EQ(search_database_index_count(db), 1);

        // Index random words, randomly in the created documents many times
        while (search_database_document_count(db) > 0)
        {
            char word_buffer[8 + 1];
            string_const_t word = random_string(STRING_BUFFER(word_buffer));

            for (int d = 0; d < ARRAY_COUNT(docs) / 2; ++d)
                search_database_index_word(db, docs[random32_range(0, ARRAY_COUNT(docs))], STRING_ARGS(word));

            // Remove a random document
            const uint32_t doc_index = random32_range(0, ARRAY_COUNT(docs));
            const search_document_handle_t doc_handle = docs[doc_index];
            if (search_database_is_document_valid(db, doc_handle))
            {
                if (!search_database_remove_document(db, doc_handle))
                    docs[doc_index] = SEARCH_DOCUMENT_INVALID_ID;
            }
        }
        
        CHECK_GT(search_database_index_count(db), 1);
        CHECK_EQ(search_database_document_count(db), 0);
        search_database_deallocate(db);
    }

    TEST_CASE("Indexing validation")
    {
        // Create a new document
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);

        CHECK_FALSE(search_database_is_document_valid(db, 0));
        CHECK_FALSE(search_database_is_document_valid(db, 99));

        search_document_handle_t doc = search_database_add_document(db, STRING_CONST("doc"));
        REQUIRE_NE(doc, SEARCH_DOCUMENT_INVALID_ID);
        
        CHECK_EQ(search_database_word_count(db), 0);

        // Index some text
        CHECK(search_database_index_text(db, doc, STRING_CONST("this is a short phrase")));
        CHECK_EQ(search_database_word_count(db), 9);

        // Index a number
        CHECK(search_database_index_property(db, doc, STRING_CONST("$"), 88));
        CHECK_EQ(search_database_word_count(db), 10);

        // Index a property (short and phrase should already be encoded)
        CHECK(search_database_index_property(db, doc, STRING_CONST("short"), STRING_CONST("phrase")));
        CHECK_EQ(search_database_word_count(db), 10);

        // Remove the document
        CHECK(search_database_remove_document(db, doc));
        CHECK_FALSE(search_database_is_document_valid(db, doc));

        // Indexing should fail
        CHECK_FALSE(search_database_index_text(db, doc, STRING_CONST("test")));
        CHECK_FALSE(search_database_index_property(db, doc, STRING_CONST("price"), 18));
        CHECK_FALSE(search_database_index_property(db, doc, STRING_CONST("name"), STRING_CONST("sam")));

        // Removing documents doesn't affect the database string table.
        CHECK_EQ(search_database_word_count(db), 10);

        search_database_deallocate(db);
    }

    TEST_CASE("Contains word")
    {
        // Create a new document
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);

        search_document_handle_t doc = search_database_add_document(db, STRING_CONST("doc"));
        REQUIRE_NE(doc, SEARCH_DOCUMENT_INVALID_ID);

        CHECK_EQ(search_database_word_count(db), 0);
        CHECK_FALSE(search_database_contains_word(db, STRING_CONST("this")));

        // Index some text
        CHECK(search_database_index_text(db, doc, STRING_CONST("this is a SHORT phrase")));
        CHECK_EQ(search_database_word_count(db), 9);
        
        CHECK_FALSE(search_database_contains_word(db, nullptr, 10));
        CHECK_FALSE(search_database_contains_word(db, "phrase", 0));
        CHECK(search_database_contains_word(db, STRING_CONST("this")));
        CHECK_FALSE(search_database_contains_word(db, STRING_CONST("is")));
        CHECK_FALSE(search_database_contains_word(db, STRING_CONST("a")));
        CHECK(search_database_contains_word(db, STRING_CONST("short")));
        CHECK(search_database_contains_word(db, STRING_CONST("shor")));
        CHECK(search_database_contains_word(db, STRING_CONST("sho")));
        CHECK_FALSE(search_database_contains_word(db, STRING_CONST("sh")));
        CHECK(search_database_contains_word(db, STRING_CONST("PHRASE")));

        search_database_deallocate(db);
    }

    TEST_CASE("Query 1")
    {
        // Create a new document
        auto db = search_database_allocate();
        REQUIRE_NE(db, nullptr);

        search_document_handle_t doc = search_database_add_document(db, STRING_CONST("doc"));
        REQUIRE_NE(doc, SEARCH_DOCUMENT_INVALID_ID);

        CHECK(search_database_index_word(db, doc, STRING_CONST("joe")));
        CHECK(search_database_index_word(db, doc, STRING_CONST("2023")));
        CHECK(search_database_index_property(db, doc, STRING_CONST("name"), STRING_CONST("joe")));
        CHECK(search_database_index_property(db, doc, STRING_CONST("age"), 18));
        CHECK(search_database_index_property(db, doc, STRING_CONST("height"), 1.8f));
        CHECK(search_database_index_property(db, doc, STRING_CONST("weight"), 80.0f));

        search_document_handle_t sam = search_database_add_document(db, STRING_CONST("Samuel"));
        REQUIRE_NE(sam, SEARCH_DOCUMENT_INVALID_ID);
        
        CHECK(search_database_index_property(db, doc, STRING_CONST("name"), STRING_CONST("SAM")));
        CHECK(search_database_index_property(db, doc, STRING_CONST("age"), 7));

        search_document_handle_t textdoc = search_database_add_document(db, STRING_CONST("short text"));
        REQUIRE_NE(textdoc, SEARCH_DOCUMENT_INVALID_ID);
        
        CHECK(search_database_index_text(db, textdoc, STRING_CONST("this is a short phrase created by joe at the age of 18")));

        string_const_t query = CTEXT("joe");
        search_query_handle_t q = search_database_query(db, STRING_ARGS(query));
                

        search_database_deallocate(db);
    }
    
}

struct SearchQueryFixture
{
    search_database_t* db;

    search_document_handle_t joe;
    search_document_handle_t bob;
    search_document_handle_t will;
    search_document_handle_t mel;
    search_document_handle_t mag;
    search_document_handle_t yolland;

    search_query_handle_t query{ 0 };

    SearchQueryFixture()
    {
        db = search_database_allocate();

        joe = search_database_add_document(db, STRING_CONST("Joe"));            // 1 <- Expected document handle index values
        bob = search_database_add_document(db, STRING_CONST("Bob"));            // 2
        will = search_database_add_document(db, STRING_CONST("Will"));          // 3
        mel = search_database_add_document(db, STRING_CONST("Mel"));            // 4
        mag = search_database_add_document(db, STRING_CONST("Mag"));            // 5
        yolland = search_database_add_document(db, STRING_CONST("Yolland"));    // 6

        search_database_index_text(db, joe, STRING_CONST("joe smith"));
        search_database_index_text(db, bob, STRING_CONST("bob smith"));
        search_database_index_text(db, will, STRING_CONST("will schmidt"));
        search_database_index_text(db, mel, STRING_CONST("mel cadotte"));
        search_database_index_text(db, mag, STRING_CONST("mag cadotte schmidt"));
        search_database_index_text(db, yolland, STRING_CONST("yolland smitton"));
        
        search_database_index_property(db, joe, STRING_CONST("age"), 40);
        search_database_index_property(db, bob, STRING_CONST("age"), 55);
        search_database_index_property(db, will, STRING_CONST("age"), 14);
        search_database_index_property(db, mel, STRING_CONST("age"), 39);
        search_database_index_property(db, mag, STRING_CONST("age"), 10);
        search_database_index_property(db, yolland, STRING_CONST("age"), 101);
        
        search_database_index_property(db, joe, STRING_CONST("height"), 1.8f);
        search_database_index_property(db, bob, STRING_CONST("height"), 1.6f);
        search_database_index_property(db, will, STRING_CONST("height"), 1.79f);
        search_database_index_property(db, mel, STRING_CONST("height"), 1.7f);
        search_database_index_property(db, mag, STRING_CONST("height"), 1.6f);
        search_database_index_property(db, yolland, STRING_CONST("height"), 1.5f);
        
        search_database_index_property(db, joe, STRING_CONST("weight"), 80.0f);
        search_database_index_property(db, bob, STRING_CONST("weight"), 90.0f);
        search_database_index_property(db, will, STRING_CONST("weight"), 70.0f);
        search_database_index_property(db, mel, STRING_CONST("weight"), 60.0f);
        search_database_index_property(db, mag, STRING_CONST("weight"), 40.0f);
        search_database_index_property(db, yolland, STRING_CONST("weight"), 40.0f);

        search_database_index_property(db, joe, STRING_CONST("job"), STRING_CONST("retired"));
        search_database_index_property(db, bob, STRING_CONST("job"), STRING_CONST("manager"));
        search_database_index_property(db, will, STRING_CONST("job"), STRING_CONST("student"));
        search_database_index_property(db, mel, STRING_CONST("job"), STRING_CONST("hr"));
        search_database_index_property(db, mag, STRING_CONST("job"), STRING_CONST("student"));
        search_database_index_property(db, yolland, STRING_CONST("job"), STRING_CONST("retired"));

        search_database_index_property(db, joe, STRING_CONST("name"), STRING_CONST("Jonathan"));
        search_database_index_property(db, bob, STRING_CONST("name"), STRING_CONST("Robert"));
        search_database_index_property(db, will, STRING_CONST("name"), STRING_CONST("William"));
        search_database_index_property(db, mel, STRING_CONST("name"), STRING_CONST("Mélanie"));
        search_database_index_property(db, mag, STRING_CONST("name"), STRING_CONST("Magaly"));
        search_database_index_property(db, yolland, STRING_CONST("name"), STRING_CONST("Yolland"));
    }

    const search_result_t* evaluate_query_sync(string_const_t query_string)
    {
        CHECK_MESSAGE(query == 0, "Query already in progress");

        log_infof(0, STRING_CONST("Query: %.*s"), STRING_FORMAT(query_string));

        REQUIRE_NOTHROW(query = search_database_query(db, STRING_ARGS(query_string)));
        REQUIRE_NE(query, SEARCH_QUERY_INVALID_ID);
        while (!search_database_query_is_completed(db, query))
            dispatcher_wait_for_wakeup_main_thread(100);

        const search_result_t* results = search_database_query_results(db, query);
        foreach(r, results)
        {
            string_const_t document_name = search_database_document_name(db, (search_document_handle_t)r->id);
            log_infof(0, STRING_CONST("Result: %.*s (%" PRIhash ")"), STRING_FORMAT(document_name), r->id);
        }

        return results;
    }

    ~SearchQueryFixture()
    {
        if (search_database_query_dispose(db, query))
            query = 0;
        search_database_deallocate(db);
    }
};

TEST_SUITE("SearchQuery")
{
    TEST_CASE("Parser")
    {
        /*
         * Query examples:
         *      number>32 and joe
         *      number>32 and (joe or bob)
         *      number>32 and (joe or bob) and not (joe and bob)
         *      "number> 32" -joe
         *      "single word"
         *      name=sam
         *      name=sam and age>32
         *      last_name!=schmidt
         *      name=sam and age>32 and (last_name!=schmidt or last_name!=smith)
         */

        {
            search_query_token_t* tokens = search_query_parse_tokens(STRING_CONST("\"number > 32\" -(-joe -last!=smith)"));
            REQUIRE_EQ(array_size(tokens), 2);
            CHECK_EQ(tokens[0].type, SearchQueryTokenType::Literal);
            CHECK_EQ(tokens[1].type, SearchQueryTokenType::Not);
            REQUIRE_EQ(array_size(tokens[1].children), 1);
            CHECK_EQ(tokens[1].children[0].children[1].children[0].type, SearchQueryTokenType::Property);
            CHECK_EQ(tokens[1].children[0].children[1].children[0].name, CTEXT("last"));
            search_query_deallocate_tokens(tokens);
        }

        {
            search_query_token_t* tokens = search_query_parse_tokens(STRING_CONST("(bob and func(smith))"));
            REQUIRE_EQ(array_size(tokens), 1);
            REQUIRE_EQ(array_size(tokens[0].children), 3);
            CHECK_EQ(tokens[0].children[0].type, SearchQueryTokenType::Word);
            CHECK_EQ(tokens[0].children[1].type, SearchQueryTokenType::And);
            CHECK_EQ(tokens[0].children[2].type, SearchQueryTokenType::Function);
            search_query_deallocate_tokens(tokens);
        }

        {
            search_query_token_t* tokens = search_query_parse_tokens(STRING_CONST("not (joe and (bob (kim or -yolland)) suzy) -will age<=10 or age>=20"));
            REQUIRE_EQ(array_size(tokens), 5);
            CHECK_EQ(tokens[0].type, SearchQueryTokenType::Not);
            REQUIRE_EQ(tokens[0].children[0].type, SearchQueryTokenType::Group);
            {
                REQUIRE_EQ(array_size(tokens[0].children[0].children), 4);
                CHECK_EQ(tokens[0].children[0].children[0].type, SearchQueryTokenType::Word);
                CHECK_EQ(tokens[0].children[0].children[1].type, SearchQueryTokenType::And);
                CHECK_EQ(tokens[0].children[0].children[2].type, SearchQueryTokenType::Group);
                CHECK_EQ(tokens[0].children[0].children[3].type, SearchQueryTokenType::Word);

            }
            CHECK_EQ(tokens[1].type, SearchQueryTokenType::Not);
            CHECK_EQ(tokens[2].type, SearchQueryTokenType::Property);
            CHECK_EQ(tokens[3].type, SearchQueryTokenType::Or);
            CHECK_EQ(tokens[4].type, SearchQueryTokenType::Property);
            search_query_deallocate_tokens(tokens);
        }

        {
            search_query_token_t* tokens = search_query_parse_tokens(STRING_CONST("-will - space age<=10 or age>=20"));
            REQUIRE_EQ(array_size(tokens), 5);
            CHECK_EQ(tokens[0].type, SearchQueryTokenType::Not);
            CHECK_EQ(tokens[0].children[0].type, SearchQueryTokenType::Word);
            CHECK_EQ(tokens[1].type, SearchQueryTokenType::Not);
            CHECK_EQ(tokens[1].children[0].type, SearchQueryTokenType::Word);
            CHECK_EQ(tokens[2].type, SearchQueryTokenType::Property);
            CHECK_EQ(tokens[3].type, SearchQueryTokenType::Or);
            CHECK_EQ(tokens[4].type, SearchQueryTokenType::Property);
            search_query_deallocate_tokens(tokens);
        }

        {
            search_query_token_t* tokens = search_query_parse_tokens(STRING_CONST("         age<=10       or age>= 2"));
            REQUIRE_EQ(array_size(tokens), 3);
            CHECK_EQ(tokens[0].type, SearchQueryTokenType::Property);
            CHECK_EQ(tokens[1].type, SearchQueryTokenType::Or);
            CHECK_EQ(tokens[2].type, SearchQueryTokenType::Property);
            search_query_deallocate_tokens(tokens);
        }

        {
            search_query_token_t* tokens = search_query_parse_tokens(STRING_CONST("age>=20"));
            REQUIRE_EQ(array_size(tokens), 1);
            CHECK_EQ(tokens[0].type, SearchQueryTokenType::Property);
            REQUIRE_EQ(array_size(tokens[0].children), 1);
            CHECK_EQ(tokens[0].children[0].type, SearchQueryTokenType::Word);
            CHECK_EQ(tokens[0].children[0].children, nullptr);
            search_query_deallocate_tokens(tokens);
        }

        {
            search_query_token_t* tokens = search_query_parse_tokens(STRING_CONST("  number>32   "));
            REQUIRE_EQ(array_size(tokens), 1);
            CHECK_EQ(tokens[0].type, SearchQueryTokenType::Property);
            REQUIRE_EQ(array_size(tokens[0].children), 1);
            CHECK_EQ(tokens[0].children[0].type, SearchQueryTokenType::Word);
            CHECK_EQ(tokens[0].children[0].children, nullptr);
            search_query_deallocate_tokens(tokens);
        }
    }

    TEST_CASE("Evaluate")
    {
        string_const_t query_string = CTEXT(R"(
            number>32 and ("joe smith" or (bob and func(smith))) and 
                not (joe and (bob (kim or -yolland)) suzy) -will age<=10 or age>=20
        )");

        search_query_t* query = search_query_allocate(STRING_ARGS(query_string));

        search_query_evaluate(query, [](
            string_const_t name,
            string_const_t value,
            search_query_eval_flags_t flags,
            search_result_t* and_set,
            void* user_data)
            {
                log_infof(0, STRING_CONST("Evaluating %28s -> Name: %-8.*s -> Value: %-10.*s -> AndSet: 0x%x (%u) -> Data: 0x%x"),
                    search_query_eval_flags_to_string(flags), STRING_FORMAT(name), STRING_FORMAT(value),
                    and_set, array_size(and_set), user_data);
                return nullptr;
            }, nullptr);

        search_query_deallocate(query);
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 1" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            smith
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 2);
        CHECK(array_contains(results, joe));
        CHECK(array_contains(results, bob));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 2" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            SMITH OR CADOTTE
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 4);
        CHECK(array_contains(results, joe));
        CHECK(array_contains(results, bob));
        CHECK(array_contains(results, mel));
        CHECK(array_contains(results, mag));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 3" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            schmidt and CADOTTE
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 1);
        CHECK(array_contains(results, mag));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 4" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            (schmidt or CADOTTE) and (joe or will)
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 1);
        CHECK(array_contains(results, will));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 5" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            smit or pascal
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 3);
        CHECK(array_contains(results, joe));
        CHECK(array_contains(results, bob));
        CHECK(array_contains(results, yolland));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 6" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            (((smit) or (pascal)) or ((will)))
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 4);
        CHECK(array_contains(results, joe));
        CHECK(array_contains(results, bob));
        CHECK(array_contains(results, will));
        CHECK(array_contains(results, yolland));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 7" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            ((schmidt) (cAdoTtE)) or (yoll smitt)
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 2);
        CHECK(array_contains(results, mag));
        CHECK(array_contains(results, yolland));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 8" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            cadotte -schmidt
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 1);
        CHECK(array_contains(results, mel));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 9" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            -cadotte or -schmidt
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 5);
        CHECK_FALSE(array_contains(results, mag));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 10" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            -cadotte AND -"schmidt"
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 3);
        CHECK(array_contains(results, joe));
        CHECK(array_contains(results, bob));
        CHECK(array_contains(results, yolland));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 11" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            age=40 or age:40
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 1);
        CHECK(array_contains(results, joe));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 12" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            -age=40
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 5);
        CHECK_FALSE(array_contains(results, joe));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 13" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            age<40
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 3);
        CHECK(array_contains(results, mag));
        CHECK(array_contains(results, mel));
        CHECK(array_contains(results, will));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 14" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            age<40 and age>=14
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 2);
        CHECK(array_contains(results, mel));
        CHECK(array_contains(results, will));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 15" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            (job=retire age>14 weight>40) or (job=student)
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 3);
        CHECK(array_contains(results, joe));
        CHECK(array_contains(results, will));
        CHECK(array_contains(results, mag));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 16" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            -job=retire age>14
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 2);
        CHECK(array_contains(results, mel));
        CHECK(array_contains(results, bob));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 17" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            age>14 -job:RET
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 2);
        CHECK(array_contains(results, mel));
        CHECK(array_contains(results, bob));
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 18" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            -age>-100 name:smi
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 0);
    }

    TEST_CASE_FIXTURE(SearchQueryFixture, "Query 18" * doctest::timeout(30))
    {
        string_const_t query_string = CTEXT(R"(
            name=MÉlanie cadotte age>=39
        )");

        const search_result_t* results = evaluate_query_sync(query_string);
        REQUIRE_EQ(array_size(results), 1);
        CHECK(array_contains(results, mel));
    }
}

#endif // BUILD_DEVELOPMENT
