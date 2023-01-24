#include "eod.h"

#include "framework/common.h"
#include "framework/query.h"
#include "framework/config.h"
#include "framework/session.h"
#include "framework/scoped_string.h"

#include <foundation/fs.h>
#include <foundation/stream.h>
#include <foundation/hashtable.h>

// "https://eodhistoricaldata.com/api/exchange-symbol-list/TO?api_token=XYZ&fmt=json"

static char EOD_KEY[32] = { '\0' };

static const char* ensure_key_loaded()
{
	if (EOD_KEY[0] != '\0')
		return EOD_KEY;

	const string_const_t& eod_key_file_path = session_get_user_file_path(STRING_CONST("eod.key"));
	if (!fs_is_file(STRING_ARGS(eod_key_file_path)))
		return string_copy(STRING_CONST_CAPACITY(EOD_KEY), STRING_CONST("demo")).str;
	
	stream_t* key_stream = fs_open_file(STRING_ARGS(eod_key_file_path), STREAM_IN);
	if (key_stream == nullptr)
		return nullptr;

	scoped_string_t key = stream_read_string(key_stream);
	string_copy(EOD_KEY, sizeof(EOD_KEY), STRING_ARGS(key.value));
	stream_deallocate(key_stream);
	return EOD_KEY;
}

string_t eod_get_key()
{
	ensure_key_loaded();
	return string_t{ STRING_CONST_CAPACITY(EOD_KEY) };
}

bool eod_save_key(string_t eod_key)
{
	eod_key.length = string_length(eod_key.str);
	log_infof(0, STRING_CONST("Saving EOD %.*s"), STRING_FORMAT(eod_key));

	if (eod_key.str != EOD_KEY)
		string_copy(STRING_CONST_CAPACITY(EOD_KEY), STRING_ARGS(eod_key));

	const string_const_t& eod_key_file_path = session_get_user_file_path(STRING_CONST("eod.key"));
	stream_t* key_stream = fs_open_file(STRING_ARGS(eod_key_file_path), STREAM_CREATE | STREAM_OUT | STREAM_TRUNCATE);
	if (key_stream == nullptr)
		return false;

	log_infof(0, STRING_CONST("Writing key file %.*s"), STRING_FORMAT(eod_key_file_path));
	stream_write_string(key_stream, STRING_ARGS(eod_key));
	stream_deallocate(key_stream);
	return true;
}

string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format)
{
	return eod_build_url(api, ticker, format, nullptr, nullptr);
}

string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1)
{
	return eod_build_url(api, ticker, format, param1, value1, nullptr, nullptr);
}

string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2)
{
	string_t EOD_URL_BUFFER = string_static_buffer(2048);
	const char* api_key = ensure_key_loaded();

	string_const_t HOST_API = string_const(STRING_CONST("https://eodhistoricaldata.com/api/"));
	string_t eod_url = string_copy(EOD_URL_BUFFER.str, EOD_URL_BUFFER.length, STRING_ARGS(HOST_API));
	eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, api, string_length(api));
	eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("/"));

	if (ticker)
	{
		string_const_t escaped_ticker = url_encode(ticker);
		eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_ARGS(escaped_ticker));
	}
	eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("?api_token="));
	eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, api_key, string_length(api_key));

	if (format != FORMAT_UNDEFINED)
	{
		eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("&fmt="));
		if (format == FORMAT_JSON || format == FORMAT_JSON_CACHE)
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("json"));
		else
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("csv"));
	}

	if (param1 != nullptr)
	{
		eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("&"));
		eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, param1, string_length(param1));

		if (value1 != nullptr) 
		{
			string_const_t escaped_value1 = url_encode(value1);
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("="));
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_ARGS(escaped_value1));
		}

		if (param2 != nullptr)
		{
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("&"));
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, param2, string_length(param2));

			if (value2 != nullptr)
			{
				string_const_t escaped_value2 = url_encode(value2);
				eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("="));
				eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_ARGS(escaped_value2));
			}
		}
	}

	return string_const(STRING_ARGS(eod_url));
}

