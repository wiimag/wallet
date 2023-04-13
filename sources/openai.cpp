/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "openai.h"

#include "eod.h"
#include "stock.h"

#include <framework/app.h>
#include <framework/imgui.h>
#include <framework/query.h>
#include <framework/session.h>
#include <framework/module.h>
#include <framework/window.h>
#include <framework/array.h>
#include <framework/config.h>
#include <framework/string_builder.h>
#include <framework/localization.h>
#include <framework/jobs.h>
#include <framework/console.h>

#include <foundation/stream.h>
#include <foundation/environment.h>

#include <ctype.h>

#define HASH_OPENAI static_hash_string("openai", 6, 0x6ce8d96f30f6bd41ULL)

constexpr size_t OPENAI_API_KEY_CAPACITY = 256;
constexpr size_t OPENAI_ORGANIZATION_CAPACITY = 256;

constexpr string_const_t PROMPT_JSON_SKIP_FIELDS[] = {
    CTEXT("date"),
    CTEXT("MostRecentQuarter"),
    CTEXT("netTangibleAssets"),
    CTEXT("MarketCapitalizationMln"),
    CTEXT("liabilitiesAndStockholdersEquity"),
    CTEXT("DividendYield"),
    CTEXT("accumulatedOtherComprehensiveIncome"),
    CTEXT("nonCurrrentAssetsOther"),
    CTEXT("SharesShortPriorMonth"),
};

typedef enum class Permission {
    None = 0,
    CreateEngine = 1 << 0,
    Sampling = 1 << 1,
    Logprobs = 1 << 2,
    SearchIndices = 1 << 3,
    View = 1 << 4,
    FineTuning = 1 << 5,
    Blocking = 1 << 6,
} openai_permission_t;
DEFINE_ENUM_FLAGS(Permission);

typedef enum class MessageRole {
    None = 0,
    Error = 1,
    User = 2,
    Assistant = 3,
} openai_message_role_t;

struct openai_choice_t
{
    int         index{ 0 };
    string_t    reason{};
    string_t    content{};
    string_t    role{};
};

struct openai_completions_t
{
    string_t id{};
    string_t type{};
    time_t created{ 0 };

    openai_choice_t* choices{ nullptr };

    int usage_completion_tokens{ 0 };
    int usage_prompt_tokens{ 0 };
    int usage_total_tokens{ 0 };
};

struct openai_message_t
{
    openai_message_role_t   role{ MessageRole::None };
    string_t                text{};
};

struct openai_model_t
{
    string_t            id;
    time_t              created;
    openai_permission_t permissions;
};

struct openai_window_t
{
    openai_model_t* models{ nullptr };
    openai_model_t* selected_model{ nullptr };

    char prompt[4096]{ 0 };
    openai_message_t* messages{ nullptr };
};

struct openai_prompt_t
{
    string_t symbol{};
    string_t text{};
    string_t user_prompt{};
};

static struct OPENAI_MODULE 
{
    string_t            apikey{};
    string_t            organization{};

    string_t*           http_headers{ nullptr };

    bool                connected{ false };

    openai_prompt_t*    prompts{ nullptr };
    openai_response_t** responses{ nullptr };

} *_openai_module;

FOUNDATION_STATIC string_t* openai_ensure_http_headers()
{
    string_array_deallocate(_openai_module->http_headers);

    string_t Authorization = string_allocate_format(STRING_CONST("Authorization: Bearer %.*s"), STRING_FORMAT(_openai_module->apikey));
    string_t Organization = string_allocate_format(STRING_CONST("OpenAI-Organization: %.*s"), STRING_FORMAT(_openai_module->organization));

    array_push(_openai_module->http_headers, Authorization);
    array_push(_openai_module->http_headers, Organization);

    return _openai_module->http_headers;
}

