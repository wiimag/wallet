/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "search.h"

#include "eod.h"
#include "stock.h"
#include "events.h"
#include "settings.h"
#include "pattern.h"
#include "logo.h"
#include "news.h"
#include "imwallet.h"
#include "report.h"
#include "backend.h"

#include <framework/imgui.h>
#include <framework/session.h>
#include <framework/module.h>
#include <framework/dispatcher.h>
#include <framework/search_database.h>
#include <framework/profiler.h>
#include <framework/table.h>
#include <framework/table_expr.h>
#include <framework/expr.h>
#include <framework/window.h>
#include <framework/array.h>
#include <framework/system.h>
#include <framework/shared_mutex.h>

#include <foundation/stream.h>
#include <foundation/thread.h>

#define HASH_SEARCH static_hash_string("search", 6, 0xc9d4e54fbae76425ULL)

constexpr const char* SEARCH_EXCHANGES_SESSION_KEY = "search_exchanges";

constexpr string_const_t COMMON_STOCK_WORDS[] = {
    CTEXT("the"), CTEXT("and"), CTEXT("inc"), CTEXT("this"), CTEXT("that"), CTEXT("not"), CTEXT("are"),
    CTEXT("was"), CTEXT("were"), CTEXT("been"), CTEXT("have"), CTEXT("has"), CTEXT("had"), CTEXT("does"),
    CTEXT("did"), CTEXT("can"), CTEXT("could"), CTEXT("may"), CTEXT("might"), CTEXT("must"), CTEXT("shall"),
    CTEXT("its"), CTEXT("also"), CTEXT("such"), CTEXT("only"), CTEXT("more"), CTEXT("most"), CTEXT("less"),
    CTEXT("with"), CTEXT("without"), CTEXT("into"), CTEXT("onto"), CTEXT("out"), CTEXT("off"), CTEXT("on"),
    CTEXT("should"), CTEXT("will"), CTEXT("would"), CTEXT("for"), CTEXT("from"), CTEXT("stock"), CTEXT("common"),
    CTEXT("through"), CTEXT("between"), CTEXT("during"), CTEXT("under"), CTEXT("over"), CTEXT("before"),
    CTEXT("company"), CTEXT("companies"), 
    CTEXT("provide"), CTEXT("provides"),
    CTEXT("annual"), CTEXT("known"),
    CTEXT("flow"), CTEXT("value"), CTEXT("growth"), CTEXT("rate"), CTEXT("rates"), CTEXT("price"), CTEXT("prices"),
    CTEXT("inc"), CTEXT("ltd"), 
    CTEXT("corp"), CTEXT("corporation"), CTEXT("limited"),
    CTEXT("mr"), CTEXT("mrs"), CTEXT("ms"), CTEXT("dr"), CTEXT("prof"), CTEXT("jr"), CTEXT("sr"), CTEXT("llc"),
    CTEXT("share"), CTEXT("shares"), 
    CTEXT("investor"), CTEXT("investors"),
    CTEXT("asset"), CTEXT("assets"),
    CTEXT("market"), CTEXT("markets"),
    CTEXT("earning"), CTEXT("earnings"),
    CTEXT("shareholder"), CTEXT("shareholders"),
    CTEXT("product"), CTEXT("products"), CTEXT("service"), CTEXT("services"),
    CTEXT("business"), CTEXT("industry"), CTEXT("sector"), CTEXT("sector"), CTEXT("industry"),
    CTEXT("result"), CTEXT("results"),
    CTEXT("cash"), CTEXT("per"), CTEXT("equity"), CTEXT("other"), CTEXT("stat"), CTEXT("officer"),
    CTEXT("well"), CTEXT("project"), CTEXT("board"), 
    CTEXT("director"), CTEXT("directors"), CTEXT("executive"), CTEXT("executives"), CTEXT("chief"),
    CTEXT("own"), CTEXT("headquartered"), CTEXT("incorporated"), CTEXT("financial"), CTEXT("management"),
    CTEXT("operate"), CTEXT("operating"), CTEXT("operates"), CTEXT("operated"), CTEXT("operate"), CTEXT("operates"),
    CTEXT("name"), CTEXT("changed"), 
    CTEXT("news"),
    CTEXT("founded"), CTEXT("located"),
    CTEXT("property"), CTEXT("properties"),
    CTEXT("engage"), CTEXT("engages"),
    CTEXT("group"), CTEXT("groups"),
    CTEXT("hold"), CTEXT("holds"),
    CTEXT("holdings"), CTEXT("holding"),
    CTEXT("area"), CTEXT("areas"),
    CTEXT("state"), CTEXT("states"),
    CTEXT("street"), CTEXT("avenue"), CTEXT("road"), CTEXT("boulevard"), CTEXT("drive"), CTEXT("lane"), CTEXT("court"),
};

static const ImU32 SEARCH_PATTERN_VIEWED_COLOR = (ImU32)ImColor::HSV(0.6f, 0.3f, 0.9f);

constexpr string_const_t SEARCH_SKIP_FIELDS_FOR_INDEXING[] = {
    CTEXT("date"),
    CTEXT("Title"),
    CTEXT("Description"),
    CTEXT("Address"),
    CTEXT("NumberDividendsByYear"),
    CTEXT("outstandingShares"),
    CTEXT("WebURL"),
    CTEXT("LogoURL"),
    CTEXT("secLink"),
    CTEXT("Disclaimer"),
    CTEXT("Company_URL"),
    CTEXT("ETF_URL"),
    CTEXT("Address"),
    CTEXT("Fixed_Income"),
    CTEXT("Asset_Allocation"),
    CTEXT("World_Regions"),
    CTEXT("Sector_Weights"),
    CTEXT("Holdings"),
    CTEXT("Holders"),
    CTEXT("InsiderTransactions"),
    CTEXT("Earnings"),
    CTEXT("Financials"),
    CTEXT("Listings"),
    //CTEXT("ESGScores"),
    CTEXT("Valuations_Growth"),
    CTEXT("Top_10_Holdings"),
    CTEXT("currency_symbol"),
    CTEXT("Phone"),
};

struct search_window_t;

typedef enum class SearchResultSourceType {

    Undefined = 0,
    EODApi,
    Database,

} search_result_source_type_t;

struct search_result_entry_t
{
    search_result_source_type_t source_type{ SearchResultSourceType::Undefined };

    search_database_t*          db{ nullptr };
    search_document_handle_t    doc{ SEARCH_DOCUMENT_INVALID_ID };

    char                        symbol[16];
    stock_handle_t              stock{};
    tick_t                      uptime{ 0 };
    bool                        viewed{ false };
    string_t                    description{};
                                
    search_window_t*            window{ nullptr };
};

struct search_window_t
{
    search_database_t*             db{ nullptr };
    table_t*                       table{ nullptr };
    search_result_entry_t*         results{ nullptr };
    char                           query[1024] = { 0 };
    tick_t                         query_tick{ 0 };
    dispatcher_event_listener_id_t event_db_loaded{ INVALID_DISPATCHER_EVENT_LISTENER_ID };
    dispatcher_event_listener_id_t event_query_updated{ INVALID_DISPATCHER_EVENT_LISTENER_ID };

    char                           error[1024] = { 0 };

    tick_t                         delayed_tick{ 0 };
    bool                           delayed_input{ false };

    shared_mutex_t                 lock;
    window_handle_t                handle{0};
};

static struct SEARCH_MODULE {

    search_database_t*          db{ nullptr };
    char                        query[1024] = { 0 };
    dispatcher_thread_handle_t  indexing_thread{};
    string_t*                   saved_queries{ nullptr };
    event_handle_t              startup_signal{};

    /*! Stock exchanges to index. */
    string_t*                   exchanges{ nullptr };
    shared_mutex                exchanges_lock{};

} *_search;

//
// PRIVATE
//

FOUNDATION_STATIC bool search_index_skip_fundamental_field(const char* field, size_t length)
{
    for (const auto& skip : SEARCH_SKIP_FIELDS_FOR_INDEXING)
    {
        if (string_equal(field, length, skip.str, skip.length))
            return true;
    }
    return false;
}

FOUNDATION_STATIC bool search_database_index_text_skip_common_words(
    search_database_t* db, search_document_handle_t doc,
    const char* _text, size_t text_length,
    bool include_variations)
{
    if (_text == nullptr || text_length == 0)
        return false;

    if (!search_database_is_document_valid(db, doc))
        return false;

    string_const_t expression, r = string_trim(string_trim(string_const(_text, text_length)), '.');

    if (r.length <= 18)
    {
        search_database_index_word(db, doc, STRING_ARGS(r), false);
    }

    do
    {
        // Split words by space
        string_split(STRING_ARGS(r), STRING_CONST(","), &expression, &r, false);
        if (expression.length)
        {
            // Split words by space
            string_const_t word, rr = string_trim(string_trim(expression), '.');
            do
            {
                string_split(STRING_ARGS(rr), STRING_CONST(" "), &word, &rr, false);
                word = string_trim(word, '.');
                word = string_trim(word, ';');
                if (word.length >= 3)
                {
                    bool skip_word = false;
                    for (const auto& cw : COMMON_STOCK_WORDS)
                    {
                        if (string_equal_nocase(STRING_ARGS(cw), STRING_ARGS(word)))
                        {
                            skip_word = true;
                            break;
                        }
                    }

                    if (skip_word)
                        continue;
                    search_database_index_word(db, doc, STRING_ARGS(word), include_variations);
                }
            } while (rr.length > 0);
        }
    } while (r.length > 0);

    return true;
}

