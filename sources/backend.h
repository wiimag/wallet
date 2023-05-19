/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */

#pragma once

#include <framework/query.h>
#include <framework/function.h>

#include <foundation/string.h>

/*! Event propagated when the backend connection is established. */
constexpr const char EVENT_BACKEND_CONNECTED[] = "BACKEND_CONNECTED";

#ifndef BUILD_BACKEND
#  define BUILD_BACKEND 0
#endif

bool backend_is_connected();

string_const_t backend_url();

bool backend_execute_news_search_query(const char* symbol, size_t symbol_length, const query_callback_t& callback);

void backend_check_new_version(bool use_notif = false);

string_t backend_translate_text(const char* id, size_t id_length, const char* text, size_t text_length, const char* lang, size_t lang_length);

bool backend_open_url(const char* url, size_t url_length, ...);
