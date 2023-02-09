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

#define REALTIME_STREAM_VERSION (1)
#define HASH_REALTIME static_hash_string("realtime", 8, 0x29e09dfa4716c805ULL)

struct stock_realtime_record_t
{
    time_t timestamp;
    double price;
    double volume;
};

struct stock_realtime_t
{
    hash_t key;
    char   code[16];
    time_t timestamp;
    double price;
    double volume;

    stock_realtime_record_t* records{ nullptr };
};

static struct REALTIME_MODULE {
    stream_t* stream{ nullptr };
    thread_t* background_thread{ nullptr };

    shared_mutex      stocks_mutex;
    stock_realtime_t* stocks{ nullptr };

    bool show_window{ false };
    table_t* table{ nullptr };

    char search[32]{ '\0' };
    int time_lapse{ 24 * 31 }; // In hours
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
    stock.volume = 0;
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
        r.volume = e["volume"].as_number(0);
        r.timestamp = (time_t)e["timestamp"].as_number(0);

        if (r.timestamp == 0 || math_real_is_nan(r.price))
            continue;

        if (_realtime->stream == nullptr)
            break;
        
        SHARED_READ_LOCK(_realtime->stocks_mutex);
        
        int fidx = array_binary_search(_realtime->stocks, array_size(_realtime->stocks), key);
        if (fidx >= 0)
        {
            stock_realtime_t& stock = _realtime->stocks[fidx];

            if (r.timestamp > stock.timestamp)
            {
                stock.price = r.price;
                stock.volume = r.volume;
                stock.timestamp = r.timestamp;

                log_debugf(HASH_REALTIME, STRING_CONST("Streaming new realtime values %.*s (%lld) > %lf (%" PRIsize " kb)"),
                    STRING_FORMAT(code), (int64_t)r.timestamp, r.price, stream_size(_realtime->stream) / 1024ULL);
                
                if (realtime_stock_add_record(&stock, r))
                {
                    stream_write(_realtime->stream, &r.timestamp, sizeof(r.timestamp));
                    stream_write(_realtime->stream, stock.code, sizeof(stock.code));
                    stream_write(_realtime->stream, &r.price, sizeof(r.price));
                    stream_write(_realtime->stream, &r.volume, sizeof(r.volume));
                }
            }
        }
    }
}

