/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/assert.h>
#include <foundation/memory.h>

#include <new>
#include <type_traits>

// Lambda macros (these macro do not work with referenced values i.e. ..., double& value)
#define L0(EXPRESSION, ...) [=](__VA_ARGS__){ return EXPRESSION; }
#define L1(EXPRESSION) [=](auto _1){ return EXPRESSION; }
#define L2(EXPRESSION) [=](auto _1, auto _2){ return EXPRESSION; }
#define L3(EXPRESSION) [=](auto _1, auto _2, auto _3){ return EXPRESSION; }
#define SL0(EXPRESSION, ...) [](__VA_ARGS__){ return EXPRESSION; }
#define SL1(EXPRESSION) [](auto _1){ return EXPRESSION; }
#define SL2(EXPRESSION) [](auto _1, auto _2){ return EXPRESSION; }
#define SL3(EXPRESSION) [](auto _1, auto _2, auto _3){ return EXPRESSION; }
#define SC1(EXPRESSION) [](const auto& _1){ return EXPRESSION; }
#define SC2(EXPRESSION) [](const auto& _1, const auto& _2){ return EXPRESSION; }
#define SC3(EXPRESSION) [](const auto& _1, const auto& _2, const auto& _3){ return EXPRESSION; }
#define LC1(EXPRESSION) [=](const auto& _1){ return EXPRESSION; }
#define LC2(EXPRESSION) [=](const auto& _1, const auto& _2){ return EXPRESSION; }
#define LC3(EXPRESSION) [=](const auto& _1, const auto& _2, const auto& _3){ return EXPRESSION; }
#define LR1(EXPRESSION) [=](auto& _1){ return EXPRESSION; }
#define LR2(EXPRESSION) [=](auto& _1, auto& _2){ return EXPRESSION; }
#define LR3(EXPRESSION) [=](auto& _1, auto& _2, auto& _3){ return EXPRESSION; }

#define R1(EXPRESSION) [=](auto& _1){ return EXPRESSION; }

template <typename T>
struct function;

template <typename R, typename... Args>
struct function<R(Args...)>
{
    typedef R(*functor_t)(Args&&...);
    typedef R(*invoke_fn_t)(const uint8_t*, Args&&...);
    typedef void (*construct_fn_t)(uint8_t*, const uint8_t*);
    typedef void (*destroy_fn_t)(uint8_t*);

    invoke_fn_t handler{ nullptr };	
    construct_fn_t construct_f{ nullptr };
    destroy_fn_t destroy_f{ nullptr };

    union {
        uint8_t fixed_closure[64];
        uint8_t* dynamic_closure{ nullptr };
    };
    unsigned int closure_size{ 0 };

    FOUNDATION_FORCEINLINE uint8_t* get_closure_ptr() const
    {
        if (closure_size > sizeof(fixed_closure))
            return dynamic_closure;
        return (uint8_t*)&fixed_closure[0];
    }

    template <typename Functor>
    FOUNDATION_FORCEINLINE static R invoke_fn(const Functor* fn, Args&&... args)
    { 
        return (*fn)(std::forward<Args>(args)...); 
    }

    template <typename Functor>
    FOUNDATION_FORCEINLINE static void construct_fn(Functor* construct_dst, const Functor* construct_src)
    {
        //log_debugf(0, S("CTOR 0x%016" PRIxPTR " <- 0x%016" PRIxPTR), construct_dst, construct_src);
        // the functor type must be copy-constructible
        new (construct_dst) Functor(*construct_src);
    }

    template <typename Functor>
    FOUNDATION_FORCEINLINE static void destroy_fn(Functor* f)
    {
        //log_debugf(0, S("DTOR 0x%016" PRIxPTR), f);
        f->~Functor();
    }

    FOUNDATION_FORCEINLINE function()
        : handler(nullptr)
        , dynamic_closure(nullptr)
        , closure_size(0)
    {}

    FOUNDATION_FORCEINLINE function(std::nullptr_t n)
        : function()
    {
    }

    FOUNDATION_FORCEINLINE function(functor_t fn)
        : handler((invoke_fn_t)fn)
        , dynamic_closure(nullptr)
        , closure_size(0)
    {}

