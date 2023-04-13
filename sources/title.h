/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 *
 * TODO: 
 *  - Remove the `get` prefix
 *  - Add better title value caching to prevent unnecessary re-calculation each frame.
 */
 
#pragma once

#include "stock.h"

#include <framework/config.h>

#include <foundation/string.h>

struct wallet_t;

const fetch_level_t TITLE_MINIMUM_FETCH_LEVEL =
    FetchLevel::REALTIME |
    FetchLevel::EOD |
    FetchLevel::FUNDAMENTALS;

const fetch_level_t INDEX_MINIMUM_FETCH_LEVEL =
    FetchLevel::REALTIME |
    FetchLevel::EOD;

/*! The title structure is used to store information about a given title. 
 *  A title is owned by a report and tracks all the transaction made for a given title.
 *
 *  @remark Some value are cached and need to be invalidated when the underlying data changes (i.e. report wallet targets).
 */
FOUNDATION_ALIGNED_STRUCT(title_t, 8) 
{
    char code[32]{ "" };
    size_t code_length{ 0 };

    stock_handle_t stock{ 0 };
    wallet_t* wallet{ nullptr };
    config_handle_t data{ nullptr };

    // Trace how many buy and sell orders have been made
    double buy_total_count{ NAN };
    double sell_total_count{ NAN };

    // Trace buy total amounts without split or price adjusted (raw quantities)
    double buy_total_price{ NAN };
    double buy_total_quantity{ NAN };

    // Trace sell total amounts without split or price adjusted (raw quantities)
    double sell_total_price{ NAN };
    double sell_total_quantity{ NAN };

    // Price with preferred exchange rate (i.e. USD > CAD), but without split
    // Quantities are the same as for raw quantities above (i.e. ##buy_total_quantity, ##sell_total_quantity)
    double buy_total_price_rated{ NAN };
    double sell_total_price_rated{ NAN };

    double buy_total_price_rated_adjusted{ NAN };
    double sell_total_price_rated_adjusted{ NAN };

    // Price and quantity adjusted with split
    double buy_total_adjusted_qty{ NAN };
    double buy_total_adjusted_price{ NAN };
    double sell_total_adjusted_qty{ NAN };
    double sell_total_adjusted_price{ NAN };

    double buy_adjusted_price{ NAN };
    double sell_adjusted_price{ NAN };

    // Average price (adjusted but not exchange rated)
    double average_price{ NAN };
    double average_price_rated{ NAN };
    double average_quantity{ NAN };
    double average_buy_price{ NAN };
    double average_buy_price_rated{ NAN };
    double remaining_shares{ NAN };
    
    double total_dividends{ NAN };
    double average_ask_price{ NAN };
    double average_exchange_rate{ 1 };
    
    time_t date_min{ 0 };
    time_t date_max{ 0 };
    time_t date_average{ 0 };
    double elapsed_days{ NAN };
    
    double_option_t today_exchange_rate{ 1.0 };
    double_option_t ps{ DNAN };
    double_option_t ask_price{ DNAN };

    bool show_buy_ui{ false };
    bool show_sell_ui{ false };
    bool show_details_ui{ false };
};

/*! Allocates a new title to be assigned to a report wallet.
 * 
 *  @param wallet The wallet to which the title belongs.
 *  @param data   The configuration data for the title.
 *
 *  @remark The title is not initialized, it is the caller's responsibility to do so.
 *          Also the title memory is owned by the caller.
 *
 *  @return A new title.
 */
title_t* title_allocate(wallet_t* wallet = nullptr, const config_handle_t& data = nullptr);

/*! Deallocates a title.
 *
 *  @param title The title to deallocate.
 */
void title_deallocate(title_t*& title);

/*! Get the title total investment.
 *
 *  @param t The title to get the total investment for.
 *
 *  @return The total investment.
 */
double title_get_total_investment(const title_t* t);

/*! Get the title percentage day change.
 *
 *  @remark We take into account the exchange rate to get the percentage day change.
 *
 *  @param t The title to get the percentage day change for.
 *  @param s The stock to get the percentage day change for.
 *
 *  @return The percentage day change.
 */
double title_get_day_change(const title_t* t, const stock_t* s);

/*! Get the total value by multiplying the current price by the number of shares.
 *
 *  @param t The title to get the total value for.
 *
 *  @return The total value.
 */
double title_get_total_value(const title_t* t);

/*! Get the total gain by subtracting the total investment from the total value.
 *
 *  @param t The title to get the total gain for.
 *
 *  @return The total gain.
 */
double title_get_total_gain(const title_t* t);

/*! Get the total gain percentage by dividing the total gain by the total investment.
 *
 *  @param t The title to get the total gain percentage for.
 *
 *  @return The total gain percentage.
 */
double title_get_total_gain_p(const title_t* t);

/*! Get the title percentage day change (compared to yesterday, not market open). 
 *
 *  @remark We take into account the exchange rate to get the percentage day change.
 *
 *  @param t The title to get the percentage day change for.
 *  @param s The stock to get the percentage day change for.
 *
 *  @return The percentage day change.
 */
double title_get_yesterday_change(const title_t* t, const stock_t* s);

