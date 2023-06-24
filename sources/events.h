/*
 * Copyright 2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 *
 * Declare application message events
 */

#pragma once

/*! Posted when a case was updated. */
constexpr const char EVENT_REPORT_UPDATED[] = "REPORT_UPDATED";

/*! Posted when a new stock is being requested by the Stock module. */
constexpr const char EVENT_STOCK_REQUESTED[] = "STOCK_REQUESTED";

/*! Posted when the search database is loaded from disk and ready to be used. */
constexpr const char EVENT_SEARCH_DATABASE_LOADED[] = "SEARCH_DATABASE_LOADED";

/*! Posted when the search query is updated. */
constexpr const char EVENT_SEARCH_QUERY_UPDATED[] = "SEARCH_QUERY_UPDATED";
