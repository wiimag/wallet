/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#pragma once

#include <framework/table.h>

typedef function<void(table_element_ptr_t element, const table_cell_t& cell, const table_column_t* column, int index)> table_expr_drawer_t;

/*! Initialize and register table expression functions */
void table_expr_initialize();

/*! Shutdown and unregister table expression functions */
void table_expr_shutdown();

/*! Register a drawer for a type. 
 *
 *  @param type The type to register the drawer for.
 *  @param length The length of the type.
 *  @param drawer The drawer to register.
 */
void table_expr_add_type_drawer(const char* type, size_t length, const table_expr_drawer_t& drawer);
