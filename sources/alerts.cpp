/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "alerts.h"

#include "pattern.h"

#include <framework/expr.h>
#include <framework/imgui.h>
#include <framework/array.h>
#include <framework/config.h>
#include <framework/session.h>
#include <framework/service.h>
#include <framework/string_table.h>
#include <framework/localization.h>

#define HASH_ALERTS static_hash_string("alerts", 5, 0x3a6761b0fb57262bULL)

/*! Expression evaluator. */
struct expr_evaluator_t
{
    char title[32]{ '\0' };
    char description[64]{ '\0' };
    char expression[1024]{ '\0' };
    double frequency{ 60.0 * 5 }; // 5 minutes

    time_t last_run_time{ 0 };
    time_t triggered_time{ 0 };
    bool discarded{ false };
};

static struct ALERTS_MODULE {
    expr_evaluator_t* evaluators = nullptr;
} *_alerts_module;

FOUNDATION_STATIC string_const_t alerts_config_file_path()
{
    return session_get_user_file_path(STRING_CONST("alerts.json"));
}

FOUNDATION_STATIC expr_evaluator_t* alerts_load_evaluators(const config_handle_t& evaluators_data)
{
    expr_evaluator_t* evaluators = nullptr;
    for (const auto cv : evaluators_data)
    {
        expr_evaluator_t e{};
        string_copy(STRING_BUFFER(e.title), STRING_ARGS(cv["code"].as_string()));
        string_copy(STRING_BUFFER(e.description), STRING_ARGS(cv["label"].as_string()));
        string_copy(STRING_BUFFER(e.expression), STRING_ARGS(cv["expression"].as_string()));
        
        e.frequency = cv["frequency"].as_number(60.0);
        e.last_run_time = (time_t)cv["last_run_time"].as_number((double)time_now());
        e.triggered_time = (time_t)cv["triggered_time"].as_number(0);
        e.discarded = cv["discarded"].as_boolean(false);

        array_push_memcpy(evaluators, &e);
    }

    return evaluators;
}

FOUNDATION_STATIC void alerts_save_evaluators(const expr_evaluator_t* evaluators)
{
    config_write_file(alerts_config_file_path(), [evaluators](config_handle_t evaluators_data)
    {
        for (unsigned i = 0; i < array_size(evaluators); ++i)
        {
            const expr_evaluator_t& e = evaluators[i];

            config_handle_t ecv = config_array_push(evaluators_data, CONFIG_VALUE_OBJECT);
            config_set(ecv, "code", e.title, string_length(e.title));
            config_set(ecv, "label", e.description, string_length(e.description));
            config_set(ecv, "expression", e.expression, string_length(e.expression));
            config_set(ecv, "frequency", e.frequency);
            config_set(ecv, "last_run_time", (double)e.last_run_time);
            config_set(ecv, "triggered_time", (double)e.triggered_time);
            config_set(ecv, "discarded", e.discarded);
        }

        return true;
    }, CONFIG_VALUE_ARRAY, 
        CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS | 
        CONFIG_OPTION_PRESERVE_INSERTION_ORDER |
        CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES | 
        CONFIG_OPTION_WRITE_NO_SAVE_ON_DATA_EQUAL);
}

FOUNDATION_STATIC bool alerts_check_expression_condition_result(const expr_result_t& result)
{
    if (result.type == EXPR_RESULT_NULL || result.type == EXPR_RESULT_FALSE)
        return false;

    if (result.type == EXPR_RESULT_TRUE)
        return true;

    if (result.type == EXPR_RESULT_NUMBER)
    {
        const double n = result.as_number(NAN);
        return math_trunc(n) != 0 && math_real_is_finite(n);
    }
    
    if (result.type == EXPR_RESULT_SYMBOL)
    {
        string_const_t sym = result.as_string();
        return !string_equal_nocase(STRING_ARGS(sym), STRING_CONST("false"));
    }

    if (result.is_set())
    {
        // Check that all elements are true
        for (unsigned j = 0; j < result.element_count(); ++j)
        {
            expr_result_t r = result.element_at(j);
            if (!alerts_check_expression_condition_result(r))
                return false;
        }

        return true;
    }

    log_debugf(HASH_ALERTS, STRING_CONST("Invalid expression result type %d"), result.type);
    return false;
}

