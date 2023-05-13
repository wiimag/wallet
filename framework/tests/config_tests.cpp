/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_TESTS

#include "test_utils.h"

#include <framework/config.h>
#include <framework/common.h>
#include <framework/string.h>

#include <foundation/path.h>

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

    TEST_CASE("Config With Raw Pointer")
    {
        config_handle_t obj = config_allocate();
        CHECK_EQ(config_value_type(obj), CONFIG_VALUE_OBJECT);
        CHECK(config_is_valid(obj));

        config_handle_t cv = config_add(obj, STRING_CONST("value"));
        CHECK(config_value_type(cv) == CONFIG_VALUE_UNDEFINED);

        // Create memory buffer.
        void* buffer = memory_allocate(0, 1024, 0, MEMORY_TEMPORARY);

        // Set raw pointer
        config_set(cv, buffer);
        CHECK_EQ(config_value_type(cv), CONFIG_VALUE_RAW_DATA);

        // Make sure the root obj was also altered.
        CHECK_EQ(obj["value"].type(), CONFIG_VALUE_RAW_DATA);

        // Get back the raw data and deallocate it.
        void* raw_data = (void*)config_value_as_pointer_unsafe(cv);
        CHECK_EQ(raw_data, buffer);
        memory_deallocate(raw_data);

        // Nullify the config value
        config_set_null(cv);
        CHECK_EQ(config_value_type(cv), CONFIG_VALUE_NIL);

        // Make sure the root obj was also altered.
        CHECK_EQ(obj["value"].type(), CONFIG_VALUE_NIL);

        // Deallocate
        config_deallocate(obj);
    }

    TEST_CASE("Config Set Raw Pointer")
    {
        // Create memory buffer.
        void* buffer = memory_allocate(0, 1024, 0, MEMORY_TEMPORARY);

        // Create config
        config_handle_t cv = config_allocate();
        CHECK(config_is_valid(cv));

        // Set raw pointer
        config_set(cv, "buffer", (const void*)buffer);
        CHECK_EQ(config_value_type(cv["buffer"]), CONFIG_VALUE_RAW_DATA);
        
        // Get back the raw data and deallocate it.
        void* raw_data = (void*)config_value_as_pointer_unsafe(cv["buffer"]);
        CHECK_EQ(buffer, raw_data);
        memory_deallocate(raw_data);

        // Deallocate
        config_deallocate(cv);
    }
    
    TEST_CASE("Config Set Float")
    {
        // Create config
        config_handle_t cv = config_allocate();
        CHECK(config_is_valid(cv));

        // Set float
        config_set(cv, "float", 42.728f);
        CHECK_EQ(cv["float"].type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ((float)config_value_as_number(cv["float"]), 42.728f);
        CHECK_EQ((float)cv["float"].as_number(), 42.728f);
        CHECK_EQ(cv["float"].as_integer(), INT32_C(42));

        // Deallocate
        config_deallocate(cv);
    }

    TEST_CASE("Config Set Integer")
    {
        // Create config
        config_handle_t cv = config_allocate();
        CHECK(config_is_valid(cv));

        // Set integer
        config_set(cv, "integer", INT32_C(42));
        CHECK_EQ(cv["integer"].type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ((int32_t)config_value_as_number(cv["integer"]), INT32_C(42));
        CHECK_EQ((int32_t)cv["integer"].as_number(), INT32_C(42));
        CHECK_EQ(cv["integer"].as_integer(), INT32_C(42));

        // Deallocate
        config_deallocate(cv);
    }

    TEST_CASE("Write file")
    {
        // Create config
        config_handle_t cv = config_allocate(CONFIG_VALUE_ARRAY);
        CHECK(config_is_valid(cv));

        // Add numbers config value
        for (unsigned i = 0; i < 10; ++i)
            config_array_push(cv, (double)i);

        CHECK_EQ(config_size(cv), 10);

        // Write to file
        string_t temp_file_path = path_make_temporary(SHARED_BUFFER(BUILD_MAX_PATHLEN));
        string_const_t temp_file_dir_path = path_directory_name(STRING_ARGS(temp_file_path));
        CHECK(fs_make_directory(STRING_ARGS(temp_file_dir_path)));
        const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv, CONFIG_OPTION_WRITE_TRUNCATE_NUMBERS);
        CHECK(write_success);

        auto sjson = config_sjson(cv, CONFIG_OPTION_WRITE_TRUNCATE_NUMBERS);
        auto sjson_string = config_sjson_to_string(sjson);

        auto file_content = fs_read_text(STRING_ARGS(temp_file_path));
        CHECK_EQ(sjson_string, file_content);
        
        // Deallocate
        string_deallocate(file_content);
        config_sjson_deallocate(sjson);
        config_deallocate(cv);
    }
}

#endif // BUILD_TESTS
