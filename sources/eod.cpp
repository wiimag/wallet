/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */

#include "eod.h"

#include <framework/app.h>
#include <framework/glfw.h>
#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/scoped_string.h>
#include <framework/module.h>
#include <framework/dispatcher.h>
#include <framework/string.h>
#include <framework/console.h>

#include <foundation/fs.h>
#include <foundation/stream.h>
#include <foundation/version.h>

#define HASH_EOD static_hash_string("eod", 3, 0x35f39422e491f3e1ULL)

#if BUILD_BACKEND
#define EOD_URL "https://wallet.wiimag.com"
#else
#define EOD_URL "https://eodhistoricaldata.com"
#endif

constexpr const char EOD_API_URL_KEY[] = "eod_api_url";

static struct EOD_MODULE {
    
    char KEY[32] = { '\0' };
    char API_URL[128] = EOD_URL "/api/";
    
    bool CONNECTED = true; // We assume that we are connected by default.
    
    char STATUS[128] = "Disconnected";
    char SUBSCRIPTION_TYPE[64] = "demo";
    char USER_NAME[64] = "";
    char USER_EMAIL[64] = "";
    char USAGE_LABEL[64] = "EOD";

    double CAPACITY = 0.0;
    double API_CALLS = 0.0;
    double API_LIMIT = 1.0;

    volatile tick_t UPDATE_TICK = 0;

    bool PROMPT_EOD_API_KEY = false;
    
} *EOD;

static const ImColor green = ImColor::HSV(150 / 360.0f, 0.4f, 0.6f);
static const ImColor red = ImColor::HSV(356 / 360.0f, 0.42f, 0.97f); // hsv(356, 42%, 97%)
static const ImColor gray = ImColor::HSV(155 / 360.0f, 0.05f, 0.85f); // hsv(155, 6%, 84%)

FOUNDATION_STATIC const char* eod_ensure_key_loaded()
{
    FOUNDATION_ASSERT_MSG(EOD, "EOD module not initialized, maybe there is a module that has a higher priority than EOD?");

    if (EOD->KEY[0] != '\0')
        return EOD->KEY;

    // Load the EOD user API url and ensure it ends with /
    string_t eod_url{};
    string_const_t eod_url_command_line{};
    if (environment_argument("backend", &eod_url_command_line))
    {
        eod_url = string_copy(STRING_BUFFER(EOD->API_URL), STRING_ARGS(eod_url_command_line));

        // Append the /api/
        if (eod_url.str[eod_url.length - 1] != '/')
            eod_url = string_append(EOD->API_URL, eod_url.length, sizeof(EOD->API_URL), STRING_CONST("/api/"));
        else
            eod_url = string_append(EOD->API_URL, eod_url.length, sizeof(EOD->API_URL), STRING_CONST("api/"));
    }
    else
    {
        eod_url = session_get_string(EOD_API_URL_KEY, STRING_BUFFER(EOD->API_URL), EOD_URL "/api/");
    }

    // Lets make sure the url ends with a / and starts with http
    if (string_length(eod_url.str) < 4 || string_compare(eod_url.str, 4, STRING_CONST("http")) != 0)
        eod_url = string_copy(STRING_BUFFER(EOD->API_URL), STRING_CONST(EOD_URL "/api/"));
    else if (EOD->API_URL[string_length(EOD->API_URL) - 1] != '/')
        string_append(EOD->API_URL, eod_url.length, sizeof(EOD->API_URL), STRING_CONST("/"));

    string_const_t eod_api_key{};
    if (environment_argument("eod-api-key", &eod_api_key))
        return string_copy(STRING_BUFFER(EOD->KEY), STRING_ARGS(eod_api_key)).str;

    string_const_t eod_key_file_path = session_get_user_file_path(STRING_CONST("eod.key"));
    if (!fs_is_file(STRING_ARGS(eod_key_file_path)))
        return string_copy(STRING_BUFFER(EOD->KEY), STRING_CONST("demo")).str;
    
    stream_t* key_stream = fs_open_file(STRING_ARGS(eod_key_file_path), STREAM_IN);
    if (key_stream == nullptr)
        return nullptr;

    scoped_string_t key = stream_read_string(key_stream);
    string_copy(EOD->KEY, sizeof(EOD->KEY), STRING_ARGS(key.value));
    stream_deallocate(key_stream);
    return EOD->KEY;
}

