/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * String tests
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT

#include "test_utils.h"

#include <framework/string.h>
#include <framework/string_template.inl.h>

#include <foundation/assert.h>

struct duder_t
{
    string_const_t name;
    int age;
};

FOUNDATION_FORCEINLINE string_t stream_dude(char* buffer, size_t capacity, void* ptr)
{
    duder_t const* dude = (duder_t const*)ptr;
    return string_template(buffer, capacity, "{0} {1} years old", dude->name, dude->age);
}

TEST_SUITE("String")
{
    TEST_CASE("Debugging")
    {
        //char buffer[256];
        string_t str = {};

        str = string_template_static("{0} {1} {0}", "Cool", "Cool");
        CHECK_EQ(str, CTEXT("Cool Cool Cool"));
    }

    TEST_CASE("Template")
    {
        {
            char buffer[1];
            string_t str = string_template(STRING_BUFFER(buffer), "Human: Say {0} {1}!\n AI: {0} {1}!", "Hello", "World");
            CHECK_EQ(str, CTEXT(""));
        }

        {
            char buffer[16];
            string_t str = string_template(STRING_BUFFER(buffer), "Human: Say {0} {1}!\n AI: {0} {1}!", "Hello", "World");
            CHECK_EQ(str, CTEXT("Human: Say Hell"));
        }

        char buffer[256];
        string_t str = {};
        
        str = string_template(STRING_BUFFER(buffer), CTEXT("Hello {0}"), "World");
        CHECK_EQ(str, CTEXT("Hello World"));

        str = string_template(STRING_BUFFER(buffer), "Hello {0}!", "World");
        CHECK_EQ(str, CTEXT("Hello World!"));

        str = string_template(STRING_BUFFER(buffer), CTEXT("{0} {1}!"), "Hello", "World");
        CHECK_EQ(str, CTEXT("Hello World!"));

        str = string_template(STRING_BUFFER(buffer), CTEXT("Human: Say {0} {1}!\n AI: {0} {1}!"), "Hello", "World");
        CHECK_EQ(str, CTEXT("Human: Say Hello World!\n AI: Hello World!"));

        str = string_template(STRING_BUFFER(buffer), CTEXT("{{Hello World}}!"), "1", "2");
        CHECK_EQ(str, CTEXT("{{Hello World}}!"));

        str = string_template(STRING_BUFFER(buffer), "He{{o Wor{d!", 42);
        CHECK_EQ(str, CTEXT("He{{o Wor{d!"));

        str = string_template(STRING_BUFFER(buffer), "Hello {0}, {1} and {2}!", "Jim", "Will", "Roland");
        CHECK_EQ(str, CTEXT("Hello Jim, Will and Roland!"));

        str = string_template(STRING_BUFFER(buffer), "Hello {2}, {0} and {1}!", "Jim", "Will", "Roland");
        CHECK_EQ(str, CTEXT("Hello Roland, Jim and Will!"));

        str = string_template(STRING_BUFFER(buffer), "{0} has {1} years old", "Jim", 12);
        CHECK_EQ(str, CTEXT("Jim has 12 years old"));

        str = string_template(STRING_BUFFER(buffer), CTEXT("{0} has {1} years old"), "Jim", 12);
        CHECK_EQ(str, CTEXT("Jim has 12 years old"));

        str = string_template(STRING_BUFFER(buffer), "{1} has {0} $", 12.5, "Yuri");
        CHECK_EQ(str, CTEXT("Yuri has 12.5 $"));

        str = string_template(STRING_BUFFER(buffer), "Point {{{0}, {1}}}", 15, 69);
        CHECK_EQ(str, CTEXT("Point {15, 69}"));

        str = string_template(STRING_BUFFER(buffer), "Point {{{0,3}, {1,3}}}", 15.0f, 69.8f);
        CHECK_EQ(str, CTEXT("Point {15, 69.8}"));

        str = string_template(STRING_BUFFER(buffer), "PI={0,11}", REAL_PI);
        CHECK_EQ(str, CTEXT("PI=3.1415926536"));

        str = string_template(STRING_BUFFER(buffer), "hex={1, hex}", nullptr, UINT32_C(0xdeadbeef));
        CHECK_EQ(str, CTEXT("hex=deadbeef"));

        str = string_template(STRING_BUFFER(buffer), "0x{0, hex}", INT32_C(0xdeadbeef));
        CHECK_EQ(str, CTEXT("0xdeadbeef"));

        str = string_template(STRING_BUFFER(buffer), "{0, hex0x}", UINT32_C(0x744f));
        CHECK_EQ(str, CTEXT("0x0000744f"));

        str = string_template(STRING_BUFFER(buffer), "{0, hex}", UINT32_C(0x744f));
        CHECK_EQ(str, CTEXT("744f"));

        str = string_template(STRING_BUFFER(buffer), "string_const_t=CTEXT({0})", CTEXT("coucou"));
        CHECK_EQ(str, CTEXT("string_const_t=CTEXT(coucou)"));

        {
            string_const_t s = CTEXT("Hello World");
            str = string_template(STRING_BUFFER(buffer), "{0} - {0,lowercase}", s);
            CHECK_EQ(str, CTEXT("Hello World - hello world"));
        }

        str = string_template(STRING_BUFFER(buffer), "{0,uppercase} - {0,lowercase}", "awesomeness");
        CHECK_EQ(str, CTEXT("AWESOMENESS - awesomeness"));

        #if BUILD_ENABLE_ASSERT
        {
            auto prev_handler = assert_handler();
            assert_set_handler([](hash_t context, const char* condition, size_t cond_length, const char* file, size_t file_length, unsigned int line, const char* msg, size_t msg_length) 
            {
                CHECK_EQ(string_const(condition, cond_length), CTEXT("<Static fail>"));
                CHECK_EQ(string_const(msg, msg_length), CTEXT("Invalid string argument type, potential overflow!"));
                return 0;
            });
            str = string_template(STRING_BUFFER(buffer), "overflow={12}", 1, 2, 3, 4);
            CHECK_EQ(str, CTEXT("overflow={12}"));
            assert_set_handler(prev_handler);
        }
        #endif

        {
            string_t dynamic_str = string_allocate_format(STRING_CONST("%.2lf $"), (double)300e3);
            str = string_template(STRING_BUFFER(buffer), "Wallet {0}", dynamic_str);
            CHECK_EQ(str, CTEXT("Wallet 300000.00 $"));
            string_deallocate(dynamic_str);
        }

        str = string_template(STRING_BUFFER(buffer), "no placeholders", 1, 2, 3, 4);
        CHECK_EQ(str, CTEXT("no placeholders"));

        str = string_template(STRING_BUFFER(buffer), "line return={0,hex0x2}", '\n');
        CHECK_EQ(str, CTEXT("line return=0x0a"));

        str = string_template(STRING_BUFFER(buffer), "bool={0}, bool={1}, int={2}, float={3,3}, {5,lowercase}={4}", true, false, 42, 3.14f, CTEXT("Hello World"), "STRING");
        CHECK_EQ(str, CTEXT("bool=true, bool=false, int=42, float=3.14, string=Hello World"));

        {
            int* numbers = nullptr;
            array_push(numbers, 1);
            array_push(numbers, 3);
            array_push(numbers, 5);
            array_push(numbers, 7);
            array_push(numbers, 9);
            array_push(numbers, 11);

            str = string_template(STRING_BUFFER(buffer), "numbers=[{0}]", numbers);
            CHECK_EQ(str, CTEXT("numbers=[1, 3, 5, 7, 9, 11]"));

            array_deallocate(numbers);
        }

        {
            duder_t dude{ CTEXT("Zack"), 9 };
            str = string_template(STRING_BUFFER(buffer), "Who: {0}", stream_dude, &dude);
            CHECK_EQ(str, CTEXT("Who: Zack 9 years old"));
        }

        {
            duder_t dude{ CTEXT("Jack"), 199 };
            str = string_template(STRING_BUFFER(buffer), "Who: {0,2048}", stream_dude, &dude);
            CHECK_EQ(str, CTEXT("Who: Jack 199 years old"));
        }
    }

    TEST_CASE("Template With Allocation")
    {
        string_t str{};

        str = string_allocate_template("{0,5}, {1}, {2}", 3.14f, true, "pi");
        CHECK_EQ(str, CTEXT("3.14, true, pi"));
        string_deallocate(str);

        str = string_allocate_template("{0,:short}, {1,:medium}, {2,:long}", 
            "this is a short string",
            "this is a medium length string, but still not that long",
            "this is a very long string, it should require some allocation?");
        CHECK_EQ(str, CTEXT("this is a short string, this is a medium length string, but still not that long, this is a very long string, it should require some allocation?"));
        string_deallocate(str);
    }

    FOUNDATION_STATIC string_t P(char* buf, size_t cap, void* ptr)
    {
        // Write into buffer, the 10 first prime numbers.
        int count = 0;
        string_t result{};
        for (int i = 2; i < 100; ++i)
        {
            bool is_prime = true;
            for (int j = 2; j < i; ++j)
            {
                if (i % j == 0)
                {
                    is_prime = false;
                    break;
                }
            }
            if (is_prime)
            {
                if (count > 0)
                    result = string_append(buf, result.length, cap, STRING_CONST(" "));
                string_const_t pstr = string_from_int_static(i, 0, 0);
                result = string_append(buf, result.length, cap, STRING_ARGS(pstr));
                ++count;
                if (count == 10)
                    break;
            }
        }
        return result;
    };

    TEST_CASE("Template Static")
    {
        CHECK_EQ(string_template_static("{0} {1} {0}", "Cool", "Cool"), CTEXT("Cool Cool Cool"));
        CHECK_EQ(string_template_static("{1} {0}", P, nullptr, 1), CTEXT("1 2 3 5 7 11 13 17 19 23 29"));
    }
}

#endif // BUILD_DEVELOPMENT
