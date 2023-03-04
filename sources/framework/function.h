/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <foundation/assert.h>
#include <foundation/memory.h>

#include <new>
#include <type_traits>

 /*! Generate a simple lambda expression that can be used to capture a variable by reference.
  *
  *  @param EXPRESSION The expression to return from the lambda.
  *  @param ...        The list of variables to be captured. i.e.
  *                         > F(execute_fn_and_return_value_to_callee())                                   // Nothing is captured (simplest form)
  *                         > F(local_var + 99), =local_var)                                               // Capture #local_var by value
  *                         > F(string_try_convert_number(STRING_ARGS(str), &local_number), &local_number) // Capture #local_number by reference
  *                         > F2(double d, int i, i + math_round(d))                                       // No capture but we define a lambda with two arguments
  */
#define F(EXPRESSION, ...) [__VA_ARGS__](){ return EXPRESSION; }
#define F1(ARG1, EXPRESSION, ...) [__VA_ARGS__](ARG1){ return EXPRESSION; }
#define F2(ARG1, ARG2, EXPRESSION, ...) [__VA_ARGS__](ARG1, ARG2){ return EXPRESSION; }
#define F3(ARG1, ARG2, ARG3, EXPRESSION, ...) [__VA_ARGS__](ARG1, ARG2, ARG3){ return EXPRESSION; }
#define F4(ARG1, ARG2, ARG3, ARG4, EXPRESSION, ...) [__VA_ARGS__](ARG1, ARG2, ARG3, ARG4){ return EXPRESSION; }
#define F5(ARG1, ARG2, ARG3, ARG4, ARG5, EXPRESSION, ...) [__VA_ARGS__](ARG1, ARG2, ARG3, ARG4, ARG5){ return EXPRESSION; }
#define F6(ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, EXPRESSION, ...) [__VA_ARGS__](ARG1, ARG2, ARG3, ARG4, ARG5, ARG6){ return EXPRESSION; }
#define F7(ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, ARG7, EXPRESSION, ...) [__VA_ARGS__](ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, ARG7){ return EXPRESSION; }
#define F8(ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, ARG7, ARG8, EXPRESSION, ...) [__VA_ARGS__](ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, ARG7, ARG8){ return EXPRESSION; }
#define F9(ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, ARG7, ARG8, ARG9, EXPRESSION, ...) [__VA_ARGS__](ARG1, ARG2, ARG3, ARG4, ARG5, ARG6, ARG7, ARG8, ARG9){ return EXPRESSION; }

// Lambda macros that can be used to simplify the creation of lambdas for readability purposes (i.e. one-liners)
#define L0(EXPRESSION, ...) [=](__VA_ARGS__){ return EXPRESSION; }
#define L1(EXPRESSION) F1(auto _1, EXPRESSION, =)
#define L2(EXPRESSION) F2(auto _1, auto _2, EXPRESSION, =)
#define L3(EXPRESSION) F3(auto _1, auto _2, auto _3, EXPRESSION, =)
#define SL0(EXPRESSION) [](){ return EXPRESSION; }
#define SL1(EXPRESSION) [](auto _1){ return EXPRESSION; }
#define SL2(EXPRESSION) [](auto _1, auto _2){ return EXPRESSION; }
#define SC1(EXPRESSION) [](const auto& _1){ return EXPRESSION; }
#define SC2(EXPRESSION) [](const auto& _1, const auto& _2){ return EXPRESSION; }
#define SC3(EXPRESSION) [](const auto& _1, const auto& _2, const auto& _3){ return EXPRESSION; }
#define LC1(EXPRESSION) [=](const auto& _1){ return EXPRESSION; }
#define LC2(EXPRESSION) [=](const auto& _1, const auto& _2){ return EXPRESSION; }
#define LC3(EXPRESSION) [=](const auto& _1, const auto& _2, const auto& _3){ return EXPRESSION; }
#define LR1(EXPRESSION) [=](auto& _1){ return EXPRESSION; }
#define LR2(EXPRESSION) [=](auto& _1, auto& _2){ return EXPRESSION; }
#define LR3(EXPRESSION) [=](auto& _1, auto& _2, auto& _3){ return EXPRESSION; }

#define LCCCR(EXPRESSION, ...) F4(const auto& _1, const auto& _2, const auto& _3, auto& _4, EXPRESSION, __VA_ARGS__)

template <typename T> struct function;

/*! Function wrapper that can hold any callable object. 
 * 
 *  This is a very simple implementation of std::function, but it is not 
 *  compatible with std::function. It is only compatible with the same
 *  function wrapper.
 * 
 *  The function wrapper is designed to be used with small objects, and
 *  it will allocate memory on the heap if the object is too large to fit
 *  in the fixed size buffer.
 * 
 *  The function wrapper is not thread safe, and it is not designed to be
 *  used with objects that are not copy-constructible.
 * 
 *  @type R       The return type of the function.
 *  @type Args... The argument types of the function.
 *  @type Functor The type of the functor to wrap.
 */
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
        FOUNDATION_ASSERT(this->handler);
        if (closure_size == 0)
            return ((functor_t)this->handler)(std::forward<Args>(args)...);
        return this->handler(get_closure_ptr(), std::forward<Args>(args)...);
    }

    FOUNDATION_FORCEINLINE R invoke(Args... args) const
    {
        if (this->handler == nullptr)
            return R();
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