FOUNDATION_STATIC uint64_t eod_fix_invalid_cache_query_after_seconds(uint64_t& invalid_cache_query_after_seconds)
{
    if (!EOD->CONNECTED || eod_is_at_capacity())
        return UINT64_MAX;

    // No need to refresh information on the weekend as often since the stock market doesn't move at this time.
    if (invalid_cache_query_after_seconds != UINT64_MAX && time_is_weekend())
        invalid_cache_query_after_seconds *= 32;
    return invalid_cache_query_after_seconds;
}

bool eod_is_at_capacity()
{
    return EOD->CAPACITY >= 1.0;
}

double eod_capacity()
{
    return EOD->CAPACITY;
}

bool eod_availalble()
{
    return eod_connected() && !eod_is_at_capacity();
}

bool eod_connected()
{
    return EOD->CONNECTED;
}

string_t eod_get_key()
{
    eod_ensure_key_loaded();
    return string_t{ STRING_BUFFER(EOD->KEY) };
}

bool eod_save_key(string_t eod_key)
{
    eod_key.length = string_length(eod_key.str);

    // Force demo key if the key is empty
    if (eod_key.length == 0)
        eod_key = string_copy(STRING_BUFFER(EOD->KEY), STRING_CONST("demo"));
    else if (eod_key.str != EOD->KEY)
        string_copy(STRING_BUFFER(EOD->KEY), STRING_ARGS(eod_key));
        
    if (eod_key.length)
        console_add_secret_key_token(STRING_ARGS(eod_key));

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
    const char* api_key = eod_ensure_key_loaded();
    string_t EOD_URL_BUFFER = string_static_buffer(2048);

    string_const_t HOST_API = string_const(EOD->API_URL, string_length(EOD->API_URL));
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

    if (format == FORMAT_JSON || format == FORMAT_JSON_CACHE || format == FORMAT_JSON_WITH_ERROR)
        eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("&fmt=json"));
    else if (format == FORMAT_CSV)
        eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("&fmt=csv"));

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

    string_const_t HOST_API = string_const(EOD_URL);
    string_t url = string_copy(STRING_BUFFER(IMAGE_URL_BUFFER), STRING_ARGS(HOST_API));
    
    return string_append(STRING_ARGS_BUFFER(url, IMAGE_URL_BUFFER), image_url, image_url_length).str;
}

const char* eod_build_url(const char* api, query_format_t format, const char* uri_format, ...)
{    
    static thread_local char URL_BUFFER[2048];

    string_const_t HOST_API = string_const(EOD->API_URL, string_length(EOD->API_URL));
    string_t url = string_copy(STRING_BUFFER(URL_BUFFER), STRING_ARGS(HOST_API));
    if (url.str[url.length - 1] != '/')
        url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), STRING_CONST("/"));
        
    if (string_length(api) > 0)
    {
        url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), api, string_length(api));
        if (url.str[url.length - 1] != '/')
            url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), STRING_CONST("/"));
    }

    va_list list;
    va_start(list, uri_format);
    char uri_formatted_buffer[2048] = { '\0' };
    string_t uri_formatted = string_vformat(STRING_BUFFER(uri_formatted_buffer), uri_format, string_length(uri_format), list);
    
    url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), STRING_ARGS(uri_formatted));
    va_end(list);
    
    const size_t qm_pos = string_rfind(STRING_ARGS(url), '?', STRING_NPOS);
    if (format != FORMAT_UNDEFINED)
    {
        if (qm_pos == STRING_NPOS)
            url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), STRING_CONST("?"));
        else
            url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), STRING_CONST("&"));

        url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), STRING_CONST("fmt="));
        if (format == FORMAT_JSON || format == FORMAT_JSON_CACHE)
            url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), STRING_CONST("json"));
        else
            url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), STRING_CONST("csv"));
    }

    const char* api_key = eod_ensure_key_loaded();
    const size_t api_key_length = string_length(api_key);
    if (api_key_length > 0)
    {
        if (qm_pos == STRING_NPOS)
            url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), STRING_CONST("?"));
        else
            url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), STRING_CONST("&"));

        url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), STRING_CONST("api_token="));
        url = string_append(STRING_ARGS_BUFFER(url, URL_BUFFER), api_key, api_key_length);
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

bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 0*/)
{
    return eod_fetch_async(api, ticker, format, nullptr, nullptr, nullptr, nullptr, json_callback, invalid_cache_query_after_seconds);
}

bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 0*/)
{
    return eod_fetch_async(api, ticker, format, param1, value1, nullptr, nullptr, json_callback, invalid_cache_query_after_seconds);
}

bool eod_fetch(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 15ULL * 60ULL*/)
{
    string_const_t url = eod_build_url(api, ticker, format, param1, value1, param2, value2);

    if (!EOD->CONNECTED && format != FORMAT_JSON_WITH_ERROR)
        log_warnf(HASH_EOD, WARNING_NETWORK, STRING_CONST("Query to %.*s might fail as we are not connected to EOD services."), STRING_FORMAT(url));

    return query_execute_json(url.str, format, json_callback, eod_fix_invalid_cache_query_after_seconds(invalid_cache_query_after_seconds));
}

char* eod_api_url_buffer()
{
    FOUNDATION_ASSERT(EOD);
    return EOD->API_URL;
}

size_t eod_api_url_buffer_capacity()
{
    FOUNDATION_ASSERT(EOD);
    return sizeof(EOD->API_URL);
}

const char* eod_save_api_url(const char* url)
{
    const size_t url_length = string_length(url); 
    if (url_length == 0)
    {
        session_clear_value(EOD_API_URL_KEY);
        return string_copy(STRING_BUFFER(EOD->API_URL), STRING_CONST(EOD_URL "/api/")).str;
    }

    FOUNDATION_ASSERT(EOD);
    string_t saved_url = string_copy(STRING_BUFFER(EOD->API_URL), url, string_length(url));

    // Ensure URL ends with a slash.
    if (saved_url.str[saved_url.length - 1] != '/')
        saved_url = string_append(STRING_ARGS_BUFFER(saved_url, EOD->API_URL), STRING_CONST("/"));

    // Save to user settings.
    if (!session_set_string(EOD_API_URL_KEY, STRING_ARGS(saved_url)))
    {
        log_warnf(HASH_EOD, WARNING_SYSTEM_CALL_FAIL, STRING_CONST("Failed to save EOD API URL to user settings"));
        return nullptr;
    }

    return saved_url.str;
}

string_const_t eod_web_site_url()
{
    return string_const(EOD_URL);
}

bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 0*/)
{
    string_const_t url = eod_build_url(api, ticker, format, param1, value1, param2, value2);

    if (!EOD->CONNECTED && format != FORMAT_JSON_WITH_ERROR)
        log_warnf(HASH_EOD, WARNING_NETWORK, STRING_CONST("Query to %.*s might fail as we are not connected to EOD services."), STRING_FORMAT(url));

    return query_execute_async_json(url.str, format, json_callback, eod_fix_invalid_cache_query_after_seconds(invalid_cache_query_after_seconds));
}

