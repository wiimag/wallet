/*
 * Copyright 2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/common.h>

typedef enum class ImCacheFlags
{
    None = 0,
    Initialized = 1 << 0,
} imgui_cache_flags_t;
DEFINE_ENUM_FLAGS(ImCacheFlags);

struct im_cache_args_t
{
    void* context;
    size_t size;
};

bool imcache(hash_t key, bool(*b)(const im_cache_args_t& args), bool default_value, void* context, size_t size, imgui_cache_flags_t flags);
float imcache(hash_t key, float(*f32)(const im_cache_args_t& args), float default_value, void* context, size_t size, imgui_cache_flags_t flags);
double imcache(hash_t key, double(*f64)(const im_cache_args_t& args), double default_value, void* context, size_t size, imgui_cache_flags_t flags);
int32_t imcache(hash_t key, int32_t(*i32)(const im_cache_args_t& args), int32_t default_value, void* context, size_t size, imgui_cache_flags_t flags);
uint32_t imcache(hash_t key, unsigned(*u32)(const im_cache_args_t& args), uint32_t default_value, void* context, size_t size, imgui_cache_flags_t flags);
int64_t imcache(hash_t key, int64_t(*i64)(const im_cache_args_t& args), int64_t default_value, void* context, size_t size, imgui_cache_flags_t flags);
uint64_t imcache(hash_t key, uint64_t(*u64)(const im_cache_args_t& args), uint64_t default_value, void* context, size_t size, imgui_cache_flags_t flags);
string_const_t imcache(hash_t key, string_const_t(*string)(const im_cache_args_t& args), const char* default_value, size_t length, void* context, size_t size, imgui_cache_flags_t flags);

// Short query

template<typename T>
FOUNDATION_FORCEINLINE T imcache(hash_t key, T default_value)
{
    return imcache(key, nullptr, default_value, nullptr, 0, ImCacheFlags::None);
}

template<typename T, size_t N>
FOUNDATION_FORCEINLINE T imcache(const char (&id)[N], T default_value)
{
    hash_t key = string_hash(id, N - 1);
    return imcache(key, nullptr, default_value, nullptr, 0, ImCacheFlags::None);
}

// Short query with functor

template<typename T>
FOUNDATION_FORCEINLINE T imcache(hash_t key, T(*f)(const im_cache_args_t& args), T default_value)
{
    return imcache(key, f, default_value, nullptr, 0, ImCacheFlags::None);
}

template<typename T, size_t N>
FOUNDATION_FORCEINLINE T imcache(const char(&id)[N], T(*f)(const im_cache_args_t& args), T default_value)
{
    hash_t key = string_hash(id, N - 1);
    return imcache(key, f, default_value, nullptr, 0, ImCacheFlags::None);
}

// Full query with name id

template<size_t N>
FOUNDATION_FORCEINLINE bool imcache(const char (&id)[N], bool(*b)(const im_cache_args_t& args), bool default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    const hash_t key = string_hash(id, N - 1);
    return imcache(key, b, (float)default_value, context, size, flags);
}

template<size_t N>
FOUNDATION_FORCEINLINE float imcache(const char(&id)[N], float(*f32)(const im_cache_args_t& args), float default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    const hash_t key = string_hash(id, N - 1);
    return imcache(key, f32, default_value, context, size, flags);
}

template<size_t N>
FOUNDATION_FORCEINLINE double imcache(const char(&id)[N], double(*f64)(const im_cache_args_t& args), double default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    const hash_t key = string_hash(id, N - 1);
    return imcache(key, f64, default_value, context, size, flags);
}

template<size_t N>
FOUNDATION_FORCEINLINE int32_t imcache(const char(&id)[N], int32_t(*i32)(const im_cache_args_t& args), int32_t default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    const hash_t key = string_hash(id, N - 1);
    return imcache(key, i32, default_value, context, size, flags);
}

template<size_t N>
FOUNDATION_FORCEINLINE uint32_t imcache(const char(&id)[N], unsigned(*u32)(const im_cache_args_t& args), uint32_t default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    const hash_t key = string_hash(id, N - 1);
    return imcache(key, u32, default_value, context, size, flags);
}

template<size_t N>
FOUNDATION_FORCEINLINE int64_t imcache(const char(&id)[N], int64_t(*i64)(const im_cache_args_t& args), int64_t default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    const hash_t key = string_hash(id, N - 1);
    return imcache(key, i64, default_value, context, size, flags);
}

template<size_t N>
FOUNDATION_FORCEINLINE uint64_t imcache(const char(&id)[N], uint64_t(*u64)(const im_cache_args_t& args), uint64_t default_value, void* context, size_t size, imgui_cache_flags_t flags)
{
    const hash_t key = string_hash(id, N - 1);
    return imcache(key, u64, default_value, context, size, flags);
}

template<size_t N>
FOUNDATION_FORCEINLINE string_const_t imcache(const char(&id)[N], string_const_t(*string)(const im_cache_args_t& args), const char* default_value, size_t length, void* context, size_t size, imgui_cache_flags_t flags)
{
    const hash_t key = string_hash(id, N - 1);
    return imcache(key, string, default_value, length, context, size, flags);
}
