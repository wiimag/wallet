/*
 * Copyright 2023 Infineis Inc. All rights reserved.
 * License: https://infineis.com/LICENSE
 * 
 * Testing helpers. Most helpers are written in UPPERCASE in order to blend 
 * well with doctest MACROS.
 */

#pragma once

#include <butils.h>

#include <framework/config.h>
#include <framework/common.h>

#include <foundation/math.h>
#include <foundation/string.h>

#include <doctest/doctest.h>

#include <iomanip>
#include <ostream>
#include <initializer_list>

extern void main_dispatcher();
extern GLFWwindow* main_test_window();
extern bool main_poll(GLFWwindow* window);
extern void main_process(GLFWwindow* window, app_render_handler_t render, app_render_handler_t begin, app_render_handler_t end);

/// <summary>
/// Output constant string to test printing text
/// </summary>
/// <param name="os">Stream</param>
/// <param name="value">String to print</param>
/// <returns>Stream</returns>
FOUNDATION_FORCEINLINE std::ostream& operator<< (std::ostream& os, const string_const_t& value)
{
    os.write(value.str, value.length);
    return os;
}

/// <summary>
/// 
/// </summary>
/// <param name="os"></param>
/// <param name="value"></param>
/// <returns></returns>
FOUNDATION_FORCEINLINE std::ostream& operator<< (std::ostream& os, const vec3& value)
{
    if (length(value) < 2.0f)
        os << std::setprecision(4);
    else
        os << std::setprecision(6);
    os << "[" << value.x << ", " << value.y << ", " << value.z << "]";
    return os;
}

FOUNDATION_FORCEINLINE std::ostream& operator<< (std::ostream& os, const std::initializer_list<float>& value)
{
    os << "[";
    size_t i = 0;
    for (auto f : value)
    {
        if (i > 0)
            os << ", ";
        if (f <= 1.0f) os << std::setprecision(4); else os << std::setprecision(6);
        os << f;
        i++;
    }
    os << "]";
    return os;
}

template <size_t N> FOUNDATION_FORCEINLINE
std::ostream& operator<< (std::ostream& os, const float(&value)[N])
{
    os << "[";
    for (size_t i = 0; i < N; ++i)
    {
        if (i > 0)
            os << ", ";
        if (value[i] <= 1.0f) os << std::setprecision(4); else os << std::setprecision(6);
        os << value[i];
    }
    os << "]";
    return os;
}

/// <summary>
/// Compare two constant string by ignoring whitespaces.
/// This is used by the testing framework to simplify REQUIRE_EQ(...)
/// </summary>
/// <param name="a">Left string</param>
/// <param name="b">Right string</param>
/// <returns>True if strings are equivalent</returns>
FOUNDATION_FORCEINLINE bool operator== (const string_const_t& a, const string_const_t& b)
{
    return string_equal_ignore_whitespace(STRING_ARGS(a), STRING_ARGS(b));
}

/// <summary>
/// Compare a const string with a constant string literal.
/// </summary>
/// <param name="a">Left string</param>
/// <param name="b">Right string</param>
/// <returns>True if strings are equivalent</returns>
FOUNDATION_FORCEINLINE bool operator== (const string_const_t& a, const char* FOUNDATION_RESTRICT b)
{
    return string_equal_ignore_whitespace(STRING_ARGS(a), b, string_length(b));
}

FOUNDATION_FORCEINLINE bool operator== (const vec3& a, const std::initializer_list<float>& _b)
{
    CHECK_LE(_b.size(), 3);

    if (_b.size() == 0)
        return math_float_is_zero(a.x) && math_float_is_zero(a.y) && math_float_is_zero(a.y);

    const float* b = _b.begin();
    if (_b.size() == 3)
        return math_float_eq(a.x, b[0], 4) && math_float_eq(a.y, b[1], 4) && math_float_eq(a.z, b[2], 4);

    if (_b.size() == 2)
        return math_float_eq(a.x, b[0], 4) && math_float_eq(a.y, b[1], 4);

    return math_float_eq(a.x, b[0], 4) && math_float_eq(a.y, b[0], 4) && math_float_eq(a.z, b[0], 4);
}

template <size_t N> FOUNDATION_FORCEINLINE
bool operator== (const vec3& a, const float(&b)[N])
{
    CHECK_LE(N, 3);

    if (N == 0)
        return math_float_is_zero(a.x) && math_float_is_zero(a.y) && math_float_is_zero(a.y);

    if (N == 3)
        return math_float_eq(a.x, b[0], 4) && math_float_eq(a.y, b[1], 4) && math_float_eq(a.z, b[2], 4);

    if (N == 2)
        return math_float_eq(a.x, b[0], 4) && math_float_eq(a.y, b[1], 4);

    return math_float_eq(a.x, b[0], 4) && math_float_eq(a.y, b[0], 4) && math_float_eq(a.z, b[0], 4);
}

/// <summary>
/// Simulate a click on the specified item using its label.
/// </summary>
/// <param name="label">Item label to find</param>
void CLICK_UI(const char* label);

/// <summary>
/// Checks if a given item was drawn in the last frame.
/// </summary>
/// <param name="label">Item label to find</param>
void REQUIRE_UI(const char* label);

/// <summary>
/// Checks if a given item is missing from the last frame.
/// </summary>
/// <param name="label">Item label to be missing</param>
void REQUIRE_UI_FALSE(const char* label);

/// <summary>
/// Renders a UI frame. Once the frame is rendered, you can assert for 
/// specific conditions that happens in the drawn frame.
/// </summary>
/// <param name="render_callback">Callback to render the frame</param>
/// <param name="test_event_callback">Callback to simulate UI events on the frame to be rendered.</param>
void TEST_RENDER_FRAME(const function<void()>& render_callback, const function<void()>& test_event_callback = nullptr);

/// <summary>
/// Clear UI states from the last call to @TEST_RENDER_FRAME
/// </summary>
void TEST_CLEAR_FRAME();

void REQUIRE_WAIT(bool* watch_var, double timeout_seconds = 5.0);