FOUNDATION_STATIC void eod_update_window_title()
{
    if (EOD == nullptr || main_is_batch_mode())
        return;
        
    GLFWwindow* window = glfw_main_window();
    FOUNDATION_ASSERT(window);

    extern const char* app_title();

    const bool is_main_branch = string_equal(STRING_CONST(GIT_BRANCH), STRING_CONST("main")) ||
        string_equal(STRING_CONST(GIT_BRANCH), STRING_CONST("master"));
        
    string_const_t branch_name{ EOD->SUBSCRIPTION_TYPE, string_length(EOD->SUBSCRIPTION_TYPE)};
    if (main_is_running_tests())
        branch_name = CTEXT("tests");
    else if (!is_main_branch)
        branch_name = string_to_const(GIT_BRANCH);

    string_const_t license_name = EOD->CONNECTED && EOD->USER_NAME[0] != 0 ? string_to_const(EOD->USER_NAME) : RTEXT("disconnected");
    string_const_t version_string = string_from_version_static(version_make(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH, VERSION_BUILD, 0));

    char title[128] = PRODUCT_NAME;
    if (license_name.length)
    {
        string_format(STRING_BUFFER(title), STRING_CONST("%s (%.*s) [%.*s] v.%.*s"),
            app_title(), STRING_FORMAT(license_name), STRING_FORMAT(branch_name), STRING_FORMAT(version_string));
    }
    else
    {
        string_format(STRING_BUFFER(title), STRING_CONST("%s [%.*s] v.%.*s"),
            app_title(), STRING_FORMAT(branch_name), STRING_FORMAT(version_string));
    }

    glfwSetWindowTitle(window, title);
}

FOUNDATION_STATIC void eod_refresh()
{
    EOD->UPDATE_TICK = 0;
}

FOUNDATION_STATIC void eod_show_login_dialog()
{
    EOD->PROMPT_EOD_API_KEY = true;
    app_open_dialog("Enter EOD API KEY", [](void*)->bool
    {     
        // Explain that the EOD api needs to be set
        ImGui::TextURL(tr("EOD API Key"), nullptr, STRING_CONST("https://eodhistoricaldata.com/r/?ref=PF9TZC2T"));
        ImGui::TextWrapped(tr("EOD API Key is required to use this application."));
        ImGui::NewLine();
        ImGui::TrTextWrapped("You can get a free API key by registering at the link above. Please enter your API key below and press Continue");

        ImGui::NewLine();
        string_t eod_key = eod_get_key();
        ImGui::ExpandNextItem();
        if (ImGui::InputTextWithHint("##EODKey", "demo", eod_key.str, eod_key.length,
            ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_Password))
        {
            eod_save_key(eod_key);
        }

        static float continue_button_width = IM_SCALEF(100);
        ImGui::MoveCursor(ImGui::GetContentRegionAvail().x - continue_button_width, 0);
        if (ImGui::Button(tr("Continue"), {IM_SCALEF(100), IM_SCALEF(30)}))
        {
            eod_refresh();
            return false;
        }
        continue_button_width = ImGui::GetItemRectSize().x;

        return true;
    }, IM_SCALEF(300), IM_SCALEF(250), false, nullptr, nullptr);
}

FOUNDATION_STATIC void eod_update_status(const json_object_t& json)
{
    const bool previous_connection_status = EOD->CONNECTED;

    EOD->CONNECTED = json.error_code == 0 && json.status_code < 400;
    EOD->API_CALLS = EOD->CONNECTED ? json["apiRequests"].as_number() : 0;
    EOD->API_LIMIT = EOD->CONNECTED ? json["dailyRateLimit"].as_number() : 1;
    EOD->CAPACITY = EOD->API_CALLS / EOD->API_LIMIT;

    string_const_t name = EOD->CONNECTED ? json["name"].as_string() : RTEXT("Disconnected");
    string_const_t email = EOD->CONNECTED ? json["email"].as_string() : RTEXT("Disconnected");
    string_const_t subtype = EOD->CONNECTED ? json["subscriptionType"].as_string() : RTEXT("Disconnected");

    if (EOD->CONNECTED)
    {
        string_copy(STRING_BUFFER(EOD->USER_NAME), STRING_ARGS(name));
        string_copy(STRING_BUFFER(EOD->USER_EMAIL), STRING_ARGS(email));
        string_copy(STRING_BUFFER(EOD->SUBSCRIPTION_TYPE), STRING_ARGS(subtype));
    }

    string_const_t fmttr = RTEXT("Name: %.*s\nEmail: %.*s\nSubscription: %.*s\nRequest: %lg/%lg");
    string_format(STRING_BUFFER(EOD->STATUS), STRING_ARGS(fmttr),
        STRING_FORMAT(name), STRING_FORMAT(email), STRING_FORMAT(subtype), EOD->API_CALLS, EOD->API_LIMIT);

    fmttr = RTEXT("EOD [API USAGE %.3lg %%]");
    string_format(STRING_BUFFER(EOD->USAGE_LABEL), STRING_ARGS(fmttr), EOD->API_CALLS * 100 / EOD->API_LIMIT);

    EOD->UPDATE_TICK = time_current();

    #if BUILD_APPLICATION
    dispatch(eod_update_window_title);

    const bool is_demo_key = string_equal(STRING_LENGTH(EOD->KEY), STRING_CONST("demo"));
    const bool is_wallet_key = string_equal(STRING_LENGTH(EOD->KEY), STRING_CONST("wallet"));

    // If we are still disconnected and no valid key is set, show the login dialog
    if (!is_wallet_key && !EOD->PROMPT_EOD_API_KEY && (!EOD->CONNECTED || is_demo_key))
        eod_show_login_dialog();
    #endif
}

