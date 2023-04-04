/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "news.h"

#include "eod.h"
#include "openai.h"
#include "stock.h"

#include <framework/app.h>
#include <framework/imgui.h>
#include <framework/string.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/module.h>
#include <framework/database.h>
#include <framework/dispatcher.h>
#include <framework/string_table.h>
#include <framework/profiler.h>
#include <framework/array.h>
#include <framework/console.h>

#include <foundation/stream.h>

#define HASH_NEWS static_hash_string("news", 4, 0xc804eb289c3e1658ULL)

static char NEWS_GOOGLE_SEARCH_API_KEY[64] = { 0 };

struct news_t
{
    time_t date;
    string_t date_string;
    string_t headline;
    string_t url;
    string_t summary;
    string_t* related;
    string_t* tags;

    double change_p;
    
    double sentiment_polarity;
    double sentiment_positive;
    double sentiment_negative;
    double sentiment_neutral;

    const openai_response_t* openai_response{ nullptr };
};

struct news_window_t
{
    char title[64]{ 0 };
    char symbol[16]{ 0 };

    news_t* news{ nullptr };
    shared_mutex news_mutex;
};

//
// # PRIVATE
//

FOUNDATION_STATIC void news_fetch_data(news_window_t* news_window, const json_object_t& json)
{
    MEMORY_TRACKER(HASH_NEWS);
    
    for (auto n : json)
    {
        time_t date;
        string_const_t date_string = n["date"].as_string();
        if (!string_try_convert_date(date_string.str, min(to_size(10), date_string.length), date))
            continue;
            
        string_const_t title = n["title"].as_string();
        if (string_is_null(title))
            continue;
            
        string_const_t content = string_trim(n["content"].as_string());
        if (string_is_null(content))
            continue;

        string_const_t link = n["link"].as_string();
        if (string_is_null(link))
            continue;

        news_t news{};
        news.date = date;
        news.date_string = string_clone(STRING_ARGS(date_string));
        news.headline = string_utf8_unescape(STRING_ARGS(title));
        news.url = string_utf8_unescape(STRING_ARGS(link));
        news.summary = string_utf8_unescape(STRING_ARGS(content));

        const day_result_t ed = stock_get_eod(STRING_LENGTH(news_window->symbol), date);
        news.change_p = ed.change_p;

        for (auto s : n["symbols"])
        {
            string_const_t tag = s.as_string();
            if (string_is_null(tag))
                continue;
                
            array_push(news.related, string_clone(STRING_ARGS(tag)));                
        }

        for (auto t : n["tags"])
        {
            string_const_t tag = t.as_string();
            if (string_is_null(tag))
                continue;

            array_push(news.tags, string_clone(STRING_ARGS(tag)));
        }

        news.sentiment_polarity = n["sentiment"]["polarity"].as_number();
        news.sentiment_positive = n["sentiment"]["pos"].as_number();
        news.sentiment_negative = n["sentiment"]["neg"].as_number();
        news.sentiment_neutral = n["sentiment"]["neu"].as_number();

        SHARED_WRITE_LOCK(news_window->news_mutex);
        int insert_at = array_binary_search_compare(news_window->news, news.date, LC2(_2 - _1.date));
        if (insert_at < 0)
            insert_at = ~insert_at;
        array_insert_memcpy(news_window->news, insert_at, &news);
    }
}

