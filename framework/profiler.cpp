/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "profiler.h"

#if BUILD_ENABLE_PROFILE

#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/service.h>
#include <framework/session.h>
#include <framework/shared_mutex.h>
#include <framework/table.h>
#include <framework/math.h>
#include <framework/string.h>

#include <foundation/stream.h>
#include <foundation/environment.h>
#include <foundation/time.h>

#include <bx/sort.h>

#define HASH_PROFILER static_hash_string("profiler", 8, 0xc9186f3fc62fa119ULL)

#define MAX_MESSAGE_LENGTH (25)

#define PROFILE_ID_ENDFRAME (4)
#define PROFILE_LAST_BUILTIN_ID (12)

struct profile_block_data_t {
    int32_t id;
    int32_t parentid;
    uint32_t processor;
    uint32_t thread;
    tick_t start;
    tick_t end;
    char name[MAX_MESSAGE_LENGTH + 1];
};  // sizeof( profile_block_data ) == 58

struct profile_tracker_t {
    hash_t key;
    uint64_t counter;

    double min;
    double max;
    double sum;
    double avg;
    double last;

    tick_t start;
    tick_t end;

    char name[MAX_MESSAGE_LENGTH + 1];
};

static bool _profiler_initialized = false;
static stream_t* _profile_stream = nullptr;

static shared_mutex _trackers_lock;
static profile_tracker_t* _trackers = nullptr;

static bool _profiler_window_opened = false;
static table_t* _profiler_table = nullptr;

static uint8_t* _profile_buffer = nullptr;

//
// # PRIVATE
//

FOUNDATION_STATIC void profiler_tracker(void* buffer, size_t size)
{
    profile_block_data_t* b = (profile_block_data_t*)buffer;
    if (b->id == PROFILE_ID_ENDFRAME)
    {
        //char profile_msg_buffer[128];
        //string_t profile_msg = string_format(STRING_BUFFER(profile_msg_buffer), S("====== FRAME %llu ======\n"), b->end);
        //system_process_debug_output(STRING_ARGS(profile_msg));
    }
    else
    {
        tick_t diff = time_diff(b->start, b->end);
        if (diff > 0 && b->id > PROFILE_LAST_BUILTIN_ID)
        {
            double diff_ms = time_ticks_to_milliseconds(diff);
            size_t name_length = string_length(b->name);
            hash_t key = string_hash(b->name, name_length);

            #if 0
            char profile_msg_buffer[128];
            string_t profile_msg = string_format(STRING_BUFFER_msg_buffer), S("[%d:%d] %.*s(%llu) -> %.4lg ms\n"), b->id, b->parentid, (int)string_length(b->name), b->name, key, diff_ms);
            system_process_debug_output(STRING_ARGS(profile_msg));
            #endif

            auto bs_compare = [](const void* _a, const void* _b)
            {
                hash_t tkey = *(const hash_t*)_a;
                hash_t rkey = ((const profile_tracker_t*)_b)->key;
                if (rkey == tkey)
                    return 0;
                return (rkey < tkey) ? -1 : 1;
            };

            if (_trackers_lock.shared_lock())
            {
                const uint32_t tracker_count = array_size(_trackers);
                int32_t insert_at = tracker_count ? bx::binarySearch(key, &_trackers[0].key, tracker_count, sizeof(profile_tracker_t), bs_compare) : -1;
                if (insert_at >= 0)
                {
                    profile_tracker_t& t = _trackers[insert_at];
                    t.counter++;
                    t.last = diff_ms;
                    t.sum += diff_ms;
                    t.avg = t.sum / t.counter;
                    if (diff_ms < t.min)
                        t.min = diff_ms;
                    if (diff_ms > t.max)
                        t.max = diff_ms;
                    t.end = b->end;

                    _trackers_lock.shared_unlock();
                }
                else
                {
                    if (_trackers_lock.shared_unlock() && _trackers_lock.exclusive_lock())
                    {
                        insert_at = tracker_count ? bx::binarySearch(key, &_trackers[0].key, tracker_count, sizeof(profile_tracker_t), bs_compare) : -1;
                        FOUNDATION_ASSERT(insert_at < 0);
                        if (insert_at < 0)
                        {
                            profile_tracker_t tracker;
                            tracker.key = key;
                            tracker.counter = 1;
                            tracker.start = b->start;
                            tracker.end = b->end;
                            tracker.last = tracker.min = tracker.max = tracker.avg = tracker.sum = diff_ms;
                            string_copy(tracker.name, ARRAY_COUNT(tracker.name), b->name, name_length);
                            array_insert_memcpy(_trackers, ~insert_at, &tracker);
                        }

                        _trackers_lock.exclusive_unlock();
                    }
                }
            }
        }
        else
        {
            //char profile_msg[128];
            //string_format(STRING_BUFFER(profile_msg), S("====== EVENT (%s:%llu) %d ======\n"), b->name, b->end, b->id);
            //system_process_debug_output(profile_msg);
        }
    }
    if (_profile_stream)
        stream_write(_profile_stream, buffer, size);
}

