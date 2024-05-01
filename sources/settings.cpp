/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2023 Wiimag inc. All rights reserved.
 */

#include "settings.h"

#include "eod.h"
#include "stock.h"
#include "logo.h"
#include "backend.h"
#include "search.h"

#include <framework/imgui.h>
#include <framework/string.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/localization.h>

settings_t SETTINGS{};

constexpr const char SESSION_KEY_CURRENT_TAB[] = "current_tab_1";
constexpr const char SESSION_KEY_SEARCH_TERMS[] = "search_terms";
constexpr const char SESSION_KEY_SEARCH_FILTER[] = "search_filter";

#if BUILD_APPLICATION

void settings_draw()
{
    ImGui::Columns(3, "#Settings", false);

    ImGui::SetColumnWidth(0, IM_SCALEF(260.0f));
    ImGui::SetColumnWidth(1, IM_SCALEF(270.0f));

    #if BUILD_ENABLE_LOCALIZATION
    {
        // Select language
        ImGui::AlignTextToFramePadding();
        ImGui::TrTextUnformatted("Language");

        ImGui::NextColumn();
        string_const_t current_language_name = localization_current_language_name();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::BeginCombo("##Language", current_language_name.str))
        {
            for (unsigned i = 0; i < localization_supported_language_count(); ++i)
            {
                string_const_t language_code = localization_language_code(i);
                string_const_t language_name = localization_language_name(i);
                bool is_selected = string_equal(STRING_ARGS(current_language_name), STRING_ARGS(language_code));
                if (ImGui::Selectable(language_name.str, is_selected))
                {
                    localization_set_current_language(STRING_ARGS(language_code));
                }
                if (is_selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::NextColumn();
    }
    #endif

    // EOD API KEY
    {
        #if BUILD_ENABLE_LOCALIZATION
        ImGui::NextColumn();
        #endif
        string_t eod_key = eod_get_key();
        ImGui::AlignTextToFramePadding();
        ImGui::TextURL(tr("EOD API Key"), nullptr, STRING_CONST("https://eodhistoricaldata.com/r/?ref=PF9TZC2T"));

        ImGui::NextColumn();
        ImGui::ExpandNextItem();
        if (ImGui::InputTextWithHint("##EODKey", tr("Enter your EOD API key"), eod_key.str, eod_key.length, ImGuiInputTextFlags_Password))
            eod_save_key(eod_key);
        ImGui::NextColumn();
    }

    // Default currency for reports
    {
        ImGui::NextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextURL("Currency", nullptr, STRING_CONST("https://eodhistoricaldata.com/financial-apis/list-supported-forex-currencies/"));

        ImGui::NextColumn();
        ImGui::ExpandNextItem();
        if (ImGui::InputTextWithHint("##Currency", tr("i.e. USD"), STRING_BUFFER(SETTINGS.preferred_currency), ImGuiInputTextFlags_AutoSelectAll))
        {
        }

        ImGui::NextColumn();
        if (!string_equal(STRING_ARGS(string_const(SETTINGS.preferred_currency)), STRING_CONST("USD")))
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TrText("i.e. USD%s is %.2lf $", SETTINGS.preferred_currency,
                stock_exchange_rate(STRING_CONST("USD"), STRING_ARGS(string_const(SETTINGS.preferred_currency))));
        }
    }

    // Dividend yielding preferred ratio using to colorize the preferred stocks
    {
        ImGui::NextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextURL(tr("Preferred Dividends %"), nullptr, STRING_CONST("https://en.wikipedia.org/wiki/Dividend#:~:text=A%20dividend%20is%20a%20distribution,business%20(called%20retained%20earnings)."));

        ImGui::NextColumn();
        double good_dividends_ratio_100 = SETTINGS.good_dividends_ratio * 100;
        ImGui::ExpandNextItem();
        if (ImGui::InputDouble("##DividendsRatio", &good_dividends_ratio_100, 1.0, 0, "%.3g %%", ImGuiInputTextFlags_AutoSelectAll))
        {
            SETTINGS.good_dividends_ratio = good_dividends_ratio_100 / 100.0;
        }

        ImGui::NextColumn();
    }

    // Search settings
    search_render_settings();

    // Logo settings
    {
        ImGui::NextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Show logo banners"));

        ImGui::NextColumn();
        if (ImGui::Checkbox("##ShowLogoBanners", &SETTINGS.show_logo_banners))
        {
        }

        ImGui::NextColumn();
        ImVec2 logo_banner_size = ImVec2(IM_SCALEF(100.0f), IM_SCALEF(20.0f));
        if (SETTINGS.show_logo_banners)
        {
            logo_render_banner(STRING_CONST("KHC.US"), logo_banner_size, true, false);

            ImGui::MoveCursor(logo_banner_size.x + 10.0f, 0.0f, false);
            logo_render_banner(STRING_CONST("LUMN.US"), logo_banner_size, true, false);

            ImGui::MoveCursor(logo_banner_size.x + 10.0f, 0.0f, false);
            logo_render_banner(STRING_CONST("FTS.TO"), logo_banner_size, true, false);
        }
        else 
        {
            const ImVec2& spos = ImGui::GetCursorScreenPos();
            ImGui::MoveCursor(2, 4.0f, false);
            logo_render_banner(STRING_CONST("U.US"), ImRect(spos, spos + logo_banner_size));
            ImGui::MoveCursor(2, -4.0f, true);
        }
    }

    // Font scaling
    {
        ImGui::MoveCursor(0, 30.0f);

        static bool restart_to_apply_effect = false;
        ImGui::NextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TrTextWrapped("Font scaling");

        ImGui::NextColumn();
        ImGui::ExpandNextItem();
        float font_scaling = session_get_float("font_scaling", 1.0f);
        if (ImGui::InputFloat("##FontScaling", &font_scaling, 0.25, 0, "%.2lf", ImGuiInputTextFlags_AutoSelectAll))
        {
            restart_to_apply_effect = true;
            imgui_set_font_ui_scale(font_scaling);
        }

        ImGui::NextColumn();
        if (restart_to_apply_effect)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImColor(TEXT_WARN_COLOR), tr("Changing that setting requires restarting the application."));
        }
    }

    // Frame throttling
    {
        ImGui::NextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TrTextWrapped("Frame Throttling");

        int frame_throttling = session_get_integer("frame_throttling", 16);

        ImGui::NextColumn();
        ImGui::ExpandNextItem();
        if (ImGui::SliderInt("##frame_throttling", &frame_throttling, 0, 1000, tr("%d milliseconds"), ImGuiSliderFlags_AlwaysClamp))
            session_set_integer("frame_throttling", frame_throttling);

        ImGui::NextColumn();
        ImGui::AlignTextToFramePadding();
        ImGui::TextWrapped(tr("Time to wait before rendering another frame (ms).\nThe higher the number, less resources are used, therefore more battery time!"));
    }
}

