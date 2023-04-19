/*
 * Copyright 2022-2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_TESTS
 
#include "test_utils.h"

#include <framework/common.h>
#include <framework/generics.h>

#include <doctest/doctest.h>

static int string_deallocate_count = 0;

TEST_SUITE("Generics")
{
    TEST_CASE("Fixed Loop")
    {
        string_deallocate_count = 0;
        generics::fixed_loop<string_t, 3, [](string_t& e)
        {
            string_deallocate_count++;
            string_deallocate(e.str);
        }> expressions;

        CHECK_EQ(expressions.index, -1);
        CHECK_EQ(expressions.count, 0);
        CHECK_EQ(expressions.capacity, 3);

        expressions.push(string_clone(STRING_CONST("1")));
        CHECK_EQ(expressions.index, 0);
        CHECK_EQ(expressions.size(), 1);
        CHECK_EQ(expressions.capacity, 3);
        CHECK_EQ(string_deallocate_count, 0);
        
        expressions.push(string_clone(STRING_CONST("2")));
        CHECK_EQ(expressions.index, 1);
        CHECK_EQ(expressions.size(), 2);
        
        expressions.push(string_clone(STRING_CONST("3")));
        CHECK_EQ(expressions.index, 2);
        CHECK_EQ(expressions.size(), 3);
        CHECK_EQ(expressions.current(), CTEXT("3"));

        expressions.push(string_clone(STRING_CONST("4")));
        CHECK_EQ(expressions.index, 0);
        CHECK_EQ(expressions.size(), 3);
        CHECK_EQ(expressions[2], CTEXT("3"));
        CHECK_EQ(string_deallocate_count, 1);

        expressions.push(string_clone(STRING_CONST("5")));
        CHECK_EQ(expressions.index, 1);
        CHECK_EQ(expressions.size(), 3);
        CHECK_EQ(string_deallocate_count, 2);
        CHECK_EQ(expressions[-1], CTEXT("4"));
        
        expressions.push(string_clone(STRING_CONST("6")));
        CHECK_EQ(expressions.index, 2);
        CHECK_EQ(expressions.size(), 3);
        CHECK_EQ(string_deallocate_count, 3);

        expressions.push(string_clone(STRING_CONST("7")));
        CHECK_EQ(expressions.index, 0);
        CHECK_EQ(expressions.size(), 3);
        CHECK_EQ(string_deallocate_count, 4);
        CHECK_EQ(expressions[5], CTEXT("6"));
        CHECK_EQ(expressions[0], CTEXT("7"));
        CHECK_EQ(expressions[-5], CTEXT("5"));

        int i = 0;
        for (const auto& e : expressions)
        {
            CHECK_EQ(e.str[0], '7' - i);
            i++;
        }
        CHECK_EQ(i, 3);

        expressions.clear();
        CHECK_EQ(expressions.index, -1);
        CHECK_EQ(expressions.count, 0);
        CHECK_EQ(expressions.capacity, 3);
        CHECK_EQ(string_deallocate_count, 7);
    }

    TEST_CASE("Fixed Loop - Move")
    {
        generics::fixed_loop<unsigned, 10> numbers;

        CHECK_EQ(numbers.index, -1);
        CHECK_EQ(numbers.count, 0);
        CHECK_EQ(numbers.capacity, 10);

        /*pushed out*/numbers.push(24); numbers.push(74); numbers.push(23); /*pushed out*/numbers.push(674); numbers.push(1224);
        numbers.push(12343322); numbers.push(664); numbers.push(466); numbers.push(11114); numbers.push(3434);
        numbers.push(10004); numbers.push(124); numbers.push(42);
        CHECK_EQ(numbers.index, 2);
        CHECK_EQ(numbers.size(), 10);
        CHECK_EQ(numbers.capacity, 10);

        CHECK_FALSE(numbers.contains(23)); // 23 was pushed out
        CHECK_FALSE(numbers.contains(21));
        CHECK(numbers.contains(11114));
        CHECK(numbers.includes([](const unsigned& v) { return v == 466; }));
        CHECK(numbers.includes<unsigned>([](const unsigned& a, const unsigned& b) { return a == b; }, 3434));

        CHECK_EQ(numbers.move(0), 42);
        CHECK_EQ(numbers.move(-1), 124);
        CHECK_EQ(numbers.move(+1), 42);
        CHECK_EQ(numbers.move(+5), 466);
        CHECK_EQ(numbers.move(-2), 12343322);
    }
}

#endif // BUILD_TESTS