    template <typename Functor>
    FOUNDATION_FORCEINLINE function(Functor f)
        : handler((invoke_fn_t)invoke_fn<Functor>)
        , construct_f(reinterpret_cast<construct_fn_t>(construct_fn<Functor>))
        , destroy_f(reinterpret_cast<destroy_fn_t>(destroy_fn<Functor>))
        , dynamic_closure(nullptr)
        , closure_size(sizeof(f))
    {
        if (closure_size > sizeof(fixed_closure))
            dynamic_closure = (uint8_t*)memory_allocate(0, closure_size, 0, MEMORY_PERSISTENT);
        this->construct_f(get_closure_ptr(), (uint8_t*)&f);
    }

    FOUNDATION_FORCEINLINE function(function&& o)
        : handler(o.handler)
        , construct_f(o.construct_f)
        , destroy_f(o.destroy_f)
        , dynamic_closure(o.dynamic_closure)
        , closure_size(o.closure_size)
    {
        if (closure_size > 0 && closure_size <= sizeof(fixed_closure))
            memcpy(fixed_closure, o.fixed_closure, closure_size);

        o.handler = nullptr;
        o.construct_f = nullptr;
        o.destroy_f = nullptr;
        o.dynamic_closure = nullptr;
        o.closure_size = 0;
    }

    FOUNDATION_FORCEINLINE function(const function& o)
        : function()
    {
        this->operator=(o);
    }

    FOUNDATION_FORCEINLINE function& operator=(const function& o)
    {
        //log_infof(0, STRING_CONST("COPY FUNCTION (%p, %u) < (%p, %u)"), handler, closure_size, o.handler, o.closure_size);
        if (destroy_f && closure_size > 0)
            this->destroy_f(get_closure_ptr());
        if (closure_size > sizeof(fixed_closure))
        {
            memory_deallocate(dynamic_closure);
            dynamic_closure = nullptr;
        }

        handler = o.handler;
        construct_f = o.construct_f;
        destroy_f = o.destroy_f;

        closure_size = o.closure_size;
        if (closure_size > sizeof(fixed_closure))
            dynamic_closure = (uint8_t*)memory_allocate(0, closure_size, 0, MEMORY_PERSISTENT);
        
        if (construct_f)
            construct_f(get_closure_ptr(), o.get_closure_ptr());
        else if (closure_size > 0)
            memcpy(get_closure_ptr(), o.get_closure_ptr(), closure_size);
        
        return *this;
    }

    FOUNDATION_FORCEINLINE function& operator=(function&& o)
    {
        handler = o.handler;
        construct_f = o.construct_f;
        destroy_f = o.destroy_f;
        closure_size = o.closure_size;
        if (closure_size > sizeof(fixed_closure))
            dynamic_closure = o.dynamic_closure;
        else if (closure_size > 0)
            memcpy(fixed_closure, o.fixed_closure, sizeof(fixed_closure));

        o.handler = nullptr;
        o.construct_f = nullptr;
        o.destroy_f = nullptr;
        o.dynamic_closure = nullptr;
        o.closure_size = 0;
        return *this;
    }
    
    FOUNDATION_FORCEINLINE ~function()
    {
        if (destroy_f && closure_size > 0)
            this->destroy_f(get_closure_ptr());
        if (closure_size > sizeof(fixed_closure))
            memory_deallocate(dynamic_closure);
    }

    FOUNDATION_FORCEINLINE R operator()(Args... args) const
    {
        if (closure_size == 0)
            return ((functor_t)this->handler)(std::forward<Args>(args)...);
        return this->handler(get_closure_ptr(), std::forward<Args>(args)...);
    }

    FOUNDATION_FORCEINLINE R invoke(Args... args) const
    {
        if (closure_size == 0)
            return ((functor_t)this->handler)(std::forward<Args>(args)...);
        return this->handler(get_closure_ptr(), std::forward<Args>(args)...);
    }

    FOUNDATION_FORCEINLINE operator bool() const
    {
        return handler != nullptr;
    }

    FOUNDATION_FORCEINLINE bool valid() const
    {
        return handler != nullptr;
    }

    FOUNDATION_FORCEINLINE bool operator!=(const std::nullptr_t& n) const
    {
        FOUNDATION_UNUSED(n);
        return handler != nullptr;
    }
};
