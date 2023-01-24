/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#include "console.h"
#include "expr.h"

#include "framework/common.h"
#include "framework/session.h"
#include "framework/service.h"
#include "framework/imgui_utils.h"
#include "framework/scoped_mutex.h"

#include <foundation/log.h>
#include <foundation/array.h>

#include <imgui/imgui.h>

#include <algorithm>

#define HASH_CONSOLE static_hash_string("console", 7, 0xf4408b2738af51e7ULL)

static bool _console_window_opened = false;
static bool _logger_focus_last_message = false;
static char _log_search_filter[256]{ 0 };
static int _filtered_message_count = -1;
static size_t _next_log_message_id = 1;

struct log_message_t
{
    size_t id{0};
    hash_t key;
    error_level_t severity;
    string_t msg{ nullptr, 0 };
    size_t occurence{1};
    bool selectable{false};
};

static mutex_t* _message_lock = nullptr;
static log_message_t* _messages = nullptr;

FOUNDATION_STATIC void logger(hash_t context, error_level_t severity, const char* msg, size_t length)
{
    scoped_mutex_t lock(_message_lock);
    memory_context_push(HASH_CONSOLE);

    if (!log_is_prefix_enabled())
    {
        log_message_t* last_message = array_last(_messages);
        if (last_message)
        {
            string_t prev = last_message->msg;
            last_message->msg = string_allocate_concat(STRING_ARGS(prev), msg, length);
            string_deallocate(prev.str);
            return;
        }
    }

    log_message_t m{ _next_log_message_id++, string_hash(msg, length), severity };
    m.msg = string_clone(msg, length);
    _logger_focus_last_message = true;
    signal_thread();

    array_push(_messages, m);
    memory_context_pop();
}

FOUNDATION_STATIC void console_render_messages()
{
    ImGui::SetWindowFontScale(0.9f);
    const size_t log_count = array_size(_messages);

    ImGuiListClipper clipper;
    clipper.Begin(_filtered_message_count <= 0 ? (int)log_count : _filtered_message_count);
    while (clipper.Step())
    {
        if (clipper.DisplayStart >= clipper.DisplayEnd)
            continue;

        if (mutex_lock(_message_lock))
        {
            const float item_width = ImGui::GetContentRegionAvail().x;
            for (size_t i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                log_message_t& log = _messages[i];

                if (log.selectable)
                {
                    _logger_focus_last_message = false;
                    ImGui::SetNextItemWidth(item_width);
                    ImGui::InputText(string_format_static_const("##%llu", log.id), log.msg.str, log.msg.length, ImGuiInputTextFlags_ReadOnly);
                }
                else
                {
                    if (log.severity == ERRORLEVEL_ERROR)
                        ImGui::PushStyleColor(ImGuiCol_Text, TEXT_BAD_COLOR);
                    ImGui::TextWrapped("%.*s", STRING_FORMAT(log.msg));
                    if (log.severity == ERRORLEVEL_ERROR)
                        ImGui::PopStyleColor(1);
                }

                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    log.selectable = !log.selectable;
            }
            mutex_unlock(_message_lock);
        }
    }

    if (_logger_focus_last_message)
    {
        ImGui::Dummy(ImVec2());
        ImGui::ScrollToItem();
        ImGui::SetItemDefaultFocus();
        _logger_focus_last_message = false;
    }

    ImGui::SetWindowFontScale(1.0f);
}

FOUNDATION_STATIC void console_clear_all()
{
    _filtered_message_count = -1;
    _log_search_filter[0] = '\0';
    const size_t log_count = array_size(_messages);
    for (size_t i = 0; i != log_count; ++i)
        string_deallocate(_messages[i].msg.str);
    array_deallocate(_messages);
}

