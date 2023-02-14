/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */
 
#pragma once

#include "stock.h"

#include "framework/string_table.h"

struct exchange1_t
{
    string_table_symbol_t name;
    string_table_symbol_t code;
    string_table_symbol_t country;
    string_table_symbol_t currency;
};

struct bulk1_t
{
    time_t date;
    string_table_symbol_t code;

    string_table_symbol_t name;
    string_table_symbol_t type;
    string_table_symbol_t exchange;

    double market_capitalization;
    double beta;
    double open;
    double high;
    double low;
    double close;
    double adjusted_close;
    double volume;
    double ema_50d;
    double ema_200d;
    double hi_250d;
    double lo_250d;
    double avgvol_14d;
    double avgvol_50d;
    double avgvol_200d;

    stock_handle_t stock_handle;
    
    bool selected{ false };
    double_option_t today_cap{ NAN };
};
