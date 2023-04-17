/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT

#include "test_utils.h"

#include <framework/config.h>
#include <framework/common.h>

#include <doctest/doctest.h>

TEST_SUITE("Configuration")
{
    TEST_CASE("Allocate/Deallocate")
    {
        auto cv = config_allocate(CONFIG_VALUE_OBJECT);
        REQUIRE(config_is_valid(cv));

        config_deallocate(cv);
        REQUIRE(!config_is_valid(cv));
    }

    TEST_CASE("Basic Object")
    {
        config_handle_t cv = config_allocate();
        CHECK(config_value_type(cv) == CONFIG_VALUE_OBJECT);

        config_handle_t titles = config_add(cv, STRING_CONST("titles"));
        config_handle_t symbols = config_set_array(cv, STRING_CONST("symbols"));
        config_array_push(symbols, STRING_CONST("U.US"));
        config_array_insert(symbols, 0, STRING_CONST("TNT-UN.TO"));
        config_array_insert(symbols, 4550, STRING_CONST("SSE.V"));
        config_set(titles, 42.72800000055);
        CHECK(42.728 == doctest::Approx(config_value_as_number(titles)).epsilon(0.001));

        config_sjson_const_t sjson = config_sjson(cv);
        string_const_t sjson_string = config_sjson_to_string(sjson);

        REQUIRE_EQ(sjson_string, R"({
             symbols = ["TNT-UN.TO" "U.US" "SSE.V"]
             titles = 42.728000000549997
        })");

        config_sjson_deallocate(sjson);
        config_deallocate(cv);
    }

    TEST_CASE("Save Escaped UTF-8 characters")
    {
        // Create config
        config_handle_t cv = config_allocate();
        CHECK(config_value_type(cv) == CONFIG_VALUE_OBJECT);

        // Add a string with escaped UTF-8 characters
        config_set_string(cv, STRING_CONST("string"), STRING_CONST("Hello \xE2\x98\xBA World!"));

        // Convert to SJSON
        config_sjson_const_t sjson = config_sjson(cv, CONFIG_OPTION_WRITE_ESCAPE_UTF8);
        string_const_t sjson_string = config_sjson_to_string(sjson);

        // Check that the string is escaped
        // Note that the escaped character are lowercase.
        REQUIRE_EQ(sjson_string, R"({
            string = "Hello \xe2\x98\xba World!"
        })");

        // Deallocate
        config_sjson_deallocate(sjson);
        config_deallocate(cv);
    }
}

#endif // BUILD_DEVELOPMENT
