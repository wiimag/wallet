/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */
 
#include "stock.h"

#include "eod.h"
#include "events.h"
#include "settings.h"

#include <framework/query.h>
#include <framework/shared_mutex.h>
#include <framework/module.h>
#include <framework/profiler.h>
#include <framework/dispatcher.h>
#include <framework/math.h>
#include <framework/string.h>
#include <framework/array.h>

#include <foundation/path.h>
#include <foundation/hashtable.h>

#define HASH_STOCK static_hash_string("stock", 5, 0x1a0dd7af24ebee7aLL)

struct technical_descriptor_t
{
    uint8_t field_count;
    const char* field_names[4];
    uint8_t field_offsets[4];
};

static size_t _db_capacity;
static shared_mutex _db_lock;
static day_result_t** _trashed_history = nullptr;
static stock_t* _db_stocks = nullptr;
static hashtable64_t* _db_hashes = nullptr;
static hashtable64_t* _exchange_rates = nullptr;

FOUNDATION_STATIC void stock_grow_db()
{
    hashtable64_t* old_table = _db_hashes;
    _db_capacity *= size_t(2);
    hashtable64_t* new_hash_table = hashtable64_allocate(_db_capacity);
    for (int i = 1, end = array_size(_db_stocks); i < end; ++i)
        hashtable64_set(new_hash_table, _db_stocks[i].id, i);

    _db_hashes = new_hash_table;

    for (size_t i = 0; i < array_size(_trashed_history); ++i)
        array_deallocate(_trashed_history[i]);
    array_clear(_trashed_history);

    hashtable64_deallocate(old_table);
}

template <size_t field_length>
FOUNDATION_STATIC bool stock_fetch_earnings_trend(stock_index_t stock_index, const char(&field)[field_length], double& value)
{
    if (_db_stocks == nullptr || stock_index >= array_size(_db_stocks))
        return false;

    SHARED_READ_LOCK(_db_lock);
    const stock_t* s = &_db_stocks[stock_index];
    if (s == nullptr || !s->has_resolve(FetchLevel::FUNDAMENTALS))
        return false;

    value = NAN;
        
    const char* ticker = string_table_decode(s->code);
    time_t since_last_year = time_add_days(time_now(), -465);
    string_const_t date_str = string_from_date(since_last_year);
    const char* url = eod_build_url("calendar", "earnings", FORMAT_JSON, "symbols", ticker, "from", date_str.str).str;
    return query_execute_async_json(url, FORMAT_JSON_CACHE, [stock_index, field](const json_object_t& json)
    {
        if (json.root == nullptr)
            return;

        double value_total = 0;
        double value_count = 0;
        auto earnings = json["earnings"];
        for (auto e : earnings)
        {
            const double v = e[field].as_number();
            if (math_real_is_finite(v))
            {
                value_total += v;
                value_count++;
            }
        }

        const double value_avg = value_count > 0 ? value_total / value_count : 0;
        
        SHARED_READ_LOCK(_db_lock);
        stock_t* s = &_db_stocks[stock_index];

        if (string_equal(field, field_length-1, STRING_CONST("actual")))
            s->earning_trend_actual = value_avg;
        else if (string_equal(field, field_length-1, STRING_CONST("estimate")))
            s->earning_trend_estimate = value_avg;
        else if (string_equal(field, field_length-1, STRING_CONST("difference")))
            s->earning_trend_difference = value_avg;
        else if (string_equal(field, field_length-1, STRING_CONST("percent")))
            s->earning_trend_percent = value_avg;
    }, 15 * 24 * 3600ULL);
}

FOUNDATION_STATIC bool stock_fetch_short_name(stock_index_t stock_index, string_table_symbol_t& value)
{
    if (_db_stocks == nullptr || stock_index >= array_size(_db_stocks))
        return false;

    SHARED_READ_LOCK(_db_lock);
    const stock_t* s = &_db_stocks[stock_index];
    
    if (!s->has_resolve(FetchLevel::FUNDAMENTALS))
        return false;

    string_const_t name = SYMBOL_CONST(s->name);

    static thread_local char short_name_buffer[64];
    string_t short_name = string_copy(short_name_buffer, sizeof(short_name_buffer), name.str, name.length);

    short_name = string_replace(short_name.str, short_name.length, sizeof(short_name_buffer), STRING_CONST("Inc"), nullptr, 0, true);
    short_name = string_replace(short_name.str, short_name.length, sizeof(short_name_buffer), STRING_CONST("Systems"), nullptr, 0, true);
    short_name = string_replace(short_name.str, short_name.length, sizeof(short_name_buffer), STRING_CONST("Technologies"), nullptr, 0, true);
    short_name = string_replace(short_name.str, short_name.length, sizeof(short_name_buffer), STRING_CONST("."), nullptr, 0, true);
    value = string_table_encode(string_trim(string_to_const(short_name)));
    return true;
}

FOUNDATION_STATIC bool stock_fetch_description(stock_index_t stock_index, string_table_symbol_t& value)
{
    if (_db_stocks == nullptr || stock_index >= array_size(_db_stocks))
        return false;	

    SHARED_READ_LOCK(_db_lock);
    const stock_t* s = &_db_stocks[stock_index];
    const char* ticker = string_table_decode(s->code);
    return eod_fetch_async("fundamentals", ticker, FORMAT_JSON_CACHE, "filter", "General::Description", [stock_index](const json_object_t& json)
    {
        if (json.root == nullptr)
            return;

        SHARED_READ_LOCK(_db_lock);
        stock_t* stock_data = &_db_stocks[stock_index];
        stock_data->description = string_table_encode_unescape(json_token_value(json.buffer, json.root));
    }, UINT64_MAX);
}

