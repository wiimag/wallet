/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

#include <framework/imgui.h>

#include <foundation/math.h>
#include <foundation/platform.h>

#if FOUNDATION_COMPILER_MSVC
// Disable C26495 warnings for this file
// https://docs.microsoft.com/en-us/cpp/code-quality/c26495?view=msvc-160
#pragma warning(push)
#pragma warning(disable: 26495)
#endif

#include <bx/math.h>

typedef struct vec2_t
{
    FOUNDATION_FORCEINLINE vec2_t() : x(0), y(0) {}
    FOUNDATION_FORCEINLINE vec2_t(std::nullptr_t) : x(NAN), y(NAN) {}
    FOUNDATION_FORCEINLINE vec2_t(float s) : x(s), y(s) {}
    FOUNDATION_FORCEINLINE vec2_t(int x, int y) : x((float)x), y((float)y) {}
    FOUNDATION_FORCEINLINE vec2_t(float x, float y) : x(x), y(y) {}
    FOUNDATION_FORCEINLINE vec2_t(double x, double y) : x((double)x), y((double)y) {}
    FOUNDATION_FORCEINLINE vec2_t(const ImVec2& iv2) : xy(iv2) {}
    FOUNDATION_FORCEINLINE vec2_t(const bx::Vec3& bxv3) : x(bxv3.x), y(bxv3.y) {}
    FOUNDATION_FORCEINLINE vec2_t(const float* p, unsigned length = UINT_MAX) : x(p[0]), y(length > 1 ? p[1] : 0) {}

    FOUNDATION_FORCEINLINE vec2_t(ImVec2&& iv2) : x(iv2.x), y(iv2.y) {}
    FOUNDATION_FORCEINLINE vec2_t(bx::Vec3&& bxv3) : vec2_t(bxv3.x, bxv3.y) {}

    FOUNDATION_FORCEINLINE operator bx::Vec3 () { return bx::Vec3(x, y, 0); }
    FOUNDATION_FORCEINLINE operator const bx::Vec3 () const { return bx::Vec3(x, y, 0); }

    FOUNDATION_FORCEINLINE operator ImVec2& () { return xy; }
    FOUNDATION_FORCEINLINE operator const ImVec2& () const { return xy; }

    FOUNDATION_FORCEINLINE operator float* () { return (float*)this; }
    FOUNDATION_FORCEINLINE operator const float* () { return (const float*)this; }

    FOUNDATION_FORCEINLINE float& operator[](unsigned index) { return components[index]; }
    FOUNDATION_FORCEINLINE constexpr float operator[](unsigned index) const { return components[index]; }

    union {
        struct {
            float x, y;
        };
        ImVec2 xy;
        struct {
            float i, j;
        };
        float components[2];
    };
} vec2;

