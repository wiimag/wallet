/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "search.h"

#include "eod.h"

#include <framework/imgui.h>
#include <framework/string.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/service.h>
#include <framework/database.h>
#include <framework/dispatcher.h>
#include <framework/string_table.h>
#include <framework/search_query.h>
#include <framework/search_database.h>
#include <framework/profiler.h>

#include <foundation/stream.h>

#define HASH_SEARCH static_hash_string("search", 6, 0xc9d4e54fbae76425ULL)

static struct SEARCH_MODULE {
    
    search_database_t* db{ nullptr };
    dispatcher_thread_handle_t indexing_thread{};

} *_search;

//
// PRIVATE
//

FOUNDATION_STATIC void search_index_fundamental_data(const json_object_t& json, search_document_handle_t doc)
{
    search_database_t* db = _search->db;

    // Get description
    string_const_t description = json["General"]["Description"].as_string();
    if (!string_is_null(description))
        search_database_index_text(db, doc, STRING_ARGS(description), false);

    // Get industry
    string_const_t industry = json["General"]["Industry"].as_string();
    if (!string_is_null(industry))
        search_database_index_text(db, doc, STRING_ARGS(industry), true);

    // Get sector
    string_const_t sector = json["General"]["Sector"].as_string();
    if (!string_is_null(sector))
        search_database_index_text(db, doc, STRING_ARGS(sector), true);

    // Get GicSector
    string_const_t gic_sector = json["General"]["GicSector"].as_string();
    if (!string_is_null(gic_sector))
        search_database_index_text(db, doc, STRING_ARGS(gic_sector), true);

    // Get GicGroup
    string_const_t gic_group = json["General"]["GicGroup"].as_string();
    if (!string_is_null(gic_group))
        search_database_index_text(db, doc, STRING_ARGS(gic_group), true);

    // Get GicIndustry
    string_const_t gic_industry = json["General"]["GicIndustry"].as_string();
    if (!string_is_null(gic_industry))
        search_database_index_text(db, doc, STRING_ARGS(gic_industry), true);

    // Get GicSubIndustry
    string_const_t gic_sub_industry = json["General"]["GicSubIndustry"].as_string();
    if (!string_is_null(gic_sub_industry))
        search_database_index_text(db, doc, STRING_ARGS(gic_sub_industry), true);

    // Get HomeCategory
    string_const_t home_category = json["General"]["HomeCategory"].as_string();
    if (!string_is_null(home_category))
        search_database_index_text(db, doc, STRING_ARGS(home_category), true);
        
    // Get Address
    string_const_t address = json["General"]["Address"].as_string();
    if (!string_is_null(address))
        search_database_index_text(db, doc, STRING_ARGS(address), true);

    for (unsigned i = 0; i < json.token_count; ++i)
    {
        const json_token_t& token = json.tokens[i];

        string_const_t id = json_token_identifier(json.buffer, &token);
        if (id.length == 0 || id.length > 30)
            continue;
        if (token.type == JSON_STRING || token.type == JSON_PRIMITIVE)
        {
            double number = NAN;
            string_const_t value = json_token_value(json.buffer, &token);
            if (string_try_convert_number(STRING_ARGS(value), number))
            {
                search_database_index_property(db, doc, STRING_ARGS(id), number);
            }
            else if (value.length > 0 && value.length < 30)
            {
                if (string_equal_nocase(STRING_ARGS(id), STRING_CONST("name")))
                    search_database_index_text(db, doc, STRING_ARGS(value), false);
                else
                    search_database_index_property(db, doc, STRING_ARGS(id), STRING_ARGS(value), false);
            }
        }
    }
}

