/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/string.h>

void news_open_window(const char* symbol, size_t symbol_length);

string_t news_google_search_api_key();

string_t news_set_google_search_api_key(const char* apikey);