FOUNDATION_STATIC stream_t* realtime_open_stream()
{
    string_const_t realtime_stream_path = session_get_user_file_path(STRING_CONST("realtime"), nullptr, 0, STRING_CONST("stream"));
    stream_t* stream = fs_open_file(STRING_ARGS(realtime_stream_path), STREAM_CREATE | STREAM_IN | STREAM_OUT | STREAM_BINARY);
    if (stream == nullptr)
    {
        log_panic(HASH_REALTIME, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to open realtime stream"));
        return nullptr;
    }

    return stream;
}

FOUNDATION_STATIC void realtime_migrate_stream(int& from, int to)
{
    // Create a new stream
    stream_t* migrate_stream = fs_temporary_file();
    FOUNDATION_ASSERT(migrate_stream);

    stream_write(migrate_stream, "REAL", 4);
    stream_write(migrate_stream, &to, sizeof(to));

    char temp_path_buffer[BUILD_MAX_PATHLEN];
    string_const_t temp_path_const = stream_path(migrate_stream);
    string_t temp_path = string_copy(STRING_CONST_CAPACITY(temp_path_buffer), temp_path_const.str, temp_path_const.length);

    char current_stream_path_buffer[BUILD_MAX_PATHLEN];
    string_const_t current_stream_path_const = stream_path(_realtime->stream);
    string_t current_stream_path = string_copy(STRING_CONST_CAPACITY(current_stream_path_buffer), current_stream_path_const.str, current_stream_path_const.length);
    
    if (from == 0 && to == 1)
    {
        stream_seek(_realtime->stream, 0, STREAM_SEEK_BEGIN);
        while (!stream_eos(_realtime->stream))
        {
            stock_realtime_t stock;
            stock_realtime_record_t r;
            r.volume = 0;

            stream_read(_realtime->stream, &r.timestamp, sizeof(r.timestamp));
            stream_read(_realtime->stream, stock.code, sizeof(stock.code));
            stream_read(_realtime->stream, &r.price, sizeof(r.price));

            if (r.timestamp <= 0 || math_real_is_nan(r.price))
                continue;

            stream_write(migrate_stream, &r.timestamp, sizeof(r.timestamp));
            stream_write(migrate_stream, stock.code, sizeof(stock.code));
            stream_write(migrate_stream, &r.price, sizeof(r.price));
            stream_write(migrate_stream, &r.volume, sizeof(r.volume));
        }
        
        from = to;
    }

    stream_deallocate(migrate_stream);
    stream_deallocate(_realtime->stream);
    if (fs_copy_file(STRING_ARGS(temp_path), STRING_ARGS(current_stream_path)))
        _realtime->stream = realtime_open_stream();

    FOUNDATION_ASSERT(_realtime->stream);
}

FOUNDATION_STATIC void* realtime_background_thread_fn(void*)
{
    shared_mutex& mutex = _realtime->stocks_mutex;

    // Read stream version
    int stream_version = 0;
    if (!stream_eos(_realtime->stream))
    {
        char file_format[4] = { '\0' };
        stream_read(_realtime->stream, file_format, sizeof(file_format));
        if (!string_equal(file_format, 4, STRING_CONST("REAL")))
            realtime_migrate_stream(stream_version, REALTIME_STREAM_VERSION);
        else if (stream_read(_realtime->stream, &stream_version, sizeof(stream_version)) == sizeof(stream_version))
        {
            if (stream_version != REALTIME_STREAM_VERSION)
                realtime_migrate_stream(stream_version, REALTIME_STREAM_VERSION);
        }
    }
    
    while (stream_version == REALTIME_STREAM_VERSION && !stream_eos(_realtime->stream))
    {
        stock_realtime_t stock;
        stock_realtime_record_t r;

        stream_read(_realtime->stream, &r.timestamp, sizeof(r.timestamp));
        stream_read(_realtime->stream, stock.code, sizeof(stock.code));
        stream_read(_realtime->stream, &r.price, sizeof(r.price));
        stream_read(_realtime->stream, &r.volume, sizeof(r.volume));

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
            stock.volume = r.volume;
            stock.records = nullptr;
            array_push_memcpy(stock.records, &r);
            
            array_insert_memcpy(_realtime->stocks, ~fidx, &stock);
            log_debugf(HASH_REALTIME, STRING_CONST("Streaming new realtime stock %s (%" PRIhash ")"), stock.code, stock.key);
        }
        else
        {
            stock_realtime_t& stock_ref = _realtime->stocks[fidx];

            if (realtime_stock_add_record(&stock_ref, r) && r.timestamp > stock_ref.timestamp)
            {
                if (r.timestamp > stock_ref.timestamp)
                {
                    stock_ref.price = r.price;
                    stock_ref.volume = r.volume;
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
                wait_time = 15 * 60000U;
            }
        }
    }
    
realtime_background_thread_fn_quit:
    array_deallocate(codes);    
    return 0;
}

FOUNDATION_STATIC int realtime_format_volume_label(double value, char* buff, int size, void* user_data)
{
    const double abs_value = math_abs(value);
    if (abs_value >= 1e12)
        return to_int(string_format(buff, size, STRING_CONST("%.3lgT"), value / 1e12).length);
    if (abs_value >= 1e9)
        return to_int(string_format(buff, size, STRING_CONST("%.3lgB"), value / 1e9).length);
    else if (abs_value >= 1e6)
        return to_int(string_format(buff, size, STRING_CONST("%.3lgM"), value / 1e6).length);
    else if (abs_value >= 1e3)
        return to_int(string_format(buff, size, STRING_CONST("%.3lgK"), value / 1e3).length);

    return to_int(string_format(buff, size, STRING_CONST("%.3lg"), value).length);
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

FOUNDATION_STATIC cell_t realtime_table_column_time(table_element_ptr_t element, const column_t* column)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        string_const_t date_string = string_from_time_static((tick_t)s->timestamp * (tick_t)1000, true);
        ImGui::TextWrapped("%.*s", STRING_FORMAT(date_string));
    }

    return s->timestamp;
}

FOUNDATION_STATIC cell_t realtime_table_column_price(table_element_ptr_t element, const column_t* column)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;
    return s->price;
}

FOUNDATION_STATIC cell_t realtime_table_column_volume(table_element_ptr_t element, const column_t* column)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;
    return s->volume;
}

FOUNDATION_STATIC cell_t realtime_table_column_last_change_p(table_element_ptr_t element, const column_t* column)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;

    const auto record_count = array_size(s->records);
    if (record_count <= 1)
        return 0.0;

    const auto last = s->records[record_count - 1].price;
    const auto prev = s->records[record_count - 2].price;

    return (last - prev) / prev * 100.0;
}

FOUNDATION_STATIC cell_t realtime_table_column_sample_count(table_element_ptr_t element, const column_t* column)
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