FOUNDATION_STATIC void alerts_run_evaluators(expr_evaluator_t* evaluators)
{
    for (unsigned i = 0; i < array_size(evaluators); ++i)
    {
        expr_evaluator_t& e = evaluators[i];

        // Check if expression has already triggered
        if (e.triggered_time || e.discarded)
            continue;

        // Check if the expression is due to be evaluated
        if ((time_now() - e.last_run_time) < e.frequency)
            continue;

        // Mark the expression has being evaluated
        e.last_run_time = time_now();
            
        // Check if the expression is valid
        const size_t expression_length = string_length(e.expression);
        if (expression_length == 0)
            continue;

        // Set the alert variables (i.e. $TITLE, $DESCRIPTION, etc.)
        expr_set_global_var(STRING_CONST("$TITLE"), e.title, string_length(e.title));
        expr_set_global_var(STRING_CONST("$DESCRIPTION"), e.description, string_length(e.description));

        // Evaluate the expression
        string_const_t expression = string_const(e.expression, expression_length);
        expr_result_t result = eval(expression);

        if (alerts_check_expression_condition_result(result))
        {
            e.discarded = false;
            e.triggered_time = time_now();
        }
    }
}

FOUNDATION_STATIC void alerts_render_evaluators(expr_evaluator_t*& evaluators)
{
    static bool has_ever_show_evaluators = session_key_exists("show_evaluators");
    static bool show_evaluators = session_get_bool("show_evaluators", false);

    bool start_show_stock_console = show_evaluators;
    if (shortcut_executed(ImGuiKey_F9))
        show_evaluators = !show_evaluators;

    if (!show_evaluators)
        return;

    if (!has_ever_show_evaluators)
        ImGui::SetNextWindowSize(ImVec2(1280, 720), ImGuiCond_Once);
    if (ImGui::Begin(tr("Alerts"), &show_evaluators))
    {
        if (ImGui::BeginTable("Alerts##4", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn(tr("Description"), ImGuiTableColumnFlags_WidthFixed, IM_SCALEF(160));
            ImGui::TableSetupColumn(tr("Title"), ImGuiTableColumnFlags_WidthFixed, IM_SCALEF(80));
            ImGui::TableSetupColumn(tr("Expression"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(tr("Status"), ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHeaderLabel, IM_SCALEF(20));
            ImGui::TableHeadersRow();

            // New row
            ImGui::TableNextRow();
            {
                bool add_alert = false;
                static expr_evaluator_t new_entry;

                if (ImGui::TableNextColumn())
                {
                    ImGui::ExpandNextItem();
                    ImGui::InputTextWithHint("##Label", "Description", STRING_BUFFER(new_entry.description), ImGuiInputTextFlags_EnterReturnsTrue);
                }

                if (ImGui::TableNextColumn())
                {
                    ImGui::ExpandNextItem();
                    ImGui::InputTextWithHint("##Title", "U.US", STRING_BUFFER(new_entry.title), ImGuiInputTextFlags_EnterReturnsTrue);
                }

                if (ImGui::TableNextColumn())
                {
                    ImGui::ExpandNextItem();
                    if (ImGui::InputTextWithHint("##Expression", "S($TITLE, price)>45.0", STRING_BUFFER(new_entry.expression), ImGuiInputTextFlags_EnterReturnsTrue))
                        add_alert = new_entry.expression[0] != 0;
                }

                if (ImGui::TableNextColumn())
                {
                    ImGui::BeginDisabled(new_entry.expression[0] == 0);
                    if (ImGui::Button(ICON_MD_ADD) || add_alert)
                    {
                        array_insert_memcpy_safe(evaluators, 0, &new_entry);

                        // Reset static entry
                        memset(&new_entry, 0, sizeof(expr_evaluator_t));
                        new_entry.frequency = 5 * 60.0;
                    }
                    ImGui::EndDisabled();
                }
            }

            for (unsigned i = 0; i < array_size(evaluators); ++i)
            {
                expr_evaluator_t& ev = evaluators[i];
                ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFontSize() * 2.0 + IM_SCALEF(10.0f));
                {
                    bool evaluate_expression = false;

                    ImGui::PushID(&ev);

                    if (ImGui::TableNextColumn())
                    {
                        ImGui::ExpandNextItem();
                        if (ImGui::InputTextWithHint("##Label", "Description", STRING_BUFFER(ev.description), ImGuiInputTextFlags_EnterReturnsTrue))
                            evaluate_expression = true;
                    }

                    if (ImGui::TableNextColumn())
                    {
                        const bool has_title = ev.title[0] != 0;
                        static float open_button_width = 10.0f;
                        ImGui::ExpandNextItem(has_title ? open_button_width : 0.0f, has_title);
                        if (ImGui::InputTextWithHint("##Title", "AAPL.US", STRING_BUFFER(ev.title), ImGuiInputTextFlags_EnterReturnsTrue))
                            evaluate_expression = true;

                        if (has_title)
                        {
                            // Open pattern in floating window
                            ImGui::SameLine();
                            if (ImGui::Button(ICON_MD_OPEN_IN_NEW))
                                pattern_open_window(ev.title, string_length(ev.title));
                            open_button_width = ImGui::GetItemRectSize().x;
                        }
                    }

                    if (ImGui::TableNextColumn())
                    {
                        ImGui::ExpandNextItem();
                        if (ImGui::InputTextWithHint("##Expression", "S(AAPL.US, price)<S(APPL.US, open)", STRING_BUFFER(ev.expression), ImGuiInputTextFlags_EnterReturnsTrue))
                            evaluate_expression = true;

                        ImGui::BeginGroup();
                        if (ev.triggered_time)
                        {
                            ImGui::Checkbox("##Enabled", &ev.discarded);
                            if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
                            {
                                ImGui::TrTextUnformatted("Discarded?");
                                ImGui::EndTooltip();
                            }

                            // Add button to reset the trigger
                            ImGui::SameLine();
                            if (ImGui::Button(ICON_MD_UPDATE))
                                evaluate_expression = true;
                            if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
                            {
                                ImGui::TrTextUnformatted("Reset the alert");
                                ImGui::EndTooltip();
                            }
                            
                            string_const_t triggered_time_string = string_from_time_static(ev.triggered_time * 1000, true);
                            ImGui::SameLine();
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextColored(ImVec4(0.0f, 0.9f, 0.0f, 1.0f), ICON_MD_NOTIFICATIONS_ACTIVE " %.*s", STRING_FORMAT(triggered_time_string));
                            if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
                            {
                                ImGui::TrTextUnformatted("This alert was triggered at the time shown above.");
                                ImGui::EndTooltip();
                            }
                        }
                        else
                        {
                            ImGui::AlignTextToFramePadding();
                            string_const_t last_run_time_string = string_from_time_static(ev.last_run_time * 1000, true);
                            ImGui::TextWrapped("%.*s", STRING_FORMAT(last_run_time_string));
                            if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
                            {
                                ImGui::TrTextUnformatted("Last time the expression was evaluated");
                                ImGui::EndTooltip();
                            }

                            ImGui::SameLine();
                            ImGui::TextUnformatted(ICON_MD_UPDATE);
                            if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
                            {
                                ImGui::TrTextUnformatted("Number of seconds to wait before re-evaluating the expression condition.");
                                ImGui::EndTooltip();
                            }
                            ImGui::SameLine();
                            ImGui::ExpandNextItem();
                            if (ImGui::InputDouble("##Frequency", &ev.frequency, ev.frequency > 60.0 ? 60.0 : 5.0, 0.0, tr("%.4g seconds")))
                            {
                                ev.discarded = false;
                                ev.triggered_time = 0;
                                ev.frequency = max(0.0, ev.frequency);
                            }

                        }
                        ImGui::EndGroup();
                    }

                    if (ImGui::TableNextColumn())
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button, BACKGROUND_CRITITAL_COLOR);
                        if (ImGui::Button(ICON_MD_DELETE_FOREVER))
                        {
                            array_erase_ordered_safe(evaluators, i--);
                        }
                        if (ImGui::IsItemHovered() && ImGui::BeginTooltip())
                        {
                            ImGui::TrText("Delete the alert `%s`", ev.description);
                            ImGui::EndTooltip();
                        }
                        ImGui::PopStyleColor();
                    }

                    if (evaluate_expression)
                    {
                        ev.last_run_time = 0;
                        ev.triggered_time = 0;
                        ev.discarded = false;
                    }

                    ImGui::PopID();
                }
            }

            ImGui::EndTable();
        }
    } ImGui::End();

    if (start_show_stock_console != show_evaluators)
    {
        session_set_bool("show_evaluators", show_evaluators);
        start_show_stock_console = show_evaluators;
    }
}

