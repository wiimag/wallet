/*
 * Copyright 2022 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 */

#include "expr.h"

#include "math.h"

#include <foundation/array.h>
#include <foundation/string.h>

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

void eval_register_vec_mat_functions(expr_func_t*& funcs)
{
    const size_t VECMAT_CONTEXT_SIZE = 0;//sizeof(vecmat_context_t); NOT USED YET

    static thread_local mat4 IDENTITY;
    bx::mtxIdentity(IDENTITY.f);
    expr_result_t EIDENTITY(&IDENTITY.f, sizeof(IDENTITY.f[0]), 16, EXPR_POINTER_ARRAY_FLOAT);
    eval_set_global_var("I", EIDENTITY.ptr, EIDENTITY.index);

    // vec2(0,0), vec3(0,0,0), vec4(0,0,0,1), q(0,0,0,1)
    array_push(funcs, (expr_func_t{ STRING_CONST("vec2"), expr_eval_vecmat_vec2, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("vec3"), expr_eval_vecmat_vec3, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("vec4"), expr_eval_vecmat_vec4, NULL, VECMAT_CONTEXT_SIZE }));
    array_push(funcs, (expr_func_t{ STRING_CONST("q"), expr_eval_vecmat_vec4, NULL, VECMAT_CONTEXT_SIZE }));

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
}
