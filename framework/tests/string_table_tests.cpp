/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * String table tests
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT

#include "test_utils.h"

#include <framework/config.h>
#include <framework/common.h>
#include <framework/string_table.h>

TEST_SUITE("String Table")
{
    TEST_CASE("Allocate")
    {
        auto st = string_table_allocate();
        REQUIRE(string_table_is_valid(st));

        CHECK_EQ(st->free_slots, nullptr);
        CHECK_EQ(st->uses_16_bit_hash_slots, 1);

        string_table_deallocate(st);
    }

    TEST_CASE("Add and remove strings")
    {
        // Create string table
        auto st = string_table_allocate();

        // Add strings
        auto str1 = string_table_add_symbol(st, STRING_CONST("Hello"));
        CHECK_EQ(str1, 1);

        auto str2 = string_table_add_symbol(st, STRING_CONST("World"));
        CHECK_EQ(str2, 7);

        auto str3 = string_table_add_symbol(st, STRING_CONST("This string will be deleted"));
        CHECK_EQ(str3, 13);

        auto str4 = string_table_add_symbol(st, STRING_CONST("Jim is back!"));
        CHECK_EQ(str4, 41);

        auto str5 = string_table_add_symbol(st, STRING_CONST("Not the Terminator :("));
        CHECK_EQ(str5, 54);

        // Check that the strings are in the table
        CHECK_EQ(string_table_to_string_const(st, str1), CTEXT("Hello"));
        CHECK_EQ(string_table_to_string_const(st, str2), CTEXT("World"));
        CHECK_EQ(string_table_to_string_const(st, str3), CTEXT("This string will be deleted"));
        CHECK_EQ(string_table_to_string_const(st, str4), CTEXT("Jim is back!"));
        CHECK_EQ(string_table_to_string_const(st, str5), CTEXT("Not the Terminator :("));

        // Remove a string
        CHECK(string_table_remove_symbol(st, str3));

        // Add two small string that should use the free slots.
        auto str6 = string_table_add_symbol(st, STRING_CONST("AA"));
        CHECK_EQ(str6, 13);

        auto str7 = string_table_add_symbol(st, STRING_CONST("JJJ"));
        CHECK_EQ(str7, 16);

        // Add a bigger string but less than 23 characters
        auto str8 = string_table_add_symbol(st, STRING_CONST("This is a new string"));
        CHECK_EQ(str8, 20);

        CHECK_EQ(string_table_to_string_const(st, str1), CTEXT("Hello"));
        CHECK_EQ(string_table_to_string_const(st, str2), CTEXT("World"));
        CHECK_NE(string_table_to_string_const(st, str3), CTEXT("This string will be deleted"));
        CHECK_EQ(string_table_to_string_const(st, str4), CTEXT("Jim is back!"));
        CHECK_EQ(string_table_to_string_const(st, str5), CTEXT("Not the Terminator :("));
        CHECK_EQ(string_table_to_string_const(st, str6), CTEXT("AA"));
        CHECK_EQ(string_table_to_string_const(st, str7), CTEXT("JJJ"));
        CHECK_EQ(string_table_to_string_const(st, str8), CTEXT("This is a new string"));

        string_table_deallocate(st);
    }
}

#endif // BUILD_DEVELOPMENT