FOUNDATION_STATIC bool string_try_convert_date_long(const char* str, size_t length, time_t& date)
{
    struct tm tm;
    memset(&tm, 0, sizeof(tm));

    // Check for the format 18 hours ago
    if (length >= 11)
    {
        constexpr string_const_t hours_ago = CTEXT(" hours ago");
        if (string_equal(STRING_ARGS(hours_ago), str + length - hours_ago.length, hours_ago.length))
        {
            int hours = (int)string_to_uint(str, length - hours_ago.length, false);
            date = time_add_hours(time_now(), -hours);
            return true;
        }
    }

    // Check for the format 3 days ago
    if (length >= 10)
    {
        constexpr string_const_t days_ago = CTEXT(" days ago");
        if (string_equal(STRING_ARGS(days_ago), str + length - days_ago.length, days_ago.length))
        {
            int days = (int)string_to_uint(str, length - days_ago.length, false);
            date = time_add_days(time_now(), -days);
            return true;
        }
    }

    // Parse the in the format Feb 16, 2023  
    if (length < 11)
        return string_try_convert_date(str, length, date);

    constexpr string_const_t january = CTEXT("Jan");
    constexpr string_const_t february = CTEXT("Feb");
    constexpr string_const_t march = CTEXT("Mar");
    constexpr string_const_t april = CTEXT("Apr");
    constexpr string_const_t may = CTEXT("May");
    constexpr string_const_t june = CTEXT("Jun");
    constexpr string_const_t july = CTEXT("Jul");
    constexpr string_const_t august = CTEXT("Aug");
    constexpr string_const_t september = CTEXT("Sep");
    constexpr string_const_t october = CTEXT("Oct");
    constexpr string_const_t november = CTEXT("Nov");
    constexpr string_const_t december = CTEXT("Dec");

    if (string_equal(STRING_ARGS(january), str, 3)) tm.tm_mon = 0;
    else if (string_equal(STRING_ARGS(february), str, 3)) tm.tm_mon = 1;
    else if (string_equal(STRING_ARGS(march), str, 3)) tm.tm_mon = 2;
    else if (string_equal(STRING_ARGS(april), str, 3)) tm.tm_mon = 3;
    else if (string_equal(STRING_ARGS(may), str, 3)) tm.tm_mon = 4;
    else if (string_equal(STRING_ARGS(june), str, 3)) tm.tm_mon = 5;
    else if (string_equal(STRING_ARGS(july), str, 3)) tm.tm_mon = 6;
    else if (string_equal(STRING_ARGS(august), str, 3)) tm.tm_mon = 7;
    else if (string_equal(STRING_ARGS(september), str, 3)) tm.tm_mon = 8;
    else if (string_equal(STRING_ARGS(october), str, 3)) tm.tm_mon = 9;
    else if (string_equal(STRING_ARGS(november), str, 3)) tm.tm_mon = 10;
    else if (string_equal(STRING_ARGS(december), str, 3)) tm.tm_mon = 11;
    else
        return string_try_convert_date(str, length, date);

    // Find the , and the space after it
    size_t comma_pos = string_find(str + 4, length - 4, ',', 0);
    if (comma_pos == STRING_NPOS)
        return string_try_convert_date(str, length, date);

    tm.tm_mday = (int)string_to_uint(str + 4, comma_pos, false);
    tm.tm_year = (int)string_to_uint(str + 4 + comma_pos + 2, 4, false) - 1900;

    if (tm.tm_year < 0)
        return string_try_convert_date(str, length, date);

    date = mktime(&tm);
    return true;
}

FOUNDATION_STATIC news_window_t* news_window_allocate(const char* symbol, size_t symbol_length)
{
    news_window_t* news_window = MEM_NEW(HASH_NEWS, news_window_t);

    string_copy(STRING_BUFFER(news_window->symbol), symbol, symbol_length);
    string_const_t news_title_format = RTEXT("News %.*s");
    string_format(STRING_BUFFER(news_window->title), STRING_ARGS(news_title_format), (int)symbol_length, symbol);

    // Fetch symbol news
    if (!eod_fetch_async("news", nullptr, FORMAT_JSON, "s", symbol, "limit", "10", L1(news_fetch_data(news_window, _1))))
    {
        log_warnf(HASH_NEWS, WARNING_RESOURCE, STRING_CONST("Failed to fetch news for symbol %*.s"), (int)symbol_length, symbol);
    }

    if (string_ends_with(symbol, symbol_length, STRING_CONST(".V")) ||
        string_ends_with(symbol, symbol_length, STRING_CONST(".TO")) ||
        string_ends_with(symbol, symbol_length, STRING_CONST(".NEO")))
    {
        string_const_t google_apis_key = string_to_const(NEWS_GOOGLE_SEARCH_API_KEY);
        if (!string_is_null(google_apis_key))
        {
            string_const_t name = stock_get_short_name(symbol, symbol_length);

            char google_search_query_buffer[2048];
            string_t google_search_query = string_format(STRING_BUFFER(google_search_query_buffer), 
                STRING_CONST("https://www.googleapis.com/customsearch/v1?key=%.*s&cx=7363b4123b9a84885&dateRestrict=d30&q=%.*s"),
                STRING_FORMAT(google_apis_key), STRING_FORMAT(name));

            char google_search_query_escaped_buffer[2048];
            string_t google_search_query_escaped = string_escape_url(STRING_BUFFER(google_search_query_escaped_buffer), STRING_ARGS(google_search_query));

            query_execute_async_json(google_search_query_escaped.str, FORMAT_JSON, [news_window](const json_object_t& res)
            {
                for (auto e : res["items"])
                {
                    news_t t{};

                    t.date = time_now();
                    t.date_string = {};
                    t.headline = e["title"].as_string_clone();
                    t.url = e["link"].as_string_clone();

                    string_const_t snippet = e["snippet"].as_string();

                    // Find first ...
                    size_t ddd_pos = string_find_string(STRING_ARGS(snippet), STRING_CONST("..."), 0);
                    if (ddd_pos != STRING_NPOS)
                    {
                        string_const_t date = {snippet.str, ddd_pos};
                        date = string_trim(date);

                        string_const_t content = {snippet.str + ddd_pos + 3, snippet.length - ddd_pos - 3};
                        content = string_trim(content);

                        if (string_try_convert_date_long(STRING_ARGS(date), t.date))
                        {
                            t.date_string = string_clone(STRING_ARGS(date));
                            t.summary = string_clone(STRING_ARGS(content));
                        }
                        else
                        {
                            t.summary = string_clone(STRING_ARGS(snippet));
                        }
                    }
                    else
                    {
                        t.summary = string_clone(STRING_ARGS(snippet));
                    }
                
                    t.sentiment_negative = 0;
                    t.sentiment_polarity = 0;
                    t.sentiment_positive = 0;
                    t.sentiment_neutral = 1;

                    t.tags = nullptr;
                    t.related = nullptr;
                    t.openai_response = nullptr;

                    const day_result_t ed = stock_get_eod(STRING_LENGTH(news_window->symbol), t.date);
                    t.change_p = ed.change_p;

                    SHARED_WRITE_LOCK(news_window->news_mutex);
                    int insert_at = array_binary_search_compare(news_window->news, t.date, LC2(_2 - _1.date));
                    if (insert_at < 0)
                        insert_at = ~insert_at;
                    array_insert_memcpy(news_window->news, insert_at, &t);
                }
            });
        }
    }

    return news_window;
}

