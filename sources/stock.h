#pragma once

#include "framework/handle.h"
#include "framework/string_table.h"
#include "framework/option.h"
#include "framework/common.h"

#include <foundation/time.h>
#include <foundation/string.h>
#include <foundation/math.h>

struct job_t;
typedef uint64_t stock_index_t;

typedef enum class FetchLevel /*: unsigned int*/ {
	NONE					= 0,

	REALTIME				= 1 << 0, // Cost  1 call
	FUNDAMENTALS			= 1 << 1, // Cost 10 calls
	EOD						= 1 << 2, // Cost 1 call
	TECHNICAL_EOD			= 1 << 3, // Cost 5 call
	TECHNICAL_SMA			= 1 << 4, // Cost 5 call
	TECHNICAL_EMA			= 1 << 5, // Cost 5 call
	TECHNICAL_WMA			= 1 << 6, // Cost 5 call
	TECHNICAL_BBANDS		= 1 << 7, // Cost 5 call
	TECHNICAL_SAR			= 1 << 8, // Cost 5 call
	TECHNICAL_SLOPE			= 1 << 9, // Cost 5 call
	TECHNICAL_CCI			= 1 << 10, // Cost 5 call
	TECHNICAL_INDEXED_PRICE = 1 << 11, // Cost 5 call
} fetch_level_t;
DEFINE_VOLATILE_ENUM_FLAGS(FetchLevel);

struct day_result_t
{
	time_t date{ 0 };
	uint8_t gmtoffset;

	double open{ NAN };
	double close{ NAN };
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

struct stock_t
{
	hash_t id;
	volatile tick_t last_update_time {0};
	volatile fetch_level_t fetch_level{ FetchLevel::NONE };
	volatile fetch_level_t resolved_level{ FetchLevel::NONE };
	volatile unsigned int fetch_errors{ 0 };

	string_table_symbol_t code{};
	string_table_symbol_t symbol{};

	// Fundamentals
	string_table_symbol_t name{};
	string_table_symbol_t country{};
	string_table_symbol_t type{};
	string_table_symbol_t currency{};
	string_table_symbol_t url{};
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

	double_option_t dividends_yield { DNAN };
	string_option_t description { STRING_TABLE_NULL_SYMBOL };

    bool is_resolving(fetch_level_t required_level, double since_seconds = 5.0) const
    {
		if (has_resolve(required_level))
			return true;

		if (time_elapsed(last_update_time) < since_seconds && (fetch_level & required_level) != 0)
			return true;
			
		return ((this->resolved_level | this->fetch_level) & required_level) == required_level;
    }

	bool has_resolve(fetch_level_t required_level) const
	{
		return (this->resolved_level & required_level) == required_level;
	}

	void mark_fetched(fetch_level_t fetched_level)
	{
		this->fetch_level |= (fetched_level & ~this->resolved_level);
		this->last_update_time = time_current();
	}

	void mark_resolved(fetch_level_t resolved_level)
	{
		this->resolved_level |= resolved_level;
		this->fetch_level &= ~this->resolved_level;
		this->last_update_time = time_current();
		this->fetch_errors = 0;
	}
};

struct stock_handle_t;

bool stock_request(const stock_handle_t& handle, const stock_t** out_stock);

struct stock_handle_t
{
	hash_t id;
	string_table_symbol_t code;
	
	mutable const stock_t* ptr{ nullptr };

	const stock_t* operator*() const
	{
		if (id == 0)
			return nullptr;

		if (stock_request(*this, &ptr))
			return ptr;
		return nullptr;
	}

	operator bool() const
	{
		if (id == 0)
			return false;
		return this->operator*() != nullptr;
	}

	operator const stock_t* () const { return this->operator*(); }

	stock_t* operator->() { return (stock_t*)get(); }
	const stock_t* operator->() const { return get(); }

	const stock_t* get() const
	{
		const stock_t* s = this->operator*();
		if (s == nullptr)
		{
			static stock_t NIL{};
			return &NIL;
		}
		return s;
	}
};

/// <summary>
/// 
/// </summary>
/// <param name="handle"></param>
/// <param name="out_stock"></param>
/// <returns></returns>
bool stock_request(const stock_handle_t& handle, const stock_t** out_stock);

/// <summary>
/// 
/// </summary>
void stock_initialize();

/// <summary>
/// 
/// </summary>
void stock_shutdown();

/// <summary>
/// 
/// </summary>
/// <param name="code"></param>
/// <param name="code_length"></param>
/// <param name="stock_handle"></param>
/// <returns></returns>
status_t stock_initialize(const char* code, size_t code_length, stock_handle_t* stock_handle);

/// <summary>
/// 
/// </summary>
/// <param name="stock_handle"></param>
/// <returns></returns>
status_t stock_resolve(stock_handle_t& stock_handle, fetch_level_t fetch_levels);

/// <summary>
/// 
/// </summary>
/// <param name="symbol"></param>
/// <param name="symbol_length"></param>
/// <param name="fetch_level"></param>
/// <param name="out_stock"></param>
/// <returns></returns>
stock_handle_t stock_request(const char* symbol, size_t symbol_length, fetch_level_t fetch_level);

bool stock_update(stock_handle_t& handle, fetch_level_t fetch_level, double timeout = 15.0);
bool stock_update(const char* code, size_t code_length, stock_handle_t& handle, fetch_level_t fetch_level, double timeout = 5.0);

double stock_exchange_rate(const char* from, size_t from_length, const char* to, size_t to_length, time_t at = 0);

const day_result_t* stock_get_EOD(const stock_t* stock_data, int rel_day, bool take_last = false);
const day_result_t* stock_get_EOD(const stock_t* stock_data, time_t day_time, bool take_last = false);
