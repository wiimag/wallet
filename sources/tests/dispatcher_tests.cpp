/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT

#include <framework/dispatcher.h>

#include <tests/test_utils.h>

#include <doctest/doctest.h>

FOUNDATION_EXTERN bool dispatcher_process_events();

TEST_SUITE("Dispatcher")
{
    TEST_CASE("Register")
    {
        SUBCASE("Default")
        {
            auto event_listener_id = dispatcher_register_event_listener("TEST_1", [](const auto& _1) { return false; });
            CHECK_NE(event_listener_id, INVALID_DISPATCHER_EVENT_LISTENER_ID);
            REQUIRE(dispatcher_unregister_event_listener(event_listener_id));
        }

        SUBCASE("Easy (No return value for handler)")
        {
            auto event_listener_id = dispatcher_register_event_listener_easy("EASY_1", [](const auto& _2) {});
            CHECK_NE(event_listener_id, INVALID_DISPATCHER_EVENT_LISTENER_ID);
            REQUIRE(dispatcher_unregister_event_listener(event_listener_id));
        }
    }

    TEST_CASE("Post Event")
    {
        SUBCASE("Default")
        {
            bool posted = false;
            auto event_listener_id = dispatcher_register_event_listener(STRING_CONST("POSTED_1"), [&posted](const auto& args)
            { 
                return (posted = true);
            });
            CHECK_NE(event_listener_id, INVALID_DISPATCHER_EVENT_LISTENER_ID);

            dispatcher_post_event("POSTED_1", nullptr, 0);
            dispatcher_process_events();

            REQUIRE(posted);
            REQUIRE(dispatcher_unregister_event_listener(event_listener_id));
        }

        SUBCASE("Easy (No return value for handler)")
        {
            bool posted = false;
            auto event_listener_id = dispatcher_register_event_listener_easy("EASY_33", [&posted](const auto& args)
            {
                posted = true;
            });
            CHECK_NE(event_listener_id, INVALID_DISPATCHER_EVENT_LISTENER_ID);
            
            dispatcher_post_event("EASY_33", nullptr, 0);
            dispatcher_process_events();

            REQUIRE(posted);
            REQUIRE(dispatcher_unregister_event_listener(event_listener_id));
        }

        SUBCASE("Post Event With Payload")
        {            
            #define EVENT_POST_42_HASH static_hash_string("POST_42", 7, 0x981017af1d50240bULL)

            hash_t posted = HASH_EMPTY_STRING;
            auto postee = [&posted](const dispatcher_event_args_t& args)
            {
                return (posted = string_hash(args.c_str(), args.size));
            };

            auto event_listener_id = dispatcher_register_event_listener(EVENT_POST_42_HASH, postee);
            CHECK_NE(event_listener_id, INVALID_DISPATCHER_EVENT_LISTENER_ID);

            static char answer[42] = "life, the universe, and everything";
            dispatcher_post_event(EVENT_POST_42_HASH, STRING_CONST_CAPACITY(answer));
            dispatcher_process_events();

            REQUIRE_EQ(posted, string_hash(STRING_CONST_CAPACITY(answer)));
            REQUIRE(dispatcher_unregister_event_listener(event_listener_id));
        }
    }

    TEST_CASE("Main Thread Dispatch")
    {
        static bool main_thread_dispatched = false;

        TEST_RENDER_FRAME([]()
        {
            if (ImGui::SmallButton("DispatchCheck"))
                dispatch([](){ main_thread_dispatched = true; });
        }, L0(CLICK_UI("DispatchCheck")));

        REQUIRE_UI("DispatchCheck");
        TEST_RENDER_FRAME([]() { /* tick in order for main thread to dispatch last frame events */});

        REQUIRE(main_thread_dispatched);
    }

    TEST_CASE("Button Event Trigger" * doctest::may_fail(true))
    {
        static bool event_sent = false;
        auto eid = dispatcher_register_event_listener("UI_EVENT", L1(event_sent = true));

        TEST_RENDER_FRAME([]()
        {
            if (ImGui::Button("Post Event"))
                dispatcher_post_event("UI_EVENT");
        }, L0(CLICK_UI("Post Event")));

        REQUIRE_UI("Post Event");

        TEST_RENDER_FRAME([](){ /* tick in order for ui event to be dispatched */});
        CHECK(dispatcher_unregister_event_listener(eid));

        REQUIRE(event_sent);
    }
}

#endif
