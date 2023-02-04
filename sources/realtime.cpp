/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#include "realtime.h"

#include "eod.h"
#include "stock.h"
#include "events.h"

#include <framework/session.h>
#include <framework/service.h>
#include <framework/dispatcher.h>
#include <framework/database.h>
#include <framework/query.h>

#include <foundation/fs.h>
#include <foundation/stream.h>
#include <foundation/thread.h>

#include <algorithm>

#define HASH_REALTIME static_hash_string("realtime", 8, 0x29e09dfa4716c805ULL)

struct stock_realtime_record_t
{
    time_t timestamp;
    double price;
};

struct stock_realtime_t
{
    hash_t key;
    char   code[16];
    time_t timestamp;
    double price;

    stock_realtime_record_t* records{ nullptr };
};

static struct REALTIME_MODULE {
    stream_t* stream{ nullptr };
    thread_t* background_thread{ nullptr };

    shared_mutex      stocks_mutex;
    stock_realtime_t* stocks{ nullptr };
} *_realtime;

//
// # PRIVATE
//

FOUNDATION_FORCEINLINE bool operator<(const stock_realtime_t& s, const hash_t& key)
{
    return s.key < key;
}

FOUNDATION_FORCEINLINE bool operator>(const stock_realtime_t& s, const hash_t& key)
{
    return s.key > key;
}

FOUNDATION_STATIC bool realtime_register_new_stock(const dispatcher_event_args_t& args)
{
    FOUNDATION_ASSERT(args.size <= 16);
    string_const_t code { (const char*)args.data, args.size };
    const hash_t key = hash(code.str, code.length);

    shared_mutex& mutex = _realtime->stocks_mutex;
    
    if (!mutex.shared_lock())
        return false;
     
    int fidx = array_binary_search(_realtime->stocks, array_size(_realtime->stocks), key);
    if (!mutex.shared_unlock() || fidx >= 0)
        return false;
    
    stock_realtime_t stock;
    string_copy(STRING_CONST_CAPACITY(stock.code), code.str, code.length);
    stock.key = key;
    stock.timestamp = 0;
    stock.price = DNAN;
    stock.records = nullptr;
    
    if (!mutex.exclusive_lock())
        return false;

    log_infof(HASH_REALTIME, STRING_CONST("Registering new realtime stock %.*s"), STRING_FORMAT(code));

    fidx = ~fidx;
    array_insert_memcpy(_realtime->stocks, fidx, &stock);
    return mutex.exclusive_unlock();
}

FOUNDATION_STATIC void realtime_fetch_query_data(const json_object_t& res)
{
    if (res.error_code > 0)
        return;

    shared_mutex& mutex = _realtime->stocks_mutex;
    
    for (auto e : res)
    {
        string_const_t code = e["code"].as_string();
        const hash_t key = hash(STRING_ARGS(code));

        stock_realtime_record_t r;
        r.price = e["close"].as_number();
        r.timestamp = (time_t)e["timestamp"].as_number();
        
        if (!mutex.shared_lock())
            continue;

        int fidx = array_binary_search(_realtime->stocks, array_size(_realtime->stocks), key);
        if (fidx >= 0)
        {
            stock_realtime_t& stock = _realtime->stocks[fidx];

            if (r.timestamp > stock.timestamp)
            {
                stock.price = r.price;
                stock.timestamp = r.timestamp;

                log_infof(HASH_REALTIME, STRING_CONST("Streaming new realtime values %lld > %.*s > %lf (%llu)"), 
                    r.timestamp, STRING_FORMAT(code), r.price, stream_size(_realtime->stream));
                
                // Is that safe enough?
                array_push_memcpy(stock.records, &r);

                stream_write(_realtime->stream, &r.timestamp, sizeof(r.timestamp));
                stream_write(_realtime->stream, stock.code, sizeof(stock.code));
                stream_write(_realtime->stream, &r.price, sizeof(r.price));
                stream_flush(_realtime->stream);
            }
        }
        mutex.shared_unlock();
    }
}