FOUNDATION_STATIC void search_index_exchange_symbols(const json_object_t& data, bool* stop_indexing)
{
    search_database_t* db = _search->db;

    for(auto e : data)
    {        
        if (e.root == nullptr || e.root->type != JSON_OBJECT)
            continue;

        string_const_t code = e["Code"].as_string();
        if (string_is_null(code))
            continue;

        string_const_t exchange = e["Exchange"].as_string();
        if (string_is_null(exchange))
            continue;

        string_const_t isin = e["Isin"].as_string();
        string_const_t type = e["Type"].as_string();

        // Do not index FUND stock and those with no ISIN
        if (string_is_null(isin) || string_equal_nocase(STRING_ARGS(type), STRING_CONST("FUND")))
            continue;

        char symbol_buffer[32];
        string_t symbol = string_format(STRING_CONST_BUFFER(symbol_buffer), STRING_CONST("%.*s.%.*s"), STRING_FORMAT(code), STRING_FORMAT(exchange));

        search_document_handle_t doc = search_database_find_document(db, STRING_ARGS(symbol));
        if (doc != SEARCH_DOCUMENT_INVALID_ID)
        {
            time_t doc_timestamp = search_database_document_timestamp(db, doc);
            if (time_elapsed_days(doc_timestamp, time_now()) < 7)
                continue;
        }

        if (doc == SEARCH_DOCUMENT_INVALID_ID)
            doc = search_database_add_document(db, STRING_ARGS(symbol));

        // Index symbol
        search_database_index_word(db, doc, STRING_ARGS(symbol), true);
            
        // Index basic information
        string_const_t name = e["Name"].as_string();
        if (!string_is_null(name))
            search_database_index_text(db, doc, STRING_ARGS(name));

        string_const_t country = e["Country"].as_string();
        if (!string_is_null(country))
            search_database_index_text(db, doc, STRING_ARGS(country), false);

        string_const_t currency = e["Currency"].as_string();
        if (!string_is_null(currency))
            search_database_index_word(db, doc, STRING_ARGS(currency), false);
            
        if (!string_is_null(isin))
            search_database_index_word(db, doc, STRING_ARGS(isin), false);

        if (!string_is_null(type))
            search_database_index_text(db, doc, STRING_ARGS(type), false);

        log_infof(HASH_SEARCH, STRING_CONST("[%5u] Indexing [%12.*s] %-10.*s -> %-14.*s -> %.*s"), 
            doc, STRING_FORMAT(isin), STRING_FORMAT(symbol), STRING_FORMAT(type), STRING_FORMAT(name));

        // Fetch symbol fundamental data
        if (!eod_fetch("fundamentals", symbol.str, FORMAT_JSON_CACHE, LC1(search_index_fundamental_data(_1, doc)), 90 * 24 * 60 * 60ULL))
        {
            log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to fetch %.*s fundamental"), STRING_FORMAT(symbol));
        }
        else
        {
            if (thread_try_wait(1000))
            {
                *stop_indexing = true;
                break;
            }
        }
    }
}

FOUNDATION_STATIC void* search_indexing_thread_fn(void* data)
{
    MEMORY_TRACKER(HASH_SEARCH);

    string_const_t stock_markets[] = {
        CTEXT("TO"),
        CTEXT("NEO"),
        CTEXT("V"),
        CTEXT("US")
    };

    bool stop_indexing = false;
    
    // Fetch all titles from stock exchange market
    for (auto market : stock_markets)
    {
        if (stop_indexing)
            break;
            
        if (thread_try_wait(1000))
            break;
            
        auto fetch_fn = [&stop_indexing](const json_object_t& data) { search_index_exchange_symbols(data, &stop_indexing); };        
        if (!eod_fetch("exchange-symbol-list", market.str, FORMAT_JSON_CACHE, fetch_fn, 30 * 24 * 60 * 60ULL))
        {
            log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to fetch %.*s symbols"), STRING_FORMAT(market));
        }
    }    

    return 0;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void search_initialize()
{
    _search = MEM_NEW(HASH_SEARCH, SEARCH_MODULE);
    
    // Load search database
    _search->db = search_database_allocate();
    string_const_t search_db_path = session_get_user_file_path(STRING_CONST("search.db"));
    stream_t* search_db_stream = fs_open_file(STRING_ARGS(search_db_path), STREAM_IN | STREAM_BINARY);
    if (search_db_stream)
    {
        TIME_TRACKER("Loading search database");
        search_database_load(_search->db, search_db_stream);
        stream_deallocate(search_db_stream);
    }

    // Start indexing thread that query a stock exchange market and then 
    // for each title query its fundamental values to build a search database.
    _search->indexing_thread = dispatch_thread("Search Indexer", search_indexing_thread_fn);
}

FOUNDATION_STATIC void search_shutdown()
{   
    dispatcher_thread_stop(_search->indexing_thread);
    
    // Save search database on disk
    string_const_t search_db_path = session_get_user_file_path(STRING_CONST("search.db"));
    stream_t* search_db_stream = fs_open_file(STRING_ARGS(search_db_path), STREAM_CREATE | STREAM_OUT | STREAM_BINARY | STREAM_TRUNCATE);
    if (search_db_stream)
    {
        TIME_TRACKER("Saving search database");
        search_database_save(_search->db, search_db_stream);
        stream_deallocate(search_db_stream);
    }
    
    search_database_deallocate(_search->db);

    MEM_DELETE(_search);
}

DEFINE_SERVICE(SEARCH, search_initialize, search_shutdown, SERVICE_PRIORITY_MODULE);