FOUNDATION_STATIC void stock_read_real_time_results(const json_object_t& json, stock_index_t index)
{
    day_result_t d{};
    stock_read_real_time_results(index, json, d);
}

bool stock_read_real_time_results(stock_index_t index, const json_object_t& json, day_result_t& d)
{
    string_const_t code = json["code"].as_string();
    string_const_t timestamp = json["timestamp"].as_string();
    if (string_equal(STRING_ARGS(timestamp), STRING_CONST("NA")))
    {
        log_warnf(HASH_STOCK, WARNING_INVALID_VALUE, STRING_CONST("Stock '%.*s' has no real time data"), STRING_FORMAT(code));
        SHARED_READ_LOCK(_db_lock);
        stock_t* entry = &_db_stocks[index];
        entry->fetch_errors++;
        entry->mark_resolved(FetchLevel::REALTIME, true);
        return false;
    }
    
    d.date = (time_t)json_read_number(json, STRING_CONST("timestamp"));
    d.gmtoffset = (uint8_t)json_read_number(json, STRING_CONST("gmtoffset"));
    d.open = json_read_number(json, STRING_CONST("open"));
    d.close = d.adjusted_close = json_read_number(json, STRING_CONST("close"));
    d.previous_close = json_read_number(json, STRING_CONST("previousClose"));
    d.low = json_read_number(json, STRING_CONST("low"));
    d.high = json_read_number(json, STRING_CONST("high"));
    d.change = json_read_number(json, STRING_CONST("change"));
    d.change_p = json_read_number(json, STRING_CONST("change_p"));
    d.change_p_high = (max(d.close, d.high) - min(d.open, d.low)) * 100.0 / math_ifnan(d.previous_close, d.close);
    d.volume = json_read_number(json, STRING_CONST("volume"));
    d.price_factor = NAN;

    {
        SHARED_READ_LOCK(_db_lock);
        stock_t* entry = &_db_stocks[index];

        if (entry->current.date != 0 && entry->current.date != d.date)
            array_push(entry->previous, entry->current);

        if (entry->current.date != d.date && !math_real_is_nan(d.close))
        {
            entry->current = d;
            #if 0
            string_const_t ticker = string_table_decode_const(entry->code);

            stock_realtime_t realtime;
            realtime.price = d.close;
            realtime.volume = d.volume;
            realtime.timestamp = d.date;
            string_copy(realtime.code, sizeof(realtime.code), ticker.str, ticker.length);

            dispatcher_post_event(EVENT_STOCK_REQUESTED, (void*)&realtime, sizeof(realtime), DISPATCHER_EVENT_OPTION_COPY_DATA);
            #endif
        }
        else
        {
            log_debugf(HASH_STOCK, STRING_CONST("Stock '%.*s' has no new real time data"), STRING_FORMAT(code));
        }

        entry->mark_resolved(FetchLevel::REALTIME);
    }
    
    return true;
}

FOUNDATION_STATIC void stock_read_fundamentals_results(const json_object_t& json, uint64_t index)
{	        
    SHARED_READ_LOCK(_db_lock);
    stock_t& entry = _db_stocks[index];

    const json_object_t& general = json["General"];
    entry.symbol = string_table_encode(general["Code"].as_string());
    entry.name = string_table_encode_unescape(general["Name"].as_string());
    entry.type = string_table_encode(general["Type"].as_string());
    entry.country = string_table_encode(general["CountryName"].as_string());
    entry.currency = string_table_encode(general["CurrencyCode"].as_string());
    entry.url = string_table_encode_unescape(general["WebURL"].as_string());
    entry.logo = string_table_encode_unescape(general["LogoURL"].as_string());
    entry.exchange = string_table_encode(general["Exchange"].as_string());
    entry.isin = string_table_encode(general["ISIN"].as_string());
    entry.description = string_table_encode_unescape(general["Description"].as_string());

    string_const_t sector = general["GicSector"].as_string();
    if (string_is_null(sector))
        sector = general["Sector"].as_string();
    entry.sector = string_table_encode_unescape(sector);
    
    string_const_t group = general["GicGroup"].as_string();
    entry.group = string_table_encode_unescape(group);
    
    string_const_t industry = general["GicIndustry"].as_string();
    if (string_is_null(industry))
        industry = general["Industry"].as_string();
    entry.industry = string_table_encode_unescape(industry);

    string_const_t subindustry = general["GicSubIndustry"].as_string();
    entry.activity = string_table_encode_unescape(subindustry);

    string_const_t category = general["Category"].as_string();
    if (string_is_null(category))
        category = general["HomeCategory"].as_string();
    entry.category = string_table_encode_unescape(category);

    const json_object_t& hightlights = json["Highlights"];
    entry.pe = hightlights["PERatio"].as_number();
    entry.peg = hightlights["PEGRatio"].as_number();
    entry.ws_target = hightlights["WallStreetTargetPrice"].as_number();
    entry.revenue_per_share_ttm = hightlights["RevenuePerShareTTM"].as_number();
    entry.profit_margin = hightlights["ProfitMargin"].as_number();

    // Get the dividend yielding
    entry.dividends_yield = hightlights["DividendYield"]
        .as_number(json["ETF_Data"]["Yield"].as_number(0) / 100.0);

    string_const_t updated_at_string = general["UpdatedAt"].as_string();
    string_try_convert_date(STRING_ARGS(updated_at_string), entry.updated_at);

    // This figure, diluted EPS, is calculated by dividing net income net of preferred dividends 
    // by a weighted average of total shares outstanding plus additional common shares that would 
    // have been outstanding if the dilutive common share would have been issued for the trailing 12 months.
    entry.diluted_eps_ttm = hightlights["DilutedEpsTTM"].as_number();

    // Get the stock market capitalization
    entry.market_cap = hightlights["MarketCapitalization"].as_number();

    const json_object_t& valuation = json["Valuation"];
    entry.trailing_pe = valuation["TrailingPE"].as_number();
    entry.forward_pe = valuation["ForwardPE"].as_number();

    entry.pe = math_ifnan(entry.pe, entry.peg);

    const json_object_t& SharesStats = json["SharesStats"];
    entry.shares_count = SharesStats["SharesFloat"].as_number();

    const json_object_t& Technicals = json["Technicals"];
    entry.low_52 = Technicals["52WeekLow"].as_number();
    entry.high_52 = Technicals["52WeekHigh"].as_number();
    entry.beta = Technicals["Beta"].as_number();
    entry.dma_50 = Technicals["50DayMA"].as_number();
    entry.dma_200 = Technicals["200DayMA"].as_number();
    entry.short_ratio = Technicals["ShortRatio"].as_number();
    entry.short_percent = Technicals["ShortPercent"].as_number();

    entry.mark_resolved(FetchLevel::FUNDAMENTALS);
}

