/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "eod.h"
#include "version.h"

#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/scoped_string.h>
#include <framework/service.h>
#include <framework/dispatcher.h>

#include <foundation/fs.h>
#include <foundation/stream.h>
#include <foundation/version.h>

#define HASH_EOD static_hash_string("eod", 3, 0x35f39422e491f3e1ULL)

// "https://eodhistoricaldata.com/api/exchange-symbol-list/TO?api_token=XYZ&fmt=json"

static char EOD_KEY[32] = { '\0' };
static const ImColor green = ImColor::HSV(150 / 360.0f, 0.4f, 0.6f);
static const ImColor red = ImColor::HSV(356 / 360.0f, 0.42f, 0.97f); // hsv(356, 42%, 97%)
static const ImColor gray = ImColor::HSV(155 / 360.0f, 0.05f, 0.85f); // hsv(155, 6%, 84%)

FOUNDATION_STATIC const char* ensure_key_loaded()
{
    if (EOD_KEY[0] != '\0')
        return EOD_KEY;

    const string_const_t& eod_key_file_path = session_get_user_file_path(STRING_CONST("eod.key"));
    if (!fs_is_file(STRING_ARGS(eod_key_file_path)))
        return string_copy(STRING_CONST_CAPACITY(EOD_KEY), STRING_CONST("demo")).str;
    
    stream_t* key_stream = fs_open_file(STRING_ARGS(eod_key_file_path), STREAM_IN);
    if (key_stream == nullptr)
        return nullptr;

    scoped_string_t key = stream_read_string(key_stream);
    string_copy(EOD_KEY, sizeof(EOD_KEY), STRING_ARGS(key.value));
    stream_deallocate(key_stream);
    return EOD_KEY;
}

string_t eod_get_key()
{
    ensure_key_loaded();
    return string_t{ STRING_CONST_CAPACITY(EOD_KEY) };
}

bool eod_save_key(string_t eod_key)
{
    eod_key.length = string_length(eod_key.str);
    log_infof(0, STRING_CONST("Saving EOD %.*s"), STRING_FORMAT(eod_key));

    if (eod_key.str != EOD_KEY)
        string_copy(STRING_CONST_CAPACITY(EOD_KEY), STRING_ARGS(eod_key));

    const string_const_t& eod_key_file_path = session_get_user_file_path(STRING_CONST("eod.key"));
    stream_t* key_stream = fs_open_file(STRING_ARGS(eod_key_file_path), STREAM_CREATE | STREAM_OUT | STREAM_TRUNCATE);
    if (key_stream == nullptr)
        return false;

    log_infof(0, STRING_CONST("Writing key file %.*s"), STRING_FORMAT(eod_key_file_path));
    stream_write_string(key_stream, STRING_ARGS(eod_key));
    stream_deallocate(key_stream);
    return true;
}

string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format)
{
    return eod_build_url(api, ticker, format, nullptr, nullptr);
}

string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1)
{
    return eod_build_url(api, ticker, format, param1, value1, nullptr, nullptr);
}

string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2)
{
    string_t EOD_URL_BUFFER = string_static_buffer(2048);
    const char* api_key = ensure_key_loaded();

    string_const_t HOST_API = string_const(STRING_CONST("https://eodhistoricaldata.com/api/"));
    string_t eod_url = string_copy(EOD_URL_BUFFER.str, EOD_URL_BUFFER.length, STRING_ARGS(HOST_API));
    eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, api, string_length(api));
    eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("/"));

    if (ticker)
    {
        string_const_t escaped_ticker = url_encode(ticker);
        eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_ARGS(escaped_ticker));
    }
    eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("?api_token="));
    eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, api_key, string_length(api_key));

    if (format != FORMAT_UNDEFINED)
    {
        eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("&fmt="));
        if (format == FORMAT_JSON || format == FORMAT_JSON_CACHE)
            eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("json"));
        else
            eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("csv"));
    }

    if (param1 != nullptr)
    {
        eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("&"));
        eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, param1, string_length(param1));

        if (value1 != nullptr) 
        {
            string_const_t escaped_value1 = url_encode(value1);
            eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("="));
            eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_ARGS(escaped_value1));
        }

        if (param2 != nullptr)
        {
            eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("&"));
            eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, param2, string_length(param2));

            if (value2 != nullptr)
            {
                string_const_t escaped_value2 = url_encode(value2);
                eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("="));
                eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_ARGS(escaped_value2));
            }
        }
    }

    return string_const(STRING_ARGS(eod_url));
}

