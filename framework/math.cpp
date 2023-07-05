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
