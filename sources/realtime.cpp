/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#include "realtime.h"

#include "eod.h"
#include "stock.h"
#include "events.h"
#include "logo.h"

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
    log_infof(HASH_REALTIME, STRING_CONST("1. Registering new realtime stock %.*s (%" PRIhash ")"), STRING_FORMAT(code), key);
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
        r.timestamp = (time_t)e["timestamp"].as_number();
        
        SHARED_READ_LOCK(_realtime->stocks_mutex);
        
        int fidx = array_binary_search(_realtime->stocks, array_size(_realtime->stocks), key);
        if (fidx >= 0)
        {
            stock_realtime_t& stock = _realtime->stocks[fidx];

            if (r.timestamp > stock.timestamp)
            {
                stock.price = r.price;
                stock.timestamp = r.timestamp;

                log_infof(HASH_REALTIME, STRING_CONST("Streaming new realtime values %lld > %.*s > %lf (%llu)"), 
                    r.timestamp, STRING_FORMAT(code), r.price, stream_size(_realtime->stream));
                
                // Is that safe enough?
                array_push_memcpy(stock.records, &r);

                stream_write(_realtime->stream, &r.timestamp, sizeof(r.timestamp));
                stream_write(_realtime->stream, stock.code, sizeof(stock.code));
                stream_write(_realtime->stream, &r.price, sizeof(r.price));
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

        if (r.timestamp == 0 || math_real_is_nan(r.price))
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

            fidx = ~fidx;
            array_insert_memcpy(_realtime->stocks, fidx, &stock);
            log_infof(HASH_REALTIME, STRING_CONST("2. Registering new realtime stock %s (%" PRIhash ")"), stock.code, stock.key);
        }
        else
        {
            stock_realtime_t& stock = _realtime->stocks[fidx];

            if (r.timestamp > stock.timestamp)
            {
                stock.price = r.price;
                stock.timestamp = r.timestamp;
            }
            array_push_memcpy(stock.records, &r);
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
            string_const_t batch[16];
            for (size_t i = 0, end = array_size(codes); i < end; ++i)
            {
                batch[batch_size++] = codes[i];
                if (batch_size == ARRAY_COUNT(batch) || i == end - 1)
                {
                    range_view<string_const_t> view = { &batch[0], batch_size };
                    string_const_t code_list = string_join(view.begin(), view.end(), [](const auto& s) { return s; }, CTEXT(","));
                    
                    // Send batch
                    string_const_t url = eod_build_url("real-time", batch[0].str, FORMAT_JSON, "s", code_list.str);
                    log_infof(HASH_REALTIME, STRING_CONST("Fetching realtime stock data for %.*s"), STRING_FORMAT(code_list));
                    if (!query_execute_json(url.str, FORMAT_JSON_WITH_ERROR, realtime_fetch_query_data))
                        break;

                    if (thread_try_wait(5000))
                        goto realtime_background_thread_fn_quit;

                    batch_size = 0;
                }
            }

            array_clear(codes);
            stream_flush(_realtime->stream);
            if (oldest != INT64_MAX)
            {
                double wait_minutes = time_elapsed_days(oldest, now) * 24.0 * 60.0;
                wait_minutes = max(0.0, 5.0 - wait_minutes);
                wait_time = max(60000U, to_uint(math_trunc(wait_minutes * 60.0 * 1000.0)));
            }
            else
            {
                wait_time = 5000;
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

    if (value >= 365)
    {
        value = math_round(value / 365);
        return (int)string_format(buff, size, STRING_CONST("%.0lfY"), value).length;
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

FOUNDATION_STATIC void realtime_render_prices()
{
    if (_realtime->show_window == false)
        return;

    static bool has_ever_shown_window = session_key_exists("show_realtime_window");
    if (!has_ever_shown_window)
        ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_Once);

    if (ImGui::Begin("Realtime Stocks##1", &_realtime->show_window, ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoCollapse))
    {
        if (ImGui::BeginTable("##Evaluators", 4,
            ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_RowBg | 
            ImGuiTableFlags_Hideable | 
            ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_Resizable, ImVec2(-1.0f, -1.0f)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);

            ImGui::TableSetupColumn("Title");
            ImGui::TableSetupColumn("Time", 0, 0, 0, table_cell_right_aligned_column_label);
            ImGui::TableSetupColumn("Price", 0, 0, 0, table_cell_right_aligned_column_label);
            ImGui::TableSetupColumn("Monitor", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            SHARED_READ_LOCK(_realtime->stocks_mutex);

            ImGuiListClipper clipper;
            clipper.Begin(array_size(_realtime->stocks), 200.0f);
            while (clipper.Step())
            {
                if (clipper.DisplayStart >= clipper.DisplayEnd)
                    continue;

                for (int element_index = clipper.DisplayStart; element_index < clipper.DisplayEnd; ++element_index)
                {
                    stock_realtime_t& ev = _realtime->stocks[element_index];
                    const size_t record_count = array_size(ev.records);
                    //if (record_count <= 2)
                      //  continue;

                    ImGui::TableNextRow(ImGuiTableRowFlags_None, 200.0f);
                    {
                        bool evaluate_expression = false;

                        ImGui::PushID(&ev);

                        if (ImGui::TableNextColumn())
                        {
                            ImRect logo_rect;
                            if (!logo_render(ev.code, string_length(ev.code), {}, true, false, &logo_rect))
                                ImGui::TextUnformatted(ev.code);
                            else
                            {
                                ImGui::SetCursorScreenPos(logo_rect.Min);
                                ImGui::Dummy(logo_rect.GetSize());
                                if (ImGui::IsItemHovered())
                                {
                                    ImGui::SetTooltip("%s", ev.code);
                                }
                            }
                        }

                        if (ImGui::TableNextColumn())
                        {
                            string_const_t date_string = string_from_date(ev.timestamp);
                            table_cell_right_aligned_label(STRING_ARGS(date_string));
                        }

                        if (ImGui::TableNextColumn())
                        {
                            char price_label_buffer[16];
                            string_t price_label = string_format(STRING_CONST_CAPACITY(price_label_buffer), STRING_CONST("%.2lf $"), ev.price);
                            table_cell_right_aligned_label(STRING_ARGS(price_label));
                        }

                        if (ImGui::TableNextColumn())
                        {
                            if (record_count > 2 && ImPlot::BeginPlot("##MonitorGraph"))
                            {
                                double min = (double)ev.records[0].timestamp;
                                double max = (double)array_last(ev.records)->timestamp;
                                ImPlot::SetupAxis(ImAxis_X1, "##Days", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
                                ImPlot::SetupAxisFormat(ImAxis_X1, realtime_format_date_range_label, &ev);
                                ImPlot::SetupAxisTicks(ImAxis_X1, min, max, 6);
                                ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, min - (max - min) * 0.05, max + (max - min) * 0.05);

                                ImPlot::PlotLineG("##Values", [](int idx, void* user_data)->ImPlotPoint
                                    {
                                        stock_realtime_t* c = (stock_realtime_t*)user_data;
                                        const stock_realtime_record_t* r = &c->records[idx];

                                        const double x = (double)r->timestamp;
                                        const double y = r->price;
                                        return ImPlotPoint(x, y);
                                    }, &ev, array_size(ev.records), ImPlotLineFlags_SkipNaN);
                                ImPlot::EndPlot();
                            }
                        }

                        ImGui::PopID();
                    }
                }
            }

            ImGui::EndTable();
        }
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

    foreach(s, _realtime->stocks)
        array_deallocate(s->records);
    array_deallocate(_realtime->stocks);

    MEM_DELETE(_realtime);
}

DEFINE_SERVICE(REALTIME, realtime_initialize, realtime_shutdown, SERVICE_PRIORITY_MODULE);
