/*
 * License: https://wiimag.com/LICENSE
 * Copyright 2023 Wiimag Inc. All rights reserved.
 */

#include <foundation/platform.h>

#if BUILD_TESTS

#include <framework/tests/test_utils.h>

#include <stock.h>
#include <events.h>
#include <scripts.h>

#include <framework/expr.h>
#include <framework/dispatcher.h>

#include <foundation/system.h>

#include <doctest/doctest.h>

// 500 stock symbols ending with .US or .TO
static const char* stock500[] = {
    "U.US", "ACN.US", "ATVI.US", "ADBE.US", "AMD.US", "AAP.US", "AES.US",
    "AFL.US", "A.US", "APD.US", "AKAM.US", "ALK.US", "ALB.US", "ARE.US", "ALGN.US", "ALLE.US",
    "LNT.US", "ALL.US", "GOOGL.US", "GOOG.US", "MO.US", "AMCR.US", "AEE.US", "AAL.US", "AEP.US",
    "AXP.US", "AIG.US", "AMT.US", "AWK.US", "AMP.US", "AME.US", "AMGN.US", "APH.US", "ADI.US",
    "ANSS.US", "AON.US", "AOS.US", "APA.US", "AIV.US", "AAPL.US", "AMAT.US", "APTV.US", "ADM.US",
    "ANET.US", "AJG.US", "AIZ.US", "ATO.US", "T.US", "ADSK.US", "ADP.US", "AZO.US", "AVB.US",
    "AVY.US", "BKR.US", "BK.US", "BAX.US", "BDX.US", "BRK-B.US", "BBY.US", "BIIB.US",
    "BLK.US", "BA.US", "BKNG.US", "BWA.US", "BXP.US", "BSX.US", "BMY.US", "BR.US", "BF-B.US",
    "CHRW.US", "CDNS.US", "CPB.US", "COF.US", "CAH.US", "KMX.US", "CCL.US", "CARR.US", "CTLT.US",
    "CAT.US", "CBOE.US", "CBRE.US", "CDW.US", "CE.US", "CNC.US", "CNP.US", "CF.US", "SCHW.US",
    "CHTR.US", "CVX.US", "CMG.US", "CB.US", "CHD.US", "CI.US", "CINF.US", "CTAS.US", 
    "CFG.US", "CLX.US", "CME.US", "CMS.US", "CTSH.US", "CL.US", "CMCSA.US", "CMA.US",
    "CAG.US", "COP.US", "ED.US", "STZ.US", "COO.US", "CPRT.US", "GLW.US", "CTVA.US", "COST.US",
    "COTY.US", "CCI.US", "CSX.US", "CMI.US", "CVS.US", "DHI.US", "DHR.US", "DRI.US", "DVA.US", "DE.US",
    "DAL.US", "XRAY.US", "DVN.US", "DXCM.US", "FANG.US", "DLR.US", "DFS.US", "DISH.US",
    "DG.US", "DLTR.US", "D.US", "DOV.US", "DOW.US", "DTE.US", "DUK.US", "DD.US", "DXC.US",
    "EMN.US", "ETN.US", "EBAY.US", "ECL.US", "EIX.US", "EW.US", "EA.US", "EMR.US", "ETR.US",
    "EOG.US", "EFX.US", "EQIX.US", "EQR.US", "ESS.US", "EL.US", "EVRG.US", "ES.US", "EXC.US",
    "EXPE.US", "EXPD.US", "EXR.US", "XOM.US", "FFIV.US", "META.US", "FAST.US", "FRT.US", "FDX.US", "FIS.US",
    "FITB.US", "FE.US", "FLT.US", "FLS.US", "FMC.US", "F.US", "FTNT.US",
    "FTV.US", "FOXA.US", "FOX.US", "BEN.US", "FCX.US", "GPS.US", "GRMN.US", "IT.US", "GD.US",
    "GE.US", "GIS.US", "GM.US", "GPC.US", "GILD.US", "GL.US", "GPN.US", "GS.US", "GWW.US", "HAL.US",
    "HBI.US", "HIG.US", "HAS.US", "HCA.US", "PEAK.US", "HSIC.US", "HSY.US", "HES.US", "HPE.US", "HLT.US",
    "HOLX.US", "HD.US", "HON.US", "HRL.US", "HST.US", "HWM.US", "HPQ.US", "HUM.US", "HBAN.US",
    "HII.US", "IDXX.US", "ITW.US", "ILMN.US", "INCY.US", "IR.US", "ICE.US", "IBM.US",
    "IP.US", "IPG.US", "IFF.US", "INTU.US", "ISRG.US", "IVZ.US", "IPGP.US", "IQV.US", "IRM.US", "JKHY.US",
    "J.US", "JBHT.US", "SJM.US", "JNJ.US", "JCI.US", "JPM.US", "JNPR.US", "K.US", "KEY.US",
    "KEYS.US", "KMB.US", "KIM.US", "KMI.US", "KLAC.US", "KSS.US", "KHC.US", "KR.US", "LHX.US",
    "LH.US", "LRCX.US", "LW.US", "LVS.US", "LEG.US", "LDOS.US", "LEN.US", "LLY.US", "LNC.US", "LIN.US",
    "LYV.US", "LKQ.US", "LMT.US", "L.US", "LOW.US", "LYB.US", "MTB.US", "MRO.US", "MPC.US", "MKTX.US",
    "MAR.US", "MMC.US", "MLM.US", "MAS.US", "MA.US", "MKC.US", "MCD.US", "MCK.US", "MDT.US",
    "MRK.US", "MET.US", "MTD.US", "MGM.US", "MCHP.US", "MU.US", "MAA.US", "MHK.US", "TAP.US",
    "MDLZ.US", "MNST.US", "MCO.US", "MS.US", "MOS.US", "MSI.US", "MSCI.US", "NDAQ.US", "NOV.US",
    "NKTR.US", "NTAP.US", "NWL.US", "NEM.US", "NWSA.US", "NWS.US", "NEE.US", 
    "NI.US", "JWN.US", "NSC.US", "NTRS.US", "NOC.US", "NCLH.US", "NRG.US", "NUE.US",
    "NVDA.US", "NVR.US", "ORLY.US", "OXY.US", "ODFL.US", "OMC.US", "OKE.US", "ORCL.US", "PCAR.US", "PKG.US",
    "PH.US", "PAYX.US", "PAYC.US", "PYPL.US", "PNR.US", "PEP.US", "PRGO.US", "PFE.US",
    "PM.US", "PSX.US", "PNW.US", "PXD.US", "PNC.US", "POOL.US", "PPG.US", "PPL.US", "PFG.US", "PG.US",
    "PGR.US", "PLD.US", "PRU.US", "PTC.US", "PEG.US", "PSA.US", "PHM.US", "PVH.US", "QRVO.US", "PWR.US",
    "QCOM.US", "DGX.US", "RL.US", "RJF.US", "RTX.US", "O.US", "REG.US", "REGN.US", "RF.US", "RSG.US",
    "RMD.US", "RHI.US", "ROK.US", "ROL.US", "ROP.US", "ROST.US", "RCL.US", "SPGI.US", "SBAC.US",
    "SLB.US", "STX.US", "SEE.US", "SRE.US", "NOW.US", "SHW.US", "SPG.US", "SWKS.US", "SLG.US", "SNA.US",
    "SO.US", "LUV.US", "SWK.US", "SBUX.US", "STT.US", "STE.US", "SYK.US", "SYF.US", "SNPS.US",
    "SYY.US", "TMUS.US", "TROW.US", "TTWO.US", "TPR.US", "TGT.US", "TEL.US", "TDY.US", "TFX.US", "TER.US",
    "TXN.US", "TXT.US", "TMO.US", "TJX.US", "TSCO.US", "TT.US", "TDG.US", "TRV.US", "TRMB.US",
    "TFC.US", "TYL.US", "TSN.US", "UDR.US", "ULTA.US", "USB.US", "UAA.US", "UA.US", "UNP.US",
    "UAL.US", "UNH.US", "UPS.US", "URI.US", "UHS.US", "UNM.US", "VFC.US", "VLO.US", "VTR.US",
    "VRSN.US", "VRSK.US", "VZ.US", "VRTX.US", "V.US", "VNO.US", "VMC.US", "WRB.US", "WAB.US",
    "WMT.US", "WBA.US", "DIS.US", "WM.US", "WAT.US", "WEC.US", "WFC.US", "WELL.US", "WST.US", "WDC.US",
    "WU.US", "WRK.US", "WY.US", "WHR.US", "WMB.US", "WYNN.US", "XEL.US", "XRX.US", "XYL.US", 
    "YUM.US", "ZBRA.US", "ZBH.US", "ZION.US", "ZTS.US"
};