FOUNDATION_STATIC bool search_database_index_property_skip_common_words(
    search_database_t* db, search_document_handle_t doc,
    const char* name, size_t name_length,
    const char* _value, size_t _value_length,
    bool include_variations)
{
    if (_value == nullptr || _value_length == 0)
        return false;

    if (!search_database_is_document_valid(db, doc))
        return false;

    string_const_t expression, r = string_trim(string_trim(string_const(_value, _value_length)), '.');

    if (r.length <= 18)
    {
        search_database_index_property(db, doc, name, name_length, STRING_ARGS(r), false);
    }

    do
    {
        // Split words by space
        string_split(STRING_ARGS(r), STRING_CONST(","), &expression, &r, false);
        if (expression.length)
        {
            // Split words by space
            string_const_t word, rr = string_trim(string_trim(expression), '.');
            do
            {
                string_split(STRING_ARGS(rr), STRING_CONST(" "), &word, &rr, false);
                word = string_trim(word, '.');
                word = string_trim(word, ';');
                if (word.length >= 3)
                {
                    bool skip_word = false;
                    for (const auto& cw : COMMON_STOCK_WORDS)
                    {
                        if (string_equal_nocase(STRING_ARGS(cw), STRING_ARGS(word)))
                        {
                            skip_word = true;
                            break;
                        }
                    }

                    if (skip_word)
                        continue;
                    search_database_index_property(db, doc, name, name_length, STRING_ARGS(word), 
                        include_variations && (word.length >= 6 || word.length < 12));
                }
            } while (rr.length > 0);
        }
    } while (r.length > 0);

    return true;
}

FOUNDATION_STATIC void search_index_news_data(const json_object_t& json, search_document_handle_t doc)
{
    search_database_t* db = _search->db;

    for (auto n : json)
    {
        time_t date;
        string_const_t date_string = n["date"].as_string();
        if (!string_try_convert_date(date_string.str, min(to_size(10), date_string.length), date))
            continue;

        for (auto t : n["tags"])
        {
            string_const_t tag = t.as_string();
            if (string_is_null(tag))
                continue;

            search_database_index_text_skip_common_words(db, doc, STRING_ARGS(tag), false);
        }
    }
}

FOUNDATION_STATIC void search_index_fundamental_object_data(const json_object_t& json, search_database_t* db, search_document_handle_t doc)
{
    for (auto e : json)
    {
        if (e.root == nullptr)
            continue;

        const json_token_t& token = *e.root;

        if (token.type != JSON_STRING && token.type != JSON_PRIMITIVE)
            continue;

        string_const_t id = e.id();
        if (id.length == 0)
            continue;

        // Skip some commonly long values
        if (search_index_skip_fundamental_field(STRING_ARGS(id)))
            continue;

        if (e.is_null())
            continue;

        string_const_t value = json_token_value(json.buffer, &token);
        if (value.length == 0 || string_equal(STRING_ARGS(value), STRING_CONST("null")))
            continue;

        // Skip date with value "0000-00-00"
        if (value.length == 10 && string_equal(STRING_ARGS(value), STRING_CONST("0000-00-00")))
            continue;

        time_t date;
        double number = DNAN;
        if (value.length < 21 && string_try_convert_number(STRING_ARGS(value), number))
        {
            if (math_real_is_finite(number))
                search_database_index_property(db, doc, STRING_ARGS(id), number);
        }
        else if (string_try_convert_date(STRING_ARGS(value), date))
        {
            search_database_index_property(db, doc, STRING_ARGS(id), (double)date);
        }
        else
        {
            search_database_index_property_skip_common_words(db, doc, STRING_ARGS(id), STRING_ARGS(value), false);
        }        
    }
}

FOUNDATION_STATIC unsigned int search_json_token_next_index(const json_token_t& token, json_token_t* tokens, unsigned int index)
{
    if (token.type == JSON_OBJECT || token.type == JSON_ARRAY)
    {
        if (token.sibling != 0)
            return token.sibling - 1;
        
        if (token.child == 0)
            return index;

        // Find last child token
        index = token.child;
        while (index != 0)
        {
            json_token_t* p = &tokens[index];
            if (p->sibling != 0)
                index = p->sibling;
            else if (p->child != 0)
                index = p->child;
            else
                return index;
        }
    }

    return index;
}

