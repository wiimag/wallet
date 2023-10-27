/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */
 
#pragma once

#include <framework/handle.h>
#include <framework/common.h>
#include <framework/string_table.h>
#include <framework/option.h>

#include <foundation/string.h>

struct job_t;
struct json_object_t;
typedef uint64_t stock_index_t;

/*! Define various stock fetch levels. 
 *  These levels are used to determine how much data to fetch from the server.
 */
typedef enum class FetchLevel {
    NONE					= 0,

    REALTIME				= 1 << 0, // Cost  1 call
    FUNDAMENTALS			= 1 << 1, // Cost 10 calls
    EOD						= 1 << 2, // Cost 1 call
    TECHNICAL_SMA			= 1 << 4, // Cost 5 call
    TECHNICAL_EMA			= 1 << 5, // Cost 5 call
    TECHNICAL_WMA			= 1 << 6, // Cost 5 call
    TECHNICAL_BBANDS		= 1 << 7, // Cost 5 call
    TECHNICAL_SAR			= 1 << 8, // Cost 5 call
    TECHNICAL_SLOPE			= 1 << 9, // Cost 5 call
    TECHNICAL_CCI			= 1 << 10, // Cost 5 call
} fetch_level_t;
DEFINE_ENUM_FLAGS(FetchLevel);

constexpr FetchLevel TECHINICAL_CHARTS = 
    FetchLevel::TECHNICAL_SMA | 
    FetchLevel::TECHNICAL_EMA | 
    FetchLevel::TECHNICAL_WMA | 
    FetchLevel::TECHNICAL_BBANDS | 
    FetchLevel::TECHNICAL_SAR | 
    FetchLevel::TECHNICAL_SLOPE | 
    FetchLevel::TECHNICAL_CCI;

/*! Represents a single realtime data snapshot. */
FOUNDATION_ALIGNED_STRUCT(stock_realtime_record_t, 8)
{
    time_t timestamp;
    double price;
    double volume;
};

/*! Represents stock realtime data. */
FOUNDATION_ALIGNED_STRUCT(stock_realtime_t, 8)
{
    hash_t key;
    char   code[16];
    time_t timestamp{ 0 };
    double price;
    double volume;
    bool   refresh{ false };

    stock_realtime_record_t* records{ nullptr };
};

FOUNDATION_ALIGNED_STRUCT(stock_eod_record_t, 8)
{
    time_t timestamp;
    double open;
    double high;
    double low;
    union {
        double close;
        double price;
    };
    double adjusted_close;
    double volume;
};

/*! Represents a stock day results. */
FOUNDATION_ALIGNED_STRUCT(day_result_t, 8)
{
    union {
        time_t date{ 0 };
        double ts;
    };
    uint8_t gmtoffset{ 0 };

    double open{ DNAN };

    union {
        double close{ DNAN };
        double price;
    };
    double adjusted_close{ DNAN };
    double previous_close{ DNAN };
    double price_factor{ DNAN };

    double low{ DNAN };
    double high{ DNAN };
        
    double change{ DNAN };
    double change_p{ DNAN };
    double change_p_high{ DNAN };
        
    double volume{ DNAN };

    double wma{ DNAN };
    double ema{ DNAN };
    double sma{ DNAN };

    double uband{ DNAN };
    double mband{ DNAN };
    double lband{ DNAN };

    double sar{ DNAN };
    double slope{ DNAN };
    double cci{ DNAN };
};

