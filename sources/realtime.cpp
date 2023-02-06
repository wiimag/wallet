/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#include "realtime.h"

#include "eod.h"
#include "stock.h"
#include "events.h"
#include "logo.h"
#include "pattern.h"

#include <framework/session.h>
#include <framework/service.h>
#include <framework/dispatcher.h>
#include <framework/database.h>
#include <framework/query.h>
#include <framework/imgui.h>
#include <framework/table.h>

#include <foundation/fs.h>
#include <foundation/stream.h>
#include <foundation/thread.h>

#include <algorithm>

#define HASH_REALTIME static_hash_string("realtime", 8, 0x29e09dfa4716c805ULL)

struct stock_realtime_record_t
{
    time_t timestamp;
    double price;
};

struct stock_realtime_t
{
    hash_t key;
    char   code[16];
    time_t timestamp;
    double price;

    stock_realtime_record_t* records{ nullptr };
};

static struct REALTIME_MODULE {
    stream_t* stream{ nullptr };
    thread_t* background_thread{ nullptr };

    shared_mutex      stocks_mutex;
    stock_realtime_t* stocks{ nullptr };

    bool show_window{ false };
    table_t* table{ nullptr };
} *_realtime;

//
// # PRIVATE
//

FOUNDATION_FORCEINLINE bool operator<(const stock_realtime_t& s, const hash_t& key)
{
    return s.key < key;
}

FOUNDATION_FORCEINLINE bool operator>(const stock_realtime_t& s, const hash_t& key)
{
    return s.key > key;
}

FOUNDATION_STATIC bool realtime_register_new_stock(const dispatcher_event_args_t& args)
{
    FOUNDATION_ASSERT(args.size <= 16);
    string_const_t code { (const char*)args.data, args.size };
    const hash_t key = hash(code.str, code.length);
        
    SHARED_WRITE_LOCK(_realtime->stocks_mutex);
     
    int fidx = array_binary_search(_realtime->stocks, array_size(_realtime->stocks), key);
    if (fidx >= 0)
        return false;
    
    stock_realtime_t stock;
    string_copy(STRING_CONST_CAPACITY(stock.code), code.str, code.length);
    stock.key = key;
    stock.timestamp = time_now();
    stock.price = DNAN;
    stock.records = nullptr;

    fidx = ~fidx;
    array_insert_memcpy(_realtime->stocks, fidx, &stock);
    log_debugf(HASH_REALTIME, STRING_CONST("Registering new realtime stock %.*s (%" PRIhash ")"), STRING_FORMAT(code), key);
    return true;
}

FOUNDATION_FORCEINLINE bool operator<(const stock_realtime_record_t& r, const time_t& key)
{
    return r.timestamp < key;
}

FOUNDATION_FORCEINLINE bool operator>(const stock_realtime_record_t& r, const time_t& key)
{
    return r.timestamp > key;
}

FOUNDATION_STATIC bool realtime_stock_add_record(stock_realtime_t* stock, const stock_realtime_record_t& record)
{
    const int fidx = array_binary_search(stock->records, array_size(stock->records), record.timestamp);
    if (fidx >= 0)
        return false;

    array_insert_memcpy(stock->records, ~fidx, &record);
    return true;   
}

FOUNDATION_STATIC void realtime_fetch_query_data(const json_object_t& res)
{
    if (res.error_code > 0)
        return;
    
    for (auto e : res)
    {
        string_const_t code = e["code"].as_string();
        const hash_t key = hash(STRING_ARGS(code));

        stock_realtime_record_t r;
        r.price = e["close"].as_number();
        r.timestamp = (time_t)e["timestamp"].as_number(0);

        if (r.timestamp == 0 || math_real_is_nan(r.price))
            continue;
        
        SHARED_READ_LOCK(_realtime->stocks_mutex);
        
        int fidx = array_binary_search(_realtime->stocks, array_size(_realtime->stocks), key);
        if (fidx >= 0)
        {
            stock_realtime_t& stock = _realtime->stocks[fidx];

            if (r.timestamp > stock.timestamp)
            {
                stock.price = r.price;
                stock.timestamp = r.timestamp;

                log_debugf(HASH_REALTIME, STRING_CONST("Streaming new realtime values %lld > %.*s > %lf (%llu)"),
                    r.timestamp, STRING_FORMAT(code), r.price, stream_size(_realtime->stream));
                
                if (realtime_stock_add_record(&stock, r))
                {
                    stream_write(_realtime->stream, &r.timestamp, sizeof(r.timestamp));
                    stream_write(_realtime->stream, stock.code, sizeof(stock.code));
                    stream_write(_realtime->stream, &r.price, sizeof(r.price));
                }
            }
        }
    }
}

