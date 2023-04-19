/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */

#define HASH_SYMBOLS static_hash_string("symbols", 7, 0x2550ceb198e6a738ULL)

#include "symbols.h"

#include "eod.h"
#include "stock.h"
#include "pattern.h"
#include "settings.h"
#include "logo.h"
#include "search.h"

#include <framework/common.h>
#include <framework/function.h>
#include <framework/session.h>
#include <framework/scoped_string.h>
#include <framework/imgui.h>
#include <framework/generics.h>
#include <framework/scoped_mutex.h>
#include <framework/table.h>
#include <framework/module.h>
#include <framework/tabs.h>
#include <framework/string.h>
#include <framework/localization.h>
#include <framework/system.h>
#include <framework/dispatcher.h>
#include <framework/array.h>
#include <framework/profiler.h>

#include <foundation/random.h>

struct market_report_t
{
    string_table_symbol_t market;
    symbol_t* symbols;
    table_t* table;

    hash_t hash{ 0 };
};

static market_report_t* _markets;
static mutex_t* _symbols_lock = nullptr;
static atom32_t _loading_symbols_id = 0;

//
// # PRIVATE
//

FOUNDATION_STATIC  double load_number_field_value(const json_object_t& json, const json_token_t& symbol_token, const char* field_name)
{
    const json_token_t* field_value_token = json_find_token(json.buffer, json.tokens, symbol_token, field_name);
    if (field_value_token == nullptr)
        return DNAN;

    string_const_t field_value = json_token_value(json.buffer, field_value_token);
    if (string_is_null(field_value))
        return DNAN;
    return string_to_real(STRING_ARGS(field_value));
}

FOUNDATION_STATIC  string_table_symbol_t load_symbol_field_value(const json_object_t& json, const json_token_t& symbol_token, const char* field_name)
{
    const json_token_t* field_value_token = json_find_token(json.buffer, json.tokens, symbol_token, field_name);
    if (field_value_token != nullptr)
    {
        string_const_t field_value = json_token_value(json.buffer, field_value_token);
        if (field_value_token->type == JSON_PRIMITIVE && string_equal(STRING_CONST("null"), STRING_ARGS(field_value)))
            return STRING_TABLE_NULL_SYMBOL;
        return string_table_encode_unescape(json_token_value(json.buffer, field_value_token));
    }
    return STRING_TABLE_NULL_SYMBOL;
}

FOUNDATION_STATIC void symbols_load(int current_symbols_load_id, symbol_t** out_symbols, const json_object_t& data, const char* market, bool filter_null_isin = true)
{
    string_const_t market_cstr = string_const(market, string_length(market));
    if (const auto& lock = scoped_mutex_t(_symbols_lock))
    {
        const size_t reserve_count = max(array_size(*out_symbols) + data.root->value_length, 1U);
        array_reserve(*out_symbols, reserve_count);
    }

    for (int i = 1; i < data.token_count; ++i)
    {
        const json_token_t& t = data.tokens[i];
        if (t.type != JSON_OBJECT)
            continue;

        const json_token_t* code_token = json_find_token(data.buffer, data.tokens, t, "Code");
        if (code_token == nullptr)
            continue;

        string_const_t code_string = json_token_value(data.buffer, code_token);
        if (code_string.length == 0)
            continue;

        string_table_symbol_t isin = load_symbol_field_value(data, t, "Isin");
        if (isin == 0)
            isin = load_symbol_field_value(data, t, "ISIN");

        if (filter_null_isin && isin == 0)
            continue;

        string_const_t exchange_code = market_cstr;
        const json_token_t* exchange_token = json_find_token(data.buffer, data.tokens, t, "Exchange");
        if (exchange_token != nullptr)
            exchange_code = json_token_value(data.buffer, exchange_token);

        string_const_t exchange = market == nullptr ? exchange_code : string_const(market, string_length(market));
        string_const_t code = string_format_static(STRING_CONST("%.*s.%.*s"), STRING_FORMAT(code_string), STRING_FORMAT(exchange));

        symbol_t symbol;
        stock_initialize(STRING_ARGS(code), &symbol.stock);
        symbol.code = string_table_encode(STRING_ARGS(code));
        if (symbol.code == STRING_TABLE_NULL_SYMBOL)
            continue;

        symbol.name = load_symbol_field_value(data, t, "Name");
        symbol.country = load_symbol_field_value(data, t, "Country");
        symbol.exchange = load_symbol_field_value(data, t, "Exchange");
        symbol.currency = load_symbol_field_value(data, t, "Currency");
        symbol.type = load_symbol_field_value(data, t, "Type");
        symbol.price = load_number_field_value(data, t, "previousClose");
        symbol.isin = isin;

        if (const auto& lock = scoped_mutex_t(_symbols_lock))
        {
            symbol.viewed = pattern_find(STRING_ARGS(code)) >= 0;

            if (current_symbols_load_id != _loading_symbols_id)
                return;
            *out_symbols = array_push(*out_symbols, symbol);
        }
    }
}