FOUNDATION_STATIC bool alerts_has_any_notifications()
{
    for (unsigned i = 0, end = array_size(_alerts_module->evaluators); i < end; ++i)
    {
        const expr_evaluator_t* e = _alerts_module->evaluators + i;
        if (e->triggered_time && !e->discarded)
            return true;
    }

    return false;
}

//
// # PUBLIC API
//

void alerts_main_menu_status()
{
    if (!alerts_has_any_notifications())
        return;

    if (ImGui::BeginMenu(ICON_MD_NOTIFICATIONS_ACTIVE))
    {
        // Add option to discard all
        ImGui::AlignTextToFramePadding();
        if (ImGui::Selectable("Discard all", false, ImGuiSelectableFlags_AllowItemOverlap))
        {
            for (unsigned i = 0, end = array_size(_alerts_module->evaluators); i < end; ++i)
            {
                expr_evaluator_t* e = _alerts_module->evaluators + i;
                if (e->triggered_time == 0 || e->discarded)
                    continue;
                e->discarded = true;
            }
        }

        ImGui::Separator();

        for (unsigned i = 0, end = array_size(_alerts_module->evaluators); i < end; ++i)
        {
            expr_evaluator_t* e = _alerts_module->evaluators + i;
            if (e->discarded || e->triggered_time == 0)
                continue;

            const char* description = e->description;
            if (string_length(description) == 0)
                description = e->expression;

            const char* time_scale = "minutes";
            double time = (double)(time_now() - e->triggered_time) / 60.0;
            if (time > 60 * 60)
            {
                time_scale = "days";
                time /= 60 * 60;
            }
            else if (time > 60)
            {
                time_scale = "hours";
                time /= 60;
            }        
            else if (time < 1.0)
            {
                time_scale = "seconds";
                time *= 60;
            }

            const char* title = e->title;
            string_const_t fmttr = RTEXT("[%s] %s %.0lf %s ago");
            if (string_length(title) == 0)
            {
                title = "";
                fmttr = RTEXT("%s%s %.0lf %s ago");
            }

            char label_buffer[128];
            string_t label = string_format(STRING_BUFFER(label_buffer), STRING_ARGS(fmttr), title, description, time, time_scale);

            ImGui::AlignTextToFramePadding();
            if (ImGui::SmallButton(ICON_MD_SNOOZE))
            {
                e->triggered_time = 0;
            }

            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            if (ImGui::SmallButton(ICON_MD_DELETE))
            {
                array_erase_ordered_safe(_alerts_module->evaluators, i);
                break;
            }

            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable(label.str))
            {
                e->discarded = true;
                if (title[0] != 0)
                {
                    pattern_open_window(title, string_length(title));
                }
            }            
        }     

        ImGui::EndMenu();
    }
}