FOUNDATION_STATIC void stock_read_technical_results(const json_object_t& json, stock_index_t index, FetchLevel level, const technical_descriptor_t& desc)
{
    SHARED_READ_LOCK(_db_lock);
    
    stock_t* s = &_db_stocks[index];
    day_result_t* history = s->history;
    int h = 0, h_end = array_size(history);
    bool applied_to_current = false;
    for (size_t i = 0; i < json.root->value_length; ++i)
    {
        const auto& e = json[i];
        const time_t date = string_to_date(STRING_ARGS(e["date"].as_string()));

        for (; h != h_end;)
        {
            day_result_t* ed = &history[h];
            if (time_date_equal(ed->date, date))
            {
                for (size_t i = 0; i < desc.field_count; i++)
                {
                    const double v = e[desc.field_names[i]].as_number();
                    *(double*)(((uint8_t*)ed) + desc.field_offsets[i]) = v;

                    if (!applied_to_current)
                        *(double*)(((uint8_t*)&s->current) + desc.field_offsets[i]) = v;
                }
                applied_to_current = true;
                break;
            }
            else if (ed->date < date)
                break;
            else
                h++;
        }
    }

    s->mark_resolved(level);
}

FOUNDATION_STATIC void stock_fetch_technical_results(
    fetch_level_t access_level, status_t& status, fetch_level_t fetch_levels, 
    const char* ticker, stock_index_t index, const char* fn_name, 
    const technical_descriptor_t& desc)
{
    stock_t* entry = &_db_stocks[index];

    if ((fetch_levels & access_level) && ((entry->fetch_level | entry->resolved_level) & access_level) == 0)
    {
        if (entry->has_resolve(FetchLevel::EOD))
        {
            if (eod_fetch_async("technical", ticker, FORMAT_JSON_CACHE, "order", "d", "function", fn_name,
                LC1(stock_read_technical_results(_1, index, access_level, desc)), 12ULL * 3600ULL))
            {
                entry->mark_fetched(access_level);
                status = STATUS_RESOLVING;
            }
            else
            {
                entry->fetch_errors++;
                log_warnf(HASH_STOCK, WARNING_RESOURCE, STRING_CONST("[%u] Failed to fetch technical results %d for %s"), entry->fetch_errors, access_level, ticker);
            }
        }
        else if (eod_availalble())
        {
            if (!entry->is_resolving(FetchLevel::EOD, 0))
            {
                log_warnf(HASH_STOCK, WARNING_RESOURCE, STRING_CONST("Missing EOD data to fetch technical results %d for %s"), access_level, ticker);

                // Request EOD, then mark access level to be resolved by EOD when finished
                auto eod_stock_handle_request = stock_request(ticker, string_length(ticker), FetchLevel::EOD);
                if (eod_stock_handle_request)
                {
                    status = STATUS_RESOLVING;
                    eod_stock_handle_request->mark_fetched(access_level);
                }
            }
            else if (entry->fetch_errors < 10)
            {
                log_warnf(HASH_STOCK, WARNING_RESOURCE, STRING_CONST("[%u] Still missing EOD data to fetch technical results %d for %s"),
                    entry->fetch_errors, access_level, ticker);

                entry->fetch_errors++;
                status = STATUS_RESOLVING;
                entry->mark_fetched(access_level);
            }
            else
            {
                entry->fetch_errors++;
                log_warnf(HASH_STOCK, WARNING_RESOURCE, STRING_CONST("Failed to fetch technical results for %s"), ticker);
            }
        }
    }
}

FOUNDATION_STATIC void stock_fetch_technical_results(
    fetch_level_t access_level, status_t& status, fetch_level_t fetch_levels,
    const char* ticker, stock_index_t index, const char* fn_name,
    const char* field_name, size_t offset)
{
    stock_fetch_technical_results(access_level, status, fetch_levels, ticker, index, fn_name, technical_descriptor_t{1, {field_name}, {(uint8_t)offset}});
}

