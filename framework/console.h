/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://wiimag.com/LICENSE
 * 
 * System logger to show every log entries.
 */

#pragma once

#include <foundation/platform.h>

/*! Clear the console logs. */
void console_clear();

/*! Open and show the console window. */
void console_show();

/*! Hide the console window. */
void console_hide();

/*! Set the console expression. 
 * 
 * @param expression        The expression to be set in the console expression input field.
 * @param expression_length The length of the expression.
 */
void console_set_expression(const char* expression, size_t expression_length);

/*! Add a secret key token to be replaced with *** in the console message window.
 * 
 *  @param key        Key to be hidden.
 *  @param key_length Key length.
 */
void console_add_secret_key_token(const char* key, size_t key_length);