FOUNDATION_STATIC bool realtime_render_graph(const stock_realtime_t* s, time_t since, float width, float height)
{
    const auto record_count = array_size(s->records);
    if (record_count <= 1)
        return false;
        
    stock_realtime_record_t* first = &s->records[0];
    const stock_realtime_record_t* last = array_last(s->records);

    if (since != 0)
    {
        int fidx = array_binary_search(s->records, record_count, since);
        if (fidx < 0)
            fidx = max(0, min(~fidx, to_int(record_count - 1)));
        first = &s->records[fidx];
    }

    const int visible_record_count = (int)(last - first) + 1;
    if (visible_record_count <= 1)
        return false;

    if (!ImPlot::BeginPlot(s->code, { width, height }, ImPlotFlags_NoTitle))
        return false;

    const double min = (double)first->timestamp;
    const double max = (double)last->timestamp;
    ImPlot::SetupAxis(ImAxis_X1, "##Days", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxis(ImAxis_Y1, "##Price", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxis(ImAxis_Y2, "##Volume", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Opposite);
    ImPlot::SetupAxisFormat(ImAxis_X1, realtime_format_date_range_label, (void*)s);
    ImPlot::SetupAxisFormat(ImAxis_Y1, realtime_monitor_price_format(s));
    ImPlot::SetupAxisFormat(ImAxis_Y2, realtime_format_volume_label, (void*)s);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, min - (max - min) * 0.05, max);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y2, 0, INFINITY);

    ImPlot::SetAxis(ImAxis_Y1);
    ImPlot::PlotLineG("##Price", [](int idx, void* user_data)->ImPlotPoint
    {
        const stock_realtime_record_t* first = (const stock_realtime_record_t*)user_data;
        const stock_realtime_record_t* r = first + idx;

        const double x = (double)r->timestamp;
        const double y = r->price;
        return ImPlotPoint(x, y);
    }, (void*)first, visible_record_count, ImPlotLineFlags_SkipNaN);

    ImPlot::SetAxis(ImAxis_Y2);
    ImPlot::PlotLineG("##Volume", [](int idx, void* user_data)->ImPlotPoint
    {
        const stock_realtime_record_t* first = (const stock_realtime_record_t*)user_data;
        const stock_realtime_record_t* r = first + idx;

        const double x = (double)r->timestamp;
        const double y = r->volume;
        return ImPlotPoint(x, y);
    }, (void*)first, visible_record_count, ImPlotLineFlags_SkipNaN);

    ImPlot::EndPlot();

    return true;
}

FOUNDATION_STATIC cell_t realtime_table_draw_monitor(table_element_ptr_t element, const column_t* column)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;

    if (column->flags & COLUMN_RENDER_ELEMENT)
    {
        const size_t record_count = array_size(s->records);
        const time_t since = _realtime->time_lapse > 0 ? time_add_hours(time_now(), -_realtime->time_lapse) : 0;
        ImGui::PushID(_realtime->time_lapse);
        if (record_count < 2 || !realtime_render_graph(s, since, -1.0f, _realtime->table->row_fixed_height))
        {
            ImGui::TextUnformatted("Not enough data");
        }
        ImGui::PopID();
    }

    return (double)array_size(s->records);
}

FOUNDATION_STATIC void realtime_code_selected(table_element_ptr_const_t element, const column_t* column, const cell_t* cell)
{
    const stock_realtime_t* s = (const stock_realtime_t*)element;
    pattern_open(s->code, string_length(s->code));
}

FOUNDATION_STATIC void realtime_render_window_tootlbar()
{
    ImGui::BeginGroup();

    // Render search box to filter realtime stocks
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Search");
    ImGui::SameLine();
    ImGui::PushItemWidth(ImGui::GetFontSize() * 14.0f);
    if (ImGui::InputTextWithHint("##Search", "Filter stock titles...", _realtime->search, sizeof(_realtime->search)))
    {
        // Update table search filter
        table_set_search_filter(_realtime->table, _realtime->search, string_length(_realtime->search));
    }

    ImGui::SameLine();
    bool show_all = _realtime->time_lapse == 0;
    if (ImGui::Checkbox("Show all records", &show_all))
    {
        _realtime->time_lapse = show_all ? 0 : session_get_integer("realtime_time_lapse_days", 24);
    }

    if (!show_all)
    {
        // Render time lapse slider in days
        ImGui::SameLine();
        ImGui::PushItemWidth(ImGui::GetFontSize() * 20.0f);
        if (ImGui::SliderInt("##TimeLapse", &_realtime->time_lapse, 1, 3 * 24, "%d hour(s)"))
            session_set_integer("realtime_time_lapse_days", _realtime->time_lapse);
    }

    ImGui::EndGroup();
}

