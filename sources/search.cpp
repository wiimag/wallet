/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "search.h"

#include "app.h"
#include "eod.h"
#include "stock.h"
#include "events.h"
#include "settings.h"
#include "pattern.h"

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

#include <foundation/stream.h>

#define HASH_SEARCH static_hash_string("search", 6, 0xc9d4e54fbae76425ULL)

struct search_window_t;

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
    search_database_t*      db{ nullptr };
    table_t*                table{ nullptr };
    search_result_entry_t*  results{ nullptr };
    char                    query[1024] = { 0 };
    tick_t                  query_tick{ 0 };
};

static struct SEARCH_MODULE {
    
    search_database_t* db{ nullptr };
    dispatcher_thread_handle_t indexing_thread{};

} *_search;

//
// PRIVATE
//

FOUNDATION_STATIC void search_index_fundamental_data(const json_object_t& json, search_document_handle_t doc)
{
    MEMORY_TRACKER(HASH_SEARCH);

    search_database_t* db = _search->db;

    const auto General = json["General"];

    // Get description
    string_const_t description = General["Description"].as_string();
    if (!string_is_null(description))
        search_database_index_text(db, doc, STRING_ARGS(description), false);

    // Get industry
    string_const_t industry = General["Industry"].as_string();
    if (!string_is_null(industry))
        search_database_index_text(db, doc, STRING_ARGS(industry), true);

    // Get sector
    string_const_t sector = General["Sector"].as_string();
    if (!string_is_null(sector))
        search_database_index_text(db, doc, STRING_ARGS(sector), true);

    // Get GicSector
    string_const_t gic_sector = General["GicSector"].as_string();
    if (!string_is_null(gic_sector))
        search_database_index_text(db, doc, STRING_ARGS(gic_sector), true);

    // Get GicGroup
    string_const_t gic_group = General["GicGroup"].as_string();
    if (!string_is_null(gic_group))
        search_database_index_text(db, doc, STRING_ARGS(gic_group), true);

    // Get GicIndustry
    string_const_t gic_industry = General["GicIndustry"].as_string();
    if (!string_is_null(gic_industry))
        search_database_index_text(db, doc, STRING_ARGS(gic_industry), true);

    // Get GicSubIndustry
    string_const_t gic_sub_industry = General["GicSubIndustry"].as_string();
    if (!string_is_null(gic_sub_industry))
        search_database_index_text(db, doc, STRING_ARGS(gic_sub_industry), true);

    // Get HomeCategory
    string_const_t home_category = General["HomeCategory"].as_string();
    if (!string_is_null(home_category))
        search_database_index_text(db, doc, STRING_ARGS(home_category), true);
        
    // Get Address
    string_const_t address = General["Address"].as_string();
    if (!string_is_null(address))
        search_database_index_text(db, doc, STRING_ARGS(address), true);

    for (unsigned i = 0; i < json.token_count; ++i)
    {
        const json_token_t& token = json.tokens[i];
        if (token.type != JSON_STRING && token.type != JSON_PRIMITIVE)
            continue;
        
        string_const_t id = json_token_identifier(json.buffer, &token);
        if (id.length == 0 || id.length >= SEARCH_INDEX_WORD_MAX_LENGTH-1)
            continue;

        // When we encounter that field we stop indexing the fundamental JSON content as what follows is
        // a list of all the fields in the fundamental data, which is not useful for searching.
        if (string_equal_nocase(STRING_ARGS(id), STRING_CONST("outstandingShares")))
            return;

        // Skip some commonly long values
        if (string_equal_nocase(STRING_ARGS(id), STRING_CONST("description")))
            continue;
        else if (string_equal_nocase(STRING_ARGS(id), STRING_CONST("address")))
            continue;
        else if (string_equal_nocase(STRING_ARGS(id), STRING_CONST("weburl")))
            continue;
        else if (string_equal_nocase(STRING_ARGS(id), STRING_CONST("seclink")))
            continue;
        else if (string_equal_nocase(STRING_ARGS(id), STRING_CONST("disclaimer")))
            continue;

        string_const_t value = json_token_value(json.buffer, &token);
        if (value.length == 0 || string_equal(STRING_ARGS(value), STRING_CONST("null")))
            continue;

        if (string_equal(STRING_ARGS(value), STRING_CONST("NA")))
            continue;
                
        double number = NAN;
        if (value.length < 21 && string_try_convert_number(STRING_ARGS(value), number))
        {
            if (math_real_is_finite(number))
                search_database_index_property(db, doc, STRING_ARGS(id), number);
        }
        else //if (value.length < SEARCH_INDEX_WORD_MAX_LENGTH-1)
        {
            if (string_equal_nocase(STRING_ARGS(id), STRING_CONST("name")) ||
                string_equal_nocase(STRING_ARGS(id), STRING_CONST("title")))
            {
                search_database_index_text(db, doc, STRING_ARGS(value), false);
            }
            else
                search_database_index_property(db, doc, STRING_ARGS(id), STRING_ARGS(value), value.length < 12);
        }
    }
}