FOUNDATION_STATIC void profiler_render_time_ms_column(double time_ms)
{
    static const ImColor TEXT_TIME_US = ImColor::HSV(140 / 360.0f, 0.20f, 0.65f);

    ImGui::NextColumn();
    if (time_ms >= 1.0f)
        ImGui::TrText("%4.0lf ms", time_ms);
    else
        ImGui::TextColored(TEXT_TIME_US, "%4.0lf us", time_ms * 1000.0f);
}

FOUNDATION_FORCEINLINE double profiler_table_format_time(double time_ms)
{
    if (time_ms > 100)
        return math_round(time_ms);
    return time_ms;
}

FOUNDATION_STATIC cell_t profiler_table_name(table_element_ptr_t element, const column_t* column)
{
    profile_tracker_t* t = (profile_tracker_t*)element;
    return t->name;
}

FOUNDATION_STATIC cell_t profiler_table_avg(table_element_ptr_t element, const column_t* column)
{
    profile_tracker_t* t = (profile_tracker_t*)element;
    return profiler_table_format_time(t->avg);
}

FOUNDATION_STATIC cell_t profiler_table_min(table_element_ptr_t element, const column_t* column)
{
    profile_tracker_t* t = (profile_tracker_t*)element;
    return profiler_table_format_time(t->min);
}

FOUNDATION_STATIC cell_t profiler_table_max(table_element_ptr_t element, const column_t* column)
{
    profile_tracker_t* t = (profile_tracker_t*)element;
    return profiler_table_format_time(t->max);
}

FOUNDATION_STATIC cell_t profiler_table_last(table_element_ptr_t element, const column_t* column)
{
    profile_tracker_t* t = (profile_tracker_t*)element;
    return profiler_table_format_time(t->last);
}

FOUNDATION_STATIC cell_t profiler_table_sample_count(table_element_ptr_t element, const column_t* column)
{
    profile_tracker_t* t = (profile_tracker_t*)element;
    return (double)t->counter;
}

FOUNDATION_STATIC void profiler_create_table()
{
    _profiler_table = table_allocate("Profiler#9");
    const float value_column_width = imgui_get_font_ui_scale(80.0f);
    table_add_column(_profiler_table, "Name", profiler_table_name, COLUMN_FORMAT_TEXT, COLUMN_SORTABLE | COLUMN_FREEZE);
    table_add_column(_profiler_table, ICON_MD_TIMER "||Avg", profiler_table_avg, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE).set_width(value_column_width * 1.1f);
    table_add_column(_profiler_table, ICON_MD_TRENDING_DOWN "||Min", profiler_table_min, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE).set_width(value_column_width);
    table_add_column(_profiler_table, ICON_MD_TRENDING_UP "||Max", profiler_table_max, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE).set_width(value_column_width);
    table_add_column(_profiler_table, ICON_MD_LAST_PAGE "||Last", profiler_table_last, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE).set_width(value_column_width);
    table_add_column(_profiler_table, ICON_MD_NUMBERS "||Sample", profiler_table_sample_count, COLUMN_FORMAT_NUMBER, COLUMN_SORTABLE | COLUMN_NUMBER_ABBREVIATION)
        .set_width(imgui_get_font_ui_scale(70.0f));
}

FOUNDATION_STATIC void profiler_window_render()
{
    static bool window_opened_once = false;
    if (!window_opened_once)
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(890, 720), ImVec2(INFINITY, INFINITY));
    }

    if (ImGui::Begin("Profiler##1", &_profiler_window_opened, ImGuiWindowFlags_AlwaysUseWindowPadding))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));

        if (_profiler_table == nullptr)
            profiler_create_table();

        if (_trackers_lock.shared_lock())
        {
            table_render(_profiler_table, _trackers, array_size(_trackers), sizeof(profile_tracker_t), 0.0f, 0.0f);

            _trackers_lock.shared_unlock();
        }

        ImGui::PopStyleVar(2);
    }

    ImGui::End();

    if (_profiler_window_opened == false)
    {
        table_deallocate(_profiler_table);
        _profiler_table = nullptr;
    }
}