FOUNDATION_STATIC void eod_update()
{    
    if (time_elapsed(EOD->UPDATE_TICK) > 60)
    {
        EOD->UPDATE_TICK = time_current();
        eod_fetch_async("user", "", FORMAT_JSON_WITH_ERROR, eod_update_status);
    }
}

FOUNDATION_STATIC void eod_main_menu_status()
{
    GLFWwindow* window = glfw_main_window();
    FOUNDATION_ASSERT(window);

    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 window_size = ImGui::GetWindowSize();
    const float space = ImGui::GetContentRegionAvail().x;
    const float content_width = ImGui::CalcTextSize(EOD->USAGE_LABEL).x + style.FramePadding.x * 2.0f;
    const ImVec2 status_box_size(IM_SCALEF(18.0f), IM_SCALEF(18.0f));

    ImGui::MoveCursor(space - content_width - status_box_size.x - style.FramePadding.x * 2.0f, 0);
    ImGui::BeginGroup();
    if (ImGui::BeginMenu(EOD->USAGE_LABEL))
    {
        string_t eod_key = eod_get_key();

        if (ImGui::MenuItem(tr("Refresh")))
            eod_refresh();

        ImGui::Separator();
        ImGui::TextURL("EOD API Key", nullptr, STRING_CONST("https://eodhistoricaldata.com/r/?ref=PF9TZC2T"));
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
            eod_refresh();
        else if (eod_is_at_capacity())
        {
            ImGui::SetTooltip(
                tr("%s\n\nYou exceeded your daily EOD API requests limit.\n"
                "Please contact support@eodhistoricaldata.com.\n\n"
                "All request will use the local cache if available."), EOD->STATUS);
        }
        else
        {
            ImGui::SetTooltip(tr("%s\n\nConnected through %s"), EOD->STATUS, EOD->API_URL);
        }
    }

    const ImRect status_box(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const ImVec2 status_box_center = status_box.GetCenter() + ImVec2(IM_SCALEF(-2.0f), IM_SCALEF(2.0f));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddCircleFilled(status_box_center, status_box_size.x / 2.0f, EOD->CONNECTED ? (eod_is_at_capacity() ? red : green) : gray);

    ImGui::EndGroup();
}

//
// # SYSTEM
//

FOUNDATION_STATIC void eod_initialize()
{
    EOD = MEM_NEW(HASH_EOD, EOD_MODULE);

    const char* key = eod_ensure_key_loaded();
    const size_t key_length = string_length(key);
    if (key_length)
        console_add_secret_key_token(key, key_length);
    
    module_register_update(HASH_EOD, eod_update);

    if (main_is_interactive_mode())
        module_register_menu_status(HASH_EOD, eod_main_menu_status);


    eod_update_window_title();
}

FOUNDATION_STATIC void eod_shutdown()
{
    MEM_DELETE(EOD);
}

DEFINE_MODULE(EOD, eod_initialize, eod_shutdown, MODULE_PRIORITY_BASE);