FOUNDATION_STATIC void news_window_deallocate(void* window)
{
    news_window_t* news_window = (news_window_t*)window;
    FOUNDATION_ASSERT(news_window);

    {
        // Delete news data
        SHARED_WRITE_LOCK(news_window->news_mutex);
        for (size_t i = 0; i < array_size(news_window->news); ++i)
        {
            news_t* news = news_window->news + i;
            string_deallocate(news->date_string);
            string_deallocate(news->headline);
            string_deallocate(news->url);
            string_deallocate(news->summary);
            string_array_deallocate(news->related);
            string_array_deallocate(news->tags);
        }
        array_deallocate(news_window->news);
    }

    MEM_DELETE(news_window);
}

FOUNDATION_STATIC bool news_window_render(void* obj)
{
    // Render news feed
    news_window_t* news_window = (news_window_t*)obj;
    FOUNDATION_ASSERT(news_window);

    if (array_empty(news_window->news))
    {
        ImGui::TextWrapped(tr("No news feed"));
        return true;
    }

    SHARED_READ_LOCK(news_window->news_mutex);
    for (unsigned i = 0, end = array_size(news_window->news); i < end; ++i)
    {
        news_t* news = news_window->news + i;

        ImGui::PushID(news);

        const float space = ImGui::GetContentRegionAvail().x;

        if (i > 0)
            ImGui::Separator();

        ImGui::SetWindowFontScale(0.75f);
        // Render sentiment information using a single line
        if (news->sentiment_positive > news->sentiment_negative)
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%.2f", news->sentiment_polarity);
        else
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "%.2f", news->sentiment_polarity);
        ImGui::SameLine();
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextURL(STRING_RANGE(news->headline), STRING_ARGS(news->url));

        ImGui::Indent();

        if (!string_is_null(news->date_string))
        {
            ImGui::SetWindowFontScale(0.95f);

            ImGui::TextWrapped("%.*s", STRING_FORMAT(news->date_string));

            ImGui::SameLine();
            if (news->change_p >= 0)
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), ICON_MD_TRENDING_UP " %.3g%%", news->change_p);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), ICON_MD_TRENDING_DOWN " %.3g%%", news->change_p);
        }

        // Render tags
        if (news->tags)
        {
            ImGui::SetWindowFontScale(0.6f);
            for (unsigned j = 0, endj = array_size(news->tags); j < endj; ++j)
            {
                if (j > 0 && ImGui::GetItemRectMax().x < space * 1.2f)
                    ImGui::SameLine();
                ImGui::Text("%.*s", STRING_FORMAT(news->tags[j]));
            }
        }

        if (news->related)
        {
            // Render related symbols
            ImGui::SetWindowFontScale(0.7f);
            for (unsigned j = 0, endj = array_size(news->related); j < endj; ++j)
            {
                if (j > 0 && ImGui::GetItemRectMax().x < space * 1.2f)
                    ImGui::SameLine();
                if (ImGui::SmallButton(news->related[j].str))
                    news_open_window(STRING_ARGS(news->related[j]));
            }
        }

        ImGui::SetWindowFontScale(0.9f);

        if (!news->openai_response)
        {
            if (ImGui::SmallButton(tr("Summarize for me...")))
            {
                openai_completion_options_t options = {};
                options.best_of = news->sentiment_negative > news->sentiment_positive ? 3 : 1;
                options.max_tokens = news->sentiment_negative > news->sentiment_positive ? 2500 : 1000;
                news->openai_response = openai_generate_news_sentiment(
                    STRING_LENGTH(news_window->symbol), news->date, STRING_ARGS(news->url), options);
            }
            else
            {
                ImGui::Unindent();
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextEx(STRING_RANGE(news->summary), ImGuiTextFlags_None);
                ImGui::PopTextWrapPos();

                ImGui::TextURL(tr("more..."), nullptr, STRING_ARGS(news->url));
            }
        }
        else
        {
            ImGui::Unindent();
            ImGui::PushTextWrapPos(0.0f);
            if (news->openai_response->output.length)
                ImGui::TextEx(STRING_RANGE(news->openai_response->output), ImGuiTextFlags_None);
            else 
                ImGui::TrText("Please wait, reading the news for you...");
            ImGui::PopTextWrapPos();
        }

        ImGui::PopID();

        ImGui::SetWindowFontScale(1.0f);
    }

    return true;
}

