/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */
 
#pragma once

#include <framework/query.h>

/*! @brief Load and return the API EOD key.
 *  @return Returns the initialized api key.
 */
string_t eod_get_key();

/*! Checks if we are connected to the EOD service. */
bool eod_connected();

/*! Checks if the EOD usage is at capacity */
bool eod_is_at_capacity();

/*! Returns the EOD usage capacity [0..1] */
double eod_capacity();

/*! Check if the EOD service is available and usable. (i.e. connected at not at capacity) */
bool eod_availalble();

/*! Force update the EOD connectivity state */
void eod_update();

/*! @brief Save the API EOD key.
 *  @param eod_key The key to save.
 *  @return Returns true if the key was saved.
 */
bool eod_save_key(string_t eod_key);

/*! @brief Build the EOD image url.
 *  @param image_url The image url.
 *  @return Returns the image url.
 */
const char* eod_build_image_url(const char* image_url, size_t image_url_length);

/*! @brief Build an EOD url.
 *  @param api The api to query.
 *  @param format The format of the query.
 *  @param uri_format The uri format.
 *  @param ... The uri format arguments.
 *  @return Returns the url.
 */
const char* eod_build_url(const char* api, query_format_t format, const char* uri_format, ...);

/*! @brief Build an EOD url.
 *  @param api The api to query.
 *  @param ticker The ticker to query.
 *  @param format The format of the query.
 *  @return Returns the url.
 */
string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format);

/*! @brief Build an EOD url.
 *  @param api The api to query.
 *  @param ticker The ticker to query.
 *  @param format The format of the query.
 *  @param param1 The first parameter.
 *  @param value1 The first value.
 *  @return Returns the url.
 */
string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1);

/*! @brief Build an EOD url.
 *  @param api The api to query.
 *  @param ticker The ticker to query.
 *  @param format The format of the query.
 *  @param param1 The first parameter.
 *  @param value1 The first value.
 *  @param param2 The second parameter.
 *  @param value2 The second value.
 *  @return Returns the url.
 */
string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2);

/*! @brief Fetch the EOD data.
 *  @param api The api to query.
 *  @param ticker The ticker to query.
 *  @param format The format of the query.
 *  @param json_callback The callback to call when the data is ready.
 *  @param invalid_cache_query_after_seconds The number of seconds after which the cache is considered invalid.
 *  @return Returns true if the query was successful.
 */
bool eod_fetch(const char* api, const char* ticker, query_format_t format, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);

/*! @brief Fetch the EOD data.
 *  @param api The api to query.
 *  @param ticker The ticker to query.
 *  @param format The format of the query.
 *  @param param1 The first parameter.
 *  @param value1 The first value.
 *  @param json_callback The callback to call when the data is ready.
 *  @param invalid_cache_query_after_seconds The number of seconds after which the cache is considered invalid.
 *  @return Returns true if the query was successful.
 */
bool eod_fetch(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);

/*! @brief Fetch the EOD data.
 *  @param api The api to query.
 *  @param ticker The ticker to query.
 *  @param format The format of the query.
 *  @param param1 The first parameter.
 *  @param value1 The first value.
 *  @param param2 The second parameter.
 *  @param value2 The second value.
 *  @param json_callback The callback to call when the data is ready.
 *  @param invalid_cache_query_after_seconds The number of seconds after which the cache is considered invalid.
 *  @return Returns true if the query was successful.
 */
bool eod_fetch(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);

/*! @brief Fetch the EOD data asynchronously.
 *  @param api The api to query.
 *  @param ticker The ticker to query.
 *  @param format The format of the query.
 *  @param json_callback The callback to call when the data is ready.
 *  @param invalid_cache_query_after_seconds The number of seconds after which the cache is considered invalid.
 *  @return Returns true if the query was successful.
 */
bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);

/*! @brief Fetch the EOD data asynchronously.
 *  @param api The api to query.
 *  @param ticker The ticker to query.
 *  @param format The format of the query.
 *  @param param1 The first parameter.
 *  @param value1 The first value.
 *  @param json_callback The callback to call when the data is ready.
 *  @param invalid_cache_query_after_seconds The number of seconds after which the cache is considered invalid.
 *  @return Returns true if the query was successful.
 */
bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);

/*! @brief Fetch the EOD data asynchronously.
 *  @param api The api to query.
 *  @param ticker The ticker to query.
 *  @param format The format of the query.
 *  @param param1 The first parameter.
 *  @param value1 The first value.
 *  @param param2 The second parameter.
 *  @param value2 The second value.
 *  @param json_callback The callback to call when the data is ready.
 *  @param invalid_cache_query_after_seconds The number of seconds after which the cache is considered invalid.
 *  @return Returns true if the query was successful.
 */
bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds = 15ULL * 60ULL);

/*! Returns the EOD API URL buffer to write too.
 * 
 *  @remark Use #eod_api_url_buffer_capacity to determine the size of the buffer.

 *  @return Returns the EOD API URL buffer to write too.
 */
char* eod_api_url_buffer();

/*! Returns the EOD API URL buffer capacity.
 * 
 *  @return Returns the EOD API URL buffer capacity.
 */
size_t eod_api_url_buffer_capacity();

/*! Returns the EOD Web Site URL. */
string_const_t eod_web_site_url();

 /*! Save the EOD API URL.
  *
  *  @param url The url to save. The url must be null terminated.
  *  @return Returns the saved url.
  */  
const char* eod_save_api_url(const char* url);