FOUNDATION_STATIC void search_index_fundamental_data(const json_object_t& json, string_const_t symbol)
{
    MEMORY_TRACKER(HASH_SEARCH);

    search_database_t* db = _search->db;

    const auto General = json["General"];
    if (General.root == nullptr || General.root->child == 0)
        return;

    string_const_t code = General["Code"].as_string();
    if (string_is_null(code))
        return (void)stock_ignore_symbol(STRING_ARGS(symbol));

    const auto Technicals = json["Technicals"];
    if (Technicals.root == nullptr || Technicals.root->child == 0)
    {
        return (void)stock_ignore_symbol(STRING_ARGS(symbol));
        return;
    }

    const auto Valuation = json["Valuation"];
    if (Valuation.root == nullptr || Valuation.root->child == 0)
    {
        return (void)stock_ignore_symbol(STRING_ARGS(symbol));
        return;
    }
        
    const bool is_delisted = General["IsDelisted"].as_boolean();
    if (is_delisted || json.token_count <= 1)
    {
        log_debugf(HASH_SEARCH, STRING_CONST("%.*s is delisted, skipping for indexing"), STRING_FORMAT(code));
        return;
    }

    time_t updated_at = 0;
    string_const_t updated_at_string = General["UpdatedAt"].as_string();
    if (string_try_convert_date(STRING_ARGS(updated_at_string), updated_at))
    {
        const double updated_elapsed_time = time_elapsed_days(updated_at, time_now());
        if (updated_elapsed_time > 180)
        {
            log_debugf(HASH_SEARCH, STRING_CONST("%.*s is too old (%lf days), skipping for indexing"), STRING_FORMAT(symbol), updated_elapsed_time);
            return (void)stock_ignore_symbol(STRING_ARGS(symbol));
        }
    }
    
    string_const_t exchange = General["Exchange"].as_string();
    if (string_is_null(exchange))
        return;

    // Do not index FUND stock and those with no ISIN
    string_const_t isin = General["ISIN"].as_string();
    if (string_is_null(isin))
        isin = json["ETF_Data"]["ISIN"].as_string();

    string_const_t type = General["Type"].as_string();
    if (string_equal_nocase(STRING_ARGS(type), STRING_CONST("FUND")))
        return;

    string_const_t description = General["Description"].as_string();
    if (string_is_null(description) || description.length < 32)
        return (void)stock_ignore_symbol(STRING_ARGS(symbol));

    // Ignore symbols with negative Beta
    const double beta = Technicals["Beta"].as_number();
    if (beta < 0)
        return (void)stock_ignore_symbol(STRING_ARGS(symbol));

    string_const_t name = General["Name"].as_string();
    string_const_t country = General["Country"].as_string();

    string_const_t industry = General["Industry"].as_string();
    string_const_t sector = General["Sector"].as_string();
    string_const_t gic_sector = General["GicSector"].as_string();
    string_const_t gic_group = General["GicGroup"].as_string();
    string_const_t gic_sub_industry = General["GicSubIndustry"].as_string();
    string_const_t gic_industry = General["GicIndustry"].as_string();
    string_const_t category = General["Category"].as_string();
    string_const_t home_category = General["HomeCategory"].as_string();
        
    bool new_document_added = false;
    search_document_handle_t doc = search_database_find_document(db, STRING_ARGS(symbol));
    if (doc == SEARCH_DOCUMENT_INVALID_ID)
    {
        new_document_added = true;
        FOUNDATION_ASSERT(symbol.length);
        doc = search_database_add_document(db, STRING_ARGS(symbol));
    }

    TIME_TRACKER(2.0, HASH_SEARCH, "[%u] Indexing [%12.*s] %-7.*s -> %.*s -> %.*s",
        doc, STRING_FORMAT(isin), STRING_FORMAT(symbol), STRING_FORMAT(type), STRING_FORMAT(name));

    // Index symbol
    search_database_index_word(db, doc, STRING_ARGS(symbol), true);

    // Index basic information
    search_database_index_text_skip_common_words(db, doc, STRING_ARGS(name), true);
    search_database_index_exact_match(db, doc, STRING_ARGS(isin), false);
    search_database_index_exact_match(db, doc, STRING_ARGS(name), false);
    search_database_index_text_skip_common_words(db, doc, STRING_ARGS(country), false);
    //search_database_index_text_skip_common_words(db, doc, STRING_ARGS(type), false);
    search_database_index_text_skip_common_words(db, doc, STRING_ARGS(description), false);
    search_database_index_text_skip_common_words(db, doc, STRING_ARGS(industry), true);
    search_database_index_text_skip_common_words(db, doc, STRING_ARGS(sector), true);
    search_database_index_text_skip_common_words(db, doc, STRING_ARGS(gic_sector), true);
    search_database_index_text_skip_common_words(db, doc, STRING_ARGS(gic_group), true);
    search_database_index_text_skip_common_words(db, doc, STRING_ARGS(gic_industry), true);
    search_database_index_text_skip_common_words(db, doc, STRING_ARGS(gic_sub_industry), true);
    search_database_index_text_skip_common_words(db, doc, STRING_ARGS(category), true);
    search_database_index_text_skip_common_words(db, doc, STRING_ARGS(home_category), true);

    search_database_index_property(db, doc, STRING_CONST("exchange"), STRING_ARGS(exchange), false);

    // Index stock yield
    const double yielding = json["Highlights"]["DividendYield"]
        .as_number(json["ETF_Data"]["Yield"].as_number(0) / 100.0) * 100.0;
    search_database_index_property(db, doc, STRING_CONST("yield"), yielding);

    // Index some quarterly financial data properties
    const auto Financials = json["Financials"]["Balance_Sheet"]["quarterly"].get(0ULL);
    if (Financials.is_valid())
    {
        time_t sheet_date;
        const auto sheet_date_string = Financials["date"].as_string();
        if (string_try_convert_date(STRING_ARGS(sheet_date_string), sheet_date))
        {
            search_database_index_property(db, doc, STRING_CONST("Financials"), (double)sheet_date);
            search_index_fundamental_object_data(Financials, db, doc);
        }
    }

    // Index recent cash flow data
    const auto Cashflow = json["Financials"]["Cash_Flow"]["quarterly"].get(0ULL);
    if (Cashflow.is_valid())
    {
        time_t sheet_date;
        const auto sheet_date_string = Cashflow["date"].as_string();
        if (string_try_convert_date(STRING_ARGS(sheet_date_string), sheet_date))
        {
            search_database_index_property(db, doc, STRING_CONST("Cashflow"), (double)sheet_date);
            search_index_fundamental_object_data(Cashflow, db, doc);
        }
    }

    // Index ETF top 10 holdings
    const auto ETF_Data = json["ETF_Data"];
    if (ETF_Data.is_valid())
    {
        const auto Holdings = ETF_Data["Holdings"];
        if (Holdings.is_valid())
        {
            for (auto h : Holdings)
            {
                string_const_t code = h["Code"].as_string();
                if (string_is_null(code))
                    continue;

                search_database_index_property(db, doc, STRING_CONST("hold"), STRING_ARGS(code), false);
            }
        }
    }
      
    for (unsigned i = 0; i < json.token_count; ++i)
    {
        const json_token_t& token = json.tokens[i];
        
        string_const_t id = json_token_identifier(json.buffer, &token);
        if (id.length == 0 || id.length >= SEARCH_INDEX_WORD_MAX_LENGTH-1)
            continue;
            
        // Skip some commonly long values
        if (search_index_skip_fundamental_field(STRING_ARGS(id)))
        {
            i = search_json_token_next_index(token, json.tokens, i);
            continue;
        }

        if (token.type != JSON_STRING && token.type != JSON_PRIMITIVE)
            continue;
        
        string_const_t value = json_token_value(json.buffer, &token);
        if (value.length == 0 || string_equal(STRING_ARGS(value), STRING_CONST("null")))
            continue;

        // Skip date with value "0000-00-00"
        if (value.length == 10 && string_equal(STRING_ARGS(value), STRING_CONST("0000-00-00")))
            continue;

        if (string_equal(STRING_ARGS(value), STRING_CONST("NA")))
            continue;
                
        time_t date;
        double number = NAN;
        if (value.length < 21 && string_try_convert_number(STRING_ARGS(value), number))
        {
            if (math_real_is_finite(number))
                search_database_index_property(db, doc, STRING_ARGS(id), number);
        }
        else if (string_try_convert_date(STRING_ARGS(value), date))
        {
            search_database_index_property(db, doc, STRING_ARGS(id), (double)date);
        }
        else
        {
            // Add suppor to index activity fields such as {"Activity":"controversialWeapons","Involvement":"Yes"}
            if (string_equal(STRING_ARGS(id), STRING_CONST("Activity")))
            {
                const json_token_t& Involvement = json.tokens[i+1];
                string_const_t yes_no = json_token_value(json.buffer, &Involvement);
                search_database_index_property(db, doc, STRING_ARGS(value), STRING_ARGS(yes_no), false);
                i++;
            }
            else if (string_equal_nocase(STRING_ARGS(id), STRING_CONST("name")) ||
                     string_equal_nocase(STRING_ARGS(id), STRING_CONST("title")))
            {
                search_database_index_text_skip_common_words(db, doc, STRING_ARGS(value), false);
            }
            else
                search_database_index_property_skip_common_words(db, doc, STRING_ARGS(id), STRING_ARGS(value), false);
        }
    }
    
    // Index some news data
    if (!eod_fetch("news", nullptr, FORMAT_JSON_CACHE, "s", symbol.str, "limit", "10", LC1(search_index_news_data(_1, doc)), 8 * 24 * 60 * 60ULL))
    {
        log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to fetch news for symbol %*.s"), STRING_FORMAT(symbol));
    }

    // Index EOD stock data
    if (new_document_added)
    {
        time_t start = 0;
        if (stock_get_time_range(STRING_ARGS(symbol), &start, nullptr, 5.0))
        {
            search_database_index_property(db, doc, STRING_CONST("since"), (double)start);
        }
        else
        {
            log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to fetch time range for symbol %*.s"), STRING_FORMAT(symbol));
        }

        search_database_document_update_timestamp(db, doc);
    }
}

FOUNDATION_STATIC void search_index_exchange_symbols(const json_object_t& data, const char* market, size_t market_length, bool* stop_indexing)
{
    MEMORY_TRACKER(HASH_SEARCH);

    search_database_t* db = _search->db;

    // Wait to connect to EOD services
    const tick_t timeout = time_current();
    while (!eod_availalble() && time_elapsed(timeout) < 30.0)
    {
        if (thread_try_wait(100))
        {
            *stop_indexing = true;
            return;
        }
    }

    for(auto e : data)
    {   
        if (thread_try_wait(50))
        {
            *stop_indexing = true;
            break;
        }

        if (!eod_availalble())
        {
            *stop_indexing = true;
            log_warnf(HASH_SEARCH, WARNING_NETWORK, STRING_CONST("Failed to connect to EOD services, terminating indexing"));
            return;
        }

        if (eod_capacity() > 0.8)
        {
            *stop_indexing = true;
            log_warnf(HASH_SEARCH, WARNING_NETWORK, STRING_CONST("EOD full api usage is near, stopping search indexing."));
            return;
        }
    
        const tick_t st = time_current();
        if (e.root == nullptr || e.root->type != JSON_OBJECT)
            continue;

        string_const_t code = e["Code"].as_string();
        if (string_is_null(code))
            continue;

        string_const_t exchange = e["Exchange"].as_string();
        string_t symbol = string_format(SHARED_BUFFER(16), STRING_CONST("%.*s.%.*s"), STRING_FORMAT(code), to_int(market_length), market);
        if (string_is_null(exchange))
        {
            stock_ignore_symbol(STRING_ARGS(symbol));
            continue;
        }

        string_const_t isin = e["Isin"].as_string();
        string_const_t type = e["Type"].as_string();

        // Do not index FUND stock and those with no ISIN
        if (string_equal_nocase(STRING_ARGS(type), STRING_CONST("FUND")))
            continue;

        // Skip ETFs
        if (string_equal_nocase(STRING_ARGS(type), STRING_CONST("ETF")))
            continue;

        // Skip Preferred Stock
        if (string_equal_nocase(STRING_ARGS(type), STRING_CONST("Preferred Stock")))
            continue;

        search_document_handle_t doc = search_database_find_document(db, STRING_ARGS(symbol));
        if (doc != SEARCH_DOCUMENT_INVALID_ID)
        {
            const time_t doc_timestamp = search_database_document_timestamp(db, doc);
            const double days_old = time_elapsed_days(doc_timestamp, time_now());
            if (days_old < 7.0)
                continue;
        }

        // Check that the stock is still valid and current
        if (!stock_valid(STRING_ARGS(symbol)))
        {
            log_debugf(HASH_SEARCH, STRING_CONST("Symbol %.*s is not valid, skipping it for indexing"), STRING_FORMAT(symbol));
            continue;
        }

        // Fetch symbol fundamental data
        if (!eod_fetch("fundamentals", symbol.str, FORMAT_JSON_CACHE, 
            LC1(search_index_fundamental_data(_1, string_to_const(symbol))), 25 * 24 * 60 * 60ULL))
        {
            log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to fetch %.*s fundamental"), STRING_FORMAT(symbol));
        }
    }
}

