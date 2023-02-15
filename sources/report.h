/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#pragma once

#include <framework/config.h>
#include <framework/option.h>
#include <framework/string_table.h>

#define HASH_REPORT static_hash_string("report", 6, 0xbaf4a5498a0604aaULL)

struct title_t;
struct table_t;
struct wallet_t;
struct report_expression_column_t;

/*! Represents a report handle that can be used to resolve a report pointer later on. */
typedef uuid_t report_handle_t;

/*! Represents a transaction of a given title within a report. 
 * 
 * @remark This is used when build the report transaction graph.
 */
struct report_transaction_t
{
    time_t date{ 0 };
    bool buy{ false };
    double qty{ 0 };
    double price{ 0 };
    title_t* title;

    double acc{ 0 };
    double rated{ 0 };
    double adjusted{ 0 };

    int rx{ 0 };
    int ry{ 0 };
};

struct report_t
{
    string_table_symbol_t name;
    config_handle_t data;
    
    // Persistent state
    uuid_t id{ 0 };
    bool dirty{ false };
    bool save{ false };
    int save_index{ 0 };
    bool opened{ true };

    // Title information
    size_t active_titles{ 0 };
    title_t** titles{ nullptr };
    report_transaction_t* transactions{ nullptr };
    double transaction_max_acc{ 0 };
    double transaction_total_sells{ 0 };

    // Report summary values
    wallet_t* wallet{ nullptr };
    tick_t summary_last_update{ 0 };
    double total_gain { 0 };
    double total_gain_p { 0 };
    double total_value{ 0 };
    double total_investment { 0 };
    double total_day_gain { 0 };
    double total_daily_average_p{ 0 };
    tick_t fully_resolved{ 0 };

    // UI data
    table_t* table;
    bool show_summary{ false };
    bool show_sold_title{ false };
    bool show_add_title_ui{ false };
    bool show_rename_ui{ false };
    bool show_order_graph{ false };

    report_expression_column_t* expression_columns{ nullptr };
};

/*! Allocate a new report. The newly allocated report will be released automatically when shutting down.
 * 
 * @param name          The name of the report.
 * @param name_length   The length of the name.
 * 
 * @return              The report handle.
 */
report_handle_t report_allocate(const char* name, size_t name_length);

/*! Returns the amount of managed reports. */
size_t report_count();

/*! Returns the report associated with the given handle. */
report_t* report_get(report_handle_t report_handle);

/*! Return the report a given index 
 * 
 * @param index     The index of the report to return.
 * 
 * @return          The report at the given index.
 */
report_t* report_get_at(unsigned int index);

/*! Loads a report from a json file stored in the user cache.
 *
 * @param report_file_path  The path to the report file.
 *
 * @return                  The report handle if loaded, otherwise an invalid handle.
 */
report_handle_t report_load(string_const_t report_file_path);

/*! Loads a report from a json file stored in the user cache.
 * 
 * @param name          The name of the report to load.
 * @param name_length   The length of the name.
 * 
 * @return              The report handle if loaded, otherwise an invalid handle.
 */
report_handle_t report_load(const char* name, size_t name_length);

/*! Saves a report back in the user cache.
 * 
 * @param report    The report to save.
 */
void report_save(report_t* report);

/*! Render the report table.
 * 
 * @param report    The report to render.
 */
void report_render(report_t* report);

/*! Render the report menu. */
void report_menu(report_t* report);

/*! Search for a given report by name.
 *
 * @param name          The name of the report to search for.
 * @param name_length   The length of the name.
 *
 * @return              The report handle if found, otherwise an invalid handle.
 */
report_handle_t report_find(const char* name, size_t name_length);

/*! Search for a given report by name (case insensitive). 
 * 
 * @param name          The name of the report to search for.
 * @param name_length   The length of the name.
 * 
 * @return              The report handle if found, otherwise an invalid handle.
 */
report_handle_t report_find_no_case(const char* name, size_t name_length);

/*! Returns true if the report handle is valid. */
bool report_handle_is_valid(report_handle_t handle);

/*! Render the report create dialog which ask the user to enter a name for the new report.
 *
 * @param show_ui   If not null, the dialog will be closed when the value is set to false.
 */
void report_render_create_dialog(bool* show_ui);

/*! Initiate a report summary update (that is displayed in the summary panel)
 * 
 * @param report    The report to update.
 */
void report_summary_update(report_t* report);

/*! Initiate a report refreshing process.
 * 
 * @param report    The report to refresh.
 * 
 * @return         True if the report is currently loading.
 */
bool report_refresh(report_t* report);

/*! Returns true if the report is currently loading. */
bool report_is_loading(report_t* report);

/*! Run a single pass on all titles to synchronize them.
 * 
 * @param report            The report to synchronize.
 * @param timeout_seconds   The maximum amount of time to spend on the synchronization.
 */
bool report_sync_titles(report_t* report, double timeout_seconds = 60.0);

/*! Sort all loaded reports from their usage. */
void report_sort_order();

/*! Renders report dialog window.
 *
 * @param name      The name of the dialog.
 * @param show_ui   If not null, the dialog will be closed when the value is set to false.
 * @param flags     ImGui window flags.
 * 
 * @return          True if the dialog is still open, false otherwise.
 */
bool report_render_dialog_begin(string_const_t name, bool* show_ui, unsigned int flags = /*ImGuiWindowFlags_NoSavedSettings*/1 << 8);

/*! Ends the rendering of a report dialog window.
 *
 * @param show_ui   If not null, the dialog will be closed when the value is set to false.
 * 
 * @return          True if the dialog is still open, false otherwise.
 */
bool report_render_dialog_end(bool* show_ui = nullptr);

/*! Render the report graph window.
 *
 * @param report   The report to render the graph for.
 */
void report_graph_render(report_t* report);

/*! Open the report transaction graph window.
 * 
 * @param report   The report to render the graph for. 
 */
void report_graph_show_transactions(report_t* report);

/*! Opens and renders the buy title lot dialog.
 *
 * @param report    The report to render the buy lot dialog for.
 * @param title     The title to render the buy lot dialog for.
 */
void report_render_buy_lot_dialog(report_t* report, title_t* title);

/*! Opens and renders the sell title lot dialog.
 *
 * @param report    The report to render the sell lot dialog for.
 * @param title     The title to render the sell lot dialog for.
 */
void report_render_sell_lot_dialog(report_t* report, title_t* title);

/*! Render the details of a title.
 *
 * @param report    The report to render the title details for.
 * @param title     The title to render the details for.
 */
void report_render_title_details(report_t* report, title_t* title);

/*! Add a new title to a report.
 *
 * @param report        The report to add the title to.
 * @param code          The code of the title to add.
 * @param code_length   The length of the code.
 * @return The newly added title.
 */
title_t* report_add_title(report_t* report, const char* code, size_t code_length);

/*! Return all loaded reports sorted alphabetically. 
 * 
 * @return  The sorted reports. The list must be deallocated with #array_deallocate
 */
report_t** report_sort_alphabetically();