FOUNDATION_STATIC void search_index_exchange_symbols(const json_object_t& data, const char* market, size_t market_length, bool* stop_indexing)
{
    MEMORY_TRACKER(HASH_SEARCH);

    search_database_t* db = _search->db;

    for(auto e : data)
    {        
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
        if (string_is_null(isin) || string_equal_nocase(STRING_ARGS(type), STRING_CONST("FUND")))
            continue;

        char symbol_buffer[SEARCH_INDEX_WORD_MAX_LENGTH];
        string_t symbol = string_format(STRING_CONST_BUFFER(symbol_buffer), STRING_CONST("%.*s.%.*s"), STRING_FORMAT(code), to_int(market_length), market);

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
            if (thread_try_wait(data.resolved_from_cache || time_elapsed(st) > 1.0 ? 0 : 1000))
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
    }    

    return 0;
}

FOUNDATION_STATIC void search_execute_query(search_database_t* db, const char* search_text, size_t search_text_length, search_result_entry_t*& entries)
{
    array_clear(entries);
    if (search_text == nullptr || search_text_length == 0)
        return;

    search_query_handle_t query = search_database_query(db, search_text, search_text_length);
    if (search_database_query_is_completed(db, query))
    {
        const search_result_t* results = search_database_query_results(db, query);

        // TODO: Sort results?

        foreach(r, results)
        {
            search_result_entry_t entry;
            entry.db = db; 
            entry.doc = r->id;
            array_push_memcpy(entries, &entry);
        }

        if (!search_database_query_dispose(db, query))
        {
            log_warnf(HASH_SEARCH, WARNING_RESOURCE, STRING_CONST("Failed to dispose query"));
        }
    }
}

FOUNDATION_STATIC bool search_quick(void* user_data)
{
    search_window_t* sw = (search_window_t*)user_data;
    FOUNDATION_ASSERT(sw && sw->db);

    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - imgui_get_font_ui_scale(88.0f));
    if (ImGui::InputTextWithHint("##SearchQuery", "Search stocks...", STRING_CONST_CAPACITY(sw->query)))
    {
        sw->query_tick = time_current();
        search_execute_query(sw->db, sw->query, string_length(sw->query), sw->results);
        sw->query_tick = time_diff(sw->query_tick, time_current());
    }

    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {

    }
    ImGui::EndGroup();

    table_render(sw->table, sw->results, 0, -ImGui::GetFontSize() - 8.0f);

    if (sw->query_tick > 0 )
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
        ImGui::Text("Search found %u result(s) and took %.3lg %s", array_size(sw->results), elapsed_time, time_unit);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(" Documents: %u \n Indexes: %u ", search_database_document_count(sw->db), search_database_index_count(sw->db));
        }
    }

    return true;
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
        return SYMBOL_CONST(s->code);

    string_const_t symbol = search_database_document_name(entry->db, entry->doc);
    FOUNDATION_ASSERT(!string_is_null(symbol));
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

