/*
 * Copyright 2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "imcache.h"

#include <framework/module.h>
#include <framework/database.h>
#include <framework/string_table.h>
#include <framework/dispatcher.h>

#define HASH_IMCACHE static_hash_string("imcache", 7, 0xa6f67d96ae77631bULL)

typedef enum class ImCacheValueType
{
    Null = 0,
    Bool = 1 << 0,
    Single = 1 << 1,
    Double = 1 << 2,
    Integer = 1 << 3,
    Unsigned = 1 << 4,
    Int64 = 1 << 5,
    UInt64 = 1 << 6,
    Symbol = 1 << 7,
    
} imgui_cache_value_type_t;
DEFINE_ENUM_FLAGS(ImCacheValueType);

struct im_cache_entry_t
{
    hash_t key{ 0 };
    const char* FOUNDATION_RESTRICT id{ nullptr };

    imgui_cache_flags_t flags{ ImCacheFlags::None };
    imgui_cache_value_type_t type{ ImCacheValueType::Null };

    union value_t
    {
        bool b{false};
        float f32;
        double f64;
        int32_t i32;
        unsigned u32;
        int64_t i64;
        uint64_t u64;
        string_table_symbol_t str;
    } value;

    union fetcher_t
    {
        bool (*b)(const im_cache_args_t& args);
        float (*f32)(const im_cache_args_t& args);
        double (*f64)(const im_cache_args_t& args);
        int32_t (*i32)(const im_cache_args_t& args);
        unsigned (*u32)(const im_cache_args_t& args);
        int64_t (*i64)(const im_cache_args_t& args);
        uint64_t (*u64)(const im_cache_args_t& args);
        string_const_t (*str)(const im_cache_args_t& args);
    } fetcher;

    tick_t access{ 0 };
    tick_t updated{ 0 };
    void* context{ nullptr };
    size_t size{ 0 }; // Size if context was copied locally
};

FOUNDATION_FORCEINLINE hash_t hash_entry(const im_cache_entry_t& value)
{
    return value.key;
}

static struct IMCACHE_MODULE {
    database<im_cache_entry_t, hash_entry> db{};
    dispatcher_thread_handle_t fetcher{ 0 };
    event_handle entry_updated_event{};

} *_im_cache;

FOUNDATION_FORCEINLINE decltype(_im_cache->db)& imcache_db()
{
    FOUNDATION_ASSERT(_im_cache);
    return _im_cache->db;
}

FOUNDATION_FORCEINLINE event_handle& imcache_event()
{
    FOUNDATION_ASSERT(_im_cache);
    return _im_cache->entry_updated_event;
}

FOUNDATION_FORCEINLINE void imcache_event_signal()
{
    FOUNDATION_ASSERT(_im_cache);
    _im_cache->entry_updated_event.signal();
}

FOUNDATION_STATIC void* imcache_fetcher_thread(void* context)
{
    hash_t* keys_to_dispose = nullptr;
    auto& sevent = imcache_event();
    while (!thread_try_wait(0))
    {
        sevent.wait(250);

        auto& db = imcache_db();
        for (auto it = db.begin(), end = db.end(); it != end; ++it)
        {
            if (time_elapsed(it->updated) < 0.250)
                continue;

            if (time_elapsed(it->access) > 1.250)
                array_push(keys_to_dispose, it->key);

            // Update entry
            if (it->type == ImCacheValueType::Bool)
                it->value.b = it->fetcher.b({it->context, it->size});
            else if (it->type == ImCacheValueType::Single)
                it->value.f32 = it->fetcher.f32({it->context, it->size});
            else if (it->type == ImCacheValueType::Double)
                it->value.f64 = it->fetcher.f64({it->context, it->size});
            else if (it->type == ImCacheValueType::Integer)
                it->value.i32 = it->fetcher.i32({it->context, it->size});
            else if (it->type == ImCacheValueType::Unsigned)
                it->value.u32 = it->fetcher.u32({it->context, it->size});
            else if (it->type == ImCacheValueType::Int64)
                it->value.i64 = it->fetcher.i64({it->context, it->size});
            else if (it->type == ImCacheValueType::UInt64)
                it->value.u64 = it->fetcher.u64({it->context, it->size});
            else if (it->type == ImCacheValueType::Symbol)
            {
                string_const_t str = it->fetcher.str({it->context, it->size});
                it->value.str = string_table_encode(str.str, str.length);
            }

            it->updated = time_current();
            it->flags |= ImCacheFlags::Initialized;
        }

        for (unsigned i = 0, count = array_size(keys_to_dispose); i < count; ++i)
            db.remove(keys_to_dispose[i]);

        array_clear(keys_to_dispose);
    }

    array_deallocate(keys_to_dispose);
    return nullptr;
}

FOUNDATION_FORCEINLINE void imcache_update_entry(im_cache_entry_t& entry, hash_t key, imgui_cache_flags_t flags, imgui_cache_value_type_t type, void* context, size_t size)
{
    entry.key = key;
    entry.flags = flags;
    entry.type = type;
    entry.updated = 0;

    if (size > 0)
    {
        entry.context = memory_allocate(HASH_IMCACHE, size, 0, MEMORY_PERSISTENT);
        memcpy(entry.context, context, size);
    }
    else
        entry.context = context;
    entry.size = size;

    // Initialize the fetcher thread on-demand only
    if (_im_cache->fetcher == 0)
        _im_cache->fetcher = dispatch_thread("imcache_fetcher", imcache_fetcher_thread);

    entry.access = time_current();
}

FOUNDATION_FORCEINLINE bool imcache_select(decltype(_im_cache->db)& db, hash_t key, im_cache_entry_t& entry)
{
    if (!db.select(key, entry))
        return false;
    
    if (time_elapsed(entry.updated) > 0.250) 
        imcache_event_signal();

    // TODO: Queue access
    return db.update(key, LR1(_1.access = time_current()));
}

#define IM_CACHE_IMPL(__FIELD__, __TYPE__) \
    auto& db = imcache_db();        \
    auto& sevent = imcache_event(); \
    im_cache_entry_t entry;         \
    if (imcache_select(db, key, entry)) \
    { \
        FOUNDATION_ASSERT(entry.type == __TYPE__); \
        return entry.value.__FIELD__; \
    } \
    imcache_update_entry(entry, key, flags, __TYPE__, context, size); \
    entry.fetcher.__FIELD__ = __FIELD__; \
    entry.value.__FIELD__ = default_value; \
    if (db.insert(entry) != 0) \
    { \
        sevent.signal(); \
        return entry.value.__FIELD__; \
    } \
    return default_value;

bool imcache(hash_t key, bool(*b)(const im_cache_args_t& args), bool default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    IM_CACHE_IMPL(b, ImCacheValueType::Bool);
}

float imcache(hash_t key, float(*f32)(const im_cache_args_t& args), float default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    IM_CACHE_IMPL(f32, ImCacheValueType::Single);
}

double imcache(hash_t key, double(*f64)(const im_cache_args_t& args), double default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    IM_CACHE_IMPL(f64, ImCacheValueType::Double);
}

int32_t imcache(hash_t key, int32_t(*i32)(const im_cache_args_t& args), int32_t default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    IM_CACHE_IMPL(i32, ImCacheValueType::Integer);
}

uint32_t imcache(hash_t key, unsigned(*u32)(const im_cache_args_t& args), uint32_t default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    IM_CACHE_IMPL(u32, ImCacheValueType::Unsigned);
}

int64_t imcache(hash_t key, int64_t(*i64)(const im_cache_args_t& args), int64_t default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    IM_CACHE_IMPL(i64, ImCacheValueType::Int64);
}

uint64_t imcache(hash_t key, uint64_t(*u64)(const im_cache_args_t& args), uint64_t default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    IM_CACHE_IMPL(u64, ImCacheValueType::UInt64);
}

string_const_t imcache(hash_t key, string_const_t(*str)(const im_cache_args_t& args), const char* default_value, size_t length, void* context, size_t size, imgui_cache_flags_t flags)
{
    auto& db = imcache_db(); 
    auto& sevent = imcache_event(); 
    
    im_cache_entry_t entry; 
    if (imcache_select(db, key, entry)) 
    {
        FOUNDATION_ASSERT(entry.type == ImCacheValueType::Symbol);
        return string_table_decode_const(entry.value.str);
    } 
    
    imcache_update_entry(entry, key, flags, ImCacheValueType::Symbol, context, size); 
    entry.fetcher.str = str; 
    entry.value.str = string_table_encode(default_value, length);
    if (db.insert(entry) != 0)
        sevent.signal(); 

    return string_table_decode_const(entry.value.str);
}

FOUNDATION_STATIC void imcache_initialize()
{
    _im_cache = MEM_NEW(HASH_IMCACHE, IMCACHE_MODULE);
}

FOUNDATION_STATIC void imcache_shutdown()
{
    if (dispatcher_thread_is_running(_im_cache->fetcher))
        dispatcher_thread_stop(_im_cache->fetcher);

    for (auto it = _im_cache->db.begin_exclusive_lock(), end = _im_cache->db.end_exclusive_lock(); it != end; ++it)
    {
        if (it->size > 0)
        {
            it->size = 0;
            memory_deallocate(it->context);
            it->context = nullptr;
        }
    }
    MEM_DELETE(_im_cache);
}

DEFINE_MODULE(IMCACHE, imcache_initialize, imcache_shutdown, MODULE_PRIORITY_UI_HEADLESS);