typedef struct vec3_t
{
    FOUNDATION_FORCEINLINE vec3_t() : x(0), y(0), z(0) {}
    FOUNDATION_FORCEINLINE vec3_t(std::nullptr_t) : x(NAN), y(NAN), z(NAN) {}
    FOUNDATION_FORCEINLINE vec3_t(const float s) : x(s), y(s), z(s) {}
    FOUNDATION_FORCEINLINE vec3_t(float x, float y) : x(x), y(y), z(0) {}
    FOUNDATION_FORCEINLINE vec3_t(float x, float y, float z) : x(x), y(y), z(z) {}
    FOUNDATION_FORCEINLINE vec3_t(const vec2_t& v2) : x(v2.x), y(v2.y), z(0) {}
    FOUNDATION_FORCEINLINE vec3_t(const ImVec2& iv2) : x(iv2.x), y(iv2.y), z(0) {}
    FOUNDATION_FORCEINLINE vec3_t(const ImPlotPoint& iv2) : x(iv2.x), y(iv2.y), z(0) {}
    FOUNDATION_FORCEINLINE vec3_t(const bx::Vec3& bxv3) : x(bxv3.x), y(bxv3.y), z(bxv3.z) {}
    FOUNDATION_FORCEINLINE vec3_t(const float* p, unsigned length = UINT_MAX) : x(p[0]), y(length > 1 ? p[1] : 0), z(length > 2 ? p[2] : 0) {}

    template<typename U>
    FOUNDATION_FORCEINLINE vec3_t(U x, U y, U z) : x((float)x), y((float)y), z((float)z) {}
    
    FOUNDATION_FORCEINLINE vec3_t(ImVec2&& iv2) : x(iv2.x), y(iv2.y), z(0) {}
    FOUNDATION_FORCEINLINE vec3_t(bx::Vec3&& bxv3) : x(bxv3.x), y(bxv3.y), z(bxv3.z) {}

    FOUNDATION_FORCEINLINE operator bx::Vec3&() { return xyz; }
    FOUNDATION_FORCEINLINE operator const bx::Vec3&() const { return xyz; }

    FOUNDATION_FORCEINLINE operator ImVec2& () { return xy; }
    FOUNDATION_FORCEINLINE operator const ImVec2& () const { return xy; }

    FOUNDATION_FORCEINLINE operator vec2_t& () { return v2; }
    FOUNDATION_FORCEINLINE operator const vec2_t& () const { return v2; }

    FOUNDATION_FORCEINLINE operator float* () { return (float*)this; }
    FOUNDATION_FORCEINLINE operator const float* () { return (const float*)this; }

    FOUNDATION_FORCEINLINE float& operator[](unsigned index) { return components[index]; }
    FOUNDATION_FORCEINLINE constexpr float operator[](unsigned index) const { return components[index]; }

    FOUNDATION_FORCEINLINE bool operator !=(const vec3_t& o) const
    {
        if (!math_float_eq(x, o.x, 4))
            return true;
        if (!math_float_eq(y, o.y, 4))
            return true;
        if (!math_float_eq(z, o.z, 4))
            return true;
        return false;
    }

    FOUNDATION_FORCEINLINE vec3_t& operator +=(const vec3_t& o)
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }

    FOUNDATION_FORCEINLINE vec3_t& operator /=(const float v)
    {
        x /= v;
        y /= v;
        z /= v;
        return *this;
    }

    FOUNDATION_FORCEINLINE vec3_t& operator *=(const float v)
    {
        x *= v;
        y *= v;
        z *= v;
        return *this;
    }

    FOUNDATION_FORCEINLINE const int i() const { return math_floor(x); }
    FOUNDATION_FORCEINLINE const int j() const { return math_floor(y); }
    FOUNDATION_FORCEINLINE const int k() const { return math_floor(z); }

    union {
        struct { float x, y, z; };
        struct { float r, g, b; };
        struct { float h, s, v; };
        bx::Vec3 xyz{ 0,0,0 };
        ImVec2 xy;
        vec2_t v2;
        float components[3];
    };
} vec3;

