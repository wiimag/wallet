/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#include "settings.h"

#include "eod.h"
#include "stock.h"

#include "framework/common.h"
#include "framework/session.h"
#include "framework/imgui.h"

#define SESSION_KEY_SEARCH_TERMS "search_terms"
#define SESSION_KEY_SEARCH_FILTER "search_filter"
#define SESSION_KEY_CURRENT_TAB "current_tab_1"

settings_t SETTINGS{};

void settings_draw()
{
    ImGui::Columns(3, nullptr, false);

    ImGui::SetColumnWidth(0, imgui_get_font_ui_scale(340.0f));
    ImGui::SetColumnWidth(1, imgui_get_font_ui_scale(650.0f));

    {
        string_t eod_key = eod_get_key();
        ImGui::MoveCursor(0, 5);
        ImGui::TextURL("EOD API Key", nullptr, STRING_CONST("https://eodhistoricaldata.com"));

        ImGui::NextColumn();
        if (ImGui::InputTextWithHint("##EODKey", "demo", eod_key.str, eod_key.length, ImGuiInputTextFlags_Password))
            eod_save_key(eod_key);
        ImGui::NextColumn();
    }

    {
        ImGui::NextColumn();
        ImGui::TextURL("Currency", nullptr, STRING_CONST("https://eodhistoricaldata.com/financial-apis/list-supported-forex-currencies/"));

        ImGui::NextColumn();
        if (ImGui::InputTextWithHint("##Currency", "i.e. USD", STRING_CONST_CAPACITY(SETTINGS.preferred_currency), ImGuiInputTextFlags_AutoSelectAll))
        {
        }

        ImGui::NextColumn();
        if (!string_equal(STRING_ARGS(string_const(SETTINGS.preferred_currency)), STRING_CONST("USD")))
        {
            ImGui::Text("i.e. USD%s is %.2lf $", SETTINGS.preferred_currency, 
                stock_exchange_rate(STRING_CONST("USD"), STRING_ARGS(string_const(SETTINGS.preferred_currency))));
        }
    }

    {
        ImGui::NextColumn();
        ImGui::TextURL("Preferred Dividends %", nullptr, STRING_CONST("https://en.wikipedia.org/wiki/Dividend#:~:text=A%20dividend%20is%20a%20distribution,business%20(called%20retained%20earnings)."));

        ImGui::NextColumn();
        double good_dividends_ratio_100 = SETTINGS.good_dividends_ratio * 100;
        if (ImGui::InputDouble("##DividendsRatio", &good_dividends_ratio_100, 1.0, 0, "%.3g %%", ImGuiInputTextFlags_AutoSelectAll))
        {
            SETTINGS.good_dividends_ratio = good_dividends_ratio_100 / 100.0;
        }

        ImGui::NextColumn();
    }

    ImGui::MoveCursor(0, 30.0f);
    {
        ImGui::NextColumn();
        ImGui::TextUnformatted("Font scaling");

        ImGui::NextColumn();
        if (ImGui::InputFloat("##FontScaling", &SETTINGS.font_scaling, 0.5, 0, "%.2lf", ImGuiInputTextFlags_AutoSelectAll))
        {
        }

        ImGui::NextColumn();
        ImGui::TextUnformatted("Changing that settings requires restarting the application.");
    }

    {
        ImGui::NextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Frame Throttling");

        ImGui::NextColumn();
        int frame_throttling = session_get_integer("frame_throttling", 16);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::SliderInt("##frame_throttling", &frame_throttling, 0, 120, "%d milliseconds", ImGuiSliderFlags_AlwaysClamp))
            session_set_integer("frame_throttling", frame_throttling);

        ImGui::NextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped("Time to wait before rendering another frame (ms).\nThe higher the number, less resources are used, therefore more battery time!");
    }
}

void settings_initialize()
{
    SETTINGS.show_bulk_eod = session_get_bool("show_bulk_eod", SETTINGS.show_bulk_eod);
    SETTINGS.show_symbols_TO = session_get_bool("show_symbols_TO", SETTINGS.show_symbols_TO);
    SETTINGS.show_symbols_US = session_get_bool("show_symbols_US", SETTINGS.show_symbols_US);
    SETTINGS.show_symbols_CVE = session_get_bool("show_symbols_CVE", SETTINGS.show_symbols_CVE);
    SETTINGS.show_symbols_NEO = session_get_bool("show_symbols_NEO", SETTINGS.show_symbols_NEO);
    SETTINGS.show_symbols_INDX = session_get_bool("show_symbols_INDX", SETTINGS.show_symbols_INDX);

    SETTINGS.current_tab = session_get_integer(SESSION_KEY_CURRENT_TAB, SETTINGS.current_tab);
    SETTINGS.good_dividends_ratio = (double)session_get_float("good_dividends_ratio", (float)SETTINGS.good_dividends_ratio);
    SETTINGS.font_scaling = session_get_float("font_scaling", 1.0f);

    // Restore some session settings from the user registry
    string_copy(STRING_CONST_CAPACITY(SETTINGS.search_terms), STRING_ARGS(session_get_string(SESSION_KEY_SEARCH_TERMS, "")));
    string_copy(STRING_CONST_CAPACITY(SETTINGS.search_filter), STRING_ARGS(session_get_string(SESSION_KEY_SEARCH_FILTER, "")));
    string_copy(STRING_CONST_CAPACITY(SETTINGS.preferred_currency), STRING_ARGS(session_get_string("preferred_currency", "CAD")));
}

void settings_shutdown()
{
    session_set_bool("show_bulk_eod", SETTINGS.show_bulk_eod);
    session_set_bool("show_symbols_TO", SETTINGS.show_symbols_TO);
    session_set_bool("show_symbols_US", SETTINGS.show_symbols_US);
    session_set_bool("show_symbols_CVE", SETTINGS.show_symbols_CVE);
    session_set_bool("show_symbols_NEO", SETTINGS.show_symbols_NEO);
    session_set_bool("show_symbols_INDX", SETTINGS.show_symbols_INDX);
    session_set_integer(SESSION_KEY_CURRENT_TAB, SETTINGS.current_tab);
    session_set_string(SESSION_KEY_SEARCH_TERMS, SETTINGS.search_terms);
    session_set_string(SESSION_KEY_SEARCH_FILTER, SETTINGS.search_filter);
    session_set_string("preferred_currency", SETTINGS.preferred_currency);
    session_set_float("good_dividends_ratio", (float)SETTINGS.good_dividends_ratio);
    session_set_float("font_scaling", SETTINGS.font_scaling);
}