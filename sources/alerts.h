/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 * 
 * The alert module is used to display alerts to the user when a given condition is met.
 * We use expressions to determine if an alert condition is met.
 */

#pragma once

#include <foundation/types.h>

/*! Make the alerts configuration window visible. */
void alerts_show_window();

/*! Render alerts notification main menu elements */
void alerts_notification_menu();

/*! Add an alert when the price of the specified title reaches the specified price. 
 * 
 *  @param title The title of the game to watch for.
 *  @param title_length The length of the title string.
 *  @param price The price to watch for.
 * 
 *  @return True if the alert was added successfully, false otherwise.
 */
bool alerts_add_price_increase(const char* title, size_t title_length, double price);

/*! Add an alert when the price of the specified title drops below the specified price. 
 * 
 *  @param title The title of the game to watch for.
 *  @param title_length The length of the title string.
 *  @param price The price to watch for.
 * 
 *  @return True if the alert was added successfully, false otherwise.
 */
bool alerts_add_price_decrease(const char* title, size_t title_length, double price);
