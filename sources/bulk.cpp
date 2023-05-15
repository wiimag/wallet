/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */

#include "bulk.h"

#if BUILD_APPLICATION

#include "eod.h"
#include "pattern.h"
#include "imwallet.h"

#include <framework/session.h>
#include <framework/scoped_mutex.h>
#include <framework/table.h>
#include <framework/module.h>
#include <framework/config.h>
#include <framework/window.h>
#include <framework/system.h>
#include <framework/dispatcher.h>
#include <framework/shared_mutex.h>
#include <framework/jobs.h>
#include <framework/string_builder.h>

#define HASH_BULK static_hash_string("bulk", 4, 0x9a6818bbbd28c09eULL)

static struct BULK_MODULE
{
    time_t fetch_date = time_work_day(time_now(), -0.7);
    tm fetch_date_tm = *localtime(&fetch_date);

    time_t start_extract_date = time_work_day(time_now(), -7);
    tm start_extract_date_tm = *localtime(&start_extract_date);

    shared_mutex_t lock;
    bulk_t* symbols{ nullptr };
    table_t* table{ nullptr };
    string_t* exchanges{ nullptr };

    bool fetch_cap_zero{ false };
    bool fetch_volume_zero{ false };
    bool fetch_negative_beta{ false };

    char search_filter[64]{ 0 };

    string_t extractor_path{};
    job_t** extractor_jobs{ nullptr };
    config_handle_t extractor_cv{ nullptr };

} *_bulk_module;

//
// # IMPLEMENTATION
//

FOUNDATION_STATIC bool bulk_add_symbols(const bulk_t* batch)
{
    size_t bz = array_size(batch);

    SHARED_WRITE_LOCK(_bulk_module->lock);
    size_t cz = array_size(_bulk_module->symbols);
    array_resize(_bulk_module->symbols, cz + bz);
    memcpy(_bulk_module->symbols + cz, batch, sizeof(bulk_t) * bz);
    return true;
}

FOUNDATION_STATIC void bulk_fetch_exchange_symbols(const json_object_t& json)
{
    if (json.root->value_length == 0)
        return;

    bulk_t* batch = nullptr;
    for (int i = 0, end = json.root->value_length; i != end; ++i)
    {
        json_object_t e = json[i];
        bulk_t s{};
        s.market_capitalization = e["MarketCapitalization"].as_number();
        if (s.market_capitalization == 0 && !_bulk_module->fetch_cap_zero)
            continue;

        s.volume = e["volume"].as_number();
        s.avgvol_200d = e["avgvol_200d"].as_number();
        if (s.avgvol_200d == 0 && s.volume == 0 && !_bulk_module->fetch_volume_zero)
            continue;

        s.beta = e["Beta"].as_number();
        if (s.beta < 0.01 && !_bulk_module->fetch_negative_beta)
            continue;

        s.avgvol_14d = e["avgvol_14d"].as_number();
        s.avgvol_50d = e["avgvol_50d"].as_number();

        string_const_t code = e["code"].as_string();
        string_const_t ex = e["exchange_short_name"].as_string();
        code = string_format_static(STRING_CONST("%.*s.%.*s"), STRING_FORMAT(code), STRING_FORMAT(ex));

        s.date = string_to_date(STRING_ARGS(e["date"].as_string()));
        s.code = string_table_encode(code);
        s.name = string_table_encode_unescape(e["name"].as_string());
        s.type = string_table_encode(e["type"].as_string());
        s.exchange = string_table_encode(ex);

        s.open = e["open"].as_number();
        s.high = e["high"].as_number();
        s.low = e["low"].as_number();
        s.close = e["close"].as_number();
        s.adjusted_close = e["adjusted_close"].as_number();
        s.ema_50d = e["ema_50d"].as_number();
        s.ema_200d = e["ema_200d"].as_number();
        s.hi_250d = e["hi_250d"].as_number();
        s.lo_250d = e["lo_250d"].as_number();

        s.selected = pattern_find(STRING_ARGS(code)) >= 0;

        array_push(batch, s);
        if (array_size(batch) > 999)
        {
            if (bulk_add_symbols(batch))
                array_clear(batch);
        }
    }

    bulk_add_symbols(batch);
    array_deallocate(batch);
}

