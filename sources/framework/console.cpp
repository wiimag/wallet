/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "console.h"

#include <framework/expr.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/service.h>
#include <framework/imgui.h>
#include <framework/scoped_mutex.h>
#include <framework/generics.h>
#include <framework/string.h>

#include <foundation/log.h>
#include <foundation/array.h>
#include <foundation/hashstrings.h>

#include <algorithm>

#define HASH_CONSOLE static_hash_string("console", 7, 0xf4408b2738af51e7ULL)

struct log_message_t
{
    size_t id{ 0 };
    hash_t key;
    error_level_t severity;
    string_table_symbol_t msg_symbol{ STRING_TABLE_NULL_SYMBOL };
    string_table_symbol_t preview_symbol{ STRING_TABLE_NULL_SYMBOL };
    size_t occurence{ 1 };
    bool selectable{ false };
    hash_t context;
};

static bool _console_window_opened = false;
static bool _logger_focus_last_message = false;
static char _log_search_filter[256]{ 0 };
static int _filtered_message_count = -1;
static size_t _next_log_message_id = 1;
static string_t _selected_msg;
static generics::fixed_loop < string_t, 20, [](string_t& s) { string_deallocate(s.str); } > _saved_expressions;
static string_table_t* _console_string_table = nullptr;
static bool _console_concat_messages = false;
static char _console_expression_buffer[4096]{ "" };
static bool _console_expression_explicitly_set = false;
static mutex_t* _message_lock = nullptr;
static log_message_t* _messages = nullptr;

FOUNDATION_STATIC string_table_symbol_t console_string_encode(const char* s, size_t length /* = 0*/)
{
    if (length == 0)
        length = string_length(s);

    if (length == 0)
        return STRING_TABLE_NULL_SYMBOL;

    string_table_symbol_t symbol = string_table_to_symbol(_console_string_table, s, length);
    while (symbol == STRING_TABLE_FULL)
    {
        string_table_grow(&_console_string_table, (int)(_console_string_table->allocated_bytes * 2.0f));
        symbol = string_table_to_symbol(_console_string_table, s, length);
    }

    return symbol;
}

FOUNDATION_STATIC void logger(hash_t context, error_level_t severity, const char* msg, size_t length)
{
    memory_context_push(HASH_CONSOLE);

    if (_console_concat_messages)
    {
        scoped_mutex_t lock(_message_lock);
        log_message_t* last_message = array_last(_messages);
        if (last_message)
        {
            string_const_t prev = string_table_to_string_const(_console_string_table, last_message->msg_symbol);
            string_t new_msg = string_allocate_concat(STRING_ARGS(prev), msg, length);
            last_message->msg_symbol = console_string_encode(new_msg.str, new_msg.length);
            string_deallocate(new_msg.str);
            return;
        }
    }

    {
        log_message_t m{ _next_log_message_id++, string_hash(msg, length), severity };
        m.context = context;

        #if BUILD_ENABLE_STATIC_HASH_DEBUG
        context = context ? context : HASH_DEFAULT;
        string_const_t context_name = hash_to_string(context);
        const size_t hash_code_start = string_find(msg, length, '<', 12);
        const size_t hash_code_end = string_find(msg, length, '>', hash_code_start);

        scoped_mutex_t lock(_message_lock);
        if (context_name.length != 0 && hash_code_start != STRING_NPOS && hash_code_end != STRING_NPOS)
        {
            string_t formatted_msg = string_allocate_format(STRING_CONST("%.*s %11.*s : %.*s"), 
                (int)hash_code_start, msg,
                STRING_FORMAT(context_name),
                (int)(length - hash_code_end - 1), msg + hash_code_end + 2);

            m.msg_symbol = console_string_encode(formatted_msg.str, formatted_msg.length);
            string_deallocate(formatted_msg.str);
        }
        else
        #endif
        {
            m.msg_symbol = console_string_encode(msg, length);
        }

        char preview_buffer[256];
        string_const_t log_msg = string_table_to_string_const(_console_string_table, m.msg_symbol);
        string_const_t preview = string_remove_line_returns(STRING_BUFFER(preview_buffer), STRING_ARGS(log_msg));

        m.preview_symbol = console_string_encode(STRING_ARGS(preview));
        array_push_memcpy(_messages, &m);
    }

    _logger_focus_last_message = true;
    memory_context_pop();
}