// Invalid stock: "WLTW.US", "VIAC.US", "VAR.US", "TWTR.US", "TIF.US", "PBCT.US", "NLOK.US", "ALXN.US", "NBL.US", "NLSN.US", "MYL.US"
//                "MXIM.US", "LB.US", "KSU.US", "INFO.US", "HFC.US", "FLIR.US", "ETFC.US", "DRE.US"

TEST_SUITE("Stocks")
{
    TEST_CASE("stock_get_time_range" * doctest::timeout(30))
    {
        string_const_t symbol = CTEXT("NPI.TO");

        CHECK_FALSE(stock_get_time_range(STRING_CONST("INVALID.US"), nullptr));
        CHECK_FALSE(stock_get_time_range(nullptr, 0, nullptr));

        time_t start = 0, end = 0;
        CHECK(stock_get_time_range(STRING_ARGS(symbol), &start, &end, 10.0));
        CHECK_NE(start, 0);
        CHECK_NE(end, 0);
        CHECK_LT(start, end);
    }

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
        CHECK_EQ(stock_initialize("MFC.TO", 0, nullptr), STATUS_ERROR_NULL_REFERENCE);
        CHECK_EQ(stock_initialize(STRING_CONST("H.TO"), nullptr), STATUS_ERROR_NULL_REFERENCE);
        
        CHECK_EQ(stock_initialize(STRING_CONST("H.TO"), &handle), STATUS_OK);
        
        CHECK_NE(handle.id, STRING_TABLE_NULL_SYMBOL);
        CHECK_NE(handle.code, STRING_TABLE_NULL_SYMBOL);

        // This will initiate a request, but it won't resolve.
        CHECK_EQ(handle->code, STRING_TABLE_NULL_SYMBOL);
    }

    TEST_CASE("Request NONE")
    {
        string_const_t code = CTEXT("ABX.TO");
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
        CHECK(math_real_is_nan(s->current.adjusted_close));
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
            CHECK_EQ(args.size, sizeof(stock_realtime_t));
            stock_realtime_t* stock_realtime = (stock_realtime_t*)args.data;
            string_const_t code{ stock_realtime->code, string_length(stock_realtime->code) };
            CHECK_EQ(code, CTEXT("SSE.V"));
            return (stock_was_requested = true);
        });

        stock_realtime_t realtime;
        realtime.price = 0.03;
        realtime.volume = 100;
        realtime.timestamp = time_now();
        string_copy(STRING_BUFFER(realtime.code), STRING_ARGS(code));

        while (!s->has_resolve(FetchLevel::REALTIME))
            dispatcher_wait_for_wakeup_main_thread();
        dispatcher_post_event(EVENT_STOCK_REQUESTED, (void*)&realtime, sizeof(realtime), DISPATCHER_EVENT_OPTION_COPY_DATA);
        dispatcher_poll(nullptr);
        
        REQUIRE_GT(s->current.date, 1);
        CHECK_GT(s->current.open, 0);
        CHECK_GT(s->current.adjusted_close, 0);
        CHECK_GT(s->current.previous_close, 0);
        CHECK_GT(s->current.low, 0);
        CHECK_GT(s->current.high, 0);
        CHECK_GE(s->current.volume, 0);
        CHECK_FALSE(math_real_is_nan(s->current.change));
        CHECK_FALSE(math_real_is_nan(s->current.change_p));

        CHECK(stock_was_requested);
        CHECK(dispatcher_unregister_event_listener(listener_id));
    }

    TEST_CASE("Concurrent Requests" * doctest::timeout(60) * doctest::skip(!system_debugger_attached()))
    {
        // Fetch realtime stock data from ##stock500
        stock_handle_t* handles = nullptr;
        for (int i = 0; i < ARRAY_COUNT(stock500); ++i)
        {
            string_const_t code = string_to_const(stock500[i]);
            stock_handle_t handle = stock_request(STRING_ARGS(code), FetchLevel::REALTIME);
            CHECK(!!handle);
            array_push(handles, handle);
        }

        // Wait for all handles in ##handles to resolve
        for (unsigned i = 0; i < array_size(handles); ++i)
        {
            while (!handles[i]->has_resolve(FetchLevel::REALTIME))
            {
                dispatcher_update();
                dispatcher_wait_for_wakeup_main_thread();
            }
        }

        // Check all of them
        for (unsigned i = 0; i < array_size(handles); ++i)
        {
            const stock_t* s = handles[i];
            string_const_t symbol = SYMBOL_CONST(s->code);
            INFO(symbol);
            CHECK_GT(s->current.date, 1);
            CHECK_GT(s->current.adjusted_close, 0);
            CHECK_GE(s->current.volume, 0);
        }

        array_deallocate(handles);
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

    TEST_CASE("EOD" * doctest::timeout(30.0))
    {
        auto prev_history_count = 0;
        {
            stock_handle_t handle = stock_request(STRING_CONST("MSFT.US"), FetchLevel::EOD);

            while (!handle->has_resolve(FetchLevel::EOD))
                dispatcher_wait_for_wakeup_main_thread();

            REQUIRE(handle->has_resolve(FetchLevel::EOD));

            const stock_t* s = handle;
            prev_history_count = array_size(s->history);
            REQUIRE_GT(prev_history_count, 0);
            CHECK_FALSE(math_real_is_nan(s->history[0].price_factor));
            CHECK_GT(s->history[0].open, 0);
            CHECK_GT(s->history[0].adjusted_close, 0);
        }
    }

    TEST_CASE("EOD Split and Adjusted Price" * doctest::timeout(30.0))
    {
        string_const_t code = CTEXT("BBD-B.TO");
        stock_handle_t handle = stock_request(code.str, code.length, FetchLevel::REALTIME | FetchLevel::EOD);

        while (!handle->has_resolve(FetchLevel::EOD))
            dispatcher_wait_for_wakeup_main_thread();

        REQUIRE(handle->has_resolve(FetchLevel::EOD));

        const stock_t* s = handle;
        REQUIRE_GT(array_size(s->history), 0);
        CHECK_FALSE(math_real_is_nan(s->history[0].price_factor));
        CHECK_GT(s->history[0].open, 0);
        CHECK_GT(s->history[0].close, 0);
        CHECK_GT(s->history[0].adjusted_close, 0);

        const time_t once_upon_a_time = string_to_date(STRING_CONST("1981-02-13"));
        const day_result_t* actual_eod = stock_get_EOD(s, once_upon_a_time);
        REQUIRE_NE(actual_eod, nullptr);
        
        const auto split_adjusted = stock_get_split(code.str, code.length, once_upon_a_time);
     
        CHECK_EQ(actual_eod->volume, split_adjusted.volume);
        CHECK_LT(actual_eod->close, actual_eod->adjusted_close);
        CHECK_GT(split_adjusted.close, actual_eod->close);
        CHECK_LE(actual_eod->adjusted_close, split_adjusted.close);

        const auto split_adjusted_price_factor = split_adjusted.close / actual_eod->close;
        CHECK_GE(split_adjusted_price_factor, actual_eod->price_factor);
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
        stock_handle_t handle = stock_request(STRING_CONST("AMZN.US"), FetchLevel::EOD | FetchLevel::TECHNICAL_SMA);

        while (!handle->has_resolve(FetchLevel::TECHNICAL_SMA))
        {
            dispatcher_update();
            dispatcher_wait_for_wakeup_main_thread();
        }

        REQUIRE(handle->has_resolve(FetchLevel::EOD));

        const stock_t* s = handle;
        REQUIRE_GT(array_size(s->history), 0);
        CHECK_FALSE(math_real_is_nan(s->history[0].price_factor));
        CHECK_FALSE(math_real_is_nan(s->history[0].adjusted_close));
        CHECK_FALSE(math_real_is_nan(s->history[0].sma));
    }

    TEST_CASE("WMA" * doctest::timeout(30.0))
    {
        stock_handle_t handle = stock_request(STRING_CONST("SPY.US"), FetchLevel::EOD | FetchLevel::TECHNICAL_WMA);

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
        const char* symbols[] = {
            "CPG.TO", "PEP.US", "INTC.US", "CSCO.US", "NKE.US"
        };

        // Request all of them at once with EOD level first
        stock_handle_t handles[ARRAY_COUNT(symbols)];
        for (int i = 0; i < ARRAY_COUNT(symbols); ++i)
        {
            string_const_t symbol = string_to_const(symbols[i]);
            handles[i] = stock_request(STRING_ARGS(symbol), FetchLevel::EOD);
            CHECK(!!handles[i]);
        }

        // Wait for all of them to resolve
        for (int i = 0; i < ARRAY_COUNT(symbols); ++i)
        {
            // Wait for EOD before requesting SAR and SLOPE
            while (!handles[i]->has_resolve(FetchLevel::EOD))
            {
                dispatcher_update();
                dispatcher_wait_for_wakeup_main_thread();
            }

            // Request SAR and SLOPE
            CHECK_FALSE(stock_update(handles[i], FetchLevel::TECHNICAL_SAR | FetchLevel::TECHNICAL_SLOPE));
            CHECK(handles[i]->is_resolving(FetchLevel::TECHNICAL_SAR));
            CHECK(handles[i]->is_resolving(FetchLevel::TECHNICAL_SLOPE));

            // Wait for SAR and SLOPE
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
        for (int i = 0; i < ARRAY_COUNT(symbols); ++i)
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
        const char* symbols[] = {
            "MMM.US", "ABT.US", "ABBV.US", "RY.TO", "ACN.US"
        };

        // Request all of them at once with EOD level first
        stock_handle_t handles[ARRAY_COUNT(symbols)];
        for (unsigned i = 0; i < ARRAY_COUNT(symbols); ++i)
        {
            string_const_t symbol = string_to_const(symbols[i]);
            handles[i] = stock_request(STRING_ARGS(symbol), FetchLevel::EOD);
            CHECK(!!handles[i]);
        }
        
        // Wait for all of them to resolve
        for (int i = 0; i < ARRAY_COUNT(symbols); ++i)
        {
            while (!handles[i]->has_resolve(FetchLevel::EOD))
            {
                dispatcher_update();
                dispatcher_wait_for_wakeup_main_thread();
            }
        }

        for (int i = 0; i < ARRAY_COUNT(symbols); ++i)
        {
            string_const_t symbol = string_to_const(symbols[i]);
            handles[i] = stock_request(STRING_ARGS(symbol), FetchLevel::TECHNICAL_CCI);
            CHECK(!!handles[i]);
        }

        // Wait for all of them to resolve
        for (int i = 0; i < ARRAY_COUNT(symbols); ++i)
        {
            while (!handles[i]->has_resolve(FetchLevel::TECHNICAL_CCI))
            {
                dispatcher_update();
                dispatcher_wait_for_wakeup_main_thread();
            }
        }
        
        // Check all of them
        for (int i = 0; i < ARRAY_COUNT(symbols); ++i)
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

    TEST_CASE("Request REALTIMEx2" * doctest::timeout(90.0) * doctest::may_fail() * doctest::skip(!system_debugger_attached() || !time_is_working_hours()))
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

    TEST_CASE("EXCHANGE")
    {
        double xgr = stock_exchange_rate(STRING_CONST("USD"), STRING_CONST("CAD"), time_now());
        REQUIRE_GT(xgr, 1.0);

        double xgr2 = stock_exchange_rate(STRING_CONST("USD"), STRING_CONST("CAD"), time_now());
        CHECK_EQ(xgr, xgr2);

        xgr = stock_exchange_rate(STRING_CONST("CAD"), STRING_CONST("CAD"), time_now());
        REQUIRE_EQ(xgr, 1.0);

        xgr = stock_exchange_rate(STRING_CONST("NA"), STRING_CONST("CAD"), time_now());
        REQUIRE_EQ(xgr, 1.0);

        xgr = stock_exchange_rate(STRING_CONST("CAD"), STRING_CONST("USD"), 0);
        REQUIRE_LT(xgr, 1.0);

        xgr = stock_exchange_rate(STRING_CONST("CAD"), STRING_CONST("SOMETHIGN"), 0);
        REQUIRE_EQ(xgr, 1.0);
    }

    TEST_CASE("History")
    {
        string_const_t code = CTEXT("AVGO.US");
        stock_handle_t handle = stock_request(STRING_ARGS(code), FetchLevel::REALTIME);

        const stock_t* s = handle;
        REQUIRE_NE(s, nullptr);

        const time_t at = time_add_days(time_now(), -30);

        // First request should be empty since we haven't request EOD data yet.
        const day_result_t* ed = stock_get_EOD(s, at, false);
        REQUIRE_EQ(ed, nullptr);
        
        CHECK_FALSE(stock_update(STRING_ARGS(code), handle, FetchLevel::EOD, 60.0));

        while (!handle->has_resolve(FetchLevel::EOD))
            dispatcher_wait_for_wakeup_main_thread();

        ed = stock_get_EOD(s, at, false);
        REQUIRE_NE(ed, nullptr);
        CHECK_GT(ed->adjusted_close, 0);
    }

    TEST_CASE("History Take Last")
    {
        stock_handle_t handle{};
        string_const_t code = CTEXT("HNU.TO");

        CHECK_FALSE(stock_update(STRING_ARGS(code), handle, FetchLevel::EOD, 60.0));
        while (!handle->has_resolve(FetchLevel::EOD))
            dispatcher_wait_for_wakeup_main_thread();
            
        const time_t at = time_add_days(time_now(), -10000);
        const day_result_t* ed = stock_get_EOD(handle, at, true);
        
        REQUIRE_NE(ed, nullptr);
        CHECK_EQ(ed->date, 1204693200);
    }

    TEST_CASE("History Too Far")
    {
        stock_handle_t handle{};
        string_const_t code = CTEXT("HUT.TO");

        CHECK_FALSE(stock_update(STRING_ARGS(code), handle, FetchLevel::EOD, 60.0));
        while (!handle->has_resolve(FetchLevel::EOD))
            dispatcher_wait_for_wakeup_main_thread();

        const time_t at = time_add_days(time_now(), -10000);
        const day_result_t* ed = stock_get_EOD(handle, at, false);
        REQUIRE_EQ(ed, nullptr);
    }

    TEST_CASE("History Relative")
    {
        string_const_t code = CTEXT("SHOP.TO");
        stock_handle_t handle = stock_request(STRING_ARGS(code), FetchLevel::EOD);
        while (!handle->has_resolve(FetchLevel::EOD))
            dispatcher_wait_for_wakeup_main_thread();
            
        const day_result_t* ed = stock_get_EOD(handle, -90, false);
        CHECK_GT(ed->date, 0);
        CHECK_LT(ed->date, time_now());
        CHECK_FALSE(math_real_is_nan(ed->open));
    }

    TEST_CASE("AC.TO Fundamental data")
    {
       string_const_t code = CTEXT("AC.TO");
        stock_handle_t handle = stock_request(STRING_ARGS(code), FetchLevel::FUNDAMENTALS);
        while (!handle->has_resolve(FetchLevel::FUNDAMENTALS))
            dispatcher_wait_for_wakeup_main_thread();

        // Test sector and industry
        const stock_t* s = handle;
        REQUIRE_NE(s, nullptr);
        
        CHECK_EQ(SYMBOL_CONST(s->sector), CTEXT("Industrials"));
        CHECK_EQ(SYMBOL_CONST(s->industry), CTEXT("Passenger Airlines"));
    }

    TEST_CASE("stock_resolve")
    {
        // Resolve existing stock
        stock_handle_t stock = stock_resolve(STRING_CONST("AAPL.US"), FetchLevel::REALTIME);
        REQUIRE(stock);
        CHECK_FALSE(math_real_is_nan(stock->current.price));

        // Resolve non-existing stock
        stock = stock_resolve(STRING_CONST("NONEXISTING.STOCK"), FetchLevel::FUNDAMENTALS);
        REQUIRE_FALSE(stock);
    }

    TEST_CASE("Inline primary ticker expression")
    {
        string_const_t primary_ticker_expression_code = CTEXT(R"(
            $t = @1
            $t = IF(ENDS_WITH($t, "NEO"), F($t, "General.Code") + ".US", $t)
            $primary_ticker = F($t, "General.PrimaryTicker")
            IF($primary_ticker, $primary_ticker, $t)
        )");

        // Create library function primary_ticker
        REQUIRE(scripts_register_function(STRING_CONST("primary_ticker"), STRING_ARGS(primary_ticker_expression_code)));

        // Evaluate expression
        string_const_t expression_code = CTEXT(R"(
            MAP(["CSH-UN.TO", "TSLA.NEO", "CTC-A.TO"], primary_ticker($1))
        )");
        expr_result_t result = eval(STRING_ARGS(expression_code));
        CHECK_EQ(result.type, EXPR_RESULT_ARRAY);
        CHECK_EQ(result.element_at(0).as_string(), CTEXT("CSH-UN.TO"));
        CHECK_EQ(result.element_at(1).as_string(), CTEXT("TSLA.US"));
        CHECK_EQ(result.element_at(2).as_string(), CTEXT("CTC.TO"));

        // Resolve existing stock
        REQUIRE(scripts_unregister_function(STRING_CONST("primary_ticker")));
    }
}

#endif // BUILD_TESTS