FOUNDATION_STATIC void* realtime_background_thread_fn(void*)
{
    shared_mutex& mutex = _realtime->stocks_mutex;
    
    while (!stream_eos(_realtime->stream))
    {
        stock_realtime_t stock;
        stock_realtime_record_t r;

        stream_read(_realtime->stream, &r.timestamp, sizeof(r.timestamp));
        stream_read(_realtime->stream, stock.code, sizeof(stock.code));
        stream_read(_realtime->stream, &r.price, sizeof(r.price));

        if (r.timestamp <= 0 || math_real_is_nan(r.price))
            continue;

        const size_t code_length = string_length(stock.code);
        stock.key = hash(stock.code, code_length);

        SHARED_WRITE_LOCK(mutex);
        int fidx = array_binary_search(_realtime->stocks, array_size(_realtime->stocks), stock.key);
        if (fidx < 0)
        { 
            stock.price = r.price;
            stock.timestamp = r.timestamp;
            stock.records = nullptr;
            array_push_memcpy(stock.records, &r);
            
            array_insert_memcpy(_realtime->stocks, ~fidx, &stock);
            log_debugf(HASH_REALTIME, STRING_CONST("Registering new realtime stock %s (%" PRIhash ")"), stock.code, stock.key);
        }
        else
        {
            stock_realtime_t& stock_ref = _realtime->stocks[fidx];

            if (realtime_stock_add_record(&stock_ref, r) && r.timestamp > stock_ref.timestamp)
            {
                if (r.timestamp > stock_ref.timestamp)
                {
                    stock_ref.price = r.price;
                    stock_ref.timestamp = r.timestamp;
                }
            }
        }
    }

    unsigned int wait_time = 1;
    static string_const_t* codes = nullptr;
    while (!thread_try_wait(wait_time))
    {
        time_t oldest = INT64_MAX;
        if (mutex.shared_lock())
        {
            time_t now = time_now();
            for (size_t i = 0, end = array_size(_realtime->stocks); i < end; ++i)
            {
                const stock_realtime_t& stock = _realtime->stocks[i];
                const double minutes = time_elapsed_days(stock.timestamp, now) * 24.0 * 60.0;
                if (minutes > 5)
                    array_push(codes, string_const(stock.code, string_length(stock.code)));

                if (stock.timestamp != 0 && stock.timestamp < oldest)
                    oldest = stock.timestamp;
            }
            mutex.shared_unlock();

            size_t batch_size = 0;
            string_const_t batch[32];
            for (size_t i = 0, end = array_size(codes); i < end; ++i)
            {
                batch[batch_size++] = codes[i];
                if (batch_size == ARRAY_COUNT(batch) || i == end - 1)
                {
                    range_view<string_const_t> view = { &batch[0], batch_size };
                    string_const_t code_list = string_join(view.begin(), view.end(), [](const auto& s) { return s; }, CTEXT(","));
                    
                    // Send batch
                    string_const_t url = eod_build_url("real-time", batch[0].str, FORMAT_JSON, "s", code_list.str);
                    log_infof(HASH_REALTIME, STRING_CONST("Fetching realtime stock data for %.*s\n%.*s"), STRING_FORMAT(code_list), STRING_FORMAT(url));
                    if (!query_execute_json(url.str, FORMAT_JSON_WITH_ERROR, realtime_fetch_query_data))
                        break;

                    if (thread_try_wait(wait_time / to_uint(end)))
                        goto realtime_background_thread_fn_quit;

                    batch_size = 0;
                }
            }

            array_clear(codes);
            stream_flush(_realtime->stream);
            if (oldest != INT64_MAX)
            {
                double wait_minutes = time_elapsed_days(oldest, now) * 24.0 * 60.0;
                wait_minutes = max(0.0, 15.0 - wait_minutes);
                wait_time = max(60000U, to_uint(math_trunc(wait_minutes * 60.0 * 1000.0)));
            }
            else
            {
                wait_time = 60000U;
            }
        }
    }
    
realtime_background_thread_fn_quit:
    array_deallocate(codes);    
    return 0;
}