FOUNDATION_STATIC void console_render_logs(const ImRect& rect)
{
    const size_t log_count = array_size(_messages);
    const int loop_count = _filtered_message_count <= 0 ? (int)log_count : _filtered_message_count;
    ImGuiListClipper clipper;
    clipper.Begin(loop_count);
    while (clipper.Step())
    {
        if (clipper.DisplayStart >= clipper.DisplayEnd)
            continue;

        if (mutex_lock(_message_lock))
        {
            const float item_width = ImGui::GetContentRegionAvail().x;
            for (size_t i = clipper.DisplayStart; i < min(clipper.DisplayEnd, (int)array_size(_messages)); ++i)
            {
                log_message_t& log = _messages[i];

                if (log.severity == ERRORLEVEL_ERROR)
                    ImGui::PushStyleColor(ImGuiCol_Text, TEXT_BAD_COLOR);
                else if (log.severity == ERRORLEVEL_WARNING)
                    ImGui::PushStyleColor(ImGuiCol_Text, TEXT_WARN_COLOR);

                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.0f));
                string_const_t msg_str = log.preview_symbol != STRING_TABLE_NULL_SYMBOL ? 
                    string_table_to_string_const(_console_string_table,log.preview_symbol) :
                    string_table_to_string_const(_console_string_table, log.msg_symbol);
                if (ImGui::Selectable(msg_str.str, &log.selectable, ImGuiSelectableFlags_DontClosePopups, {0, 0}))
                {
                    string_deallocate(_selected_msg.str);
                    string_const_t csmm = string_table_to_string_const(_console_string_table, log.msg_symbol);
                    _selected_msg = string_clone(STRING_ARGS(csmm));
                }
                ImGui::PopStyleVar();

                if (log.severity == ERRORLEVEL_ERROR && ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", string_table_to_string(_console_string_table, log.msg_symbol));
                }

                if (log.severity == ERRORLEVEL_ERROR || log.severity == ERRORLEVEL_WARNING)
                    ImGui::PopStyleColor(1);
            }
            mutex_unlock(_message_lock);
        }
    }

    if (_logger_focus_last_message)
    {
        ImGui::Dummy({});
        ImGui::ScrollToItem();
        ImGui::SetItemDefaultFocus();
        _logger_focus_last_message = false;
    }
}

FOUNDATION_STATIC void console_render_selected_log(const ImRect& rect)
{
    if (_selected_msg.length == 0)
        return;
    const ImVec2 asize = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##SelectedTex", _selected_msg.str, _selected_msg.length,
        asize, ImGuiInputTextFlags_ReadOnly);
}

FOUNDATION_STATIC void console_render_messages()
{
    ImGui::SetWindowFontScale(0.9f);

    imgui_frame_render_callback_t selected_log_frame = nullptr;
    if (_selected_msg.length)
        selected_log_frame = console_render_selected_log;

    imgui_draw_splitter("Messages", 
        console_render_logs, 
        selected_log_frame,
        IMGUI_SPLITTER_VERTICAL, ImGuiWindowFlags_None, 0.80f, true);

    ImGui::SetWindowFontScale(1.0f);
}

FOUNDATION_STATIC void console_clear_all()
{
    string_deallocate(_selected_msg.str);
    _selected_msg = {};
    
    if (!mutex_lock(_message_lock))
        return;

    _filtered_message_count = -1;
    _log_search_filter[0] = '\0';
    array_deallocate(_messages);

    int new_size = to_int(_console_string_table->allocated_bytes);
    string_table_deallocate(_console_string_table);
    _console_string_table = string_table_allocate(new_size, 64);
    
    mutex_unlock(_message_lock);
}

