/*
 * Copyright 2023 - All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT

#include "test_utils.h"

#include <framework/imgui.h>
#include <framework/imcache.h>
#include <framework/window.h>

#include <foundation/thread.h>

template<typename T, T AndReturn> T wait_one(const im_cache_args_t& args)
{ 
    thread_sleep(1000);
    return AndReturn; 
};

TEST_SUITE("ImCache")
{
    TEST_CASE("Basic")
    {
        float f1 = imcache("test1", [](auto){ return 33.0f; }, -1.0f, nullptr, SIZE_C(0), ImCacheFlags::None);
        float f2 = imcache("test2", [](auto){ return 43.0f; }, -2.0f, nullptr, SIZE_C(0), ImCacheFlags::None);

        int itr = 0;
        while (f1 != 33.0f || f2 != 43.0f)
        {
            f1 = imcache("test1", -1.0f);
            f2 = imcache("test2", -2.0f);
            ++itr;
        }

        MESSAGE("Iterations: ", itr);
        CHECK_EQ(f1, 33.0f);
        CHECK_EQ(f2, 43.0f);
    }    

    TEST_CASE("Boolean")
    {
        static int counter = 0;
        tick_t s = time_current();
        auto win = window_open("Test", [](window_handle_t win)
        {
            constexpr auto f = [](const im_cache_args_t& args)
            { 
                thread_sleep(1000);
                return true; 
            };

            if (imcache<bool>("bool", f, false))
                window_close(win);

            counter++;

        }, WindowFlags::Transient);

        REQUIRE(window_valid(win));

        while (window_valid(win))
        {
            dispatcher_update();
            window_update();
        }

        CHECK_GT(counter, 0);
        CHECK_GE(time_elapsed(s), 1.0);
    }

    TEST_CASE("Double")
    {
        auto win = window_open("Test Doubles", [](window_handle_t win)
        {
            if (imcache<double>("double", wait_one<double, 34.0>, 0.0))
                window_close(win);
        }, WindowFlags::Transient);
        REQUIRE(window_valid(win));

        tick_t timeout = time_current();
        while (time_elapsed(timeout) < 30.0 && window_valid(win))
        {
            window_update();
            dispatcher_update();
        }

        REQUIRE_FALSE(window_valid(win));
    }
}

#endif // BUILD_DEVELOPMENT