FOUNDATION_STATIC table_t* search_create_table()
{
    table_t* table = table_allocate("QuickSearch##2");
    table->flags |= TABLE_HIGHLIGHT_HOVERED_ROW;

    table_add_column(table, search_table_column_symbol, "Symbol", COLUMN_FORMAT_TEXT, COLUMN_SORTABLE)
        .set_width(imgui_get_font_ui_scale(100.0f))
        .set_style_formatter(search_table_column_code_color);

    table_add_column(table, search_table_column_name, ICON_MD_BUSINESS " Name", COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_STRETCH)
        .set_style_formatter(search_table_column_code_color)
        .set_tooltip_callback(search_table_column_description_tooltip);
        
    table_add_column(table, search_table_column_country, ICON_MD_FLAG " Country", COLUMN_FORMAT_SYMBOL, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE)
        .set_width(imgui_get_font_ui_scale(100.0f));

    table_add_column(table, search_table_column_exchange, ICON_MD_LOCATION_CITY "||" ICON_MD_LOCATION_CITY " Exchange", COLUMN_FORMAT_SYMBOL, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE | COLUMN_MIDDLE_ALIGN)
        .set_width(imgui_get_font_ui_scale(100.0f));

    table_add_column(table, search_table_column_currency, ICON_MD_FLAG "||" ICON_MD_FLAG " Currency", COLUMN_FORMAT_SYMBOL, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE | COLUMN_MIDDLE_ALIGN)
        .set_width(imgui_get_font_ui_scale(100.0f));

    table_add_column(table, search_table_column_type, ICON_MD_INVENTORY " Type", COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE)
        .set_width(imgui_get_font_ui_scale(100.0f));

    table_add_column(table, search_table_column_isin, ICON_MD_FINGERPRINT " ISIN     ", COLUMN_FORMAT_SYMBOL, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE | COLUMN_MIDDLE_ALIGN)
        .set_width(imgui_get_font_ui_scale(100.0f));

    table_add_column(table, search_table_column_change_p, " Day %||" ICON_MD_PRICE_CHANGE " Day % ", COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE)
        .set_width(imgui_get_font_ui_scale(100.0f));

    table_add_column(table, search_table_column_change_week, "  1W " ICON_MD_CALENDAR_VIEW_WEEK "||" ICON_MD_CALENDAR_VIEW_WEEK " % since 1 week", COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ROUND_NUMBER)
        .set_width(imgui_get_font_ui_scale(100.0f));
    
    table_add_column(table, search_table_column_change_month, "  1M " ICON_MD_CALENDAR_VIEW_MONTH "||" ICON_MD_CALENDAR_VIEW_MONTH " % since 1 month", COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ROUND_NUMBER)
        .set_width(imgui_get_font_ui_scale(100.0f))
        .set_style_formatter([](auto _1, auto _2, auto _3, auto& _4) { search_table_column_change_p_formatter(_1, _2, _3, _4, 3.0); });

    table_add_column(table, search_table_column_change_year, "1Y " ICON_MD_CALENDAR_MONTH "||" ICON_MD_CALENDAR_MONTH " % since 1 year", COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT | COLUMN_ROUND_NUMBER)
        .set_width(imgui_get_font_ui_scale(100.0f))
        .set_style_formatter([](auto _1, auto _2, auto _3, auto& _4) { search_table_column_change_p_formatter(_1, _2, _3, _4, 10.0); });
    
    table_add_column(table, search_table_column_change_max, "MAX %||" ICON_MD_CALENDAR_MONTH " % since creation", COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT |  COLUMN_ROUND_NUMBER)
        .set_width(imgui_get_font_ui_scale(100.0f))
        .set_style_formatter([](auto _1, auto _2, auto _3, auto& _4) { search_table_column_change_p_formatter(_1, _2, _3, _4, 25.0); });

    table_add_column(table, search_table_column_return_rate, " R. " ICON_MD_ASSIGNMENT_RETURN "||" ICON_MD_ASSIGNMENT_RETURN " Return Rate (Yield)", COLUMN_FORMAT_PERCENTAGE, COLUMN_HIDE_DEFAULT | COLUMN_ZERO_USE_DASH)
        .set_width(imgui_get_font_ui_scale(100.0f))
        .set_style_formatter(search_table_column_dividends_formatter);
    
    table_add_column(table, search_table_column_price, "    Price " ICON_MD_MONETIZATION_ON "||" ICON_MD_MONETIZATION_ON " Market Price", COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_SORTABLE | COLUMN_NOCLIP_CONTENT);

    return table;
}

FOUNDATION_STATIC search_window_t* search_window_allocate()
{
    search_window_t* search_window = MEM_NEW(HASH_SEARCH, search_window_t);
    search_window->db = _search->db;
    search_window->table = search_create_table();
    return search_window;
}

FOUNDATION_STATIC void search_window_deallocate(void* window)
{
    search_window_t* search_window = (search_window_t*)window;
    FOUNDATION_ASSERT(search_window);
    
    table_deallocate(search_window->table);
    array_deallocate(search_window->results);

    MEM_DELETE(search_window);
}

FOUNDATION_STATIC void search_open_quick_search()
{
    FOUNDATION_ASSERT(_search->db);
    
    app_open_dialog("Search", search_quick, 0, 0, true, search_window_deallocate, search_window_allocate());
}

FOUNDATION_STATIC void search_menu()
{        
    if (!search_available())
        return;

    if (shortcut_executed(false, true, ImGuiKey_GraveAccent))
        search_open_quick_search();
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

    service_register_menu(HASH_SEARCH, search_menu);
}

FOUNDATION_STATIC void search_shutdown()
{   
    dispatcher_thread_stop(_search->indexing_thread);
    
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
    
    search_database_deallocate(_search->db);

    MEM_DELETE(_search);
}

DEFINE_SERVICE(SEARCH, search_initialize, search_shutdown, SERVICE_PRIORITY_MODULE);