string_const_t eod_build_url(const char* api, const char* ticker, query_format_t format, size_t args_count, ...)
{
	va_list list;
	va_start(list, args_count);
	
	string_t EOD_URL_BUFFER = string_static_buffer(2048);
	const char* api_key = ensure_key_loaded();

	string_const_t HOST_API = string_const(STRING_CONST("https://eodhistoricaldata.com/api/"));
	string_t eod_url = string_copy(EOD_URL_BUFFER.str, EOD_URL_BUFFER.length, STRING_ARGS(HOST_API));
	eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, api, string_length(api));
	eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("/"));

	if (ticker)
	{
		string_const_t escaped_ticker = url_encode(ticker);
		eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_ARGS(escaped_ticker));
	}
	eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("?api_token="));
	eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, api_key, string_length(api_key));

	if (format != FORMAT_UNDEFINED)
	{
		eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("&fmt="));
		if (format == FORMAT_JSON || format == FORMAT_JSON_CACHE)
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("json"));
		else
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("csv"));
	}

	while (args_count--)
	{
		const char* param = va_arg(list, const char*);
		if (param != nullptr)
		{
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("&"));
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, param, string_length(param));
		}

		const char* value = va_arg(list, const char*);
		if (value != nullptr)
		{
			string_const_t escaped_value = url_encode(value);
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_CONST("="));
			eod_url = string_append(STRING_ARGS(eod_url), EOD_URL_BUFFER.length, STRING_ARGS(escaped_value));
		}
	}

	va_end(list);
	return string_const(STRING_ARGS(eod_url));
}

bool eod_fetch(const char* api, const char* ticker, query_format_t format, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 15ULL * 60ULL*/)
{
	return eod_fetch(api, ticker, format, nullptr, nullptr, nullptr, nullptr, json_callback, invalid_cache_query_after_seconds);
}

bool eod_fetch(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 15ULL * 60ULL*/)
{
	return eod_fetch(api, ticker, format, param1, value1, nullptr, nullptr, json_callback, invalid_cache_query_after_seconds);
}

bool eod_fetch(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2, const query_callback_t& json_callback, uint64_t invalid_cache_query_after_seconds /*= 15ULL * 60ULL*/)
{
	string_const_t url = eod_build_url(api, ticker, format, param1, value1, param2, value2);
	return query_execute_json(url.str, format, json_callback, invalid_cache_query_after_seconds);
}

bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const query_callback_t& json_callback, int ignore_if_queue_more_than /*= 0*/, uint64_t invalid_cache_query_after_seconds /*= 0*/)
{
	return eod_fetch_async(api, ticker, format, nullptr, nullptr, nullptr, nullptr, json_callback, ignore_if_queue_more_than, invalid_cache_query_after_seconds);
}

bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const query_callback_t& json_callback, int ignore_if_queue_more_than /*= 0*/, uint64_t invalid_cache_query_after_seconds /*= 0*/)
{
	return eod_fetch_async(api, ticker, format, param1, value1, nullptr, nullptr, json_callback, ignore_if_queue_more_than, invalid_cache_query_after_seconds);
}

bool eod_fetch_async(const char* api, const char* ticker, query_format_t format, const char* param1, const char* value1, const char* param2, const char* value2, const query_callback_t& json_callback, int ignore_if_queue_more_than /*= 0*/, uint64_t invalid_cache_query_after_seconds /*= 0*/)
{
	string_const_t url = eod_build_url(api, ticker, format, param1, value1, param2, value2);
	log_debugf(0, STRING_CONST("Built query %.*s"), STRING_FORMAT(url));
	return query_execute_async_json(url.str, format, json_callback, ignore_if_queue_more_than, invalid_cache_query_after_seconds);
}
