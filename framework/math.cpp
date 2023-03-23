/*
 * Copyright 2022 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
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

double math_average_parallel(const double* pn, size_t count, size_t stride /*= sizeof(double)*/)
{
    double total = 0;
    #pragma omp parallel for reduction(+:total)
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