FOUNDATION_STATIC void* search_indexing_thread_fn(void* data)
{
    MEMORY_TRACKER(HASH_SEARCH);

    // Load search database
    _search->db = search_database_allocate(SearchDatabaseFlags::SkipCommonWords);

    // Wait a few seconds before starting the indexing process.
    // Delaying the start of the indexing process helps when the users wants to
    // quickly close the application after starting it, as the indexing process
    // takes a few seconds just to load the search database
    if (_search->startup_signal.wait(30000))
    {
        log_debugf(0, STRING_CONST("Search indexing kick off"));
    }

    // Check if the thread was aborted before starting
    if (thread_try_wait(0))
    {
        log_warnf(0, WARNING_PERFORMANCE,
            STRING_CONST("Search indexing thread aborted before starting"));
        return 0;
    }

    string_const_t search_db_path = session_get_user_file_path(STRING_CONST("search.db"));
    stream_t* search_db_stream = fs_open_file(STRING_ARGS(search_db_path), STREAM_IN | STREAM_BINARY);
    if (search_db_stream)
    {
        if (thread_try_wait(0))
        {
            stream_deallocate(search_db_stream);
            return 0;
        }
            
        TIME_TRACKER("Loading search database");
        search_database_load(_search->db, search_db_stream);
        stream_deallocate(search_db_stream);

        if (thread_try_wait(0))
            return 0;
    }
    
    dispatcher_post_event(EVENT_SEARCH_DATABASE_LOADED);

    // If using demo key, skip indexing
    string_const_t eod_key = string_to_const(eod_get_key().str);
    if (string_equal_nocase(STRING_ARGS(eod_key), STRING_CONST("demo")))
    {
        log_warnf(HASH_SEARCH, WARNING_SUSPICIOUS, STRING_CONST("Demo key, skipping search indexing"));
        return 0;
    }

    if (environment_argument("disable-indexing"))
    {
        log_warnf(HASH_SEARCH, WARNING_SUSPICIOUS, STRING_CONST("Search indexing is disabled, skipping search indexing"));
        return 0;
    }

    if (main_is_daemon_mode())
    {
        log_warnf(HASH_SEARCH, WARNING_SUSPICIOUS, STRING_CONST("Batch mode, skipping search indexing"));
        return 0;
    }

    // Remove old documents so that we don't keep growing the database with invalidated data.
    search_database_remove_old_documents(_search->db, time_add_days(time_now(), -25), 7.0);

    if (thread_try_wait(0))
        return 0;
    
    // Fetch all titles from stock exchange market
    SHARED_READ_LOCK(_search->exchanges_lock);

    bool stop_indexing = false;
    foreach (market, _search->exchanges)
    {
        if (stop_indexing)
            break;
            
        if (thread_try_wait(1000))
            break;
            
        auto fetch_fn = [market, &stop_indexing](const json_object_t& data) 
        { 
            search_index_exchange_symbols(data, STRING_ARGS(*market), &stop_indexing); 
        };
        if (!eod_fetch("exchange-symbol-list", market->str, FORMAT_JSON_CACHE, fetch_fn, 30 * 24 * 60 * 60ULL))
        {
            tr_warn(HASH_SEARCH, WARNING_RESOURCE, "Failed to fetch {0} symbols", *market);
        }

        tr_info(HASH_SEARCH, "Search indexing completed for the market {0}", *market);
    }    

    return 0;
}

FOUNDATION_STATIC bool search_compare_search_and_saved_query(const string_t& saved_query, const string_const_t& search_query)
{
    if (string_equal_nocase(STRING_ARGS(saved_query), STRING_ARGS(search_query)))
        return true;

    int lvd = string_levenstein_distance(STRING_ARGS(search_query), STRING_ARGS(saved_query));
    log_debugf(HASH_SEARCH,
        STRING_CONST("Levenstein distance between `%.*s` and `%.*s` is %d"),
        STRING_FORMAT(search_query), STRING_FORMAT(saved_query), lvd);

    if (lvd < 4)
        return true;

    return false;
}

FOUNDATION_STATIC void search_save_query(const char* search_text, size_t search_text_length)
{
    string_const_t search_query = string_const(search_text, search_text_length);
    if (string_is_null(search_query))
        return;

    // Save query to history
    const int entry_pos = array_index_of(_search->saved_queries, search_query, search_compare_search_and_saved_query);
    if (entry_pos >= 0)
    {
        // Move to end of list
        if (entry_pos != array_size(_search->saved_queries) - 1)
        {
            string_t tmp = _search->saved_queries[entry_pos];
            array_erase_ordered_safe(_search->saved_queries, entry_pos);
            array_push(_search->saved_queries, tmp);
        }
    }
    else
    {
        if (array_size(_search->saved_queries) > 20)
        {
            string_deallocate(_search->saved_queries[0].str);
            array_erase(_search->saved_queries, 0);
        }

        array_push(_search->saved_queries, string_clone(search_text, search_text_length));
    }
}

FOUNDATION_STATIC string_const_t search_entry_resolve_symbol(const search_result_entry_t* entry)
{
    if (entry->db && entry->doc)
    {
        string_const_t symbol = search_database_document_name(entry->db, entry->doc);
        return symbol;
    }

    return string_to_const(entry->symbol);   
}

FOUNDATION_STATIC bool search_insert_symbol_result(window_handle_t window_handle, hash_t query_hash, string_const_t symbol)
{
    search_result_entry_t entry{};
    entry.db = nullptr;
    entry.doc = 0;
    entry.source_type = SearchResultSourceType::EODApi;

    string_copy(STRING_BUFFER(entry.symbol), STRING_ARGS(symbol));

    search_window_t* sw = (search_window_t*)window_get_user_data(window_handle);
    if (sw == nullptr)
        return false;

    {
        SHARED_READ_LOCK(sw->lock);
        hash_t search_window_query_hash = string_hash(STRING_LENGTH(sw->query));
        if (search_window_query_hash != query_hash)
            return false;

        // Check that we do not already have this symbol in the list
        for (unsigned i = 0, end = array_size(sw->results); i < end; ++i)
        {
            const search_result_entry_t* re = sw->results + i;
            string_const_t re_symbol = search_entry_resolve_symbol(re);
            if (string_equal(STRING_ARGS(re_symbol), STRING_ARGS(symbol)))
                return false;
        }
    }

    {
        SHARED_WRITE_LOCK(sw->lock);
        entry.window = sw;
        array_push_memcpy(sw->results, &entry);
    }
    return true;
}

FOUNDATION_STATIC void search_fetch_single_symbol_callback(hash_t query_hash, window_handle_t window_handle, const json_object_t& json)
{
    if (!json.resolved())
        return;

    auto price = json["close"].as_number();
    if (math_real_is_nan(price))
        return;

    string_const_t code = json["code"].as_string();
    if (string_is_null(code))
        return;

    search_insert_symbol_result(window_handle, query_hash, code);
}

FOUNDATION_STATIC void search_fetch_search_api_results_callback(hash_t query_hash, window_handle_t window_handle, const json_object_t& json)
{
    if (!json.resolved())
        return;

    for (auto e : json)
    {
        search_window_t* sw = (search_window_t*)window_get_user_data(window_handle);
        if (sw == nullptr)
            break;

        string_const_t code = e["Code"].as_string();
        string_const_t exchange = e["Exchange"].as_string();

        string_t symbol = string_format(SHARED_BUFFER(16), STRING_CONST("%.*s.%.*s"), STRING_FORMAT(code), STRING_FORMAT(exchange));
        if (!stock_valid(symbol.str, symbol.length))
            continue;

        search_insert_symbol_result(window_handle, query_hash, string_to_const(symbol));
    }        
}

FOUNDATION_STATIC void search_window_clear_results(search_window_t* sw)
{
    // Clear any previous errors
    sw->error[0] = 0;

    {
        SHARED_WRITE_LOCK(sw->lock);
        for (unsigned i = 0, end = array_size(sw->results); i < end; ++i)
        {
            search_result_entry_t* re = sw->results + i;
            string_deallocate(re->description.str);
        }
        array_clear(sw->results);
    }
}


FOUNDATION_STATIC void search_window_execute_query(search_window_t* sw, const char* search_text, size_t search_text_length)
{
    FOUNDATION_ASSERT(sw);

    search_database_t* db = sw->db;
    FOUNDATION_ASSERT(db);

    search_window_clear_results(sw);
    if (search_text == nullptr || search_text_length == 0)
        return;

    sw->query_tick = time_current();
    try
    {
        // Start with a query to EOD /api/search
        window_handle_t sw_handle = sw->handle;

        // Run simple EOD API query if the search text does not contain any special characters
        if (search_text_length > 1 &&
            string_find(search_text, search_text_length, ':', 1) == STRING_NPOS &&
            string_find(search_text, search_text_length, '=', 1) == STRING_NPOS &&
            string_find(search_text, search_text_length, '!', 1) == STRING_NPOS &&
            string_find(search_text, search_text_length, '<', 1) == STRING_NPOS &&
            string_find(search_text, search_text_length, '>', 1) == STRING_NPOS)
        {
            hash_t search_query_hash = string_hash(search_text, search_text_length);
            eod_fetch_async("search", search_text, FORMAT_JSON, "limit", "5",
                LC1(search_fetch_search_api_results_callback(search_query_hash, sw_handle, _1)));

            // If the search text looks like a symbol, i.e. GFL.TO, then lets query the real-time value to see if it resolves.
            if (search_text_length > 3 && search_text_length < 16 && 
                search_text[search_text_length] != '.' &&
                string_find(search_text, search_text_length, '.', 1) != STRING_NPOS &&
                string_find(search_text, search_text_length, ' ', 1) == STRING_NPOS)
            {
                eod_fetch_async("real-time", search_text, FORMAT_JSON, LC1(search_fetch_single_symbol_callback(search_query_hash, sw_handle, _1)));
            }
        }

        // Meanwhile query the indexed database
        search_query_handle_t query = search_database_query(db, search_text, search_text_length);
        if (search_database_query_is_completed(db, query))
        {
            SHARED_WRITE_LOCK(sw->lock);

            const search_result_t* results = search_database_query_results(db, query);

            foreach(r, results)
            {
                search_result_entry_t entry{};
                entry.db = db;
                entry.doc = (search_document_handle_t)r->id;
                entry.window = sw;
                entry.source_type = SearchResultSourceType::Database;

                string_const_t symbol = search_database_document_name(db, (search_document_handle_t)r->id);
                string_copy(STRING_BUFFER(entry.symbol), STRING_ARGS(symbol));

                // Check that we do not already have this symbol in the list
                bool unique = true;
                for (unsigned i = 0, end = array_size(sw->results); i < end; ++i)
                {
                    const search_result_entry_t* re = sw->results + i;
                    string_const_t re_symbol = search_entry_resolve_symbol(re);
                    if (string_equal(STRING_ARGS(re_symbol), STRING_ARGS(symbol)))
                    {
                        unique = false;
                        break;
                    }
                }

                if (unique)
                    array_push_memcpy(sw->results, &entry);
            }

            // TODO: Remove duplicates

            if (!search_database_query_dispose(db, query))
            {
                log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to dispose query"));
            }
            else if (sw->table && array_size(sw->results) > 0)
            {
                dispatcher_post_event(EVENT_SEARCH_QUERY_UPDATED);
                search_save_query(search_text, search_text_length);
            }
        }
        else
        {
            log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Query not completed"));
        }

        // Save last query to module
        string_copy(STRING_BUFFER(_search->query), search_text, search_text_length);
    }
    catch (SearchQueryException err)
    {
        string_format(STRING_BUFFER(sw->error), STRING_CONST("(%u) %s at %.*s"), (unsigned)err.error, err.msg, STRING_FORMAT(err.token));
    }
    
    sw->query_tick = time_diff(sw->query_tick, time_current());
}