/*! Get the title percentage change from the given number of days ago. 
 *
 *  @remark We take into account the exchange rate to get the percentage day change.
 *
 *  @param t The title to get the percentage day change for.
 *  @param s The stock to get the percentage day change for.
 *  @param rel_days The number of days ago to get the percentage change from.
 *
 *  @return The percentage day change.
 */
double title_get_range_change_p(const title_t* t, const stock_t* s, int rel_days, bool take_last = false);

/*! Fetch the title stock fundamental value from the given filter name.
 *
 *  @param title The title to fetch the fundamental value for.
 *  @param filter_name The filter name to fetch the fundamental value for.
 *  @param filter_name_length The length of the filter name.
 *
 *  @remark The return data can be null.
 *
 *  @return The fundamental value.
 */
config_handle_t title_get_fundamental_config_value(title_t* title, const char* filter_name, size_t filter_name_length);

/*! Initialize a title with the given configuration data.
 *
 *  @param t The title to initialize.
 *  @param wallet The wallet to which the title belongs.
 *  @param data The configuration data for the title.
 *
 *  @remark We unroll all title transactions to compute the title statistics.
 */
void title_init(title_t* t, wallet_t* wallet, const config_handle_t& data);

/*! Refresh the title data.
 *
 *  We fetch the stock data and then refresh the title. You need to recall this function
 *  to refresh the title data when the report wallet targets changes.
 *
 *  @param title The title to refresh.
 *
 *  @return True if the title was refreshed, false otherwise.
 */
bool title_refresh(title_t* title);

/*! Return the minimal stock fetch level for this title. 
 *
 *  @remark We take into account the title type and the stock type.
 *
 *  @param t The title to get the minimal stock fetch level for.
 *
 *  @return The minimal stock fetch level.
 */
fetch_level_t title_minimum_fetch_level(const title_t* t);

/*! Checks if the title stock and stats are fully resolved. 
 *
 *  Note that the title initialization is an async process done on multiple frames.
 *
 *  @param t The title to check if it is resolved.
 *
 *  @return True if the title is resolved, false otherwise.
 */
bool title_is_resolved(const title_t* t);

/*! Checks if the title is up-to-date, if not, then we fetch the stock data and re-initialize the title.
 *
 *  @param t The title to check if it is a stock.
 *
 *  @return True if the title got updated, false otherwise.
 */
bool title_update(title_t* t, double timeout = 3.0);

/*! Checks if the title is used as an index. 
 *
 *  @param t The title to check if it is an index.
 *
 *  @return True if the title is an index, false otherwise.
 */
bool title_is_index(const title_t* t);

/*! Checks if the title stock price has increased since last update. 
 *
 *  @param t               The title to check if it has increased.
 *  @param delta           The delta value.
 *  @param since_seconds   The number of seconds since the last update.
 *  @param elapsed_seconds The elapsed seconds since the last update.
 *
 *  @return True if the title has increased, false otherwise.
 */
bool title_has_increased(const title_t* t, double* delta = nullptr, double since_seconds = 15.0 * 60.0, double* elapsed_seconds = nullptr);

/*! Checks if the title stock price has decreased since last update. 
 *
 *  @param t               The title to check if it has decreased.
 *  @param delta           The delta value.
 *  @param since_seconds   The number of seconds since the last update.
 *  @param elapsed_seconds The elapsed seconds since the last update.
 *
 *  @return True if the title has decreased, false otherwise.
 */
bool title_has_decreased(const title_t* t, double* out_delta = nullptr, double since_seconds = 15.0 * 60.0, double* elapsed_seconds = nullptr);

/*! Get the title last transaction date.
 *
 *  @param t The title to get the last transaction date for.
 *  @param date The last transaction date.
 *
 *  @return The last transaction date.
 */
time_t title_get_last_transaction_date(const title_t* t, time_t* date = nullptr);

/*! Get the title first transaction date.
 *
 *  @param t The title to get the first transaction date for.
 *  @param date The first transaction date.
 *
 *  @return The first transaction date.
 */
time_t title_get_first_transaction_date(const title_t* t, time_t* date = nullptr);

/*! Checks if the title is fully sold.
 * 
 *  Note that we check if the title is fully sold by checking if the title has transactions and if the title current price is NAN.
 *
 *  @param t The title to check if it is fully sold.
 *
 *  @return True if the title is fully sold, false otherwise.
 */
bool title_sold(const title_t* title);

/*! Checks if the title has transactions.
 *
 *  @param t The title to check if it has transactions.
 *
 *  @return True if the title has transactions, false otherwise.
 */
bool title_has_transactions(const title_t* title);

/*! Compute the title average cost after buying and selling stock. 
 *
 *  @param t The title to compute the average cost for.
 *
 *  @return The average cost.
 */
double title_get_bought_price(const title_t* title);

/*! Compute the gain when selling the title. 
 *
 *  @param t The title to compute the gain for.
 *
 *  @return The gain.
 */
double title_get_sell_gain_rated(const title_t* title);

/*! Compute the price target to sell the title. 
 *
 *  @remark The price is computed based on the moving report wallet targets.
 *
 *  @param t The title to compute the price target for.
 *
 *  @return The price target.
 */
double title_get_ask_price(const title_t* title);

/*! Get the title current price or NAN if not yet available.
 *
 *  @param title The title to get the current price for.
 *
 *  @return The current price or NAN if not yet available.
 */
double title_current_price(const title_t* title);