FOUNDATION_STATIC int realtime_format_date_range_label(double value, char* buff, int size, void* user_data)
{
    stock_realtime_t* ev = (stock_realtime_t*)user_data;
    if (math_real_is_nan(value))
        return 0;
    
    time_t last_run_time = ev->timestamp;
    const double diff = difftime(last_run_time, (time_t)value);
    value = diff / time_one_day();

    if (value >= 30 * 3)
    {
        string_const_t date_str = string_from_date(value);
        return (int)string_format(buff, size, STRING_CONST("%.*s"), STRING_FORMAT(date_str)).length;
    }
    else if (value >= 30)
    {
        value = math_round(value / 30);
        return (int)string_format(buff, size, STRING_CONST("%.0lfM"), value).length;
    }
    else if (value >= 7)
    {
        value = math_round(value / 7);
        return (int)string_format(buff, size, STRING_CONST("%.0lfW"), value).length;
    }
    else if (value >= 1)
    {
        value = math_round(value);
        return (int)string_format(buff, size, STRING_CONST("%.0lfD"), value).length;
    }
    else if (value >= 0.042)
    {
        value = math_round(value * 24);
        return (int)string_format(buff, size, STRING_CONST("%.0lfH"), value).length;
    }

    value = math_round(value * 24 * 60);
    return (int)string_format(buff, size, STRING_CONST("%.3g mins."), value).length;
}

FOUNDATION_STATIC cell_t realtime_table_draw_title(table_element_ptr_t element, const column_t* column)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        ImRect logo_rect;
        if (!logo_render(s->code, string_length(s->code), {}, true, false, &logo_rect))
            ImGui::TextUnformatted(s->code);
        else
        {
            ImGui::SetCursorScreenPos(logo_rect.Min);
            ImGui::Dummy(logo_rect.GetSize());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", s->code);
            }
        }
    }

    return s->code;
}

FOUNDATION_STATIC cell_t realtime_table_draw_time(table_element_ptr_t element, const column_t* column)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        string_const_t date_string = string_from_time_static((tick_t)s->timestamp * (tick_t)1000, true);
        ImGui::TextWrapped("%.*s", STRING_FORMAT(date_string));
    }

    return s->timestamp;
}

FOUNDATION_STATIC cell_t realtime_table_draw_price(table_element_ptr_t element, const column_t* column)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;
    return s->price;
}

FOUNDATION_STATIC cell_t realtime_table_draw_last_change_p(table_element_ptr_t element, const column_t* column)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;

    const auto record_count = array_size(s->records);
    if (record_count <= 1)
        return 0.0;

    const auto last = s->records[record_count - 1].price;
    const auto prev = s->records[record_count - 2].price;

    return (last - prev) / prev * 100.0;
}

FOUNDATION_STATIC cell_t realtime_table_draw_sample_count(table_element_ptr_t element, const column_t* column)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;
    return (double)array_size(s->records);
}

FOUNDATION_STATIC const char* realtime_monitor_price_format(const stock_realtime_t* s)
{
    if (s->price < 9)
        return "%.3g $";
    if (s->price < 99)
        return "%.2g $";
    if (s->price < 999)
        return "%.3g $";
    if (s->price > 9999)
        return "%.5g $";
    return "%.4g $";
}

FOUNDATION_STATIC cell_t realtime_table_draw_monitor(table_element_ptr_t element, const column_t* column)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        const size_t record_count = array_size(s->records);
        if (record_count >= 2 && ImPlot::BeginPlot(s->code, {-1.0f, _realtime->table->fixed_height}, ImPlotFlags_NoTitle))
        {
            double min = (double)s->records[0].timestamp;
            double max = (double)array_last(s->records)->timestamp;
            ImPlot::SetupAxis(ImAxis_X1, "##Days", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
            ImPlot::SetupAxisFormat(ImAxis_X1, realtime_format_date_range_label, (void*)s);
            ImPlot::SetupAxisFormat(ImAxis_Y1, realtime_monitor_price_format(s));
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, min - (max - min) * 0.05, max);

            ImPlot::PlotLineG("##Values", [](int idx, void* user_data)->ImPlotPoint
            {
                stock_realtime_t* c = (stock_realtime_t*)user_data;
                const stock_realtime_record_t* r = &c->records[idx];

                const double x = (double)r->timestamp;
                const double y = r->price;
                return ImPlotPoint(x, y);
            }, (void*)s, array_size(s->records), ImPlotLineFlags_SkipNaN);
            ImPlot::EndPlot();
        }
        else
        {
            ImGui::TextUnformatted("Not enough data");
        }
    }

    return (double)array_size(s->records);
}