FOUNDATION_STATIC void search_window_render(void* user_data)
{
    search_window_t* sw = (search_window_t*)user_data;
    FOUNDATION_ASSERT(sw && sw->db);

    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();

    const float button_padding = IM_SCALEF(4.0f);
    const float update_button_width = IM_SCALEF(90.0f);
    const float drop_down_button_width = IM_SCALEF(20.0f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - update_button_width - drop_down_button_width - button_padding * 2.0f);
    if (ImGui::InputTextWithHint("##SearchQuery", 
        tr("Search stocks... " ICON_MD_FILTER_LIST_ALT), 
        STRING_BUFFER(sw->query), ImGuiInputTextFlags_AutoSelectAll))
    {
        sw->delayed_input = true;
        sw->delayed_tick = time_current();
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_MD_ARROW_DROP_DOWN, { drop_down_button_width, 0 }))
    {
        ImGui::OpenPopup("##SearchQueryHistory");
    }

    if (ImGui::BeginPopup("##SearchQueryHistory"))
    {
        foreach(query, _search->saved_queries)
        {
            if (ImGui::Selectable(query->str, false, ImGuiSelectableFlags_DontClosePopups))
            {
                string_copy(STRING_BUFFER(sw->query), query->str, query->length);
                sw->delayed_input = true;

                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Update"), { update_button_width, 0 }) || 
       (sw->delayed_input && time_elapsed(sw->delayed_tick) > 0.250))
    {
        const size_t search_query_length = string_length(sw->query);
        search_window_execute_query(sw, sw->query, search_query_length);

        sw->delayed_input = false;
    }

    {
        SHARED_READ_LOCK(sw->lock);
        table_render(sw->table, sw->results, 0.0f, -ImGui::GetFontSize() - 8.0f);
    }

    if (sw->error[0] != 0)
    {
        ImGui::TextColored(ImColor(IM_COL32(200, 10, 10, 245)), "%s", sw->error);
    }
    else if (sw->query_tick > 0 )
    {
        double elapsed_time = sw->query_tick / (double)time_ticks_per_second() * 1000.0;
        const char* time_unit = "ms";
        if (elapsed_time > 999)
        {
            time_unit = "seconds";
            elapsed_time /= 1000.0;
        }
        else if (elapsed_time < 1.0)
        {
            time_unit = "us";
            elapsed_time *= 1000.0;
        }
        ImGui::TrText("Search found %u result(s) and took %.3lg %s", array_size(sw->results), elapsed_time, time_unit);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::SetTooltip(" Symbols: %u \n Properties: %u ", search_database_document_count(sw->db), search_database_index_count(sw->db));
        }
    }
}

FOUNDATION_STATIC const stock_t* search_result_resolve_stock(search_result_entry_t* entry, const table_column_t* column, fetch_level_t fetch_levels)
{
    if (entry->stock.initialized() && entry->stock->has_resolve(fetch_levels))
        return entry->stock.resolve();

    const bool sorting = (column->flags & COLUMN_SORTING_ELEMENT) != 0;
    if (entry->uptime == 0)
    {
        entry->uptime = time_current();
        if (!sorting)
            return nullptr;
    }
    
    // Return if we haven't been visible for at least 1 second
    if (time_elapsed(entry->uptime) < 1.0)
        return nullptr;

    if (!entry->stock.initialized())
    {
        string_const_t symbol = search_entry_resolve_symbol(entry);
        entry->stock = stock_request(STRING_ARGS(symbol), fetch_levels);
        entry->viewed = pattern_find(STRING_ARGS(symbol)) >= 0;
    }
    else if (!entry->stock->has_resolve(fetch_levels))
    {
        stock_update(entry->stock, fetch_levels);
    }    

    return entry->stock.resolve();
}

FOUNDATION_STATIC void search_table_column_symbol_selected(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    const search_result_entry_t* entry = (const search_result_entry_t*)element;
    FOUNDATION_ASSERT(entry);

    if (entry->window == nullptr)
        return;

    if (const stock_t* s = entry->stock)
    {
        string_const_t code = SYMBOL_CONST(s->code);
        if (pattern_open(STRING_ARGS(code)) && entry->window->handle != 0)
            window_close(entry->window->handle);
    }
}

FOUNDATION_STATIC void search_table_draw_symbol(string_const_t symbol, bool viewed)
{
    ImGui::BeginGroup();
    if (viewed)
        ImGui::PushStyleColor(ImGuiCol_Text, SEARCH_PATTERN_VIEWED_COLOR);
            
    const float font_size = ImGui::GetFontSize();
    ImGui::Text("%.*s", STRING_FORMAT(symbol));

    if (viewed)
        ImGui::PopStyleColor();

    #if BUILD_APPLICATION
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - font_size, 0.0f);
    ImVec2 logo_size{ font_size, font_size };
    ImRect logo_rect{};
    if (logo_render_icon(STRING_ARGS(symbol), logo_size, false, true, &logo_rect))
    {
        ImGui::SetCursorScreenPos(logo_rect.Min);
        ImGui::Dummy(logo_size);
    }
    #endif
    ImGui::EndGroup();
}

FOUNDATION_STATIC table_cell_t search_table_column_symbol(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    FOUNDATION_ASSERT(entry);

    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::NONE);
    if (s)
    {
        string_const_t code = SYMBOL_CONST(s->code);
        if (column->flags & COLUMN_RENDER_ELEMENT)
            search_table_draw_symbol(code, entry->viewed);
        
        return code;
    }

    string_const_t symbol = search_entry_resolve_symbol(entry);
    if (string_is_null(symbol))
        return nullptr;

    if (column->flags & COLUMN_RENDER_ELEMENT)
        ImGui::Text("%.*s", STRING_FORMAT(symbol));

    return symbol;
}

FOUNDATION_STATIC table_cell_t search_table_column_name(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    FOUNDATION_ASSERT(entry);

    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return nullptr;

    return s->name;
}

FOUNDATION_STATIC table_cell_t search_table_column_country(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return nullptr;
    return s->country;
}

FOUNDATION_STATIC table_cell_t search_table_column_exchange(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return nullptr;
    return s->exchange;
}

FOUNDATION_STATIC table_cell_t search_table_column_currency(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return nullptr;
    return s->currency;
}

FOUNDATION_STATIC table_cell_t search_table_column_type(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return nullptr;
    return s->type;
}

FOUNDATION_STATIC table_cell_t search_table_column_sector(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return nullptr;
    if (s->sector)
        return s->sector;
    if (s->category)
        return s->category;
    return s->type;
}

FOUNDATION_STATIC table_cell_t search_table_column_industry(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return nullptr;
    return s->industry;
}

FOUNDATION_STATIC table_cell_t search_table_column_category(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return nullptr;
    return s->category;
}

FOUNDATION_STATIC table_cell_t search_table_column_isin(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return nullptr;
    return s->isin;
}

FOUNDATION_STATIC table_cell_t search_table_column_change_p(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::REALTIME);
    if (s == nullptr)
        return NAN;
    return s->current.change_p;
}

FOUNDATION_STATIC table_cell_t search_table_column_change_week(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::REALTIME | FetchLevel::EOD);
    if (s == nullptr)
        return NAN;

    auto ed = stock_get_EOD(s, -7, true);
    if (ed == nullptr)
        return NAN;
    return (s->current.close - ed->adjusted_close) / ed->adjusted_close * 100.0f;
}

