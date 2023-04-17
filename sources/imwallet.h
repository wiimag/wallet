/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 * 
 * Module containing various Wallet specific ImGui widgets.
 */

#pragma once

#include "logo.h"

#include <framework/imgui.h>

namespace ImWallet {

/*! Draw a list of stock exchanges.
 * 
 *  The user can select multiple stock exchanges within the list.
 *  
 *  @param[in,out] stock_exchanges A string array containing the stock exchanges.
 * 
 *  @return True if the list has changes, false otherwise.
 */
bool Exchanges(string_t*& stock_exchanges);

} // namespace ImWallet
