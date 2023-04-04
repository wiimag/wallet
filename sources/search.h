/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#pragma once

/*! Returns true if the search service is ready to be used. */
bool search_available();

/*! Render search settings. 
 * 
 *  @returns True if the settings changed and the search service needs to be restarted.
 */
bool search_render_settings();