//
// # SYSTEM
//

FOUNDATION_STATIC void alerts_initialize()
{
    const auto json_flags =
        CONFIG_OPTION_WRITE_SKIP_DOUBLE_COMMA_FIELDS |
        CONFIG_OPTION_PRESERVE_INSERTION_ORDER |
        CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES |
        CONFIG_OPTION_WRITE_TRUNCATE_NUMBERS |
        CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS;

    _alerts_module = MEM_NEW(HASH_ALERTS, ALERTS_MODULE);

    string_const_t evaluators_file_path = alerts_config_file_path();
    config_handle_t evaluators_data = config_parse_file(STRING_ARGS(evaluators_file_path), json_flags);
    if (evaluators_data)
    {
        _alerts_module->evaluators = alerts_load_evaluators(evaluators_data);
        config_deallocate(evaluators_data);
    }

    service_register_update(HASH_ALERTS, L0(alerts_run_evaluators(_alerts_module->evaluators)));
    service_register_window(HASH_ALERTS, L0(alerts_render_evaluators(_alerts_module->evaluators)));
}

FOUNDATION_STATIC void alerts_shutdown()
{
    alerts_save_evaluators(_alerts_module->evaluators);

    array_deallocate(_alerts_module->evaluators);
    MEM_DELETE(_alerts_module);
}

DEFINE_SERVICE(ALERTS, alerts_initialize, alerts_shutdown, SERVICE_PRIORITY_UI);