FOUNDATION_STATIC void stock_read_eod_results(const json_object_t& json, stock_index_t index)
{
    MEMORY_TRACKER(HASH_STOCK);

    day_result_t* history = nullptr;
    array_reserve(history, json.root->value_length + 1);

    bool logged_skip_eod_data = false;
    double first_price_factor = DNAN;
    const int element_count = json.root->value_length;
    const json_token_t* e = &json.tokens[json.root->child];
    for (int i = 0; i < element_count; ++i)
    {
        json_object_t jday(json, e);
        string_const_t date_str = jday["date"].as_string();
        const double volume = jday["volume"].as_number();
        if (volume >= 1.0 || i < 7)
        {
            day_result_t d{};

            d.date = string_to_date(STRING_ARGS(date_str));
            d.gmtoffset = 0;

            d.volume = volume;
            d.open = jday["open"].as_number();
            d.close = jday["close"].as_number();
            d.low = jday["low"].as_number();
            d.high = jday["high"].as_number();
            d.adjusted_close = jday["adjusted_close"].as_number();

            d.price_factor = d.adjusted_close / d.close;
            if (math_real_is_nan(first_price_factor) && !math_real_is_nan(d.price_factor))
                first_price_factor = d.price_factor;            
            
            d.change = d.close - d.open;
            d.change_p = d.change * 100.0 / d.open;
            d.change_p_high = (max(d.close, d.high) - min(d.open, d.low)) * 100.0 / math_ifnan(d.previous_close, d.close);
                
            d.previous_close = NAN;
            if (e->sibling != 0)
            {
                json_object_t yday(json, &json.tokens[e->sibling]);
                d.previous_close = yday["adjusted_close"].as_number();
            }
            
            array_push_memcpy(history, &d);
        }
        else if (!logged_skip_eod_data)
        {
            SHARED_READ_LOCK(_db_lock);
            string_const_t ticker = SYMBOL_CONST(_db_stocks[index].code);
            log_debugf(HASH_STOCK, STRING_CONST("Skipping EOD result for %.*s on %.*s using %.*s"), 
                STRING_FORMAT(ticker), STRING_FORMAT(date_str), STRING_FORMAT(json.query));
            logged_skip_eod_data = true;
        }

        if (e->sibling == 0)
            break;
        e = &json.tokens[e->sibling];
    }

    {
        SHARED_READ_LOCK(_db_lock);
        stock_t& entry = _db_stocks[index];
        if (entry.history != nullptr)
            array_push(_trashed_history, entry.history);
        entry.history = history;
        entry.history_count = array_size(history);

        if (math_real_is_nan(entry.current.price_factor) && !math_real_is_nan(first_price_factor))
            entry.current.price_factor = first_price_factor;

        entry.mark_resolved(FetchLevel::EOD);

        // Check if we need to fetch any awaiting technical results 
        // since now we have EOD stock data
        const fetch_level_t cfetch_level = entry.fetch_level & TECHINICAL_CHARTS;
        if (cfetch_level != 0)
        {
            // Remove technical fetch levels so we make sure the request is not
            // reissued if the stock is already resolved
            entry.fetch_level &= ~TECHINICAL_CHARTS;

            string_table_symbol_t ccode = entry.code;
            dispatch([ccode, cfetch_level]()
            {
                string_const_t symbol = SYMBOL_CONST(ccode);
                stock_request(STRING_ARGS(symbol), cfetch_level);
            });
        }
    }
}

//
// # PUBLIC API
//

stock_handle_t stock_resolve(const char* _symbol, size_t symbol_length, fetch_level_t fetch_levels, double timeout /*= 5.0f*/)
{
    // Get title name from symbol
    string_const_t symbol = string_const(_symbol, symbol_length);
    stock_handle_t stock = stock_request(STRING_ARGS(symbol), fetch_levels);
    if (!stock)
        return {};

    // Wait for handle to resolve
    tick_t timeout_ticks = time_current();
    while (!stock->has_resolve(fetch_levels) )
    {
        dispatcher_wait_for_wakeup_main_thread(math_trunc(timeout * 100));

        if (time_elapsed(timeout_ticks) > timeout)
        {
            error_report(ERRORLEVEL_WARNING, ERROR_EXCEPTION);
            log_warnf(HASH_STOCK, WARNING_TIMEOUT, STRING_CONST("Stock resolve timed out for %.*s"), STRING_FORMAT(symbol));
            return {};
        }
    }

    if (stock->fetch_errors > 0)
    {
        error_report(ERRORLEVEL_WARNING, ERROR_EXCEPTION);
        log_warnf(HASH_STOCK, WARNING_RESOURCE, STRING_CONST("Stock resolve failed for %.*s"), STRING_FORMAT(symbol));
        return {};
    }

    return stock;
}

