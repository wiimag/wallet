/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT

#include "test_utils.h"

#include <stock.h>

#include <framework/common.h>
#include <framework/database.h>
#include <framework/handle.h>

#include <foundation/uuid.h>
#include <foundation/random.h>
#include <foundation/thread.h>

#include <doctest/doctest.h>

struct kvp_t { uuid_t id; uint256_t data; };

struct price_t { uint64_t id; double price; };

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL hash_t hashint(const int& value)
{
    if (value == 0)
        return (hash_t)UINT64_MAX;
    return (hash_t)value;
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL hash_t hash_uuid(const kvp_t& kvp)
{
    return kvp.id.word[0] ^ kvp.id.word[1];
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL hash_t hash(const stock_t& value)
{
    return value.id;
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL hash_t hash(const string_const_t& value)
{
    return hash(STRING_ARGS(value));
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL void print_stock(const stock_t* s)
{
    INFO(s->id);
    INFO(s->current.close);
}

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL void print_stock_day_result(const day_result_t& ed)
{
    INFO(ed.change);
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
        db.update(k1, [&db](test_t& v)
        {
            CHECK(db.mutex.locked());
            CHECK_EQ(v.payload, 42U);
            v.payload = 24U;
        });

        db.select(k1, [&db](const test_t& v)
        {
            CHECK(db.mutex.locked());
            CHECK_EQ(v.payload, 24U);
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

        auto k1v = db[k1].value->payload;
        CHECK_EQ(k1v, 84U);
        CHECK_EQ(db[k2].value->payload, 12384U);
        REQUIRE_EQ(db.size(), 5);
    }

    TEST_CASE("Put")
    {
        database<stock_t> db;

        auto u = stock_t{ hash(STRING_CONST("U.US")) };
        auto p = stock_t{ hash(STRING_CONST("PFE.US")) };
        auto s = stock_t{ hash(STRING_CONST("SSE.V")) };
        s.current.close = 0.025;

        CHECK_EQ(db.insert(u), u.id);
        CHECK_EQ(db.insert(p), p.id);
        CHECK_EQ(db.insert(s), s.id);
        REQUIRE_EQ(db.size(), 3);

        for (const auto& e : db)
        {
            CHECK_NE(e.id, 0);
            INFO(e.current.close);
        }

        CHECK(math_real_is_nan(db[u.id]->current.close));
        CHECK_EQ(db[s.id]->current.close, 0.025);
        CHECK_EQ(db[u.id]->fetch_level, FetchLevel::NONE);

        {
            auto t = db[u.id];
            t->name = string_table_encode("Unity");
            t->exchange = string_table_encode("US");
        }

        print_stock(db[p.id]);
        // The exclude lock should be disposed only when returning from the function call.

        print_stock_day_result(db[p.id]->current);
        print_stock_day_result(db.lock(p.id)->current);

        db[s.id]->country = string_table_encode("Canada");
        db[s.id]->exchange = string_table_encode("Venture");

        db.put(stock_t{ hash(STRING_CONST("U.US")), 0, FetchLevel::REALTIME });
        REQUIRE_EQ(db.size(), 3);
        CHECK_EQ(db[u.id]->fetch_level, FetchLevel::REALTIME);

        db.put(stock_t{ hash(STRING_CONST("AMZN.US")), 1, FetchLevel::TECHNICAL_EMA });
        REQUIRE_EQ(db.size(), 4);
        CHECK_EQ(db[hash(STRING_CONST("AMZN.US"))]->fetch_level, FetchLevel::TECHNICAL_EMA);
    }

    TEST_CASE("Remove")
    {
        database<string_const_t> db;

        string_const_t jo = CTEXT("Jonathan");
        string_const_t seb = CTEXT("Sebastien");
        string_const_t steeve = CTEXT("Steeve");
        string_const_t mathilde = CTEXT("Mathilde");

        CHECK_EQ(db.insert(jo), hash(STRING_ARGS(jo)));
        CHECK_EQ(db.insert(seb), hash(STRING_ARGS(seb)));
        CHECK_EQ(db.insert(steeve), hash(STRING_ARGS(steeve)));
        REQUIRE_EQ(db.size(), 3);

        CHECK(db.contains(steeve));
        CHECK(db.contains(hash(STRING_ARGS(seb))));
        CHECK_FALSE(db.contains(mathilde));
        CHECK_FALSE(db.contains(hash(STRING_CONST("Arnold"))));
        CHECK_FALSE(db.contains(hash(STRING_CONST("Mathilde"))));

        CHECK_FALSE(db.remove(hash(STRING_ARGS(mathilde))));
        CHECK(db.remove(hash(STRING_ARGS(jo))));
        REQUIRE_EQ(db.size(), 2);

        CHECK_NE(db.insert(string_const("Arnold")), 0);
        REQUIRE_EQ(db.size(), 3);

        string_const_t removed;
        CHECK(db.remove(hash(STRING_CONST("Arnold")), &removed));
        CHECK(string_equal(STRING_ARGS(removed), STRING_CONST("Arnold")));
        REQUIRE_EQ(db.size(), 2);

        db.clear();
        CHECK_GT(db.capacity, 1);
        CHECK_EQ(db.size(), 0);
        CHECK(db.elements != nullptr); // The element array should only be cleared, not deallocated
    }

    TEST_CASE("Failures")
    {
        database<kvp_t, hash_uuid> db;

        const uuid_t u1 = uuid_generate_random();
        const uuid_t u2 = uuid_generate_random();
        db.put({ u1, {1, random64(), random64(), random64()}});
        db.put({ uuid_generate_random(), {random64(), 2, random64(), random64()} });
        db.put({ u2, {random64(), random64(), 3, random64()} });
        db.put({ u2, {random64(), random64(), random64(), 4} }); // Should not be added

        REQUIRE_EQ(db.size(), 3);

        CHECK_EQ(db.get(u1.word[0] ^ u1.word[1]).data.word[0], 1);
        CHECK_EQ(db.get(u2.word[0] ^ u2.word[1]).data.word[3], 4);

        CHECK_FALSE(db.select((hash_t)random64(), nullptr));

        CHECK(uuid_is_null(db.get((hash_t)random64()).id));
        CHECK_EQ(db.get((hash_t)random64()).data.word[0], 0ULL);
        CHECK_EQ(db.get((hash_t)random64()).data.word[1], 0ULL);
        CHECK_EQ(db.get((hash_t)random64()).data.word[2], 0ULL);
        CHECK_EQ(db.get((hash_t)random64()).data.word[3], 0ULL);

        kvp_t dummy;
        CHECK_FALSE(db.select((hash_t)random64(), dummy));
    }

    TEST_CASE("Enumerate")
    {
        database<price_t> db;

        const hash_t h1 = db.insert({ 1, 12.0 });
        const hash_t h2 = db.insert({ 2, 13.0 });
        const hash_t h3 = db.insert({ 3, 14.0 });

        REQUIRE_EQ(db.size(), 3);

        { // Shared lock raw access (unsafe)
            SHARED_READ_LOCK(db.mutex);
            int iteration_count = 0;
            foreach(p, db.elements)
            {
                INFO(iteration_count, p->id, p->price);
                CHECK(db.mutex.locked());
                iteration_count++;
            }
            CHECK(db.mutex.locked()); // db is still locked
            REQUIRE_EQ(iteration_count, 3);
        }

        { // Iteration with implicit shared lock
            int iteration_count = 0;
            for (const auto& e : db)
            {
                INFO(iteration_count, e.id, e.price);
                CHECK(db.mutex.locked());
                iteration_count++;
            }
            CHECK_FALSE(db.mutex.locked());
            REQUIRE_EQ(iteration_count, 3);
        }

        { // Iteration with implicit shared lock
            int iteration_count = 0;
            for (auto& e : db)
            {
                CHECK(db.mutex.locked());
                e.price = 55.0;
                INFO(iteration_count, e.id, e.price);
                iteration_count++;
            }
            CHECK_EQ(db.get(h1).price, 55.0);
            CHECK_EQ(db.lock(h2)->price, 55.0);
            CHECK_EQ(db[h3]->price, 55.0);
            CHECK_FALSE(db.mutex.locked());
            REQUIRE_EQ(iteration_count, 3);
        }

        { // Iteration with exclusive lock
            int iteration_count = 0;
            for(auto it = db.begin_exclusive_lock(),
                     end = db.end_exclusive_lock(); it != end; ++it)
            {
                CHECK(db.mutex.locked());
                INFO(iteration_count, it->id, it->price);
                iteration_count++;
                break; // Only iterate once and make sure lock is released as soon as possible.
            }
            CHECK_FALSE(db.mutex.locked());
            REQUIRE_EQ(iteration_count, 1);
        }
    }

    TEST_CASE("Concurrent Inserts" * doctest::timeout(30))
    {
        typedef database<price_t, hash> price_database_t;

        thread_t jobs_insert[8];
        thread_t jobs_enumerator[4];

        price_database_t db;

        const hash_t h1 = db.insert({ 1, 12.0 });
        const hash_t h2 = db.insert({ 2, 13.0 });
        const hash_t h3 = db.insert({ 3, 14.0 });

        REQUIRE_EQ(db.size(), 3);

        static atom32_t duplicates{ 0 };
        static atom32_t enumerations{ 0 };

        // Create inserter threads
        {
            constexpr auto job_insert_thread_fn = [](void* arg)->void*
            {
                price_database_t& db = *(price_database_t*)arg;
                while (!thread_try_wait(1))
                {
                    if (db.insert({ random64(), random_range(20.0, 100.0) }) == 0)
                        duplicates++;
                }
                return 0;
            };

            for (int i = 0; i < ARRAY_COUNT(jobs_insert); ++i)
            {
                string_const_t thread_name = string_format_static(STRING_CONST("test_job_%d"), i + 1);
                thread_initialize(&jobs_insert[i], job_insert_thread_fn, &db, STRING_ARGS(thread_name), (thread_priority_t)(random32() % 6), 0);
            }

            for (int i = 0; i < ARRAY_COUNT(jobs_insert); ++i)
                CHECK(thread_start(&jobs_insert[i]));
        }

        const hash_t h4 = db.put({ 4, 15.0 });

        // Create enumerator threads
        {
            constexpr auto job_enumerator_thread_fn = [](void* arg)->void*
            {
                price_database_t& db = *(price_database_t*)arg;
                while (!thread_try_wait(enumerations.load()))
                {
                    for (const auto& e : db)
                        INFO(e.id, e.price);
                    ++enumerations;
                }
                return 0;
            };

            for (int i = 0; i < ARRAY_COUNT(jobs_enumerator); ++i)
            {
                string_const_t thread_name = string_format_static(STRING_CONST("test_job_%d"), i + 1);
                thread_initialize(&jobs_enumerator[i], job_enumerator_thread_fn, &db, STRING_ARGS(thread_name), (thread_priority_t)(random32() % 6), 0);
            }

            for (int i = 0; i < ARRAY_COUNT(jobs_enumerator); ++i)
                CHECK(thread_start(&jobs_enumerator[i]));
        }

        while ((duplicates >= 1 || duplicates < 10) && db.size() < 64 * 1024)
        {
            thread_sleep(1);
            REQUIRE_EQ(db.get(h1).price, 12.0);

            thread_sleep(1);
            price_t p;
            REQUIRE(db.select(h2, p));
            REQUIRE_EQ(p.price, 13.0);

            thread_sleep(1);
            REQUIRE_EQ(db.lock(h3)->price, 14.0);

           thread_sleep(1);
           REQUIRE_EQ(db[h4]->price, 15.0);
        }

        for (int i = 0; i < ARRAY_COUNT(jobs_insert); ++i)
        {
            thread_signal(&jobs_insert[i]);
            thread_join(&jobs_insert[i]);
            thread_finalize(&jobs_insert[i]);
        }

        for (int i = 0; i < ARRAY_COUNT(jobs_enumerator); ++i)
        {
            thread_signal(&jobs_enumerator[i]);
            thread_join(&jobs_enumerator[i]);
            thread_finalize(&jobs_enumerator[i]);
        }

        INFO(db.capacity, db.size(), (int32_t)duplicates, (int32_t)enumerations);

        CHECK_GE(db.size(), 1024);
        CHECK_GT((int32_t)duplicates, 1);
        CHECK_GE((int32_t)enumerations, (int32_t)duplicates);
    }
}

#endif // BUILD_DEVELOPMENT
