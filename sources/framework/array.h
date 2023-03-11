/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * Array helper extensions on top of the foundation_lib <foundation/array.h>
 */

#pragma once

#include <framework/function.h>

#include <foundation/array.h>

 /*! @def foreach 
  * 
  *  Iterate over an array created with any array_* APIs.
  * 
  *  @note The variable #i declared as #unsigned is available as the 0 based index of the current element.
  * 
  *  @important The param #_VAR_NAME leaks outside the scope of the loop.
  *             So if you have many #foreach in the same scope make sure to 
  *             define a unique name for each one.
  * 
  *  @param _VAR_NAME The name of the variable to use for the current element.
  *  @param _ARR      The array to iterate over.
  * 
  *  @example 
  *     int* numbers = nullptr;
  *     array_push(numbers, 10);
  *     array_push(numbers, 20);
  *     array_push(numbers, 30);
  * 
  *     foreach(n, numbers) {
  *         printf("%u: %d", i, n);
  *     }
  * 
  *     printf("last number printed %d since `n` leaks outside the foreach", n);
  * 
  *     // Output: 123
  * 
  *     array_deallocate(numbers);
  *      
  */
#define foreach(_VAR_NAME, _ARR) \
    std::remove_reference<decltype(_ARR)>::type _VAR_NAME = array_size(_ARR) > 0 ? &_ARR[0] : nullptr; \
    for (unsigned i = 0, end = array_size(_ARR); i < end; _VAR_NAME = &_ARR[++i])

/*! \def foreach_reverse
 * 
 *  Iterate over an array created with any array_* APIs in reverse order.
 * 
 *  @see #foreach
 * 
 *  @param _VAR_NAME The name of the variable to use for the current element.
 *  @param _ARR      The array to iterate over.
 */
#define foreach_reverse(_VAR_NAME, _ARR) \
    std::remove_reference<decltype(_ARR)>::type _VAR_NAME = array_size(_ARR) > 0 ? &_ARR[array_size(_ARR) - 1] : nullptr; \
    for (unsigned i = array_size(_ARR) - 1; i != UINT32_MAX; _VAR_NAME = &_ARR[--i])

/*! @def ARRAY_LESS_BY
 * 
 *  @brief Create a lambda expression that compares two elements of an array by a field.
 *       The lambda expression can be used with #array_sort.
 * 
 *  @param FIELD The field to compare by.
 * 
 *  @example
 *      struct Foo {
 *          int a;
 *          int b;
 *      }
 *      
 *      Foo foos[2];
 *      foos[0].a = 1;
 *      foos[0].b = 2;
 *      foos[1].a = 3;
 *      foos[1].b = 4;
 * 
 *      array_sort(foos, ARRAY_LESS_BY(a));
 */
#define ARRAY_LESS_BY(FIELD) [](const auto& a, const auto& b) { \
        const auto& va = a.FIELD; \
        const auto& vb = b.FIELD; \
        return va < vb ? -1 : va > vb ? 1 : 0; \
    }

/*! @def ARRAY_GREATER_BY
 * 
 *  @brief Create a lambda expression that compares two elements of an array by a field.
 * 
 *  @param FIELD The field to compare by.
 * 
 *  @see #ARRAY_LESS_BY for more info
 */
#define ARRAY_GREATER_BY(FIELD) [](const auto& a, const auto& b) { \
        const auto& va = a.FIELD; \
        const auto& vb = b.FIELD; \
        return va > vb ? -1 : va < vb ? 1 : 0; \
    }

/*! @def ARRAY_COMPARE_EXPRESSION
 * 
 *  @brief Create a lambda expression that compares two elements of an array by an expression.
 *  
 *  @param EXPRESSION The expression to compare by.
 *  
 *  @example
 *      struct Foo {
 *          int a;
 *      }
 * 
 *      Foo foos[2];
 *      foos[0].a = 1;
 *      foos[1].a = 3;
 * 
 *      array_sort(foos, ARRAY_COMPARE_EXPRESSION(a.a - b.a));
 */
#define ARRAY_COMPARE_EXPRESSION(EXPRESSION) [](const auto& a, const auto& b){ return EXPRESSION; }

 /*! Checks if an array is empty. */
template<typename T>
FOUNDATION_FORCEINLINE bool array_empty(const T* arr)
{
    return array_size(arr) == 0;
}

/*! Returns the first element of an array. */
template<typename T>
FOUNDATION_FORCEINLINE T* array_first(T* arr)
{
    return array_size(arr) > 0 ? &arr[0] : nullptr;
}

/*! Returns the last element of an array or nullptr if empty. */
template<typename T> T* array_last(T* arr)
{
    const size_t count = array_size(arr);
    if (count == 0)
        return nullptr;
    return &arr[count - 1];
}

/*! Returns the element index offset from the start of the array. 
 * 
 *  @param arr      The array to get the offset from.
 *  @param element  The element to get the offset of.
 * 
 *  @return The offset of the element from the start of the array.
 */
template<typename T> size_t array_offset(const T* arr, const T* element)
{
    return pointer_diff(element, arr) / sizeof(T);
}