status_t stock_resolve(stock_handle_t& handle, fetch_level_t fetch_levels)
{
    MEMORY_TRACKER(HASH_STOCK);
    
    if (handle.id == 0)
        return STATUS_ERROR_INVALID_HANDLE;

    // Check if we have a slot index for that stock
    if (!_db_lock.shared_lock())
        return STATUS_ERROR_DB_ACCESS;

    stock_t* entry = nullptr;
    stock_index_t index = hashtable64_get(_db_hashes, handle.id);
    if (index != 0)
    {
        FOUNDATION_ASSERT(index > 0 && index < array_size(_db_stocks));

        handle.ptr = entry = &_db_stocks[index];
        FOUNDATION_ASSERT(entry);
        FOUNDATION_ASSERT(entry->id == handle.id);

        if (((entry->fetch_level | entry->resolved_level) & fetch_levels) == fetch_levels)
        {
            _db_lock.shared_unlock();
            return STATUS_OK;
        }

        if (entry->fetch_errors >= 20)
        {
            if (entry->fetch_errors == 20)
                log_errorf(HASH_STOCK, ERROR_EXCEPTION, STRING_CONST("Too many fetch failures %s"), string_table_decode(entry->code));
            _db_lock.shared_unlock();
            return STATUS_ERROR_INVALID_REQUEST;
        }
        _db_lock.shared_unlock();
    }
    else if (_db_lock.shared_unlock() && index == 0 && _db_lock.exclusive_lock())
    {
        // Create stock slot and trigger async resolution.
        if (array_size(_db_stocks) >= _db_capacity)
            stock_grow_db();

        // Create slot
        _db_stocks = array_push(_db_stocks, stock_t{});
        index = array_size(_db_stocks) - 1;
        FOUNDATION_ASSERT(index > 0);

        handle.ptr = entry = &_db_stocks[index];
        FOUNDATION_ASSERT(entry);

        // Initialize stock entries
        entry->id = handle.id;
        entry->code = handle.code;
        entry->earning_trend_actual.reset(LR1(stock_fetch_earnings_trend(index, "actual", _1)));
        entry->earning_trend_estimate.reset(LR1(stock_fetch_earnings_trend(index, "estimate", _1)));
        entry->earning_trend_difference.reset(LR1(stock_fetch_earnings_trend(index, "difference", _1)));
        entry->earning_trend_percent.reset(LR1(stock_fetch_earnings_trend(index, "percent", _1)));
        entry->description.reset(LR1(stock_fetch_description(index, _1)));
        entry->short_name.reset(LR1(stock_fetch_short_name(index, _1)));

        FOUNDATION_ASSERT(handle.id != 0);
        if (!hashtable64_set(_db_hashes, handle.id, index))
        {
            _db_lock.exclusive_unlock();
            return STATUS_ERROR_HASH_TABLE_NOT_LARGE_ENOUGH;
        }

        // Initialize a minimal set of data, the rest will be initialized asynchronously.
        entry->last_update_time = time_current();
        entry->fetch_level = FetchLevel::NONE;
        entry->resolved_level = FetchLevel::NONE;

        _db_lock.exclusive_unlock();
    }
    
    SHARED_READ_LOCK(_db_lock);
    // Fetch stock data
    char ticker[64] { 0 };
    string_const_t code_string = string_table_decode_const(handle.code);
    string_copy(STRING_BUFFER(ticker), STRING_ARGS(code_string));

    status_t status = STATUS_OK;
    if ((fetch_levels & FetchLevel::REALTIME) && ((entry->fetch_level | entry->resolved_level) & FetchLevel::REALTIME) == 0)
    {
        const uint64_t invalid_cache_query_after_seconds = main_is_running_tests() ? 30ULL : 5 * 60ULL;
        if (eod_fetch_async("real-time", ticker, FORMAT_JSON_CACHE, LC1(stock_read_real_time_results(_1, index)), invalid_cache_query_after_seconds))
        {
            entry->mark_fetched(FetchLevel::REALTIME);
            status = STATUS_RESOLVING;
        }
        else
        {
            entry->fetch_errors++;
            log_warnf(HASH_STOCK, WARNING_RESOURCE, STRING_CONST("[%u] Failed to fetch real-time results for %s"), entry->fetch_errors, ticker);
        }
    }
        
    if ((fetch_levels & FetchLevel::FUNDAMENTALS) && ((entry->fetch_level | entry->resolved_level) & FetchLevel::FUNDAMENTALS) == 0)
    {
        if (eod_fetch_async("fundamentals", ticker, FORMAT_JSON_CACHE, LC1(stock_read_fundamentals_results(_1, index)), 14 * 24ULL * 3600ULL))
        {
            entry->mark_fetched(FetchLevel::FUNDAMENTALS);
            status = STATUS_RESOLVING;
        }
        else
        {
            entry->fetch_errors++;
            log_warnf(HASH_STOCK, WARNING_RESOURCE, STRING_CONST("[%u] Failed to fetch fundamentals results for %s"), entry->fetch_errors, ticker);
        }
    }

    if ((fetch_levels & FetchLevel::EOD) && ((entry->fetch_level | entry->resolved_level) & FetchLevel::EOD) == 0)
    {
        if (eod_fetch_async("eod", ticker, FORMAT_JSON_CACHE, "order", "d", LC1(stock_read_eod_results(_1, index)), 12ULL * 3600ULL))
        {
            entry->mark_fetched(FetchLevel::EOD);
            status = STATUS_RESOLVING;
        }
        else
        {
            entry->fetch_errors++;
            log_warnf(HASH_STOCK, WARNING_RESOURCE, STRING_CONST("[%u] Failed to fetch EOD results for %s"), entry->fetch_errors, ticker);
        }
    }

    if ((fetch_levels & TECHINICAL_CHARTS) != 0)
    {
        stock_fetch_technical_results(FetchLevel::TECHNICAL_EMA, status, fetch_levels, ticker, index, "ema", "ema", offsetof(day_result_t, ema));
        stock_fetch_technical_results(FetchLevel::TECHNICAL_SMA, status, fetch_levels, ticker, index, "sma", "sma", offsetof(day_result_t, sma));
        stock_fetch_technical_results(FetchLevel::TECHNICAL_WMA, status, fetch_levels, ticker, index, "wma", "wma", offsetof(day_result_t, wma));
        stock_fetch_technical_results(FetchLevel::TECHNICAL_SAR, status, fetch_levels, ticker, index, "sar", "sar", offsetof(day_result_t, sar));
        stock_fetch_technical_results(FetchLevel::TECHNICAL_SLOPE, status, fetch_levels, ticker, index, "slope", "slope", offsetof(day_result_t, slope));
        stock_fetch_technical_results(FetchLevel::TECHNICAL_CCI, status, fetch_levels, ticker, index, "cci", "cci", offsetof(day_result_t, cci));
        stock_fetch_technical_results(FetchLevel::TECHNICAL_BBANDS, status, fetch_levels, ticker, index, "bbands",
            technical_descriptor_t{ 3, { "uband", "mband", "lband" }, { offsetof(day_result_t, uband), offsetof(day_result_t, mband), offsetof(day_result_t, lband) } });
    }

    return status;
}