const char* eod_build_image_url(const char* image_url, size_t image_url_length)
{
    static thread_local char IMAGE_URL_BUFFER[2048];

    string_const_t HOST_API = string_const(STRING_CONST("https://eodhistoricaldata.com"));
    string_t url = string_copy(STRING_CONST_CAPACITY(IMAGE_URL_BUFFER), STRING_ARGS(HOST_API));
    
    return string_append(STRING_ARGS(url), STRING_CONST_LENGTH(IMAGE_URL_BUFFER), image_url, image_url_length).str;
}

const char* eod_build_url(const char* api, query_format_t format, const char* uri_format, ...)
{    
    static thread_local char URL_BUFFER[2048];

    string_const_t HOST_API = CTEXT("https://eodhistoricaldata.com/api/");
    string_t url = string_copy(STRING_CONST_CAPACITY(URL_BUFFER), STRING_ARGS(HOST_API));
    if (url.str[url.length - 1] != '/')
        url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), STRING_CONST("/"));
        
    if (string_length(api) > 0)
    {
        url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), api, string_length(api));
        if (url.str[url.length - 1] != '/')
            url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), STRING_CONST("/"));
    }

    va_list list;
    va_start(list, uri_format);
    char uri_formatted_buffer[2048] = { '\0' };
    string_t uri_formatted = string_vformat(STRING_CONST_CAPACITY(uri_formatted_buffer), uri_format, string_length(uri_format), list);
    
    url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), STRING_ARGS(uri_formatted));
    va_end(list);
    
    const size_t qm_pos = string_rfind(STRING_ARGS(url), '?', STRING_NPOS);
    if (format != FORMAT_UNDEFINED)
    {
        if (qm_pos == STRING_NPOS)
            url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), STRING_CONST("?"));
        else
            url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), STRING_CONST("&"));

        url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), STRING_CONST("fmt="));
        if (format == FORMAT_JSON || format == FORMAT_JSON_CACHE)
            url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), STRING_CONST("json"));
        else
            url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), STRING_CONST("csv"));
    }

    const char* api_key = ensure_key_loaded();
    const size_t api_key_length = string_length(api_key);
    if (api_key_length > 0)
    {
        if (qm_pos == STRING_NPOS)
            url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), STRING_CONST("?"));
        else
            url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), STRING_CONST("&"));

        url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), STRING_CONST("api_token="));
        url = string_append(STRING_ARGS(url), STRING_CONST_LENGTH(URL_BUFFER), api_key, api_key_length);
    }
    
    return url.str;
}

bool eod_fetch(const char* api, const char* ticker, query_format_t format, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 15ULL * 60ULL*/)
{
    return eod_fetch(api, ticker, format, nullptr, nullptr, nullptr, nullptr, json_callback, invalid_cache_query_after_seconds);
}

bool eod_fetch(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 15ULL * 60ULL*/)
{
    return eod_fetch(api, ticker, format, param1, value1, nullptr, nullptr, json_callback, invalid_cache_query_after_seconds);
}

bool eod_fetch(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 15ULL * 60ULL*/)
{
    string_const_t url = eod_build_url(api, ticker, format, param1, value1, param2, value2);
    return query_execute_json(url.str, format, json_callback, invalid_cache_query_after_seconds);
}

bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 0*/)
{
    return eod_fetch_async(api, ticker, format, nullptr, nullptr, nullptr, nullptr, json_callback, invalid_cache_query_after_seconds);
}

bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 0*/)
{
    return eod_fetch_async(api, ticker, format, param1, value1, nullptr, nullptr, json_callback, invalid_cache_query_after_seconds);
}

bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 0*/)
{
    string_const_t url = eod_build_url(api, ticker, format, param1, value1, param2, value2);
    return query_execute_async_json(url.str, format, json_callback, invalid_cache_query_after_seconds);
}

