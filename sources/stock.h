/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
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

typedef enum class FetchLevel /*: unsigned int*/ {
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

    TECHINICAL_CHARTS = TECHNICAL_SMA | TECHNICAL_EMA | TECHNICAL_WMA | TECHNICAL_BBANDS | TECHNICAL_SAR | TECHNICAL_SLOPE | TECHNICAL_CCI
} fetch_level_t;
DEFINE_ENUM_FLAGS(FetchLevel);

FOUNDATION_ALIGNED_STRUCT(stock_realtime_record_t, 8)
{
    time_t timestamp;
    double price;
    double volume;
};

FOUNDATION_ALIGNED_STRUCT(stock_realtime_t, 8)
{
    hash_t key;
    char   code[16];
    time_t timestamp;
    double price;
    double volume;
    bool   refresh{ false };

    stock_realtime_record_t* records{ nullptr };
};

FOUNDATION_ALIGNED_STRUCT(day_result_t, 8)
{
    time_t date{ 0 };
    uint8_t gmtoffset{ 0 };

    double open{ NAN };
    double close{ NAN };
    double adjusted_close{ NAN };
    double previous_close{ NAN };
    double price_factor{ NAN };

    double low{ NAN };
    double high{ NAN };
        
    double change{ NAN };
    double change_p{ NAN };
    double change_p_high{ NAN };
        
    double volume{ NAN };

    double wma{ NAN };
    double ema{ NAN };
    double sma{ NAN };

    double uband{ NAN };
    double mband{ NAN };
    double lband{ NAN };

    double sar{ NAN };
    double slope{ NAN };
    double cci{ NAN };
};

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
    string_table_symbol_t updated_at{};
    string_table_symbol_t exchange{};
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

    day_result_t current{};
    day_result_t* history{ nullptr };
    size_t history_count{ 0 };
    day_result_t* previous{ nullptr };

    double_option_t earning_trend_actual{ DNAN };
    double_option_t earning_trend_estimate{ DNAN };
    double_option_t earning_trend_difference{ DNAN };
    double_option_t earning_trend_percent{ DNAN };
    double_option_t dividends_yield { DNAN };
    string_option_t short_name { STRING_TABLE_NULL_SYMBOL };
    string_option_t description{ STRING_TABLE_NULL_SYMBOL };

    //! @brief Checks if the stock is either already resolved or is in the process of being resolved.
    //! @param required_level The level of resolution required.
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL bool is_resolving(fetch_level_t required_level, double timeout = 5.0) const
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
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL bool has_resolve(fetch_level_t required_level) const
    {
        return (this->resolved_level & required_level) == required_level;
    }

    //! @brief Mark the stock as currently resolving/fetching some data.
    //! @param fetched_level The level of resolution that is being fetched.
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL void mark_fetched(fetch_level_t fetched_level)
    {
        this->fetch_level |= (fetched_level & ~this->resolved_level);
        this->last_update_time = time_current();
    }

    //! @brief Mark the stock as resolved for a given level.
    //! @param resolved_level The level of resolution that has being resolved.
    FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL void mark_resolved(fetch_level_t resolved_level)
    {
        this->resolved_level |= resolved_level;
        this->fetch_level &= ~this->resolved_level;
        this->last_update_time = time_current();
        this->fetch_errors = 0;
    }
};

struct stock_handle_t
{
    hash_t id;
    string_table_symbol_t code;
    
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

    FOUNDATION_FORCEINLINE const stock_t* operator*() const { return resolve(); }
    FOUNDATION_FORCEINLINE operator const stock_t* () const { return resolve(); }
    FOUNDATION_FORCEINLINE stock_t* operator->() { return (stock_t*)get(); }
    FOUNDATION_FORCEINLINE const stock_t* operator->() const { return get(); }
};

/// <summary>
/// Request the stock data pointer if already resolved.
/// The returned pointer is unsafe as it might get invalidated over time.
/// </summary>
bool stock_request(const stock_handle_t& handle, const stock_t** out_stock);

/// <summary>
/// Initialize a stock handle structure.
/// </summary>
status_t stock_initialize(const char* code, size_t code_length, stock_handle_t* stock_handle);

/// <summary>
/// Attempt to resolve a stock at a given fetch level.
/// </summary>
status_t stock_resolve(stock_handle_t& stock_handle, fetch_level_t fetch_levels);

/// <summary>
/// Request and resolve a stock symbol.
/// </summary>
/// <param name="symbol">The symbol to resolve.</param>
/// <param name="symbol_length">The length of the symbol.</param>
/// <param name="fetch_level">The fetch level to resolve.</param>
/// 
stock_handle_t stock_request(const char* symbol, size_t symbol_length, fetch_level_t fetch_level);

/// <summary>
/// 
/// </summary>
bool stock_update(stock_handle_t& handle, fetch_level_t fetch_level, double timeout = 15.0);

/// <summary>
/// 
/// </summary>
bool stock_update(const char* code, size_t code_length, stock_handle_t& handle, fetch_level_t fetch_level, double timeout = 5.0);

/// <summary>
/// 
/// </summary>
double stock_exchange_rate(const char* from, size_t from_length, const char* to, size_t to_length, time_t at = 0);

/// <summary>
/// 
/// </summary>
const day_result_t* stock_get_EOD(const stock_t* stock_data, int rel_day, bool take_last = false);

/// <summary>
/// 
/// </summary>
const day_result_t* stock_get_EOD(const stock_t* stock_data, time_t day_time, bool take_last = false);

/*! @brief Get the split adjusted data at a given date.
 *  @param code         The stock symbol code
 *  @param code_length  The length of the stock symbol code
 *  @param at           Query date
 *  @return The split adjusted data at the given date
 */
day_result_t stock_get_split(const char* code, size_t code_length, time_t at);

day_result_t stock_get_eod(const char* code, size_t code_length, time_t at);

double stock_get_split_factor(const char* code, size_t code_length, time_t at);

double stock_get_eod_price_factor(const char* code, size_t code_length, time_t at);

double stock_get_split_adjusted_factor(const char* code, size_t code_length, time_t at);

string_const_t stock_get_name(const char* code, size_t code_length);

string_const_t stock_get_short_name(const char* code, size_t code_length);

string_const_t stock_get_name(const stock_handle_t& handle);

string_const_t stock_get_short_name(const stock_handle_t& handle);

string_const_t stock_get_currency(const char* code, size_t code_length);

bool stock_read_real_time_results(stock_index_t stock_index, const json_object_t& json, day_result_t& d);

stock_index_t stock_index(const char* symbol, size_t symbol_length);