FOUNDATION_STATIC const char* openai_ensure_key_loaded()
{
    FOUNDATION_ASSERT_MSG(_openai_module, "OpenAI module not initialized");

    if (_openai_module->apikey.length)
        return _openai_module->apikey.str;

    string_deallocate(_openai_module->apikey.str);
    string_deallocate(_openai_module->organization.str);

    _openai_module->apikey = string_allocate(0, OPENAI_API_KEY_CAPACITY);
    _openai_module->organization = string_allocate(0, OPENAI_ORGANIZATION_CAPACITY);

    _openai_module->apikey.str[0] = '\0';
    _openai_module->organization.str[0] = '\0';

    string_const_t key_file_path = session_get_user_file_path(STRING_CONST("openai.key"));
    if (fs_is_file(STRING_ARGS(key_file_path)))
    {
        stream_t* key_stream = fs_open_file(STRING_ARGS(key_file_path), STREAM_IN);
        if (key_stream)
        {
            string_t key = stream_read_string(key_stream);
            string_t organization = stream_read_string(key_stream);

            _openai_module->apikey = string_copy(_openai_module->apikey.str, OPENAI_API_KEY_CAPACITY, STRING_ARGS(key));
            _openai_module->organization = string_copy(_openai_module->organization.str, OPENAI_ORGANIZATION_CAPACITY, STRING_ARGS(organization));

            string_deallocate(key.str);
            string_deallocate(organization.str);

            stream_deallocate(key_stream);
        }
    }

    // Check if the key is specified on the command line
    string_const_t cmdline_key;
    if (environment_argument("openai-api-key", &cmdline_key))
        _openai_module->apikey = string_copy(_openai_module->apikey.str, OPENAI_API_KEY_CAPACITY, STRING_ARGS(cmdline_key));

    string_const_t cmdline_organization;
    if (environment_argument("openai-organization", &cmdline_organization))
        _openai_module->organization = string_copy(_openai_module->organization.str, OPENAI_ORGANIZATION_CAPACITY, STRING_ARGS(cmdline_organization));

    console_add_secret_key_token(STRING_ARGS(_openai_module->apikey));
    console_add_secret_key_token(STRING_ARGS(_openai_module->organization));

    openai_ensure_http_headers();
    return _openai_module->apikey.str;
}

FOUNDATION_STATIC void openai_save_api_key()
{
    FOUNDATION_ASSERT_MSG(_openai_module, "OpenAI module not initialized");
    
    // Save the API key to a file
    if (_openai_module->apikey.length)
    {
        string_const_t key_file_path = session_get_user_file_path(STRING_CONST("openai.key"));
        stream_t* key_stream = fs_open_file(STRING_ARGS(key_file_path), STREAM_CREATE | STREAM_OUT | STREAM_TRUNCATE);
        if (key_stream)
        {
            stream_write_string(key_stream, STRING_ARGS(_openai_module->apikey));
            stream_write_endl(key_stream);
            stream_write_string(key_stream, STRING_ARGS(_openai_module->organization));
            stream_write_endl(key_stream);
            stream_deallocate(key_stream);
        }

        openai_ensure_http_headers();
    }
}

FOUNDATION_STATIC string_const_t openai_api_url()
{
    return CTEXT("https://api.openai.com");
}

FOUNDATION_STATIC const char* openai_build_url(const char* api, const char* fmt = nullptr, ...)
{
    static thread_local char url_buffer[2048] = {0};

    string_const_t root_url = openai_api_url();

    if (fmt)
    {
        char fmt_buffer[2048] = { 0 };

        va_list args;
        va_start(args, fmt);
        string_t fmtstr = string_vformat(STRING_BUFFER(fmt_buffer), fmt, string_length(fmt), args);
        va_end(args);

        string_t url = string_format(STRING_BUFFER(url_buffer), STRING_CONST("%.*s/v1/%s/%.*s"), STRING_FORMAT(root_url), api, STRING_FORMAT(fmtstr));
        return url.str;
    }

    string_t url = string_format(STRING_BUFFER(url_buffer), STRING_CONST("%.*s/v1/%s"), STRING_FORMAT(root_url), api);
    return url.str;
}

FOUNDATION_STATIC bool openai_execute_query(const char* query, const query_callback_t& callback)
{
    string_t* headers = openai_ensure_http_headers();
    return query_execute_json(query, headers, callback);
}

FOUNDATION_STATIC bool openai_execute_query(const char* query, config_handle_t data, const query_callback_t& callback)
{
    string_t* headers = openai_ensure_http_headers();
    return query_execute_json(query, headers, data, callback);
}

FOUNDATION_STATIC void openai_check_connectivity()
{
    string_t* headers = openai_ensure_http_headers();
    query_execute_json(openai_build_url("models"), headers, [](const json_object_t& res)
    {
        _openai_module->connected = res.is_valid();
        if (_openai_module->connected)
        {
            log_infof(HASH_OPENAI, STRING_CONST("OpenAI connectivity check succeeded (%d,%d)"), res.error_code, res.status_code);
        }
        else
        {
            log_warnf(HASH_OPENAI, WARNING_NETWORK, STRING_CONST("OpenAI connectivity check failed (%d,%d): %.*s"),
                res.error_code, res.status_code, STRING_FORMAT(res.to_string()));
        }
    });
}

