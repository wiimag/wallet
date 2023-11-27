/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 */

#include <foundation/platform.h>

#if BUILD_TESTS

#include "test_utils.h"

#include <framework/config.h>
#include <framework/common.h>
#include <framework/string.h>

#include <foundation/path.h>
#include <foundation/stream.h>
#include <foundation/bufferstream.h>

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

    TEST_CASE("Indexing")
    {
        // Create config
        config_handle_t cv = config_allocate(CONFIG_VALUE_ARRAY);
        for (unsigned i = 0; i < 10; ++i)
            config_array_push(cv, (double)i);

        auto e = cv[7U];
        CHECK_EQ(e.as_integer(), 7);

        config_deallocate(cv);
    }

    TEST_CASE("Invalid indexing")
    {
        config_handle_t cv = config_allocate();
        cv.index = 999;

        void* vv = (void*)cv;
        CHECK_EQ(vv, nullptr);

        config_deallocate(cv);
    }

    TEST_CASE("Accessors")
    {
        config_handle_t cv = config_allocate(CONFIG_VALUE_OBJECT);
        CHECK(config_is_valid(cv));
        CHECK_EQ(config_get_options(cv), CONFIG_OPTION_NONE);

        CHECK_EQ(config_set_options(cv, CONFIG_OPTION_PRESERVE_INSERTION_ORDER), CONFIG_OPTION_NONE);
        CHECK_EQ(config_get_options(cv), CONFIG_OPTION_PRESERVE_INSERTION_ORDER);

        config_set(cv, "float", 42.728f);
        CHECK_EQ(cv["float"].type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ((float)config_value_as_number(cv["float"]), 42.728f);
        CHECK_EQ((float)cv["float"].as_number(), 42.728f);
        CHECK_EQ(cv["float"].as_integer(), INT32_C(42));

        auto arr = config_set_array(cv, "arr");
        config_array_push(arr, 77.9);
        config_array_push(arr, 78.9);

        config_set(cv, "pi", DBL_PI);
        CHECK_EQ(cv["pi"].type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ(cv["pi"].as_number(), DBL_PI);

        CHECK(config_array_pop(arr));
        CHECK(config_array_pop(arr));
        CHECK_FALSE(config_array_pop(arr));

        config_set(cv, "integer", INT32_C(42));
        CHECK_EQ(cv["integer"].type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ((int32_t)config_value_as_number(cv["integer"]), INT32_C(42));
        CHECK_EQ((int32_t)cv["integer"].as_number(), INT32_C(42));
        CHECK_EQ(cv[string_to_const("integer")].as_integer(), INT32_C(42));

        config_set(cv, "string", string_to_const("Hello World!"));
        CHECK_EQ(cv["string"].type(), CONFIG_VALUE_STRING);
        CHECK_EQ(cv["string"].as_string(), CTEXT("Hello World!"));

        config_set(cv, "boolean", true);
        CHECK_EQ(cv["boolean"].type(), CONFIG_VALUE_TRUE);
        CHECK_EQ(cv["boolean"].as_boolean(), true);
        CHECK_EQ(cv["boolean"].name(), CTEXT("boolean"));

        config_set_null(cv, STRING_CONST("null"));
        CHECK_EQ(cv["null"].type(), CONFIG_VALUE_NIL);
        CHECK(config_is_null(cv, STRING_CONST("null")));

        config_set_array(cv, STRING_CONST("array"));
        CHECK_EQ(cv["array"].type(), CONFIG_VALUE_ARRAY);
        CHECK_EQ(config_value_as_pointer_unsafe(cv["array"]), nullptr);

        config_set_object(cv, STRING_CONST("object"));
        CHECK_EQ(cv["object"].type(), CONFIG_VALUE_OBJECT);

        // The null fixed value is always undefined.
        CHECK(config_is_null(config_null()));
        CHECK(config_is_undefined(config_null()));
        CHECK_EQ(config_get_options(config_null()), CONFIG_OPTION_NONE);
        CHECK_EQ(config_null().type(), CONFIG_VALUE_UNDEFINED);

        config_deallocate(cv);
    }

    TEST_CASE("Undefined value")
    {
        config_handle_t cv = config_allocate(CONFIG_VALUE_OBJECT);

        auto e = config_add(cv, "value");
        CHECK_EQ(e.type(), CONFIG_VALUE_UNDEFINED);
        CHECK(config_is_undefined(e));

        CHECK(config_is_undefined(cv, STRING_CONST("value")));
        CHECK(config_is_undefined(cv, STRING_CONST("value1")));

        config_deallocate(cv);
    }

    TEST_CASE("Boolean value")
    {
        config_handle_t cv = config_allocate(CONFIG_VALUE_OBJECT);
        CHECK(config_is_valid(cv));

        config_set(cv, "true", true);
        config_set(cv, "false", false);
        config_set(cv, "string", string_to_const("true"));
        config_set(cv, "string2", string_to_const("false"));
        config_set(cv, "string3", string_to_const("patate"));
        config_set(cv, "number", 45.7);
        config_set(cv, "zero", 0.0);

        CHECK_EQ(config_value_as_boolean(cv["true"], false), true);
        CHECK_EQ(config_value_as_boolean(cv["false"], true), false);
        CHECK_EQ(config_value_as_boolean(cv["string"], false), true);
        CHECK_EQ(config_value_as_boolean(cv["string2"], true), false);
        CHECK_EQ(config_value_as_boolean(cv["string3"], true), true);
        CHECK_EQ(config_value_as_boolean(cv["string3"], false), false);
        CHECK_EQ(config_value_as_boolean(cv["number"], false), true);
        CHECK_EQ(config_value_as_boolean(cv["zero"], true), false);

        config_set_array(cv, "array");
        CHECK_EQ(config_value_as_boolean(cv["array"], true), false);

        config_array_insert(cv["array"], 0, 66.0);
        CHECK_EQ(config_value_as_boolean(cv["array"], false), true);

        config_set_object(cv, "object");
        CHECK_EQ(config_value_as_boolean(cv["object"], true), false);

        config_set(cv["object"], "child", 0);
        CHECK_EQ(config_value_as_boolean(cv["object"], false), true);

        const double v = 0;
        config_set(cv, "p", (const void*)&v);
        CHECK_EQ(config_value_as_boolean(cv["p"], false), true);

        config_set(cv, "p", (const void*)nullptr);
        CHECK_EQ(config_value_as_boolean(cv["p"], true), false);

        config_deallocate(cv);
    }

    TEST_CASE("Number value")
    {
        config_handle_t cv = config_allocate(CONFIG_VALUE_NUMBER);
        CHECK(config_is_valid(cv));

        config_set(cv, 42.728f);
        CHECK_EQ(cv.type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ((float)config_value_as_number(cv, 66.9), 42.728f);

        config_set(cv, DBL_PI);
        CHECK_EQ(cv.type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ(config_value_as_number(cv, 66.9), DBL_PI);

        config_set(cv, true);
        CHECK_EQ(cv.type(), CONFIG_VALUE_TRUE);
        CHECK_EQ(config_value_as_number(cv, 66.9), 1.0);

        config_set(cv, false);
        CHECK_EQ(cv.type(), CONFIG_VALUE_FALSE);
        CHECK_EQ(config_value_as_number(cv, 66.9), 0.0);

        config_set(cv, "Hello World!");
        CHECK_EQ(cv.type(), CONFIG_VALUE_STRING);
        CHECK_EQ(config_value_as_number(cv, 66.9), 0.0);

        config_set(cv, "77.8");
        CHECK_EQ(cv.type(), CONFIG_VALUE_STRING);
        CHECK_EQ(config_value_as_number(cv, 66.9), 77.8);

        config_set(cv, "21e5");
        CHECK_EQ(cv.type(), CONFIG_VALUE_STRING);
        CHECK_EQ(config_value_as_number(cv, 66.9), 21e5);

        config_set(cv, "0x21");
        CHECK_EQ(cv.type(), CONFIG_VALUE_STRING);
        CHECK_EQ(config_value_as_number(cv, 66.9), 33.0);

        config_set(cv, (const void*)nullptr);
        CHECK_EQ(cv.type(), CONFIG_VALUE_NIL);
        CHECK_EQ(config_value_as_number(cv, 66.9), 0.0);

        config_set(cv, (const void*)0xdeadbeefULL);
        CHECK_EQ(cv.type(), CONFIG_VALUE_RAW_DATA);
        CHECK_EQ(config_value_as_number(cv, 66.9), 0xdeadbeef);

        auto arr = config_allocate(CONFIG_VALUE_ARRAY);
        CHECK_EQ(arr.type(), CONFIG_VALUE_ARRAY);
        CHECK_EQ(config_value_as_number(arr, 66.9), 0.0f);

        config_array_push(arr, 77.9);
        CHECK_EQ(config_value_as_number(arr, 66.9), 1.0);

        config_array_push(arr, 78.9);
        CHECK_EQ(config_value_as_number(arr, 66.9), 2.0);

        CHECK(config_array_pop(arr));
        CHECK_EQ(config_value_as_number(arr, 66.9), 1.0);

        CHECK(config_array_pop(arr));
        CHECK_EQ(config_value_as_number(arr, 66.9), 0.0);

        CHECK_FALSE(config_array_pop(arr));
        CHECK_EQ(config_value_as_number(arr, 66.9), 0.0);

        config_deallocate(arr);
        config_deallocate(cv);
    }

    TEST_CASE("String value")
    {
        config_handle_t cv = config_allocate(CONFIG_VALUE_STRING, CONFIG_OPTION_WRITE_TRUNCATE_NUMBERS);

        config_set(cv, "Hello World!");
        CHECK_EQ(cv.type(), CONFIG_VALUE_STRING);
        CHECK_EQ(config_value_as_string(cv), CTEXT("Hello World!"));

        config_set(cv, 42.728f);
        CHECK_EQ(cv.type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ(config_value_as_string(cv, "%.2lf"), CTEXT("42.73"));

        config_set(cv, DBL_PI);
        CHECK_EQ(cv.type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ(config_value_as_string(cv, nullptr), CTEXT("3.14"));

        config_set(cv, 0.005);
        CHECK_EQ(cv.type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ(config_value_as_string(cv, nullptr), CTEXT("0.0050"));

        config_set(cv, 0.105);
        CHECK_EQ(cv.type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ(config_value_as_string(cv, nullptr), CTEXT("0.105"));

        config_set(cv, DNAN);
        CHECK_EQ(cv.type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ(config_value_as_string(cv, nullptr), CTEXT("null"));

        config_set(cv, true);
        CHECK_EQ(cv.type(), CONFIG_VALUE_TRUE);
        CHECK_EQ(config_value_as_string(cv), CTEXT("true"));

        config_set(cv, false);
        CHECK_EQ(cv.type(), CONFIG_VALUE_FALSE);
        CHECK_EQ(config_value_as_string(cv), CTEXT("false"));

        config_set(cv, (const void*)nullptr);
        CHECK_EQ(cv.type(), CONFIG_VALUE_NIL);
        CHECK_EQ(config_value_as_string(cv), CTEXT(""));

        config_set(cv, (const void*)0xdeadbeefULL);
        CHECK_EQ(cv.type(), CONFIG_VALUE_RAW_DATA);
        CHECK_EQ(config_value_as_string(cv), CTEXT("0x00000000deadbeef"));

        config_set(cv, "Hello World!");
        CHECK_EQ(cv.type(), CONFIG_VALUE_STRING);
        CHECK_EQ(config_value_as_string(cv), CTEXT("Hello World!"));

        config_set(cv, "77.8");
        CHECK_EQ(cv.type(), CONFIG_VALUE_STRING);
        CHECK_EQ(config_value_as_string(cv), CTEXT("77.8"));

        CHECK_EQ(config_value_as_string(cv["them"]), string_null());

        auto obj = config_add(cv, "element");
        CHECK_EQ(cv.type(), CONFIG_VALUE_OBJECT);
        CHECK_EQ(obj.type(), CONFIG_VALUE_UNDEFINED);
        CHECK_EQ(config_value_as_string(cv), CTEXT(""));
        CHECK_EQ(config_value_as_string(obj), CTEXT(""));

        string_const_t datestr = string_from_date(time_now());
        config_set(obj, STRING_CONST("id"), datestr);
        CHECK_EQ(obj.type(), CONFIG_VALUE_OBJECT);
        CHECK_EQ(config_value_as_string(obj["id"]), datestr);

        config_deallocate(cv);
    }

    TEST_CASE("Time value")
    {
        config_handle_t cv = config_allocate(CONFIG_VALUE_OBJECT, CONFIG_OPTION_ALLOCATE_TEMPORARY);
        CHECK(config_is_valid(cv));
        CHECK_EQ(config_get_options(cv), CONFIG_OPTION_ALLOCATE_TEMPORARY);

        const time_t now = time_now();
        config_set(cv, "time", now);
        CHECK_EQ(cv["time"].type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ(cv["time"].as_time(), now);

        string_t timestr = string_from_date(SHARED_BUFFER(16), now);
        config_set(cv, "time", timestr.str, timestr.length);
        CHECK_EQ(cv["time"].type(), CONFIG_VALUE_STRING);
        CHECK(time_same_day(cv["time"].as_time(), now));

        CHECK_EQ(config_set(cv, "b", true).as_time(42), 42);

        config_deallocate(cv);
    }

    TEST_CASE("Find")
    {
        CHECK(config_is_null(config_find(config_null(), STRING_CONST("test"))));
    }

    TEST_CASE("Tags")
    {
        config_handle_t cv = config_allocate(CONFIG_VALUE_OBJECT);
        CHECK(config_is_valid(cv));

        // Object cannot be cleared with this method.
        CHECK_EQ(config_array_clear(cv).type(), CONFIG_VALUE_UNDEFINED);

        config_set(cv, "n", 42.24);
        CHECK_EQ(cv["n"].type(), CONFIG_VALUE_NUMBER);

        config_tag_t tag = config_tag(cv, STRING_CONST("n"));

        auto e = config_find(cv, tag);
        CHECK_EQ(e.type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ(e.as_number(), 42.24);

        config_set(cv, tag, nullptr);
        CHECK_EQ(cv[tag].type(), CONFIG_VALUE_NIL);

        config_set(cv, tag, 42.24);
        CHECK_EQ(cv[tag].type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ(cv[tag].as_number(), 42.24);

        config_set(cv, tag, STRING_CONST("Hello World!"));
        CHECK_EQ(cv[tag].type(), CONFIG_VALUE_STRING);
        CHECK_EQ(cv[tag].as_string(), CTEXT("Hello World!"));

        config_set(cv, tag, true);
        CHECK_EQ(cv[tag].type(), CONFIG_VALUE_TRUE);
        CHECK_EQ(cv[tag].as_boolean(), true);

        config_set(cv, tag, false);
        CHECK_EQ(cv[tag].type(), CONFIG_VALUE_FALSE);
        CHECK_EQ(cv[tag].as_boolean(), false);

        config_set(cv, tag, (const void*)0xdeadbeefULL);
        CHECK_EQ(cv[tag].type(), CONFIG_VALUE_RAW_DATA);
        CHECK_EQ(cv[tag].as_number(), 0xdeadbeefULL);

        config_tag_t newtag = config_tag(cv, STRING_CONST("sub"));
        auto sub = config_get_or_create(cv, newtag);
        CHECK_EQ(sub.type(), CONFIG_VALUE_UNDEFINED);

        config_set(sub, tag, 42.24);
        CHECK_EQ(sub[tag].type(), CONFIG_VALUE_NUMBER);
        CHECK_EQ(sub[tag].as_number(), 42.24);

        config_deallocate(cv);
    }

    TEST_CASE("Invalid object")
    {
        config_handle_t cv{};
        config_set_null(cv);
        config_set_null(cv, STRING_CONST("null"));

        CHECK_FALSE(config_is_valid(cv));
        CHECK_EQ(config_set_options(cv, CONFIG_OPTION_PRESERVE_INSERTION_ORDER), CONFIG_OPTION_NONE);
        CHECK_EQ(config_get_options(cv), CONFIG_OPTION_NONE);
        CHECK_EQ(config_add(cv, "invalid").type(), CONFIG_VALUE_UNDEFINED);
        CHECK_FALSE(config_remove(cv, "child"));
        CHECK_FALSE(config_remove(cv, cv["child"]));

        CHECK_EQ(config_array_clear(cv).type(), CONFIG_VALUE_UNDEFINED);
        CHECK_EQ(config_array_push(cv, 42.24).type(), CONFIG_VALUE_UNDEFINED);
        CHECK_EQ(config_array_insert(cv, 0, 42.24).type(), CONFIG_VALUE_UNDEFINED);
        CHECK_FALSE(config_array_pop(cv));
        CHECK_FALSE(config_exists(cv, nullptr, 0)); // self

        CHECK_NOTHROW(config_array_sort(cv, LC2(true)));
        CHECK_NOTHROW(config_array_sort(cv, LC2(false)));
        CHECK_NOTHROW(config_pack(cv));
        CHECK_NOTHROW(config_clear(cv));

        CHECK_EQ(config_name(cv), CTEXT(""));
        CHECK_EQ(config_size(cv), 0);
        CHECK_EQ(config_type(cv), CONFIG_VALUE_UNDEFINED);
        CHECK(config_is_undefined(cv));

        CHECK_EQ(config_sjson(cv), nullptr);

        unsigned itr_count = 0;
        for (auto e : cv)
        {
            (void)e;
            ++itr_count;
        }

        CHECK_EQ(itr_count, 0U);
        CHECK_EQ(config_element_at(cv, 4).as_number(55.0), 55.0);
    }

    TEST_CASE("Iterators")
    {
        config_handle_t cv = config_allocate(CONFIG_VALUE_NIL, CONFIG_OPTION_PACK_STRING_TABLE);

        auto a = config_add(cv, "a");
        config_array_insert(a, 0, 42.24);
        CHECK_EQ(config_size(a), 1);
        CHECK_EQ(config_element_at(a, 0).as_number(), 42.24);

        config_array_pop(a);
        CHECK_EQ(config_size(a), 0);

        CHECK(config_is_valid(cv));
        CHECK(config_is_valid(cv, nullptr));
        CHECK(config_is_valid(cv, STRING_CONST("a")));
        CHECK_FALSE(config_is_null(cv, STRING_CONST("a")));
        CHECK_FALSE(config_is_valid(cv, STRING_CONST("b")));
        CHECK(config_remove(cv, "a"));
        config_set_null(cv);

        for (unsigned i = 0; i < 10; ++i)
            config_array_push(cv, (double)i);

        CHECK_EQ(config_element_at(cv, 4).as_number(), 4.0);

        config_array_insert(cv, 4, 42.24);
        CHECK_EQ(config_size(cv), 11);
        CHECK_EQ(config_element_at(cv, 4).as_number(), 42.24);

        CHECK_FALSE(config_is_null(cv));
        config_array_clear(cv);
        CHECK_EQ(config_size(cv), 0);

        for (unsigned i = 0; i < 2; ++i)
            config_array_push(cv, (bool)i);

        CHECK_EQ(config_element_at(cv, 0).as_boolean(), false);
        CHECK_EQ(config_element_at(cv, 1).as_boolean(), true);

        config_array_insert(cv, 0, true);
        CHECK_EQ(config_element_at(cv, 0).as_boolean(), true);

        config_deallocate(cv);
    }

    TEST_CASE("Remove")
    {
        config_handle_t cv = config_allocate(CONFIG_VALUE_OBJECT, CONFIG_OPTION_SORT_OBJECT_FIELDS);

        config_set(cv, "a", 1);
        config_set(cv, "b", true);
        config_set(cv, "c", STRING_CONST("Hello World!"));
        config_set(cv, "d", 3.14);

        CHECK_EQ(cv["a"].as_integer(), 1);
        CHECK_EQ(cv["b"].as_boolean(), true);
        CHECK_EQ(cv["c"].as_string(), CTEXT("Hello World!"));
        CHECK_EQ(cv["d"].as_number(), 3.14);

        CHECK(config_exists(cv, STRING_CONST("a")));
        CHECK_FALSE(config_exists(cv, STRING_CONST("e")));
        CHECK_EQ(config_size(cv), 4);

        CHECK(config_remove(cv, "b"));
        CHECK_EQ(config_size(cv), 3);

        CHECK_FALSE(config_remove(cv, "abcdef"));
        CHECK_EQ(config_size(cv), 3);

        CHECK(config_remove(cv, STRING_CONST("a")));
        CHECK_EQ(config_size(cv), 2);

        CHECK(config_remove(cv, cv["c"]));
        CHECK_EQ(config_size(cv), 1);

        CHECK(config_remove(cv, STRING_CONST("d")));
        CHECK_EQ(config_size(cv), 0);

        CHECK_FALSE(config_remove(cv, STRING_CONST("d")));
        CHECK_EQ(config_size(cv), 0);

        CHECK(config_exists(cv, nullptr, 0)); // self
        CHECK_FALSE(config_exists(cv, "a", 0));
        CHECK_FALSE(config_is_valid(cv, STRING_CONST("e")));
        CHECK_EQ(config_size(cv), 1);

        CHECK_NOTHROW(config_pack(cv));
        CHECK_NOTHROW(config_clear(cv));
        config_deallocate(cv);
    }

    TEST_CASE("Array")
    {
        // Create config array with random values
        auto arr = config_allocate(CONFIG_VALUE_ARRAY);

        for (unsigned i = 0; i < 100; ++i)
            config_array_push(arr, (double)rand());

        config_array_sort(arr, [](const auto& a, const auto& b)
        {
            return a.as_number() < b.as_number();
        });

        // Check that array is sorted
        for (unsigned i = 1; i < to_uint(config_size(arr)); ++i)
            CHECK_LE(arr[i - 1].as_number(), arr[i].as_number());

        config_deallocate(arr);
    }

    TEST_CASE("Parse / Write / NOT CONFIG_OPTION_PRESERVE_INSERTION_ORDER")
    {
        string_const_t sjson = CTEXT(R"({
             n1 = 0
             n2 = 1
        })");

        config_handle_t cv = config_parse(sjson.str, sjson.length, 0);

        CHECK_EQ(config_size(cv), 2);
        CHECK_EQ(config_element_at(cv, 0).as_integer(), 1);
        CHECK_EQ(config_element_at(cv, 1).as_integer(), 0);

        config_deallocate(cv);
    }

    TEST_CASE("Parse / Write / CONFIG_OPTION_PRESERVE_INSERTION_ORDER")
    {
        string_const_t sjson = CTEXT(R"({
             n1 = 0
             n2 = 1
        })");

        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PRESERVE_INSERTION_ORDER);

        CHECK_EQ(config_size(cv), 2);
        CHECK_EQ(config_element_at(cv, 0).as_integer(), 0);
        CHECK_EQ(config_element_at(cv, 1).as_integer(), 1);



        config_deallocate(cv);
    }

    TEST_CASE("Parse / Write / Undefined Not Saved")
    {
        string_const_t sjson = CTEXT(R"({
             v = 1
        })");

        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PRESERVE_INSERTION_ORDER);

        CHECK_EQ(config_size(cv), 1);
        CHECK(config_is_undefined(config_add(cv, "undef")));
        //CHECK(config_is_undefined(config_element_at(cv, 0)));

        string_t temp_file_path = path_make_temporary(SHARED_BUFFER(BUILD_MAX_PATHLEN));
        string_const_t temp_file_dir_path = path_directory_name(STRING_ARGS(temp_file_path));
        CHECK(fs_make_directory(STRING_ARGS(temp_file_dir_path)));
        const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv, CONFIG_OPTION_NONE);
        CHECK(write_success);

        auto file_content = fs_read_text(STRING_ARGS(temp_file_path));
        INFO("File content: " << file_content.str);
        CHECK_EQ(sjson, file_content);

        string_deallocate(file_content.str);
        config_deallocate(cv);
    }

    TEST_CASE("Parse / Write / (Skip) Null")
    {
        string_const_t sjson = CTEXT(R"({
             a = 1
             b = null
        })");

        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PRESERVE_INSERTION_ORDER);

        string_t temp_file_path = path_make_temporary(SHARED_BUFFER(BUILD_MAX_PATHLEN));
        string_const_t temp_file_dir_path = path_directory_name(STRING_ARGS(temp_file_path));
        CHECK(fs_make_directory(STRING_ARGS(temp_file_dir_path)));
        
        {
            const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv, CONFIG_OPTION_WRITE_SKIP_NULL);
            CHECK(write_success);

            auto file_content = fs_read_text(STRING_ARGS(temp_file_path));
            INFO("File content: " << file_content.str);
            CHECK_EQ(CTEXT(R"({
                     a = 1
                })"), file_content);

            string_deallocate(file_content.str);
        }


        {
            const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv, CONFIG_OPTION_NONE);
            CHECK(write_success);

            auto file_content = fs_read_text(STRING_ARGS(temp_file_path));
            INFO("File content: " << file_content.str);
            CHECK_EQ(CTEXT(R"({
                     a = 1
                     b = null
                })"), file_content);

            string_deallocate(file_content.str);
        }

        config_deallocate(cv);
    }

    TEST_CASE("Write / String")
    {
        string_const_t sjson = CTEXT(R"({
            hash = "c0aa848e6fa77ad4"
            en = "Bulk Extractor"
            e8_F9 = test
            "not simple": "true"
            fr = "Extracteur de marchés"
            notes = "\" \tnew line \r\n \b\f"
        })");

        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PRESERVE_INSERTION_ORDER);

        string_t temp_file_path = path_make_temporary(SHARED_BUFFER(BUILD_MAX_PATHLEN));
        string_const_t temp_file_dir_path = path_directory_name(STRING_ARGS(temp_file_path));
        CHECK(fs_make_directory(STRING_ARGS(temp_file_dir_path)));
        
        const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv, CONFIG_OPTION_WRITE_ESCAPE_UTF8);
        CHECK(write_success);

        auto file_content = fs_read_text(STRING_ARGS(temp_file_path));
        INFO("File content: " << file_content.str);
        CHECK_EQ(CTEXT("{ "
            "hash = \"c0aa848e6fa77ad4\" "
            "en = \"Bulk Extractor\" "
            "e8_F9 = \"test\" "
            "\"not simple\" = \"true\" "
            "fr = \"Extracteur de march\\xc3\\xa9s\" "
            "notes = \"\\\" \\tnew line \\r\\n \\b\\f\" "
        "}"), file_content);

        string_deallocate(file_content.str);
        config_deallocate(cv);
    }

    TEST_CASE("Write / Same Line")
    {
        string_const_t sjson = CTEXT(R"({
            obj = {
                hash = "c0aa848e6fa77ad4"
                e8_F9 = test
            }
        })");

        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PRESERVE_INSERTION_ORDER);

        string_t temp_file_path = path_make_temporary(SHARED_BUFFER(BUILD_MAX_PATHLEN));
        string_const_t temp_file_dir_path = path_directory_name(STRING_ARGS(temp_file_path));
        CHECK(fs_make_directory(STRING_ARGS(temp_file_dir_path)));
        
        const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv, 
            CONFIG_OPTION_WRITE_SKIP_FIRST_BRACKETS | CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES);
        CHECK(write_success);

        auto file_content = fs_read_text(STRING_ARGS(temp_file_path));
        INFO("File content: " << file_content.str);
        CHECK_EQ(CTEXT("obj = { hash = \"c0aa848e6fa77ad4\" e8_F9 = \"test\" }"), file_content);

        string_deallocate(file_content.str);
        config_deallocate(cv);
    }

    TEST_CASE("Write / Same Line Not Possible")
    {
        string_const_t sjson = CTEXT(R"({
            hash = "c0aa848e6fa77ad4"
            "not simple" = test
        })");

        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PRESERVE_INSERTION_ORDER);

        string_t temp_file_path = path_make_temporary(SHARED_BUFFER(BUILD_MAX_PATHLEN));
        string_const_t temp_file_dir_path = path_directory_name(STRING_ARGS(temp_file_path));
        CHECK(fs_make_directory(STRING_ARGS(temp_file_dir_path)));
        
        const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv, 
            CONFIG_OPTION_WRITE_OBJECT_SAME_LINE_PRIMITIVES);
        CHECK(write_success);

        auto file_content = fs_read_text(STRING_ARGS(temp_file_path));
        INFO("File content: " << file_content.str);
        CHECK_EQ(CTEXT("{ hash = \"c0aa848e6fa77ad4\" \"not simple\" = \"test\" }"), file_content);

        string_deallocate(file_content.str);
        config_deallocate(cv);
    }

    TEST_CASE("Write / Pure JSON")
    {
        string_const_t sjson = CTEXT(R"({
            hash = "c0aa848e6fa77ad4"
            "::filter": false,
            "not simple" = test
        })");

        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PRESERVE_INSERTION_ORDER);

        string_t temp_file_path = path_make_temporary(SHARED_BUFFER(BUILD_MAX_PATHLEN));
        string_const_t temp_file_dir_path = path_directory_name(STRING_ARGS(temp_file_path));
        CHECK(fs_make_directory(STRING_ARGS(temp_file_dir_path)));
        
        const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv, CONFIG_OPTION_WRITE_JSON | CONFIG_OPTION_WRITE_SKIP_DOUBLE_COMMA_FIELDS);
        CHECK(write_success);

        auto file_content = fs_read_text(STRING_ARGS(temp_file_path));
        INFO("File content: " << file_content.str);
        CHECK_EQ(CTEXT("{ \"hash\": \"c0aa848e6fa77ad4\", \"not simple\": \"test\" }"), file_content);

        string_deallocate(file_content.str);
        config_deallocate(cv);
    }

    TEST_CASE("Write / Array")
    {
        string_const_t sjson = CTEXT(R"({
            c = {
                a = [1 2 3 4 5, true, { n = 42 }, false, "a string" 33 [1 null]]
            }
        })");

        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PRESERVE_INSERTION_ORDER);

        string_t temp_file_path = path_make_temporary(SHARED_BUFFER(BUILD_MAX_PATHLEN));
        string_const_t temp_file_dir_path = path_directory_name(STRING_ARGS(temp_file_path));
        CHECK(fs_make_directory(STRING_ARGS(temp_file_dir_path)));
        
        {
            const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv);
            CHECK(write_success);

            auto file_content = fs_read_text(STRING_ARGS(temp_file_path));
            INFO("File content: " << file_content.str);
            CHECK_EQ(CTEXT("c = { a = [1 2 3 4 5 true { n = 42 } false \"a string\" 33 [1]] }"), file_content);

            string_deallocate(file_content.str);
        }

        {
            const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv, CONFIG_OPTION_WRITE_JSON);
            CHECK(write_success);

            auto file_content = fs_read_text(STRING_ARGS(temp_file_path));
            INFO("File content: " << file_content.str);
            CHECK_EQ(CTEXT("{ \"c\": { \"a\": [1,  2,  3,  4,  5,  true, { \"n\": 42 },  false,  \"a string\",  33, [1, null]] } }"), file_content);

            string_deallocate(file_content.str);
        }

        config_deallocate(cv);
    }

    TEST_CASE("Write / Undefined")
    {
        config_handle_t cv = config_allocate(CONFIG_VALUE_UNDEFINED);

        string_t temp_file_path = path_make_temporary(SHARED_BUFFER(BUILD_MAX_PATHLEN));
        string_const_t temp_file_dir_path = path_directory_name(STRING_ARGS(temp_file_path));
        CHECK(fs_make_directory(STRING_ARGS(temp_file_dir_path)));
        
        const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv);
        CHECK_FALSE(write_success);
        CHECK_FALSE(fs_is_file(STRING_ARGS(temp_file_path)));
        config_deallocate(cv);
    }

    TEST_CASE("Parse / BOM")
    {
        // Create BOM string and add "c = 42"
        char bom[] = { (char)0xEF, (char)0xBB, (char)0xBF, 'c', ' ', '=', ' ', '4', '2', 0 };
        string_const_t sjson = { bom, sizeof(bom) - 1 };
        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PRESERVE_INSERTION_ORDER);

        CHECK_EQ(cv["c"].as_number(), 42);

        config_deallocate(cv);
    }

    TEST_CASE("Parse simple")
    {
        string_const_t sjson = CTEXT(R"({
            b = false
            n = 42
            s = "a string"
            a = [1 2 3 4 5]
            o = { a = 1 b = 2 }
            u = null
        })");
        config_handle_t cv = config_parse(sjson.str, sjson.length);

        CHECK_EQ(cv["n"].as_number(), 42.0);
        CHECK_EQ(cv["s"].as_string(), CTEXT("a string"));
        CHECK_EQ(cv["b"].as_boolean(), false);
        CHECK_EQ(cv["u"].type(), CONFIG_VALUE_NIL);
        CHECK_EQ(cv["a"].type(), CONFIG_VALUE_ARRAY);
        CHECK_EQ(cv["o"].type(), CONFIG_VALUE_OBJECT);

        config_deallocate(cv);
    }

    TEST_CASE("Parse JSON")
    {
        string_const_t sjson = CTEXT(R"({
            "b": false,
            "n": 42,
            "s": "a string",
            "a": [1, 2, 3, 4, 5],
            "o": { "a": 1, "b": 2 }
        })");
        config_handle_t cv = config_parse(sjson.str, sjson.length);

        CHECK_EQ(cv["n"].as_number(), 42.0);
        CHECK_EQ(cv["s"].as_string(), CTEXT("a string"));
        CHECK_EQ(cv["b"].as_boolean(), false);
        CHECK_EQ(cv["a"].type(), CONFIG_VALUE_ARRAY);
        CHECK_EQ(cv["o"].type(), CONFIG_VALUE_OBJECT);

        config_deallocate(cv);
    }

    TEST_CASE("Parse Many Levels")
    {
        string_const_t sjson = CTEXT(R"({
            "b": true,
            // Add one more level
            c = {
                "n": 42,
                "s": "a string",
                /* Add one more level, 
                   again 
                 */
                "a": [1, 2, 3, { "a": 10, "b": 2 }, 5],
            }

            unicode = "\ue958 this is an icon"

            // multiline string
            shader = """
                int main()
                {
                    // Return red color
                    gl_Color.xyz = vec3(1.0, 0.0, 0.0);
                }
            """

            size = 4e44fa4
        })");
        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PACK_STRING_TABLE);

        CHECK_EQ(cv["b"].as_boolean(), true);
        CHECK_EQ(cv["c"]["n"].as_number(), 42.0);
        CHECK_EQ(cv["c"]["s"].as_string(), CTEXT("a string"));
        CHECK_EQ(cv["c"]["a"][3UL]["a"].as_number(), 10.0);

        string_const_t s1 = cv["shader"].as_string();
        string_const_t s2 = CTEXT("int main()\n{\n    // Return red color\n    gl_Color.xyz = vec3(1.0, 0.0, 0.0);\n}\n");
        CHECK_EQ(s1, s2);

        CHECK_EQ(cv["unicode"].as_string(), CTEXT_UTF8("\\ue958 this is an icon"));

        string_t temp_file_path = path_make_temporary(SHARED_BUFFER(BUILD_MAX_PATHLEN));
        string_const_t temp_file_dir_path = path_directory_name(STRING_ARGS(temp_file_path));
        CHECK(fs_make_directory(STRING_ARGS(temp_file_dir_path)));
        const bool write_success = config_write_file(STRING_ARGS(temp_file_path), cv, CONFIG_OPTION_WRITE_ESCAPE_UTF8);
        CHECK(write_success);

        config_deallocate(cv);

        cv = config_parse_file(STRING_ARGS(temp_file_path), CONFIG_OPTION_PARSE_UNICODE_UTF8);
        CHECK_EQ(cv["b"].as_boolean(), true);
        CHECK_EQ(cv["c"]["n"].as_number(), 42.0);
        CHECK_EQ(cv["c"]["s"].as_string(), CTEXT("a string"));
        CHECK_EQ(cv["c"]["a"][3UL]["a"].as_number(), 10.0);

        config_deallocate(cv);
    }

    TEST_CASE("Parse Unicode/UTF-8 characters")
    {
        string_const_t sjson = CTEXT(R"({

            utf8 = "\xef\xa3\xbd"
            more = "\x1f\xA7 \xc4\x77\xA8\x9F "
            unicode = "\ue958 this is an icon\x00"

        })");
        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PARSE_UNICODE_UTF8);

        string_const_t s1 = cv["unicode"].as_string();
        string_const_t s2 = CTEXT("\xee\xa5\x98 this is an icon");
        CHECK_EQ(s1, s2);

        s1 = cv["utf8"].as_string();
        s2 = CTEXT("\xef\xa3\xbd");
        CHECK_EQ(s1, s2);

        config_deallocate(cv);
    }

    TEST_CASE("Parse Array")
    {
        string_const_t sjson = CTEXT(R"([
            {
                a: 0
            }
            {
                a: 1
            }
            {
                a: 2
            }
        ])");
        config_handle_t cv = config_parse(sjson.str, sjson.length, CONFIG_OPTION_PACK_STRING_TABLE);

        CHECK_EQ(cv[0U]["a"].as_integer(), 0);
        CHECK_EQ(cv[1U]["a"].as_integer(), 1);
        CHECK_EQ(cv[2U]["a"].as_integer(), 2);
        
        config_deallocate(cv);
    }
}