/*! Represents a stock. */
FOUNDATION_ALIGNED_STRUCT(stock_t, 8)
{
    hash_t id;
    tick_t last_update_time {0};
    fetch_level_t fetch_level{ FetchLevel::NONE };
    fetch_level_t resolved_level{ FetchLevel::NONE };
    unsigned int fetch_errors{ 0 };

    string_table_symbol_t code{};
    string_table_symbol_t symbol{};

    // Fundamentals
    string_table_symbol_t name{};
    string_table_symbol_t country{};
    string_table_symbol_t type{};
    string_table_symbol_t currency{};
    string_table_symbol_t isin{};
    string_table_symbol_t industry{};
    string_table_symbol_t sector{};
    string_table_symbol_t group{};
    string_table_symbol_t activity{};
    string_table_symbol_t category{};
    string_table_symbol_t url{};
    string_table_symbol_t logo{};
    string_table_symbol_t exchange{};
    double market_cap{ DNAN };
    double shares_count{ DNAN };
    double low_52{ DNAN };
    double high_52{ DNAN };
    double pe{ DNAN };
    double peg{ DNAN };
    double ws_target{ DNAN };
    double beta{ DNAN };
    double dma_50{ DNAN };
    double dma_200{ DNAN };
    double revenue_per_share_ttm{ DNAN };
    double trailing_pe{ DNAN };
    double forward_pe{ DNAN };
    double short_ratio{ DNAN };
    double short_percent{ DNAN };
    double profit_margin{ DNAN };
    double diluted_eps_ttm{ DNAN };

    time_t updated_at{ 0 };

    day_result_t current{};
    day_result_t* history{ nullptr };
    size_t history_count{ 0 };
    day_result_t* previous{ nullptr };

    double_option_t earning_next_quarter{ DNAN };
    double_option_t earning_current_quarter{ DNAN };
    double_option_t earning_trend_actual{ DNAN };
    double_option_t earning_trend_estimate{ DNAN };
    double_option_t earning_trend_difference{ DNAN };
    double_option_t earning_trend_percent{ DNAN };
    double_option_t dividends_yield { DNAN };
    string_option_t short_name { STRING_TABLE_NULL_SYMBOL };
    string_option_t description{ STRING_TABLE_NULL_SYMBOL };

    //! @brief Checks if the stock is either already resolved or is in the process of being resolved.
    //! @param required_level The level of resolution required.
    FOUNDATION_FORCEINLINE bool is_resolving(fetch_level_t required_level, double timeout = 5.0) const
    {
        if (has_resolve(required_level))
            return true;

        if (timeout != 0 && time_elapsed(last_update_time) > timeout)
            return false;
            
        return ((this->resolved_level | this->fetch_level) & required_level) == required_level;
    }

    //! @brief Checks if the stock is resolved.
    //! @param required_level The level of resolution required.
    //! @return True if the stock is resolved, false otherwise.
    FOUNDATION_FORCEINLINE bool has_resolve(fetch_level_t required_level) const
    {
        return (this->resolved_level & required_level) == required_level;
    }

    //! @brief Mark the stock as currently resolving/fetching some data.
    //! @param fetched_level The level of resolution that is being fetched.
    FOUNDATION_FORCEINLINE void mark_fetched(fetch_level_t fetched_level)
    {
        this->fetch_level |= (fetched_level & ~this->resolved_level);
        this->last_update_time = time_current();
    }

    //! @brief Mark the stock as resolved for a given level.
    //! @param resolved_level The level of resolution that has being resolved.
    FOUNDATION_FORCEINLINE void mark_resolved(fetch_level_t resolved_level, bool keep_errors = false)
    {
        this->resolved_level |= resolved_level;
        this->fetch_level &= ~this->resolved_level;
        this->last_update_time = time_current();
        if (!keep_errors)
            this->fetch_errors = 0;
    }
};

/*! Represents a stock handle. 
 *  The handles are used to reference stocks without having to keep a pointer to the stock_t object.
 */
struct stock_handle_t
{
    hash_t id{ 0 };
    string_table_symbol_t code { STRING_TABLE_NULL_SYMBOL };
    
    mutable const stock_t* ptr{ nullptr };

    const stock_t* get() const
    {
        const stock_t* s = resolve();
        if (s != nullptr)
            return s;

        static const stock_t NIL{};
        return &NIL;
    }

    const stock_t* resolve() const
    {        
        extern bool stock_request(const stock_handle_t & handle, const stock_t** out_stock);
        
        if (id == 0)
            return nullptr;

        if (stock_request(*this, &ptr))
            return ptr;
        return nullptr;
    }

    FOUNDATION_FORCEINLINE operator bool() const
    {
        if (id == 0)
            return false;
        return this->operator*() != nullptr;
    }

    FOUNDATION_FORCEINLINE bool initialized() const
    {
        if (id == 0)
            return false;
        return true;
    }

