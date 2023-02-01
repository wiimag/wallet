/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#include "stock.h"

#include "eod.h"

#include <framework/common.h>
#include <framework/query.h>
#include <framework/scoped_string.h>
#include <framework/scoped_mutex.h>
#include <framework/shared_mutex.h>
#include <framework/jobs.h>
#include <framework/query.h>
#include <framework/service.h>
#include <framework/profiler.h>

#include <foundation/log.h>
#include <foundation/array.h>
#include <foundation/time.h>
#include <foundation/hash.h>
#include <foundation/objectmap.h>
#include <foundation/hashtable.h>
#include <foundation/mutex.h>
#include <foundation/thread.h>

#include <ctime>
#include <algorithm>

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
    // TODO: Dispose of old stocks (outdated)?
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

FOUNDATION_STATIC bool stock_fetch_description(stock_index_t stock_index, string_table_symbol_t& value)
{
    if (_db_stocks == nullptr || stock_index >= array_size(_db_stocks))
        return false;	
    stock_t* stock_data = &_db_stocks[stock_index];
    const char* ticker = string_table_decode(stock_data->code);
    return eod_fetch_async("fundamentals", ticker, FORMAT_JSON_CACHE,
        "filter", "General::Description", [stock_index](const json_object_t& json)
    {
        if (json.root == nullptr)
            return;
        stock_t* stock_data = &_db_stocks[stock_index];
        stock_data->description = string_table_encode_unescape(json_token_value(json.buffer, json.root));
    }, 7 * 24 * 60 * 60ULL);
}