FOUNDATION_STATIC table_cell_t search_table_column_change_month(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::REALTIME | FetchLevel::EOD);
    if (s == nullptr)
        return NAN;

    auto ed = stock_get_EOD(s, -31, true);
    if (ed == nullptr)
        return NAN;
    return (s->current.close - ed->adjusted_close) / ed->adjusted_close * 100.0f;
}

FOUNDATION_STATIC table_cell_t search_table_column_change_year(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::REALTIME | FetchLevel::EOD);
    if (s == nullptr)
        return NAN;

    auto ed = stock_get_EOD(s, -365, true);
    if (ed == nullptr)
        return NAN;
    return (s->current.close - ed->adjusted_close) / ed->adjusted_close * 100.0f;
}

FOUNDATION_STATIC table_cell_t search_table_column_change_max(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::REALTIME | FetchLevel::EOD);
    if (s == nullptr)
        return NAN;

    auto ed = stock_get_EOD(s, -365 * 40, true);
    if (ed == nullptr)
        return NAN;
    return (s->current.close - ed->adjusted_close) / ed->adjusted_close * 100.0f;
}

FOUNDATION_STATIC table_cell_t search_table_column_return_rate(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return nullptr;
    return s->dividends_yield.fetch() * 100.0;
}

FOUNDATION_STATIC table_cell_t search_table_column_price(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::REALTIME);
    if (s == nullptr)
        return nullptr;
    return s->current.close;
}

FOUNDATION_STATIC table_cell_t search_table_column_since(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::EOD);
    if (s == nullptr)
        return nullptr;

    auto* first_day = array_last(s->history);
    if (first_day)
        return first_day->date;
    return (time_t)0;
}

FOUNDATION_STATIC table_cell_t search_table_column_percentage_per_year(table_element_ptr_t element, const table_column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::REALTIME | FetchLevel::EOD);
    if (s == nullptr)
        return nullptr;

    auto* first_day = array_last(s->history);
    if (first_day)
    {
        const double years = time_elapsed_days(first_day->date, time_now()) / 365.0;
        double max_percentage = (s->current.close - first_day->adjusted_close) / first_day->adjusted_close * 100.0f;
        return max_percentage / years;
    }

    return NAN;
}

FOUNDATION_STATIC void search_table_column_dividends_formatter(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell, cell_style_t& style)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    FOUNDATION_ASSERT(entry);
    
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return;

    if (s->dividends_yield.fetch() > SETTINGS.good_dividends_ratio)
    {
        style.types |= COLUMN_COLOR_TEXT;
        style.text_color = ImColor::HSV(117 / 360.0f, 0.68f, 0.90f); // hsv(117, 68%, 90%)
    }
}

FOUNDATION_STATIC void search_table_column_change_p_formatter(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell, cell_style_t& style, double threshold)
{
    if (cell->number > threshold)
    {
        style.types |= COLUMN_COLOR_TEXT;
        style.text_color = ImColor::HSV(117 / 360.0f, 0.68f, 0.90f); // hsv(117, 68%, 90%)
    }
}

FOUNDATION_STATIC void search_table_column_description_tooltip(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::FUNDAMENTALS);
    if (s == nullptr)
        return;
    string_table_symbol_t tooltip_symbol = s->description.fetch();
    if (tooltip_symbol == 0)
        return;

    ImGui::PushTextWrapPos(IM_SCALEF(500));
    if (entry->description.length)
    {
        ImGui::Text("%.*s", STRING_FORMAT(entry->description));
    }
    else
    {
        string_const_t tooltip = string_table_decode_const(tooltip_symbol);
        if (backend_is_connected())
        {
            string_const_t lang = localization_current_language();
            const bool is_english = string_equal(lang.str, lang.length, STRING_CONST("en"));
            if (!is_english)
            {
                string_const_t code = search_entry_resolve_symbol(entry);
                entry->description = backend_translate_text(code.str, code.length, STRING_ARGS(tooltip), STRING_ARGS(lang));
            }
            else
            {
                entry->description = string_clone(STRING_ARGS(tooltip));
            }

            ImGui::Text("%.*s", STRING_FORMAT(entry->description));
        }
        else
        {
            ImGui::Text("%.*s", STRING_FORMAT(tooltip));
        }
    }
    ImGui::PopTextWrapPos();
}

FOUNDATION_STATIC void search_table_column_code_color(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell, cell_style_t& style)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::NONE);
    if (s != nullptr && entry->viewed)
    {
        style.types |= COLUMN_COLOR_TEXT;
        style.text_color = ImColor::HSV(!entry->viewed ? 0.4f : 0.6f, 0.3f, 0.9f);
    }
}

FOUNDATION_STATIC void search_table_contextual_menu(table_element_ptr_const_t element, const table_column_t* column, const table_cell_t* cell)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column, FetchLevel::NONE);
    string_const_t symbol = search_entry_resolve_symbol(entry);

    if (s == nullptr && string_is_null(symbol))
        return;

    if (pattern_contextual_menu(STRING_ARGS(symbol)))
    {
        entry->viewed = true;
        window_close(entry->window->handle);
    }

    ImGui::Separator();

    if (ImGui::MenuItem(tr(ICON_MD_NEWSPAPER " Read News")))
    {
        string_const_t lang = localization_current_language();
        backend_open_url(STRING_CONST("/news/%.*s?lang=%.*s&summary=true&limit=5"), STRING_FORMAT(symbol), STRING_FORMAT(lang));
    }

    ImGui::Separator();

    if (ImGui::MenuItem(tr("Re-index...")))
    {
        string_const_t expr = string_format_static(STRING_CONST("SEARCH_REMOVE_DOCUMENT(\"%.*s\")\nSEARCH_INDEX(\"%.*s\")"),  STRING_FORMAT(symbol), STRING_FORMAT(symbol), STRING_FORMAT(symbol));
        eval(STRING_ARGS(expr));
    }

    if (ImGui::MenuItem(tr("Remove index...")))
    {
        string_const_t expr = string_format_static(STRING_CONST("SEARCH_REMOVE_DOCUMENT(\"%.*s\")"), STRING_FORMAT(symbol), STRING_FORMAT(symbol));
        eval(STRING_ARGS(expr));
        dispatcher_post_event(EVENT_SEARCH_DATABASE_LOADED);
    }
}

FOUNDATION_STATIC table_t* search_create_table()
{
    table_t* table = table_allocate("QuickSearch##15", TABLE_HIGHLIGHT_HOVERED_ROW | TABLE_LOCALIZATION_CONTENT);
    table->context_menu = search_table_contextual_menu;

    table_add_column(table, search_table_column_symbol, "Symbol", COLUMN_FORMAT_TEXT, COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING)
        .set_width(imgui_get_font_ui_scale(120.0f))
        .set_selected_callback(search_table_column_symbol_selected);

    table_add_column(table, search_table_column_name, ICON_MD_BUSINESS " Name", COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_STRETCH)
        .set_style_formatter(search_table_column_code_color)
        .set_tooltip_callback(search_table_column_description_tooltip);
        
    table_add_column(table, search_table_column_country, ICON_MD_FLAG " Country", COLUMN_FORMAT_SYMBOL, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE)
        .set_width(imgui_get_font_ui_scale(100.0f));

    table_add_column(table, search_table_column_exchange, ICON_MD_LOCATION_CITY "||" ICON_MD_LOCATION_CITY " Exchange", COLUMN_FORMAT_SYMBOL, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE | COLUMN_MIDDLE_ALIGN)
        .set_width(imgui_get_font_ui_scale(100.0f));

    table_add_column(table, search_table_column_currency, ICON_MD_FLAG "||" ICON_MD_FLAG " Currency", COLUMN_FORMAT_SYMBOL, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE | COLUMN_MIDDLE_ALIGN)
        .set_width(imgui_get_font_ui_scale(80.0f));

    table_add_column(table, search_table_column_type, ICON_MD_INVENTORY " Type", COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT)
        .set_width(imgui_get_font_ui_scale(160.0f));

    table_add_column(table, search_table_column_sector, ICON_MD_CORPORATE_FARE " Sector", COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT)
        .set_width(imgui_get_font_ui_scale(170.0f));
    table_add_column(table, search_table_column_industry, ICON_MD_FACTORY " Industry", COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_STRETCH);
    table_add_column(table, search_table_column_category, ICON_MD_CATEGORY " Category", COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT)
        .set_width(imgui_get_font_ui_scale(160.0f));

    table_add_column(table, search_table_column_isin, ICON_MD_FINGERPRINT " ISIN     ", COLUMN_FORMAT_SYMBOL, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE | COLUMN_MIDDLE_ALIGN)
        .set_width(imgui_get_font_ui_scale(120.0f));

    table_add_column(table, search_table_column_change_p, " Day %||" ICON_MD_PRICE_CHANGE " Day % ", COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE)
        .set_width(imgui_get_font_ui_scale(100.0f))
        .set_style_formatter(LCCCR(search_table_column_change_p_formatter(_1, _2, _3, _4, 2.9)));

    table_add_column(table, search_table_column_change_week, "  1W " ICON_MD_CALENDAR_VIEW_WEEK "||" ICON_MD_CALENDAR_VIEW_WEEK " % since 1 week", COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ROUND_NUMBER)
        .set_width(imgui_get_font_ui_scale(80.0f))
        .set_style_formatter(LCCCR(search_table_column_change_p_formatter(_1, _2, _3, _4, 1.6)));
    
    table_add_column(table, search_table_column_change_month, "  1M " ICON_MD_CALENDAR_VIEW_MONTH "||" ICON_MD_CALENDAR_VIEW_MONTH " % since 1 month", COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ROUND_NUMBER)
        .set_width(imgui_get_font_ui_scale(80.0f))
        .set_style_formatter(LCCCR(search_table_column_change_p_formatter(_1, _2, _3, _4, 4.0)));

    table_add_column(table, search_table_column_change_year, "1Y " ICON_MD_CALENDAR_MONTH "||" ICON_MD_CALENDAR_MONTH " % since 1 year", COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ROUND_NUMBER)
        .set_width(imgui_get_font_ui_scale(80.0f))
        .set_style_formatter(LCCCR(search_table_column_change_p_formatter(_1, _2, _3, _4, 10.0)));
    
    table_add_column(table, search_table_column_change_max, "MAX %||" ICON_MD_CALENDAR_MONTH " % since creation", COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT |  COLUMN_ROUND_NUMBER)
        .set_width(imgui_get_font_ui_scale(100.0f))
        .set_style_formatter(LCCCR(search_table_column_change_p_formatter(_1, _2, _3, _4, 25.0)));

    table_add_column(table, search_table_column_return_rate, " R. " ICON_MD_ASSIGNMENT_RETURN "||" ICON_MD_ASSIGNMENT_RETURN " Return Rate (Yield)", COLUMN_FORMAT_PERCENTAGE, COLUMN_HIDE_DEFAULT | COLUMN_ZERO_USE_DASH | COLUMN_SORTABLE)
        .set_width(imgui_get_font_ui_scale(90.0f))
        .set_style_formatter(search_table_column_dividends_formatter);

    table_add_column(table, search_table_column_since, "Since " ICON_MD_FILTER_VINTAGE "||" ICON_MD_FILTER_VINTAGE " First day with stock", COLUMN_FORMAT_DATE, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE)
        .set_width(imgui_get_font_ui_scale(120.0f));

    table_add_column(table, search_table_column_percentage_per_year, "Y/Y %||" ICON_MD_CALENDAR_MONTH " Year after year % gain", COLUMN_FORMAT_PERCENTAGE, COLUMN_HIDE_DEFAULT |COLUMN_SORTABLE)
        .set_style_formatter(LCCCR(search_table_column_change_p_formatter(_1, _2, _3, _4, SETTINGS.good_dividends_ratio * 100.0)));

    table_add_column(table, search_table_column_price, "    Price " ICON_MD_MONETIZATION_ON "||" ICON_MD_MONETIZATION_ON " Market Price", COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_NOCLIP_CONTENT)
        .set_width(imgui_get_font_ui_scale(120.0f));

    return table;
}

