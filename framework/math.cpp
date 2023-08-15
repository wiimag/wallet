/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "math.h"

#include <foundation/array.h>

#include <numeric>
#include <algorithm>

double math_average(const double* pn, size_t count, size_t stride /*= sizeof(double)*/)
{
    double total = 0;
    for (size_t i = 0; i < count; ++i)
    {
        total += math_ifnan(*pn, 0.0);
        pn = (double*)(((uint8_t*)pn) + stride);
    }

    return total / count;
}

double math_trend(double* x, double* y, size_t count, size_t stride, double* b, double *a)
{
    double sum_x = 0.0;
    double sum_y = 0.0;
    double sum_xx = 0.0;
    double sum_xy = 0.0;
    double sum_yy = 0.0;

    for (size_t i = 0; i < count; ++i)
    {
        const double xi = *x;
        const double yi = *y;
        sum_x += xi;
        sum_y += yi;
        sum_xx += xi * xi;
        sum_xy += xi * yi;
        sum_yy += yi * yi;
        x = (double*)(((uint8_t*)x) + stride);
        y = (double*)(((uint8_t*)y) + stride);
    }

    const double n = (double)count;
    const double det = n * sum_xx - sum_x * sum_x;
    if (det == 0.0)
    {
        *b = 0.0;
        *a = 0.0;
        return 0.0;
    }

    *b = (sum_xx * sum_y - sum_x * sum_xy) / det;
    *a = (n * sum_xy - sum_x * sum_y) / det;

    const double r = (n * sum_xy - sum_x * sum_y) / sqrt((n * sum_xx - sum_x * sum_x) * (n * sum_yy - sum_y * sum_y));
    return r;
}

double math_median_average(double* values, double& median, double& average)
{
    const size_t count = array_size(values);
    if (count == 0)
    {
        median = 0;
        average = 0;
        return 0;
    }

    const size_t n = count / 2;
    double* value_end = values + count;
    std::nth_element(values, values + n, value_end);
    median = values[n];

    double total = std::accumulate(values, value_end, 0.0);
    average = total / count;
    return (median + average) / 2.0;
}

float math_cosine_similarity(const float* em1, const float* em2)
{
    FOUNDATION_ASSERT(array_size(em1) == array_size(em2));
    float dot = 0.0f;
    float mag1 = 0.0f;
    float mag2 = 0.0f;
    for (unsigned i = 0, count = array_size(em1); i < count; ++i)
    {
        dot += em1[i] * em2[i];
        mag1 += em1[i] * em1[i];
        mag2 += em2[i] * em2[i];
    }

    return dot / (sqrtf(mag1) * sqrtf(mag2));
}

double math_array_min(const double* values, size_t count)
{
    double min = values[0];
    for (size_t i = 1; i < count; ++i)
    {
        min = math_min(min, values[i]);
    }

    return min;
}

double math_array_max(const double* values, size_t count)
{
    double max = values[0];
    for (size_t i = 1; i < count; ++i)
    {
        max = math_max(max, values[i]);
    }

    return max;
}
