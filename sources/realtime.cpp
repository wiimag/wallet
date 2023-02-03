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
} *REALTIME;

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

    shared_mutex& mutex = REALTIME->stocks_mutex;
    
    if (!mutex.shared_lock())
        return false;
     
    int fidx = array_binary_search(REALTIME->stocks, array_size(REALTIME->stocks), key);
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

    fidx = ~fidx;
    array_insert_memcpy(REALTIME->stocks, fidx, &stock);
    return mutex.exclusive_unlock();
}

FOUNDATION_STATIC void realtime_fetch_query_data(const json_object_t& res)
{
    if (res.error_code > 0)
        return;

    shared_mutex& mutex = REALTIME->stocks_mutex;
    
    for (auto e : res)
    {
        string_const_t code = e["code"].as_string();
        const hash_t key = hash(STRING_ARGS(code));

        stock_realtime_record_t r;
        r.price = e["close"].as_number();
        r.timestamp = (time_t)e["timestamp"].as_number();
        
        if (!mutex.shared_lock())
            continue;

        int fidx = array_binary_search(REALTIME->stocks, array_size(REALTIME->stocks), key);
        if (fidx >= 0)
        {
            stock_realtime_t& stock = REALTIME->stocks[fidx];
            stock.price = r.price;
            stock.timestamp = r.timestamp;

            // Is that safe enough?
            array_push_memcpy(stock.records, &r);
        }
        mutex.shared_unlock();
    }
}

FOUNDATION_STATIC void* realtime_background_thread_fn(void*)
{
    unsigned int wait_time = 1;
    static string_const_t* codes = nullptr;
    while (!thread_try_wait(wait_time))
    {
        shared_mutex& mutex = REALTIME->stocks_mutex;
        if (mutex.shared_lock())
        {
            for (size_t i = 0, end = array_size(REALTIME->stocks); i < end; ++i)
            {
                const stock_realtime_t& stock = REALTIME->stocks[i];
                array_push(codes, string_const(stock.code, string_length(stock.code)));
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
                    if (!query_execute_json(url.str, FORMAT_JSON_WITH_ERROR, realtime_fetch_query_data))
                        break;

                    if (thread_try_wait(5000))
                        goto realtime_background_thread_fn_quit;
                }
            }

            array_clear(codes);
            wait_time = 5000;
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
    REALTIME = MEM_NEW(HASH_REALTIME, REALTIME_MODULE);

    // Open realtime stock stream.
    string_const_t realtime_stream_path = session_get_user_file_path(STRING_CONST("realtime"), nullptr, 0, STRING_CONST("stream"));
    REALTIME->stream = fs_open_file(STRING_ARGS(realtime_stream_path), STREAM_CREATE | STREAM_IN | STREAM_OUT | STREAM_BINARY);
    if (REALTIME->stream == nullptr)
    {
        log_panic(HASH_REALTIME, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to open realtime stream"));
        return;
    }

    // Create thread to query realtime stock
    REALTIME->background_thread = thread_allocate(realtime_background_thread_fn, nullptr, STRING_CONST("realtime"), THREAD_PRIORITY_NORMAL, 0);
    if (REALTIME->background_thread == nullptr || !thread_start(REALTIME->background_thread))
    {
        log_panic(HASH_REALTIME, ERROR_SYSTEM_CALL_FAIL, STRING_CONST("Failed to create realtime background thread"));
        return;
    }

    dispatcher_register_event_listener(EVENT_STOCK_REQUESTED, realtime_register_new_stock);
}

FOUNDATION_STATIC void realtime_shutdown()
{   
    stream_deallocate(REALTIME->stream);
    
    thread_signal(REALTIME->background_thread);
    thread_deallocate(REALTIME->background_thread);

    foreach(s, REALTIME->stocks)
        array_deallocate(s->records);
    array_deallocate(REALTIME->stocks);

    MEM_DELETE(REALTIME);
}

DEFINE_SERVICE(REALTIME, realtime_initialize, realtime_shutdown, SERVICE_PRIORITY_REALTIME);
