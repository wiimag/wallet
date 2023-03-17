/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "search.h"

#include "eod.h"
#include "stock.h"
#include "events.h"
#include "settings.h"
#include "pattern.h"
#include "logo.h"
#include "news.h"

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
#include <framework/table.h>
#include <framework/expr.h>
#include <framework/window.h>
#include <framework/array.h>

#include <foundation/stream.h>

#define HASH_SEARCH static_hash_string("search", 6, 0xc9d4e54fbae76425ULL)

struct search_window_t;

static const ImU32 SEARCH_PATTERN_VIEWED_COLOR = (ImU32)ImColor::HSV(0.6f, 0.3f, 0.9f);

struct search_result_entry_t
{
    search_database_t*       db{ nullptr };
    search_document_handle_t doc{ SEARCH_DOCUMENT_INVALID_ID };
    
    stock_handle_t           stock{};
    tick_t                   uptime{ 0 };
    bool                     viewed{ false };
};

struct search_window_t 
{
    search_database_t*             db{ nullptr };
    table_t*                       table{ nullptr };
    search_result_entry_t*         results{ nullptr };
    char                           query[1024] = { 0 };
    tick_t                         query_tick{ 0 };
    dispatcher_event_listener_id_t dispatcher_event_db_loaded{ INVALID_DISPATCHER_EVENT_LISTENER_ID };

    char                           error[1024] = { 0 };
};

static struct SEARCH_MODULE {
    
    search_database_t*             db{ nullptr };
    char                           query[1024] = { 0 };
    dispatcher_thread_handle_t     indexing_thread{};

} *_search;

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
};

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

FOUNDATION_STATIC void search_index_news_data(const json_object_t& json, search_document_handle_t doc)
{
    search_database_t* db = _search->db;

    for (auto n : json)
    {
        time_t date;
        string_const_t date_string = n["date"].as_string();
        if (!string_try_convert_date(date_string.str, min(to_size(10), date_string.length), date))
            continue;

        search_database_index_property(db, doc, STRING_CONST("news"), (double)date);

        #if 0
        for (auto s : n["symbols"])
        {
            string_const_t symbol = s.as_string();
            if (string_is_null(symbol))
                continue;

            search_database_index_property(db, doc, STRING_CONST("news"), STRING_ARGS(symbol), false);
        }
        #endif

        for (auto t : n["tags"])
        {
            string_const_t tag = t.as_string();
            if (string_is_null(tag))
                continue;

            search_database_index_text(db, doc, STRING_ARGS(tag), false);
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
            search_database_index_property(db, doc, STRING_ARGS(id), STRING_ARGS(value), value.length < 12);
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

    string_const_t code = General["Code"].as_string();
    if (string_is_null(code))
        return;
        
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
        if (updated_elapsed_time > 90)
        {
            log_debugf(HASH_SEARCH, STRING_CONST("%.*s is too old, skipping for indexing"), STRING_FORMAT(code));
            return;
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

    string_const_t name = General["Name"].as_string();
    string_const_t country = General["Country"].as_string();
    string_const_t description = General["Description"].as_string();
    string_const_t industry = General["Industry"].as_string();
    string_const_t sector = General["Sector"].as_string();
    string_const_t gic_sector = General["GicSector"].as_string();
    string_const_t gic_group = General["GicGroup"].as_string();
    string_const_t gic_sub_industry = General["GicSubIndustry"].as_string();
    string_const_t gic_industry = General["GicIndustry"].as_string();
    string_const_t home_category = General["HomeCategory"].as_string();
        
    search_document_handle_t doc = search_database_find_document(db, STRING_ARGS(symbol));
    if (doc == SEARCH_DOCUMENT_INVALID_ID)
        doc = search_database_add_document(db, STRING_ARGS(symbol));

    TIME_TRACKER(2.0, HASH_SEARCH, "[%u] Indexing [%12.*s] %-7.*s -> %.*s -> %.*s",
        doc, STRING_FORMAT(isin), STRING_FORMAT(symbol), STRING_FORMAT(type), STRING_FORMAT(name));

    // Index symbol
    search_database_index_word(db, doc, STRING_ARGS(symbol), true);

    // Index basic information
    search_database_index_text(db, doc, STRING_ARGS(name));
    search_database_index_word(db, doc, STRING_ARGS(name), false);
    search_database_index_text(db, doc, STRING_ARGS(country), false);
    search_database_index_text(db, doc, STRING_ARGS(type), false);
    search_database_index_text(db, doc, STRING_ARGS(description), false);
    search_database_index_text(db, doc, STRING_ARGS(industry), true);
    search_database_index_text(db, doc, STRING_ARGS(sector), true);
    search_database_index_text(db, doc, STRING_ARGS(gic_sector), true);    
    search_database_index_text(db, doc, STRING_ARGS(gic_group), true);
    search_database_index_text(db, doc, STRING_ARGS(gic_industry), true);
    search_database_index_text(db, doc, STRING_ARGS(gic_sub_industry), true);
    search_database_index_text(db, doc, STRING_ARGS(home_category), true);
    search_database_index_exact_match(db, doc, STRING_ARGS(isin), true);

    search_database_index_property(db, doc, STRING_CONST("exchange"), STRING_ARGS(exchange), false);

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
        else //if (value.length < SEARCH_INDEX_WORD_MAX_LENGTH-1)
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
                search_database_index_text(db, doc, STRING_ARGS(value), false);
            }
            else
                search_database_index_property(db, doc, STRING_ARGS(id), STRING_ARGS(value), value.length < 12);
        }
    }
    
    if (!eod_fetch("news", nullptr, FORMAT_JSON_CACHE, "s", symbol.str, LC1(search_index_news_data(_1, doc)), 8 * 24 * 60 * 60ULL))
    {
        log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to fetch news for symbol %*.s"), STRING_FORMAT(symbol));
    }

    search_database_document_update_timestamp(db, doc);
}

