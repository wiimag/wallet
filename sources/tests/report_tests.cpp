/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2023 Wiimag Inc. All rights reserved.
 */

#include <framework/tests/test_utils.h>

#if 1 //BUILD_TESTS

#include <title.h>
#include <report.h>
#include <wallet.h>

TEST_SUITE("Report")
{
    TEST_CASE("Create")
    {
        string_t name = string_random(SHARED_BUFFER(16));
        report_handle_t handle = report_allocate(STRING_ARGS(name));
        CHECK_FALSE(uuid_is_null(handle));

        report_t* report = report_get(handle);
        CHECK(report != 0);

        CHECK_EQ(report_name(report), name);

        report_deallocate(handle);
    }

    TEST_CASE("Buy & Sell Some")
    {
        string_t name = string_random(SHARED_BUFFER(16));
        report_handle_t handle = report_allocate(STRING_ARGS(name));
        report_t* report = report_get(handle);
        CHECK(report != 0);
        CHECK_NE(report->wallet, nullptr);

        // Make sure we work in CAD
        string_deallocate(report->wallet->preferred_currency.str);
        report->wallet->preferred_currency = string_clone(STRING_CONST("CAD"));

        // Add a title
        title_t* title = report_add_title(report, STRING_CONST("SXP.TO"));
        CHECK(title != 0);
        CHECK(report->dirty);

        // Buy some shares
        report_title_buy(report, title, string_to_date(STRING_CONST("2023-06-14")), 5.0, 2.0);
        CHECK_EQ(array_size(report->titles), 1);

        CHECK(report_sync_titles(report));
        CHECK_GT(report->total_value, 0);
        CHECK_EQ(report->total_investment, 10.0);
        CHECK_EQ(report->total_value < report->total_investment, report->total_gain < 0);
        CHECK_EQ(report->total_value < report->total_investment, report->total_gain_p < 0);
        CHECK_EQ(title->average_price, 2.0);
        CHECK_EQ(title->average_quantity, 5);

        const double previousTotalValue = report->total_value;

        // Buy some share again at a different price
        report_title_buy(report, title, string_to_date(STRING_CONST("2023-06-15")), 10.0, 1.0);
        CHECK_EQ(array_size(report->titles), 1);

        CHECK(report_sync_titles(report));
        CHECK_GT(report->total_value, previousTotalValue);
        CHECK_EQ(report->total_investment, 20.0);
        CHECK_EQ(report->total_value < report->total_investment, report->total_gain < 0);
        CHECK_EQ(report->total_value < report->total_investment, report->total_gain_p < 0);
        CHECK_EQ(title->average_price, 20.0 / 15.0);
        CHECK_EQ(title->average_quantity, 15);
        CHECK_EQ(title->average_price_rated, title->average_price);

        // Sell 5 shares
        report_title_sell(report, title, string_to_date(STRING_CONST("2023-06-16")), 5.0, 1.0);

        CHECK(report_sync_titles(report));
        CHECK_EQ(report->total_investment, 15.0);
        CHECK_EQ(title->average_price, 1.5);
        CHECK_EQ(title->average_quantity, 10);
        CHECK_EQ(title->average_price_rated, 1.5);

        // Transaction should now have a xcg and split factor
        auto orders = title->data["orders"];
        CHECK_EQ(config_size(orders), 3);
        for (auto e : orders)
        {
            CHECK_EQ(e["xcg"].as_number(), 1.0);
            CHECK_EQ(e["split"].as_number(), 1.0);
        }

        report_deallocate(handle);
    }

    TEST_CASE("Buy, Split and Sell")
    {
        string_t name = string_random(SHARED_BUFFER(16));
        report_handle_t handle = report_allocate(STRING_ARGS(name));
        report_t* report = report_get(handle);

        // Make sure we work in CAD
        string_deallocate(report->wallet->preferred_currency.str);
        report->wallet->preferred_currency = string_clone(STRING_CONST("CAD"));

        // Add a title
        title_t* title = report_add_title(report, STRING_CONST("SHOP.TO"));
        CHECK_EQ(array_size(report->titles), 1);

        // Buy some shares
        report_title_buy(report, title, string_to_date(STRING_CONST("2021-12-29")), 10.00, 1769.45);

        CHECK(report_sync_titles(report));
        CHECK_EQ(report->total_investment, 17694.50);

        // Since we bought before the split, we should have a split factor and the average price should be adjusted
        CHECK_NEAR_EQ(title->average_quantity, 100);
        CHECK_NEAR_EQ(title->average_price, 176.945);

        // Sell all shares
        report_title_sell(report, title, string_to_date(STRING_CONST("2021-12-30")), 10.00, 1825.00);

        CHECK(report_sync_titles(report));
        CHECK_EQ(report->total_investment, 0);
        CHECK_EQ(title->average_price, 0);
        CHECK_EQ(title->average_quantity, 0);
        CHECK_NEAR_EQ(report->wallet->sell_total_gain, 18250.0 - 17694.50);

        // Check split factor for all transactions
        auto orders = title->data["orders"];
        CHECK_EQ(config_size(orders), 2);
        for (auto e : orders)
        {
            CHECK_NEAR_EQ(e["split"].as_number(), 0.1);
        }

        report_deallocate(handle);
    }

    TEST_CASE("TODO: NTR.TO")
    {
        /*
        "NTR.TO" = {
		    orders = [
			    { date = "2023-05-03" buy = true qty = 110.00 price = 93.54 split = 1.00 xcg = 1.00 }
			    { date = "2023-06-21" sell = true qty = 110.00 price = 77.61 split = 1.00 xcg = 1.00 }
			    { date = "2023-06-22" buy = true qty = 140.00 price = 76.76 split = 1.00 xcg = 1.00 }
		    ]
	    }
        */
     }
}

#endif // BUILD_TESTS
