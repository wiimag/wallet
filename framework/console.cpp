/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "console.h"

#include <framework/app.h>
#include <framework/expr.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/module.h>
#include <framework/imgui.h>
#include <framework/scoped_mutex.h>
#include <framework/generics.h>
#include <framework/string.h>
#include <framework/array.h>
#include <framework/system.h>

#include <foundation/log.h>
#include <foundation/error.h>
#include <foundation/hashstrings.h>
#include <foundation/stream.h>

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
    bool prefix{ false };
    hash_t context;
};

static struct CONSOLE_MODULE
{
    mutex_t* lock = nullptr;

    bool opened = false;
    bool focus_last_message = false;
    char search_filter[256]{ 0 };
    int filtered_message_count = -1;
    size_t next_log_message_id = 1;
    string_t selected_msg;
    string_table_t* strings = nullptr;
    bool concat_messages = false;
    char expression_buffer[4096]{ "" };
    bool expression_explicitly_set = false;
    log_message_t* messages{ nullptr };
    size_t max_context_name_length = 0;
    string_t* secret_keys{ nullptr };

    generics::fixed_loop < string_t, 20, [](string_t& s) { string_deallocate(s.str); } > saved_expressions;

    stream_t* log_stream = nullptr;

} *_console_module;

FOUNDATION_STATIC string_table_symbol_t console_string_encode(const char* s, size_t length /* = 0*/)
{
    if (length == 0)
        length = string_length(s);

    if (length == 0)
        return STRING_TABLE_NULL_SYMBOL;

    string_t str = string_clone(s, length);

    // Remove secret key tokens
    for (size_t i = 0; i < array_size(_console_module->secret_keys); ++i)
    {
        string_t key = _console_module->secret_keys[i];
        str = string_replace(STRING_ARGS_CAPACITY(str), STRING_ARGS(key), STRING_CONST("***"), true);
    }

    string_table_symbol_t symbol = string_table_to_symbol(_console_module->strings, STRING_ARGS(str));
    while (symbol == STRING_TABLE_FULL)
    {
        string_table_grow(&_console_module->strings, (int)(_console_module->strings->allocated_bytes * 2.0f));
        symbol = string_table_to_symbol(_console_module->strings, STRING_ARGS(str));
    }

    string_deallocate(str.str);

    return symbol;
}

FOUNDATION_STATIC void logger(hash_t context, error_level_t severity, const char* msg, size_t length)
{
    if (_console_module->log_stream)
    {
        stream_write_string(_console_module->log_stream, msg, length);
        stream_write_endl(_console_module->log_stream);
    }

	#if BUILD_DEBUG
    if (error() == ERROR_ASSERT)
        return;

    if (system_debugger_attached() && severity <= ERRORLEVEL_DEBUG)
        return;
	#endif

    memory_context_push(HASH_CONSOLE);

    if (_console_module->concat_messages)
    {
        scoped_mutex_t lock(_console_module->lock);
        log_message_t* last_message = array_last(_console_module->messages);
        if (last_message)
        {
            string_const_t prev = string_table_to_string_const(_console_module->strings, last_message->msg_symbol);
            string_t new_msg = string_allocate_concat(STRING_ARGS(prev), msg, length);
            last_message->msg_symbol = console_string_encode(new_msg.str, new_msg.length);
            string_deallocate(new_msg.str);
            return;
        }
    }

    {
        log_message_t m{ _console_module->next_log_message_id++, string_hash(msg, length), severity };
        m.context = context;
        m.prefix = log_is_prefix_enabled();

        #if BUILD_ENABLE_STATIC_HASH_DEBUG
        context = context ? context : HASH_DEFAULT;
        string_const_t context_name = hash_to_string(context);
        const size_t hash_code_start = string_find(msg, length, '<', 12);
        const size_t hash_code_end = string_find(msg, length, '>', hash_code_start);

        scoped_mutex_t lock(_console_module->lock);
        if (context_name.length != 0 && hash_code_start != STRING_NPOS && hash_code_end != STRING_NPOS)
        {
            _console_module->max_context_name_length = max(_console_module->max_context_name_length, context_name.length);
            string_t formatted_msg = string_allocate_format(STRING_CONST("%.*s %-*.*s : %.*s"), 
                (int)hash_code_start - 1, msg,
                (int)_console_module->max_context_name_length, STRING_FORMAT(context_name),
                (int)(length - hash_code_end - 1), msg + hash_code_end + 2);

            m.msg_symbol = console_string_encode(formatted_msg.str, formatted_msg.length);
            string_deallocate(formatted_msg.str);
        }
        else
        #endif
        {
            m.msg_symbol = console_string_encode(msg, length);
        }

        string_const_t log_msg = string_table_to_string_const(_console_module->strings, m.msg_symbol);
        string_const_t preview = string_remove_line_returns(SHARED_BUFFER(256), STRING_ARGS(log_msg));

        m.preview_symbol = console_string_encode(STRING_ARGS(preview));
        array_push_memcpy(_console_module->messages, &m);
    }

    _console_module->focus_last_message = true;
    memory_context_pop();
}

