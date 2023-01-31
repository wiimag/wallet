/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#pragma once

#include "stock.h"

#include "framework/config.h"

#include <foundation/string.h>

struct wallet_t;

FOUNDATION_ALIGNED_STRUCT(title_t, 8) 
{
    char code[64]{ "" };
    size_t code_length{ 0 };
    config_handle_t data{ nullptr };

    double buy_total_price{ 0 };
    double buy_total_quantity{ 0 };
    double buy_total_price_rated{ 0 };

    double sell_total_price{ 0 };
    double sell_total_quantity{ 0 };
    double sell_total_price_rated{ 0 };

    double buy_adjusted_price{ NAN };
    double buy_adjusted_quantity{ NAN };
    double sell_adjusted_price{ NAN };
    double sell_adjusted_quantity{ NAN };

    double average_price{ 0 };
    double average_price_rated{ 0 };
    double average_quantity{ 0 };
    double average_ask_price{ 0 };

    double total_dividends{ 0 };

    time_t date_min{ 0 };
    time_t date_max{ 0 };
    time_t date_average{ 0 };
    double elapsed_days{ 0 };

    wallet_t* wallet{ nullptr };
    stock_handle_t stock{ 0 };
    double_option_t exchange_rate{ 1.0 };
    double_option_t today_exchange_rate{ 1.0 };
    double_option_t ps{ DNAN };
    double_option_t ask_price{ DNAN };

    bool show_buy_ui{ false };
    bool show_sell_ui{ false };
    bool show_details_ui{ false };
};

title_t* title_allocate();

void title_deallocate(title_t*& title);

double title_get_total_investment(const title_t* t);

double title_get_day_change(const title_t* t, const stock_t* s);

double title_get_total_value(const title_t* t, const stock_t* s);

double title_get_total_gain(const title_t* t, const stock_t* s);

double title_get_total_gain_p(const title_t* t, const stock_t* s);

double title_get_yesterday_change(const title_t* t, const stock_t* s);

double title_get_range_change_p(const title_t* t, const stock_t* s, int rel_days, bool take_last = false);

config_handle_t title_get_fundamental_config_value(title_t* title, const char* filter_name, size_t filter_name_length);

void title_init(wallet_t* wallet, title_t* t, const config_handle_t& data);

bool title_refresh(title_t* title);

bool title_update(title_t* t, double timeout = 3.0);

bool title_is_index(const title_t* t);

bool title_has_increased(const title_t* t, double* delta = nullptr, double since_seconds = 15.0 * 60.0, double* elapsed_seconds = nullptr);
bool title_has_decreased(const title_t* t, double* out_delta = nullptr, double since_seconds = 15.0 * 60.0, double* elapsed_seconds = nullptr);