FOUNDATION_STATIC void search_index_exchange_symbols(const json_object_t& data, const char* market, size_t market_length, bool* stop_indexing)
{
    MEMORY_TRACKER(HASH_SEARCH);

    search_database_t* db = _search->db;

    for(auto e : data)
    {        
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

        if (!eod_availalble())
        {
            *stop_indexing = true;
            log_warnf(HASH_SEARCH, WARNING_NETWORK, STRING_CONST("Failed to connect to EOD services, terminating indexing"));
            return;
        }

        if (eod_capacity() > 0.8)
        {
            *stop_indexing = true;
            log_warnf(HASH_SEARCH, WARNING_NETWORK, STRING_CONST("EOD full capacity is near, stopping search indexing."));
            return;
        }
    
        const tick_t st = time_current();
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
        if (string_equal_nocase(STRING_ARGS(type), STRING_CONST("FUND")))
            continue;

        if (string_is_null(isin) && string_equal_nocase(STRING_ARGS(type), STRING_CONST("ETF")))
            continue;

        char symbol_buffer[SEARCH_INDEX_WORD_MAX_LENGTH];
        string_t symbol = string_format(STRING_BUFFER(symbol_buffer), STRING_CONST("%.*s.%.*s"), STRING_FORMAT(code), to_int(market_length), market);

        search_document_handle_t doc = search_database_find_document(db, STRING_ARGS(symbol));
        if (doc != SEARCH_DOCUMENT_INVALID_ID)
        {
            const time_t doc_timestamp = search_database_document_timestamp(db, doc);
            const double days_old = time_elapsed_days(doc_timestamp, time_now());
            if (days_old < 7.0)
                continue;

            if (days_old > 25)
            {
                if (search_database_remove_document(db, doc))
                    doc = SEARCH_DOCUMENT_INVALID_ID;
            }
        }

        // Fetch symbol fundamental data
        if (!eod_fetch("fundamentals", symbol.str, FORMAT_JSON_CACHE, 
            LC1(search_index_fundamental_data(_1, string_to_const(symbol))), 31 * 24 * 60 * 60ULL))
        {
            log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to fetch %.*s fundamental"), STRING_FORMAT(symbol));
        }
            
        if (thread_try_wait(50))
        {
            *stop_indexing = true;
            break;
        }
    }
}

