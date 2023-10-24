/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "expr.h"

#include <framework/math.h>
#include <framework/string.h>
#include <framework/array.h>
#include <framework/handle.h>
#include <framework/progress.h>

typedef enum VecMatType : uint32_t {
    VECMAT_NIL = 0,
    VECMAT_SCALAR = 1,
    VECMAT_VECTOR2 = 2,
    VECMAT_VECTOR3 = 3,
    VECMAT_VECTOR4 = 4,
    VECMAT_MAT4X4 = 16,
} vecmat_type_t;

struct vecmat_t {
    vecmat_type_t type;
    union {
        float values[16];
        float f;
        vec2 v2;
        vec3 v3;
        vec4 v4;
        mat4 m4;
    };
};

struct vecmat_context_t
{
    //unsigned index{0};
    //vecmat_t results[8];
};

static thread_local vecmat_t _results[64]{};
static thread_local unsigned _results_ring_index{ 0 };

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_noop(const expr_func_t* f, vec_expr_t* args, void* c)
{
    #if BUILD_DEBUG
    throw ExprError(EXPR_ERROR_EVALUATION_NOT_IMPLEMENTED, "Missing implementation for %.*s", STRING_FORMAT(f->name));
    #else
    return NIL;
    #endif
}

template<typename T>
FOUNDATION_STATIC vecmat_t* expr_eval_pointer_array_copy(const expr_result_t& e, vecmat_t* arg)
{
    const unsigned element_size = e.element_size();
    const unsigned element_count = e.element_count();
    for (unsigned i = 0; i < element_count; ++i)
        arg->values[i] = (float)*((const T*)e.ptr + i);

    return arg;
}

FOUNDATION_STATIC vecmat_t* expr_eval_pointer_read(const expr_result_t& e, vecmat_t* arg)
{
    uint64_t flags = e.index;
    const unsigned element_size = e.element_size();
    const unsigned element_count = e.element_count();

    arg->type = (vecmat_type_t)element_count;
    if ((flags & EXPR_POINTER_ARRAY_FLOAT))
    {
        if (element_size == 4)
        {
            memcpy(arg->values, e.ptr, element_size * element_count);
            return arg;
        }
        else if (element_size == 8)
            return expr_eval_pointer_array_copy<double>(e, arg);
    }
    else if ((flags & EXPR_POINTER_ARRAY_INTEGER))
    {
        if ((flags & EXPR_POINTER_ARRAY_UNSIGNED) == EXPR_POINTER_ARRAY_UNSIGNED)
        {
            if (element_size == 1) return expr_eval_pointer_array_copy<uint8_t>(e, arg);
            if (element_size == 2) return expr_eval_pointer_array_copy<uint16_t>(e, arg);
            if (element_size == 4) return expr_eval_pointer_array_copy<uint32_t>(e, arg);
            if (element_size == 8) return expr_eval_pointer_array_copy<uint64_t>(e, arg);
        }
        else
        {
            if (element_size == 1) return expr_eval_pointer_array_copy<int8_t>(e, arg);
            if (element_size == 2) return expr_eval_pointer_array_copy<int16_t>(e, arg);
            if (element_size == 4) return expr_eval_pointer_array_copy<int32_t>(e, arg);
            if (element_size == 8) return expr_eval_pointer_array_copy<int64_t>(e, arg);
        }
    }

    arg->type = VECMAT_NIL;
    throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid expression data element size `%.*s`", STRING_FORMAT(e.as_string()));
}

