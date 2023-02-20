/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#pragma once

#include "stock.h"

#include "framework/config.h"

#include <foundation/string.h>

struct wallet_t;

const fetch_level_t TITLE_MINIMUM_FETCH_LEVEL =
    FetchLevel::REALTIME |
    FetchLevel::EOD |
    FetchLevel::FUNDAMENTALS;

const fetch_level_t INDEX_MINIMUM_FETCH_LEVEL =
    FetchLevel::REALTIME |
    FetchLevel::EOD;
    
FOUNDATION_ALIGNED_STRUCT(title_t, 8) 
{
    char code[64]{ "" };
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

title_t* title_allocate(wallet_t* wallet = nullptr, const config_handle_t& data = nullptr);

void title_deallocate(title_t*& title);

double title_get_total_investment(const title_t* t);

double title_get_day_change(const title_t* t, const stock_t* s);

double title_get_total_value(const title_t* t);

double title_get_total_gain(const title_t* t);

double title_get_total_gain_p(const title_t* t);

double title_get_yesterday_change(const title_t* t, const stock_t* s);

double title_get_range_change_p(const title_t* t, const stock_t* s, int rel_days, bool take_last = false);

config_handle_t title_get_fundamental_config_value(title_t* title, const char* filter_name, size_t filter_name_length);

void title_init(title_t* t, wallet_t* wallet, const config_handle_t& data);

bool title_refresh(title_t* title);

fetch_level_t title_minimum_fetch_level(const title_t* t);

bool title_is_resolved(const title_t* t);

bool title_update(title_t* t, double timeout = 3.0);

bool title_is_index(const title_t* t);

bool title_has_increased(const title_t* t, double* delta = nullptr, double since_seconds = 15.0 * 60.0, double* elapsed_seconds = nullptr);
bool title_has_decreased(const title_t* t, double* out_delta = nullptr, double since_seconds = 15.0 * 60.0, double* elapsed_seconds = nullptr);

time_t title_get_last_transaction_date(const title_t* t, time_t* date = nullptr);
time_t title_get_first_transaction_date(const title_t* t, time_t* date = nullptr);

bool title_sold(const title_t* title);

bool title_has_transactions(const title_t* title);

double title_get_bought_price(const title_t* title);

double title_get_sell_gain_rated(const title_t* title);