FOUNDATION_STATIC void stock_read_real_time_results(const json_object_t& json, uint64_t index)
{
    day_result_t dresult{};
    dresult.date = (time_t)json_read_number(json, STRING_CONST("timestamp"));
    dresult.gmtoffset = (uint8_t)json_read_number(json, STRING_CONST("gmtoffset"));
    dresult.open = json_read_number(json, STRING_CONST("open"));
    dresult.close = json_read_number(json, STRING_CONST("close"));
    dresult.previous_close = json_read_number(json, STRING_CONST("previousClose"));
    dresult.low = json_read_number(json, STRING_CONST("low"));
    dresult.high = json_read_number(json, STRING_CONST("high"));
    dresult.change = json_read_number(json, STRING_CONST("change"));
    dresult.change_p = json_read_number(json, STRING_CONST("change_p"));
    dresult.volume = json_read_number(json, STRING_CONST("volume"));

    {
        SHARED_READ_LOCK(_db_lock);
        stock_t* entry = &_db_stocks[index];

        if (entry->current.date != 0 && entry->current.date != dresult.date)
            array_push(entry->previous, entry->current);

        entry->current = dresult;
        entry->mark_resolved(FetchLevel::REALTIME);
    }
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
    entry.updated_at = string_table_encode(general["UpdatedAt"].as_string());
    entry.exchange = string_table_encode(general["Exchange"].as_string());
    entry.description = string_table_encode_unescape(general["Description"].as_string());

    const json_object_t& hightlights = json["Highlights"];
    entry.dividends_yield = hightlights["DividendYield"].as_number(0.0);
    entry.pe = hightlights["PERatio"].as_number();
    entry.peg = hightlights["PEGRatio"].as_number();
    entry.ws_target = hightlights["WallStreetTargetPrice"].as_number();
    entry.revenue_per_share_ttm = hightlights["RevenuePerShareTTM"].as_number();
    entry.profit_margin = hightlights["ProfitMargin"].as_number();
    entry.diluted_eps_ttm = hightlights["DilutedEpsTTM"].as_number();

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
    for (size_t i = 0; i < json.root->value_length; ++i)
    {
        const auto& e = json[i];
        const time_t date = string_to_date(STRING_ARGS(e["date"].as_string()));

        bool applied_to_current = false;
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
    technical_descriptor_t desc)
{
    stock_t* entry = &_db_stocks[index];

    if ((fetch_levels & access_level) && ((entry->fetch_level | entry->resolved_level) & access_level) == 0)
    {
        if (entry->has_resolve(FetchLevel::TECHNICAL_EOD) || entry->has_resolve(FetchLevel::EOD))
        {
            if (eod_fetch_async("technical", ticker, FORMAT_JSON_CACHE, "order", "d", "function", fn_name,
                [index, access_level, desc](const json_object_t& json)
                {
                    stock_read_technical_results(json, index, access_level, desc);
                }, 12ULL * 3600ULL))
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
    }
}

FOUNDATION_STATIC void stock_fetch_technical_results(
    fetch_level_t access_level, status_t& status, fetch_level_t fetch_levels,
    const char* ticker, stock_index_t index, const char* fn_name,
    const char* field_name, size_t offset)
{
    stock_fetch_technical_results(access_level, status, fetch_levels, ticker, index, fn_name,
        technical_descriptor_t{1, {field_name}, {(uint8_t)offset}});
}

FOUNDATION_STATIC void stock_read_eod_indexed_prices(const json_object_t& json, stock_index_t index)
{
    tick_t timeout = time_current();
    do
    {
        {
            SHARED_READ_LOCK(_db_lock);
            
            stock_t* s = &_db_stocks[index];
            if (s->has_resolve(FetchLevel::TECHNICAL_EOD) || s->has_resolve(FetchLevel::EOD))
                break;
        }
        
        thread_sleep(10);
    } while (time_elapsed(timeout) < 60.0);
    
    day_result_t* history = nullptr;
    {
        SHARED_READ_LOCK(_db_lock);
        
        stock_t* s = &_db_stocks[index];
        if (!s->has_resolve(FetchLevel::TECHNICAL_EOD) && !s->has_resolve(FetchLevel::EOD))
        {
            log_warnf(HASH_STOCK, WARNING_NETWORK, STRING_CONST("Missing EOD results to resolve indexed price for %s"), SYMBOL_CSTR(s->code));
            return;
        }
        history = s->history;
    }
    
    double first_price_factor = DNAN;
    int h = 0, h_end = array_size(history);
    for (size_t i = 0; i < json.root->value_length; ++i)
    {
        const auto& e = json[i];
        const time_t date = string_to_date(STRING_ARGS(e["date"].as_string()));
        const double ed_price_factor = e["adjusted_close"].as_number() / e["close"].as_number();
        
        for (; h != h_end;)
        {
            day_result_t* ed = &history[h];
            if (time_date_equal(ed->date, date))
            {
                ed->price_factor = ed_price_factor;
                if (!math_real_is_nan(ed->price_factor))
                    first_price_factor = ed->price_factor;
                break;
            }
            else if (ed->date < date)
                break;
            else
                h++;
        }
    }

    {
        SHARED_WRITE_LOCK(_db_lock);
        stock_t* s = &_db_stocks[index];
        if (math_real_is_nan(s->current.price_factor) && !math_real_is_nan(first_price_factor))
            s->current.price_factor = first_price_factor;
        s->mark_resolved(FetchLevel::TECHNICAL_INDEXED_PRICE);
    }
}

FOUNDATION_STATIC void stock_read_eod_results(const json_object_t& json, stock_index_t index, FetchLevel eod_fetch_level)
{
    MEMORY_TRACKER(HASH_STOCK);

    day_result_t* history = nullptr;
    array_reserve(history, json.root->value_length + 1);

    double first_change_p_high = DNAN, first_price_factor = DNAN;
    int element_count = json.root->value_length;
    const json_token_t* e = &json.tokens[json.root->child];
    for (int i = 0; i < element_count; ++i)
    {
        json_object_t jday(json, e);
        double volume = jday["volume"].as_number();
        if (volume >= 1.0 || i < 7)
        {
            day_result_t d;

            string_const_t date_str = jday["date"].as_string();
            d.date = string_to_date(STRING_ARGS(date_str));
            d.gmtoffset = 0;

            d.open = jday["open"].as_number();
            if (eod_fetch_level == FetchLevel::EOD)
            {
                d.close = jday["adjusted_close"].as_number();
                d.price_factor = d.close / jday["close"].as_number();

                if (!math_real_is_nan(d.price_factor))
                    first_price_factor = d.price_factor;
            }
            else 
                d.close = jday["close"].as_number();

            d.change_p_high = NAN;
            d.previous_close = NAN;

            if (e->sibling != 0)
            {
                json_object_t yday(json, &json.tokens[e->sibling]);
                d.previous_close = yday["close"].as_number();
            }

            d.low = jday["low"].as_number();
            d.high = jday["high"].as_number();

            d.change = d.close - d.open;
            d.change_p = d.change * 100.0 / d.open;
            d.change_p_high = (max(d.close, d.high) - min(d.open, d.low)) * 100.0 / math_ifnan(d.previous_close, d.close);
            d.volume = volume;

            if (!math_real_is_nan(d.change_p_high))
                first_change_p_high = d.change_p_high;

            history = array_push(history, d);
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

        if (math_real_is_nan(entry.current.change_p_high) && !math_real_is_nan(first_change_p_high))
            entry.current.change_p_high = first_change_p_high;

        if (math_real_is_nan(entry.current.price_factor) && !math_real_is_nan(first_price_factor))
            entry.current.price_factor = first_price_factor;

        if (eod_fetch_level == FetchLevel::EOD)
            eod_fetch_level |= FetchLevel::TECHNICAL_INDEXED_PRICE;

        entry.mark_resolved(eod_fetch_level);
    }
}

//
// # PUBLIC API
//

status_t stock_resolve(stock_handle_t& handle, fetch_level_t fetch_levels)
{
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

        entry->id = handle.id;
        entry->code = handle.code;

        entry->description.reset([index](string_table_symbol_t& value) {  return stock_fetch_description(index, value); });

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
    string_copy(STRING_CONST_CAPACITY(ticker), STRING_ARGS(string_table_decode_const(handle.code)));

    status_t status = STATUS_OK;
    if ((fetch_levels & FetchLevel::REALTIME) && ((entry->fetch_level | entry->resolved_level) & FetchLevel::REALTIME) == 0)
    {
        if (eod_fetch_async("real-time", ticker, FORMAT_JSON_CACHE, E21(stock_read_real_time_results, _1, index), 5 * 60ULL))
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
        if (eod_fetch_async("fundamentals", ticker, FORMAT_JSON_CACHE,
            E21(stock_read_fundamentals_results, _1, index), 3 * 24ULL * 3600ULL))
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
        if (eod_fetch_async("eod", ticker, FORMAT_JSON_CACHE, "order", "d", E31(stock_read_eod_results, _1, index, FetchLevel::EOD), 12ULL * 3600ULL))
        {
            entry->mark_fetched(FetchLevel::EOD | FetchLevel::TECHNICAL_INDEXED_PRICE);
            status = STATUS_RESOLVING;
        }
        else
        {
            entry->fetch_errors++;
            log_warnf(HASH_STOCK, WARNING_RESOURCE, STRING_CONST("[%u] Failed to fetch EOD results for %s"), entry->fetch_errors, ticker);
        }
    }

    if ((fetch_levels & FetchLevel::TECHNICAL_EOD) && ((entry->fetch_level | entry->resolved_level) & FetchLevel::TECHNICAL_EOD) == 0)
    {
        // Fix some fetch level combinations
        if ((entry->resolved_level & FetchLevel::EOD) == FetchLevel::EOD)
        {
            entry->history_count = 0;
            if (entry->history != nullptr)
                array_push(_trashed_history, entry->history);
            entry->history = nullptr;
            entry->resolved_level &= ~FetchLevel::EOD;

            if ((entry->resolved_level & FetchLevel::TECHNICAL_INDEXED_PRICE) == FetchLevel::TECHNICAL_INDEXED_PRICE)
            {
                fetch_levels |= FetchLevel::TECHNICAL_INDEXED_PRICE;
                entry->resolved_level &= ~FetchLevel::TECHNICAL_INDEXED_PRICE;
            }
        }

        if (eod_fetch_async("technical", ticker, FORMAT_JSON_CACHE, "order", "d", "function", "splitadjusted",
            E31(stock_read_eod_results, _1, index, FetchLevel::TECHNICAL_EOD), 12ULL * 3600ULL))
        {
            entry->mark_fetched(FetchLevel::TECHNICAL_EOD);
            status = STATUS_RESOLVING;
        }
        else
        {
            entry->fetch_errors++;
            log_warnf(HASH_STOCK, WARNING_RESOURCE, STRING_CONST("[%u] Failed to fetch technical EOD results for %s"), entry->fetch_errors, ticker);
        }
    }

    if ((fetch_levels & FetchLevel::TECHNICAL_INDEXED_PRICE) && ((entry->fetch_level | entry->resolved_level) & FetchLevel::TECHNICAL_INDEXED_PRICE) == 0)
    {
        if (eod_fetch_async("eod", ticker, FORMAT_JSON_CACHE, "order", "d", E21(stock_read_eod_indexed_prices, _1, index), 24ULL * 3600ULL))
        {
            entry->mark_fetched(FetchLevel::TECHNICAL_INDEXED_PRICE);
            status = STATUS_RESOLVING;
        }
        else
        {
            entry->fetch_errors++;
            log_warnf(HASH_STOCK, WARNING_RESOURCE, STRING_CONST("[%u] Failed to fetch EOD indexed prices for %s"), entry->fetch_errors, ticker);
        }
    }

    stock_fetch_technical_results(FetchLevel::TECHNICAL_SMA, status, fetch_levels, ticker, index, "ema", "ema", offsetof(day_result_t, ema));
    stock_fetch_technical_results(FetchLevel::TECHNICAL_EMA, status, fetch_levels, ticker, index, "sma", "sma", offsetof(day_result_t, sma));
    stock_fetch_technical_results(FetchLevel::TECHNICAL_WMA, status, fetch_levels, ticker, index, "wma", "wma", offsetof(day_result_t, wma));
    stock_fetch_technical_results(FetchLevel::TECHNICAL_SAR, status, fetch_levels, ticker, index, "sar", "sar", offsetof(day_result_t, sar));
    stock_fetch_technical_results(FetchLevel::TECHNICAL_SLOPE, status, fetch_levels, ticker, index, "slope", "slope", offsetof(day_result_t, slope));
    stock_fetch_technical_results(FetchLevel::TECHNICAL_CCI, status, fetch_levels, ticker, index, "cci", "cci", offsetof(day_result_t, cci));
    stock_fetch_technical_results(FetchLevel::TECHNICAL_BBANDS, status, fetch_levels, ticker, index, "bbands",
        technical_descriptor_t{ 3, { "uband", "mband", "lband" }, { offsetof(day_result_t, uband), offsetof(day_result_t, mband), offsetof(day_result_t, lband) } });

    return status;
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
        return stock_handle;

    if (stock_resolve(stock_handle, fetch_levels) < 0)
        return stock_handle;

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
            eod_fetch("real-time", exchange_code, FORMAT_JSON_CACHE, [&rate](const auto& json)
            {
                const json_object_t& jc = json["close"];
                rate = jc.as_number(rate);
            }, 15 * 60ULL);
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
                }, 48 * 60 * 60ULL);
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

    stock_handle->code = string_table_encode(code, code_length);
    stock_handle->id = hash(code, code_length);

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

        for (size_t i = 0; i < array_size(_db_stocks); ++i)
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

DEFINE_SERVICE(STOCK, stock_initialize, stock_shutdown, SERVICE_PRIORITY_BASE);