FOUNDATION_STATIC void* search_indexing_thread_fn(void* data)
{
    MEMORY_TRACKER(HASH_SEARCH);

    // Load search database
    _search->db = search_database_allocate();
    string_const_t search_db_path = session_get_user_file_path(STRING_CONST("search.db"));
    stream_t* search_db_stream = fs_open_file(STRING_ARGS(search_db_path), STREAM_IN | STREAM_BINARY);
    if (search_db_stream)
    {
        if (thread_try_wait(0))
            return 0;
            
        TIME_TRACKER("Loading search database");
        search_database_load(_search->db, search_db_stream);
        stream_deallocate(search_db_stream);

        if (thread_try_wait(0))
            return 0;
    }
    
    dispatcher_post_event(EVENT_SEARCH_DATABASE_LOADED);

    if (environment_command_line_arg("disable-indexing"))
    {
        log_warnf(HASH_SEARCH, WARNING_SUSPICIOUS, STRING_CONST("Search indexing is disabled, skipping search indexing"));
        return 0;
    }

    if (main_is_daemon_mode())
    {
        log_warnf(HASH_SEARCH, WARNING_SUSPICIOUS, STRING_CONST("Batch mode, skipping search indexing"));
        return 0;
    }
    
    string_const_t stock_markets[] = {
        CTEXT("TO"),
        CTEXT("NEO"),
        CTEXT("V"),
        CTEXT("US")
    };
    
    // Fetch all titles from stock exchange market
    bool stop_indexing = false;
    for (auto market : stock_markets)
    {
        if (stop_indexing)
            break;
            
        if (thread_try_wait(1000))
            break;
            
        auto fetch_fn = [market, &stop_indexing](const json_object_t& data) { search_index_exchange_symbols(data, STRING_ARGS(market), &stop_indexing); };
        if (!eod_fetch("exchange-symbol-list", market.str, FORMAT_JSON_CACHE, fetch_fn, 30 * 24 * 60 * 60ULL))
        {
            log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to fetch %.*s symbols"), STRING_FORMAT(market));
        }

        log_infof(HASH_SEARCH, STRING_CONST("Search indexing completed for the market %.*s"), STRING_FORMAT(market));
    }    

    return 0;
}

FOUNDATION_STATIC void search_window_execute_query(search_window_t* sw, const char* search_text, size_t search_text_length)
{
    FOUNDATION_ASSERT(sw);

    search_database_t* db = sw->db;
    FOUNDATION_ASSERT(db);

    // Clear any previous errors
    sw->error[0] = 0;

    array_clear(sw->results);
    if (search_text == nullptr || search_text_length == 0)
        return;

    sw->query_tick = time_current();
    try
    {
        search_query_handle_t query = search_database_query(db, search_text, search_text_length);
        if (search_database_query_is_completed(db, query))
        {
            const search_result_t* results = search_database_query_results(db, query);

            // TODO: Sort results?

            foreach(r, results)
            {
                search_result_entry_t entry;
                entry.db = db;
                entry.doc = (search_document_handle_t)r->id;
                array_push_memcpy(sw->results, &entry);
            }

            if (!search_database_query_dispose(db, query))
            {
                log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to dispose query"));
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

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - imgui_get_font_ui_scale(98.0f));
    if (ImGui::InputTextWithHint("##SearchQuery", "Search stocks... " ICON_MD_FILTER_LIST_ALT, STRING_BUFFER(sw->query), ImGuiInputTextFlags_AutoSelectAll))
    {
        const size_t search_query_length = string_length(sw->query);
        search_window_execute_query(sw, sw->query, search_query_length);        
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Update")))
    {
        sw->table->needs_sorting = true;
        const size_t search_query_length = string_length(sw->query);
        search_window_execute_query(sw, sw->query, search_query_length);
    }

    table_render(sw->table, sw->results, 0, -ImGui::GetFontSize() - 8.0f);

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
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(" Symbols: %u \n Properties: %u ", search_database_document_count(sw->db), search_database_index_count(sw->db));
        }
    }
}

FOUNDATION_STATIC const stock_t* search_result_resolve_stock(search_result_entry_t* entry, const column_t* column)
{
    if (entry->stock.initialized())
        return entry->stock.resolve();

    if (column->flags & COLUMN_SORTING_ELEMENT)
        return nullptr;
     
    if (entry->uptime == 0)
    {
        entry->uptime = time_current();
        return nullptr;
    }
    else if (time_elapsed(entry->uptime) < 1.25)
        return nullptr;

    string_const_t symbol = search_database_document_name(entry->db, entry->doc);
    entry->stock = stock_request(STRING_ARGS(symbol), FetchLevel::REALTIME | FetchLevel::FUNDAMENTALS | FetchLevel::EOD);
    entry->viewed = pattern_find(STRING_ARGS(symbol)) >= 0;

    return entry->stock.resolve();
}

FOUNDATION_STATIC cell_t search_table_column_symbol(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    FOUNDATION_ASSERT(entry);

    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s)
    {
        string_const_t code = SYMBOL_CONST(s->code);
        if (column->flags & COLUMN_RENDER_ELEMENT)
        {
            if (entry->viewed)
                ImGui::PushStyleColor(ImGuiCol_Text, SEARCH_PATTERN_VIEWED_COLOR);
            
            const float font_size = ImGui::GetFontSize();
            ImGui::Text("%.*s", STRING_FORMAT(code));

            if (entry->viewed)
                ImGui::PopStyleColor();

            ImGui::SameLine(ImGui::GetContentRegionAvail().x - font_size, 0.0f);
            ImVec2 logo_size{ font_size, font_size };
            logo_render_icon(STRING_ARGS(code), logo_size, false, true, nullptr);
        }
        
        return code;
    }

    string_const_t symbol = search_database_document_name(entry->db, entry->doc);
    FOUNDATION_ASSERT(!string_is_null(symbol));

    if (column->flags & COLUMN_RENDER_ELEMENT)
        ImGui::Text("%.*s", STRING_FORMAT(symbol));

    return symbol;
}