//
// # PUBLIC API
//

void news_open_window(const char* symbol, size_t symbol_length)
{
    news_window_t* news_window = news_window_allocate(symbol, symbol_length);
    app_open_dialog(news_window->title, news_window_render, 900, 1200, true, news_window, news_window_deallocate);
}

string_t news_google_search_api_key()
{
    return {STRING_BUFFER(NEWS_GOOGLE_SEARCH_API_KEY)};
}

string_t news_set_google_search_api_key(const char* apikey)
{
    string_t k = string_copy(STRING_BUFFER(NEWS_GOOGLE_SEARCH_API_KEY), apikey, string_length(apikey));
    if (NEWS_GOOGLE_SEARCH_API_KEY[0])
        console_add_secret_key_token(STRING_LENGTH(NEWS_GOOGLE_SEARCH_API_KEY));

    string_const_t key_file_path = session_get_user_file_path(STRING_CONST("google.key"));
    stream_t* key_stream = fs_open_file(STRING_ARGS(key_file_path), STREAM_CREATE | STREAM_OUT | STREAM_TRUNCATE);
    if (key_stream == nullptr)
        return k;

    log_infof(0, STRING_CONST("Writing key file %.*s"), STRING_FORMAT(key_file_path));
    stream_write_string(key_stream, STRING_ARGS(k));
    stream_deallocate(key_stream);

    return k;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void news_initialize()
{
    string_const_t google_apis_key{};
    if (environment_command_line_arg("google-apis-key", &google_apis_key))
    {
        string_copy(STRING_BUFFER(NEWS_GOOGLE_SEARCH_API_KEY), STRING_ARGS(google_apis_key));
        console_add_secret_key_token(STRING_ARGS(google_apis_key));
    }
    else
    {
        string_const_t key_file_path = session_get_user_file_path(STRING_CONST("google.key"));
        stream_t* key_stream = fs_open_file(STRING_ARGS(key_file_path), STREAM_IN);
        if (key_stream)
        {
            string_t key = stream_read_string(key_stream);
            string_copy(STRING_BUFFER(NEWS_GOOGLE_SEARCH_API_KEY), STRING_ARGS(key));
            string_deallocate(key.str);
            stream_deallocate(key_stream);
        }
        else
        {
            // Clear key
            NEWS_GOOGLE_SEARCH_API_KEY[0] = 0;
        }
    }

    if (NEWS_GOOGLE_SEARCH_API_KEY[0])
        console_add_secret_key_token(STRING_LENGTH(NEWS_GOOGLE_SEARCH_API_KEY));
}

FOUNDATION_STATIC void news_shutdown()
{
    
}

DEFINE_MODULE(NEWS, news_initialize, news_shutdown, MODULE_PRIORITY_UI);