FOUNDATION_STATIC void openai_handle_prompt_completions(const json_object_t& res, openai_window_t* window, openai_completions_t& completions)
{
    if (!res.resolved())
    {
        /*
        TODO Handle errors with :
        "error": {
            "message": "Unrecognized request argument supplied: conversation_id",
            "type": "invalid_request_error",
            "param": null,
            "code": null
          }
        */
        

        auto cverror = res["error"];
        if (cverror.is_valid())
        {
            string_const_t message = cverror["message"].as_string();
            string_const_t type = cverror["type"].as_string();
            string_const_t param = cverror["param"].as_string();
            string_const_t code = cverror["code"].as_string();

            openai_message_t msg;
            msg.role = MessageRole::Error;
            msg.text = string_allocate_format(STRING_CONST("OpenAI prompt completion failed (%d,%d): [%.*s] %.*s\n%.*s"),
                           res.error_code, res.status_code,  STRING_FORMAT(type), STRING_FORMAT(message), STRING_FORMAT(res.to_string()));
            array_push_memcpy(window->messages, &msg);
        }
        else
        {
            log_warnf(HASH_OPENAI, WARNING_NETWORK, STRING_CONST("OpenAI prompt completion failed (%d,%d): %.*s"),
                res.error_code, res.status_code, STRING_FORMAT(res.to_string()));
        }

        return;
    }

    string_const_t id = res["id"].as_string();
    string_const_t object = res["object"].as_string();

    completions.id = string_clone(STRING_ARGS(id));
    completions.type = string_clone(STRING_ARGS(object));
    completions.created = res["created"].as_time();
    completions.usage_completion_tokens = res["usage"]["completion_tokens"].as_integer();
    completions.usage_prompt_tokens = res["usage"]["prompt_tokens"].as_integer();
    completions.usage_total_tokens = res["usage"]["total_tokens"].as_integer();

    auto choices = res["choices"];
    for (auto e : choices)
    {
        openai_choice_t choice;
        choice.index = e["index"].as_integer();
        choice.reason = e["finish_reason"].as_string_clone();
        choice.content = e["message"]["content"].as_string_clone();
        choice.role = e["message"]["role"].as_string_clone();

        array_push_memcpy(completions.choices, &choice);
    }
}

FOUNDATION_STATIC openai_completions_t openai_window_execute_prompt(openai_window_t* window, const char* prompt, size_t prompt_length)
{
    config_handle_t data = config_allocate();
    config_set(data, "model", STRING_ARGS(window->selected_model->id));
    config_set(data, "temperature", 0.7);
    config_set(data, "n", 3.0);

    {
        config_handle_t cvmessages = config_set_array(data, STRING_CONST("messages"));

        // Retrieve the context from the last messages
        foreach(h, window->messages)
        {
            if (h->role == MessageRole::User)
            {
                config_handle_t history = config_array_push(cvmessages, CONFIG_VALUE_OBJECT);
                config_set(history, "role", STRING_CONST("user"));
                config_set(history, "content", STRING_ARGS(h->text));
            }
            else if (h->role == MessageRole::Assistant)
            {
                config_handle_t history = config_array_push(cvmessages, CONFIG_VALUE_OBJECT);
                config_set(history, "role", STRING_CONST("assistant"));
                config_set(history, "content", STRING_ARGS(h->text));
            }
        }

        // Add the new prompt message
        config_handle_t cvmessage = config_array_push(cvmessages, CONFIG_VALUE_OBJECT);
        config_set(cvmessage, "role", STRING_CONST("user"));
        config_set(cvmessage, "content", prompt, prompt_length);
    }

    openai_completions_t completions;
    const char* prompt_query = openai_build_url("chat", "completions");
    if (!openai_execute_query(prompt_query, data, [window, &completions](const auto& _1) { openai_handle_prompt_completions(_1, window, completions); }))
    {
        log_warnf(HASH_OPENAI, WARNING_NETWORK, STRING_CONST("OpenAI prompt completion failed: %s"), prompt_query);
    }

    config_deallocate(data);

    return completions;
}

FOUNDATION_STATIC void openai_window_render_messages(openai_window_t* window)
{
    const float table_height = ImGui::GetContentRegionAvail().y - IM_SCALEF(100);
    if (!ImGui::BeginTable("##Messages", 1, ImGuiTableFlags_Hideable | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, table_height)))
        return;

    ImGui::TableSetupColumn("Messages", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    foreach(msg, window->messages)
    {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (msg->role == MessageRole::User)
            ImGui::BulletTextWrapped("%.*s", STRING_FORMAT(msg->text));
        else if (msg->role == MessageRole::Assistant)
            ImGui::TextWrapped(STRING_RANGE(msg->text));
        else if (msg->role == MessageRole::Error)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
            ImGui::TextWrapped(STRING_RANGE(msg->text));
            ImGui::PopStyleColor();
        }
    }

    ImGui::EndTable();
}

FOUNDATION_STATIC void openai_dispose_completions(openai_completions_t& completions)
{
    string_deallocate(completions.id);
    string_deallocate(completions.type);
    foreach(choice, completions.choices)
    {
        string_deallocate(choice->reason);
        string_deallocate(choice->content);
        string_deallocate(choice->role);
    }
    array_deallocate(completions.choices);
}

