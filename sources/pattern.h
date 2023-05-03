/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */
 
#pragma once

#include "stock.h"
#include "openai.h"

#include <framework/config.h>

struct bulk_t;

typedef int pattern_handle_t;

struct pattern_check_t
{
    bool checked{ false };
};

struct pattern_mark_t
{
    time_t date;
    bool fetched {false};
    double change_p { NAN };
};

#define PATTERN_FLEX_NONE 0
#define PATTERN_FLEX_BUY 'B'
#define PATTERN_FLEX_SKIP 'S'
#define PATTERN_FLEX_EXECUTE 'E'
#define PATTERN_FLEX_HOLD 'H'

struct pattern_flex_t
{
    int days;
    int history_index;

    double change_p;
    int high_counter;
    char state;
    double acc;
};

struct pattern_flex_medavg_t
{
    double median;
    double average;
    double medavg;
};

/*! Pattern limits. */
struct pattern_limits_t
{
    double xmin{ NAN }, xmax{ NAN };
    double ymin{ NAN }, ymax{ NAN };
};

/*! Pattern data. */
struct pattern_t
{
    string_table_symbol_t code;
    mutable stock_handle_t stock;

    time_t date { 0 };
    bool save{ false };
    bool autofit{ false };

    // On-demand computed values
    pattern_mark_t marks[12]{};
    pattern_flex_t* flex{ nullptr };
    pattern_flex_medavg_t flex_buy{};
    pattern_flex_medavg_t flex_execute{};
    double_option_t flex_low{ DNAN };
    double_option_t flex_high{ DNAN };
    double_option_t yy_ratio{ DNAN };
    double_option_t performance_ratio{ DNAN };
    double_option_t years{ DNAN };
    double_option_t average_volume_3months{ DNAN };

    // Persisted view data
    int type{ 0 };
    int range { 90 };
    bool opened{ true };
    pattern_check_t checks[8] {};
    bool show_limits{ true };
    bool extra_charts{ false };
    bool x_axis_inverted{ false };
    bool show_trend_equation{ false };
    pattern_limits_t price_limits;

    // Y./Y. data
    struct yy_t { time_t beg, end; double change_p; } *yy{ nullptr };

    // Open AI data
    string_t*                   analysis_summary{};
    openai_completion_options_t analysis_options{};

    // Notes data
    char notes[2048]{ '\0' };
    bool notes_opened{ false };

    // Fundamentals view data
    config_handle_t fundamentals;
    bool fundamentals_fetched{ false };
    bool fundamentals_dialog_opened{ false };

    // Intraday view data
    day_result_t* intradays{ nullptr };
};

pattern_handle_t pattern_find(const char* symbol, size_t symbol_length);

pattern_handle_t pattern_load(const char* symbol, size_t symbol_length);

pattern_handle_t pattern_open(const char* symbol, size_t symbol_length);

pattern_handle_t pattern_open_window(const char* symbol, size_t symbol_length);

bool pattern_menu_item(const char* symbol, size_t symbol_length);

double pattern_get_bid_price_low(pattern_handle_t pattern_handle);

double pattern_get_bid_price_high(pattern_handle_t pattern_handle);