FOUNDATION_STATIC void symbols_fetch(symbol_t** symbols, const char* market, bool filter_null_isin)
{
    if (*symbols == nullptr)
        array_reserve(*symbols, 1);

    int loading_symbols_id = ++_loading_symbols_id;
    if (!eod_fetch_async("exchange-symbol-list", market, FORMAT_JSON_CACHE,
        [loading_symbols_id, market, symbols, filter_null_isin](const json_object_t& data)
        {
            symbols_load(loading_symbols_id, symbols, data, market, filter_null_isin);
        }))
    {
        log_warnf(HASH_SYMBOLS, WARNING_RESOURCE, STRING_CONST("Failed to fetch %s symbols"), market);
    }
}

FOUNDATION_STATIC bool symbols_contains(const symbol_t* symbols, string_const_t code)
{
    for (const auto& s : generics::fixed_array(symbols))
    {
        if (string_equal(STRING_ARGS(code), STRING_ARGS(string_table_decode_const(s.code))))
            return true;
    }
    return false;
}

FOUNDATION_STATIC void symbols_read_search_results(int loading_symbols_id, const json_object_t& data, symbol_t** symbols, string_const_t search_filter)
{
    symbols_load(loading_symbols_id, symbols, data, nullptr);
}

FOUNDATION_STATIC void symbols_search(symbol_t** symbols, string_const_t search_filter)
{
    if (*symbols == nullptr)
        array_reserve(*symbols, 1);

    int loading_symbols_id = ++_loading_symbols_id;
    if (!eod_fetch_async("search", search_filter.str, FORMAT_JSON_CACHE, "limit", "50",
        [loading_symbols_id, symbols, search_filter](const json_object_t& data)
        {
            symbols_read_search_results(loading_symbols_id, data, symbols, search_filter);
        }, 6 * 60 * 60ULL))
    {
        log_warnf(HASH_SYMBOLS, WARNING_RESOURCE, STRING_CONST("Failed to execute search"));
    }
}

FOUNDATION_STATIC cell_t symbol_get_code(void* element, const column_t* column)
{
    symbol_t* symbol = (symbol_t*)element;
    string_const_t code = string_table_decode_const(symbol->code);

    #if BUILD_APPLICATION
    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        const ImRect& cell_rect = table_current_cell_rect();
        logo_render_banner(STRING_ARGS(code), cell_rect, nullptr);
    }
    #endif

    return code;
}

FOUNDATION_STATIC cell_t symbol_get_name(void* element, const column_t* column)
{
    symbol_t* symbol = (symbol_t*)element;
    if (!symbol->name)
        symbol->name = symbol->stock->name;
    return cell_t(string_table_decode(symbol->name));
}

FOUNDATION_STATIC cell_t symbol_get_country(void* element, const column_t* column)
{
    symbol_t* symbol = (symbol_t*)element;
    if (!symbol->country)
        symbol->country = symbol->stock->country;
    return cell_t(string_table_decode(symbol->country));
}

FOUNDATION_STATIC cell_t symbol_get_exchange(void* element, const column_t* column)
{
    symbol_t* symbol = (symbol_t*)element;
    if (!symbol->exchange)
        symbol->exchange = symbol->stock->exchange;
    return cell_t(string_table_decode(symbol->exchange));
}

FOUNDATION_STATIC cell_t symbol_get_currency(void* element, const column_t* column)
{
    symbol_t* symbol = (symbol_t*)element;
    if (!symbol->currency)
        symbol->currency = symbol->stock->currency;
    return cell_t(string_table_decode(symbol->currency));
}