#endif 

void settings_initialize()
{
    SETTINGS.show_logo_banners = session_get_bool("show_logo_banners", SETTINGS.show_logo_banners);

    SETTINGS.current_tab = session_get_integer(SESSION_KEY_CURRENT_TAB, SETTINGS.current_tab);
    SETTINGS.good_dividends_ratio = (double)session_get_float("good_dividends_ratio", (float)SETTINGS.good_dividends_ratio);

    // Restore some session settings from the user registry
    string_copy(STRING_BUFFER(SETTINGS.search_terms), STRING_ARGS(session_get_string(SESSION_KEY_SEARCH_TERMS, "")));
    string_copy(STRING_BUFFER(SETTINGS.search_filter), STRING_ARGS(session_get_string(SESSION_KEY_SEARCH_FILTER, "")));
    string_copy(STRING_BUFFER(SETTINGS.preferred_currency), STRING_ARGS(session_get_string("preferred_currency", "CAD")));
}

void settings_shutdown()
{
    session_set_bool("show_logo_banners", SETTINGS.show_logo_banners);
    session_set_integer(SESSION_KEY_CURRENT_TAB, SETTINGS.current_tab);
    session_set_string(SESSION_KEY_SEARCH_TERMS, SETTINGS.search_terms);
    session_set_string(SESSION_KEY_SEARCH_FILTER, SETTINGS.search_filter);
    session_set_string("preferred_currency", SETTINGS.preferred_currency);
    session_set_float("good_dividends_ratio", (float)SETTINGS.good_dividends_ratio);
}

