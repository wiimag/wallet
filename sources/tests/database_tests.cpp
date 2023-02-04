/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT
 
#include "test_utils.h"

#include <framework/common.h>
#include <framework/database.h>

#include <foundation/random.h>

#include <doctest/doctest.h>

FOUNDATION_FORCEINLINE hash_t hashint(const int& value)
{
    if (value == 0)
        return (hash_t)UINT64_MAX;
    return (hash_t)value;
}

TEST_SUITE("Database")
{
    TEST_CASE("Insert And Grow")
    {
        database<int, hashint> db;
        
        CHECK(db.empty());
        
        size_t start_capacity = db.capacity;
        CHECK_GT(db.capacity, 1);

        CHECK_EQ(db.insert(0), (hash_t)UINT64_MAX);
        CHECK_FALSE(db.empty());

        CHECK_EQ(db.insert(1), (hash_t)1);
        CHECK_EQ(db.insert(1), (hash_t)0); // Should not be added again.
        CHECK_EQ(db.insert(2), (hash_t)2);
        CHECK_EQ(db.insert(3), (hash_t)3);
        
        CHECK_FALSE(db.empty());
        REQUIRE_EQ(db.size(), 4);

        // Generate 300 random numbers in an array.
        int random_numbers[1024];
        for (int i = 0; i < ARRAY_COUNT(random_numbers); ++i)
            random_numbers[i] = random32();

        // Remove duplicates
        size_t duplicates = 0;
        for (int i = 0; i < ARRAY_COUNT(random_numbers); ++i)
        {
            for (int j = i + 1; j < ARRAY_COUNT(random_numbers); ++j)
            {
                if (random_numbers[i] == random_numbers[j])
                {
                    random_numbers[j] = 0;
                    duplicates++;
                }
            }
        }

        for (int i = 0, r = 0; i < min(ARRAY_COUNT(random_numbers) - duplicates, start_capacity); ++i)
        {
            if (random_numbers[r] != 0)
            {
                CHECK_NE(db.insert(random_numbers[r]), 0);
                r++;
            }
        }

        CHECK_GT(db.capacity, start_capacity);
    }

    TEST_CASE("Update")
    {
        struct test_t
        {
            int a, b; // Key
            double payload;
        };

        database<test_t, [](const test_t& v){ return hash(&v, sizeof(int) * 2); }> db;

        CHECK(db.empty());

        hash_t k1 = db.insert({42, 24, 55.44});
        CHECK_NE(k1, 0);

        test_t v;
        CHECK(db.select(k1, v));
        CHECK_EQ(v.payload, 55.44);

        CHECK_EQ(db.insert({ 42, 24, 88.44 }), (hash_t)0); // Should not be added again.
        REQUIRE_EQ(db.size(), 1);

        CHECK(db.select(k1, v));
        CHECK_EQ(v.payload, 55.44);

        // Update the element with some new data.
        CHECK(db.update({ 42, 24, 88.44 }));
        REQUIRE_EQ(db.size(), 1);

        CHECK(db.select(k1, v));
        CHECK_EQ(v.payload, 88.44);

        // Try to update something that was never inserted
        CHECK_FALSE(db.update({ 424242, 24, 88.44 }));
        REQUIRE_EQ(db.size(), 1);
    }

    TEST_CASE("Select And Update")
    {
        struct test_t
        {
            const char* key;
            uint32_t payload;
        };

        database<test_t, [](const test_t& v) { return hash(v.key, 4); }> db;

        CHECK(db.empty());

        hash_t k1 = db.insert({ "ABCD", 42U });
        CHECK_NE(k1, 0);
        
        // Select and update data while lock is active
        db.select(k1, [&db](test_t& v) 
        {
            CHECK(db.mutex.locked());
            CHECK_EQ(v.payload, 42U);
            v.payload = 24U;
        });

        test_t v;
        CHECK(db.select(k1, v));
        CHECK_EQ(v.payload, 24);
        
        REQUIRE_EQ(db.size(), 1);
    }

    TEST_CASE("Lock And Update")
    {
        struct test_t
        {
            const char* key;
            uint32_t payload;
        };

        database < test_t, [](const test_t& v) { return hash(v.key, string_length(v.key)); } > db;

        CHECK(db.empty());

        const hash_t k1 = db.insert({ "ZOO", 442U });
        CHECK_NE(k1, 0);

        db.insert({ "U.US", 0U });
        db.insert({ "GFL.US", 1U });
        const hash_t k2 = db.insert({ "PFE.US", 2U });
        db.insert({ "APPL.US", 3U });
        REQUIRE_EQ(db.size(), 5);

        CHECK_FALSE(db.mutex.locked());
        if (const auto& lock = db.lock(k1))
        {
            REQUIRE(lock.m);
            REQUIRE(lock.value);
            CHECK(lock.m->locked());
            CHECK_EQ(lock.value->payload, 442U);
            lock.value->payload = 84U;
        }

        CHECK_FALSE(db.mutex.locked());

        if (const auto& lock = db.lock(k2))
        {
            REQUIRE(lock.m);
            REQUIRE(lock.value);
            CHECK(lock.m->locked());
            CHECK_EQ(lock.value->payload, 2U);
            lock.value->payload = 12384U;
        }

        if (const auto& lock = db.lock(0x6554))
        {
            FAIL("Should not be here");
        }
        CHECK_FALSE(db.mutex.locked());

        CHECK_EQ(db[k1].payload, 84U);
        CHECK_EQ(db[k2].payload, 12384U);
        REQUIRE_EQ(db.size(), 5);
    }
}

#endif // BUILD_DEVELOPMENT
