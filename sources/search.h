/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */

#pragma once

#include <foundation/string.h>

/*! Returns true if the search service is ready to be used. */
bool search_available();

/*! Render search settings. 
 * 
 *  @returns True if the settings changed and the search service needs to be restarted.
 */
bool search_render_settings();

/*! Returns the list of stock exchanges for which the search service is configured. 
 *
 *  @returns An array string contains stock markets symbols.
 */
const string_t* search_stock_exchanges();

/*! Renders a search view that is shared globally. */
void search_render_global_view();
