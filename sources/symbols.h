/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */
 
#pragma once

#include "stock.h"

#include "framework/common.h"
#include "framework/string_table.h"

#include <foundation/math.h>
#include <foundation/string.h>

/// <summary>
/// Stock symbol information used in the symbol exchange view.
/// </summary>
struct symbol_t
{
    string_table_symbol_t code{};
    string_table_symbol_t name{};
    string_table_symbol_t country{};
    string_table_symbol_t exchange{};
    string_table_symbol_t currency{};
    string_table_symbol_t type{};
    string_table_symbol_t isin{};
    double price{ DNAN };

    stock_handle_t stock{};

    bool viewed{ false };
};

/// <summary>
/// 
/// </summary>
/// <param name="market"></param>
/// <param name="filter_null_isin"></param>
void symbols_render(const char* market, bool filter_null_isin = true);

/// <summary>
/// 
/// </summary>
/// <param name="selector"></param>
bool symbols_render_search(const function<void(string_const_t)>& selector = nullptr);