FOUNDATION_STATIC void console_render_toolbar()
{
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - imgui_get_font_ui_scale(100.0f));
    if (ImGui::InputTextWithHint("##SearchLog", "Search logs...", STRING_CONST_CAPACITY(_log_search_filter)))
    {
        _filtered_message_count = 0;
        const size_t filter_length = string_length(_log_search_filter);
        if (filter_length > 0)
        {
            size_t log_count = array_size(_messages);
            for (_filtered_message_count = 0; _filtered_message_count < log_count;)
            {
                const log_message_t& log = _messages[_filtered_message_count];
                if (string_contains_nocase(STRING_ARGS(log.msg), _log_search_filter, filter_length))
                {
                    _filtered_message_count++;
                }
                else
                {
                    std::swap(_messages[_filtered_message_count], _messages[log_count - 1]);
                    log_count--;
                }
            }
        }
        else
        {
            array_sort(_messages, a.id < b.id);
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        console_clear_all();
    ImGui::EndGroup();
}

FOUNDATION_STATIC void console_log_evaluation_result(string_const_t expression_string, const expr_result_t& result)
{
    if (result.type == EXPR_RESULT_ARRAY && result.element_count() > 1 && result.list[0].type == EXPR_RESULT_POINTER)
    {
        if (expression_string.length)
            log_infof(HASH_EXPR, STRING_CONST("%.*s\n"), STRING_FORMAT(expression_string));
        log_enable_prefix(false);
        for (unsigned i = 0; i < result.element_count(); ++i)
            console_log_evaluation_result({nullptr, 0}, result.element_at(i));
        log_enable_prefix(true);
    }
    else if (result.type == EXPR_RESULT_POINTER && result.element_count() == 16 && result.element_size() == sizeof(float))
    {
        const float* m = (const float*)result.ptr;
        log_infof(HASH_EXPR, STRING_CONST("%.*s %s \n" \
            "\t[%7.4g, %7.4g, %7.4g, %7.4g\n" \
            "\t %7.4g, %7.4g, %7.4g, %7.4g\n" \
            "\t %7.4g, %7.4g, %7.4g, %7.4g\n" \
            "\t %7.4g, %7.4g, %7.4g, %7.4g ]\n"), STRING_FORMAT(expression_string), expression_string.length > 0 ? "=>": "",
                m[0], m[1], m[2], m[3],
                m[4], m[5], m[6], m[7],
                m[8], m[9], m[10], m[11],
                m[12], m[13], m[14], m[15]);
    }
    else
    {
        string_const_t result_string = expr_result_to_string(result);
        if (expression_string.length)
        {
            if (expression_string.length + result_string.length > 64)
            {
                log_infof(HASH_EXPR, STRING_CONST("%.*s =>\n\t%.*s"), STRING_FORMAT(expression_string), STRING_FORMAT(result_string));
            }
            else
            {
                log_infof(HASH_EXPR, STRING_CONST("%.*s => %.*s"), STRING_FORMAT(expression_string), STRING_FORMAT(result_string));
                ImGui::SetClipboardText(result_string.str);
            }
        }
        else
            log_infof(HASH_EXPR, STRING_CONST("\t%.*s"), STRING_FORMAT(result_string));
    }
}

FOUNDATION_STATIC void console_render_evaluator()
{
    static bool focus_text_field = true;
    static char expression_buffer[4096]{ "" };

    if (focus_text_field)
    {
        ImGui::SetKeyboardFocusHere();
        focus_text_field = false;
    }

    if (ImGui::IsWindowAppearing())
    {
        string_copy(STRING_CONST_CAPACITY(expression_buffer),
            STRING_ARGS(session_get_string("console_expression", "")));
    }

    const float control_height = ImGui::GetContentRegionAvail().y;
    bool evaluate = false;
    if (ImGui::InputTextMultiline("##Expression", STRING_CONST_CAPACITY(expression_buffer),
        ImVec2(imgui_get_font_ui_scale(-98.0f), control_height), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine))
    {
        evaluate = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Eval", ImVec2(-1, control_height)))
        evaluate = true;

    if (evaluate)
    {
        string_const_t expression_string = string_const(expression_buffer, string_length(expression_buffer));
        session_set_string("console_expression", STRING_ARGS(expression_string));

        expr_result_t result = eval(expression_string);
        if (EXPR_ERROR_CODE == 0)
        {
            console_log_evaluation_result(expression_string, result);
        }
        else if (EXPR_ERROR_CODE != 0)
        {
            log_errorf(HASH_EXPR, ERROR_SCRIPT, STRING_CONST("[%d] %.*s -> %.*s"),
                EXPR_ERROR_CODE, STRING_FORMAT(expression_string), (int)string_length(EXPR_ERROR_MSG), EXPR_ERROR_MSG);
        }

        focus_text_field = true;
    }
}

FOUNDATION_STATIC void console_render_window()
{
    static bool window_opened_once = false;
    if (!window_opened_once)
    {
        ImGui::SetNextWindowSizeConstraints(ImVec2(980, 720), ImVec2(INFINITY, INFINITY));
        window_opened_once = true;
    }

    if (ImGui::Begin("Console##5", &_console_window_opened, 
        ImGuiWindowFlags_AlwaysUseWindowPadding))
    {
        console_render_toolbar();

        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(0, 0));
        imgui_draw_splitter("ConsoleSplitter2", [](const auto& rect)
        {
            ImVec2 space = ImGui::GetContentRegionAvail();
            if (ImGui::BeginChild("Messages"))
                console_render_messages();
            ImGui::EndChild();
        }, [](const auto& rect)
        {
            console_render_evaluator();
        }, IMGUI_SPLITTER_VERTICAL, ImGuiWindowFlags_None, 0.85f, true);
        ImGui::PopStyleVar(2);
    }

    ImGui::End();    
}

FOUNDATION_STATIC void console_menu()
{
    if (shortcut_executed(ImGuiKey_F10))
        _console_window_opened = true;

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("Windows"))
        {
            ImGui::MenuItem(ICON_MD_LOGO_DEV " Console", "F10", &_console_window_opened);
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    if (_console_window_opened)
        console_render_window();
}

FOUNDATION_STATIC void console_initialize()
{
    _message_lock = mutex_allocate(STRING_CONST("console_lock"));

    if (!main_is_running_tests())
    {
        log_set_handler(logger);
        _console_window_opened = environment_command_line_arg("console") || session_get_bool("show_console", _console_window_opened);
        service_register_menu(HASH_CONSOLE, console_menu);
    }
}

FOUNDATION_STATIC void console_shutdown()
{
    mutex_lock(_message_lock);
    log_set_handler(nullptr);
    console_clear_all();
    mutex_unlock(_message_lock);
    mutex_deallocate(_message_lock);
    session_set_bool("show_console", _console_window_opened);
}

DEFINE_SERVICE(CONSOLE, console_initialize, console_shutdown, -1);