FOUNDATION_STATIC string_const_t console_get_log_trimmed_text(const log_message_t& log)
{
    // Find the first : character and truncate the text length
    string_const_t tooltip_log_message = string_table_to_string_const(_console_module->strings, log.msg_symbol);
    if (!log.prefix)
        return tooltip_log_message;
    
    constexpr size_t log_prefix_time_skip_char_count = 13;
    const size_t sep_pos = string_find(STRING_ARGS(tooltip_log_message), ':', log_prefix_time_skip_char_count);
    if (sep_pos == STRING_NPOS)
        return tooltip_log_message;

    tooltip_log_message.str += sep_pos + 1;
    tooltip_log_message.length -= sep_pos + 1;
    tooltip_log_message = string_trim(string_trim(tooltip_log_message), '\n');
    return tooltip_log_message;
}

FOUNDATION_STATIC void console_render_logs(const ImRect& rect)
{
    const size_t log_count = array_size(_console_module->messages);
    const int loop_count = _console_module->filtered_message_count <= 0 ? (int)log_count : _console_module->filtered_message_count;
    ImGuiListClipper clipper;
    clipper.Begin(loop_count);
    while (clipper.Step())
    {
        if (clipper.DisplayStart >= clipper.DisplayEnd)
            continue;

        if (mutex_lock(_console_module->lock))
        {
            const float window_width = ImGui::GetWindowWidth();
            const float item_available_width = ImGui::GetContentRegionAvail().x;
            for (size_t i = clipper.DisplayStart; i < min(clipper.DisplayEnd, (int)array_size(_console_module->messages)); ++i)
            {
                log_message_t& log = _console_module->messages[i];

                if (log.severity == ERRORLEVEL_ERROR)
                    ImGui::PushStyleColor(ImGuiCol_Text, TEXT_BAD_COLOR);
                else if (log.severity == ERRORLEVEL_WARNING)
                    ImGui::PushStyleColor(ImGuiCol_Text, TEXT_WARN_COLOR);

                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.0f));
                string_const_t msg_str = log.preview_symbol != STRING_TABLE_NULL_SYMBOL ? 
                    string_table_to_string_const(_console_module->strings,log.preview_symbol) :
                    string_table_to_string_const(_console_module->strings, log.msg_symbol);

                if (ImGui::Selectable(msg_str.str, &log.selectable, ImGuiSelectableFlags_DontClosePopups, {0, 0}))
                {
                    string_deallocate(_console_module->selected_msg.str);
                    string_const_t csmm = console_get_log_trimmed_text(log);
                    _console_module->selected_msg = string_clone(STRING_ARGS(csmm));
                    ImGui::SetClipboardText(_console_module->selected_msg.str);
                }
                ImGui::PopStyleVar();

                // Check if last item is clipped in order to display a tooltip if it is the case.
                const float item_renderered_width = ImGui::GetItemRectMax().x - ImGui::GetItemRectMin().x;
                if (ImGui::IsItemHovered() && item_renderered_width > window_width)
                {
                    ImGui::SetNextWindowSize({ window_width * 0.9f, 0 });
                    if (ImGui::BeginTooltip())
                    {
                        string_const_t tooltip_log_message = console_get_log_trimmed_text(log);
                        ImGui::TextWrapped("%.*s", STRING_FORMAT(tooltip_log_message));
                        ImGui::EndTooltip();
                    }
                    
                }

                if (log.severity == ERRORLEVEL_ERROR || log.severity == ERRORLEVEL_WARNING)
                    ImGui::PopStyleColor(1);
            }
            mutex_unlock(_console_module->lock);
        }
    }

    if (_console_module->focus_last_message)
    {
        // First check if the view is already scrolled up?
        const float sm = ImGui::GetScrollMaxY();
        const float sy = ImGui::GetScrollY();

        if (sy >= sm)
        {
            ImGui::Dummy({});
            ImGui::ScrollToItem();
            ImGui::SetItemDefaultFocus();
        }

        _console_module->focus_last_message = false;
    }
}

