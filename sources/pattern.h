/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#pragma once

#include "bulk.h"
#include "stock.h"

#include "framework/config.h"

typedef int pattern_handle_t;

struct pattern_check_t
{
    bool checked{ false };
};

struct pattern_mark_t
{
    time_t date;
    bool fetched {false};
    double change_p { DNAN };
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

struct pattern_limits_t
{
    double xmin{ NAN }, xmax{ NAN };
    double ymin{ NAN }, ymax{ NAN };
};

struct pattern_t
{
    string_table_symbol_t code;
    stock_handle_t stock;

    time_t date { 0 };
    bool save{ true };
    bool autofit{ false };

    // Computed values
    pattern_mark_t marks[12]{};
    pattern_flex_t* flex{ nullptr };
    pattern_flex_medavg_t flex_buy{};
    pattern_flex_medavg_t flex_execute{};
    double_option_t flex_low{ DNAN };
    double_option_t flex_high{ DNAN };

    // Persisted data
    bool opened{ true };
    pattern_check_t checks[8] {};
    bool extra_charts{ false };
    bool show_limits{ true };
    bool x_axis_inverted{ false };
    int range { 90 };
    int type{ 0 };
    pattern_limits_t price_limits;
    char notes[2048]{ '\0' };

    // LCF
    tick_t lcf_fetch_time{ 0 };
    job_t* lcf_job{ nullptr };
    pattern_lcf_t* lcf{ nullptr };
};

pattern_handle_t pattern_find(const char* code, size_t code_length);

pattern_handle_t pattern_load(const char* code, size_t code_length);

pattern_handle_t pattern_open(const char* code, size_t code_length);

pattern_t* pattern_get(pattern_handle_t handle);

size_t pattern_count();

string_const_t pattern_get_user_file_path();

void pattern_menu(pattern_handle_t handle);
void pattern_render(pattern_handle_t handle);

void pattern_initialize();

void pattern_shutdown();
