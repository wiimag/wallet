/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include "function.h"
#include "string_table.h"

#include <new>

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

    O& operator=(O&& o) noexcept
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

constexpr double DOUBLE_OPTION_DEFAULT_VALUE = __builtin_nan("0");
struct double_option_t
{
    typedef double T;
    typedef double_option_t O;
    typedef function<bool(T& out_value)> fetcher_handler_t;

    mutable T value{ DOUBLE_OPTION_DEFAULT_VALUE };
    mutable bool initialized{ false };
    fetcher_handler_t fetcher;

    double_option_t(T d = DOUBLE_OPTION_DEFAULT_VALUE)
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

    double_option_t(O&& o) noexcept
        : value(o.value)
        , initialized(o.initialized)
    {
        fetcher = std::move(o.fetcher);
        o.value = DOUBLE_OPTION_DEFAULT_VALUE;
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

    O& operator=(O&& o) noexcept
    {
        if (this == &o)
            return *this;

        value = o.value;
        initialized = o.initialized;
        fetcher = std::move(o.fetcher);

        o.value = DOUBLE_OPTION_DEFAULT_VALUE;
        o.initialized = false;
        o.fetcher = nullptr;
        return *this;
    }

    bool try_get(double& out_value) const
    {
        if (initialized)
        {
            out_value = value;
            return true;
        }

        return false;
    }

    T get_or_default(T dv = DOUBLE_OPTION_DEFAULT_VALUE) const
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
            return DOUBLE_OPTION_DEFAULT_VALUE;

        initialized = true;
        return value;
    }

    void reset(const fetcher_handler_t& handler = nullptr)
    {
        initialized = false;
        fetcher = handler;
    }
};

typedef option_t<string_table_symbol_t, STRING_TABLE_NULL_SYMBOL> string_option_t;