FOUNDATION_STATIC cell_t search_table_column_name(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    FOUNDATION_ASSERT(entry);

    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return nullptr;

    return s->name;
}

FOUNDATION_STATIC cell_t search_table_column_country(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return nullptr;
    return s->country;
}

FOUNDATION_STATIC cell_t search_table_column_exchange(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return nullptr;
    return s->exchange;
}

FOUNDATION_STATIC cell_t search_table_column_currency(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return nullptr;
    return s->currency;
}

FOUNDATION_STATIC cell_t search_table_column_type(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return nullptr;
    return s->type;
}

FOUNDATION_STATIC cell_t search_table_column_sector(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return nullptr;
    return s->sector;
}

FOUNDATION_STATIC cell_t search_table_column_industry(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return nullptr;
    return s->industry;
}

FOUNDATION_STATIC cell_t search_table_column_category(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return nullptr;
    return s->category;
}

FOUNDATION_STATIC cell_t search_table_column_isin(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return nullptr;
    return s->isin;
}

FOUNDATION_STATIC cell_t search_table_column_change_p(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return NAN;
    return s->current.change_p;
}

FOUNDATION_STATIC cell_t search_table_column_change_week(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return NAN;

    auto ed = stock_get_EOD(s, -7, true);
    if (ed == nullptr)
        return NAN;
    return (s->current.close - ed->adjusted_close) / ed->adjusted_close * 100.0f;
}

FOUNDATION_STATIC cell_t search_table_column_change_month(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return NAN;

    auto ed = stock_get_EOD(s, -31, true);
    if (ed == nullptr)
        return NAN;
    return (s->current.close - ed->adjusted_close) / ed->adjusted_close * 100.0f;
}

FOUNDATION_STATIC cell_t search_table_column_change_year(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return NAN;

    auto ed = stock_get_EOD(s, -365, true);
    if (ed == nullptr)
        return NAN;
    return (s->current.close - ed->adjusted_close) / ed->adjusted_close * 100.0f;
}

FOUNDATION_STATIC cell_t search_table_column_change_max(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return NAN;

    auto ed = stock_get_EOD(s, -365 * 40, true);
    if (ed == nullptr)
        return NAN;
    return (s->current.close - ed->adjusted_close) / ed->adjusted_close * 100.0f;
}

FOUNDATION_STATIC cell_t search_table_column_return_rate(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return nullptr;
    return s->dividends_yield.fetch() * 100.0;
}

FOUNDATION_STATIC cell_t search_table_column_price(table_element_ptr_t element, const column_t* column)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return nullptr;
    return s->current.close;
}

FOUNDATION_STATIC void search_table_column_dividends_formatter(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    FOUNDATION_ASSERT(entry);
    
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return;

    if (s->dividends_yield.fetch() > SETTINGS.good_dividends_ratio)
    {
        style.types |= COLUMN_COLOR_TEXT;
        style.text_color = ImColor::HSV(117 / 360.0f, 0.68f, 0.90f); // hsv(117, 68%, 90%)
    }
}

FOUNDATION_STATIC void search_table_column_change_p_formatter(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style, double threshold)
{
    if (cell->number > threshold)
    {
        style.types |= COLUMN_COLOR_TEXT;
        style.text_color = ImColor::HSV(117 / 360.0f, 0.68f, 0.90f); // hsv(117, 68%, 90%)
    }
}

FOUNDATION_STATIC void search_table_column_description_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s == nullptr)
        return;
    string_table_symbol_t tooltip_symbol = s->description.fetch();
    if (tooltip_symbol == 0)
        return;

    string_const_t tooltip = string_table_decode_const(tooltip_symbol);
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 800.0f);
    ImGui::Text("%.*s", STRING_FORMAT(tooltip));
    ImGui::PopTextWrapPos();
}

FOUNDATION_STATIC void search_table_column_code_color(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    if (s != nullptr && entry->viewed)
    {
        style.types |= COLUMN_COLOR_TEXT;
        style.text_color = ImColor::HSV(!entry->viewed ? 0.4f : 0.6f, 0.3f, 0.9f);
    }
}

