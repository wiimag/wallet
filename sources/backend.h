/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/query.h>
#include <framework/function.h>

#include <foundation/string.h>

bool backend_is_connected();

string_const_t backend_url();

string_t backend_google_search_api_key();

string_t backend_set_google_search_api_key(const char* apikey);

bool backend_execute_news_search_query(const char* symbol, size_t symbol_length, const query_callback_t& callback);