/*! Sort a fixed size array.
 *
 *  @param arr              The array to sort.
 *  @param element_count    The number of elements to sort. If SIZE_MAX, the entire array is sorted.
 *  @param comparer         The comparer to use for sorting.
 *
 *  @note The comparer must be a function that takes two arguments of type T and returns an int.
 *        The return value should be -1 if the first argument is less than the second, 1 if the first
 *        argument is greater than the second, and 0 if the arguments are equal.
 *
 *  @example
 *      int numbers[3] = { 3, 2, 1 };
 *      array_sort(numbers, [](int a, int b) { return a < b ? -1 : a > b ? 1 : 0; });
 *      // numbers is now { 1, 2, 3 }
 */
template<typename T, size_t N, typename Comparer>
void array_sort(T (&arr)[N], size_t element_count, const Comparer& comparer)
{
    if (element_count == SIZE_MAX)
        element_count = N;

    constexpr auto comparer_wrapper = [](void* context, const void* va, const void* vb)
    {
        const auto& comparer = *(const Comparer*)context;
        const T& a = *(const T*)va;
        const T& b = *(const T*)vb;
        return comparer(a, b);
    };

    #if FOUNDATION_PLATFORM_WINDOWS
    qsort_s(arr, element_count, sizeof(T), comparer_wrapper, (void*)&comparer);
    #else
    qsort_r(arr, element_count, sizeof(T), (void*)&comparer, comparer_wrapper);
    #endif
}

/*! Sort a dynamic array (i.e. allocated with #array_push)
 *
 *  @param arr              The array to sort.
 *  @param comparer         The comparer to use for sorting.
 *
 *  @note The comparer must be a function that takes two arguments of type T and returns an int.
 *        The return value should be -1 if the first argument is less than the second, 1 if the first
 *        argument is greater than the second, and 0 if the arguments are equal.
 *
 *  @example
 *      array_sort(numbers, [](int a, int b) { return a < b ? -1 : a > b ? 1 : 0; });
 */
template<typename T>
T array_sort(T arr, const function<int(const typename std::remove_pointer<T>::type& a, const typename std::remove_pointer<T>::type& b)>& comparer)
{
    constexpr auto comparer_wrapper = [](void* context, const void* va, const void* vb)
    {
        const auto& comparer = *(function<int(const typename std::remove_pointer<T>::type & a, const typename std::remove_pointer<T>::type & b)>*)context;
        const typename std::remove_pointer<T>::type& a = *(const typename std::remove_pointer<T>::type*)va;
        const typename std::remove_pointer<T>::type& b = *(const typename std::remove_pointer<T>::type*)vb;
        return comparer(a, b);
    };
    
    #if FOUNDATION_PLATFORM_WINDOWS
    qsort_s(arr, array_size(arr), sizeof(typename std::remove_pointer<T>::type), comparer_wrapper, (void*)&comparer);
    #else
    qsort_r(arr, array_size(arr), sizeof(typename std::remove_pointer<T>::type), (void*)&comparer, comparer_wrapper);
    #endif
    
    return arr;
}

/*! Sort a dynamic array directly using standard library qsort callback signature. 
 * 
 *  @param arr              The array to sort.
 *  @param comparer         The comparer to use for sorting.
 *  @param context          The context to pass to the #qsort comparer.
 * 
 *  @note The comparer must be a function that takes two arguments of type T and returns an int.
 * 
 *  @example 
 *      array_sort(numbers, [](void* context, const int* a, const int* b) { return *a < *b ? -1 : *a > *b ? 1 : 0; }, nullptr);
 */
template<typename T, typename Comparer>
T* array_qsort(T* arr, unsigned element_count, const Comparer& comparer, void* context)
{
    #if FOUNDATION_PLATFORM_WINDOWS
    qsort_s(arr, array_size(arr), sizeof(T), comparer, context);
    #else
    qsort_r(arr, array_size(arr), sizeof(T), context, comparer);
    #endif
    return arr;
}

/*! Simplified version of #array_sort that uses the default comparison operator for the type.
 *
 *  @param arr              The array to sort.
 *
 *  @example
 *      int* numbers = numbers_allocated_with_array_push();
 *      array_sort(numbers);
 *      // numbers sort in ascending order
 */
template<typename T>
T array_sort(T arr)
{
    return array_sort(arr, [](const auto& a, const auto& b)
    {
        return a < b ? -1 : a > b ? 1 : 0;
    });
}

/*! Checks if a dynamic array contains a given element.
 *
 *  @param arr              The array to search.
 *  @param v                The value to search for.
 *  @param compare_equal    The comparison function to use.
 *
 *  @return True if the array contains the value, false otherwise.
 *
 *  @note The comparison function must be a function that takes two arguments of type T and returns a bool.
 *        The return value should be true if the arguments are equal, false otherwise.
 *
 *  @example
 *      FOUNDATION_ASSERT(array_contains(numbers, 2, [](int a, int b) { return a == b; }));
 */
