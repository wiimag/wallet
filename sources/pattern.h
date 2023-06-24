/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 *
 * Stock pattern module.
 *
 * A stock pattern loads and aggregate data from different sources to provide
 * a complete view of a stock. It is used to analyze a stock and make decisions
 * about it.
 *
 * Currently a pattern is not contextual to a report.
 */
 
#pragma once

#include "stock.h"
#include "openai.h"

#include <framework/config.h>

struct bulk_t;
struct watch_context_t;

/*! Pattern handle. */
typedef int pattern_handle_t;

/*! Pattern check mark state. */
struct pattern_check_t
{
    bool checked{ false };
};

/*! Pattern mark. */
struct pattern_mark_t
{
    time_t date;
    bool fetched {false};
    double change_p { DNAN };
};

/*! Pattern flex entry state. */
struct pattern_flex_t
{
    int days;
    int history_index;

    double change_p;
    int high_counter;
    char state;
    double acc;
};

/*! Pattern flex median/average. */
struct pattern_flex_medavg_t
{
    double median;
    double average;
    double medavg;
};

/*! Pattern limits. */
struct pattern_limits_t
{
    double xmin{ DNAN }, xmax{ DNAN };
    double ymin{ DNAN }, ymax{ DNAN };
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

    // Watch points
    watch_context_t* watch_context{ nullptr };
};

/*! Finds and return a pattern handle. 
 *
 *  @param symbol        The symbol to find.
 *  @param symbol_length The length of the symbol.
 *
 *  @return The pattern handle.
 */
pattern_handle_t pattern_find(const char* symbol, size_t symbol_length);

/*! Loads a pattern. 
 *
 *  @param symbol        The symbol to load.
 *  @param symbol_length The length of the symbol.
 *
 *  @return The pattern handle.
 */
pattern_handle_t pattern_load(const char* symbol, size_t symbol_length);

/*! Loads and open a stock pattern into a tab of the main window. 
 *
 *  @param symbol        The symbol to load.
 *  @param symbol_length The length of the symbol.
 *
 *  @return The pattern handle.
 */
pattern_handle_t pattern_open(const char* symbol, size_t symbol_length);

/*! Loads and opens a stock pattern floating window. 
 *
 *  @param symbol        The symbol to load.
 *  @param symbol_length The length of the symbol.
 *
 *  @return The pattern handle.
 */
pattern_handle_t pattern_open_window(const char* symbol, size_t symbol_length);

/*! Loads and opens the stock pattern watch window. 
 *
 *  @param symbol        The symbol to load.
 *  @param symbol_length The length of the symbol.
 *
 *  @return The pattern handle.
 */
void pattern_open_watch_window(const char* symbol, size_t symbol_length);

/*! Renders pattern contextual menu items. 
 *
 *  @remark It loads a new pattern if the symbol is not found.
 *
 *  @param symbol        The symbol to render.
 *  @param symbol_length The length of the symbol.
 *  @param show_all      Whether to show all items.
 *
 *  @return True if the menu was rendered.
 */
bool pattern_contextual_menu(const char* symbol, size_t symbol_length, bool show_all = true);

/*! Computes a stock pattern FLEX lowest bid price. 
 *
 *  @param pattern_handle The pattern handle.
 *
 *  @return The lowest bid price.
 */
double pattern_get_bid_price_low(pattern_handle_t pattern_handle);

/*! Computes a stock pattern FLEX highest bid price. 
 *
 *  @param pattern_handle The pattern handle.
 *
 *  @return The highest bid price.
 */
double pattern_get_bid_price_high(pattern_handle_t pattern_handle);