TEST_SUITE("YAML")
{

    TEST_CASE("m_Name:")
    {
        string_const_t yaml = CTEXT(R"(
%YAML 1.1
%TAG !u! tag:unity3d.com,2011:
--- !u!114 &7
MonoBehaviour:
  m_ObjectHideFlags: 52
  m_PrefabParentObject: {fileID: 0}
  m_PrefabInternal: {fileID: 0}
  m_GameObject: {fileID: 0}
  m_EditorHideFlags: 1
  m_Script: {fileID: 12011, guid: 0000000000000000e000000000000000, type: 0}
  m_Name:
  m_Enabled: 1
  m_EditorClassIdentifier:
  m_Children: []
  m_Position:
    serializedVersion: 2
    x: 0
    y: 0
    width: 2560
    height: 30
  m_MinSize: {x: 0, y: 0}
  m_MaxSize: {x: 0, y: 0}
  m_LastLoadedLayoutName:
--- !u!114 &8
MonoBehaviour:
  m_ObjectHideFlags: 52
)");

        stream_t* stream = buffer_stream_allocate((void*)yaml.str, STREAM_IN, yaml.length, yaml.length + 1, false, false);
        CHECK_NE(stream, nullptr);

        config_handle_t cv = config_parse_yaml(stream);
        CHECK(cv);

        auto sjson = config_sjson(cv, CONFIG_OPTION_NONE);
        string_const_t text = config_sjson_to_string(sjson);
        log_infof(0, STRING_CONST("%.*s"), STRING_FORMAT(text));
        config_sjson_deallocate(sjson);

        CHECK_EQ(cv["7"]["#type"].as_string(), CTEXT("MonoBehaviour"));
        CHECK_EQ(config_value_type(cv["7"]["m_Name"]), CONFIG_VALUE_NIL);
        CHECK_EQ(cv["7"]["m_Enabled"].as_number(), 1.0);

        config_deallocate(cv);
        stream_deallocate(stream);
    }

    TEST_CASE("m_TexEnvs")
    {
        string_const_t yaml = CTEXT(R"(
%YAML 1.1
%TAG !u! tag:unity3d.com,2011:
--- !u!21 &2100000
Material:
  m_Name: Default_Material
  m_SavedProperties:
    serializedVersion: 3
    m_TexEnvs:
    - _BaseMap:
        m_Texture: {fileID: 0}
        m_Scale: {x: 2, y: 1}
        m_Offset: {x: 0, y: 0}
    - _BumpMap:
        m_Texture: {fileID: 0}
        m_Scale: {x: 1, y: 3}
        m_Offset: {x: 0, y: 0}
)");

        stream_t* stream = buffer_stream_allocate((void*)yaml.str, STREAM_IN, yaml.length, yaml.length + 1, false, false);
        CHECK_NE(stream, nullptr);

        config_handle_t cv = config_parse_yaml(stream);
        CHECK(cv);

        auto sjson = config_sjson(cv, CONFIG_OPTION_NONE);
        string_const_t text = config_sjson_to_string(sjson);
        log_infof(0, STRING_CONST("%.*s"), STRING_FORMAT(text));
        config_sjson_deallocate(sjson);

        CHECK_EQ(cv["2100000"]["#type"].as_string(), CTEXT("Material"));
        CHECK_EQ(config_size(cv["2100000"]["m_SavedProperties"]["m_TexEnvs"]), 2);
        CHECK_EQ(cv["2100000"]["m_TexEnvs"][0U]["_BaseMap"]["m_Scale"]["x"].as_number(), 2.0);
        CHECK_EQ(cv["2100000"]["m_TexEnvs"][0U]["_BumpMap"]["m_Scale"]["y"].as_number(), 3.0);

        config_deallocate(cv);
        stream_deallocate(stream);
    }

    TEST_CASE("Default_Material.mat")
    {
        string_const_t yaml = CTEXT(R"(
%YAML 1.1
%TAG !u! tag:unity3d.com,2011:
--- !u!114 &-45820535484175795
MonoBehaviour:
  m_ObjectHideFlags: 11
  m_PrefabAsset: {fileID: 0}
  m_Enabled: 1
  m_EditorHideFlags: 0
  m_Name: 
  m_EditorClassIdentifier: 
  version: 6
--- !u!21 &2100000
Material:
  serializedVersion: 8
  m_Name: Default_Material
  m_Shader: {fileID: 4800000, guid: 933532a4fcc9baf4fa0491de14d08ed7, type: 3}
  m_ModifiedSerializedProperties: 0
  m_ValidKeywords:
  - _ENVIRONMENTREFLECTIONS_OFF
  - _SPECULARHIGHLIGHTS_OFF
  m_InvalidKeywords:
  - _GLOSSYREFLECTIONS_OFF
  m_CustomRenderQueue: -1
  stringTagMap:
    RenderType: Opaque
  disabledShaderPasses: []
  m_LockedProperties: 
  m_SavedProperties:
    serializedVersion: 3
    m_TexEnvs:
    - _BaseMap:
        m_Texture: {fileID: 0}
        m_Scale: {x: 1, y: 1}
        m_Offset: {x: 0, y: 0}
    - _BumpMap:
        m_Texture: {fileID: 0}
        m_Scale: {x: 1, y: 1}
        m_Offset: {x: 0, y: 0}
)");

        stream_t* stream = buffer_stream_allocate((void*)yaml.str, STREAM_IN, yaml.length, yaml.length + 1, false, false);
        CHECK_NE(stream, nullptr);

        config_handle_t cv = config_parse_yaml(stream);
        CHECK(cv);

        auto sjson = config_sjson(cv, CONFIG_OPTION_NONE);
        string_const_t text = config_sjson_to_string(sjson);
        log_infof(0, STRING_CONST("%.*s"), STRING_FORMAT(text));
        config_sjson_deallocate(sjson);

        CHECK_EQ(config_size(cv["#headers"]), 2);
        CHECK_EQ(config_size(cv["2100000"]["m_ValidKeywords"]), 2);
        CHECK_EQ(config_size(cv["2100000"]["m_SavedProperties"]["m_TexEnvs"]), 2);

        CHECK_EQ(cv["2100000"]["#type"].as_string(), CTEXT("Material"));
        CHECK_EQ(cv["2100000"]["stringTagMap"]["RenderType"].as_string(), CTEXT("Opaque"));
        CHECK_FALSE(config_exists(cv["2100000"], STRING_CONST("disabledShaderPasses")));

        config_deallocate(cv);
        stream_deallocate(stream);

    }
}

#endif // BUILD_TESTS
