/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include "function.h"
#include "string_table.h"

#if FOUNDATION_PLATFORM_MACOS
    #include <new>
#endif

template<typename T, T DEFAULT_VALUE = T{}>
struct option_t
{
    typedef option_t<T, DEFAULT_VALUE> O;
    typedef function<bool(T&)> fetcher_handler_t;

    mutable T value{ DEFAULT_VALUE };
    mutable bool initialized{ false };
    fetcher_handler_t fetcher;

    option_t(T d = DEFAULT_VALUE)
        : value(d)
        , initialized(false)
        , fetcher(nullptr)
    {
    }

    option_t(const O& o)
        : value(o.value)
        , initialized(o.initialized)
        , fetcher(o.fetcher)
    {
    }

    option_t(O&& o)
        : value(o.value)
        , initialized(o.initialized)
    {
        fetcher = std::move(o.fetcher);
        o.value = DEFAULT_VALUE;
        o.initialized = false;
    }

    operator bool() const
    {
        return initialized;
    }

    operator T() const
    {
        return value;
    }

    O& operator=(T d)
    {
        value = d;
        initialized = true;
        fetcher = nullptr;
        return *this;
    }

    O& operator=(const O& o)
    {
        if (this == &o)
            return *this;

        value = o.value;
        initialized = o.initialized;
        new (&fetcher) fetcher_handler_t(o.fetcher);
        return *this;
    }

    O& operator=(O&& o)
    {
        if (this == &o)
            return *this;

        value = o.value;
        initialized = o.initialized;
        fetcher = std::move(o.fetcher);

        o.value = DEFAULT_VALUE;
        o.initialized = false;
        o.fetcher = nullptr;
        return *this;
    }

    T get_or_default(T dv = DEFAULT_VALUE) const
    {
        if (initialized)
            return value;
        return dv;
    }

    T fetch() const
    {
        if (initialized)
            return value;

        if (!fetcher)
            return get_or_default(value);

        if (!fetcher(value))
            return DEFAULT_VALUE;

        initialized = true;
        return value;
    }

    void reset(const fetcher_handler_t& handler = nullptr)
    {
        initialized = false;
        fetcher = handler;
    }
};

#if FOUNDATION_PLATFORM_WINDOWS
typedef option_t<double, __builtin_nan("0")> double_option_t;
#else
struct double_option_t
{
    typedef double T;
    typedef double_option_t O;
    typedef function<bool(T& out_value)> fetcher_handler_t;

    mutable T value{ __builtin_nan("0") };
    mutable bool initialized{ false };
    fetcher_handler_t fetcher;

    double_option_t(T d = __builtin_nan("0"))
        : value(d)
        , initialized(false)
        , fetcher(nullptr)
    {
    }

    double_option_t(const O& o)
        : value(o.value)
        , initialized(o.initialized)
        , fetcher(o.fetcher)
    {
    }

    double_option_t(O&& o)
        : value(o.value)
        , initialized(o.initialized)
    {
        fetcher = std::move(o.fetcher);
        o.value = __builtin_nan("0");
        o.initialized = false;
    }

    operator bool() const
    {
        return initialized;
    }

    operator T() const
    {
        return value;
    }

    O& operator=(T d)
    {
        value = d;
        initialized = true;
        fetcher = nullptr;
        return *this;
    }

    O& operator=(const O& o)
    {
        if (this == &o)
            return *this;

        value = o.value;
        initialized = o.initialized;
        new (&fetcher) fetcher_handler_t(o.fetcher);
        return *this;
    }

    O& operator=(O&& o)
    {
        if (this == &o)
            return *this;

        value = o.value;
        initialized = o.initialized;
        fetcher = std::move(o.fetcher);

        o.value = __builtin_nan("0");
        o.initialized = false;
        o.fetcher = nullptr;
        return *this;
    }

    T get_or_default(T dv = __builtin_nan("0")) const
    {
        if (initialized)
            return value;
        return dv;
    }

    T fetch() const
    {
        if (initialized)
            return value;

        if (!fetcher)
            return get_or_default(value);

        if (!fetcher(value))
            return __builtin_nan("0");

        initialized = true;
        return value;
    }

    void reset(const fetcher_handler_t& handler = nullptr)
    {
        initialized = false;
        fetcher = handler;
    }
};
#endif
typedef option_t<string_table_symbol_t, STRING_TABLE_NULL_SYMBOL> string_option_t;