    FOUNDATION_FORCEINLINE stock_t* operator->() { return (stock_t*)get(); }
    FOUNDATION_FORCEINLINE const stock_t* operator->() const { return get(); }
    FOUNDATION_FORCEINLINE const stock_t* operator*() const { return resolve(); }
    FOUNDATION_FORCEINLINE operator const stock_t* () const { return resolve(); }
};

/*! Request the stock data pointer if already resolved.
 *  The returned pointer is unsafe as it might get invalidated over time. 
 * 
 *  @param handle The stock handle.
 *  @param out_stock The stock pointer.
 * 
 *  @return True if the stock was already resolved, false otherwise.
 */
bool stock_request(const stock_handle_t& handle, const stock_t** out_stock);

/*! Initialize a stock handle structure. 
 *  The stock handle is used to reference stocks without having to keep a pointer to the stock_t object.
 * 
 *  @param code The stock code.
 *  @param code_length The length of the stock code.
 *  @param stock_handle The stock handle to initialize.
 * 
 *  @return True if the stock handle was initialized, false otherwise.
 */
status_t stock_initialize(const char* code, size_t code_length, stock_handle_t* stock_handle);

/*! Attempt to resolve a stock at a given fetch level. 
 * 
 *  @param stock_handle The stock handle to resolve.
 *  @param fetch_levels The fetch level to resolve.
 * 
 *  @return True if the stock was resolved, false otherwise.
 */
status_t stock_resolve(stock_handle_t& stock_handle, fetch_level_t fetch_levels);

/*! Attempt to resolve a stock at a given fetch level. 
 * 
 *  @param symbol The stock symbol.
 *  @param symbol_length The length of the stock symbol.
 *  @param fetch_levels The fetch level to resolve.
 *  @param timeout The timeout in seconds to wait for the stock to be resolved.
 * 
 *  @return True if the stock was resolved, false otherwise.
 */
stock_handle_t stock_resolve(const char* symbol, size_t symbol_length, fetch_level_t fetch_levels, double timeout = 5.0f);

/*! Request and resolve a stock symbol. 
 *  This function will attempt to resolve the stock at the given fetch level.
 * 
 *  @param symbol The stock symbol.
 *  @param symbol_length The length of the stock symbol.
 *  @param fetch_level The fetch level to resolve.
 * 
 *  @return The stock handle.
 */
stock_handle_t stock_request(const char* symbol, size_t symbol_length, fetch_level_t fetch_level);

/*! Request and update a stock symbol. 
 * 
 *  @param handle The stock handle to update.
 *  @param fetch_level The fetch level to resolve.
 *  @param timeout The timeout in seconds to wait for the stock to be resolved.
 * 
 *  @return True if the stock was resolved, false otherwise (i.e. timeout happens).
 */
bool stock_update(stock_handle_t& handle, fetch_level_t fetch_level, double timeout = 15.0);

/*! Request and update a stock symbol. 
 * 
 *  @param code The stock symbol code.
 *  @param code_length The length of the stock symbol code.
 *  @param handle The stock handle to update.
 *  @param fetch_level The fetch level to resolve.
 *  @param timeout The timeout in seconds to wait for the stock to be resolved.
 * 
 *  @return True if the stock was resolved, false otherwise (i.e. timeout happens).
 */
bool stock_update(const char* code, size_t code_length, stock_handle_t& handle, fetch_level_t fetch_level, double timeout = 5.0);

/*! Returns the exchange rate between two currencies.
 * 
 *  @param from The currency code to convert from.
 *  @param from_length The length of the currency code to convert from.
 *  @param to The currency code to convert to.
 *  @param to_length The length of the currency code to convert to.
 *  @param at The date to get the exchange rate at.
 * 
 *  @return The exchange rate between the two currencies.
 */
double stock_exchange_rate(const char* from, size_t from_length, const char* to, size_t to_length, time_t at = 0);

/*! Returns the end-of-day data for a stock from a given range based on today.
 * 
 *  @param stock_data The stock data.
 *  @param rel_day The relative day to get the data for.
 *  @param take_last If true, the last available data will be returned if the given date is not available.
 * 
 *  @return The end-of-day data for the stock at the given date.
 */
