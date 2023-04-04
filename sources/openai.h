/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * This module uses OpenAI to make predictions about the stock market.
 */

#pragma once

#include <foundation/string.h>

struct openai_completion_options_t
{
    int best_of = 3;
    int max_tokens = 2500;
    float temperature = 0.7f;
    float top_p = 0.9f;
    float presence_penalty = 1.56f;
    float frequency_penalty = 0.48f;
};

struct openai_response_t
{
    openai_completion_options_t options;

    string_t symbol{};
    string_t input{};
    string_t prompt{};
    string_t output{};

    time_t dateref{ 0 };
    bool success{ false };
};

/*! Checks if the OpenAI service is available.
 * 
 *  @return True if the OpenAI service is available, false otherwise.
 */
bool openai_available();

/*! Get the OpenAI API key from the environment variable OPENAI_API_KEY. */
string_t openai_get_api_key();

/*! Get the capacity of the OpenAI API key. */
size_t openai_get_api_key_capacity();

/*! Get the OpenAI organization from the environment variable OPENAI_ORGANIZATION. */
string_t openai_get_organization();

/*! Get the capacity of the OpenAI organization. */
size_t openai_get_organization_capacity();

/*! Set the OpenAI API key. 
 * 
 *  This will save the key to a file in the user's home directory.
 * 
 *  @param[in] key              The API key to set.
 *  @param[opt] organization    The organization to set.
 */
void openai_set_api_key(const char* key, const char* organization = nullptr);

/*! Set the OpenAI organization. 
 * 
 *  This will save the organization to a file in the user's home directory.
 * 
 *  @param[in] organization     The organization to set.
 */
void openai_set_organization(const char* organization);

/*! Generate a summary prompt to ask OpenAI to summarize stock symbol financial results.
 * 
 *  @param[in] symbol           The symbol to generate a summary for.
 *  @param[in] symbol_length    The length of the symbol.
 *  @param[in] user_prompt      The user prompt to use when generating the summary.
 *  @param[in] user_prompt_length The length of the user prompt.
 * 
 *  @return The text summary.
 */
string_const_t openai_generate_summary_prompt(
    const char* symbol, size_t symbol_length,
    const char* user_prompt, size_t user_prompt_length);

/*! Generate a summary prompt to ask OpenAI to summarize stock symbol financial results.
 * 
 *  @param[in] symbol           The symbol to generate a summary for.
 *  @param[in] symbol_length    The length of the symbol.
 * 
 *  @return The text summary.
 */
FOUNDATION_FORCEINLINE string_const_t openai_generate_summary_prompt(const char* symbol, size_t symbol_length)
{
    return openai_generate_summary_prompt(symbol, symbol_length, nullptr, 0);
}

/*! Generate a summary of stock symbol financial results.
 * 
 *  @param[in] symbol           The symbol to generate a summary for.
 *  @param[in] symbol_length    The length of the symbol.
 *  @param[in] user_prompt      The user prompt to use when generating the summary.
 *  @param[in] user_prompt_length The length of the user prompt.
 *  @param[in] options          The options to use when generating the summary.
 * 
 *  @return The text summary.
 */
string_t* openai_generate_summary_sentiment(
    const char* symbol, size_t symbol_length,
    const char* user_prompt, size_t user_prompt_length,
    const openai_completion_options_t& options);

/*! Generate a summary of stock symbol financial results.
 * 
 *  @param[in] symbol           The symbol to generate a summary for.
 *  @param[in] symbol_length    The length of the symbol.
 * 
 *  @return The text summary.
 */
FOUNDATION_FORCEINLINE string_t* openai_generate_summary_sentiment(const char* symbol, size_t symbol_length)
{
    static openai_completion_options_t defaults{};
    return openai_generate_summary_sentiment(symbol, symbol_length, nullptr, 0, defaults);
}

const openai_response_t* openai_generate_news_sentiment(
    const char* symbol, size_t symbol_length, 
    time_t news_date, const char* news_url, size_t news_url_length,
    const openai_completion_options_t& options);