/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * TODO: Rename module to http
 */

#pragma once

#include "function.h"
#include "query_json.h"

#include <foundation/hash.h>

#if !defined(ENABLE_QUERY_MOCKING)
#if BUILD_TESTS
#define ENABLE_QUERY_MOCKING (1)
#else
#define ENABLE_QUERY_MOCKING (0)
#endif
#endif

#define HASH_QUERY static_hash_string("http", 4, 0xbcccd6bcde9fa872ULL)

struct config_handle_t;

/// <summary>
/// Callback handler used when the query are executed.
/// </summary>
typedef function<void(const json_object_t& data)> query_callback_t;

typedef enum {
    FORMAT_UNDEFINED = -1,
    FORMAT_JSON = 0,
    FORMAT_CSV = 1,
    FORMAT_JSON_CACHE = 2,
    FORMAT_JSON_WITH_ERROR = 3,
    FORMAT_IN_FILE_OUT_JSON = 4,
} query_format_t;

/// <summary>
/// Initialize the query system.
/// Must be called once and early.
/// </summary>
void query_initialize();

/// <summary>
/// Shutdown the query system.
/// Must be called once and before ending main
/// </summary>
void query_shutdown();

/// <summary>
/// Execute a query and retrieve the JSON response. 
/// The user code is called back in the main thread.
/// </summary>
/// <param name="query">GET URL</param>
/// <param name="callback"></param>
/// <returns></returns>
bool query_execute_json(const char* query, query_format_t format, const query_callback_t& callback, uint64_t invalid_cache_query_after_seconds = 0);

/// <summary>
/// 
/// </summary>
/// <param name="query"></param>
/// <param name="format"></param>
/// <param name="body"></param>
/// <param name="callback"></param>
/// <param name="invalid_cache_query_after_seconds"></param>
/// <returns></returns>
bool query_execute_json(const char* query, query_format_t format, string_t body, const query_callback_t& callback, uint64_t invalid_cache_query_after_seconds = 0);

/// <summary>
/// 
/// </summary>
/// <param name="query"></param>
/// <param name="format"></param>
/// <param name="file_path"></param>
/// <param name="callback"></param>
/// <returns></returns>
bool query_execute_send_file(const char* query, query_format_t format, string_t file_path, const query_callback_t& callback);

/// <summary>
/// Executes an HTTP query and calls back the user code with the raw JSON data.
/// </summary>
/// <param name="query">GET URL</param>
/// <param name="callback"></param>
/// <returns></returns>
bool query_execute_json(const char* query, query_format_t format, void(*callback)(const char* json, const json_token_t* tokens), uint64_t invalid_cache_query_after_seconds = 0);

/*! Execute a query and retrieve the JSON response. 
 *  The user code is called back in the main thread.
 * 
 *  @param[in] query        GET URL
 *  @param[in] callback     Callback executed when the server returns the response.
 *  @param[in] headers      Headers to send with the query.
 *  @returns                True if the query was executed, false otherwise.
 */
bool query_execute_json(const char* query, string_t* headers, const query_callback_t& callback);

/*! Execute a query and retrieve the JSON response. 
 *  The user code is called back in the main thread.
 * 
 *  @param[in] query        GET URL
 *  @param[in] headers      Headers to send with the query.
 *  @param[in] data         Data to send with the query.
 *                          The config data gets deallocated by the query system.
 *  @param[in] callback     Callback executed when the server returns the response.
 * 
 *  @returns                True if the query was executed, false otherwise.
 */
bool query_execute_json(const char* query, string_t* headers, config_handle_t data, const query_callback_t& callback);

/// <summary>
/// Queues an HTTP query. The query is executed in another thread and 
/// the user callback is resolved in that thread later on.
/// </summary>
/// <param name="query">GET URL</param>
/// <param name="callback">Callback executed when the server returns the response.</param>
/// /// <param name="invalid_cache_query_after_seconds">If @FORMAT_JSON_CACHE is used, then the query cache will be ignored if older than @invalid_cache_query_after_seconds.</param>
/// <returns></returns>
bool query_execute_async_json(const char* query, query_format_t format, const query_callback_t& callback, uint64_t invalid_cache_query_after_seconds = 0);

/// <summary>
/// 
/// </summary>
/// <param name="query"></param>
/// <param name="post_data"></param>
/// <param name="callback"></param>
/// <returns></returns>
bool query_execute_async_json(const char* query, const config_handle_t& post_data, const query_callback_t& callback);

/// <summary>
/// POST a query to the following URL.
/// </summary>
/// <param name="url">POST URL</param>
/// <param name="json">JSON payload (Request body)</param>
/// <param name="callback">Callback executed when the server returns the response.</param>
/// <returns></returns>
bool query_post_json(const char* url, const config_handle_t& json, const query_callback_t& callback);

/// <summary>
/// 
/// </summary>
/// <param name="url"></param>
/// <param name="file_path"></param>
/// <param name="callback"></param>
/// <returns></returns>
bool query_execute_async_send_file(const char* query, string_t file_path, const query_callback_t& callback);

/// <summary>
/// 
/// </summary>
/// <param name="query"></param>
/// <returns></returns>
stream_t* query_execute_download_file(const char* query);

#if ENABLE_QUERY_MOCKING

void query_mock_initialize();

void query_mock_shutdown();

void query_mock_register_request_response(
    const char* query, size_t query_size, 
    const char* response, size_t response_length, 
    query_format_t format = FORMAT_UNDEFINED);

bool query_mock_is_enabled(const char* query, bool* mock_success, string_t* result);

#endif