FOUNDATION_STATIC vecmat_t& expr_eval_vecmat_set_arg(const expr_func_t* f, const expr_result_t& e, vecmat_t* arg)
{
    if (e.type == EXPR_RESULT_NULL)
    {
        arg->type = VECMAT_NIL;
    }
    else if (e.type == EXPR_RESULT_FALSE || e.type == EXPR_RESULT_TRUE || e.type == EXPR_RESULT_NUMBER)
    {
        arg->type = VECMAT_SCALAR;
        float scalar = (float)e.as_number(0.0);
        for (size_t i = 0; i < ARRAY_COUNT(arg->v4.components); ++i)
            arg->values[i] = scalar;
    }
    else if (e.type == EXPR_RESULT_ARRAY)
    {
        const unsigned e_args_count = array_size(e.list);
        if (e_args_count == 1)       arg->type = VECMAT_SCALAR;
        else if (e_args_count == 2)  arg->type = VECMAT_VECTOR2;
        else if (e_args_count == 3)  arg->type = VECMAT_VECTOR3;
        else if (e_args_count == 4)  arg->type = VECMAT_VECTOR4;
        else if (e_args_count == 16) arg->type = VECMAT_MAT4X4;
        else
            throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Invalid expression vector argument `%.*s` size", STRING_FORMAT(e.as_string()));

        for (unsigned i = 0; i < e_args_count; ++i)
            arg->values[i] = (float)e.list[i].as_number(NAN);
    }
    else if (e.type == EXPR_RESULT_POINTER)
    {
        expr_eval_pointer_read(e, arg);
    }
    else
    {
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Expression argument type not supported: %.*s", STRING_FORMAT(e.as_string()));
    }

    return *arg;
}

