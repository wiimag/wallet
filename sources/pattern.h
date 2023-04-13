/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#pragma once

#include "stock.h"
#include "openai.h"

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

struct pattern_lcf_t
{
    time_t date{ 0 };
    bulk_t* symbols{ nullptr };

    int type{ 0 };
};

struct pattern_lcf_symbol_t
{
    string_table_symbol_t code;
    const bulk_t* bulk{ nullptr };
    char* sequence{ nullptr };
    int matches{ 0 };
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

    // Computed values
    pattern_mark_t marks[12]{};
    pattern_flex_t* flex{ nullptr };
    pattern_flex_medavg_t flex_buy{};
    pattern_flex_medavg_t flex_execute{};
    double_option_t flex_low{ DNAN };
    double_option_t flex_high{ DNAN };
    double_option_t yy_ratio{ DNAN };
    double_option_t performance_ratio{ DNAN };
    double_option_t years{ DNAN };
    mutable double_option_t average_volume_3months{ DNAN };

    // Persisted data
    bool opened{ true };
    pattern_check_t checks[8] {};
    bool extra_charts{ false };
    bool show_limits{ true };
    bool x_axis_inverted{ false };
    int range { 90 };
    float percent{ 90 };
    int type{ 0 };
    pattern_limits_t price_limits;
    char notes[2048]{ '\0' };

    // LCF
    job_t* lcf_job{ nullptr };
    pattern_lcf_t* lcf{ nullptr };
    pattern_lcf_symbol_t* lcf_symbols{ nullptr };

    // Y./Y. data
    struct yy_t
    {
        time_t beg, end;
        double change_p;
    } *yy{ nullptr };

    string_t*                   analysis_summary{};
    openai_completion_options_t analysis_options{};

    bool notes_opened{ false };
};

pattern_handle_t pattern_find(const char* symbol, size_t symbol_length);

pattern_handle_t pattern_load(const char* symbol, size_t symbol_length);

pattern_handle_t pattern_open(const char* symbol, size_t symbol_length);

pattern_handle_t pattern_open_window(const char* symbol, size_t symbol_length);

bool pattern_menu_item(const char* symbol, size_t symbol_length);

double pattern_get_bid_price_low(pattern_handle_t pattern_handle);

double pattern_get_bid_price_high(pattern_handle_t pattern_handle);