stock_index_t stock_index(const char* symbol, size_t symbol_length)
{
    FOUNDATION_ASSERT(_db_hashes);
    
    const hash_t id = hash(symbol, symbol_length);
    return (stock_index_t)hashtable64_get(_db_hashes, id);
}

bool stock_request(const stock_handle_t& handle, const stock_t** out_stock)
{
    FOUNDATION_ASSERT(handle.id);
    FOUNDATION_ASSERT(out_stock);
    FOUNDATION_ASSERT(_db_hashes);

    *out_stock = nullptr;
    uint64_t index = hashtable64_get(_db_hashes, handle.id);
    if (index == 0)
        return false;

    FOUNDATION_ASSERT(_db_stocks);

    SHARED_READ_LOCK(_db_lock);
    FOUNDATION_ASSERT(index > 0 && index < array_size(_db_stocks));

    const stock_t& s = _db_stocks[index];
    FOUNDATION_ASSERT(s.id == handle.id);
    *out_stock = &s;
    return true;
}

stock_handle_t stock_request(const char* symbol, size_t symbol_length, fetch_level_t fetch_levels)
{
    stock_handle_t stock_handle;
    if (stock_initialize(symbol, symbol_length, &stock_handle) != STATUS_OK)
        return {};

    const status_t status = stock_resolve(stock_handle, fetch_levels);
    if (status <= STATUS_ERROR)
    {
        log_errorf(HASH_STOCK, ERROR_UNKNOWN_RESOURCE, STRING_CONST("Failed to resolve stock %.*s (%d)"), (int)symbol_length, symbol, status);
        return stock_handle;
    }

    return stock_handle;
}

double stock_exchange_rate(const char* from, size_t from_length, const char* to, size_t to_length, time_t at /*= 0*/)
{
    if (_exchange_rates == nullptr)
        _exchange_rates = hashtable64_allocate(16);

    if (string_equal(from, from_length, to, to_length))
        return 1.0;

    if (string_equal(STRING_CONST("NA"), from, from_length))
        return 1.0;

    char exchange_code[32];
    string_t exg = string_format(exchange_code, sizeof(exchange_code), STRING_CONST("%.*s%.*s.FOREX"), (int)from_length, from, (int)to_length, to);
    const hash_t exchange_hash = string_hash(STRING_ARGS(exg)) + at;

    double rate = 1;
    uint64_t xr = hashtable64_get(_exchange_rates, exchange_hash);
    if (xr != 0)
        rate = *(double*)&xr;
    else
    {
        if (at == 0)
        {
            eod_fetch("real-time", exchange_code, FORMAT_JSON_CACHE, [&rate](const json_object_t& json)
            {
                rate = json["close"].as_number(rate);
            }, 60 * 60ULL);
        }
        else
        {
            string_const_t from_date_str = string_from_date(at - time_one_day() * 5);
            if (!string_is_null(from_date_str))
            {
                string_const_t to_date_str = string_from_date(at);
                const char* eod_url = eod_build_url("eod", FORMAT_JSON_CACHE, "%s?from=%.*s&to=%.*s&order=d", exchange_code, STRING_FORMAT(from_date_str), STRING_FORMAT(to_date_str));
                query_execute_json(eod_url, FORMAT_JSON_CACHE, [eod_url, &rate](const auto& json)
                {
                    if (json.root == nullptr)
                        return;

                    if (json.root->value_length != 0)
                    {
                        const json_object_t& jc = json.get(0)["adjusted_close"];
                        rate = jc.as_number(rate);
                    }
                    else
                    {
                        log_warnf(HASH_STOCK, WARNING_SUSPICIOUS, STRING_CONST("Failed to get exchange rate with %s"), eod_url);
                    }
                }, UINT64_MAX);
            }
        }

        if (!hashtable64_set(_exchange_rates, exchange_hash, *(uint64_t*)&rate))
        {
            size_t new_size = hashtable64_size(_exchange_rates) * 2;
            hashtable64_deallocate(_exchange_rates);
            _exchange_rates = hashtable64_allocate(new_size);
        }
    }

    return rate;
}

const day_result_t* stock_get_EOD(const stock_t* stock_data, time_t day_time, bool take_last /*= false*/)
{
    if (!stock_data)
        return nullptr;

    const day_result_t* history = stock_data->history;
    const size_t history_count = stock_data->history_count;
    if (!history || history_count == 0)
        return nullptr;

    const time_t ONE_DAY = time_one_day();
    time_t day_trunc = (day_time / ONE_DAY);
    for (size_t i = 0, end = history_count; i < end; ++i)
    {
        const day_result_t* ed = &history[i];
        if ((ed->date / ONE_DAY) > day_trunc)
            continue;

        return ed;
    }

    if (take_last)
        return &history[history_count - 1];
    return nullptr;
}

