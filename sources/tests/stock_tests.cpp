/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include <foundation/platform.h>

#if BUILD_DEVELOPMENT

#include "test_utils.h"

#include <stock.h>
#include <events.h>

#include <framework/dispatcher.h>

#include <doctest/doctest.h>

TEST_SUITE("Stocks")
{
    TEST_CASE("Initialize")
    {
        stock_handle_t handle{};

        CHECK_FALSE(!!handle);
        CHECK_EQ(handle.id, (hash_t)0);
        CHECK_EQ(handle.code, STRING_TABLE_NULL_SYMBOL);

        const stock_t* s = handle;
        CHECK_EQ(s, nullptr);
        CHECK_EQ(*handle, nullptr);

        CHECK(math_real_is_nan(handle->low_52));
        CHECK_EQ(handle->code, STRING_TABLE_NULL_SYMBOL);

        CHECK_EQ(stock_initialize(nullptr, 32, nullptr), STATUS_ERROR_NULL_REFERENCE);
        CHECK_EQ(stock_initialize("U.US", 0, nullptr), STATUS_ERROR_NULL_REFERENCE);
        CHECK_EQ(stock_initialize(STRING_CONST("H.TO"), nullptr), STATUS_ERROR_NULL_REFERENCE);
        
        CHECK_EQ(stock_initialize(STRING_CONST("H.TO"), &handle), STATUS_OK);
        
        CHECK_NE(handle.id, STRING_TABLE_NULL_SYMBOL);
        CHECK_NE(handle.code, STRING_TABLE_NULL_SYMBOL);

        // This will initiate a request, but it won't resolve.
        CHECK_EQ(handle->code, STRING_TABLE_NULL_SYMBOL);
    }

    TEST_CASE("Request NONE")
    {
        string_const_t code = CTEXT("U.US");
        stock_handle_t handle = stock_request(STRING_ARGS(code), FetchLevel::NONE);

        CHECK(!!handle);
        CHECK_EQ(handle.id, hash(STRING_ARGS(code)));
        CHECK_EQ(SYMBOL_CONST(handle.code), code);

        const stock_t* s = handle;
        CHECK_NE(s, nullptr);

        CHECK_EQ(s->id, handle.id);
        CHECK_EQ(s->code, handle.code);
        CHECK_EQ(s->history, nullptr);
        CHECK_EQ(s->history_count, 0);
        CHECK_EQ(s->previous, nullptr);

        CHECK(math_real_is_nan(s->current.open));
        CHECK(math_real_is_nan(s->current.close));
        CHECK_FALSE(s->is_resolving(FetchLevel::EOD, 30.0));
        CHECK_FALSE(s->is_resolving(FetchLevel::REALTIME, 0.0));
        CHECK_FALSE(s->has_resolve(FetchLevel::REALTIME));
    }

    TEST_CASE("Request REALTIME" * doctest::timeout(30.0))
    {
        string_const_t code = CTEXT("SSE.V");
        stock_handle_t handle = stock_request(STRING_ARGS(code), FetchLevel::REALTIME);

        const stock_t* s = handle;
        REQUIRE_NE(s, nullptr);
        CHECK_EQ(s->history, nullptr);
        CHECK_EQ(s->history_count, 0);
        CHECK_EQ(s->previous, nullptr);

        static bool stock_was_requested = false;
        const auto listener_id = dispatcher_register_event_listener(EVENT_STOCK_REQUESTED, [](const dispatcher_event_args_t& args)
        {
            CHECK_EQ(string_const_t{ args.c_str(), args.size }, CTEXT("SSE.V"));
            return (stock_was_requested = true);
        });

        while (!s->has_resolve(FetchLevel::REALTIME))
            dispatcher_wait_for_wakeup_main_thread();
        dispatcher_poll(nullptr);
        
        CHECK_EQ(s->fetch_level, FetchLevel::NONE);
        CHECK_FALSE(s->has_resolve(FetchLevel::EOD));
        REQUIRE_GT(s->current.date, 1);
        CHECK_GT(s->current.open, 0);
        CHECK_GT(s->current.close, 0);
        CHECK_GT(s->current.previous_close, 0);
        CHECK_GT(s->current.low, 0);
        CHECK_GT(s->current.high, 0);
        CHECK_GE(s->current.volume, 0);
        CHECK_FALSE(math_real_is_nan(s->current.change));
        CHECK_FALSE(math_real_is_nan(s->current.change_p));

        CHECK(stock_was_requested);
        CHECK(dispatcher_unregister_event_listener(listener_id));
    }
    
    TEST_CASE("Fetch Description" * doctest::timeout(30.0))
    {
        string_const_t code = CTEXT("ENB.TO");
        stock_handle_t handle = stock_request(STRING_ARGS(code), FetchLevel::REALTIME);
        
        while (handle->description.fetch() == STRING_TABLE_NULL_SYMBOL)
            dispatcher_wait_for_wakeup_main_thread();

        // This type of fetching does not fetch full level data.
        CHECK_FALSE(handle->has_resolve(FetchLevel::FUNDAMENTALS));

        string_const_t description = SYMBOL_CONST(handle->description.fetch());
        CHECK(string_starts_with(STRING_ARGS(description), STRING_CONST("Enbridge Inc.")));
    }

    TEST_CASE("Fundamentals" * doctest::timeout(30.0))
    {
        stock_handle_t handle = stock_request(STRING_CONST("SU.TO"), FetchLevel::FUNDAMENTALS);

        while (!handle->has_resolve(FetchLevel::FUNDAMENTALS))
            dispatcher_wait_for_wakeup_main_thread();
            
        REQUIRE(handle->has_resolve(FetchLevel::FUNDAMENTALS));

        const stock_t* s = handle;
        
        CHECK(s->description.initialized);
        CHECK(s->dividends_yield.initialized);

        CHECK_EQ(SYMBOL_CONST(s->symbol), CTEXT("SU"));
        CHECK_EQ(SYMBOL_CONST(s->name), CTEXT("Suncor Energy Inc"));
        CHECK_EQ(SYMBOL_CONST(s->currency), CTEXT("CAD"));
        CHECK_EQ(SYMBOL_CONST(s->exchange), CTEXT("TO"));
        
        CHECK_LE(s->low_52, handle->high_52);
        CHECK_GT(s->dividends_yield.fetch(), 0);
    }

    TEST_CASE("TECHNICAL_INDEXED_PRICE" * doctest::timeout(30.0))
    {
        stock_handle_t handle = stock_request(STRING_CONST("QQQ.US"), FetchLevel::TECHNICAL_INDEXED_PRICE);

        while (!handle->has_resolve(FetchLevel::TECHNICAL_INDEXED_PRICE))
        {
            dispatcher_update();
            dispatcher_wait_for_wakeup_main_thread();
        }

        REQUIRE(handle->has_resolve(FetchLevel::TECHNICAL_EOD));
        REQUIRE(handle->has_resolve(FetchLevel::TECHNICAL_INDEXED_PRICE));

        const stock_t* s = handle;
        REQUIRE_GT(array_size(s->history), 6000);
        CHECK_FALSE(math_real_is_nan(s->history[0].price_factor));
        CHECK_GT(s->history[0].open, 0);
        CHECK_GT(s->history[0].close, 0);
    }

    TEST_CASE("EOD" * doctest::timeout(30.0))
    {
        auto prev_history_count = 0;
        {
            stock_handle_t handle = stock_request(STRING_CONST("MSFT.US"), FetchLevel::EOD);

            while (!handle->has_resolve(FetchLevel::EOD))
                dispatcher_wait_for_wakeup_main_thread();

            REQUIRE(handle->has_resolve(FetchLevel::EOD));
            REQUIRE(handle->has_resolve(FetchLevel::TECHNICAL_INDEXED_PRICE));

            const stock_t* s = handle;
            prev_history_count = array_size(s->history);
            REQUIRE_GT(prev_history_count, 0);
            CHECK_FALSE(math_real_is_nan(s->history[0].price_factor));
            CHECK_GT(s->history[0].open, 0);
            CHECK_GT(s->history[0].close, 0);
        }

        // This will reset previous EOD data
        {            
            stock_handle_t handle = stock_request(STRING_CONST("MSFT.US"), FetchLevel::TECHNICAL_EOD);

            while (!handle->has_resolve(FetchLevel::TECHNICAL_EOD))
                dispatcher_wait_for_wakeup_main_thread();

            REQUIRE(handle->has_resolve(FetchLevel::TECHNICAL_EOD));

            const stock_t* s = handle;
            REQUIRE_EQ(array_size(s->history), prev_history_count);
        }
    }

    TEST_CASE("TECHNICAL_EOD" * doctest::timeout(30.0))
    {
        stock_handle_t handle = stock_request(STRING_CONST("NFLX.US"), FetchLevel::TECHNICAL_EOD);

        while (!handle->has_resolve(FetchLevel::TECHNICAL_EOD))
            dispatcher_wait_for_wakeup_main_thread();

        REQUIRE(handle->has_resolve(FetchLevel::TECHNICAL_EOD));

        const stock_t* s = handle;
        REQUIRE_GT(array_size(s->history), 0);
        CHECK(math_real_is_nan(s->history[0].price_factor));
        CHECK_GT(s->history[0].open, 0);
        CHECK_GT(s->history[0].close, 0);
    }

    TEST_CASE("EMA" * doctest::timeout(30.0))
    {
        stock_handle_t handle = stock_request(STRING_CONST("TSLA.US"), FetchLevel::TECHNICAL_EMA);

        while (!handle->has_resolve(FetchLevel::TECHNICAL_EMA))
        {
            dispatcher_update();
            dispatcher_wait_for_wakeup_main_thread();
        }

        REQUIRE(handle->has_resolve(FetchLevel::EOD));
            
        const stock_t* s = handle;
        REQUIRE_GT(array_size(s->history), 0);
        CHECK_FALSE(math_real_is_nan(s->history[0].ema));
        CHECK_FALSE(math_real_is_nan(s->history[0].price_factor));
    }

    TEST_CASE("SMA" * doctest::timeout(30.0))
    {
        stock_handle_t handle = stock_request(STRING_CONST("AMZN.US"), FetchLevel::TECHNICAL_EOD | FetchLevel::TECHNICAL_SMA);

        while (!handle->has_resolve(FetchLevel::TECHNICAL_SMA))
        {
            dispatcher_update();
            dispatcher_wait_for_wakeup_main_thread();
        }

        REQUIRE(handle->has_resolve(FetchLevel::TECHNICAL_EOD));

        const stock_t* s = handle;
        REQUIRE_GT(array_size(s->history), 0);
        CHECK(math_real_is_nan(s->history[0].price_factor));
        CHECK_FALSE(math_real_is_nan(s->history[0].close));
        CHECK_FALSE(math_real_is_nan(s->history[0].sma));
    }

    TEST_CASE("WMA" * doctest::timeout(30.0))
    {
        stock_handle_t handle = stock_request(STRING_CONST("SPY.US"), FetchLevel::TECHNICAL_EOD | FetchLevel::TECHNICAL_INDEXED_PRICE | FetchLevel::TECHNICAL_WMA);

        while (!handle->has_resolve(FetchLevel::TECHNICAL_WMA))
        {
            dispatcher_update();
            dispatcher_wait_for_wakeup_main_thread();
        }

        REQUIRE(handle->has_resolve(FetchLevel::TECHNICAL_WMA));

        const stock_t* s = handle;
        REQUIRE_GT(array_size(s->history), 0);
        CHECK_FALSE(math_real_is_nan(s->history[0].wma));
    }

    TEST_CASE("BBANDS" * doctest::timeout(30.0))
    {
        stock_handle_t handle = stock_request(STRING_CONST("GME.US"), FetchLevel::EOD);

        while (!handle->has_resolve(FetchLevel::EOD))
        {
            dispatcher_update();
            dispatcher_wait_for_wakeup_main_thread();
        }
    
        handle = stock_request(STRING_CONST("GME.US"), FetchLevel::REALTIME | FetchLevel::FUNDAMENTALS | FetchLevel::TECHNICAL_BBANDS);
        while (!handle->has_resolve(FetchLevel::TECHNICAL_BBANDS))
        {
            dispatcher_update();
            dispatcher_wait_for_wakeup_main_thread();
        }

        CHECK(stock_update(handle, FetchLevel::TECHNICAL_BBANDS, 0));

        REQUIRE(handle->has_resolve(FetchLevel::TECHNICAL_BBANDS));

        const stock_t* s = handle;
        REQUIRE_GT(array_size(s->history), 0);
        CHECK_FALSE(math_real_is_nan(s->history[0].lband));
        CHECK_FALSE(math_real_is_nan(s->history[0].mband));
        CHECK_FALSE(math_real_is_nan(s->history[0].uband));
    }

    TEST_CASE("SAR AND SLOPE" * doctest::timeout(60.0))
    {
        // Get 50 stock symbols from https://www.nasdaq.com/market-activity/quotes/nasdaq-ndx-index
        const char* symbols[50];
        symbols[0] = "AAPL.US"; symbols[1] = "MSFT.US"; symbols[2] = "AMZN.US"; symbols[3] = "FB.US"; symbols[4] = "GOOGL.US"; symbols[5] = "GOOG.US"; symbols[6] = "TSLA.US"; symbols[7] = "BRK-B.US"; symbols[8] = "JPM.US"; symbols[9] = "JNJ.US";
        symbols[10] = "V.US"; symbols[11] = "PG.US"; symbols[12] = "UNH.US"; symbols[13] = "HD.US"; symbols[14] = "MA.US"; symbols[15] = "VZ.US"; symbols[16] = "DIS.US"; symbols[17] = "NVDA.US"; symbols[18] = "PYPL.US"; symbols[19] = "ADBE.US";
        symbols[20] = "CMCSA.US"; symbols[21] = "NFLX.US"; symbols[22] = "BAC.US"; symbols[23] = "T.US"; symbols[24] = "PEP.US"; symbols[25] = "CRM.US"; symbols[26] = "INTC.US"; symbols[27] = "CSCO.US"; symbols[28] = "KO.US"; symbols[29] = "NKE.US";
        symbols[30] = "PFE.US"; symbols[31] = "ABT.US"; symbols[32] = "MRK.US"; symbols[33] = "WFC.US"; symbols[34] = "TMO.US"; symbols[35] = "ACN.US"; symbols[36] = "C.US"; symbols[37] = "XOM.US"; symbols[38] = "MCD.US"; symbols[39] = "ABBV.US";
        symbols[40] = "CVX.US"; symbols[41] = "ORCL.US"; symbols[42] = "AVGO.US"; symbols[43] = "LLY.US"; symbols[44] = "QCOM.US"; symbols[45] = "UNP.US"; symbols[46] = "AMGN.US"; symbols[47] = "DHR.US"; symbols[48] = "MDT.US"; symbols[49] = "NEE.US";

        // Request all of them at once with EOD level first
        stock_handle_t handles[50];
        for (int i = 0; i < 50; ++i)
        {
            string_const_t symbol = string_to_const(symbols[i]);
            handles[i] = stock_request(STRING_ARGS(symbol), FetchLevel::TECHNICAL_EOD | FetchLevel::TECHNICAL_SAR | FetchLevel::TECHNICAL_SLOPE);
            CHECK(!!handles[i]);
        }

        // Wait for all of them to resolve
        for (int i = 0; i < 50; ++i)
        {
            while (!handles[i]->has_resolve(FetchLevel::TECHNICAL_SAR))
            {
                dispatcher_update();
                dispatcher_wait_for_wakeup_main_thread();
            }

            while (!handles[i]->has_resolve(FetchLevel::TECHNICAL_SLOPE))
            {
                dispatcher_update();
                dispatcher_wait_for_wakeup_main_thread();
            }
        }

        // Check all of them
        for (int i = 0; i < 50; ++i)
        {
            const stock_t* s = handles[i];
            string_const_t symbol = SYMBOL_CONST(s->code);
            INFO(symbol);
            REQUIRE_GT(array_size(s->history), 0);
            CHECK_FALSE(math_real_is_nan(s->history[0].slope));
        }
    }

    TEST_CASE("CCI" * doctest::timeout(30.0))
    {
        // Get 50 stock symbols from https://www.slickcharts.com/sp500
        const char* symbols[50];
        symbols[0] = "MMM.US"; symbols[1] = "ABT.US"; symbols[2] = "ABBV.US"; symbols[3] = "ABMD.US"; symbols[4] = "ACN.US";
        symbols[5] = "ATVI.US"; symbols[6] = "ADBE.US"; symbols[7] = "AMD.US"; symbols[8] = "AAP.US"; symbols[9] = "AES.US";
        symbols[10] = "AMG.US"; symbols[11] = "AFL.US"; symbols[12] = "A.US"; symbols[13] = "APD.US"; symbols[14] = "AKAM.US";
        symbols[15] = "ALK.US"; symbols[16] = "ALB.US"; symbols[17] = "ARE.US"; symbols[18] = "ALXN.US"; symbols[19] = "ALGN.US";
        symbols[20] = "ALLE.US"; symbols[21] = "LNT.US"; symbols[22] = "ALL.US"; symbols[23] = "GOOGL.US"; symbols[24] = "GOOG.US";
        symbols[25] = "MO.US"; symbols[26] = "AMZN.US"; symbols[27] = "AMCR.US"; symbols[28] = "AEE.US"; symbols[29] = "AAL.US";
        symbols[30] = "AEP.US"; symbols[31] = "AXP.US"; symbols[32] = "AIG.US"; symbols[33] = "AMT.US"; symbols[34] = "AWK.US";
        symbols[35] = "AMP.US"; symbols[36] = "ABC.US"; symbols[37] = "AME.US"; symbols[38] = "AMGN.US"; symbols[39] = "APH.US";
        symbols[40] = "ADI.US"; symbols[41] = "ANSS.US"; symbols[42] = "ANTM.US"; symbols[43] = "AON.US"; symbols[44] = "AOS.US";
        symbols[45] = "APA.US"; symbols[46] = "AIV.US"; symbols[47] = "AAPL.US"; symbols[48] = "AMAT.US"; symbols[49] = "APTV.US";

        // Request all of them at once with EOD level first
        stock_handle_t handles[50];
        for (int i = 0; i < 50; ++i)
        {
            string_const_t symbol = string_to_const(symbols[i]);
            handles[i] = stock_request(STRING_ARGS(symbol), FetchLevel::TECHNICAL_EOD);
            CHECK(!!handles[i]);
        }
        
        // Wait for all of them to resolve
        for (int i = 0; i < 50; ++i)
        {
            while (!handles[i]->has_resolve(FetchLevel::TECHNICAL_EOD))
            {
                dispatcher_update();
                dispatcher_wait_for_wakeup_main_thread();
            }
        }

        for (int i = 0; i < 50; ++i)
        {
            string_const_t symbol = string_to_const(symbols[i]);
            handles[i] = stock_request(STRING_ARGS(symbol), FetchLevel::TECHNICAL_CCI);
            CHECK(!!handles[i]);
        }

        // Wait for all of them to resolve
        for (int i = 0; i < 50; ++i)
        {
            while (!handles[i]->has_resolve(FetchLevel::TECHNICAL_CCI))
            {
                dispatcher_update();
                dispatcher_wait_for_wakeup_main_thread();
            }
        }
        
        // Check all of them
        for (int i = 0; i < 50; ++i)
        {
            const stock_t* s = handles[i];
            string_const_t symbol = SYMBOL_CONST(s->code);
            INFO(symbol);
            REQUIRE_GT(array_size(s->history), 0);
            CHECK_FALSE(math_real_is_nan(s->history[0].cci));
        }
    }

    TEST_CASE("Invalid Request")
    {
        stock_handle_t handle = stock_request(nullptr, 0, FetchLevel::NONE);

        CHECK_FALSE(!!handle);
        CHECK_EQ(handle.id, (hash_t)0);
        CHECK_EQ(handle.code, STRING_TABLE_NULL_SYMBOL);

        const stock_t* s = handle;
        CHECK_EQ(s, nullptr);
        CHECK_EQ(*handle, nullptr);
    }

    TEST_CASE("TODO Concurrent Requests")
    {
        // Initiate multiple requests in many threads
        // Actively check resolve status in the main thread without explicitly locking stock pointers (which are cached once)
    }

    TEST_CASE("Request REALTIMEx2" * doctest::timeout(90.0) * doctest::may_fail() * doctest::skip(true))
    {
        string_const_t code = CTEXT("AAPL.US");
        stock_handle_t handle = stock_request(STRING_ARGS(code), FetchLevel::REALTIME);

        const stock_t* s = handle;
        CHECK_EQ(s->previous, nullptr);

        while (!s->has_resolve(FetchLevel::REALTIME))
            dispatcher_wait_for_wakeup_main_thread();

        const time_t current_date = s->current.date;
        REQUIRE_GT(current_date, 1);

        while (s->current.date == current_date && s->fetch_errors == 0)
        {
            dispatcher_wait_for_wakeup_main_thread();
            handle->resolved_level = FetchLevel::NONE;
            stock_update(handle, FetchLevel::REALTIME);
            dispatcher_wait_for_wakeup_main_thread();
        }

        REQUIRE_GE(array_size(handle->previous), 1);
        REQUIRE_EQ(handle->previous[0].date, current_date);
    }

    TEST_CASE("Failures")
    {
        stock_handle_t handle = stock_request(nullptr, 0, FetchLevel::NONE);
        CHECK_FALSE(stock_update(handle, FetchLevel::TECHNICAL_BBANDS));
    }
}

#endif // BUILD_DEVELOPMENT