FOUNDATION_STATIC void console_render_selected_log(const ImRect& rect)
{
    if (_console_module->selected_msg.length == 0)
        return;
    const ImVec2 asize = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##SelectedTex", _console_module->selected_msg.str, _console_module->selected_msg.length,
        asize, ImGuiInputTextFlags_ReadOnly);
}

FOUNDATION_STATIC void console_render_messages()
{
    ImGui::SetWindowFontScale(0.9f);

    imgui_frame_render_callback_t selected_log_frame = nullptr;
    if (_console_module->selected_msg.length)
        selected_log_frame = console_render_selected_log;

    imgui_draw_splitter("Messages", 
        console_render_logs, 
        selected_log_frame,
        IMGUI_SPLITTER_VERTICAL, ImGuiWindowFlags_None, 0.80f, true);

    ImGui::SetWindowFontScale(1.0f);
}

FOUNDATION_STATIC void console_clear_all()
{
    string_deallocate(_console_module->selected_msg.str);
    _console_module->selected_msg = {};
    
    if (!mutex_lock(_console_module->lock))
        return;

    _console_module->filtered_message_count = -1;
    _console_module->search_filter[0] = '\0';
    array_deallocate(_console_module->messages);

    int new_size = to_int(_console_module->strings->allocated_bytes);
    string_table_deallocate(_console_module->strings);
    _console_module->strings = string_table_allocate(new_size, 64);
    _console_module->max_context_name_length = 0;
    
    mutex_unlock(_console_module->lock);
}

FOUNDATION_STATIC void console_render_toolbar()
{
    static float clear_button_width = IM_SCALEF(100.0f);
    static const float button_frame_padding = IM_SCALEF(8.0f);
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - clear_button_width - button_frame_padding);
    if (ImGui::InputTextWithHint("##SearchLog", tr("Search logs..."), STRING_BUFFER(_console_module->search_filter)))
    {
        _console_module->filtered_message_count = 0;
        const size_t filter_length = string_length(_console_module->search_filter);
        if (filter_length > 0)
        {
            size_t log_count = array_size(_console_module->messages);
            for (_console_module->filtered_message_count = 0; _console_module->filtered_message_count < log_count;)
            {
                const log_message_t& log = _console_module->messages[_console_module->filtered_message_count];
                string_const_t log_msg = string_table_to_string_const(_console_module->strings, log.msg_symbol);
                if (string_contains_nocase(STRING_ARGS(log_msg), _console_module->search_filter, filter_length))
                {
                    _console_module->filtered_message_count++;
                }
                else
                {
                    std::swap(_console_module->messages[_console_module->filtered_message_count], _console_module->messages[log_count - 1]);
                    log_count--;
                }
            }
        }
        else
        {
            array_sort(_console_module->messages, ARRAY_LESS_BY(id));
        }
    }

    ImGui::SameLine();
    if (ImGui::Button(tr("Clear")))
        console_clear_all();
    clear_button_width = ImGui::GetItemRectSize().x;
    ImGui::EndGroup();
}