FOUNDATION_STATIC cell_t symbol_get_isin(void* element, const column_t* column)
{
    symbol_t* symbol = (symbol_t*)element;
    return cell_t(string_table_decode(symbol->isin));
}

FOUNDATION_STATIC cell_t symbol_get_type(void* element, const column_t* column)
{
    symbol_t* symbol = (symbol_t*)element;
    if (!symbol->type)
        symbol->type = symbol->stock->type;
    return cell_t(string_table_decode(symbol->type));
}

FOUNDATION_STATIC double symbol_get_change(void* element, const column_t* column, int rel_days, bool take_last = false)
{
    symbol_t* symbol = (symbol_t*)element;
    const stock_t* stock_data = symbol->stock;
    if (stock_data == nullptr)
        return NAN;
    const day_result_t* ed = rel_days == 0 ? &(stock_data->current) : stock_get_EOD(stock_data, rel_days, take_last);
    if (ed == nullptr)
        return NAN;
    if (rel_days == 0)
        return ed->change_p;
    return (stock_data->current.adjusted_close - ed->adjusted_close) / ed->adjusted_close * 100.0;
}

FOUNDATION_STATIC cell_t symbol_get_change_cell(void* element, const column_t* column, int rel_days, bool take_last = false)
{
    double diff = symbol_get_change(element, column, rel_days, take_last);
    return cell_t(diff, COLUMN_FORMAT_PERCENTAGE);
}

FOUNDATION_STATIC cell_t symbol_get_day_change(void* element, const column_t* column)
{
    return symbol_get_change_cell(element, column, 0);
}

FOUNDATION_STATIC cell_t symbol_get_week_change(void* element, const column_t* column)
{
    return symbol_get_change_cell(element, column, -7);
}

FOUNDATION_STATIC cell_t symbol_get_month_change(void* element, const column_t* column)
{
    return symbol_get_change_cell(element, column, -30);
}

FOUNDATION_STATIC cell_t symbol_get_year_change(void* element, const column_t* column)
{
    return symbol_get_change_cell(element, column, -365);
}

FOUNDATION_STATIC cell_t symbol_get_max_change(void* element, const column_t* column)
{
    return symbol_get_change_cell(element, column, -365 * 30, true);
}

FOUNDATION_STATIC cell_t symbol_get_dividends_yield(void* element, const column_t* column)
{
    symbol_t* symbol = (symbol_t*)element;

    const stock_t* s = symbol->stock;
    if (s == nullptr)
        return nullptr;

    if (math_real_is_nan(s->dividends_yield))
    {
        if (!s->is_resolving(FetchLevel::FUNDAMENTALS, 10.0))
            stock_update(symbol->stock, FetchLevel::FUNDAMENTALS);
    }
    return s->dividends_yield.fetch() * 100.0;
}

FOUNDATION_STATIC cell_t symbol_get_price(void* element, const column_t* column)
{
    symbol_t* symbol = (symbol_t*)element;
    return cell_t(symbol->price, column->format);
}

FOUNDATION_STATIC void symbol_description_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    symbol_t* symbol = (symbol_t*)element;
    const stock_t* stock_data = symbol->stock;
    if (stock_data == nullptr)
        return;
    string_table_symbol_t tooltip_symbol = stock_data->description.fetch();
    if (tooltip_symbol == 0)
        return;

    string_const_t tooltip = string_table_decode_const(tooltip_symbol);
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 800.0f);
    ImGui::Text("%.*s", STRING_FORMAT(tooltip));
    ImGui::PopTextWrapPos();
}

FOUNDATION_STATIC void symbol_dividends_formatter(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)
{
    symbol_t* symbol = (symbol_t*)element;
    const stock_t* s = symbol->stock;
    if (s == nullptr)
        return;

    if (s->dividends_yield.fetch() > SETTINGS.good_dividends_ratio)
    {
        style.types |= COLUMN_COLOR_TEXT;
        style.text_color = ImColor::HSV(117 / 360.0f, 0.68f, 0.90f); // hsv(117, 68%, 90%)
    }
}

FOUNDATION_STATIC void symbol_change_p_formatter(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style, double threshold)
{
    if (cell->number > threshold)
    {
        style.types |= COLUMN_COLOR_TEXT;
        style.text_color = ImColor::HSV(117 / 360.0f, 0.68f, 0.90f); // hsv(117, 68%, 90%)
    }
}