FOUNDATION_STATIC void eod_update_window_title()
{
    GLFWwindow* window = main_window();
    FOUNDATION_ASSERT(window);

    extern const char* app_title();

    eod_fetch_async("user", "", FORMAT_JSON, [window](const json_object_t& json)
    {
        const bool is_main_branch = string_equal(STRING_CONST(GIT_BRANCH), STRING_CONST("main")) ||
            string_equal(STRING_CONST(GIT_BRANCH), STRING_CONST("master"));

        string_const_t subscription = json["subscriptionType"].as_string();
        string_const_t branch_name{ subscription.str, subscription.length };
        if (main_is_running_tests())
            branch_name = CTEXT("tests");
        else if (!is_main_branch)
            branch_name = string_to_const(GIT_BRANCH);

        string_const_t license_name = json["name"].as_string();
        string_const_t version_string = string_from_version_static(version_make(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD, 0));

        static char title[128] = PRODUCT_NAME;
        string_format(STRING_CONST_CAPACITY(title), STRING_CONST("%s (%.*s) [%.*s] v.%.*s"),
            app_title(), STRING_FORMAT(license_name), STRING_FORMAT(branch_name), STRING_FORMAT(version_string));

        dispatch(L0(glfwSetWindowTitle(window, title)));
    });
}

FOUNDATION_STATIC void eod_main_menu_status()
{
    GLFWwindow* window = main_window();
    FOUNDATION_ASSERT(window);
    
    static char eod_status[128] = "";
    static char eod_menu_title[64] = "EOD";
    static volatile bool eod_connected = false;
    static volatile tick_t eod_menu_title_last_update = 0;
    if (time_elapsed(eod_menu_title_last_update) > 60)
    {
        eod_menu_title_last_update = time_current();
        eod_fetch_async("user", "", FORMAT_JSON, [](const json_object_t& json)
        {
            eod_connected = true;
            double api_calls = json["apiRequests"].as_number();
            double api_calls_limit = json["dailyRateLimit"].as_number();
            string_format(STRING_CONST_CAPACITY(eod_menu_title), STRING_CONST("EOD [API USAGE %.2lf %%]"), api_calls * 100 / api_calls_limit);

            string_const_t name = json["name"].as_string();
            string_const_t email = json["email"].as_string();
            string_const_t subtype = json["subscriptionType"].as_string();
            string_format(STRING_CONST_CAPACITY(eod_status), STRING_CONST("Name: %.*s\nEmail: %.*s\nSubscription: %.*s\nRequest: %lg/%lg"),
                STRING_FORMAT(name), STRING_FORMAT(email), STRING_FORMAT(subtype), api_calls, api_calls_limit);

            eod_menu_title_last_update = time_current();
        });
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 window_size = ImGui::GetWindowSize();
    const float space = ImGui::GetContentRegionAvail().x;
    const float content_width = ImGui::CalcTextSize(eod_menu_title).x + style.FramePadding.x * 2.0f;
    const ImVec2 status_box_size(38.0f, 38.0f);

    ImGui::MoveCursor(space - content_width - status_box_size.x - style.FramePadding.x * 2.0f, 0);
    ImGui::BeginGroup();
    if (ImGui::BeginMenu(eod_menu_title))
    {
        string_t eod_key = eod_get_key();

        if (ImGui::MenuItem("Refresh"))
        {
            eod_menu_title_last_update = 0;
            eod_update_window_title();
        }

        ImGui::Separator();
        ImGui::TextURL("EOD API Key", nullptr, STRING_CONST("https://eodhistoricaldata.com"));
        if (ImGui::InputTextWithHint("##EODKey", "demo", eod_key.str, eod_key.length,
            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_Password))
        {
            eod_save_key(eod_key);
        }

        ImGui::EndMenu();
    }

    ImGui::Dummy(status_box_size);
    if (ImGui::IsItemHovered())
    {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, false))
        {
            eod_menu_title_last_update = 0;
            eod_update_window_title();
        }
        else
            ImGui::SetTooltip("%s", eod_status);
    }

    const ImRect status_box(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImVec2 status_box_center = status_box.GetCenter() + ImVec2(-4.0f, 4.0f);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddCircleFilled(status_box_center, status_box_size.x / 2.0f, eod_connected ? green : gray);

    ImGui::EndGroup();
}

//
// # SYSTEM
//

FOUNDATION_STATIC void eod_initialize()
{
    eod_update_window_title();    
    service_register_menu_status(HASH_EOD, eod_main_menu_status);
}

DEFINE_SERVICE(EOD, eod_initialize, nullptr, SERVICE_PRIORITY_HIGH);