FOUNDATION_STATIC void console_render_toolbar()
{
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - imgui_get_font_ui_scale(100.0f));
    if (ImGui::InputTextWithHint("##SearchLog", "Search logs...", STRING_BUFFER(_log_search_filter)))
    {
        _filtered_message_count = 0;
        const size_t filter_length = string_length(_log_search_filter);
        if (filter_length > 0)
        {
            size_t log_count = array_size(_messages);
            for (_filtered_message_count = 0; _filtered_message_count < log_count;)
            {
                const log_message_t& log = _messages[_filtered_message_count];
                string_const_t log_msg = string_table_to_string_const(_console_string_table, log.msg_symbol);
                if (string_contains_nocase(STRING_ARGS(log_msg), _log_search_filter, filter_length))
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

FOUNDATION_STATIC void console_render_evaluator()
{
    static bool focus_text_field = true;

    if (ImGui::IsWindowAppearing())
    {
        if (!_console_expression_explicitly_set && _saved_expressions.size())
        {
            const string_t last_expression = _saved_expressions.current();
            string_copy(STRING_BUFFER(_console_expression_buffer), STRING_ARGS(last_expression));
        }
        _console_expression_explicitly_set = false;
    }

    static char input_id[32] = "##Expression";
    if (_saved_expressions.size() > 2 && ImGui::IsWindowFocused())
    {
        if (ImGui::Shortcut(ImGuiKey_UpArrow | ImGuiMod_Alt))
        {
            const string_t& last_expression = _saved_expressions.move(-1);
            string_t ec = string_copy(STRING_BUFFER(_console_expression_buffer), STRING_ARGS(last_expression));
            string_format(STRING_BUFFER(input_id), STRING_CONST("##%" PRIhash), string_hash(ec.str, ec.length));
            focus_text_field = true;
        }
        else if (ImGui::Shortcut(ImGuiKey_DownArrow | ImGuiMod_Alt))
        {
            const string_t& last_expression = _saved_expressions.move(+1);
            string_t ec = string_copy(STRING_BUFFER(_console_expression_buffer), STRING_ARGS(last_expression));
            string_format(STRING_BUFFER(input_id), STRING_CONST("##%" PRIhash), string_hash(ec.str, ec.length));
            focus_text_field = true;
        }
    }

    if (focus_text_field)
    {
        ImGui::SetKeyboardFocusHere();
    }
    
    bool evaluate = false;
    if (ImGui::InputTextMultiline(input_id, STRING_BUFFER(_console_expression_buffer),
        ImVec2(imgui_get_font_ui_scale(-98.0f), -1), 
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_AllowTabInput |
        (focus_text_field ? ImGuiInputTextFlags_AutoSelectAll : ImGuiInputTextFlags_None)))
    {
        evaluate = true;
    }

    if (focus_text_field)
    {
        ImGui::SetItemDefaultFocus();
        focus_text_field = false;
    }

    ImGui::SameLine();
    if (ImGui::Button("Eval", ImVec2(-1, -1)))
        evaluate = true;

    if (evaluate)
    {
        string_const_t expression_string = string_const(_console_expression_buffer, string_length(_console_expression_buffer));
        if (!_saved_expressions.includes<string_const_t>(L2(string_equal_ignore_whitespace(STRING_ARGS(_1), STRING_ARGS(_2))), expression_string))
            _saved_expressions.push(string_clone(STRING_ARGS(expression_string)));

        expr_result_t result = eval(expression_string);
        if (EXPR_ERROR_CODE == 0)
        {
            //_console_concat_messages = true;
            expr_log_evaluation_result(expression_string, result);
            //_console_concat_messages = false;
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

//
// # PUBLIC API
//

void console_clear()
{
    console_clear_all();
}

void console_show()
{
    _console_window_opened = true;
}

void console_hide()
{
    _console_window_opened = false;
}

void console_set_expression(const char* expression, size_t expression_length)
{
    string_copy(STRING_BUFFER(_console_expression_buffer), expression, expression_length);
    _console_expression_explicitly_set = true;
    console_show();
}

//
// # SYSTEM
//

FOUNDATION_STATIC void console_initialize()
{
    _message_lock = mutex_allocate(STRING_CONST("console_lock"));

    _console_string_table = string_table_allocate(64 * 1024, 64);

    if (!main_is_running_tests())
    {
        log_set_handler(logger);
        _console_window_opened = environment_command_line_arg("console") || session_get_bool("show_console", _console_window_opened);
        service_register_menu(HASH_CONSOLE, console_menu);
    }

    string_const_t joined_expressions = session_get_string("console_expressions", "");
    if (joined_expressions.length)
    {
        string_const_t expression, r = joined_expressions;
        do
        {
            string_split(STRING_ARGS(r), STRING_CONST(";;"), &expression, &r, false);
            if (expression.length)
                _saved_expressions.push(string_clone(STRING_ARGS(expression)));
        } while (r.length > 0); 
    }
}

FOUNDATION_STATIC void console_shutdown()
{
    log_set_handler(nullptr);
    console_clear_all();
    mutex_deallocate(_message_lock);
    session_set_bool("show_console", _console_window_opened);
    string_deallocate(_selected_msg.str);

    if (_saved_expressions.size() > 0)
    {
        string_const_t joined_expressions = string_join(_saved_expressions.begin(), _saved_expressions.end(), LC1(string_to_const(_1)), CTEXT(";;"));
        session_set_string("console_expressions", STRING_ARGS(joined_expressions));
        _saved_expressions.clear();
    }

    string_table_deallocate(_console_string_table);
}

DEFINE_SERVICE(CONSOLE, console_initialize, console_shutdown, SERVICE_PRIORITY_UI_HEADLESS);