FOUNDATION_STATIC bool search_window_event_query_updated(const dispatcher_event_args_t& args)
{
    search_window_t* search_window = (search_window_t*)args.user_data;
    FOUNDATION_ASSERT(search_window && search_window->table);

    if (search_window->table)
        search_window->table->needs_sorting = true;
    return true;
}

FOUNDATION_STATIC bool search_window_event_db_loaded(const dispatcher_event_args_t& args)
{
    search_window_t* search_window = (search_window_t*)args.user_data;
    FOUNDATION_ASSERT(search_window && search_window->table);

    const size_t query_length = string_length(_search->query);
    if (query_length)
        search_window_execute_query(search_window, _search->query, query_length);
    return true;
}

FOUNDATION_STATIC search_window_t* search_window_allocate()
{
    search_window_t* search_window = MEM_NEW(HASH_SEARCH, search_window_t);
    search_window->db = _search->db;
    search_window->table = search_create_table();

    string_t opening_query = string_copy(STRING_BUFFER(search_window->query), _search->query, string_length(_search->query));
    if (!string_is_null(opening_query))
        search_window_execute_query(search_window, STRING_ARGS(opening_query));

    search_window->event_db_loaded = dispatcher_register_event_listener(EVENT_SEARCH_DATABASE_LOADED, search_window_event_db_loaded, 0U, search_window);
    search_window->event_query_updated = dispatcher_register_event_listener(EVENT_SEARCH_QUERY_UPDATED, search_window_event_query_updated, 0U, search_window);

    return search_window;
}

FOUNDATION_STATIC void search_window_deallocate(void* window)
{
    search_window_t* search_window = (search_window_t*)window;
    FOUNDATION_ASSERT(search_window);

    dispatcher_unregister_event_listener(search_window->event_db_loaded);
    dispatcher_unregister_event_listener(search_window->event_query_updated);

    search_window_clear_results(search_window);
    
    {
        SHARED_WRITE_LOCK(search_window->lock);
        search_window->handle = 0;
        table_deallocate(search_window->table);
        array_deallocate(search_window->results);
    }

    MEM_DELETE(search_window);
}

FOUNDATION_STATIC void search_open_quick_search()
{
    FOUNDATION_ASSERT(_search->db);

    _search->startup_signal.signal();
    search_window_t* search_window = search_window_allocate();
    search_window->handle = window_open(HASH_SEARCH, STRING_CONST("Search"), 
        L1(search_window_render(window_get_user_data(_1))),
        L1(search_window_deallocate(window_get_user_data(_1))), 
        search_window, WindowFlags::Dialog);
}