FOUNDATION_STATIC void bulk_load_symbols()
{
    {
        SHARED_WRITE_LOCK(_bulk_module->lock);
        array_clear(_bulk_module->symbols);
    }

    for (int i = 0, end = array_size(_bulk_module->exchanges); i != end; ++i)
    {
        const string_t& code = _bulk_module->exchanges[i];
        if (!eod_fetch_async("eod-bulk-last-day", code.str, FORMAT_JSON_CACHE,
            "date", string_from_date(_bulk_module->fetch_date).str,
            "filter", "extended", bulk_fetch_exchange_symbols, 4 * 60 * 60ULL))
        {
            log_errorf(0, ERROR_EXCEPTION, STRING_CONST("Failed to fetch %s bulk data"), code);
        }
    }
}

FOUNDATION_STATIC string_const_t bulk_get_symbol_code(const bulk_t* b)
{
    return string_table_decode_const(b->code);
}

FOUNDATION_STATIC cell_t bulk_column_symbol_code(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        string_const_t code = SYMBOL_CONST(b->code);
        const ImRect& cell_rect = table_current_cell_rect();
        logo_render_banner(STRING_ARGS(code), cell_rect, nullptr);
    }
    
    return bulk_get_symbol_code(b);
}

FOUNDATION_STATIC cell_t bulk_column_symbol_name(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->name;
}

FOUNDATION_STATIC cell_t bulk_column_symbol_date(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->date;
}

FOUNDATION_STATIC cell_t bulk_column_symbol_type(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->type;
}

FOUNDATION_STATIC cell_t bulk_column_symbol_exchange(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->exchange;
}

FOUNDATION_STATIC void bulk_column_today_cap_tooltip(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    bulk_t* b = (bulk_t*)element;

    if (!b->today_cap)
    {
        string_const_t code = bulk_get_symbol_code(b);
        if (stock_update(STRING_ARGS(code), b->stock_handle, FetchLevel::EOD))
        {
            size_t n = 0;
            double a = 0;
            const time_t today = time_now();
            const day_result_t* history = b->stock_handle->history;
            while (n < array_size(history) && time_elapsed_days(history[n].date, today) <= 14.0)
            {
                a += history[n].volume * (history[n].adjusted_close - history[n].open);
                n++;
            }
            b->today_cap = a / n;
        }
    }

    ImGui::TrText("Average capitalization movement since 14 days\n%.*s", STRING_FORMAT(string_from_currency(b->today_cap.fetch(), "9 999 999 999 $")));
}

FOUNDATION_STATIC cell_t bulk_column_today_cap(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->avgvol_14d * (b->close - b->open);
}