FOUNDATION_STATIC void realtime_code_selected(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;
    pattern_open(s->code, string_length(s->code));
}

FOUNDATION_STATIC void realtime_render_prices()
{
    if (_realtime->show_window == false)
        return;

    static bool has_ever_shown_window = session_key_exists("show_realtime_window");
    if (!has_ever_shown_window)
        ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_Once);

    if (ImGui::Begin("Realtime Stocks##1", &_realtime->show_window, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoCollapse))
    {
        if (_realtime->table == nullptr)
        {
            _realtime->table = table_allocate("realtime");
            _realtime->table->fixed_height = imgui_get_font_ui_scale(250.0f);
            table_add_column(_realtime->table, "Title", realtime_table_draw_title, COLUMN_FORMAT_TEXT, 
                COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING | COLUMN_NOCLIP_CONTENT)
                .set_selected_callback(realtime_code_selected);
            table_add_column(_realtime->table, "Time", realtime_table_draw_time, COLUMN_FORMAT_DATE, COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING);
            table_add_column(_realtime->table, "Price", realtime_table_draw_price, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_VALIGN_TOP);
            table_add_column(_realtime->table, "%||Change %", realtime_table_draw_last_change_p, COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_VALIGN_TOP | COLUMN_HIDE_DEFAULT);
            table_add_column(_realtime->table, "Monitor", realtime_table_draw_monitor, COLUMN_FORMAT_NUMBER, 
                COLUMN_SORTABLE | COLUMN_STRETCH | COLUMN_CUSTOM_DRAWING | COLUMN_LEFT_ALIGN | COLUMN_DEFAULT_SORT);
            table_add_column(_realtime->table, "#||Samples", realtime_table_draw_sample_count, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_VALIGN_TOP | COLUMN_HIDE_DEFAULT);
        }

        SHARED_READ_LOCK(_realtime->stocks_mutex);
        table_render(_realtime->table, _realtime->stocks, array_size(_realtime->stocks), sizeof(stock_realtime_t), 0, 0);
    } ImGui::End();
}

FOUNDATION_STATIC void realtime_menu()
{
    if (!ImGui::BeginMenuBar())
        return;
    
    if (ImGui::BeginMenu("Windows"))
    {
        ImGui::MenuItem(ICON_MD_RADIO_BUTTON_ON " Realtime", nullptr, &_realtime->show_window);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

//
// # SYSTEM
//

FOUNDATION_STATIC void realtime_initialize()
{
    _realtime = MEM_NEW(HASH_REALTIME, REALTIME_MODULE);

    _realtime->show_window = session_get_bool("show_realtime_window", _realtime->show_window);

    // Open realtime stock stream.
    string_const_t realtime_stream_path = session_get_user_file_path(STRING_CONST("realtime"), nullptr, 0, STRING_CONST("stream"));
    _realtime->stream = fs_open_file(STRING_ARGS(realtime_stream_path), STREAM_CREATE | STREAM_IN | STREAM_OUT | STREAM_BINARY);
    if (_realtime->stream == nullptr)
    {
        log_panic(HASH_REALTIME, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to open realtime stream"));
        return;
    }

    // Create thread to query realtime stock
    _realtime->background_thread = thread_allocate(realtime_background_thread_fn, nullptr, STRING_CONST("realtime"), THREAD_PRIORITY_NORMAL, 0);
    if (_realtime->background_thread == nullptr || !thread_start(_realtime->background_thread))
    {
        log_panic(HASH_REALTIME, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to create realtime background thread"));
        return;
    }

    dispatcher_register_event_listener(EVENT_STOCK_REQUESTED, realtime_register_new_stock);

    service_register_menu(HASH_REALTIME, realtime_menu);
    service_register_window(HASH_REALTIME, realtime_render_prices);
}

FOUNDATION_STATIC void realtime_shutdown()
{   
    session_set_bool("show_realtime_window", _realtime->show_window);

    stream_deallocate(_realtime->stream);
    thread_signal(_realtime->background_thread);
    thread_deallocate(_realtime->background_thread);
    table_deallocate(_realtime->table);

    foreach(s, _realtime->stocks)
        array_deallocate(s->records);
    array_deallocate(_realtime->stocks);

    MEM_DELETE(_realtime);
}

DEFINE_SERVICE(REALTIME, realtime_initialize, realtime_shutdown, SERVICE_PRIORITY_UI);
