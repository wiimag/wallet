/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT
 
#include "test_utils.h"

#include <eod.h>
#include <stock.h>
#include <search.h>
#include <search_query.h>

#include <framework/common.h>

#include <foundation/random.h>

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
            string_const_t word = random_string(STRING_CONST_BUFFER(word_buffer));

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

     // Example: number>32 and (joe or (bob and func(smith))) and not (joe and (bob (kim or -yolland)) suzy) -will age<=10 or age>=20
     // 
     //          number>32
     //   AND    (joe or (bob and smith)) and not (joe and (bob (kim or -yolland)) suzy) -will age<=10 or age>=20
     //              (joe or (bob and smith))
     //                  joe
     //    OR            (bob and smith)
     //                      bob
     //                      smith
     //   AND         not (joe and (bob (kim or -yolland)) suzy) -will age<=10 or age>=20
     //                  not (joe and (bob (kim or -yolland)) suzy)
     //                      (joe and (bob (kim or -yolland)) suzy)
     //                          joe
     //                          (bob (kim or -yolland)) suzy
     //                              bob (kim or -yolland)
     //                                  bob
     //                                  kim or -yolland
     //                                      kim
     //                                      -yolland
     //                              suzy
     //                  -will age<=10 or age>=20
     //                      -will
     //                          will
     //                      age<=10 or age>= 20
     //                          age<=10
     //                          age>=20

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

    TEST_CASE("Compiler")
    {
        string_const_t query_string = CTEXT(R"(
            number>32 and ("joe smith" or (bob and func(smith))) and 
                not (joe and (bob (kim or -yolland)) suzy) -will age<=10 or age>=20
        )");
        
        search_query_t* query = search_query_allocate(STRING_ARGS(query_string));
        
        search_query_deallocate(query);
    }
}

#endif // BUILD_DEVELOPMENT