const day_result_t* stock_get_EOD(const stock_t* stock_data, int rel_day, bool take_last /*= false*/)
{
    const time_t one_day = 24 * 60 * 60;
    time_t day_time;
    time(&day_time);
    day_time = day_time + (rel_day * one_day);
    return stock_get_EOD(stock_data, day_time, take_last);
}

status_t stock_initialize(const char* code, size_t code_length, stock_handle_t* stock_handle)
{
    if (code == nullptr || code_length <= 0 || stock_handle == nullptr)
        return STATUS_ERROR_NULL_REFERENCE;

    stock_handle->id = hash(code, code_length);
    stock_handle->code = string_table_encode(code, code_length);

    return STATUS_OK;
}

bool stock_update(stock_handle_t& handle, fetch_level_t fetch_level, double timeout /*= 5.0*/)
{
    stock_t* s = (stock_t*)(const stock_t*)handle;
    if (!s)
        return false;

    {
        SHARED_WRITE_LOCK(_db_lock);
        bool fully_resolved = (s->resolved_level & fetch_level) == fetch_level;
        if (fully_resolved)
            return true;

        fetch_level = (fetch_level & ~s->resolved_level);
        const deltatime_t since = time_elapsed(s->last_update_time);
        if (timeout > 0 && since > timeout)
        {
            s->fetch_errors = 0;
            s->fetch_level = s->fetch_level & ~fetch_level;

            log_warnf(HASH_STOCK, WARNING_PERFORMANCE, STRING_CONST("Refreshing stock data %s [%u,%u,%u] (%.4g > %.4g)"),
                string_table_decode(s->code), s->fetch_level, fetch_level, s->resolved_level, since, timeout);
        }
        else
            fetch_level = (fetch_level & ~s->fetch_level);

        if (s->fetch_errors >= 20)
            return false;

        if (fetch_level == FetchLevel::NONE)
            return true;
    }

    if (stock_resolve(handle, fetch_level) != STATUS_OK)
        s->last_update_time = time_current();
    return s->has_resolve(fetch_level);
}

bool stock_update(const char* code, size_t code_length, stock_handle_t& handle, fetch_level_t fetch_level, double timeout /*= 5.0*/)
{
    stock_t* s = (stock_t*)(const stock_t*)handle;
    if (s == nullptr)
    {
        handle = stock_request(code, code_length, fetch_level);
        return (handle->resolved_level & fetch_level) == fetch_level;
    }

    return stock_update(handle, fetch_level, timeout);
}

day_result_t stock_get_eod(const char* code, size_t code_length, time_t at)
{    
    const stock_t* s = stock_request(code, code_length, FetchLevel::EOD);
    if (s == nullptr)
        return {};

    while(!s->has_resolve(FetchLevel::EOD))
        dispatcher_wait_for_wakeup_main_thread();
    const day_result_t* ed = stock_get_EOD(s, at, true);
    if (ed == nullptr)
        return {};

    return *ed;
}

day_result_t stock_get_split(const char* code, size_t code_length, time_t at)
{
    if (math_abs(time_elapsed_days(at, time_now())) < 2)
        return stock_get_eod(code, code_length, at);

    // TODO: Cache these technical results and provide a quick access to them
    day_result_t d{};
    string_const_t ticker = { code, code_length };
    string_const_t date_str = string_from_date(at);
    const char* url = eod_build_url("technical", FORMAT_JSON_CACHE, "%.*s?order=d&function=splitadjusted&from=%.*s&to=%.*s", 
        STRING_FORMAT(ticker), STRING_FORMAT(date_str), STRING_FORMAT(date_str));
    query_execute_json(url, FORMAT_JSON_CACHE, [&d](const json_object_t& res) 
    {
        const auto dayresult = res.get(0);
        if (!dayresult.is_valid())
            return;
            
        string_const_t date_str = dayresult["date"].as_string();
        d.date = string_to_date(STRING_ARGS(date_str));
        d.open = dayresult["open"].as_number();
        d.close = d.adjusted_close = dayresult["close"].as_number();
        d.low = dayresult["low"].as_number();
        d.high = dayresult["high"].as_number();
        d.volume = dayresult["volume"].as_number();

        d.gmtoffset = 0;
        d.price_factor = 1.0;
        
        d.change = d.close - d.open;
        d.change_p = d.change * 100.0 / d.open;
        d.change_p_high = (max(d.close, d.high) - min(d.open, d.low)) * 100.0 / d.close;
        d.previous_close = NAN;
        
    }, 30 * 86400ULL);
    
    return d;
}

double stock_get_eod_price_factor(const char* code, size_t code_length, time_t at)
{
    return stock_get_eod(code, code_length, at).price_factor;
}

double stock_get_split_factor(const char* code, size_t code_length, time_t at)
{
    if (math_abs(time_elapsed_days(at, time_now())) <= 3)
        return 1.0;

    day_result_t eod = stock_get_eod(code, code_length, at);
    if ((math_abs(eod.adjusted_close - eod.close) / min(eod.close, eod.adjusted_close)) < 1.0)
        return 1.0;
        
    day_result_t split = stock_get_split(code, code_length, at);
    return math_ifzero(split.close / eod.close, 1.0);
}

double stock_get_split_adjusted_factor(const char* code, size_t code_length, time_t at)
{
    day_result_t eod = stock_get_eod(code, code_length, at);
    if (eod.close == eod.adjusted_close)
        return eod.price_factor;
        
    day_result_t split = stock_get_split(code, code_length, at);
    return math_ifzero(split.close / eod.adjusted_close, 1.0);
}

