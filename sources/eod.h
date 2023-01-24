#pragma once

#include "framework/query.h"

string_t eod_get_key();
bool eod_save_key(string_t eod_key);

string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format);
string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1);
string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2);
string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format, size_t args_count, ...);

bool eod_fetch(const char* api, const char* ticker, query_format_t format, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);
bool eod_fetch(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);
bool eod_fetch(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);

bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const query_callback_t& json_callback, int ignore_if_queue_more_than = 0, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);
bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const query_callback_t& json_callback, int ignore_if_queue_more_than = 0, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);
bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2, const query_callback_t& json_callback, int ignore_if_queue_more_than = 0, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);