FOUNDATION_STATIC void* realtime_background_thread_fn(void*)
{
    shared_mutex& mutex = _realtime->stocks_mutex;
    
    while (!stream_eos(_realtime->stream))
    {
        char code[16];
        size_t code_length;
        stock_realtime_record_t r;

        stream_read(_realtime->stream, &r.timestamp, sizeof(r.timestamp));
        stream_read(_realtime->stream, code, sizeof(code));
        stream_read(_realtime->stream, &r.price, sizeof(r.price));

        code_length = string_length(code);
        const hash_t key = hash(code, code_length);

        SHARED_WRITE_LOCK(mutex);
        int fidx = array_binary_search(_realtime->stocks, array_size(_realtime->stocks), key);
        if (fidx < 0)
        {
            stock_realtime_t stock;
            string_copy(STRING_CONST_CAPACITY(stock.code), code, code_length);
            stock.key = key;
            stock.timestamp = r.timestamp;
            stock.price = r.price;
            stock.records = nullptr;
            array_push_memcpy(stock.records, &r);

            fidx = ~fidx;
            array_insert_memcpy(_realtime->stocks, fidx, &stock);
        }
        else
        {
            stock_realtime_t& stock = _realtime->stocks[fidx];

            if (r.timestamp > stock.timestamp)
            {
                stock.timestamp = r.timestamp;
                stock.price = r.price;
            }
            array_push_memcpy(stock.records, &r);
        }
    }

    unsigned int wait_time = 1;
    static string_const_t* codes = nullptr;
    while (!thread_try_wait(wait_time))
    {
        time_t oldest = INT64_MAX;
        if (mutex.shared_lock())
        {
            time_t now = time_now();
            for (size_t i = 0, end = array_size(_realtime->stocks); i < end; ++i)
            {
                const stock_realtime_t& stock = _realtime->stocks[i];
                const double minutes = time_elapsed_days(stock.timestamp, now) * 24.0 * 60.0;
                if (minutes > 5)
                    array_push(codes, string_const(stock.code, string_length(stock.code)));

                if (stock.timestamp != 0 && stock.timestamp < oldest)
                    oldest = stock.timestamp;
            }
            mutex.shared_unlock();

            size_t batch_size = 0;
            string_const_t batch[16];
            for (size_t i = 0, end = array_size(codes); i < end; ++i)
            {
                batch[batch_size++] = codes[i];
                if (batch_size == ARRAY_COUNT(batch) || i == end - 1)
                {
                    range_view<string_const_t> view = { &batch[0], batch_size };
                    string_const_t code_list = string_join(view.begin(), view.end(), [](const auto& s) { return s; }, CTEXT(","));
                    
                    // Send batch
                    string_const_t url = eod_build_url("real-time", batch[0].str, FORMAT_JSON, "s", code_list.str);
                    log_infof(HASH_REALTIME, STRING_CONST("Fetching realtime stock data for %.*s"), STRING_FORMAT(code_list));
                    if (!query_execute_json(url.str, FORMAT_JSON_WITH_ERROR, realtime_fetch_query_data))
                        break;

                    if (thread_try_wait(5000))
                        goto realtime_background_thread_fn_quit;

                    batch_size = 0;
                }
            }

            array_clear(codes);
            if (oldest != INT64_MAX)
            {
                double wait_minutes = time_elapsed_days(oldest, now) * 24.0 * 60.0;
                wait_minutes = max(0.0, 5.0 - wait_minutes);
                wait_time = max(60000U, to_uint(math_trunc(wait_minutes * 60.0 * 1000.0)));
            }
            else
            {
                wait_time = 5000;
            }
        }
    }
    
realtime_background_thread_fn_quit:
    array_deallocate(codes);    
    return 0;
}

//
// # SYSTEM
//

FOUNDATION_STATIC void realtime_initialize()
{
    _realtime = MEM_NEW(HASH_REALTIME, REALTIME_MODULE);

    // Open realtime stock stream.
    string_const_t realtime_stream_path = session_get_user_file_path(STRING_CONST("realtime"), nullptr, 0, STRING_CONST("stream"));
    _realtime->stream = fs_open_file(STRING_ARGS(realtime_stream_path), STREAM_CREATE | STREAM_IN | STREAM_OUT | STREAM_BINARY);
    if (_realtime->stream == nullptr)
    {
        log_panic(HASH_REALTIME, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to open realtime stream"));
        return;
    }

    // Create thread to query realtime stock
    _realtime->background_thread = thread_allocate(realtime_background_thread_fn, nullptr, STRING_CONST("realtime"), THREAD_PRIORITY_NORMAL, 0);
    if (_realtime->background_thread == nullptr || !thread_start(_realtime->background_thread))
    {
        log_panic(HASH_REALTIME, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to create realtime background thread"));
        return;
    }

    dispatcher_register_event_listener(EVENT_STOCK_REQUESTED, realtime_register_new_stock);
}

FOUNDATION_STATIC void realtime_shutdown()
{   
    stream_deallocate(_realtime->stream);
    
    thread_signal(_realtime->background_thread);
    thread_deallocate(_realtime->background_thread);

    foreach(s, _realtime->stocks)
        array_deallocate(s->records);
    array_deallocate(_realtime->stocks);

    MEM_DELETE(_realtime);
}

DEFINE_SERVICE(REALTIME, realtime_initialize, realtime_shutdown, SERVICE_PRIORITY_REALTIME);