template<typename T, typename U, typename Compare>
bool array_contains(const T* arr, const U& v, const Compare& compare_equal)
{
    for (unsigned i = 0, end = array_size(arr); i < end; ++i)
    {
        if (compare_equal(arr[i], v))
            return true;
    }

    return false;
}

/*! Simplified version of #array_contains that uses the default comparison operator for the type.
 * 
 *  @template T             The type of the array elements.
 *  @template U             The type of the value to search for.
 * 
 *  @param arr              The array to search.
 *  @param v                The value to search for.
 * 
 *  @note A global comparator must be defined to compare the type array element type #T with the value type #U.
 *
 *  @return True if the array contains the value, false otherwise.
 *
 *  @example
 *      FOUNDATION_ASSERT(array_contains(numbers, 2));
 */
template<typename T, typename U>
bool array_contains(const T* arr, const U& v)
{
    return array_contains(arr, v, [](const T& a, const U& b) { return a == b; });
}

/*! Executes a binary search on #array to find the insertion index of #_key.
 *
 *  @template T             The type of the array elements.
 *  @template V             The type of the key to search for.
 *
 *  @param array            The array to search.
 *  @param _num             The number of elements in the array.
 *  @param _key             The key to search for.
 *
 *  @return The insertion index of the key in the array. If the key is already present in the array, the index of the first occurrence is returned.
 *          If the key is not present in the array, the bitwise complement of the index of the first element greater than the key is returned.
 *
 *  @note Global comparators < and > must be defined to compare the type array element type #T with the key type #V.
 *
 *  @example
 *      int numbers[] = { 1, 2, 3, 4, 5 };
 *      FOUNDATION_ASSERT(array_binary_search(numbers, 5, 3) == 2);
 *      FOUNDATION_ASSERT(array_binary_search(numbers, 5, 6) == ~5);
 */
template<typename T, typename V>
int array_binary_search(const T* array, uint32_t _num, const V& _key)
{
    uint32_t offset = 0;
    for (uint32_t ll = _num; offset < ll;)
    {
        const uint32_t idx = (offset + ll) / 2;

        const T& mid_value = array[idx];
        if (mid_value > _key)
            ll = idx;
        else if (mid_value < _key)
            offset = idx + 1;
        else
            return idx;
    }

    return ~offset;
}

/*! Executes a binary search on a dynamic array for which the size if determined by #array_size. 
 *  
 *  @remark The array must be sorted in ascending order.
 *
 *  @template T             The type of the array elements.
 *  @template V             The type of the key to search for.
 *
 *  @param array            The array to search.
 *  @param _key             The key to search for.
 *
 *  @return The insertion index of the key in the array. If the key is already present in the array, the index of the first occurrence is returned.
 *          If the key is not present in the array, the bitwise complement of the index of the first element greater than the key is returned.
 *
 *  @note Global comparators < and > must be defined to compare the type array element type #T with the key type #V.
 *
 *  @example
 *      int* numbers = numbers_allocated_with_array_push();
 *      FOUNDATION_ASSERT(array_binary_search(numbers, 3) == 2);
 *      FOUNDATION_ASSERT(array_binary_search(numbers, 6) == ~5);
 */
template<typename T, typename V>
int array_binary_search(const T* array, const V& _key)
{
    return array_binary_search<T, V>(array, array_size(array), _key);
}

/*! Executes a binary search on a dynamic array to find the insertion index of #_key using a custom comparator.
 *
 *  @remark The array must be sorted in ascending order.
 *
 *  @template T             The type of the array elements.
 *  @template V             The type of the key to search for.
 *  @template Comparer      The type of the comparator function.
 *
 *  @param array            The array to search.
 *  @param _key             The key to search for.
 *  @param compare          The comparator function to use.
 *
 *  @return The insertion index of the key in the array. If the key is already present in the array, the index of the first occurrence is returned.
 *          If the key is not present in the array, the bitwise complement of the index of the first element greater than the key is returned.
 *
 *  @note The comparator function must be a function that takes two arguments of type T and returns an int.
 *        The return value should be negative if the first argument is less than the second, positive if the first argument is greater than the second,
 *        and zero if the arguments are equal.
 *
 *  @example
 *      int numbers[] = { 1, 2, 3, 4, 5 };
 *      FOUNDATION_ASSERT(array_binary_search_compare(numbers, 5, 3, [](int a, int b) { return a - b; }) == 2);
 *      FOUNDATION_ASSERT(array_binary_search_compare(numbers, 5, 6, [](int a, int b) { return a - b; }) == ~5);
 */
template<typename T, typename V, typename Comparer>
int array_binary_search_compare(const T array, const V& _key, Comparer compare)
{
    uint32_t offset = 0;
    for (uint32_t ll = array_size(array); offset < ll;)
    {
        const uint32_t idx = (offset + ll) / 2;

        const typename std::remove_pointer<T>::type& mid_value = array[idx];
        const int cmp = compare(mid_value, _key);
        if (cmp > 0)
            ll = idx;
        else if (cmp < 0)
            offset = idx + 1;
        else
            return idx;
    }

    return ~offset;
}