FOUNDATION_STATIC vecmat_t expr_eval_vecmat_arg_nth(const expr_func_t* f, vec_expr_t* args, unsigned& arg_index)
{
    if (args == nullptr || (int)arg_index >= args->len)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Missing argument %u in %.*s", arg_index + 1, STRING_FORMAT(f->name));

    vecmat_t arg{};
    const expr_result_t& e = expr_eval(&args->buf[arg_index++]);
    return expr_eval_vecmat_set_arg(f, e, &arg);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_push_result(vecmat_context_t* context, const vecmat_t& result)
{
    if (result.type == VECMAT_NIL)
        return expr_result_t(EXPR_RESULT_NULL);

    if (result.type == VECMAT_SCALAR)
        return expr_result_t((double)result.f);

    if (_results_ring_index >= ARRAY_COUNT(_results))
        throw ExprError(EXPR_ERROR_EVALUATION_STACK_FULL, "Expression result stack is full");
    
    vecmat_t& vm = _results[_results_ring_index++];
    _results_ring_index = _results_ring_index % ARRAY_COUNT(_results);
    vm = result;
    return expr_result_t(vm.values, sizeof(vm.values[0]), vm.type, EXPR_POINTER_ARRAY_FLOAT);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_add(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& vma = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& vmb = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ max(vma.type, vmb.type)  };
    for (unsigned i = 0; i < r.type; ++i)        
        r.values[i] = vma.values[i] + vmb.values[i];
        
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_sub(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& vma = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& vmb = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ max(vma.type, vmb.type) };
    for (unsigned i = 0; i < r.type; ++i)
        r.values[i] = vma.values[i] - vmb.values[i];

    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_mul(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& vma = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& vmb = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ max(vma.type, vmb.type) };
    if (vma.type <= VECMAT_VECTOR4 && vmb.type == VECMAT_SCALAR)
    {
        for (unsigned i = 0; i < r.type; ++i)
            r.values[i] = vma.values[i] * vmb.f;
    }
    else if (vma.type <= VECMAT_VECTOR4 && vmb.type <= VECMAT_VECTOR4)
    {
        for (unsigned i = 0; i < r.type; ++i)
            r.values[i] = vma.values[i] * vmb.values[i];
    }
    else if (vma.type <= VECMAT_VECTOR3 && vmb.type == VECMAT_MAT4X4)
    {
        r.type = VECMAT_VECTOR3;
        r.v3.xyz = bx::mul(vma.v3.xyz, vmb.values);
    }
    else if (vma.type == VECMAT_VECTOR4 && vmb.type == VECMAT_MAT4X4)
    {
        r.type = VECMAT_VECTOR4;
        bx::vec4MulMtx(r.values, vma.v4.components, vmb.values);
    }
    else if (vma.type == VECMAT_MAT4X4 && vmb.type == VECMAT_MAT4X4)
    {
        r.type = VECMAT_MAT4X4;
        bx::mtxMul(r.values, vma.values, vmb.values);
    }

    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_rad_to_deg(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& rad = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ rad.type };
    for (unsigned i = 0; i < r.type; ++i)
        r.values[i] = bx::toDeg(rad.values[i]);

    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_simple_moving_average(const expr_func_t* f, vec_expr_t* args, void* c)
{
    if (args->len < 2)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Missing arguments: SMA(set, distance)");

    expr_result_t set = expr_eval(args->get(0));
    if (set.element_count() <= 0)
        return set;

    expr_result_t* sma = nullptr;
    expr_result_t edistance = expr_eval(args->get(1));
    const int distance = to_int(edistance.as_number(2.0));
    for (int i = 0, end = to_int(set.element_count()); i < end; ++i)
    {
        const int rs = max(INT32_C(0), i - distance);
        const int re = min(to_int(set.element_count()), i + distance + 1);
        
        int count = re - rs;
        double sum = 0.0;
        for (int j = rs; j < re; ++j)
        {
            double v = set.as_number(DNAN, (size_t)j);
            if (math_real_is_nan(v))
                --count;
            else
                sum += v;
        }

        if (count <= 1)
        {
            array_push(sma, set.element_at(i));
        }
        else
        {
            array_push(sma, expr_result_t(sum / count));
        }
    }

    return expr_eval_list(sma);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_deg_to_rad(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& rad = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ rad.type };
    for (unsigned i = 0; i < r.type; ++i)
        r.values[i] = bx::toRad(rad.values[i]);

    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_cross(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& B = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ VECMAT_VECTOR3 };
    r.v3.xyz = bx::cross(A.v3.xyz, B.v3.xyz);
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_dot(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& B = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ VECMAT_SCALAR, NAN };
    if (A.type <= VECMAT_VECTOR3 && B.type <= VECMAT_VECTOR3)
        r.f = bx::dot(A.v3.xyz, B.v3.xyz);
    else if (A.type == VECMAT_VECTOR4 && B.type == VECMAT_VECTOR4)
        r.f = bx::dot(A.v4.q, B.v4.q);
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_vec2(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& B = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ VECMAT_VECTOR2, {A.f, B.f} };
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_vec3(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& B = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& C = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ VECMAT_VECTOR3, A.f, B.f, C.f };
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_vec4(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& B = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& C = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& D = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ VECMAT_VECTOR4, A.f, B.f, C.f, D.f };
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_normalize(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ A.type, NAN };
    if (A.type <= VECMAT_VECTOR3)
        r.v3.xyz = bx::normalize(A.v3.xyz);
    else if (A.type == VECMAT_VECTOR4)
        r.v4.q = bx::normalize(A.v4.q);
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_length(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ VECMAT_SCALAR, bx::length(A.v3.xyz) };
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_abs(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ A.type };
    r.v3.xyz = bx::abs(A.v3.xyz);
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_identity(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    vecmat_t r{ VECMAT_MAT4X4 };
    bx::mtxIdentity(r.m4.f);
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_translation(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& B = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& C = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ VECMAT_MAT4X4 };
    bx::mtxTranslate(r.m4.f, A.f, B.f, C.f);
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_rotation(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;    
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& B = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& C = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ VECMAT_MAT4X4 };
    bx::mtxRotateXYZ(r.m4.f, A.f, B.f, C.f);    
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_scale(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    vecmat_t r{ VECMAT_MAT4X4 }; 
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    if (args->len == 1)
    {
        bx::mtxScale(r.m4.f, A.f);
    }
    else
    {
        const vecmat_t& B = expr_eval_vecmat_arg_nth(f, args, arg_index);
        const vecmat_t& C = expr_eval_vecmat_arg_nth(f, args, arg_index);
        bx::mtxScale(r.m4.f, A.f, B.f, C.f);
    }

    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_rotation_zyx(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;

    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& B = expr_eval_vecmat_arg_nth(f, args, arg_index);
    const vecmat_t& C = expr_eval_vecmat_arg_nth(f, args, arg_index);

    vecmat_t r{ VECMAT_MAT4X4 };
    bx::mtxRotateXYZ(r.m4.f, A.f, B.f, C.f);
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_rotation_x(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;
    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    vecmat_t r{ VECMAT_MAT4X4 };
    bx::mtxRotateX(r.m4.f, A.f);
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_rotation_y(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;
    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    vecmat_t r{ VECMAT_MAT4X4 };
    bx::mtxRotateY(r.m4.f, A.f);
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_rotation_z(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;
    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    vecmat_t r{ VECMAT_MAT4X4 };
    bx::mtxRotateZ(r.m4.f, A.f);
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_inverse(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;
    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    vecmat_t r{ VECMAT_MAT4X4 };
    bx::mtxInverse(r.m4.f, A.m4.f);
    return expr_eval_vecmat_push_result(context, r);
}

FOUNDATION_STATIC expr_result_t expr_eval_vecmat_transpose(const expr_func_t* f, vec_expr_t* args, void* c)
{
    vecmat_context_t* context = (vecmat_context_t*)c;
    unsigned arg_index = 0;
    const vecmat_t& A = expr_eval_vecmat_arg_nth(f, args, arg_index);
    vecmat_t r{ VECMAT_MAT4X4 };
    bx::mtxTranspose(r.m4.f, A.m4.f);
    return expr_eval_vecmat_push_result(context, r);
}

struct expr_solve_int_variable_t
{
    string_const_t name;
    int min;
    int max;

    /*! Testing value */
    int index;
    int value;
};

struct expr_solve_equation_t
{
    expr_t* expr;
    int failure;
};

FOUNDATION_STATIC expr_result_t expr_eval_solve_int(const expr_func_t* f, vec_expr_t* args, void* c)
{
    // Get how may expression to solve
    unsigned equations = math_trunc(expr_eval(args->get(0)).as_number(0));
    if (equations == 0)
        return NIL;

    // Get expressions to evaluates
    expr_solve_equation_t* expressions = nullptr;
    for (unsigned i = 0; i < equations; ++i)
    {
        expr_solve_equation_t e{};
        e.expr = args->get(i + 1);
        e.failure = 0;
        array_push(expressions, e);
    }

    // Then for all remaining arguments, consider them to be the variables to solve: NAME, [MIN_INT_VALUE, MAX_INT_VALUE]
    size_t max_evaluation_count = 0;
    expr_solve_int_variable_t* variables = nullptr;
    for (int i = equations + 1, var_index = 0; i + 2 < args->len; i += 3, ++var_index)
    {
        expr_solve_int_variable_t var{};

        var.index = var_index;
        var.name = args->get(i)->token;
        //var.name = expr_eval(args->get(i + 1)).as_string();
        var.min = math_trunc(expr_eval(args->get(i + 1)).as_number(0));
        var.max = math_trunc(expr_eval(args->get(i + 2)).as_number(0));

        // If min is greater than max, swap them
        if (max_evaluation_count == 0)
            max_evaluation_count = var.max - var.min + 1;
        else
            max_evaluation_count *= var.max - var.min + 1;

        array_push(variables, var);
    }

    const unsigned variable_count = array_size(variables);
    if (variable_count == 0)
        return NIL;

    #if !BUILD_DEBUG
    // Shuffle variables in order to try different combinations
    array_shuffle(variables);
    #endif

    // For all equation, brute force and try all possible values for all variables
    // If all equations are true, then we found a solution
    bool found_solution = false;

    // Lets initialize all variables to their min value
    for (unsigned i = 0; i < variable_count; ++i)
        variables[i].value = variables[i].min;

    tick_t start = time_current();
    atom32 evaluation_count = 0;

    size_t progress = 0;
    size_t steps = max_evaluation_count / 100;
    size_t next_step_report = steps;

    // Log what we are about to solve
    log_infof(HASH_EXPR, 
        STRING_CONST("Solving %d equations with %d variables for a total of %u possibilities"), 
        equations, variable_count, max_evaluation_count);

    // For all variables, try all possible values
    expr_solve_int_variable_t* bit = variables;
    const expr_solve_int_variable_t* end = array_end(variables);
    while (bit < end)
    {
        if (++progress >= next_step_report)
        {
            next_step_report += steps;
            log_debugf(HASH_EXPR, STRING_CONST("Progress: %d%%"), (int)(progress * 100ULL / max_evaluation_count));
            progress_set(progress, max_evaluation_count);
        }

        // Set all variables to their current value
        for (unsigned i = 0; i < variable_count; ++i)
            expr_set_global_var(STRING_ARGS(variables[i].name), (double)variables[i].value);

        // Evaluate all equations with the current values
        {
            bool all_true = true;

            for (unsigned i = 0, endi = array_size(expressions); i < endi; ++i)
            {
                expr_solve_equation_t* eq = expressions + i;

                // Increment evaluation count
                ++evaluation_count;

                expr_t* e = eq->expr;
                expr_result_t assertion = expr_eval(e);
                if (assertion.type == EXPR_RESULT_SYMBOL)
                    continue; // Assume it is a comment and continue...

                if (assertion.type != EXPR_RESULT_TRUE)
                {
                    eq->failure++;
                    all_true = false;

                    // Sort equation with most failure first
                    if (i > 0 && expressions[i-1].failure < eq->failure)
                        array_sort(expressions, LC2(_2.failure - _1.failure));

                    // If one equation is false, then we can stop
                    break;
                }
            }

            if (all_true)
            {
                // We found a solution
                found_solution = true;
                break;
            }
        }

        // Increment the current variable
        if (bit->value == bit->max)
        {
            // If we reached the max value, reset it to min and increment the next variable
            bit->value = bit->min;

            // Otherwise, increment the next variable
            while (++bit < end && ++bit->value > bit->max)
            {
                bit->value = bit->min;
            }

            // If we reached the end, we are done
            if (bit >= end)
                break;

            // And reset the bit to the first variable
            bit = variables;
        }
        else
        {
            // Otherwise, just increment the current variable
            ++bit->value;
        }
    }

    #if !BUILD_DEBUG
    // Re-order
    array_sort(variables, [](const auto& a, const auto& b) { return a.index - b.index; });
    #endif

    log_infof(HASH_EXPR, STRING_CONST("Solved %u equations with %u variables in %.2lf seconds by evaluating %d expressions."), 
        equations, variable_count, time_elapsed(start), evaluation_count.load());

    // Log the solution
    if (found_solution)
    {
        log_infof(HASH_EXPR, STRING_CONST("Solution:"));
        for (unsigned i = 0; i < variable_count; ++i)
            log_infof(HASH_EXPR, STRING_CONST("  %.*s = %d"), STRING_FORMAT(variables[i].name), variables[i].value);
    }
    else
    {
        log_infof(HASH_EXPR, STRING_CONST("No solution found"));
    }

    array_deallocate(expressions);

    if (!found_solution)
    {
        array_deallocate(variables);
        return NIL;
    }
    
    // Return the solution
    expr_result_t* results = nullptr;
    foreach(v, variables)
    {
        expr_result_t kvp = expr_eval_pair(expr_result_t(v->name), (double)v->value);
        array_push(results, kvp);
    }

    array_deallocate(variables);
    return results;
}

void expr_register_vec_mat_functions(expr_func_t*& funcs)
{
    const size_t VECMAT_CONTEXT_SIZE = 0;//sizeof(vecmat_context_t); NOT USED YET

    static thread_local mat4 IDENTITY;
    bx::mtxIdentity(IDENTITY.f);
    expr_result_t EIDENTITY(&IDENTITY.f, sizeof(IDENTITY.f[0]), 16, EXPR_POINTER_ARRAY_FLOAT);
    expr_set_global_var("I", EIDENTITY.ptr, EIDENTITY.index);

    // vec2(0,0), vec3(0,0,0), vec4(0,0,0,1), q(0,0,0,1)
    array_push(funcs, (expr_func_t{ STRING_CONST("vec2"), expr_eval_vecmat_vec2, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("vec3"), expr_eval_vecmat_vec3, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("vec4"), expr_eval_vecmat_vec4, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("quat"), expr_eval_vecmat_vec4, NULL, VECMAT_CONTEXT_SIZE }));

    // ADD([1, 2, 3], [1, 2, 3]) == [2, 4, 6]
    array_push(funcs, (expr_func_t{ STRING_CONST("ADD"), expr_eval_vecmat_add, NULL, VECMAT_CONTEXT_SIZE }));

    // SUB([1,2], [1,2]) == [0, 0]
    array_push(funcs, (expr_func_t{ STRING_CONST("SUB"), expr_eval_vecmat_sub, NULL, VECMAT_CONTEXT_SIZE }));

    // MUL([1, 2, 2], 2) == [2, 4, 4]
    array_push(funcs, (expr_func_t{ STRING_CONST("MUL"), expr_eval_vecmat_mul, NULL, VECMAT_CONTEXT_SIZE }));

    // CROSS([1, 0, 0], [0, 1, 0])
    array_push(funcs, (expr_func_t{ STRING_CONST("CROSS"), expr_eval_vecmat_cross, NULL, VECMAT_CONTEXT_SIZE }));

    // DOT([32, 44.5, 0], [1, 0.5, 0])
    array_push(funcs, (expr_func_t{ STRING_CONST("DOT"), expr_eval_vecmat_dot, NULL, VECMAT_CONTEXT_SIZE }));

    // NORMALIZE([1.5, 1, 1])
    array_push(funcs, (expr_func_t{ STRING_CONST("NORMALIZE"), expr_eval_vecmat_normalize, NULL, VECMAT_CONTEXT_SIZE }));

    // LENGTH([2, 2, 2]) == 2
    array_push(funcs, (expr_func_t{ STRING_CONST("LENGTH"), expr_eval_vecmat_length, NULL, VECMAT_CONTEXT_SIZE }));

    // ABS(-3.4641015) == LENGTH(ABS(-2, -3, -4))
    array_push(funcs, (expr_func_t{ STRING_CONST("ABS"), expr_eval_vecmat_abs, NULL, VECMAT_CONTEXT_SIZE }));
    
    array_push(funcs, (expr_func_t{ STRING_CONST("IDENTITY"), expr_eval_vecmat_identity, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("TRANSLATION"), expr_eval_vecmat_translation, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("ROTATION"), expr_eval_vecmat_rotation, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("RX"), expr_eval_vecmat_rotation_x, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("RY"), expr_eval_vecmat_rotation_y, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("RZ"), expr_eval_vecmat_rotation_z, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("RZYZ"), expr_eval_vecmat_rotation_zyx, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("SCALE"), expr_eval_vecmat_scale, NULL, VECMAT_CONTEXT_SIZE }));

    array_push(funcs, (expr_func_t{ STRING_CONST("INVERSE"), expr_eval_vecmat_inverse, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("TRANSPOSE"), expr_eval_vecmat_transpose, NULL, VECMAT_CONTEXT_SIZE }));

    array_push(funcs, (expr_func_t{ STRING_CONST("RAD2DEG"), expr_eval_vecmat_rad_to_deg, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("DEG2RAD"), expr_eval_vecmat_deg_to_rad, NULL, VECMAT_CONTEXT_SIZE }));

    array_push(funcs, (expr_func_t{ STRING_CONST("SMA"), expr_eval_simple_moving_average, NULL, VECMAT_CONTEXT_SIZE }));

    /*
     * SOLVE_INT(2,
     *  (6 * $A) + (2 * $B) + (1 * $C) + (3 * $D) + (4 * $E) + (2 * $F) + (2 * $G) + (2 * $H) + (2 * $I) + (1 * $J) + (1 * $K) == 258,
     *  $A + $B + $C + $D + $E + $F + $G + $H + $I + $J + $K == 100,
     *  A, 10, 15,
     *  B, 6, 12,
     *  C, 4, 8,
     *  D, 9, 10,
     *  E, 9, 10,
     *  F, 7, 10,
     *  G, 5, 15,
     *  H, 5, 15,
     *  I, 6, 13,
     *  J, 6, 13,
     *  K, 5, 15)
     */
     array_push(funcs, (expr_func_t{ STRING_CONST("SOLVE_INT"), expr_eval_solve_int, NULL, 0 }));
}
