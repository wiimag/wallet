/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
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

typedef enum BackendLogType : uint32_t {

    BACKEND_LOG_INFO = 0,
    BACKEND_LOG_WARNING,
    BACKEND_LOG_ERROR,
    BACKEND_LOG_DEBUG,
    BACKEND_LOG_FATAL

} backend_log_type_t;

bool backend_is_connected();

string_const_t backend_url();

bool backend_execute_news_search_query(const char* symbol, size_t symbol_length, const query_callback_t& callback);

void backend_check_new_version(bool use_notif = false, bool ignore_if_latest = false);

string_t backend_translate_text(const char* id, size_t id_length, const char* text, size_t text_length, const char* lang, size_t lang_length);

bool backend_open_url(const char* url, size_t url_length, ...);

bool backend_log(backend_log_type_t type, const char* msg, size_t msg_length, const char* tag = nullptr, size_t tag_length = 0);

bool backend_log(const char* type, size_t type_length, const char* info = nullptr, size_t info_length = 0, const char* tag = nullptr, size_t tag_length = 0, const char* _user = nullptr, size_t user_length = 0);

bool backend_fetch_sync(query_format_t format, const query_callback_t& json_callback, const char* uri, size_t length, ...);

bool backend_fetch_async(query_format_t format, const query_callback_t& json_callback, const char* uri, size_t length, ...);
