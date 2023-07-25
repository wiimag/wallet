/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "openai.h"

#include "eod.h"
#include "stock.h"
#include "backend.h"

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
#include <framework/dispatcher.h>

#include <foundation/stream.h>
#include <foundation/environment.h>

#include <ctype.h>

#define HASH_OPENAI static_hash_string("openai", 6, 0x6ce8d96f30f6bd41ULL)

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
    bool                connected{ false };
    openai_prompt_t*    prompts{ nullptr };
    openai_response_t** responses{ nullptr };

} *_openai_module;

FOUNDATION_STATIC string_const_t openai_api_url()
{
    if (backend_is_connected())
        return backend_url();

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
    return query_execute_json(query, nullptr, callback);
}

FOUNDATION_STATIC bool openai_execute_query(const char* query, config_handle_t data, const query_callback_t& callback)
{
    return query_execute_json(query, nullptr, data, callback);
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

FOUNDATION_STATIC string_const_t openai_camel_case_to_lowercase_phrase(const char* expression, size_t expression_length)
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
        string_builder_append(sb, openai_camel_case_to_lowercase_phrase(STRING_ARGS(field_name)));
        string_builder_append(sb, STRING_CONST(": "));
        string_builder_append(sb, value);
        string_builder_append_new_line(sb);
    }
}

FOUNDATION_STATIC bool openai_backend_connected_event(const dispatcher_event_args_t& args)
{
    return (_openai_module->connected = true);
}

//
// # PUBLIC API
//

bool openai_available()
{
    if (_openai_module == nullptr)
        return false;
    return _openai_module->connected && backend_is_connected();
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

bool openai_complete_prompt(const char* prompt, size_t length, const openai_completion_options_t& options, const function<void(string_t)>& completed)
{
    struct openai_complete_prompt_args_t
    {
        string_t prompt;
        openai_completion_options_t options;
        function<void(string_t)> completed;
    };

    openai_complete_prompt_args_t* args = memory_allocate<openai_complete_prompt_args_t>(HASH_OPENAI);
    args->prompt = string_clone(prompt, length);
    args->options = options;
    args->completed = completed;
    
    // Start a job to fetch the data
    return job_execute([](void* context)
    {
        openai_complete_prompt_args_t* args = (openai_complete_prompt_args_t*)context;
        const char* query_url = openai_build_url("completions");

        config_handle_t data = config_allocate();
        config_set(data, "model", STRING_CONST("text-davinci-003"));
        config_set(data, "temperature", args->options.temperature);
        config_set(data, "max_tokens", args->options.max_tokens);
        config_set(data, "top_p", args->options.top_p);
        config_set(data, "best_of", args->options.best_of);
        config_set(data, "presence_penalty", args->options.presence_penalty);
        config_set(data, "frequency_penalty", args->options.frequency_penalty);
        config_set(data, "stop", STRING_CONST("---\n"));
        config_set(data, "prompt", STRING_ARGS(args->prompt));

        if (!openai_execute_query(query_url, data, [args](const auto& res)
        {
            if (!res.resolved())
            {
                string_const_t error_message = res["error"]["message"].as_string();
                log_errorf(HASH_OPENAI, ERROR_EXCEPTION, STRING_CONST("Failed to complete summary prompt: %.*s"), STRING_FORMAT(error_message));
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

            string_t response = string_clone(STRING_ARGS(first_choice));

            // Replace all occurrences of \\n with \n
            response = string_replace(STRING_ARGS_CAPACITY(response), STRING_CONST("\\n"), STRING_CONST("\n"), true);

            // Replace all occurrences of \xe2\x80\x99 with '
            response = string_replace(STRING_ARGS_CAPACITY(response), STRING_CONST("\xe2\x80\x99"), STRING_CONST("'"), true);

            args->completed(response);
        }))
        {
            log_errorf(HASH_OPENAI, ERROR_EXCEPTION, STRING_CONST("Failed to execute OpenAI query"));
        }

        config_deallocate(data);
        string_deallocate(args->prompt);
        memory_deallocate(args);
        return 0;
    }, args, JOB_DEALLOCATE_AFTER_EXECUTION) != nullptr;
}

string_t* openai_generate_summary_sentiment(
    const char* symbol, size_t symbol_length, 
    const char* user_prompt, size_t user_prompt_length,
    const openai_completion_options_t& options)
{
    // Allocate the response and provide a default notice.
    string_const_t loading_string = tr(STRING_CONST("Loading..."), true);
    string_t* response = (string_t*)memory_allocate(HASH_OPENAI, sizeof(string_t), 0, MEMORY_PERSISTENT | MEMORY_ZERO_INITIALIZED);
    *response = string_clone(STRING_ARGS(loading_string));
    
    // Start a job to fetch the data
    job_execute([symbol, symbol_length, response, options, user_prompt, user_prompt_length](void* context)
    {
        const char* query_url = openai_build_url("chat/completions");
        string_const_t prompt = openai_generate_summary_prompt(symbol, symbol_length, user_prompt, user_prompt_length);

        config_handle_t data = config_allocate(CONFIG_VALUE_OBJECT, CONFIG_OPTION_PRESERVE_INSERTION_ORDER);

        config_set(data, "model", STRING_CONST("gpt-4"));
        config_handle_t messages = config_set_array(data, "messages");
        config_handle_t system = config_array_push(messages, CONFIG_VALUE_OBJECT);
        config_set(system, "role", STRING_CONST("system"));
        config_set(system, "content", RTEXT("You are a financial stock enthusiat expert"));
        config_handle_t user = config_array_push(messages, CONFIG_VALUE_OBJECT);
        config_set(user, "role", STRING_CONST("user"));
        config_set(user, "content", STRING_ARGS(prompt));

        if (!openai_execute_query(query_url, data, [&response](const auto& res)
        {
            char* previous_response = response->str;

            if (!res.resolved())
            {
                string_const_t error_message = res["error"]["message"].as_string();
                if (error_message.length == 0)
                    error_message = string_to_const(res.buffer);
                log_errorf(HASH_OPENAI, ERROR_EXCEPTION, STRING_CONST("Failed to complete summary prompt: %.*s"), STRING_FORMAT(error_message));

                *response = string_allocate_format(STRING_CONST("Failed to complete summary prompt: %.*s"), STRING_FORMAT(error_message));
                if (previous_response)
                    string_deallocate(previous_response);
                return;
            }

            string_const_t payload = res.to_string();
            log_debugf(HASH_OPENAI, STRING_CONST("Response: %.*s"), STRING_FORMAT(payload));

            string_const_t first_choice = res["choices"].get(0ULL)["message"]["content"].as_string();
            
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
        return 0;
    }, nullptr, JOB_DEALLOCATE_AFTER_EXECUTION);

    return response;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void openai_initialize()
{
    _openai_module = MEM_NEW(HASH_OPENAI, OPENAI_MODULE);

    log_infof(HASH_OPENAI, STRING_CONST("OpenAI module initialized"));

    dispatcher_register_event_listener(EVENT_BACKEND_CONNECTED, openai_backend_connected_event);
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

    MEM_DELETE(_openai_module);
}

DEFINE_MODULE(OPENAI, openai_initialize, openai_shutdown, MODULE_PRIORITY_UI_HEADLESS);