FOUNDATION_STATIC void search_menu()
{        
    if (!search_available())
        return;

    if (shortcut_executed(false, true, ImGuiKey_GraveAccent))
        search_open_quick_search();

    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu(tr("Symbols")))
    {
        //ImGui::Separator();
        if (ImGui::MenuItem(tr("Search"), ICON_MD_KEYBOARD_OPTION_KEY "+`", nullptr, true))
            search_open_quick_search();

        ImGui::Separator();
        
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

FOUNDATION_STATIC expr_result_t search_expr_stats(const expr_func_t* f, vec_expr_t* args, void* context)
{
    search_database_t* db = _search->db;
    FOUNDATION_ASSERT(db);

    search_database_print_stats(db);

    return NIL;
}

FOUNDATION_STATIC expr_result_t search_expr_index_document(const expr_func_t* f, vec_expr_t* args, void* context)
{
    search_database_t* db = _search->db;
    FOUNDATION_ASSERT(db);

    string_const_t symbol = expr_eval_get_string_arg(args, 0, "Failed to get document name");
    if (!eod_fetch("fundamentals", symbol.str, FORMAT_JSON, LC1(search_index_fundamental_data(_1, symbol))))
    {
        log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to fetch %.*s fundamental"), STRING_FORMAT(symbol));
        return false;
    }

    log_infof(HASH_SEARCH, STRING_CONST("Indexed %.*s\n\tSymbols: %u\n\tProperties: %u"), 
        STRING_FORMAT(symbol), search_database_document_count(db), search_database_index_count(db));

    // Raise event for search window to refresh itself.
    return dispatcher_post_event(EVENT_SEARCH_DATABASE_LOADED);
}

FOUNDATION_STATIC expr_result_t search_expr_remove_document(const expr_func_t* f, vec_expr_t* args, void* context)
{
    search_database_t* db = _search->db;
    FOUNDATION_ASSERT(db);

    string_const_t doc_name = expr_eval_get_string_arg(args, 0, "Failed to get document name");
    auto doc = search_database_find_document(db, STRING_ARGS(doc_name));
    if (doc == SEARCH_DOCUMENT_INVALID_ID)
        return NIL;

    return search_database_remove_document(db, doc);
}

FOUNDATION_STATIC expr_result_t search_expr_eval(const expr_func_t* f, vec_expr_t* args, void* context)
{
    expr_result_t* results = nullptr;
    string_const_t search_expression = expr_eval_get_string_arg(args, 0, "Failed to get search expression");

    _search->startup_signal.signal();

    // Run simple EOD API query if the search text does not contain any special characters
    if (search_expression.length > 1 &&
        string_find(STRING_ARGS(search_expression), ':', 1) == STRING_NPOS &&
        string_find(STRING_ARGS(search_expression), '=', 1) == STRING_NPOS &&
        string_find(STRING_ARGS(search_expression), '!', 1) == STRING_NPOS &&
        string_find(STRING_ARGS(search_expression), '<', 1) == STRING_NPOS &&
        string_find(STRING_ARGS(search_expression), '>', 1) == STRING_NPOS)
    {
        eod_fetch("search", search_expression.str, FORMAT_JSON, "limit", "5", [&results](const json_object_t& json)
        {
            if (!json.resolved())
                return;

            for (auto e : json)
            {
                string_const_t code = e["Code"].as_string();
                string_const_t exchange = e["Exchange"].as_string();

                string_t symbol = string_format(SHARED_BUFFER(16), STRING_CONST("%.*s.%.*s"), STRING_FORMAT(code), STRING_FORMAT(exchange));

                if (!stock_valid(symbol.str, symbol.length))
                    return;

                expr_result_t result(string_to_const(symbol));
                array_push(results, result);
            }   
        });

        // If the search text looks like a symbol, i.e. GFL.TO, then lets query the real-time value to see if it resolves.
        if (search_expression.length > 3 && search_expression.length < 16 && search_expression.str[0] != '.' &&
            string_find(STRING_ARGS(search_expression), '.', 1) != STRING_NPOS &&
            string_find(STRING_ARGS(search_expression), ' ', 1) == STRING_NPOS)
        {
            eod_fetch("real-time", search_expression.str, FORMAT_JSON, [&results](const json_object_t& json)
            {
                if (!json.resolved())
                    return;

                auto price = json["close"].as_number();
                if (math_real_is_nan(price))
                    return;

                string_const_t code = json["code"].as_string();
                if (string_is_null(code))
                    return;

                expr_result_t result(code);
                array_push(results, result);
            }, 0);
        }
    }

    if (_search->db)
    {
        try
        {
            auto* db = _search->db;
            search_query_handle_t query = search_database_query(db, STRING_ARGS(search_expression));
            if (search_database_query_is_completed(db, query))
            {
                const search_result_t* search_results = search_database_query_results(db, query);

                foreach(r, search_results)
                {
                    string_const_t symbol = search_database_document_name(db, (search_document_handle_t)r->id);
                    expr_result_t result(symbol);
                    array_push(results, result);
                }

                search_database_query_dispose(db, query);
            }
        }
        catch (SearchQueryException err)
        {
            array_deallocate(results);
            throw ExprError(EXPR_ERROR_EXCEPTION, "Failed to evaluate search expression %s (%d)", err.msg, err.error);
        }
    }

    // Remove duplicates
    if (results)
    {
        array_sort(results, [](const expr_result_t& a, const expr_result_t& b)
        {
            string_const_t sa = a.as_string();
            string_const_t sb = b.as_string();
            return string_compare(sa.str, sa.length, sb.str, sb.length);
        });

        for (unsigned i = 1, end = array_size(results); i < end; ++i)
        {
            expr_result_t& prev = results[i-1];
            expr_result_t& current = results[i];

            if (prev.value == current.value && prev.index == current.index)
            {
                array_erase_ordered_safe(results, i);
                --i;
                --end;
            }
        }
    }

    return expr_eval_list(results);
}

FOUNDATION_STATIC expr_result_t search_expr_keywords(const expr_func_t* f, vec_expr_t* args, void* context)
{
    search_database_t* db = _search->db;
    FOUNDATION_ASSERT(db);

    string_t* keywords = search_database_property_keywords(db);

    // Print all keywords
    for (size_t i = 0, end = array_size(keywords); i < end; ++i)
    {
        log_info(HASH_SEARCH, STRING_ARGS(keywords[i]));
    }

    string_array_deallocate(keywords);

    return NIL;
}

FOUNDATION_STATIC string_t* search_load_queries(const char* filename, size_t filename_length)
{
    string_const_t queries_file_path = session_get_user_file_path(filename, filename_length);
    stream_t* queries_stream = fs_open_file(STRING_ARGS(queries_file_path), STREAM_IN);
    if (!queries_stream)
    {
        log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to open queries file %.*s"), STRING_FORMAT(queries_file_path));
        return nullptr;
    }

    string_t* queries = nullptr;
    while (!stream_eos(queries_stream))
    {
        string_t query = stream_read_line(queries_stream, '\n');
        if (string_is_null(query))
        {
            string_deallocate(query.str);
            continue;
        }

        array_push(queries, query);
    }

    stream_deallocate(queries_stream);
    return queries;
}

FOUNDATION_STATIC void search_save_queries(string_t* queries, const char* filename, size_t filename_length)
{
    string_const_t queries_file_path = session_get_user_file_path(filename, filename_length);
    stream_t* queries_stream = fs_open_file(STRING_ARGS(queries_file_path), STREAM_CREATE | STREAM_OUT | STREAM_TRUNCATE);
    if (!queries_stream)
    {
        log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to open queries file %.*s"), STRING_FORMAT(queries_file_path));
        return;
    }

    foreach(q, queries)
    {
        stream_write_string(queries_stream, q->str, q->length);
        stream_write_endl(queries_stream);
    }

    stream_deallocate(queries_stream);
}

FOUNDATION_STATIC void search_start_indexing()
{
    FOUNDATION_ASSERT_MSG(!dispatcher_thread_is_running(_search->indexing_thread), "Stop indexing thread before starting it again");

    {
        SHARED_WRITE_LOCK(_search->exchanges_lock);
        if (_search->exchanges)
            string_array_deallocate(_search->exchanges);
    }

    _search->exchanges = (string_t*)search_stock_exchanges();
    
    // Start indexing thread that query a stock exchange market and then 
    // for each title query its fundamental values to build a search database.
    _search->indexing_thread = dispatch_thread("Search Indexer", search_indexing_thread_fn);
    FOUNDATION_ASSERT(_search->indexing_thread);
}

FOUNDATION_STATIC bool search_stop_indexing(bool save_db)
{
    // Make sure we stop waiting for initial startup
    dispatcher_thread_signal(_search->indexing_thread);
    _search->startup_signal.signal();

    if (!dispatcher_thread_stop(_search->indexing_thread))
        return false;
    _search->indexing_thread = 0;

    if (save_db && search_database_is_dirty(_search->db))
    {
        if (main_is_interactive_mode())
        {
            // Save search database on disk
            string_const_t search_db_path = session_get_user_file_path(STRING_CONST("search.db"));
            stream_t* search_db_stream = fs_open_file(STRING_ARGS(search_db_path), STREAM_CREATE | STREAM_OUT | STREAM_BINARY | STREAM_TRUNCATE);
            if (search_db_stream)
            {
                TIME_TRACKER("Saving search database");
                search_database_save(_search->db, search_db_stream);
                stream_deallocate(search_db_stream);
            }
        }
        else
        {
            log_warnf(HASH_SEARCH, WARNING_SUSPICIOUS, STRING_CONST("Search database not saved, running in non-interactive mode"));
        }
    }

    search_database_deallocate(_search->db);

    return true;
}

//
// # PUBLIC API
//

bool search_available()
{
    return _search && _search->db;
}

const string_t* search_stock_exchanges()
{
    FOUNDATION_ASSERT(_search);

    if (_search->exchanges)
        return _search->exchanges;

    SHARED_WRITE_LOCK(_search->exchanges_lock);
    if (session_key_exists(SEARCH_EXCHANGES_SESSION_KEY))
    {
        _search->exchanges = session_get_string_list(SEARCH_EXCHANGES_SESSION_KEY);
    }
    else
    {
        array_push(_search->exchanges, string_clone(STRING_CONST("TO")));
        array_push(_search->exchanges, string_clone(STRING_CONST("US")));
    }

    return _search->exchanges;
}

bool search_render_settings()
{
    bool updated = false;

    ImGui::NextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TrTextWrapped("Search stock exchange to index");

    ImGui::NextColumn();
    ImGui::ExpandNextItem();
    if (ImWallet::Exchanges(_search->exchanges))
    {
        if (session_set_string_list(SEARCH_EXCHANGES_SESSION_KEY, _search->exchanges))
        {
            // Stop indexing thread and restart it.
            if (search_stop_indexing(false))
            {
                search_start_indexing();
                updated = true;
            }
        }

    }

    ImGui::NextColumn();
    ImGui::TrTextWrapped("Changing that setting will restart the indexing process but if will not delete already indexed stock from removed exchanges. "
        "Indexing a new stock exchange can take between 1 to 3 hours.");

    return updated;
}

FOUNDATION_STATIC void search_table_expr_symbol_drawer(const table_cell_t& value)
{
    string_const_t code{};
    if (value.format == COLUMN_FORMAT_SYMBOL)
        code = SYMBOL_CONST(value.symbol);
    else if (value.format == COLUMN_FORMAT_TEXT)
        code = string_to_const(value.text);

    if (code.length)
    {
        ImGui::PushID(code.str);
        search_table_draw_symbol(code, false);
        ImGui::PopID();
        if (ImGui::BeginPopupContextItem(code.str))
        {
            pattern_contextual_menu(code.str, code.length);
            ImGui::EndPopup();
        }
        
    }
}

//
// # SYSTEM
//

FOUNDATION_STATIC void search_initialize()
{
    _search = MEM_NEW(HASH_SEARCH, SEARCH_MODULE);

    search_start_indexing();

    _search->saved_queries = search_load_queries(STRING_CONST("queries.txt"));
    session_get_string("search_query", STRING_BUFFER(_search->query), "");

    expr_register_function("SEARCH", search_expr_eval, nullptr, 0);
    expr_register_function("SEARCH_KEYWORDS", search_expr_keywords, nullptr, 0);
    expr_register_function("SEARCH_REMOVE_DOCUMENT", search_expr_remove_document, nullptr, 0);
    expr_register_function("SEARCH_INDEX", search_expr_index_document, nullptr, 0);
    expr_register_function("SEARCH_STATS", search_expr_stats, nullptr, 0);

    table_expr_add_type_drawer(STRING_CONST("symbol"), search_table_expr_symbol_drawer);

    module_register_menu(HASH_SEARCH, search_menu);
}

FOUNDATION_STATIC void search_shutdown()
{   
    session_set_string("search_query", _search->query, string_length(_search->query));

    // Save queries to queries.json
    search_save_queries(_search->saved_queries, STRING_CONST("queries.txt"));
    string_array_deallocate(_search->saved_queries);

    search_stop_indexing(true);

    string_array_deallocate(_search->exchanges);

    MEM_DELETE(_search);
}

DEFINE_MODULE(SEARCH, search_initialize, search_shutdown, MODULE_PRIORITY_MODULE);