string_const_t stock_get_name(const char* code, size_t code_length)
{
    SHARED_READ_LOCK(_db_lock);
    stock_index_t index = stock_index(code, code_length);
    if (index == 0)
        return {};
        
    return SYMBOL_CONST(_db_stocks[index].name);
}

string_const_t stock_get_short_name(const char* code, size_t code_length)
{
    SHARED_READ_LOCK(_db_lock);
    stock_index_t index = stock_index(code, code_length);
    if (index == 0)
        return {};

    string_table_symbol_t symbol;
    if (!stock_fetch_short_name(index, symbol))
        return {};
    _db_stocks[index].short_name = symbol;
    return SYMBOL_CONST(symbol);
}

string_const_t stock_get_name(const stock_handle_t& handle)
{
    const stock_t* s = handle.resolve();
    if (s == nullptr)
        return {};
    return SYMBOL_CONST(s->name);
}

string_const_t stock_get_short_name(const stock_handle_t& handle)
{
    return SYMBOL_CONST(handle->short_name.fetch());
}

string_const_t stock_get_currency(const char* code, size_t code_length)
{
    // Make some quick assumption based on the code itself.
    string_const_t exchange = path_file_extension(code, code_length);
    if (string_equal(STRING_ARGS(exchange), STRING_CONST("TO"))) return CTEXT("CAD");
    if (string_equal(STRING_ARGS(exchange), STRING_CONST("V"))) return CTEXT("CAD");
    if (string_equal(STRING_ARGS(exchange), STRING_CONST("US"))) return CTEXT("USD");
    if (string_equal(STRING_ARGS(exchange), STRING_CONST("NEO"))) return CTEXT("CAD");

    SHARED_READ_LOCK(_db_lock);
    stock_index_t index = stock_index(code, code_length);

    if (index == 0 || _db_stocks[index].currency == STRING_TABLE_NULL_SYMBOL)
    {
        tick_t timeout = time_current();
        stock_handle_t handle = stock_request(code, code_length, FetchLevel::FUNDAMENTALS);
        while (handle->currency == 0 && time_elapsed(timeout) < 5)
            dispatcher_wait_for_wakeup_main_thread(250);
            
        if (handle->currency != STRING_TABLE_NULL_SYMBOL)
            return SYMBOL_CONST(handle->currency);
            
        log_warnf(HASH_STOCK, WARNING_INVALID_VALUE,
            STRING_CONST("Failed to get stock '%.*s' currency"), (int)code_length, code);
    }
    else
    {
        return SYMBOL_CONST(_db_stocks[index].currency);
    }

    return string_const(SETTINGS.preferred_currency, string_length(SETTINGS.preferred_currency));
}

double stock_current_price(stock_handle_t& handle)
{
    if (stock_resolve(handle, FetchLevel::REALTIME) < 0)
        return NAN;

    const tick_t timeout = time_current();
    while (!handle->has_resolve(FetchLevel::REALTIME) && time_elapsed(timeout) < 5)
        dispatcher_wait_for_wakeup_main_thread(250);

    return handle->current.price;
}

double stock_price_on_date(stock_handle_t& handle, time_t at)
{
    if (stock_resolve(handle, FetchLevel::EOD) < 0)
        return NAN;

    const tick_t timeout = time_current();
    while (!handle->has_resolve(FetchLevel::EOD) && time_elapsed(timeout) < 5)
        dispatcher_wait_for_wakeup_main_thread(250);

    const day_result_t* ed = stock_get_EOD(handle, at, true);
    if (ed == nullptr)
        return NAN;

    return ed->adjusted_close;
}

bool stock_get_time_range(const char* symbol, size_t symbol_length, time_t* start_time, time_t* end_time, double timeout_seconds)
{
    stock_handle_t handle = stock_request(symbol, symbol_length, FetchLevel::EOD);
    if (!handle)
        return false;

    tick_t timeout = time_current();
    while (!handle->has_resolve(FetchLevel::EOD) && time_elapsed(timeout) < timeout_seconds)
        dispatcher_wait_for_wakeup_main_thread();

    if (!handle->has_resolve(FetchLevel::EOD))
        return false;

    const stock_t* stock = handle.resolve();
    if (stock == nullptr || stock->history_count == 0)
        return false;

    if (start_time)
        *start_time = array_last(stock->history)->date;

    if (end_time)
        *end_time = array_first(stock->history)->date;

    return true;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void stock_initialize()
{
    _db_capacity = 256;
    _db_hashes = hashtable64_allocate(_db_capacity);
    array_push(_db_stocks, stock_t{});
}

FOUNDATION_STATIC void stock_shutdown()
{
    hashtable64_deallocate(_exchange_rates);
    _exchange_rates = nullptr;

    {
        SHARED_WRITE_LOCK(_db_lock);
        for (size_t i = 0; i < array_size(_trashed_history); ++i)
            array_deallocate(_trashed_history[i]);
        array_deallocate(_trashed_history);

        for (size_t i = 1; i < array_size(_db_stocks); ++i)
        {
            stock_t* stock_data = &_db_stocks[i];
            array_deallocate(stock_data->previous);
            array_deallocate(stock_data->history);
            stock_data->history_count = 0;
        }

        array_deallocate(_db_stocks);
        _db_stocks = nullptr;

        hashtable64_deallocate(_db_hashes);
        _db_hashes = nullptr;
    }
}

DEFINE_MODULE(STOCK, stock_initialize, stock_shutdown, MODULE_PRIORITY_BASE);
