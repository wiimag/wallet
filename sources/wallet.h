/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */
 
#pragma once

#include "framework/config.h"

#include <foundation/string.h>

struct table_t;
struct wallet_t;
struct report_t;

typedef enum HistoryPeriod {
    WALLET_HISTORY_ALL = 0,
    WALLET_HISTORY_MONTLY,
    WALLET_HISTORY_YEARLY
} history_period_t;

struct history_t
{
    time_t date{ 0 };
    double funds{ 0 };
    double gain{ 0 };
    double investments{ 0 };
    double total_value{ 0 };
    double broker_value{ 0 };
    double other_assets{ 0 };

    bool show_edit_ui{ false };
    wallet_t* source{ nullptr };
};

struct wallet_fund_t
{
    double amount{ 0 };
    string_t currency{};
};

struct wallet_t
{
    wallet_fund_t* funds{ nullptr };

    double main_target{ 0.50 };
    double target_ask{ 0.25 };
    double profit_ask{ 0.25 };
    double average_days{ 0 };
    double total_title_sell_count{ NAN };
    double total_sell_gain_if_kept{ NAN };
    double total_sell_gain_if_kept_p{ NAN };
    double sell_average{ NAN };
    double sell_gain_average{ NAN };
    double sell_total_gain{ NAN };
    double enhanced_earnings{ NAN };
    double total_dividends{ 0 };

    history_period_t history_period{ WALLET_HISTORY_ALL };

    bool show_extra_charts{ false };
    bool show_add_historical_data_ui{ false };

    bool track_history{ false };
    string_t preferred_currency{};

    history_t* history{ nullptr };
    table_t* history_table{ nullptr };
    double* history_dates{ nullptr };
};

/*! Draw the wallet table summary. 
 * 
 *  @param[in] wallet The wallet to draw.
 *  @param[in] available_space The available space to draw in.
 * 
 *  @return True if the wallet was drawn, false otherwise.
 */
bool wallet_draw(wallet_t* wallet, float available_space);

/*! Draw the wallet history table. */
void wallet_history_draw();

/*! Save the wallet data to the config file. 
 * 
 *  @param[in] wallet The wallet to save.
 *  @param[in] wallet_data The config file to save to.
 */
void wallet_save(wallet_t* wallet, config_handle_t wallet_data);

/*! Allocate a new wallet object. 
 * 
 *  @param[in] wallet_data The config file to load from.
 * 
 *  @return The allocated wallet object.
 */
wallet_t* wallet_allocate(config_handle_t wallet_data);

/*! Deallocate a wallet object. 
 * 
 *  @param[in] wallet The wallet to deallocate.
 */
void wallet_deallocate(wallet_t* wallet);

/*! Get the total funds in the wallet. 
 * 
 *  We convert all fund to the preferred currency and sum them up.
 * 
 *  @param[in] wallet The wallet to get the total funds from.
 * 
 *  @return The total funds in the wallet.
 */
double wallet_total_funds(wallet_t* wallet);

/*! Update the wallet history with the latest data. 
 * 
 *  @param[in] wallet The wallet to update the history for.
 */
void wallet_update_history(report_t* report, wallet_t* wallet);
