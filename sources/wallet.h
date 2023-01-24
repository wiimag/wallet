#pragma once

#include "framework/config.h"

#include <foundation/string.h>

struct table_t;
struct wallet_t;

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

typedef enum HistoryPeriod {
	WALLET_HISTORY_ALL = 0,
	WALLET_HISTORY_MONTLY,
	WALLET_HISTORY_YEARLY
} history_period_t;

struct wallet_t
{
	double funds{ 0 };

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

bool wallet_draw(wallet_t* wallet, float available_space);
void wallet_history_draw();

void wallet_save(wallet_t* wallet, config_handle_t wallet_data);

wallet_t* wallet_allocate(config_handle_t wallet_data);
void wallet_deallocate(wallet_t* wallet);