FOUNDATION_STATIC void console_render_evaluator()
{
    static bool focus_text_field = true;

    if (ImGui::IsWindowAppearing())
    {
        if (!_console_module->expression_explicitly_set)
            session_get_string("console_expression", STRING_BUFFER(_console_module->expression_buffer), "");
        _console_module->expression_explicitly_set = false;
    }

    static char input_id[32] = "##Expression";
    if (_console_module->saved_expressions.size() > 2 && ImGui::IsWindowFocused())
    {
        if (ImGui::Shortcut(ImGuiKey_UpArrow | ImGuiMod_Alt))
        {
            const string_t& last_expression = _console_module->saved_expressions.move(-1);
            string_t ec = string_copy(STRING_BUFFER(_console_module->expression_buffer), STRING_ARGS(last_expression));
            string_format(STRING_BUFFER(input_id), STRING_CONST("##%" PRIhash), string_hash(ec.str, ec.length));
            focus_text_field = true;
        }
        else if (ImGui::Shortcut(ImGuiKey_DownArrow | ImGuiMod_Alt))
        {
            const string_t& last_expression = _console_module->saved_expressions.move(+1);
            string_t ec = string_copy(STRING_BUFFER(_console_module->expression_buffer), STRING_ARGS(last_expression));
            string_format(STRING_BUFFER(input_id), STRING_CONST("##%" PRIhash), string_hash(ec.str, ec.length));
            focus_text_field = true;
        }
    }

    if (focus_text_field)
    {
        ImGui::SetKeyboardFocusHere();
    }
    
    bool evaluate = false;
    if (ImGui::InputTextMultiline(input_id, STRING_BUFFER(_console_module->expression_buffer),
        ImVec2(IM_SCALEF(-98.0f), -1), 
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
    if (ImGui::Button(tr("Eval"), ImVec2(-1, -1)))
        evaluate = true;

    if (evaluate)
    {
        string_t expression_string = string_clone(_console_module->expression_buffer, string_length(_console_module->expression_buffer));
        if (!_console_module->saved_expressions.includes<string_t>(L2(string_equal_ignore_whitespace(STRING_ARGS(_1), STRING_ARGS(_2))), expression_string))
            _console_module->saved_expressions.push(string_clone(STRING_ARGS(expression_string)));

        session_set_string("console_expression", STRING_ARGS(expression_string));

        // Remove all comments starting with # or // from the expression on each line
        char* line_start = expression_string.str;
        char* line_end = expression_string.str;
        while (line_end)
        {
            size_t line_start_offset = line_start - expression_string.str;
            size_t line_end_pos = string_find(STRING_ARGS(expression_string), '\n', line_start_offset);
            if (line_end_pos != STRING_NPOS)
            {
                size_t comment_start_pos = string_find(STRING_ARGS(expression_string), '#', line_start_offset);
                if (comment_start_pos == STRING_NPOS || comment_start_pos > line_start_offset)
                    comment_start_pos = string_find_string(STRING_ARGS(expression_string), STRING_CONST("//"), line_start_offset);
                if (comment_start_pos != STRING_NPOS && comment_start_pos < line_end_pos)
                {
                    memmove(expression_string.str + comment_start_pos, expression_string.str + line_end_pos, expression_string.length - line_end_pos);
                    expression_string.length -= line_end_pos - comment_start_pos;
                    line_end = expression_string.str + comment_start_pos;
                }
                else
                {
                    line_end = expression_string.str + line_end_pos;
                }

                line_start = line_end + 1;
            }
            else
            {
                line_end = 0;
                expression_string.str[expression_string.length] = 0;
            }
        }

        // Remove empty lines
        line_start = expression_string.str;
        line_end = expression_string.str;
        while (line_end)
        {
            size_t line_start_offset = line_start - expression_string.str;
            size_t line_end_pos = string_find(STRING_ARGS(expression_string), '\n', line_start_offset);
            if (line_end_pos != STRING_NPOS)
            {
                if (line_end_pos == line_start_offset)
                {
                    memmove(expression_string.str + line_start_offset, expression_string.str + line_end_pos + 1, expression_string.length - line_end_pos);
                    expression_string.length -= line_end_pos - line_start_offset + 1;
                    line_end = expression_string.str + line_start_offset;
                }
                else
                {
                    line_end = expression_string.str + line_end_pos;
                }
                line_start = line_end + 1;
            }
            else
            {
                line_end = 0;
                expression_string.str[expression_string.length] = 0;
            }
        }

        expr_result_t result = eval(string_to_const(expression_string));
        if (EXPR_ERROR_CODE == 0)
        {
            expr_log_evaluation_result(string_to_const(expression_string), result);
        }
        else if (EXPR_ERROR_CODE != 0)
        {
            log_errorf(HASH_EXPR, ERROR_SCRIPT, STRING_CONST("[%d] %.*s -> %.*s"),
                EXPR_ERROR_CODE, STRING_FORMAT(expression_string), (int)string_length(EXPR_ERROR_MSG), EXPR_ERROR_MSG);
        }

        focus_text_field = true;

        string_deallocate(expression_string.str);
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

    if (ImGui::Begin("Console##5", &_console_module->opened,
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
    if (_console_module->opened)
        console_render_window();
}

FOUNDATION_STATIC void console_module_ensure_initialized()
{
    if (_console_module == nullptr)
    {
        _console_module = MEM_NEW(HASH_CONSOLE, CONSOLE_MODULE);
        _console_module->lock = mutex_allocate(STRING_CONST("console_lock"));
        
        string_const_t log_path = session_get_user_file_path(STRING_CONST("log.txt"));
        if (fs_is_file(STRING_ARGS(log_path)))
        {
            // Move log file to prev_log.txt
            string_const_t prev_log_path = session_get_user_file_path(STRING_CONST("prev_log.txt"));
            fs_move_file(STRING_ARGS(log_path), STRING_ARGS(prev_log_path));
        }
        
        _console_module->log_stream = stream_open(STRING_ARGS(log_path), STREAM_OUT | STREAM_CREATE | STREAM_TRUNCATE | STREAM_SYNC);
    }
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
    _console_module->opened = true;
}

void console_hide()
{
    _console_module->opened = false;
}

void console_set_expression(const char* expression, size_t expression_length)
{
    string_copy(STRING_BUFFER(_console_module->expression_buffer), expression, expression_length);
    _console_module->expression_explicitly_set = true;
    console_show();
}

void console_add_secret_key_token(const char* key, size_t key_length)
{
    console_module_ensure_initialized();
    scoped_mutex_t lock(_console_module->lock);
    string_t secret_key = string_clone(key, key_length);
    array_push(_console_module->secret_keys, secret_key);
}

//
// # SYSTEM
//

FOUNDATION_STATIC void console_initialize()
{
    console_module_ensure_initialized();

    _console_module->strings = string_table_allocate(64 * 1024, 64);

    if (BUILD_APPLICATION && !main_is_running_tests())
    {
        log_set_handler(logger);
        _console_module->opened = environment_argument("console") || session_get_bool("show_console", _console_module->opened);
        module_register_menu(HASH_CONSOLE, console_menu);

        app_register_menu(HASH_CONSOLE,  STRING_CONST("Windows/" ICON_MD_LOGO_DEV " Console"), STRING_CONST("F10"), AppMenuFlags::Append, [](void*)
        {
            _console_module->opened = !_console_module->opened;
        });
    }

    string_const_t joined_expressions = session_get_string("console_expressions", "");
    if (joined_expressions.length)
    {
        string_const_t expression, r = joined_expressions;
        do
        {
            string_split(STRING_ARGS(r), STRING_CONST(";;"), &expression, &r, false);
            if (expression.length)
                _console_module->saved_expressions.push(string_clone(STRING_ARGS(expression)));
        } while (r.length > 0); 
    }
}

FOUNDATION_STATIC void console_shutdown()
{
    {
        scoped_mutex_t lock(_console_module->lock);
        log_set_handler(nullptr);
    }

    console_clear_all();
    mutex_deallocate(_console_module->lock);
    session_set_bool("show_console", _console_module->opened);
    string_deallocate(_console_module->selected_msg.str);

    if (_console_module->saved_expressions.size() > 0)
    {
        string_const_t joined_expressions = string_join(_console_module->saved_expressions.begin(), _console_module->saved_expressions.end(), LC1(string_to_const(_1)), CTEXT(";;"));
        session_set_string("console_expressions", STRING_ARGS(joined_expressions));
        _console_module->saved_expressions.clear();
    }

    string_table_deallocate(_console_module->strings);
    string_array_deallocate(_console_module->secret_keys);

    if (_console_module->log_stream)
    {
        stream_deallocate(_console_module->log_stream);
        _console_module->log_stream = nullptr;
    }

    MEM_DELETE(_console_module);
}

DEFINE_MODULE(CONSOLE, console_initialize, console_shutdown, MODULE_PRIORITY_UI_HEADLESS);