FOUNDATION_STATIC void symbol_code_color(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)
{
    symbol_t* symbol = (symbol_t*)element;
    if (symbol->viewed)
    {
        style.types |= COLUMN_COLOR_TEXT;
        style.text_color = ImColor::HSV(!symbol->viewed ? 0.4f : 0.6f, 0.3f, 0.9f);
    }
}

FOUNDATION_STATIC void symbol_code_selected(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    symbol_t* symbol = (symbol_t*)element;
    pattern_open(STRING_ARGS(string_table_decode_const(symbol->code)));
    symbol->viewed = true;
}

FOUNDATION_STATIC table_t* symbols_table_init(const char* name, function<void(string_const_t)> selector = nullptr)
{
    table_t* table = table_allocate(name, TABLE_HIGHLIGHT_HOVERED_ROW | TABLE_LOCALIZATION_CONTENT);

    table->update = [](table_element_ptr_t element)->bool
    {
        const fetch_level_t REQUIRED_FETCH_LEVEL = FetchLevel::REALTIME | FetchLevel::EOD;
        symbol_t* symbol = (symbol_t*)element;
        string_const_t code = string_table_decode_const(symbol->code);
        return stock_update(STRING_ARGS(code), symbol->stock, REQUIRED_FETCH_LEVEL);
    };

    table->search = [](table_element_ptr_const_t element, const char* search_filter, size_t search_filter_length)->bool
    {
        if (search_filter_length == 0)
            return true;

        const symbol_t* symbol = (const symbol_t*)element;
        string_const_t code = string_table_decode_const(symbol->code);
        if (string_contains_nocase(STRING_ARGS(code), search_filter, search_filter_length))
            return true;

        string_const_t name = string_table_decode_const(symbol->name);
        if (string_contains_nocase(STRING_ARGS(name), search_filter, search_filter_length))
            return true;

        string_const_t country = string_table_decode_const(symbol->country);
        if (string_contains_nocase(STRING_ARGS(country), search_filter, search_filter_length))
            return true;

        string_const_t type = string_table_decode_const(symbol->type);
        if (string_contains_nocase(STRING_ARGS(type), search_filter, search_filter_length))
            return true;

        return false;
    };

    table->context_menu = [selector](table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
    {
        const symbol_t* symbol = (const symbol_t*)element;
        if (symbol == nullptr)
            return ImGui::CloseCurrentPopup();

        ImGui::MoveCursor(8.0f, 2.0f);

        string_const_t code = string_table_decode_const(symbol->code);
        if (selector)
        {
            if (ImGui::MenuItem(tr("Select symbol")))
                selector(code);
        }
        else if (pattern_menu_item(STRING_ARGS(code)))
        {
            ((symbol_t*)symbol)->viewed = true;
        }

        ImGui::MoveCursor(0.0f, 2.0f);
    };

    if (selector)
    {
        table->selected = [selector](table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
        {
            const symbol_t* symbol = (const symbol_t*)element;
            if (symbol != nullptr)
                selector(string_table_decode_const(symbol->code));
        };
    }

    auto& symbol_column = table_add_column(table, STRING_CONST("Symbol"), symbol_get_code, 
        COLUMN_FORMAT_TEXT, COLUMN_FREEZE | COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING);

    if (!selector)
        symbol_column.set_selected_callback(symbol_code_selected);

    column_t& c_name = table_add_column(table, STRING_CONST(ICON_MD_BUSINESS " Name"), symbol_get_name, COLUMN_FORMAT_TEXT, COLUMN_DYNAMIC_VALUE | COLUMN_SORTABLE | (selector ? COLUMN_STRETCH : COLUMN_OPTIONS_NONE));
    c_name.set_style_formatter(symbol_code_color);
    c_name.tooltip = symbol_description_tooltip;

    table_add_column(table, STRING_CONST(ICON_MD_FLAG " Country"), symbol_get_country, COLUMN_FORMAT_TEXT, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE);
    table_add_column(table, STRING_CONST(ICON_MD_LOCATION_CITY "||" ICON_MD_LOCATION_CITY " Exchange"), symbol_get_exchange, COLUMN_FORMAT_TEXT, (selector ? COLUMN_OPTIONS_NONE : COLUMN_HIDE_DEFAULT) | COLUMN_SORTABLE | COLUMN_MIDDLE_ALIGN);
    table_add_column(table, STRING_CONST(ICON_MD_FLAG "||" ICON_MD_FLAG " Currency"), symbol_get_currency, COLUMN_FORMAT_TEXT, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE | COLUMN_MIDDLE_ALIGN);
    table_add_column(table, STRING_CONST(ICON_MD_INVENTORY " Type"), symbol_get_type, COLUMN_FORMAT_TEXT, COLUMN_SORTABLE);
    table_add_column(table, STRING_CONST(ICON_MD_FINGERPRINT " ISIN     "), symbol_get_isin, COLUMN_FORMAT_TEXT, COLUMN_HIDE_DEFAULT | COLUMN_SORTABLE | COLUMN_MIDDLE_ALIGN);

    if (!selector)
    {
        table_add_column(table, STRING_CONST(" Day %||" ICON_MD_PRICE_CHANGE " Day % "), symbol_get_day_change, COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_DYNAMIC_VALUE);
        table_add_column(table, STRING_CONST("  1W " ICON_MD_CALENDAR_VIEW_WEEK "||" ICON_MD_CALENDAR_VIEW_WEEK " % since 1 week"), symbol_get_week_change, COLUMN_FORMAT_PERCENTAGE, COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE);
        table_add_column(table, STRING_CONST("  1M " ICON_MD_CALENDAR_VIEW_MONTH "||" ICON_MD_CALENDAR_VIEW_MONTH " % since 1 month"),
            symbol_get_month_change, COLUMN_FORMAT_PERCENTAGE, COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | COLUMN_ROUND_NUMBER)
            .set_style_formatter(LCCCR(symbol_change_p_formatter(_1, _2, _3, _4, 3.0)));
        table_add_column(table, STRING_CONST("1Y " ICON_MD_CALENDAR_MONTH "||" ICON_MD_CALENDAR_MONTH " % since 1 year"),
            symbol_get_year_change, COLUMN_FORMAT_PERCENTAGE, COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | COLUMN_ROUND_NUMBER)
            .set_style_formatter(LCCCR(symbol_change_p_formatter(_1, _2, _3, _4, 10.0)));
        table_add_column(table, STRING_CONST("MAX %||" ICON_MD_CALENDAR_MONTH " % since creation"),
            symbol_get_max_change, COLUMN_FORMAT_PERCENTAGE, COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | COLUMN_ROUND_NUMBER)
            .set_style_formatter(LCCCR(symbol_change_p_formatter(_1, _2, _3, _4, 25.0)));

        table_add_column(table, STRING_CONST(" R. " ICON_MD_ASSIGNMENT_RETURN "||" ICON_MD_ASSIGNMENT_RETURN " Return Rate (Yield)"),
            symbol_get_dividends_yield, COLUMN_FORMAT_PERCENTAGE, COLUMN_HIDE_DEFAULT | COLUMN_DYNAMIC_VALUE | COLUMN_ZERO_USE_DASH)
            .set_style_formatter(symbol_dividends_formatter);
    }
    else
    {
        table_add_column(table, STRING_CONST("    Price " ICON_MD_MONETIZATION_ON "||" ICON_MD_MONETIZATION_ON " Market Price"), symbol_get_price, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_NOCLIP_CONTENT);
    }

    return table;
}

FOUNDATION_STATIC void symbols_market_deallocate(market_report_t* m)
{
    table_deallocate(m->table);
    m->table = nullptr;
    array_deallocate(m->symbols);
}

FOUNDATION_STATIC market_report_t* symbols_get_or_create_market(const char* market, size_t market_length)
{
    string_table_symbol_t market_symbol = string_table_encode(market, market_length);

    for (size_t i = 0; i < array_size(_markets); ++i)
    {
        market_report_t* const mkr = &_markets[i];
        if (mkr->market == market_symbol)
            return mkr;
    }

    _markets = array_push(_markets, market_report_t{});
    market_report_t* market_report = &_markets[array_size(_markets) - 1];
    market_report->market = market_symbol;
    return market_report;
}

FOUNDATION_STATIC void symbols_render_search(string_const_t search_filter, const function<void(string_const_t)>& selector = nullptr)
{
    market_report_t* market_report = symbols_get_or_create_market(STRING_CONST("search"));
    if (market_report == nullptr)
        return;

    hash_t search_hash = string_hash(STRING_ARGS(search_filter));
    if (selector)
        search_hash += 1;
    if (market_report->symbols == nullptr || market_report->hash != search_hash)
    {
        if (auto lock = scoped_mutex_t(_symbols_lock))
            array_clear(market_report->symbols);

        symbols_search(&market_report->symbols, search_filter);

        if (market_report->table && ((selector && !market_report->table->selected)
            || (!selector && market_report->table->selected)))
        {
            table_deallocate(market_report->table);
            market_report->table = nullptr;
        }

        if (market_report->symbols && market_report->table == nullptr)
            market_report->table = symbols_table_init("Search", selector);

        market_report->hash = search_hash;
    }

    size_t symbol_count = array_size(market_report->symbols);
    if (symbol_count > 0)
    {
        if (auto lock = scoped_mutex_t(_symbols_lock))
            table_render(market_report->table, market_report->symbols, (int)symbol_count, sizeof(symbol_t), 0.0f, 0.0f);
    }
    else
    {
        ImGui::TextWrapped(tr("No search results for %.*s\nYou can still add the search term as a title by pressing Add."), STRING_FORMAT(search_filter));
    }
}

//
// # PUBLIC API
//

void symbols_render(const char* market, bool filter_null_isin /*= true*/)
{
    market_report_t* market_report = symbols_get_or_create_market(STRING_LENGTH(market));
    if (market_report == nullptr)
        return;

    if (market_report->symbols == nullptr)
    {
        symbols_fetch(&market_report->symbols, market, filter_null_isin);

        if (market_report->symbols && market_report->table == nullptr)
            market_report->table = symbols_table_init(market);
    }

    size_t symbol_count = array_size(market_report->symbols);
    if (symbol_count > 0)
    {
        if (auto lock = scoped_mutex_t(_symbols_lock))
        {
            market_report->table->search_filter = string_to_const(SETTINGS.search_filter);
            table_render(market_report->table, market_report->symbols, (int)symbol_count, sizeof(symbol_t), 0.0f, 0.0f);
        }
    }
    else
    {
        ImGui::TrText("No results for %s", market);
    }
}

void symbols_render_search(const function<void(string_const_t)>& selector /*= nullptr*/)
{
    ImGui::InputTextEx("##SearchField", "Search...", STRING_BUFFER(SETTINGS.search_terms),
        ImVec2(selector ? -100.0f : 300.0f, 0), ImGuiInputTextFlags_AutoSelectAll, 0, 0);

    string_const_t search_filter{ SETTINGS.search_terms, string_length(SETTINGS.search_terms) };
    if (selector)
    {
        ImGui::SameLine();
        if (ImGui::Button(tr("Add")))
            selector(search_filter);
    }

    if (SETTINGS.search_terms[0] != '\0')
        symbols_render_search(search_filter, selector);
    else
        ImGui::TrTextUnformatted("No search query");
}

FOUNDATION_STATIC bool symbols_fetch_market_symbols(const char* exchange, size_t exchange_length, string_t*& symbols)
{
    return eod_fetch("exchange-symbol-list", exchange, FORMAT_JSON_CACHE, [&symbols](const json_object_t& res)
    {
        for (auto e : res)
        {
            string_const_t code = e["Code"].as_string();
            string_const_t exchange = e["Exchange"].as_string();
            
            string_t ticker = string_allocate_format(STRING_CONST("%.*s.%.*s"), STRING_FORMAT(code), STRING_FORMAT(exchange));
            array_push(symbols, ticker);
        }
    });
}

FOUNDATION_STATIC void symbols_open_random_stock_pattern()
{
    FOUNDATION_ASSERT_MSG(!thread_is_main(), "Function is written to run in another thread");

    //TIME_TRACKER("symbols_open_random_stock_pattern");

    string_t* symbols = nullptr;
    const string_t* exchanges = search_stock_exchanges();
    for (unsigned i = 0, end = array_size(exchanges); i < end; ++i)
    {
        const string_t& exchange = exchanges[i];
        if (!symbols_fetch_market_symbols(STRING_ARGS(exchange), symbols))
        {
            log_warnf(HASH_SYMBOLS, WARNING_RESOURCE, STRING_CONST("Failed to fetch %.*s symbols"), STRING_ARGS(exchange));
            break;
        }
    }

    // Select a random symbol from the list.
    const unsigned random_symbol_index = random32_range(0, array_size(symbols));
    string_t random_symbol = string_clone(STRING_ARGS(symbols[random_symbol_index]));
    dispatch([random_symbol]()
    {
        pattern_open_window(STRING_ARGS(random_symbol));
        string_deallocate(random_symbol.str);
    });
    string_array_deallocate(symbols);
}

FOUNDATION_STATIC void symbols_render_menus()
{
    if (!ImGui::BeginMenuBar())
        return;
    
    if (ImGui::BeginMenu(tr("Symbols")))
    {
        ImGui::MenuItem(tr("Indexes"), nullptr, &SETTINGS.show_symbols_INDX);
        if (ImGui::MenuItem("La Presse", nullptr, nullptr, true))
            system_execute_command("https://www.google.com/search?q=bourse+site:lapresse.ca&tbas=0&source=lnt&tbs=qdr:w&sa=X&biw=1920&bih=902&dpr=2");

        ImGui::Separator();
        #if BUILD_DEVELOPMENT
        if (ImGui::MenuItem(tr("IPOs"), nullptr, nullptr, true))
            system_execute_command(eod_build_url("calendar", "ipos", FORMAT_JSON).str);
        #endif
        ImGui::MenuItem(tr("TO Symbols"), nullptr, &SETTINGS.show_symbols_TO);
        ImGui::MenuItem(tr("CVE Symbols"), nullptr, &SETTINGS.show_symbols_CVE);
        ImGui::MenuItem(tr("NEO Symbols"), nullptr, &SETTINGS.show_symbols_NEO);
        ImGui::MenuItem(tr("US Symbols"), nullptr, &SETTINGS.show_symbols_US);      

        ImGui::Separator();
        if (ImGui::TrMenuItem("Random"))
            dispatch_fire(symbols_open_random_stock_pattern);

        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

FOUNDATION_STATIC void symbols_render_tabs()
{
    static const ImVec4 TAB_COLOR_SYMBOLS(0.6f, 0.2f, 0.5f, 1.0f);
    
    tab_set_color(TAB_COLOR_SYMBOLS);
    if (SETTINGS.show_symbols_TO) tab_draw(ICON_MD_CURRENCY_EXCHANGE " Symbols (TO)", &SETTINGS.show_symbols_TO, L0(symbols_render("TO")));
    if (SETTINGS.show_symbols_CVE) tab_draw(ICON_MD_CURRENCY_EXCHANGE " Symbols (CVE)", &SETTINGS.show_symbols_CVE, L0(symbols_render("V")));
    if (SETTINGS.show_symbols_NEO) tab_draw(ICON_MD_CURRENCY_EXCHANGE " Symbols (NEO)", &SETTINGS.show_symbols_NEO, L0(symbols_render("NEO")));
    if (SETTINGS.show_symbols_US) tab_draw(ICON_MD_CURRENCY_EXCHANGE " Symbols (US)", &SETTINGS.show_symbols_US, L0(symbols_render("US")));
    if (SETTINGS.show_symbols_INDX) tab_draw(ICON_MD_TRENDING_UP " Indexes", &SETTINGS.show_symbols_INDX, L0(symbols_render("INDX", false)));

    tab_draw(tr(ICON_MD_MANAGE_SEARCH " Search ##Search"), nullptr, ImGuiTabItemFlags_Trailing, []() { symbols_render_search(nullptr); }, nullptr);
}

//
// # SYSTEM
//

FOUNDATION_STATIC void symbols_initialize()
{
    _symbols_lock = mutex_allocate(STRING_CONST("Symbols"));
    array_reserve(_markets, 1);

    module_register_tabs(HASH_SYMBOLS, symbols_render_tabs);
    module_register_menu(HASH_SYMBOLS, symbols_render_menus);
}

FOUNDATION_STATIC void symbols_shutdown()
{
    for (size_t i = 0; i < array_size(_markets); ++i)
    {
        market_report_t& m = _markets[i];
        symbols_market_deallocate(&m);
    }
    array_deallocate(_markets);
    mutex_deallocate(_symbols_lock);
    _symbols_lock = nullptr;
}

DEFINE_MODULE(SYMBOLS, symbols_initialize, symbols_shutdown, MODULE_PRIORITY_MODULE);