const day_result_t* stock_get_EOD(const stock_t* stock_data, int rel_day, bool take_last = false);

/*! Returns the end-of-day data for a stock at a given date.
 * 
 *  @param stock_data The stock data.
 *  @param day_time The date to get the data for.
 *  @param take_last If true, the last available data will be returned if the given date is not available.
 * 
 *  @return The end-of-day data for the stock at the given date.
 */
const day_result_t* stock_get_EOD(const stock_t* stock_data, time_t day_time, bool take_last = false);

/*! Get the split adjusted data at a given date.
 * 
 *  @param code         The stock symbol code
 *  @param code_length  The length of the stock symbol code
 *  @param at           Query date
 * 
 *  @return The split adjusted data at the given date
 */
day_result_t stock_get_split(const char* code, size_t code_length, time_t at);

/*! Get the EOD data for a given symbol (skipping the need for a stock handle).
 * 
 *  @param code         The stock symbol code
 *  @param code_length  The length of the stock symbol code
 *  @param at           Query date
 * 
 *  @return The EOD data at the given date. It is possible that the data is not available and therefore we return a default constructed day_result_t.
 */
day_result_t stock_get_eod(const char* code, size_t code_length, time_t at);

/*! Get the split factor at a given date.
 * 
 *  @param handle The stock handle.
 *  @param at     Query date
 * 
 *  @return The split adjusted data at the given date
 */
double stock_get_split_factor(const char* code, size_t code_length, time_t at);

/*! Get the EOD price factor at a given date. We take the adjusted price and divide it by the unadjusted price of that day.
 * 
 *  @param handle The stock handle.
 *  @param at     Query date
 * 
 *  @return The EOD price factor at the given date
 */
double stock_get_eod_price_factor(const char* code, size_t code_length, time_t at);

/*! Get the split adjusted factor at a given date. We take the adjusted price and divide it by the unadjusted price of that day.
 * 
 *  @param handle The stock handle.
 *  @param at     Query date
 * 
 *  @return The split adjusted factor at the given date
 */
double stock_get_split_adjusted_factor(const char* code, size_t code_length, time_t at);

/*! Get the full stock name for a given symbol.
 * 
 *  @param code         The stock symbol code
 *  @param code_length  The length of the stock symbol code
 * 
 *  @return The full stock name for the given symbol.
 */
string_const_t stock_get_name(const char* code, size_t code_length);

/*! Get the short stock name for a given symbol.
 * 
 *  Too shorten the name we remove some suffixes like "Inc.", "Corp.", "Ltd.", "AG", "SA", "NV", "PLC", "AB", "S.A.", "S.A",
 *  This is helpful to run web queries on the name.
 * 
 *  @param code         The stock symbol code
 *  @param code_length  The length of the stock symbol code
 * 
 *  @return The short stock name for the given symbol.
 */
string_const_t stock_get_short_name(const char* code, size_t code_length);

/*! Get the stock name using a valid stock handle.
 * 
 *  @param handle The stock handle.
 * 
 *  @return The stock name.
 */
string_const_t stock_get_name(const stock_handle_t& handle);

/*! Get the short stock name using a valid stock handle.
 * 
 *  Too shorten the name we remove some suffixes like "Inc.", "Corp.", "Ltd.", "AG", "SA", "NV", "PLC", "AB", "S.A.", "S.A",
 *  This is helpful to run web queries on the name.
 * 
 *  @param handle The stock handle.
 * 
 *  @return The short stock name.
 */
string_const_t stock_get_short_name(const stock_handle_t& handle);

/*! Get the currency for a given symbol.
 * 
 *  @param code         The stock symbol code
 *  @param code_length  The length of the stock symbol code
 * 
 *  @return The currency for the given symbol.
 */
string_const_t stock_get_currency(const char* code, size_t code_length);

/*! Extract from a "real-time" EOD query the realtime data.
 * 
 *  This can be useful if you want to query the realtime data for a stock yourself using #eod_fetch.
 * 
 *  @param stock_index The stock index.
 *  @param json        The JSON object.
 *  @param d           The day result.
 * 
 *  @return True if the data was extracted successfully.
 */