FOUNDATION_STATIC void search_table_contextual_menu(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    search_result_entry_t* entry = (search_result_entry_t*)element;
    const stock_t* s = search_result_resolve_stock(entry, column);
    string_const_t symbol = search_database_document_name(entry->db, entry->doc);

    if (s == nullptr && string_is_null(symbol))
        return;

    if (pattern_menu_item(STRING_ARGS(symbol)))
        entry->viewed = true;

    ImGui::Separator();

    if (ImGui::MenuItem(tr("Read News")))
        news_open_window(STRING_ARGS(symbol));

    #if BUILD_DEVELOPMENT
    if (ImGui::MenuItem(tr("Browse News")))
        {open_in_shell(eod_build_url("news", nullptr, FORMAT_JSON, "s", symbol.str).str);}

    if (ImGui::MenuItem(tr("Browse Fundamentals")))
        open_in_shell(eod_build_url("fundamentals", symbol.str, FORMAT_JSON).str);
    #endif

    ImGui::Separator();

    if (ImGui::MenuItem(tr("Re-index...")))
    {
        string_const_t expr = string_format_static(STRING_CONST("SEARCH_REMOVE_DOCUMENT(\"%.*s\")\nSEARCH_INDEX(\"%.*s\")"), STRING_FORMAT(symbol), STRING_FORMAT(symbol));
        eval(STRING_ARGS(expr));
    }
}

FOUNDATION_STATIC table_t* search_create_table()
{
    table_t* table = table_allocate("QuickSearch##15", TABLE_HIGHLIGHT_HOVERED_ROW | TABLE_LOCALIZATION_CONTENT);
    table->context_menu = search_table_contextual_menu;

    table_add_column(table, search_table_column_symbol, "Symbol", COLUMN_FORMAT_TEXT, COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING)
        .set_width(imgui_get_font_ui_scale(120.0f));

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
    
    table_add_column(table, search_table_column_price, "    Price " ICON_MD_MONETIZATION_ON "||" ICON_MD_MONETIZATION_ON " Market Price", COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_SORTABLE | COLUMN_NOCLIP_CONTENT)
        .set_width(imgui_get_font_ui_scale(120.0f));

    return table;
}

FOUNDATION_STATIC bool search_window_event_db_loaded(const dispatcher_event_args_t& args)
{
    search_window_t* search_window = (search_window_t*)args.user_data;
    FOUNDATION_ASSERT(search_window && search_window->table);

    const size_t query_length = string_length(_search->query);
    if (query_length)
        search_window_execute_query(search_window, _search->query, query_length);
    search_window->table->needs_sorting = true;
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

    search_window->dispatcher_event_db_loaded = dispatcher_register_event_listener(
        EVENT_SEARCH_DATABASE_LOADED, search_window_event_db_loaded, 0U, search_window);

    return search_window;
}

FOUNDATION_STATIC void search_window_deallocate(void* window)
{
    search_window_t* search_window = (search_window_t*)window;
    FOUNDATION_ASSERT(search_window);

    dispatcher_unregister_event_listener(search_window->dispatcher_event_db_loaded);
    
    table_deallocate(search_window->table);
    array_deallocate(search_window->results);

    MEM_DELETE(search_window);
}

FOUNDATION_STATIC void search_open_quick_search()
{
    FOUNDATION_ASSERT(_search->db);

    search_window_t* search_window = search_window_allocate();
    window_open(HASH_SEARCH, STRING_CONST("Search"), 
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

//
// # PUBLIC API
//

bool search_available()
{
    return _search && _search->db;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void search_initialize()
{
    _search = MEM_NEW(HASH_SEARCH, SEARCH_MODULE);

    // Start indexing thread that query a stock exchange market and then 
    // for each title query its fundamental values to build a search database.
    _search->indexing_thread = dispatch_thread("Search Indexer", search_indexing_thread_fn);
    FOUNDATION_ASSERT(_search->indexing_thread);

    session_get_string("search_query", STRING_BUFFER(_search->query), "");

    expr_register_function("SEARCH_KEYWORDS", search_expr_keywords, nullptr, 0);
    expr_register_function("SEARCH_REMOVE_DOCUMENT", search_expr_remove_document, nullptr, 0);
    expr_register_function("SEARCH_INDEX", search_expr_index_document, nullptr, 0);

    service_register_menu(HASH_SEARCH, search_menu);
}

FOUNDATION_STATIC void search_shutdown()
{   
    dispatcher_thread_stop(_search->indexing_thread);

    session_set_string("search_query", _search->query, string_length(_search->query));
    
    if (search_database_is_dirty(_search->db))
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

    MEM_DELETE(_search);
}

DEFINE_SERVICE(SEARCH, search_initialize, search_shutdown, SERVICE_PRIORITY_MODULE);