FOUNDATION_STATIC void profiler_menu()
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::TrBeginMenu("Windows"))
        {
            ImGui::TrMenuItem(ICON_MD_LOGO_DEV " Profiler", nullptr, &_profiler_window_opened);
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    if (_profiler_window_opened)
        profiler_window_render();
}

//
// # PUBLIC API
//

void profiler_menu_timer()
{
    #if BUILD_DEVELOPMENT && BUILD_ENABLE_PROFILE
    {
        static tick_t last_frame_tick = time_current();
        tick_t elapsed_ticks = time_diff(last_frame_tick, time_current());

        static unsigned index = 0;
        static double elapsed_times[16] = { 0.0 };
        elapsed_times[index++ % ARRAY_COUNT(elapsed_times)] = time_ticks_to_milliseconds(elapsed_ticks);
        const double smooth_elapsed_time = math_average(elapsed_times, ARRAY_COUNT(elapsed_times));
        const double tick_elapsed_time = main_tick_elapsed_time_ms();

        if (tick_elapsed_time > 4)
        {
            const auto& mem_stats = memory_statistics();

            char frame_time[32];
            if (tick_elapsed_time < smooth_elapsed_time - 1)
            {
                string_format(STRING_BUFFER(frame_time), STRING_CONST("%.0lf/%.0lf ms (%.4lg mb)"),
                    tick_elapsed_time, smooth_elapsed_time, mem_stats.allocated_current / 1024.0 / 1024.0);
            }
            else
            {
                string_format(STRING_BUFFER(frame_time), STRING_CONST("%.0lf ms (%.4lg mb)"),
                    tick_elapsed_time, mem_stats.allocated_current / 1024.0 / 1024.0);
            }

            ImGui::MenuItem(frame_time, nullptr, nullptr, false);
        }

        last_frame_tick = time_current();
    }
    #endif
}

//
// # SYSTEM
//

FOUNDATION_STATIC void profiler_initialize()
{
    if (!environment_command_line_arg("profile"))
        return;
        
    const size_t profile_buffer_size = 2 * 1024 * 1024;
    const application_t* app = environment_application();
    _profile_buffer = (uint8_t*)memory_allocate(HASH_PROFILER, profile_buffer_size, 0, MEMORY_PERSISTENT);
    profile_initialize(STRING_ARGS(app->name), _profile_buffer, profile_buffer_size);
    profile_enable(true);

    string_const_t session_profile_file_path;
    if (environment_command_line_arg("profile-log", &session_profile_file_path))
    {
        if (string_is_null(session_profile_file_path))
        {
            string_const_t timestamp = string_from_uint_static(time_current(), false, 0, 0);
            session_profile_file_path = session_get_user_file_path(STRING_ARGS(timestamp), 
                STRING_CONST("profiles"), STRING_CONST("profile"), true);
        }
        _profile_stream = fs_open_file(STRING_ARGS(session_profile_file_path), STREAM_CREATE | STREAM_OUT | STREAM_BINARY);
        FOUNDATION_ASSERT(_profile_stream);
    }

    profile_set_output(profiler_tracker);
    _profiler_initialized = true;

    service_register_menu(HASH_PROFILER, profiler_menu);
}

FOUNDATION_STATIC void profiler_shutdown()
{
    if (_profiler_table)
        table_deallocate(_profiler_table);

    if (_profile_stream)
    {
        string_const_t profile_log_saved_path = stream_path(_profile_stream);
        if (!string_is_null(profile_log_saved_path))
            log_infof(HASH_PROFILER, STRING_CONST("Session profile log saved at %.*s"), STRING_FORMAT(profile_log_saved_path));
        stream_deallocate(_profile_stream);
    }
    
    if (_profiler_initialized)
        profile_finalize();

    memory_deallocate(_profile_buffer);
    array_deallocate(_trackers);
}

DEFINE_SERVICE(PROFILER, profiler_initialize, profiler_shutdown, SERVICE_PRIORITY_UI_HEADLESS);

#endif
