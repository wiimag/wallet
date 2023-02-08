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

// 500 stock symbols ending with .US or .TO
static const char* stock500[] = {
    "MMM.US", "ABT.US", "ABBV.US", "U.US", "ACN.US", "ATVI.US", "ADBE.US", "AMD.US", "AAP.US", "AES.US",
    "AFL.US", "A.US", "APD.US", "AKAM.US", "ALK.US", "ALB.US", "ARE.US", "ALGN.US", "ALLE.US",
    "LNT.US", "ALL.US", "GOOGL.US", "GOOG.US", "MO.US", "AMZN.US", "AMCR.US", "AEE.US", "AAL.US", "AEP.US",
    "AXP.US", "AIG.US", "AMT.US", "AWK.US", "AMP.US", "ABC.US", "AME.US", "AMGN.US", "APH.US", "ADI.US",
    "ANSS.US", /*"ANTM.US",*/ "AON.US", "AOS.US", "APA.US", "AIV.US", "AAPL.US", "AMAT.US", "APTV.US", "ADM.US",
    "ARNC.US", "ANET.US", "AJG.US", "AIZ.US", "ATO.US", "T.US", "ADSK.US", "ADP.US", "AZO.US", "AVB.US",
    "AVY.US", "BKR.US", /*"BLL.US",*/ "BAC.US", "BK.US", "BAX.US", "BDX.US", "BRK-B.US", "BBY.US", "BIIB.US",
    "BLK.US", "BA.US", "BKNG.US", "BWA.US", "BXP.US", "BSX.US", "BMY.US", "AVGO.US", "BR.US", "BF-B.US",
    "CHRW.US", /*"COG.US",*/ "CDNS.US", "CPB.US", "COF.US", "CAH.US", "KMX.US", "CCL.US", "CARR.US", "CTLT.US",
    "CAT.US", "CBOE.US", "CBRE.US", "CDW.US", "CE.US", "CNC.US", "CNP.US", /*"CERN.US",*/ "CF.US", "SCHW.US",
    "CHTR.US", "CVX.US", "CMG.US", "CB.US", "CHD.US", "CI.US", "CINF.US", "CTAS.US", "CSCO.US", "C.US",
    "CFG.US", /*"CTXS.US",*/ "CLX.US", "CME.US", "CMS.US", "KO.US", "CTSH.US", "CL.US", "CMCSA.US", "CMA.US",
    "CAG.US", /*"CXO.US",*/ "COP.US", "ED.US", "STZ.US", "COO.US", "CPRT.US", "GLW.US", "CTVA.US", "COST.US",
    "COTY.US", "CCI.US", "CSX.US", "CMI.US", "CVS.US", "DHI.US", "DHR.US", "DRI.US", "DVA.US", "DE.US",
    "DAL.US", "XRAY.US", "DVN.US", "DXCM.US", "FANG.US", "DLR.US", "DFS.US", /*"DISCA.US", "DISCK.US",*/ "DISH.US",
    "DG.US", "DLTR.US", "D.US", "DOV.US", "DOW.US", "DTE.US", "DUK.US", "DD.US", "DXC.US",
    "EMN.US", "ETN.US", "EBAY.US", "ECL.US", "EIX.US", "EW.US", "EA.US", "EMR.US", "ETR.US",
    "EOG.US", "EFX.US", "EQIX.US", "EQR.US", "ESS.US", "EL.US", "EVRG.US", "ES.US", "RE.US", "EXC.US",
    "EXPE.US", "EXPD.US", "EXR.US", "XOM.US", "FFIV.US", "META.US", "FAST.US", "FRT.US", "FDX.US", "FIS.US",
    "FITB.US", "FE.US", "FRC.US", "FISV.US", "FLT.US", "FLS.US", "FMC.US", "F.US", "FTNT.US",
    "FTV.US", "FBHS.US", "FOXA.US", "FOX.US", "BEN.US", "FCX.US", "GPS.US", "GRMN.US", "IT.US", "GD.US",
    "GE.US", "GIS.US", "GM.US", "GPC.US", "GILD.US", "GL.US", "GPN.US", "GS.US", "GWW.US", "HAL.US",
    "HBI.US", "HIG.US", "HAS.US", "HCA.US", "PEAK.US", "HSIC.US", "HSY.US", "HES.US", "HPE.US", "HLT.US",
    "HOLX.US", "HD.US", "HON.US", "HRL.US", "HST.US", "HWM.US", "HPQ.US", "HUM.US", "HBAN.US",
    "HII.US", "IDXX.US", "ITW.US", "ILMN.US", "INCY.US", "IR.US", "INTC.US", "ICE.US", "IBM.US",
    "IP.US", "IPG.US", "IFF.US", "INTU.US", "ISRG.US", "IVZ.US", "IPGP.US", "IQV.US", "IRM.US", "JKHY.US",
    "J.US", "JBHT.US", "SJM.US", "JNJ.US", "JCI.US", "JPM.US", "JNPR.US", "K.US", "KEY.US",
    "KEYS.US", "KMB.US", "KIM.US", "KMI.US", "KLAC.US", "KSS.US", "KHC.US", "KR.US", "LHX.US",
    "LH.US", "LRCX.US", "LW.US", "LVS.US", "LEG.US", "LDOS.US", "LEN.US", "LLY.US", "LNC.US", "LIN.US",
    "LYV.US", "LKQ.US", "LMT.US", "L.US", "LOW.US", "LYB.US", "MTB.US", "MRO.US", "MPC.US", "MKTX.US",
    "MAR.US", "MMC.US", "MLM.US", "MAS.US", "MA.US", "MKC.US", "MCD.US", "MCK.US", "MDT.US",
    "MRK.US", "MET.US", "MTD.US", "MGM.US", "MCHP.US", "MU.US", "MSFT.US", "MAA.US", "MHK.US", "TAP.US",
    "MDLZ.US", "MNST.US", "MCO.US", "MS.US", "MOS.US", "MSI.US", "MSCI.US", "NDAQ.US", "NOV.US",
    "NKTR.US", "NTAP.US", "NFLX.US", "NWL.US", "NEM.US", "NWSA.US", "NWS.US", "NEE.US", "NKE.US",
    "NI.US", "JWN.US", "NSC.US", "NTRS.US", "NOC.US", "NCLH.US", "NRG.US", "NUE.US",
    "NVDA.US", "NVR.US", "ORLY.US", "OXY.US", "ODFL.US", "OMC.US", "OKE.US", "ORCL.US", "PCAR.US", "PKG.US",
    "PH.US", "PAYX.US", "PAYC.US", "PYPL.US", "PNR.US", "PEP.US", "PKI.US", "PRGO.US", "PFE.US",
    "PM.US", "PSX.US", "PNW.US", "PXD.US", "PNC.US", "POOL.US", "PPG.US", "PPL.US", "PFG.US", "PG.US",
    "PGR.US", "PLD.US", "PRU.US", "PTC.US", "PEG.US", "PSA.US", "PHM.US", "PVH.US", "QRVO.US", "PWR.US",
    "QCOM.US", "DGX.US", "RL.US", "RJF.US", "RTX.US", "O.US", "REG.US", "REGN.US", "RF.US", "RSG.US",
    "RMD.US", "RHI.US", "ROK.US", "ROL.US", "ROP.US", "ROST.US", "RCL.US", "SPGI.US", "CRM.US", "SBAC.US",
    "SLB.US", "STX.US", "SEE.US", "SRE.US", "NOW.US", "SHW.US", "SPG.US", "SWKS.US", "SLG.US", "SNA.US",
    "SO.US", "LUV.US", "SWK.US", "SBUX.US", "STT.US", "STE.US", "SYK.US", "SIVB.US", "SYF.US", "SNPS.US",
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

    TEST_CASE("Concurrent Requests" * doctest::timeout(60))
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
            CHECK_GT(s->current.close, 0);
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
        const char* symbols[] = {
            "CMCSA.US", "NFLX.US", "BAC.US", "T.US", "PEP.US", "CRM.US", "INTC.US", "CSCO.US", "KO.US", "NKE.US"
        };

        // Request all of them at once with EOD level first
        stock_handle_t handles[ARRAY_COUNT(symbols)];
        for (int i = 0; i < ARRAY_COUNT(symbols); ++i)
        {
            string_const_t symbol = string_to_const(symbols[i]);
            handles[i] = stock_request(STRING_ARGS(symbol), FetchLevel::TECHNICAL_EOD | FetchLevel::TECHNICAL_SAR | FetchLevel::TECHNICAL_SLOPE);
            CHECK(!!handles[i]);
        }

        // Wait for all of them to resolve
        for (int i = 0; i < ARRAY_COUNT(symbols); ++i)
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
            "MMM.US", "ABT.US", "ABBV.US", "U.US", "ACN.US"
        };

        // Request all of them at once with EOD level first
        stock_handle_t handles[ARRAY_COUNT(symbols)];
        for (unsigned i = 0; i < ARRAY_COUNT(symbols); ++i)
        {
            string_const_t symbol = string_to_const(symbols[i]);
            handles[i] = stock_request(STRING_ARGS(symbol), FetchLevel::TECHNICAL_EOD);
            CHECK(!!handles[i]);
        }
        
        // Wait for all of them to resolve
        for (int i = 0; i < ARRAY_COUNT(symbols); ++i)
        {
            while (!handles[i]->has_resolve(FetchLevel::TECHNICAL_EOD))
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