FOUNDATION_STATIC void realtime_render_window()
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
            _realtime->table->row_fixed_height = imgui_get_font_ui_scale(250.0f);
            table_add_column(_realtime->table, "Title", realtime_table_draw_title, COLUMN_FORMAT_TEXT, 
                COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING | COLUMN_NOCLIP_CONTENT | COLUMN_SEARCHABLE)
                .set_selected_callback(realtime_code_selected);
            table_add_column(_realtime->table, "Time", realtime_table_column_time, COLUMN_FORMAT_DATE, COLUMN_SORTABLE | COLUMN_CUSTOM_DRAWING);
            table_add_column(_realtime->table, "Price", realtime_table_column_price, COLUMN_FORMAT_CURRENCY, COLUMN_SORTABLE | COLUMN_VALIGN_TOP);
            table_add_column(_realtime->table, "Volume", realtime_table_column_volume, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_VALIGN_TOP | COLUMN_HIDE_DEFAULT | COLUMN_NUMBER_ABBREVIATION);
            table_add_column(_realtime->table, "%||Change %", realtime_table_column_last_change_p, COLUMN_FORMAT_PERCENTAGE, COLUMN_SORTABLE | COLUMN_VALIGN_TOP | COLUMN_HIDE_DEFAULT);
            table_add_column(_realtime->table, "Monitor", realtime_table_draw_monitor, COLUMN_FORMAT_NUMBER, 
                COLUMN_SORTABLE | COLUMN_STRETCH | COLUMN_CUSTOM_DRAWING | COLUMN_LEFT_ALIGN | COLUMN_DEFAULT_SORT);
            table_add_column(_realtime->table, "#||Samples", realtime_table_column_sample_count, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_VALIGN_TOP | COLUMN_HIDE_DEFAULT);
        }

        realtime_render_window_tootlbar();

        {
            SHARED_READ_LOCK(_realtime->stocks_mutex);
            table_render(_realtime->table, _realtime->stocks, array_size(_realtime->stocks), sizeof(stock_realtime_t), 0, 0);
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
// # PUBLIC API
//

bool realtime_render_graph(const char* code, size_t code_length, time_t since /*= 0*/, float width /*= -1.0f*/, float height /*= -1.0f*/)
{
    const hash_t key = hash(code, code_length);
    const int fidx = array_binary_search(_realtime->stocks, array_size(_realtime->stocks), key);
    if (fidx < 0)
        return false;
    
    const stock_realtime_t* s = &_realtime->stocks[fidx];
    return realtime_render_graph(s, since, width, height);
}

//
// # SYSTEM
//

FOUNDATION_STATIC void realtime_initialize()
{
    _realtime = MEM_NEW(HASH_REALTIME, REALTIME_MODULE);

    _realtime->show_window = session_get_bool("realtime_show_window", _realtime->show_window);
    _realtime->time_lapse = session_get_integer("realtime_time_lapse_days", _realtime->time_lapse);

    // Open realtime stock stream.
    _realtime->stream = realtime_open_stream();

    // Create thread to query realtime stock
    _realtime->background_thread = thread_allocate(realtime_background_thread_fn, nullptr, STRING_CONST("realtime"), THREAD_PRIORITY_NORMAL, 0);
    if (_realtime->background_thread == nullptr || !thread_start(_realtime->background_thread))
    {
        log_panic(HASH_REALTIME, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to create realtime background thread"));
        return;
    }

    dispatcher_register_event_listener(EVENT_STOCK_REQUESTED, realtime_register_new_stock);

    service_register_menu(HASH_REALTIME, realtime_menu);
    service_register_window(HASH_REALTIME, realtime_render_window);
}

FOUNDATION_STATIC void realtime_shutdown()
{   
    session_set_bool("realtime_show_window", _realtime->show_window);
    session_set_integer("realtime_time_lapse_days", _realtime->time_lapse);

    SHARED_WRITE_LOCK(_realtime->stocks_mutex);

    stream_deallocate(_realtime->stream);
    _realtime->stream = nullptr;
    thread_signal(_realtime->background_thread);
    thread_deallocate(_realtime->background_thread);
    table_deallocate(_realtime->table);

    foreach(s, _realtime->stocks)
        array_deallocate(s->records);
    array_deallocate(_realtime->stocks);

    MEM_DELETE(_realtime);
}

DEFINE_SERVICE(REALTIME, realtime_initialize, realtime_shutdown, SERVICE_PRIORITY_UI);
