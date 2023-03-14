/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "news.h"

#include "eod.h"

#include <framework/app.h>
#include <framework/imgui.h>
#include <framework/string.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/service.h>
#include <framework/database.h>
#include <framework/dispatcher.h>
#include <framework/string_table.h>
#include <framework/profiler.h>
#include <framework/array.h>

#define HASH_NEWS static_hash_string("news", 4, 0xc804eb289c3e1658ULL)

struct news_t
{
    time_t date;
    string_t date_string;
    string_t headline;
    string_t url;
    string_t summary;
    string_t* related;
    string_t* tags;
    
    double sentiment_polarity;
    double sentiment_positive;
    double sentiment_negative;
    double sentiment_neutral;
};

struct news_window_t
{
    char title[64]{ 0 };
    char symbol[16]{ 0 };

    news_t* news{ nullptr };
};

//
// # PRIVATE
//

FOUNDATION_STATIC void news_fetch_data(news_window_t* news_window, const json_object_t& json)
{
    MEMORY_TRACKER(HASH_NEWS);
    
    news_t* news_feed = nullptr;
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

        array_push(news_feed, news);
    }

    news_window->news = news_feed;
}

FOUNDATION_STATIC news_window_t* news_window_allocate(const char* symbol, size_t symbol_length)
{
    news_window_t* news_window = MEM_NEW(HASH_NEWS, news_window_t);

    string_copy(STRING_BUFFER(news_window->symbol), symbol, symbol_length);
    string_format(STRING_BUFFER(news_window->title), STRING_CONST("News %.*s"), (int)symbol_length, symbol);

    // Fetch symbol news
    if (!eod_fetch_async("news", nullptr, FORMAT_JSON, "s", symbol, "limit", "10", L1(news_fetch_data(news_window, _1))))
    {
        log_warnf(HASH_NEWS, WARNING_RESOURCE, STRING_CONST("Failed to fetch news for symbol %*.s"), (int)symbol_length, symbol);
    }

    return news_window;
}

FOUNDATION_STATIC void news_window_deallocate(void* window)
{
    news_window_t* news_window = (news_window_t*)window;
    FOUNDATION_ASSERT(news_window);

    // Delete news data{
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

    MEM_DELETE(news_window);
}

FOUNDATION_STATIC bool news_window_render(void* obj)
{
    // Render news feed
    news_window_t* news_window = (news_window_t*)obj;
    FOUNDATION_ASSERT(news_window);

    if (array_empty(news_window->news))
    {
        ImGui::TextWrapped("No news feed");
        return true;
    }

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

        ImGui::SetWindowFontScale(0.95f);
        ImGui::TextWrapped("%.*s", STRING_FORMAT(news->date_string));

        // Render tags
        ImGui::SetWindowFontScale(0.6f);
        for (unsigned j = 0, endj = array_size(news->tags); j < endj; ++j)
        {
            if (j > 0 && ImGui::GetItemRectMax().x < space * 1.2f)
                ImGui::SameLine();
            ImGui::Text("%.*s", STRING_FORMAT(news->tags[j]));
        }

        ImGui::SetWindowFontScale(0.8f);
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

        ImGui::Unindent();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextEx(STRING_RANGE(news->summary), ImGuiTextFlags_None);
        ImGui::PopTextWrapPos();

        ImGui::TextURL("more...", nullptr, STRING_ARGS(news->url));

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

//
// # SYSTEM
//

FOUNDATION_STATIC void news_initialize()
{
}

FOUNDATION_STATIC void news_shutdown()
{   
    
}

DEFINE_SERVICE(NEWS, news_initialize, news_shutdown, SERVICE_PRIORITY_UI);