typedef struct vec4_t
{
    union {
        struct {
            float x, y, z, w;
        };
        float components[4]{ 0, 0, 0, 1 };
        ImVec2 xy;
        ImVec4 xyzw;
        ImRect rect;
        ImColor color;
        bx::Vec3 xyz;
        bx::Quaternion q;
        bx::Plane plane;
        vec2_t v2;
        vec3_t v3;
        struct {
            float r, g, b, a;
        };
    };

    FOUNDATION_FORCEINLINE vec4_t() : vec4_t(0, 0, 0, 1) {}
    FOUNDATION_FORCEINLINE vec4_t(const float s) : x(s), y(s), z(s), w(s) {}
    FOUNDATION_FORCEINLINE vec4_t(float x, float y) : x(x), y(y), z(0), w(1) {}
    FOUNDATION_FORCEINLINE vec4_t(float x, float y, float z) : x(x), y(y), z(z), w(1) {}
    FOUNDATION_FORCEINLINE vec4_t(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    FOUNDATION_FORCEINLINE vec4_t(const ImVec2& iv2) : x(iv2.x), y(iv2.y), z(0), w(1) {}
    FOUNDATION_FORCEINLINE vec4_t(const ImVec4& iv4) : x(iv4.x), y(iv4.y), z(iv4.z), w(iv4.w) {}
    FOUNDATION_FORCEINLINE vec4_t(const ImColor& c) : vec4_t(c.Value) {}
    FOUNDATION_FORCEINLINE vec4_t(const bx::Vec3& bxv3) : vec4_t(bxv3.x, bxv3.y, bxv3.z) {}
    FOUNDATION_FORCEINLINE vec4_t(const bx::Quaternion& q) : vec4_t(q.x, q.y, q.z, q.w) {}
    FOUNDATION_FORCEINLINE vec4_t(const float* p, unsigned length = UINT_MAX) : x(p[0]), y(length > 1 ? p[1] : 0), z(length > 2 ? p[2] : 0), w(length > 3 ? p[3] : 1) {}
                           
    FOUNDATION_FORCEINLINE vec4_t(ImVec2&& iv2) : vec4_t(iv2.x, iv2.y) {}
    FOUNDATION_FORCEINLINE vec4_t(bx::Vec3&& bxv3) : vec4_t(bxv3.x, bxv3.y, bxv3.z) {}
    FOUNDATION_FORCEINLINE vec4_t(bx::Quaternion&& q) : vec4_t(q.x, q.y, q.z, q.w) {}

    FOUNDATION_FORCEINLINE operator ImVec2& () { return xy; }
    FOUNDATION_FORCEINLINE operator const ImVec2& () const { return xy; }

    FOUNDATION_FORCEINLINE operator bx::Vec3& () { return xyz; }
    FOUNDATION_FORCEINLINE operator const bx::Vec3& () const { return xyz; }

    FOUNDATION_FORCEINLINE operator bx::Quaternion& () { return q; }
    FOUNDATION_FORCEINLINE operator const bx::Quaternion& () const { return q; }

    FOUNDATION_FORCEINLINE operator float* () { return (float*)this; }
    FOUNDATION_FORCEINLINE operator const float* () { return (const float*)this; }

    FOUNDATION_FORCEINLINE float& operator[](unsigned index) { return components[index]; }
    FOUNDATION_FORCEINLINE constexpr float operator[](unsigned index) const { return components[index]; }

} vec4;

typedef struct mat4_t {
    union {
        struct {
            vec4 row1, row2, row3, row4;
        };
        struct {
            float m11, m12, m13, m14,
                  m21, m22, m23, m24,
                  m31, m32, m33, m34,
                  m41, m42, m43, m44;
        };
       
        float f[16];
    };

    FOUNDATION_FORCEINLINE mat4_t(){};
    FOUNDATION_FORCEINLINE mat4_t(const float* p) { memcpy((float*)this, p, sizeof(*this)); }
    FOUNDATION_FORCEINLINE mat4_t(float _m11, float _m12, float _m13, float _m14,
                                  float _m21, float _m22, float _m23, float _m24,
                                  float _m31, float _m32, float _m33, float _m34,
                                  float _m41, float _m42, float _m43, float _m44)
        : m11(_m11), m12(_m12), m13(_m13), m14(_m14),
          m21(_m21), m22(_m22), m23(_m23), m24(_m24),
          m31(_m31), m32(_m32), m33(_m33), m34(_m34),
          m41(_m41), m42(_m42), m43(_m43), m44(_m44)
    {
    }

    FOUNDATION_FORCEINLINE operator float* () { return (float*)this; }
    FOUNDATION_FORCEINLINE operator const float* () const { return (const float*)this; }

    FOUNDATION_FORCEINLINE vec4& operator[](unsigned r) { return *((vec4*)this + r); }
    FOUNDATION_FORCEINLINE const vec4& operator[](unsigned r) const { return *((const vec4*)this + r); }

} mat4;

// ## Helpers

/*! @brief Returns the given value if it is not NAN, otherwise returns the default value.
 *  @param n The value to check.
 *  @param default_value The default value to return if the given value is NAN.
 *  @return The given value if it is not NAN, otherwise returns the default value.
 */
FOUNDATION_FORCEINLINE double math_ifnan(const double n, const double default_value)
{
    if (math_real_is_finite(n))
        return n;
    return default_value;
}

/*! @brief Returns the given value if it is not zero or NAN, otherwise returns the default value.
 *  @param n The value to check.
 *  @param default_value The default value to return if the given value is zero.
 *  @return The given value if it is not zero, otherwise returns the default value.
 */
FOUNDATION_FORCEINLINE double math_ifzero(const double n, const double default_value)
{
    if (math_real_is_zero(n) || !math_real_is_finite(n))
        return default_value;
    return n;
}

/*! @brief Returns the given value if it is not negative or NAN, otherwise returns the default value.
 *  @param n The value to check.
 *  @param default_value The default value to return if the given value is negative.
 *  @return The given value if it is not negative, otherwise returns the default value.
 */
FOUNDATION_FORCEINLINE double math_ifneg(const double n, const double default_value)
{
    if (n <= 0 || !math_real_is_finite(n))
        return default_value;
    return n;
}

/*! @brief Returns the average of the given values.
 *  @param pn Pointer to the first value.
 *  @param count Number of values.
 *  @param stride Stride between values.
 *  @return The average of the given values.
*/
double math_average(const double* pn, size_t count, size_t stride = sizeof(double));

/*! @brief Returns the median and average of the given values.
 *  @param values Pointer to the first value.
 *  @param median The median of the given values.
 *  @param average The average of the given values.
 *  @return The median of the given values.
*/
double math_median_average(double* values, double& median, double& average);

// ## Scalar helpers

/*! @brief Returns the given value clamped to the given range.
 *  @param _v The value to clamp.
 *  @param _min The minimum value.
 *  @param _max The maximum value.
 *  @return The given value clamped to the given range.
*/
FOUNDATION_FORCEINLINE float clamp(float _v, float _min, float _max) { return _v < _min ? _min : _v > _max ? _max : _v; }

// ## Vector 2D helpers

FOUNDATION_FORCEINLINE vec2 add(const vec2& _a, const vec2& _b) { return vec2(_a.x + _b.x, _a.y + _b.y); }
FOUNDATION_FORCEINLINE vec2 add(const vec2& _a, const vec3& _b) { return vec2(_a.x + _b.x, _a.y + _b.y); }
FOUNDATION_FORCEINLINE vec2 add(const vec2& _a, const ImVec2& _b) { return vec2(_a.x + _b.x, _a.y + _b.y); }
FOUNDATION_FORCEINLINE vec2 sub(const vec2& _a, const vec2& _b) { return vec2(_a.x - _b.x, _a.y - _b.y); }
FOUNDATION_FORCEINLINE vec2 sub(const vec2& _a, const vec3& _b) { return vec2(_a.x - _b.x, _a.y - _b.y); }
FOUNDATION_FORCEINLINE vec2 sub(const vec2& _a, const ImVec2& _b) { return vec2(_a.x - _b.x, _a.y - _b.y); }
FOUNDATION_FORCEINLINE vec2 clamp(const vec2& v, const vec2& min, const vec2& max) { return vec2(clamp(v.x, min.x, max.x), clamp(v.y, min.y, max.y)); }
FOUNDATION_FORCEINLINE vec2 round(const vec2& v) { return vec2(math_round(v.x), math_round(v.y)); }

FOUNDATION_FORCEINLINE vec2 operator+ (const vec2& a, const vec2& b) { return add(a, b); }
FOUNDATION_FORCEINLINE vec2 operator+ (const vec2& a, const ImVec2& b) { return add(a, b); }
FOUNDATION_FORCEINLINE vec2 operator- (const vec2& a, const vec2& b) { return sub(a, b); }
FOUNDATION_FORCEINLINE vec2 operator- (const vec2& a, const ImVec2& b) { return sub(a, b); }
FOUNDATION_FORCEINLINE vec2 operator* (const vec2& a, const float v) { return vec2(a.x * v, a.y * v); }
FOUNDATION_FORCEINLINE vec2 operator/ (const vec2& a, const float v) { return vec2(a.x / v, a.y / v); }
FOUNDATION_FORCEINLINE vec2 operator/ (const vec2& a, const vec2& b) { return vec2(a.x / b.x, a.y / b.y); }

// ## Vector 3D helpers

FOUNDATION_FORCEINLINE vec3 add(const vec3& _a, const vec3& _b) { return bx::add(_a, _b); }
FOUNDATION_FORCEINLINE vec3 sub(const vec3& _a, const vec3& _b) { return bx::sub(_a, _b); }
FOUNDATION_FORCEINLINE vec3 mul(const vec3& _a, const float _b) { return bx::mul(_a, _b); }
FOUNDATION_FORCEINLINE vec3 mul(const vec3& _a, const vec3& _b) { return bx::mul(_a, _b); }
FOUNDATION_FORCEINLINE float dot(const vec3& _a, const vec3& _b) { return bx::dot(_a, _b); }
FOUNDATION_FORCEINLINE vec3 cross(const vec3& _a, const vec3& _b) { return bx::cross(_a, _b); }
FOUNDATION_FORCEINLINE vec3 normalize(const vec3& _v) { return bx::normalize(_v); }
FOUNDATION_FORCEINLINE float length(const vec3& _v) { return bx::length(_v); }
FOUNDATION_FORCEINLINE vec3 absolute(const vec3& _v) { return vec3(math_abs(_v.x), math_abs(_v.y), math_abs(_v.z)); }

FOUNDATION_FORCEINLINE float dot(const vec4& _a, const vec4& _b) { return bx::dot(_a.q, _b.q); }

FOUNDATION_FORCEINLINE vec3 operator+ (const vec3& a, const vec3& b) { return add(a, b); }
FOUNDATION_FORCEINLINE vec3 operator- (const vec3& a, const vec3& b) { return sub(a, b); }
FOUNDATION_FORCEINLINE vec3 operator* (const vec3& a, const vec3& b) { return mul(a, b); }
FOUNDATION_FORCEINLINE vec3 operator* (const vec3& a, const float v) { return mul(a, v); }
FOUNDATION_FORCEINLINE vec3 operator/ (const vec3& a, const float v) { return mul(a, 1.0f/v); }
FOUNDATION_FORCEINLINE vec3 operator- (const vec3& a) { return mul(a, -1.0f); }

// ## Matrix helpers

FOUNDATION_FORCEINLINE mat4 midentity() { mat4 m; bx::mtxIdentity(m); return m; }

FOUNDATION_FORCEINLINE mat4 mtranslate(float x, float y, float z) { mat4 m; bx::mtxTranslate(m, x, y, z); return m; }
FOUNDATION_FORCEINLINE mat4 mtranslate(const vec3& v) { mat4 m; bx::mtxTranslate(m, v.x, v.y, v.z); return m; }

FOUNDATION_FORCEINLINE mat4 mrotateX(float x) { mat4 m; bx::mtxRotateX(m, x); return m; }
FOUNDATION_FORCEINLINE mat4 mrotateY(float y) { mat4 m; bx::mtxRotateY(m, y); return m; }
FOUNDATION_FORCEINLINE mat4 mrotateZ(float z) { mat4 m; bx::mtxRotateZ(m, z); return m; }
FOUNDATION_FORCEINLINE mat4 mrotate(float x, float y) { mat4 m; bx::mtxRotateXY(m, x, y); return m; }
FOUNDATION_FORCEINLINE mat4 mrotate(float x, float y, float z) { mat4 m; bx::mtxRotateXYZ(m, x, y, z); return m; }
FOUNDATION_FORCEINLINE mat4 mrotateZYX(float x, float y, float z) { mat4 m; bx::mtxRotateZYX(m, x, y, z); return m; }

FOUNDATION_FORCEINLINE mat4 mscale(float _s) { mat4 m; bx::mtxScale(m, _s); return m; }
FOUNDATION_FORCEINLINE mat4 mscale(float _sx, float _sy, float _sz) { mat4 m; bx::mtxScale(m, _sx, _sy, _sz); return m; }
FOUNDATION_FORCEINLINE mat4 mscale(const vec3& _v) { mat4 m; bx::mtxScale(m, _v.x, _v.y, _v.z); return m; }

FOUNDATION_FORCEINLINE vec3 mul(const vec3& v, const mat4& m) { return bx::mul(v, m); }
FOUNDATION_FORCEINLINE vec3 mulH(const vec3& v, const mat4& m) { return bx::mulH(v, m); }
FOUNDATION_FORCEINLINE vec3 mulXyz0(const mat4& m, const vec3& v) { return bx::mulXyz0(v, m); }
FOUNDATION_FORCEINLINE mat4 mul(const mat4& a, const mat4& b) { mat4 r; bx::mtxMul(r, a, b); return r; }

FOUNDATION_FORCEINLINE mat4 transpose(const mat4& m) { mat4 r; bx::mtxTranspose(r, m); return r; }
FOUNDATION_FORCEINLINE mat4 inverse(const mat4& m) { mat4 r; bx::mtxInverse(r, m); return r; }

// ## Operators

/// <summary>
/// Checks if two vector are equivalent.
/// </summary>
/// <param name="a">Left vector</param>
/// <param name="b">Right vector</param>
/// <returns>True if they are equivalent</returns>
FOUNDATION_FORCEINLINE bool operator== (const vec3& a, const vec3& b)
{
    return math_float_eq(a.x, b.x, 100) && math_float_eq(a.y, b.y, 100) && math_float_eq(a.z, b.z, 100);
}

#if FOUNDATION_COMPILER_MSVC
#pragma warning (pop)
#endif