bool stock_read_real_time_results(stock_index_t stock_index, const json_object_t& json, day_result_t& d);

/*! Get the stock index for a given symbol.
 * 
 *  @remark This is mainly used for internal purposes. There is no guarantee that the index will be the same in the future.
 * 
 *  @param symbol         The stock symbol code
 *  @param symbol_length  The length of the stock symbol code
 * 
 *  @return The stock index for the given symbol.
 */
stock_index_t stock_index(const char* symbol, size_t symbol_length);

/*! Get the start and end time for which we have data for a given symbol.
 * 
 *  @param symbol         The stock symbol code
 *  @param symbol_length  The length of the stock symbol code
 *  @param start_time     The start time
 *  @param end_time       The end time
 *  @param timeout        The timeout in seconds. If 0, the default timeout will be used.
 * 
 *  @return True if the data was extracted successfully.
 */
bool stock_get_time_range(const char* symbol, size_t symbol_length, time_t* start_time, time_t* end_time = nullptr, double timeout = 1);

/*! Get the stock today's price. This is the price at the end of the day.
 *
 *  @remark This is a convenience function that calls #stock_price_on_date with the current date.
 *          This function blocks until the data is available or the timeout is reached.
 * 
 *  @param handle The stock handle.
 * 
 *  @return The stock today's price.
 */
double stock_current_price(stock_handle_t& handle);

/*! Get the stock price at a given date.
 *
 *  @remark This function blocks until the data is available or the timeout is reached.
 * 
 *  @param handle The stock handle.
 *  @param at     Query date
 * 
 *  @return The stock price at the given date.
 */
double stock_price_on_date(stock_handle_t& handle, time_t at);

/*! Runs a serie of tests to check if a given symbol is valid.
 *  Most of all we check that we have some recent real-time data for the symbol.
 *
 *  @remark This function blocks until the data is available or the timeout is reached.
 * 
 *  @param symbol  The stock symbol code
 *  @param length  The length of the stock symbol code
 *  @param timeout The timeout in seconds.
 * 
 *  @return True if the symbol is valid.
 */
bool stock_valid(const char* symbol, size_t length);

/*! Mark a symbol as invalid. Therefore it will be ignored until removed from the list.
 *
 *  An ignored symbol will be skipped by #stock_valid.
 *  The system will remove it from the list after a while (i.e. 15 days).
 * 
 *  @param symbol  The stock symbol code
 *  @param length  The length of the stock symbol code
 */
bool stock_ignore_symbol(const char* symbol, size_t length, hash_t key = 0);

/* Fetch stock real-time data if any. This is a convenience function that calls #eod_fetch.
 * 
 *  @param symbol  The stock symbol code
 *  @param length  The length of the stock symbol code
 * 
 *  @return The stock real-time data.
 */
day_result_t stock_realtime_record(const char* symbol, size_t length);

/*! Get synchronously the EOD data at a given date.
 * 
 *  @param symbol  The stock symbol code
 *  @param length  The length of the stock symbol code
 *  @param at      Query date
 * 
 *  @return The stock EOD data at the given date.
 */
stock_eod_record_t stock_eod_record(const char* symbol, size_t length, time_t at, uint64_t invalid_cache_query_after_seconds = 7 * 24 * 60 * 60ULL);

/*! Checks if the stock represents an index.
 * 
 *  @param symbol  The stock symbol code
 *  @param length  The length of the stock symbol code
 * 
 *  @return True if the stock represents an index.
 */
bool stock_is_index(const char* symbol, size_t length);

/*! Checks if the stock represents an index.
 * 
 *  @param handle The stock handle.
 * 
 *  @return True if the stock represents an index.
 */
bool stock_is_index(stock_handle_t stock);

/*! Checks if the stock represents an index.
 * 
 *  @param stock The stock.
 * 
 *  @return True if the stock represents an index.
 */
bool stock_is_index(const stock_t* stock);

/*! Checks if the stock represents a common stock.
 * 
 *  @param stock Initialized stock handle.
 * 
 *  @return True if the stock represents a common stock.
 */
bool stock_is_common(stock_handle_t stock);
