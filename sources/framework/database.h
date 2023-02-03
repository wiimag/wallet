/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#pragma once

#include <framework/shared_mutex.h>

#include <foundation/array.h>
#include <foundation/hashtable.h>

template<typename T>
struct database
{
    size_t capacity;
    shared_mutex lock;

    T* element{ nullptr };
    hashtable64_t* hashes{ nullptr };
};
