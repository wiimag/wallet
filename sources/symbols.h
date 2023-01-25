/*
 * Copyright 2022-2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#pragma once

#include "stock.h"

#include "framework/common.h"
#include "framework/string_table.h"

#include <foundation/math.h>
#include <foundation/string.h>

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

void symbols_render(const char* market, bool filter_null_isin = true);
void symbols_render_search(const function<void(string_const_t)>& selector = nullptr);

void symbols_initialize();
void symbols_shutdown();