FOUNDATION_STATIC bool openai_window_log_completions(openai_window_t* window, const char* prompt, size_t prompt_length, openai_completions_t& completions)
{
    const size_t num_choices = array_size(completions.choices);
    if (!num_choices)
        return false;
    if (num_choices)
    {
        // Add prompt as a message
        openai_message_t msg;
        msg.role = MessageRole::User;
        msg.text = string_clone(prompt, prompt_length);
        array_push_memcpy(window->messages, &msg);

        // Select a completion choice
        const openai_choice_t* choice = completions.choices + 0;
        msg.role = MessageRole::Assistant;
        msg.text = string_clone(STRING_ARGS(choice->content));
        array_push_memcpy(window->messages, &msg);
    }

    // Dispose of the completions data
    openai_dispose_completions(completions);

    return true;
}

FOUNDATION_STATIC void openai_window_render(openai_window_t* window)
{
    FOUNDATION_ASSERT(window);

    ImGui::TrTextUnformatted("Models");
    ImGui::SameLine();
    ImGui::ExpandNextItem();
    if (ImGui::BeginCombo("##Models", window->selected_model ? (const char*)window->selected_model->id.str : tr("None")))
    {
        foreach (m, window->models)
        {
            bool selected = window->selected_model == m;
            if (ImGui::Selectable(m->id.str, selected))
            {
                window->selected_model = m;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();

            if ((m->permissions & Permission::FineTuning) != 0)
            {
                ImGui::SameLine();
                ImGui::TextUnformatted(tr("(Fine-tuning)"));
            }

            string_const_t created_string = string_from_time_static(m->created * 1000ULL, true);
            ImGui::SameLine(IM_SCALEF(400));
            ImGui::TextUnformatted(created_string.str);
        }

        ImGui::EndCombo();
    }

    if (window->selected_model == nullptr)
        return;

    openai_window_render_messages(window);

    static bool set_focus = true;
    ImGui::ExpandNextItem();
    if (set_focus)
    {
        ImGui::SetKeyboardFocusHere();
        set_focus = false;
    }
    if (ImGui::InputTextMultiline("##Prompt", STRING_BUFFER(window->prompt), ImVec2(-1, ImGui::GetContentRegionAvail().y),
        ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const size_t prompt_length = string_length(window->prompt);
        if (prompt_length)
        {
            auto completions = openai_window_execute_prompt(window, window->prompt, prompt_length);

            if (openai_window_log_completions(window, window->prompt, prompt_length, completions))
            {
                window->prompt[0] = 0;
                set_focus = true;
            }
        }
    }
}

FOUNDATION_STATIC void openai_window_load_models(openai_window_t* window)
{
    FOUNDATION_ASSERT(window);

    const char* query = openai_build_url("models");
    openai_execute_query(query, [window](const json_object_t& json)
    {
        openai_model_t* models = nullptr;
        for (auto cvm : json["data"])
        {
            string_const_t id = cvm["id"].as_string();
            if (string_is_null(id))
                continue;

            openai_model_t m{};
            m.id = string_clone(STRING_ARGS(id));
            m.permissions = Permission::None;
            m.created = cvm["created"].as_time();

            auto cvpermissions = cvm["permission"].get(to_size(0));
            if (cvpermissions["allow_create_engine"].as_boolean()) m.permissions |= Permission::CreateEngine;
            if (cvpermissions["allow_sampling"].as_boolean()) m.permissions |= Permission::Sampling;
            if (cvpermissions["allow_logprobs"].as_boolean()) m.permissions |= Permission::Logprobs;
            if (cvpermissions["allow_search_indices"].as_boolean()) m.permissions |= Permission::SearchIndices;
            if (cvpermissions["allow_view"].as_boolean()) m.permissions |= Permission::View;
            if (cvpermissions["allow_fine_tuning"].as_boolean()) m.permissions |= Permission::FineTuning;
            if (cvpermissions["is_blocking"].as_boolean()) m.permissions |= Permission::Blocking;

            array_push_memcpy(models, &m);
        }

        array_sort(models, LC2(_2.created - _1.created));
        window->models = models;
    });
}

FOUNDATION_STATIC void openai_window_render_handle(window_handle_t handle)
{
    openai_window_t* window = (openai_window_t*)window_get_user_data(handle);
    openai_window_render(window);
}

FOUNDATION_STATIC void openai_window_close(window_handle_t handle)
{
    openai_window_t* window = (openai_window_t*)window_get_user_data(handle);
    FOUNDATION_ASSERT(window);

    foreach(m, window->models)
        string_deallocate(m->id.str);
    array_deallocate(window->models);

    foreach(msg, window->messages)
        string_deallocate(msg->text);
    array_deallocate(window->models);

    MEM_DELETE(window);
}

FOUNDATION_STATIC openai_window_t* openai_window_allocate()
{
    openai_window_t* window = MEM_NEW(HASH_OPENAI, openai_window_t);
    openai_window_load_models(window);
    return window;
}

FOUNDATION_STATIC void openai_run_tests(void* user_data)
{
    window_open(HASH_OPENAI, STRING_CONST("OpenAI"), 
        openai_window_render_handle, 
        openai_window_close, 
        openai_window_allocate(), WindowFlags::Singleton);
}

//
// # PUBLIC API
//

bool openai_available()
{
    const char* key = openai_ensure_key_loaded();
    return key && key[0];
}

string_t openai_get_api_key()
{
    return _openai_module->apikey;
}

size_t openai_get_api_key_capacity()
{
    return OPENAI_API_KEY_CAPACITY-1;
}

string_t openai_get_organization()
{
    return _openai_module->organization;
}

size_t openai_get_organization_capacity()
{
    return OPENAI_ORGANIZATION_CAPACITY-1;
}

void openai_set_api_key(const char* key, const char* organization)
{
    FOUNDATION_ASSERT_MSG(key, "Key cannot be null");
    FOUNDATION_ASSERT_MSG(_openai_module, "OpenAI module not initialized");

    _openai_module->apikey = string_copy(_openai_module->apikey.str, OPENAI_API_KEY_CAPACITY, key, min(OPENAI_API_KEY_CAPACITY, string_length(key)));

    if (organization)
        openai_set_organization(organization);
    else
        openai_save_api_key();

    console_add_secret_key_token(STRING_ARGS(_openai_module->apikey));
    console_add_secret_key_token(STRING_ARGS(_openai_module->organization));
}

void openai_set_organization(const char* organization)
{
    FOUNDATION_ASSERT_MSG(organization, "Organization cannot be null");
    FOUNDATION_ASSERT_MSG(_openai_module, "OpenAI module not initialized");

    _openai_module->organization = string_copy(_openai_module->organization.str, OPENAI_ORGANIZATION_CAPACITY, 
        organization, min(OPENAI_ORGANIZATION_CAPACITY, string_length(organization)));

    openai_save_api_key();
}

FOUNDATION_STATIC string_const_t string_camel_case_to_lowercase_phrase(const char* expression, size_t expression_length)
{
    // Ignore expression that are all uppercase
    bool all_uppercase = true;
    for (size_t i = 0; i < expression_length; ++i)
    {
        char c = expression[i];
        if (c >= 'a' && c <= 'z')
        {
            all_uppercase = false;
            break;
        }
    }

    if (all_uppercase)
        return string_const(expression, expression_length);

    size_t phrase_length = 0;
    static thread_local char phrase_buffer[1024];
    for (size_t i = 0; i < min(expression_length, sizeof(phrase_buffer)-1); ++i)
    {
        char c = expression[i];

        // If we have a capital letter, add a space and the lowercase version, ignore if the previous character was a capital letter
        if (c >= 'A' && c <= 'Z' && (i == 0 || (expression[i-1] < 'A' || expression[i-1] > 'Z')))
        {
            if (i > 0)
                phrase_buffer[phrase_length++] = ' ';

            // Convert to lowercase only if the next character is not a capital letter
            if (i < expression_length-1 && (expression[i+1] < 'A' || expression[i+1] > 'Z'))
                phrase_buffer[phrase_length++] = tolower(c);
            else
                phrase_buffer[phrase_length++] = c;
        }
        else if (c == '_')
        {
            phrase_buffer[phrase_length++] = ' ';
        }
        else
            phrase_buffer[phrase_length++] = c;
    }
    phrase_buffer[phrase_length] = 0;
    return string_const(phrase_buffer, phrase_length);
}

FOUNDATION_STATIC void openai_generate_json_object_prompt(string_builder_t* sb, const json_object_t& obj)
{
    for (auto e : obj)
    {
        bool skip_field = e.is_null();
        if (skip_field)
            continue;

        string_const_t field_name = e.id();
        for (auto f : PROMPT_JSON_SKIP_FIELDS)
        {
            if (string_equal(f, field_name))
            {
                skip_field = true;
                break;
            }
        }

        if (skip_field)
            continue;

        string_const_t value = e.as_string();
        if (string_is_null(value))
            continue;

        // Skip fields with zero value
        double n = 0;
        if (string_try_convert_number(value.str, value.length, n) && math_real_is_zero(n))
            continue;

        // Remove trailing 0 from strings
        if (string_ends_with(STRING_ARGS(value), STRING_CONST(".0")))
            value.length-=2;
        else if (string_ends_with(STRING_ARGS(value), STRING_CONST(".00")))
            value.length-=3;
        else if (string_ends_with(STRING_ARGS(value), STRING_CONST(".000")))
            value.length-=4;
        else if (string_ends_with(STRING_ARGS(value), STRING_CONST(".0000")))
            value.length-=5;

        string_builder_append(sb, STRING_CONST("- "));
        string_builder_append(sb, string_camel_case_to_lowercase_phrase(STRING_ARGS(field_name)));
        string_builder_append(sb, STRING_CONST(": "));
        string_builder_append(sb, value);
        string_builder_append_new_line(sb);
    }
}

string_const_t openai_generate_summary_prompt(
    const char* symbol, size_t symbol_length,
    const char* user_prompt, size_t user_prompt_length)
{
    // Testing tool: https://platform.openai.com/playground/p/default-tldr-summary?model=text-davinci-003
  
    openai_prompt_t prompt{};
    prompt.symbol = string_clone(symbol, symbol_length);
    prompt.user_prompt = string_clone(user_prompt, user_prompt_length);

    // Get the stock fundamental data
    eod_fetch("fundamentals", prompt.symbol.str, FORMAT_JSON_CACHE, [&prompt](const json_object_t& json)
    {
        auto General = json["General"];
        auto Highlights = json["Highlights"];
        auto Valuation = json["Valuation"];
        auto Technicals = json["Technicals"];
        string_const_t description = General["Description"].as_string();
        const double dividend_yield = Highlights["DividendYield"].as_number(0) * 100.0;

        // Only keep the first sentences of the description.
        size_t num_sentences = 0;
        size_t description_length = description.length;
        for (size_t i = 0; i < description_length; ++i)
        {
            if (description.str[i] == '.')
            {
                ++num_sentences;
                if (num_sentences == 3)
                {
                    description_length = i+1;
                    break;
                }
            }
        }
        description.length = description_length;

        json_object_t quarters[] = 
        {
            json["Financials"]["Balance_Sheet"]["quarterly"].get(0ULL),
            json["Financials"]["Balance_Sheet"]["quarterly"].get(1ULL)
        };

        string_builder_t* sb = string_builder_allocate(4096);

        string_builder_append(sb, tr(STRING_CONST("Here's a company description, sector and industry:")));
        string_builder_append_new_line(sb);
        string_builder_append_new_line(sb);

        string_builder_append(sb, tr(STRING_CONST("> Sector: ")));
        string_builder_append(sb, General["Sector"].as_string());
        string_builder_append_new_line(sb);
        string_builder_append(sb, tr(STRING_CONST("> Industry: ")));
        string_builder_append(sb, General["Industry"].as_string());
        string_builder_append_new_line(sb);
        string_builder_append(sb, tr(STRING_CONST("> ")));
        string_builder_append(sb, description);
        string_builder_append_new_line(sb);

        string_builder_append_new_line(sb);
        string_builder_append(sb, tr(STRING_CONST("Please provide guidance from the company data below using these instructions:\n")));
        string_builder_append(sb, tr(STRING_CONST("- explain if the financial results in the last quarter are better than the previous one,\n")));
        string_builder_append(sb, tr(STRING_CONST("- compare these results to other companies in the same sector and industry and provide comparisons,\n")));
        string_builder_append(sb, tr(STRING_CONST("- state if this company could part of a paradigm market shift,\n")));
        string_builder_append(sb, tr(STRING_CONST("- raise any data point that could be of a concern for an investor,\n")));
        string_builder_append(sb, tr(STRING_CONST("- provide any prediction if possible or link to recent news or event affecting the stock price,\n")));
        string_builder_append(sb, tr(STRING_CONST("- and popularize as much as possible to reflecting the investor sentiment against that company.")));
        string_builder_append_new_line(sb);

        if (prompt.user_prompt.length)
        {
            string_builder_append_new_line(sb);
            string_builder_append(sb, tr(STRING_CONST("## Additional notes and comments to consider in the stock evaluation\n")));
            string_builder_append(sb, prompt.user_prompt);
            string_builder_append_new_line(sb);
        }

        string_builder_append_new_line(sb);
        string_builder_append(sb, tr(STRING_CONST("## Highlights")));
        string_builder_append_new_line(sb);
        openai_generate_json_object_prompt(sb, Highlights);
        string_builder_append_format(sb, "- dividend yield: %.3g", dividend_yield);
        string_builder_append_new_line(sb);

        #if 0
        string_builder_append_new_line(sb);
        string_builder_append(sb, STRING_CONST("## Valuation"));
        string_builder_append_new_line(sb);
        openai_generate_json_object_prompt(sb, Valuation);
        #endif

        string_builder_append_new_line(sb);
        string_builder_append(sb, tr(STRING_CONST("## Technicals")));
        string_builder_append_new_line(sb);
        openai_generate_json_object_prompt(sb, Technicals);

        string_builder_append_new_line(sb);
        string_builder_append(sb, tr(STRING_CONST("## Financials results of last two quarters")));
        string_builder_append_new_line(sb);

        for (const auto& q : quarters)
        {
            string_builder_append(sb, STRING_CONST("### "));
            string_builder_append(sb, q["date"].as_string());
            string_builder_append_new_line(sb);

            openai_generate_json_object_prompt(sb, q);
            string_builder_append_new_line(sb);
        }

        string_builder_append(sb, STRING_CONST("---\n"));
        string_builder_append(sb, '\0');

        string_const_t sbtext = string_builder_text(sb);
        prompt.text = string_clone(STRING_ARGS(sbtext));
        string_builder_deallocate(sb);

    }, 60 * 60ULL);

    array_push_memcpy(_openai_module->prompts, &prompt);
    return string_to_const(prompt.text);
}

const openai_response_t* openai_generate_news_sentiment(
    const char* symbol, size_t symbol_length,
    time_t news_date, const char* news_url, size_t news_url_length,
    const openai_completion_options_t& options)
{
    /*
    curl https://api.openai.com/v1/completions \
      -H "Content-Type: application/json" \
      -H "Authorization: Bearer $OPENAI_API_KEY" \
      -d '{
      "model": "text-davinci-003",
      "prompt": "...",
      "temperature": 0.7,
      "max_tokens": 3533,
      "top_p": 0.75,
      "frequency_penalty": 1.51,
      "presence_penalty": 0.7,
      "stop": ["---"]
    }'
    */

    // Allocate the response and provide a default notice.
    openai_response_t* response = (openai_response_t*)memory_allocate(HASH_OPENAI, sizeof(openai_response_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);

    response->options = options;
    response->dateref = news_date;
    response->symbol = string_clone(symbol, symbol_length);
    response->input = string_clone(news_url, news_url_length);

    // Keep track of the response
    array_push(_openai_module->responses, response);

    // Start a job to fetch the data
    job_execute([](void* context)
    {
        openai_response_t* response = (openai_response_t*)context;
        const openai_completion_options_t& options = response->options;

        // Get company name and stock price at the time of the news.
        stock_handle_t stock = stock_resolve(STRING_ARGS(response->symbol), FetchLevel::FUNDAMENTALS | FetchLevel::EOD);
        if (!stock)
        {
            response->success = false;
            response->output = string_clone(STRING_CONST("Failed to fetch stock info to summarize news"));
            return -1;
        }

        const day_result_t* ed = stock_get_EOD((const stock_t*)stock, response->dateref, true);
        if (!ed)
        {
            response->success = false;
            response->output = string_clone(STRING_CONST("Failed to fetch stock price data"));
            return -1;
        }

        string_const_t name = SYMBOL_CONST(stock->name);
        const double stock_price_change_that_day = ed->change_p;
        
        string_const_t fmttr = tr(STRING_CONST("Resume the following article %.*s ; "
            "explain why it is related to %.*s and share any sentiment regarding the price change of %.3g%% "
            "an investor could have regarding this event.\n\n---\n"), true);
        response->prompt = string_allocate_format(STRING_ARGS(fmttr), STRING_FORMAT(response->input), STRING_FORMAT(name), stock_price_change_that_day);

        config_handle_t data = config_allocate();
        config_set(data, "model", STRING_CONST("text-davinci-003"));
        config_set(data, "temperature", options.temperature);
        config_set(data, "max_tokens", (double)options.max_tokens);
        config_set(data, "top_p", options.top_p);
        config_set(data, "best_of", (double)options.best_of);
        config_set(data, "presence_penalty", options.presence_penalty);
        config_set(data, "frequency_penalty", options.frequency_penalty);
        config_set(data, "stop", STRING_CONST("---\n"));

        config_set(data, "prompt", STRING_ARGS(response->prompt));

        const char* query_url = openai_build_url("completions");
        openai_execute_query(query_url, data, [&response](const auto& res)
        {
            if (!res.resolved())
            {
                string_const_t error_message = res["error"]["message"].as_string();

                response->success = false;
                response->output = string_allocate_format(STRING_CONST("Failed to complete summary prompt: %.*s"), STRING_FORMAT(error_message));
                return;
            }

            string_const_t first_choice = res["choices"].get(0ULL)["text"].as_string();

            // Skip the first \\n if any
            if (string_starts_with(STRING_ARGS(first_choice), STRING_CONST("\\n")))
            {
                first_choice.str += 2;
                first_choice.length -= 2;
            }

            response->output = string_clone(STRING_ARGS(first_choice));

            // Replace all occurrences of \\n with \n
            response->output = string_replace(STRING_ARGS_CAPACITY(response->output), STRING_CONST("\\n"), STRING_CONST("\n"), true);

            // Replace all occurrences of \xe2\x80\x99 with '
            response->output = string_replace(STRING_ARGS_CAPACITY(response->output), STRING_CONST("\xe2\x80\x99"), STRING_CONST("'"), true);

            response->success = response->output.str && response->output.length;
        });

        config_deallocate(data);
        return response->success ? 0 : -1;
    }, (void*)response, JOB_DEALLOCATE_AFTER_EXECUTION);

    return response;
}

string_t* openai_generate_summary_sentiment(
    const char* symbol, size_t symbol_length, 
    const char* user_prompt, size_t user_prompt_length,
    const openai_completion_options_t& options)
{
    /*
    curl https://api.openai.com/v1/completions \
      -H "Content-Type: application/json" \
      -H "Authorization: Bearer $OPENAI_API_KEY" \
      -d '{
      "model": "text-davinci-003",
      "prompt": "...",
      "temperature": 0.7,
      "max_tokens": 2568,
      "top_p": 0.45,
      "best_of": 3,
      "frequency_penalty": 0.48,
      "presence_penalty": 1.56,
      "stop": ["---"]
    }'
    */

    // Allocate the response and provide a default notice.
    string_const_t loading_string = tr(STRING_CONST("Loading..."), true);
    string_t* response = (string_t*)memory_allocate(HASH_OPENAI, sizeof(string_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
    *response = string_clone(STRING_ARGS(loading_string));
    
    // Start a job to fetch the data
    job_execute([symbol, symbol_length, response, options, user_prompt, user_prompt_length](void* context)
    {
        const char* query_url = openai_build_url("completions");
        string_const_t prompt = openai_generate_summary_prompt(symbol, symbol_length, user_prompt, user_prompt_length);

        config_handle_t data = config_allocate();
        config_set(data, "model", STRING_CONST("text-davinci-003"));
        config_set(data, "temperature", options.temperature);
        config_set(data, "max_tokens", (double)options.max_tokens);
        config_set(data, "top_p", options.top_p);
        config_set(data, "best_of", (double)options.best_of);
        config_set(data, "presence_penalty", options.presence_penalty);
        config_set(data, "frequency_penalty", options.frequency_penalty);
        config_set(data, "stop", STRING_CONST("---\n"));

        config_set(data, "prompt", STRING_ARGS(prompt));

        if (!openai_execute_query(query_url, data, [&response](const auto& res)
        {
            char* previous_response = response->str;

            if (!res.resolved())
            {
                string_const_t error_message = res["error"]["message"].as_string();
                log_errorf(HASH_OPENAI, ERROR_EXCEPTION, STRING_CONST("Failed to complete summary prompt: %.*s"), STRING_FORMAT(error_message));

                *response = string_allocate_format(STRING_CONST("Failed to complete summary prompt: %.*s"), STRING_FORMAT(error_message));
                if (previous_response)
                    string_deallocate(previous_response);
                return;
            }

            string_const_t payload = res.to_string();
            log_debugf(HASH_OPENAI, STRING_CONST("Response: %.*s"), STRING_FORMAT(payload));

            string_const_t first_choice = res["choices"].get(0ULL)["text"].as_string();
            
            // Skip the first \\n if any
            if (string_starts_with(STRING_ARGS(first_choice), STRING_CONST("\\n")))
            {
                first_choice.str += 2;
                first_choice.length -= 2;
            }

            *response = string_clone(STRING_ARGS(first_choice));
            if (previous_response)
                string_deallocate(previous_response);

            // Replace all occurrences of \\n with \n
            *response = string_replace(STRING_ARGS_CAPACITY(*response), STRING_CONST("\\n"), STRING_CONST("\n"), true);

            // Replace all occurrences of \xe2\x80\x99 with '
            *response = string_replace(STRING_ARGS_CAPACITY(*response), STRING_CONST("\xe2\x80\x99"), STRING_CONST("'"), true);
        }))
        {
            log_errorf(HASH_OPENAI, ERROR_EXCEPTION, STRING_CONST("Failed to execute OpenAI query"));
        }

        config_deallocate(data);
        return nullptr;
    }, nullptr, JOB_DEALLOCATE_AFTER_EXECUTION);

    return response;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void openai_initialize()
{
    _openai_module = MEM_NEW(HASH_OPENAI, OPENAI_MODULE);
    openai_ensure_key_loaded();

    log_infof(HASH_OPENAI, STRING_CONST("OpenAI module initialized"));

    // Check connectivity to OpenAI
    openai_check_connectivity();

    #if BUILD_DEBUG
    app_register_menu(HASH_OPENAI, STRING_CONST("Modules/" ICON_MD_PSYCHOLOGY " OpenAI"), 
        STRING_CONST("Alt+F1"), AppMenuFlags::Append, openai_run_tests, nullptr);
    #endif
}

FOUNDATION_STATIC void openai_shutdown()
{
    // Deallocate generated prompts
    foreach(p, _openai_module->prompts)
    {
        string_deallocate(p->text.str);
        string_deallocate(p->symbol.str);
        string_deallocate(p->user_prompt.str);
    }
    array_deallocate(_openai_module->prompts);

    foreach(r, _openai_module->responses)
    {
        string_deallocate((*r)->symbol.str);
        string_deallocate((*r)->input.str);
        string_deallocate((*r)->prompt.str);
        string_deallocate((*r)->output.str);
        memory_deallocate(*r);
    }
    array_deallocate(_openai_module->responses);

    string_deallocate(_openai_module->apikey.str);
    string_deallocate(_openai_module->organization.str);
    string_array_deallocate(_openai_module->http_headers);
    MEM_DELETE(_openai_module);
}

DEFINE_MODULE(OPENAI, openai_initialize, openai_shutdown, MODULE_PRIORITY_UI_HEADLESS);