FOUNDATION_STATIC cell_t bulk_column_symbol_cap(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->market_capitalization;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_beta(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->beta * 100.0;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_open(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->open;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_close(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->adjusted_close;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_low(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->low;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_high(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->high;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_volume(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->volume;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_ema_50d(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->ema_50d;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_ema_p(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return (b->ema_50d - b->adjusted_close) / b->close * 100.0;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_change_p(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return (b->close - b->open) / b->open * 100.0;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_lost_cap(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->market_capitalization * bulk_draw_symbol_change_p(element, column).number / 100.0;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_ema_200d(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->ema_200d;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_lo_250d(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->lo_250d;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_hi_250d(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->hi_250d;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_avgvol_14d(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->avgvol_14d;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_avgvol_50d(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->avgvol_50d;
}

FOUNDATION_STATIC cell_t bulk_draw_symbol_avgvol_200d(table_element_ptr_t element, const column_t* column)
{
    bulk_t* b = (bulk_t*)element;
    return b->avgvol_200d;
}

FOUNDATION_STATIC void bulk_table_context_menu(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    if (element == nullptr)
        return ImGui::CloseCurrentPopup();

    bulk_t* b = (bulk_t*)element;

    string_const_t code = bulk_get_symbol_code(b);
    if (pattern_contextual_menu(STRING_ARGS(code)))
    {

    }
}

FOUNDATION_STATIC void bulk_column_title_selected(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    bulk_t* b = (bulk_t*)element;
    string_const_t code = bulk_get_symbol_code(b);
    pattern_open(STRING_ARGS(code));
    b->selected = true;
}

FOUNDATION_STATIC void bulk_draw_symbol_code_color(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)
{
    bulk_t* b = (bulk_t*)element;
    if (b->selected || (b->beta > 1 && b->close > b->open))
    {
        style.types |= COLUMN_COLOR_TEXT;
        style.text_color = ImColor::HSV(!b->selected ? 0.4f : 0.6f, 0.3f, 0.9f);
    }
}

FOUNDATION_STATIC void bulk_set_beta_styling(table_element_ptr_const_t element, const column_t* column, const cell_t* cell, cell_style_t& style)
{
    bulk_t* b = (bulk_t*)element;
    if (b->beta > 1)
    {
        style.types |= COLUMN_COLOR_BACKGROUND | COLUMN_COLOR_TEXT;
        style.text_color = ImColor(0.051f, 0.051f, 0.051f);
        style.background_color = ImColor(218 / 255.0f, 234 / 255.0f, 210 / 255.0f);
    }
}

FOUNDATION_STATIC bool bulk_table_search(table_element_ptr_const_t element, const char* filter, size_t filter_length)
{
    bulk_t* b = (bulk_t*)element;

    string_const_t code = bulk_get_symbol_code(b);
    if (string_contains_nocase(STRING_ARGS(code), filter, filter_length))
        return true;

    string_const_t name = string_table_decode_const(b->name);
    if (string_contains_nocase(STRING_ARGS(name), filter, filter_length))
        return true;

    return false;
}

FOUNDATION_STATIC void bulk_create_symbols_table()
{
    if (_bulk_module->table)
        table_deallocate(_bulk_module->table);

    _bulk_module->table = table_allocate("Bulk##_2", TABLE_HIGHLIGHT_HOVERED_ROW | TABLE_LOCALIZATION_CONTENT);
    _bulk_module->table->context_menu = bulk_table_context_menu;
    _bulk_module->table->search = bulk_table_search;

    table_add_column(_bulk_module->table, STRING_CONST("Title"), bulk_column_symbol_code, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING)
        .set_selected_callback(bulk_column_title_selected);

    table_add_column(_bulk_module->table, STRING_CONST("Name"), 
        bulk_column_symbol_name, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT)
        .set_selected_callback(bulk_column_title_selected)
        .set_style_formatter(bulk_draw_symbol_code_color);

    table_add_column(_bulk_module->table, STRING_CONST("Type"), bulk_column_symbol_type, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE);
    table_add_column(_bulk_module->table, STRING_CONST("Ex.||Exchange"), bulk_column_symbol_exchange, COLUMN_FORMAT_SYMBOL, COLUMN_SORTABLE | COLUMN_MIDDLE_ALIGN);

    table_add_column(_bulk_module->table, STRING_CONST(ICON_MD_EXPAND " Cap.||Moving Capitalization"), bulk_column_today_cap, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_NUMBER_ABBREVIATION | COLUMN_HIDE_DEFAULT)
        .set_tooltip_callback(bulk_column_today_cap_tooltip);

    table_add_column(_bulk_module->table, STRING_CONST("  Cap.||Capitalization"), bulk_column_symbol_cap, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_NUMBER_ABBREVIATION);
    table_add_column(_bulk_module->table, STRING_CONST("Lost Cap.||Lost Capitalization"), bulk_draw_symbol_lost_cap, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_NUMBER_ABBREVIATION | COLUMN_HIDE_DEFAULT);

    table_add_column(_bulk_module->table, STRING_CONST("  Beta||Beta"), bulk_draw_symbol_beta, COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE)
        .set_style_formatter(bulk_set_beta_styling);

    table_add_column(_bulk_module->table, STRING_CONST("    Open||Open"), bulk_draw_symbol_open, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE);
    table_add_column(_bulk_module->table, STRING_CONST("   Close||Close"), bulk_draw_symbol_close, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE);
    table_add_column(_bulk_module->table, STRING_CONST("     Low||Low"), bulk_draw_symbol_low, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE);
    table_add_column(_bulk_module->table, STRING_CONST("    High||High"), bulk_draw_symbol_high, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE);

    table_add_column(_bulk_module->table, STRING_CONST("    %||Day Change"), bulk_draw_symbol_change_p, COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE);
    table_add_column(_bulk_module->table, STRING_CONST("EMA %||Exponential Moving Averages Gain"), bulk_draw_symbol_ema_p, COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE);

    table_add_column(_bulk_module->table, STRING_CONST("EMA 50d||Exponential Moving Averages (50 days)"), bulk_draw_symbol_ema_50d, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);
    table_add_column(_bulk_module->table, STRING_CONST("EMA 200d||Exponential Moving Averages (200 days)"), bulk_draw_symbol_ema_200d, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);
    table_add_column(_bulk_module->table, STRING_CONST(" L. 250d||Low 250 days"), bulk_draw_symbol_lo_250d, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);
    table_add_column(_bulk_module->table, STRING_CONST(" H. 250d||High 250 days"), bulk_draw_symbol_hi_250d, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_HIDE_DEFAULT);

    table_add_column(_bulk_module->table, STRING_CONST("Volume"), bulk_draw_symbol_volume, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_NUMBER_ABBREVIATION);
    table_add_column(_bulk_module->table, STRING_CONST("V. 14d||Average Volume 14 days"), bulk_draw_symbol_avgvol_14d, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_ROUND_NUMBER | COLUMN_NUMBER_ABBREVIATION);
    table_add_column(_bulk_module->table, STRING_CONST("V. 50d||Average Volume 50 days"), bulk_draw_symbol_avgvol_50d, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_ROUND_NUMBER | COLUMN_NUMBER_ABBREVIATION | COLUMN_HIDE_DEFAULT);
    table_add_column(_bulk_module->table, STRING_CONST("V. 200d||Average Volume 200 days"), bulk_draw_symbol_avgvol_200d, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_ROUND_NUMBER | COLUMN_NUMBER_ABBREVIATION | COLUMN_HIDE_DEFAULT);
}

FOUNDATION_STATIC void bulk_initialize_exchanges()
{
    array_reserve(_bulk_module->exchanges, 8);

    string_const_t selected_exchanges_file_path = session_get_user_file_path(STRING_CONST("exchanges.json"));
    if (fs_is_file(STRING_ARGS(selected_exchanges_file_path)))
    {
        int code_hash = 0;
        config_handle_t selected_exchanges_data = config_parse_file(STRING_ARGS(selected_exchanges_file_path));
        for (auto p : selected_exchanges_data)
        {
            string_const_t code = p.as_string();
            string_t selected_code = string_clone(STRING_ARGS(code));
            array_push(_bulk_module->exchanges, selected_code);
        }

        config_deallocate(selected_exchanges_data);
    }

    if (_bulk_module->symbols == nullptr)
        bulk_load_symbols();

    if (_bulk_module->table == nullptr)
        bulk_create_symbols_table();
}

FOUNDATION_STATIC bool bulk_render_exchange_selector()
{
    bool updated = false;

    if (_bulk_module->exchanges == nullptr)
        bulk_initialize_exchanges();

    ImGui::SameLine();
    ImGui::MoveCursor(0, -2);
    ImGui::SetNextItemWidth(IM_SCALEF(200));
    if (ImWallet::Exchanges(_bulk_module->exchanges))
        updated = true;

    return updated;
}

FOUNDATION_STATIC void bulk_render()
{
    ImGui::MoveCursor(8, 8);
    ImGui::BeginGroup();
    ImGui::MoveCursor(0, -2);
    ImGui::TextUnformatted("Exchanges");

    bool exchanges_updated = bulk_render_exchange_selector();

    ImGui::MoveCursor(0, -2, true);
    ImGui::SetNextItemWidth(IM_SCALEF(150));
    if (ImGui::DateChooser("##Date", _bulk_module->fetch_date_tm, "%Y-%m-%d", true))
    {
        _bulk_module->fetch_date = mktime(&_bulk_module->fetch_date_tm);
        exchanges_updated = true;
    }

    ImGui::MoveCursor(0, -2, true);
    if (ImGui::Checkbox(tr("No capitalization"), &_bulk_module->fetch_cap_zero))
        exchanges_updated = true;

    ImGui::MoveCursor(0, -2, true);
    if (ImGui::Checkbox(tr("No Volume"), &_bulk_module->fetch_volume_zero))
        exchanges_updated = true;

    ImGui::MoveCursor(0, -2, true);
    if (ImGui::Checkbox(tr("No Beta"), &_bulk_module->fetch_negative_beta))
        exchanges_updated = true;

    if (exchanges_updated)
        bulk_load_symbols();

    if (_bulk_module->table)
    {
        // Render search filter input text
        ImGui::MoveCursor(IM_SCALEF(8), -2, true);
        ImGui::SetNextItemWidth(IM_SCALEF(200));
        if (ImGui::InputTextWithHint("##Search", tr("Filter symbols..."), STRING_BUFFER(_bulk_module->search_filter)) || exchanges_updated)
            table_set_search_filter(_bulk_module->table, STRING_LENGTH(_bulk_module->search_filter));

        {
            SHARED_READ_LOCK(_bulk_module->lock);
            int symbol_count = array_size(_bulk_module->symbols);
            ImGui::MoveCursor(0, -2, true);
            ImGui::TrText("%5d symbols", symbol_count);
        }

        ImGui::EndGroup();

        ImGui::MoveCursor(0, -2, true);
        if (ImGui::Button(tr("Export...")))
        {
            dispatch([]()
            {
                system_save_file_dialog(
                    tr("Export table to CSV..."), 
                    tr("Comma-Separated-Value (*.csv)|*.csv"), 
                    nullptr, [](string_const_t save_path)
                {
                    string_t path = string_clone(STRING_ARGS(save_path));
                    SHARED_READ_LOCK(_bulk_module->lock);
                    table_export_csv(_bulk_module->table, STRING_ARGS(path));
                    string_deallocate(path.str);
                    return true;
                });
            });
        }

        SHARED_READ_LOCK(_bulk_module->lock);
        int symbol_count = array_size(_bulk_module->symbols);
        table_render(_bulk_module->table, _bulk_module->symbols, symbol_count, sizeof(bulk_t), 0.0f, 0.0f);
    }
}

FOUNDATION_STATIC void bulk_open_window()
{
    auto window = window_open("bulk_last_day", STRING_CONST("Last Day Results"),
        L1(bulk_render()), nullptr, nullptr, WindowFlags::Maximized | WindowFlags::Singleton);
    window_set_menu_render_callback(window, [](window_handle_t window_handle)
    {
        if (ImGui::BeginMenu(tr("File")))
        {
            if (ImGui::MenuItem(tr(ICON_MD_CLOSE " Close")))
                window_close(window_handle);

            ImGui::EndMenu();
        }
    });
}

FOUNDATION_STATIC void bulk_extract(string_const_t path, time_t start, time_t end)
{
    FOUNDATION_ASSERT(_bulk_module->exchanges);
    FOUNDATION_ASSERT(_bulk_module->extractor_cv == nullptr);
    FOUNDATION_ASSERT(string_is_null(_bulk_module->extractor_path));
    _bulk_module->extractor_cv = config_allocate();

    string_deallocate(_bulk_module->extractor_path);
    _bulk_module->extractor_path = string_clone(path.str, path.length);

    // Loop each day between start and end, skip weekends
    time_t current = start;
    while (current <= end)
    {
        struct tm* current_tm = localtime(&current);
        if (current_tm->tm_wday == 0 || current_tm->tm_wday == 6)
        {
            current += 86400;
            continue;
        }

        // Fetch data for current day
        job_t* j = job_execute([](void* payload)
        {
            time_t ts = (time_t)(intptr_t)(payload);

            config_handle_t date_cv = nullptr;
            string_const_t datestr = string_from_date(ts);
            {
                SHARED_WRITE_LOCK(_bulk_module->lock);
                date_cv = config_set_object(_bulk_module->extractor_cv, STRING_ARGS(datestr));
            }

            for (unsigned i = 0, end = array_size(_bulk_module->exchanges); i < end; ++i)
            {
                const string_t& exchange = _bulk_module->exchanges[i];

                eod_fetch("eod-bulk-last-day", exchange.str, FORMAT_JSON_CACHE,
                    "date", datestr.str,
                    "filter", "extended", [date_cv](const json_object_t& json)
                {
                    if (!json.resolved())
                        return;

                    for (auto e : json)
                    {
                        string_const_t code = e["code"].as_string();
                        string_const_t exchange = e["exchange_short_name"].as_string();

                        char symbol_buffer[16];
                        string_t symbol = string_format(STRING_BUFFER(symbol_buffer), 
                            STRING_CONST("%.*s.%.*s"), STRING_FORMAT(code), STRING_FORMAT(exchange));

                        double MarketCapitalization = e["MarketCapitalization"].as_number();
                        double Beta = e["Beta"].as_number();
                        double open = e["open"].as_number();
                        double high = e["high"].as_number();
                        double low = e["low"].as_number();
                        double close = e["close"].as_number();
                        double adjusted_close = e["adjusted_close"].as_number();
                        double volume = e["volume"].as_number();
                        double ema_50d = e["ema_50d"].as_number();
                        double ema_200d = e["ema_200d"].as_number();
                        double hi_250d = e["hi_250d"].as_number();
                        double lo_250d = e["lo_250d"].as_number();
                        double avgvol_14d = e["avgvol_14d"].as_number();
                        double avgvol_50d = e["avgvol_50d"].as_number();
                        double avgvol_200d = e["avgvol_200d"].as_number();

                        {
                            SHARED_WRITE_LOCK(_bulk_module->lock);
                            config_handle_t symbol_cv = config_set_object(date_cv, STRING_ARGS(symbol));

                            config_set(symbol_cv, "open", open);
                            config_set(symbol_cv, "close", close);
                            config_set(symbol_cv, "price", adjusted_close);
                            config_set(symbol_cv, "volume", volume);

                            if (!math_real_is_zero(MarketCapitalization) && !math_real_is_nan(MarketCapitalization)) config_set(symbol_cv, "cap", MarketCapitalization);
                            if (!math_real_is_zero(Beta) && !math_real_is_nan(Beta)) config_set(symbol_cv, "beta", Beta);
                            if (!math_real_is_zero(high) && !math_real_is_nan(high)) config_set(symbol_cv, "high", high);
                            if (!math_real_is_zero(low) && !math_real_is_nan(low)) config_set(symbol_cv, "low", low);
                            if (!math_real_is_zero(ema_50d) && !math_real_is_nan(ema_50d)) config_set(symbol_cv, "ema_50d", ema_50d);
                            if (!math_real_is_zero(ema_200d) && !math_real_is_nan(ema_200d)) config_set(symbol_cv, "ema_200d", ema_200d);
                            if (!math_real_is_zero(hi_250d) && !math_real_is_nan(hi_250d)) config_set(symbol_cv, "hi_250d", hi_250d);
                            if (!math_real_is_zero(lo_250d) && !math_real_is_nan(lo_250d)) config_set(symbol_cv, "lo_250d", lo_250d);
                            if (!math_real_is_zero(avgvol_14d) && !math_real_is_nan(avgvol_14d)) config_set(symbol_cv, "avgvol_14d", avgvol_14d);
                            if (!math_real_is_zero(avgvol_50d) && !math_real_is_nan(avgvol_50d)) config_set(symbol_cv, "avgvol_50d", avgvol_50d);
                            if (!math_real_is_zero(avgvol_200d) && !math_real_is_nan(avgvol_200d)) config_set(symbol_cv, "avgvol_200d", avgvol_200d);
                        }
                    }
                }, 30 * 24 * 60 * 60ULL);
            }
            return 0;
        }, to_ptr(current));
        array_push(_bulk_module->extractor_jobs, j);

        // Advance to next day
        current += 86400;
    }
}

FOUNDATION_STATIC void bulk_extractor_clean_up()
{
    for (unsigned i = 0, job_count = array_size(_bulk_module->extractor_jobs); i < job_count; ++i)
        job_deallocate(_bulk_module->extractor_jobs[i]);
    array_deallocate(_bulk_module->extractor_jobs);

    config_deallocate(_bulk_module->extractor_cv);
}

FOUNDATION_STATIC void bulk_extractor_render()
{
    unsigned job_count = array_size(_bulk_module->extractor_jobs);

    ImGui::BeginDisabled(job_count > 0);
    ImGui::TrTextUnformatted("Markets");
    bulk_render_exchange_selector();

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::TrTextUnformatted("Start");
    ImGui::SameLine(IM_SCALEF(60));
    ImGui::SetNextItemWidth(IM_SCALEF(130));
    if (ImGui::DateChooser("##StartDate", _bulk_module->start_extract_date_tm, "%Y-%m-%d", true))
    {
        _bulk_module->start_extract_date = mktime(&_bulk_module->start_extract_date_tm);
    }

    ImGui::SameLine();
    ImGui::TrTextUnformatted("End");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(IM_SCALEF(130));
    if (ImGui::DateChooser("##EndDate", _bulk_module->fetch_date_tm, "%Y-%m-%d", true))
    {
        _bulk_module->fetch_date = mktime(&_bulk_module->fetch_date_tm);
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Extract...")))
    {
        dispatch([]()
        {
            system_save_file_dialog(
                tr("Bulk Extractor to JSON..."), 
                tr("JSON (*.json)|*.json"), 
                nullptr, [](string_const_t save_path)
            {
                bulk_extract(save_path, _bulk_module->start_extract_date, _bulk_module->fetch_date);
                return true;
            });
        });
    }

    ImGui::EndDisabled();


    ImGui::Spacing();

    unsigned job_completed_count = 0;
    for (unsigned i = 0; i < job_count; ++i)
    {
        if (job_completed(_bulk_module->extractor_jobs[i]))
            job_completed_count++;
    }

    if (job_count > 0)
    {
        ImGui::ProgressBar((float)job_completed_count  / (float)job_count);

        if (job_count == job_completed_count)
        {
            if (_bulk_module->extractor_cv && !string_is_null(_bulk_module->extractor_path))
            {
                config_write_file(
                    string_to_const(_bulk_module->extractor_path), _bulk_module->extractor_cv, 
                    CONFIG_OPTION_WRITE_TRUNCATE_NUMBERS |
                    CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES | 
                    CONFIG_OPTION_WRITE_JSON);
            }

            bulk_extractor_clean_up();
        }
    }
    else if (!string_is_null(_bulk_module->extractor_path))
    {
        if (ImGui::TextURL(STRING_RANGE(_bulk_module->extractor_path), nullptr, 0))
        {
            system_browse_to_file(STRING_ARGS(_bulk_module->extractor_path));
        }
    }
}

FOUNDATION_STATIC void bulk_open_extractor_window()
{
    window_open("bulk_extractor", STRING_CONST("Bulk Extractor"),
        L1(bulk_extractor_render()), nullptr, nullptr, WindowFlags::Singleton | WindowFlags::Dialog);
}

FOUNDATION_STATIC void bulk_menu()
{
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu(tr("Symbols")))
    {
        if (ImGui::MenuItem(tr("Last Day")))
            bulk_open_window();

        if (ImGui::MenuItem(tr("Bulk Extractor")))
            bulk_open_extractor_window();

        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

// 
// # SYSTEM
//

FOUNDATION_STATIC void bulk_initialize()
{
    _bulk_module = MEM_NEW(HASH_BULK, BULK_MODULE);

    module_register_menu(HASH_BULK, bulk_menu);
}

FOUNDATION_STATIC void bulk_shutdown()
{
    if (_bulk_module->exchanges)
    {
        string_const_t selected_exchanges_file_path = session_get_user_file_path(STRING_CONST("exchanges.json"));
        config_write_file(selected_exchanges_file_path, [](config_handle_t selected_exchange_data)
        {
            const size_t selected_exchange_count = array_size(_bulk_module->exchanges);
            for (int i = 0; i < selected_exchange_count; ++i)
            {
                const string_t& ex = _bulk_module->exchanges[i];
                config_array_push(selected_exchange_data, STRING_ARGS(ex));
            }
            return true;
        }, CONFIG_VALUE_ARRAY);
    }

    bulk_extractor_clean_up();
    string_deallocate(_bulk_module->extractor_path);
    table_deallocate(_bulk_module->table);
    string_array_deallocate(_bulk_module->exchanges);
    array_deallocate(_bulk_module->symbols);

    MEM_DELETE(_bulk_module);
}

DEFINE_MODULE(BULK, bulk_initialize, bulk_shutdown, MODULE_PRIORITY_UI);

#endif // BUILD_APPLICATION
