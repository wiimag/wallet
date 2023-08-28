/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 */

#include "financials.h"

#include "eod.h"
#include "stock.h"

#include <framework/app.h>
#include <framework/imgui.h>
#include <framework/string.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/module.h>
#include <framework/dispatcher.h>
#include <framework/string_table.h>
#include <framework/profiler.h>
#include <framework/array.h>
#include <framework/window.h>

#define HASH_FINANCIALS static_hash_string("financials", 10, 0x3b2f926a5f4bff66ULL)

template<typename T>
FOUNDATION_FORCEINLINE ImPlotPoint financial_field_plot(int idx, void* user_data, size_t field_offset)
{
    const auto* r = ((T*)user_data) + idx;
    const double x = (double)r->date;
    const double y = *(double*)(((const uint8_t*)r) + field_offset);
    return ImPlotPoint(x, y);
}

//
// Balance Sheet
//

typedef enum class FinancialBalance : uint64_t
{
    None = 0,
    totalAssets = 1 << 0,
    intangibleAssets = 1 << 1,
    earningAssets = 1 << 2,
    otherCurrentAssets = 1 << 3,
    totalLiab = 1 << 4,
    totalStockholderEquity = 1 << 5,
    deferredLongTermLiab = 1 << 6,
    otherCurrentLiab = 1 << 7,
    commonStock = 1 << 8,
    capitalStock = 1 << 9,
    retainedEarnings = 1 << 10,
    otherLiab = 1 << 11,
    goodWill = 1 << 12,
    otherAssets = 1 << 13,
    cash = 1 << 14,
    cashAndEquivalents = 1 << 15,
    totalCurrentLiabilities = 1 << 16,
    currentDeferredRevenue = 1 << 17,
    netDebt = 1 << 18,
    shortTermDebt = 1 << 19,
    shortLongTermDebt = 1 << 20,
    shortLongTermDebtTotal = 1 << 21,
    otherStockholderEquity = 1 << 22,
    propertyPlantEquipment = 1 << 23,
    totalCurrentAssets = 1 << 24,
    longTermInvestments = 1 << 25,
    netTangibleAssets = 1 << 26,
    shortTermInvestments = 1 << 27,
    netReceivables = 1 << 28,
    longTermDebt = 1 << 29,
    inventory = 1 << 30,
    accountsPayable = 1ULL << 31ULL,
    totalPermanentEquity = 1ULL << 32ULL,
    noncontrollingInterestInConsolidatedEntity = 1ULL << 33ULL,
    temporaryEquityRedeemableNoncontrollingInterests = 1ULL << 34ULL,
    accumulatedOtherComprehensiveIncome = 1ULL << 35ULL,
    additionalPaidInCapital = 1ULL << 36ULL,
    commonStockTotalEquity = 1ULL << 37ULL,
    preferredStockTotalEquity = 1ULL << 38ULL,
    retainedEarningsTotalEquity = 1ULL << 39ULL,
    treasuryStock = 1ULL << 40ULL,
    accumulatedAmortization = 1ULL << 41ULL,
    nonCurrrentAssetsOther = 1ULL << 42ULL,
    deferredLongTermAssetCharges = 1ULL << 43ULL,
    nonCurrentAssetsTotal = 1ULL << 44ULL,
    capitalLeaseObligations = 1ULL << 45ULL,
    longTermDebtTotal = 1ULL << 46ULL,
    nonCurrentLiabilitiesOther = 1ULL << 47ULL,
    nonCurrentLiabilitiesTotal = 1ULL << 48ULL,
    negativeGoodwill = 1ULL << 49ULL,
    warrants = 1ULL << 50ULL,
    preferredStockRedeemable = 1ULL << 51ULL,
    capitalSurpluse = 1ULL << 52ULL,
    liabilitiesAndStockholdersEquity = 1ULL << 53ULL,
    cashAndShortTermInvestments = 1ULL << 54ULL,
    propertyPlantAndEquipmentGross = 1ULL << 55ULL,
    propertyPlantAndEquipmentNet = 1ULL << 56ULL,
    accumulatedDepreciation = 1ULL << 57ULL,
    netWorkingCapital = 1ULL << 58ULL,
    netInvestedCapital = 1ULL << 59ULL,
    commonStockSharesOutstanding = 1ULL << 60ULL,
} financial_balance_value_t;

struct financial_balance_sheet_t
{
    time_t date;
    double totalAssets;
    double intangibleAssets;
    double earningAssets;
    double otherCurrentAssets;
    double totalLiab;
    double totalStockholderEquity;
    double deferredLongTermLiab; //" : null,
    double otherCurrentLiab;
    double commonStock;
    double capitalStock;
    double retainedEarnings;
    double otherLiab; //" : null,
    double goodWill;
    double otherAssets;
    double cash;
    double cashAndEquivalents;
    double totalCurrentLiabilities;
    double currentDeferredRevenue; //" : null,
    double netDebt;
    double shortTermDebt;
    double shortLongTermDebt;
    double shortLongTermDebtTotal; //" : null,
    double otherStockholderEquity;
    double propertyPlantEquipment;
    double totalCurrentAssets;
    double longTermInvestments;
    double netTangibleAssets; //" : null,
    double shortTermInvestments;
    double netReceivables;
    double longTermDebt;
    double inventory;
    double accountsPayable;
    double totalPermanentEquity; //" : null,
    double noncontrollingInterestInConsolidatedEntity; //" : null,
    double temporaryEquityRedeemableNoncontrollingInterests; //" : null,
    double accumulatedOtherComprehensiveIncome; //" : null,
    double additionalPaidInCapital; //"" : null,
    double commonStockTotalEquity; //"" : null,
    double preferredStockTotalEquity; //"" : null,
    double retainedEarningsTotalEquity; //"" : null,
    double treasuryStock; //"" : null,
    double accumulatedAmortization; //"" : null,
    double nonCurrrentAssetsOther;
    double deferredLongTermAssetCharges; //"" : null,
    double nonCurrentAssetsTotal;
    double capitalLeaseObligations;
    double longTermDebtTotal;
    double nonCurrentLiabilitiesOther;
    double nonCurrentLiabilitiesTotal;
    double negativeGoodwill; //"" : null,
    double warrants; //"" : null,
    double preferredStockRedeemable; //"" : null,
    double capitalSurpluse;
    double liabilitiesAndStockholdersEquity;
    double cashAndShortTermInvestments;
    double propertyPlantAndEquipmentGross; //"" : null,
    double propertyPlantAndEquipmentNet; //"" : null,
    double accumulatedDepreciation; //"" : null,
    double netWorkingCapital;
    double netInvestedCapital;
    double commonStockSharesOutstanding;
};

#undef FIELD_PLOT
#define FIELD_PLOT(FIELD) \
    offsetof(financial_balance_sheet_t, FIELD),  \
    [](int idx, void* user_data)->ImPlotPoint \
    { \
        return financial_field_plot<financial_balance_sheet_t>(idx, user_data, offsetof(financial_balance_sheet_t, FIELD)); \
    } \
    
struct {
    financial_balance_value_t code;
    string_const_t name;
    size_t field_offset{ 0 };
    ImPlotGetter plot_fn{ nullptr };
    
    bool selected{ false };

} BALANCE_FIELDS[] = {

    { FinancialBalance::totalAssets, CTEXT("Total Assets"), FIELD_PLOT(totalAssets), true },
    { FinancialBalance::cash, CTEXT("Cash"), FIELD_PLOT(cash), false },
    { FinancialBalance::accountsPayable, CTEXT("Accounts Payable"), FIELD_PLOT(accountsPayable) },
    { FinancialBalance::accumulatedAmortization, CTEXT("Accumulated Amortization"), FIELD_PLOT(accumulatedAmortization) },
    { FinancialBalance::accumulatedDepreciation, CTEXT("Accumulated Depreciation"), FIELD_PLOT(accumulatedDepreciation) },
    { FinancialBalance::accumulatedOtherComprehensiveIncome, CTEXT("Accumulated Other Comprehensive Income"), FIELD_PLOT(accumulatedOtherComprehensiveIncome) },
    { FinancialBalance::additionalPaidInCapital, CTEXT("Additional Paid In Capital"), FIELD_PLOT(additionalPaidInCapital) },
    { FinancialBalance::capitalLeaseObligations, CTEXT("Capital Lease Obligations"), FIELD_PLOT(capitalLeaseObligations) },
    { FinancialBalance::capitalStock, CTEXT("Capital Stock"), FIELD_PLOT(capitalStock) },
    { FinancialBalance::capitalSurpluse, CTEXT("Capital Surpluse"), FIELD_PLOT(capitalSurpluse) },
    { FinancialBalance::cashAndEquivalents, CTEXT("Cash and Equivalents"), FIELD_PLOT(cashAndEquivalents) },
    { FinancialBalance::cashAndShortTermInvestments, CTEXT("Cash And Short Term Investments"), FIELD_PLOT(cashAndShortTermInvestments) },
    { FinancialBalance::commonStock, CTEXT("Common Stock"), FIELD_PLOT(commonStock) },
    { FinancialBalance::commonStockSharesOutstanding, CTEXT("Common Stock Shares Outstanding"), FIELD_PLOT(commonStockSharesOutstanding) },
    { FinancialBalance::commonStockTotalEquity, CTEXT("Common Stock Total Equity"), FIELD_PLOT(commonStockTotalEquity) },
    { FinancialBalance::currentDeferredRevenue, CTEXT("Current Deferred Revenue"), FIELD_PLOT(currentDeferredRevenue) },
    { FinancialBalance::deferredLongTermAssetCharges, CTEXT("Deferred Long Term Asset Charges"), FIELD_PLOT(deferredLongTermAssetCharges) },
    { FinancialBalance::deferredLongTermLiab, CTEXT("Deferred Long Term Liabilities"), FIELD_PLOT(deferredLongTermLiab) },
    { FinancialBalance::earningAssets, CTEXT("Earning Assets"), FIELD_PLOT(earningAssets) },
    { FinancialBalance::goodWill, CTEXT("Good Will"), FIELD_PLOT(goodWill) },
    { FinancialBalance::intangibleAssets, CTEXT("Intangible Assets"), FIELD_PLOT(intangibleAssets) },
    { FinancialBalance::inventory, CTEXT("Inventory"), FIELD_PLOT(inventory) },
    { FinancialBalance::liabilitiesAndStockholdersEquity, CTEXT("Liabilities And Stockholders Equity"), FIELD_PLOT(liabilitiesAndStockholdersEquity) },
    { FinancialBalance::longTermDebt, CTEXT("Long Term Debt"), FIELD_PLOT(longTermDebt) },
    { FinancialBalance::longTermDebtTotal, CTEXT("Long Term Debt Total"), FIELD_PLOT(longTermDebtTotal) },
    { FinancialBalance::longTermInvestments, CTEXT("Long Term Investments"), FIELD_PLOT(longTermInvestments) },
    { FinancialBalance::negativeGoodwill, CTEXT("Negative Goodwill"), FIELD_PLOT(negativeGoodwill) },
    { FinancialBalance::netDebt, CTEXT("Net Debt"), FIELD_PLOT(netDebt) },
    { FinancialBalance::netInvestedCapital, CTEXT("Net Invested Capital"), FIELD_PLOT(netInvestedCapital) },
    { FinancialBalance::netReceivables, CTEXT("Net Receivables"), FIELD_PLOT(netReceivables) },
    { FinancialBalance::netTangibleAssets, CTEXT("Net Tangible Assets"), FIELD_PLOT(netTangibleAssets) },
    { FinancialBalance::netWorkingCapital, CTEXT("Net Working Capital"), FIELD_PLOT(netWorkingCapital) },
    { FinancialBalance::noncontrollingInterestInConsolidatedEntity, CTEXT("Noncontrolling Interest In Consolidated Entity"), FIELD_PLOT(noncontrollingInterestInConsolidatedEntity) },
    { FinancialBalance::nonCurrentAssetsTotal, CTEXT("Non Current Assets Total"), FIELD_PLOT(nonCurrentAssetsTotal) },
    { FinancialBalance::nonCurrentLiabilitiesOther, CTEXT("Non Current Liabilities Other"), FIELD_PLOT(nonCurrentLiabilitiesOther) },
    { FinancialBalance::nonCurrentLiabilitiesTotal, CTEXT("Non Current Liabilities Total"), FIELD_PLOT(nonCurrentLiabilitiesTotal) },
    { FinancialBalance::nonCurrrentAssetsOther, CTEXT("Non Current Assets Other"), FIELD_PLOT(nonCurrrentAssetsOther) },
    { FinancialBalance::otherAssets, CTEXT("Other Assets"), FIELD_PLOT(otherAssets) },
    { FinancialBalance::otherCurrentAssets, CTEXT("Other Current Assets"), FIELD_PLOT(otherCurrentAssets) },
    { FinancialBalance::otherCurrentLiab, CTEXT("Other Current Liabilities"), FIELD_PLOT(otherCurrentLiab) },
    { FinancialBalance::otherLiab, CTEXT("Other Liabilities"), FIELD_PLOT(otherLiab) },
    { FinancialBalance::otherStockholderEquity, CTEXT("Other Stockholder Equity"), FIELD_PLOT(otherStockholderEquity) },
    { FinancialBalance::preferredStockRedeemable, CTEXT("Preferred Stock Redeemable"), FIELD_PLOT(preferredStockRedeemable) },
    { FinancialBalance::preferredStockTotalEquity, CTEXT("Preferred Stock Total Equity"), FIELD_PLOT(preferredStockTotalEquity) },
    { FinancialBalance::propertyPlantAndEquipmentGross, CTEXT("Property Plant And Equipment Gross"), FIELD_PLOT(propertyPlantAndEquipmentGross) },
    { FinancialBalance::propertyPlantAndEquipmentNet, CTEXT("Property Plant And Equipment Net"), FIELD_PLOT(propertyPlantAndEquipmentNet) },
    { FinancialBalance::propertyPlantEquipment, CTEXT("Property Plant Equipment"), FIELD_PLOT(propertyPlantEquipment) },
    { FinancialBalance::retainedEarnings, CTEXT("Retained Earnings"), FIELD_PLOT(retainedEarnings) },
    { FinancialBalance::retainedEarningsTotalEquity, CTEXT("Retained Earnings Total Equity"), FIELD_PLOT(retainedEarningsTotalEquity) },
    { FinancialBalance::shortLongTermDebt, CTEXT("Short Long Term Debt"), FIELD_PLOT(shortLongTermDebt) },
    { FinancialBalance::shortLongTermDebtTotal, CTEXT("Short Long Term Debt Total"), FIELD_PLOT(shortLongTermDebtTotal), false },
    { FinancialBalance::shortTermDebt, CTEXT("Short Term Debt"), FIELD_PLOT(shortTermDebt) },
    { FinancialBalance::shortTermInvestments, CTEXT("Short Term Investments"), FIELD_PLOT(shortTermInvestments) },
    { FinancialBalance::temporaryEquityRedeemableNoncontrollingInterests, CTEXT("Temporary Equity Redeemable Noncontrolling Interests"), FIELD_PLOT(temporaryEquityRedeemableNoncontrollingInterests) },
    { FinancialBalance::totalCurrentAssets, CTEXT("Total Current Assets"), FIELD_PLOT(totalCurrentAssets) },
    { FinancialBalance::totalCurrentLiabilities, CTEXT("Total Current Liabilities"), FIELD_PLOT(totalCurrentLiabilities) },
    { FinancialBalance::totalLiab, CTEXT("Total Liabilities"), FIELD_PLOT(totalLiab) },
    { FinancialBalance::totalPermanentEquity, CTEXT("Total Permanent Equity"), FIELD_PLOT(totalPermanentEquity) },
    { FinancialBalance::totalStockholderEquity, CTEXT("Total Stockholder Equity"), FIELD_PLOT(totalStockholderEquity) },
    { FinancialBalance::treasuryStock, CTEXT("Treasury Stock"), FIELD_PLOT(treasuryStock) },
    { FinancialBalance::warrants, CTEXT("Warrants"), FIELD_PLOT(warrants) },
};

//
// Cash Flow
//

typedef enum class FinancialCashFlow : uint64_t
{
    None = 0,

    investments = 1 << 0,
    changeToLiabilities = 1 << 1,
    totalCashflowsFromInvestingActivities = 1 << 2,
    netBorrowings = 1 << 3,
    totalCashFromFinancingActivities = 1 << 4,
    changeToOperatingActivities = 1 << 5,
    netIncome = 1 << 6,
    changeInCash = 1 << 7,
    beginPeriodCashFlow = 1 << 8,
    endPeriodCashFlow = 1 << 9,
    totalCashFromOperatingActivities = 1 << 10,
    issuanceOfCapitalStock = 1 << 11,
    depreciation = 1 << 12,
    otherCashflowsFromInvestingActivities = 1 << 13,
    dividendsPaid = 1 << 14,
    changeToInventory = 1 << 15,
    changeToAccountReceivables = 1 << 16,
    salePurchaseOfStock = 1 << 17,
    otherCashflowsFromFinancingActivities = 1 << 18,
    changeToNetincome = 1 << 19,
    capitalExpenditures = 1 << 20,
    changeReceivables = 1 << 21,
    cashFlowsOtherOperating = 1 << 22,
    exchangeRateChanges = 1 << 23,
    cashAndCashEquivalentsChanges = 1 << 24,
    changeInWorkingCapital = 1 << 25,
    stockBasedCompensation = 1 << 26,
    otherNonCashItems = 1 << 27,
    freeCashFlow = 1 << 28,
    
} financial_cash_flow_value_t;

struct financial_cash_flow_sheet_t
{
    time_t date;

    double investments;
    double changeToLiabilities;
    double totalCashflowsFromInvestingActivities;
    double netBorrowings;
    double totalCashFromFinancingActivities;
    double changeToOperatingActivities;
    double netIncome;
    double changeInCash;
    double beginPeriodCashFlow;
    double endPeriodCashFlow;
    double totalCashFromOperatingActivities;
    double issuanceOfCapitalStock;
    double depreciation;
    double otherCashflowsFromInvestingActivities;
    double dividendsPaid;
    double changeToInventory;
    double changeToAccountReceivables;
    double salePurchaseOfStock;
    double otherCashflowsFromFinancingActivities;
    double changeToNetincome;
    double capitalExpenditures;
    double changeReceivables;
    double cashFlowsOtherOperating;
    double exchangeRateChanges;
    double cashAndCashEquivalentsChanges;
    double changeInWorkingCapital;
    double stockBasedCompensation;
    double otherNonCashItems;
    double freeCashFlow;
    
};

#undef FIELD_PLOT
#define FIELD_PLOT(FIELD) \
    offsetof(financial_cash_flow_sheet_t, FIELD),  \
    [](int idx, void* user_data)->ImPlotPoint \
    { \
        return financial_field_plot<financial_cash_flow_sheet_t>(idx, user_data, offsetof(financial_cash_flow_sheet_t, FIELD)); \
    } \

struct {
    financial_cash_flow_value_t code;
    string_const_t name;
    size_t field_offset{ 0 };
    ImPlotGetter plot_fn{ nullptr };

    bool selected{ false };

} CASH_FLOW_FIELDS[] = {

    { FinancialCashFlow::investments, CTEXT("Investiments"), FIELD_PLOT(investments), false },
    { FinancialCashFlow::beginPeriodCashFlow, CTEXT("Begin Period Cash Flow"), FIELD_PLOT(beginPeriodCashFlow) },
    { FinancialCashFlow::capitalExpenditures, CTEXT("Capital Expenditures"), FIELD_PLOT(capitalExpenditures) },
    { FinancialCashFlow::cashAndCashEquivalentsChanges, CTEXT("Cash And Cash Equivalents Changes"), FIELD_PLOT(cashAndCashEquivalentsChanges) },
    { FinancialCashFlow::cashFlowsOtherOperating, CTEXT("Cash Flows Other Operating"), FIELD_PLOT(cashFlowsOtherOperating) },
    { FinancialCashFlow::changeInCash, CTEXT("Change In Cash"), FIELD_PLOT(changeInCash) },
    { FinancialCashFlow::changeInWorkingCapital, CTEXT("Change In Working Capital"), FIELD_PLOT(changeInWorkingCapital) },
    { FinancialCashFlow::changeReceivables, CTEXT("Change Receivables"), FIELD_PLOT(changeReceivables) },
    { FinancialCashFlow::changeToAccountReceivables, CTEXT("Change To Account Receivables"), FIELD_PLOT(changeToAccountReceivables) },
    { FinancialCashFlow::changeToInventory, CTEXT("Change To Inventory"), FIELD_PLOT(changeToInventory) },
    { FinancialCashFlow::changeToLiabilities, CTEXT("Change To Liabilities"), FIELD_PLOT(changeToLiabilities) },
    { FinancialCashFlow::changeToNetincome, CTEXT("Change To Netincome"), FIELD_PLOT(changeToNetincome) },
    { FinancialCashFlow::changeToOperatingActivities, CTEXT("Change To Operating Activities"), FIELD_PLOT(changeToOperatingActivities) },
    { FinancialCashFlow::depreciation, CTEXT("Depreciation"), FIELD_PLOT(depreciation), true },
    { FinancialCashFlow::dividendsPaid, CTEXT("Dividends Paid"), FIELD_PLOT(dividendsPaid), true },
    { FinancialCashFlow::endPeriodCashFlow, CTEXT("End Period Cash Flow"), FIELD_PLOT(endPeriodCashFlow) },
    { FinancialCashFlow::exchangeRateChanges, CTEXT("Exchange Rate Changes"), FIELD_PLOT(exchangeRateChanges) },
    { FinancialCashFlow::freeCashFlow, CTEXT("Free Cash Flow"), FIELD_PLOT(freeCashFlow), false },
    { FinancialCashFlow::issuanceOfCapitalStock, CTEXT("Issuance Of Capital Stock"), FIELD_PLOT(issuanceOfCapitalStock) },
    { FinancialCashFlow::netBorrowings, CTEXT("Net Borrowings"), FIELD_PLOT(netBorrowings) },
    { FinancialCashFlow::netIncome, CTEXT("Net Income"), FIELD_PLOT(netIncome), true },
    { FinancialCashFlow::otherCashflowsFromFinancingActivities, CTEXT("Other Cashflows From Financing Activities"), FIELD_PLOT(otherCashflowsFromFinancingActivities) },
    { FinancialCashFlow::otherNonCashItems, CTEXT("Other Non Cash Items"), FIELD_PLOT(otherNonCashItems) },
    { FinancialCashFlow::salePurchaseOfStock, CTEXT("Sale Purchase Of Stock"), FIELD_PLOT(salePurchaseOfStock) },
    { FinancialCashFlow::stockBasedCompensation, CTEXT("Stock Based Compensation"), FIELD_PLOT(stockBasedCompensation) },
    { FinancialCashFlow::totalCashflowsFromInvestingActivities, CTEXT("Total Cashflows From Investing Activities"), FIELD_PLOT(totalCashflowsFromInvestingActivities), true },
    { FinancialCashFlow::totalCashFromFinancingActivities, CTEXT("Total Cash From Financing Activities"), FIELD_PLOT(totalCashFromFinancingActivities) },
    { FinancialCashFlow::totalCashFromOperatingActivities, CTEXT("Total Cash From Operating Activities"), FIELD_PLOT(totalCashFromOperatingActivities) },
};

//
// Incomes
//

typedef enum class FinancialIncome : uint64_t
{
    None = 0,

    researchDevelopment = 1 << 0,
    effectOfAccountingCharges = 1 << 1,
    incomeBeforeTax = 1 << 2,
    minorityInterest = 1 << 3,
    netIncome = 1 << 4,
    sellingGeneralAdministrative = 1 << 5,
    sellingAndMarketingExpenses = 1 << 6,
    grossProfit = 1 << 7,
    reconciledDepreciation = 1 << 8,
    ebit = 1 << 9,
    ebitda = 1 << 10,
    depreciationAndAmortization = 1 << 11,
    nonOperatingIncomeNetOther = 1 << 12,
    operatingIncome = 1 << 13,
    otherOperatingExpenses = 1 << 14,
    interestExpense = 1 << 15,
    taxProvision = 1 << 16,
    interestIncome = 1 << 17,
    netInterestIncome = 1 << 18,
    extraordinaryItems = 1 << 19,
    nonRecurring = 1 << 20,
    otherItems = 1 << 21,
    incomeTaxExpense = 1 << 22,
    totalRevenue = 1 << 23,
    totalOperatingExpenses = 1 << 24,
    costOfRevenue = 1 << 25,
    totalOtherIncomeExpenseNet = 1 << 26,
    discontinuedOperations = 1 << 27,
    netIncomeFromContinuingOps = 1 << 28,
    netIncomeApplicableToCommonShares = 1 << 29,
    preferredStockAndOtherAdjustments = 1 << 30,

} financial_income_value_t;

struct financial_income_sheet_t
{
    time_t date;

    double researchDevelopment;
    double effectOfAccountingCharges;
    double incomeBeforeTax;
    double minorityInterest;
    double netIncome;
    double sellingGeneralAdministrative;
    double sellingAndMarketingExpenses;
    double grossProfit;
    double reconciledDepreciation;
    double ebit;
    double ebitda;
    double depreciationAndAmortization;
    double nonOperatingIncomeNetOther;
    double operatingIncome;
    double otherOperatingExpenses;
    double interestExpense;
    double taxProvision;
    double interestIncome;
    double netInterestIncome;
    double extraordinaryItems;
    double nonRecurring;
    double otherItems;
    double incomeTaxExpense;
    double totalRevenue;
    double totalOperatingExpenses;
    double costOfRevenue;
    double totalOtherIncomeExpenseNet;
    double discontinuedOperations;
    double netIncomeFromContinuingOps;
    double netIncomeApplicableToCommonShares;
    double preferredStockAndOtherAdjustments;

};

#undef FIELD_PLOT
#define FIELD_PLOT(FIELD) \
    offsetof(financial_income_sheet_t, FIELD),  \
    [](int idx, void* user_data)->ImPlotPoint \
    { \
        return financial_field_plot<financial_income_sheet_t>(idx, user_data, offsetof(financial_income_sheet_t, FIELD)); \
    } \

struct {
    financial_income_value_t code;
    string_const_t name;
    size_t field_offset{ 0 };
    ImPlotGetter plot_fn{ nullptr };

    bool selected{ false };

} INCOME_FIELDS[] = {

    { FinancialIncome::netIncome, CTEXT("Net Income"), FIELD_PLOT(netIncome), false },
    { FinancialIncome::grossProfit, CTEXT("Gross Profit"), FIELD_PLOT(grossProfit), true },
    { FinancialIncome::totalRevenue, CTEXT("Total Revenue"), FIELD_PLOT(totalRevenue), true },
    { FinancialIncome::totalOperatingExpenses, CTEXT("Total Operating Expenses"), FIELD_PLOT(totalOperatingExpenses) },
    { FinancialIncome::costOfRevenue, CTEXT("Cost Of Revenue"), FIELD_PLOT(costOfRevenue) },
    { FinancialIncome::depreciationAndAmortization, CTEXT("Depreciation And Amortization"), FIELD_PLOT(depreciationAndAmortization) },
    { FinancialIncome::discontinuedOperations, CTEXT("Discontinued Operations"), FIELD_PLOT(discontinuedOperations) },
    { FinancialIncome::ebit, CTEXT("EBIT"), FIELD_PLOT(ebit) },
    { FinancialIncome::ebitda, CTEXT("EBITDA"), FIELD_PLOT(ebitda), true },
    { FinancialIncome::effectOfAccountingCharges, CTEXT("Effect Of Accounting Charges"), FIELD_PLOT(effectOfAccountingCharges) },
    { FinancialIncome::extraordinaryItems, CTEXT("Extraordinary Items"), FIELD_PLOT(extraordinaryItems) },
    { FinancialIncome::incomeBeforeTax, CTEXT("Income Before Tax"), FIELD_PLOT(incomeBeforeTax) },
    { FinancialIncome::incomeTaxExpense, CTEXT("Income Tax Expense"), FIELD_PLOT(incomeTaxExpense) },
    { FinancialIncome::interestExpense, CTEXT("Interest Expense"), FIELD_PLOT(interestExpense) },
    { FinancialIncome::interestIncome, CTEXT("Interest Income"), FIELD_PLOT(interestIncome) },
    { FinancialIncome::minorityInterest, CTEXT("Minority Interest"), FIELD_PLOT(minorityInterest) },
    { FinancialIncome::netIncomeApplicableToCommonShares, CTEXT("Net Income Applicable To Common Shares"), FIELD_PLOT(netIncomeApplicableToCommonShares) },
    { FinancialIncome::netIncomeFromContinuingOps, CTEXT("Net Income From Continuing Ops"), FIELD_PLOT(netIncomeFromContinuingOps) },
    { FinancialIncome::netInterestIncome, CTEXT("Net Interest Income"), FIELD_PLOT(netInterestIncome) },
    { FinancialIncome::nonOperatingIncomeNetOther, CTEXT("Non Operating Income Net Other"), FIELD_PLOT(nonOperatingIncomeNetOther) },
    { FinancialIncome::nonRecurring, CTEXT("Non Recurring"), FIELD_PLOT(nonRecurring) },
    { FinancialIncome::operatingIncome, CTEXT("Operating Income"), FIELD_PLOT(operatingIncome) },
    { FinancialIncome::otherItems, CTEXT("Other Items"), FIELD_PLOT(otherItems) },
    { FinancialIncome::otherOperatingExpenses, CTEXT("Other Operating Expenses"), FIELD_PLOT(otherOperatingExpenses) },
    { FinancialIncome::preferredStockAndOtherAdjustments, CTEXT("Preferred Stock And Other Adjustments"), FIELD_PLOT(preferredStockAndOtherAdjustments) },
    { FinancialIncome::reconciledDepreciation, CTEXT("Reconciled Depreciation"), FIELD_PLOT(reconciledDepreciation) },
    { FinancialIncome::researchDevelopment, CTEXT("R&D"), FIELD_PLOT(researchDevelopment), false },
    { FinancialIncome::sellingAndMarketingExpenses, CTEXT("Selling And Marketing Expenses"), FIELD_PLOT(sellingAndMarketingExpenses) },
    { FinancialIncome::sellingGeneralAdministrative, CTEXT("Selling General Administrative"), FIELD_PLOT(sellingGeneralAdministrative) },
    { FinancialIncome::taxProvision, CTEXT("Tax Provision"), FIELD_PLOT(taxProvision) },
    { FinancialIncome::totalOtherIncomeExpenseNet, CTEXT("Total Other Income Expense Net"), FIELD_PLOT(totalOtherIncomeExpenseNet) },
};

//
// # PRIVATE
//

struct financials_window_t
{
    char title[64]{ 0 };
    char symbol[16]{ 0 };

    bool show_balance_values{ true };
    bool show_cash_flow_values{ false };
    bool show_income_values{ false };

    financial_balance_sheet_t* balances{ nullptr };
    financial_cash_flow_sheet_t* cash_flows{ nullptr };
    financial_income_sheet_t* incomes{ nullptr };

    time_t min_date{ 0 }, max_date{ 0 };

    bool auto_fit{ true };
    bool rendered_once{ false };
};

FOUNDATION_STATIC financial_balance_sheet_t* financials_fetch_balance_sheets(const json_object_t& json)
{    
    const auto Financials = json["Financials"]["Balance_Sheet"]["quarterly"];
    if (!Financials.is_valid())
        return nullptr;

    financial_balance_sheet_t* sheets = nullptr;
    for (auto e : Financials)
    {
        financial_balance_sheet_t sheet{};

        string_const_t date_string = e["date"].as_string();
        if (!string_try_convert_date(STRING_ARGS(date_string), sheet.date))
            continue;

        sheet.totalAssets = e["totalAssets"].as_number();
        sheet.intangibleAssets = e["intangibleAssets"].as_number();
        sheet.otherCurrentAssets = e["otherCurrentAssets"].as_number();
        sheet.totalLiab = e["totalLiab"].as_number();
        sheet.totalStockholderEquity = e["totalStockholderEquity"].as_number();
        sheet.otherCurrentLiab = e["otherCurrentLiab"].as_number();
        sheet.commonStock = e["commonStock"].as_number();
        sheet.capitalStock = e["capitalStock"].as_number();
        sheet.retainedEarnings = e["retainedEarnings"].as_number();
        sheet.otherLiab = e["otherLiab"].as_number();
        sheet.cash = e["cash"].as_number();
        sheet.cashAndEquivalents = e["cashAndEquivalents"].as_number();
        sheet.totalCurrentLiabilities = e["totalCurrentLiabilities"].as_number();
        sheet.netDebt = e["netDebt"].as_number();
        sheet.shortTermDebt = e["shortTermDebt"].as_number();
        sheet.shortLongTermDebt = e["shortLongTermDebt"].as_number();
        sheet.otherStockholderEquity = e["otherStockholderEquity"].as_number();
        sheet.propertyPlantEquipment = e["propertyPlantEquipment"].as_number();
        sheet.totalCurrentAssets = e["totalCurrentAssets"].as_number();
        sheet.netTangibleAssets = e["netTangibleAssets"].as_number();
        sheet.inventory = e["inventory"].as_number();
        sheet.accountsPayable = e["accountsPayable"].as_number();
        sheet.netReceivables = e["netReceivables"].as_number();
        sheet.nonCurrrentAssetsOther = e["nonCurrrentAssetsOther"].as_number();
        sheet.capitalLeaseObligations = e["capitalLeaseObligations"].as_number();
        sheet.longTermDebtTotal = e["longTermDebtTotal"].as_number();
        sheet.nonCurrentLiabilitiesTotal = e["nonCurrentLiabilitiesTotal"].as_number();
        sheet.nonCurrentAssetsTotal = e["nonCurrentAssetsTotal"].as_number();
        sheet.capitalSurpluse = e["capitalSurpluse"].as_number();
        sheet.liabilitiesAndStockholdersEquity = e["liabilitiesAndStockholdersEquity"].as_number();
        sheet.cashAndShortTermInvestments = e["cashAndShortTermInvestments"].as_number();
        sheet.netWorkingCapital = e["netWorkingCapital"].as_number();
        sheet.netInvestedCapital = e["netInvestedCapital"].as_number();
        sheet.commonStockSharesOutstanding = e["commonStockSharesOutstanding"].as_number();
        sheet.shortTermInvestments = e["shortTermInvestments"].as_number();
        sheet.shortLongTermDebtTotal = e["shortLongTermDebtTotal"].as_number();
        sheet.accumulatedOtherComprehensiveIncome = e["accumulatedOtherComprehensiveIncome"].as_number();
        sheet.commonStockTotalEquity = e["commonStockTotalEquity"].as_number();
        sheet.propertyPlantAndEquipmentGross = e["propertyPlantAndEquipmentGross"].as_number();
        sheet.nonCurrentLiabilitiesOther = e["nonCurrentLiabilitiesOther"].as_number();
        sheet.goodWill = e["goodWill"].as_number();
        sheet.longTermInvestments = e["longTermInvestments"].as_number();
        sheet.deferredLongTermLiab = e["deferredLongTermLiab"].as_number();
        sheet.propertyPlantAndEquipmentNet = e["propertyPlantAndEquipmentNet"].as_number();
        sheet.currentDeferredRevenue = e["currentDeferredRevenue"].as_number();
        sheet.earningAssets = e["earningAssets"].as_number();
        sheet.totalPermanentEquity = e["totalPermanentEquity"].as_number();
        sheet.noncontrollingInterestInConsolidatedEntity = e["noncontrollingInterestInConsolidatedEntity"].as_number();
        sheet.temporaryEquityRedeemableNoncontrollingInterests = e["temporaryEquityRedeemableNoncontrollingInterests"].as_number();
        sheet.additionalPaidInCapital = e["additionalPaidInCapital"].as_number();
        sheet.preferredStockTotalEquity = e["preferredStockTotalEquity"].as_number();
        sheet.retainedEarningsTotalEquity = e["retainedEarningsTotalEquity"].as_number();
        sheet.treasuryStock = e["treasuryStock"].as_number();
        sheet.deferredLongTermAssetCharges = e["deferredLongTermAssetCharges"].as_number();
        sheet.negativeGoodwill = e["negativeGoodwill"].as_number();
        sheet.warrants = e["warrants"].as_number();
        sheet.preferredStockRedeemable = e["preferredStockRedeemable"].as_number();
        sheet.accumulatedDepreciation = e["accumulatedDepreciation"].as_number();

        array_push_memcpy(sheets, &sheet);
    }

    // Sort by date
    array_sort(sheets, ARRAY_COMPARE_EXPRESSION(a.date - b.date));
    return sheets;
}

FOUNDATION_STATIC financial_cash_flow_sheet_t* financials_fetch_cash_flows(const json_object_t& json)
{
    const auto CashFlow = json["Financials"]["Cash_Flow"]["quarterly"];
    if (!CashFlow.is_valid())
        return nullptr;

    financial_cash_flow_sheet_t* sheets = nullptr;
    for (auto e : CashFlow)
    {
        financial_cash_flow_sheet_t sheet{};

        string_const_t date_string = e["date"].as_string();
        if (!string_try_convert_date(STRING_ARGS(date_string), sheet.date))
            continue;
            
        sheet.investments = e["investments"].as_number();
        sheet.changeToLiabilities = e["changeToLiabilities"].as_number();
        sheet.totalCashflowsFromInvestingActivities = e["totalCashflowsFromInvestingActivities"].as_number();
        sheet.netBorrowings = e["netBorrowings"].as_number();
        sheet.totalCashFromFinancingActivities = e["totalCashFromFinancingActivities"].as_number();
        sheet.changeToOperatingActivities = e["changeToOperatingActivities"].as_number();
        sheet.netIncome = e["netIncome"].as_number();
        sheet.changeInCash = e["changeInCash"].as_number();
        sheet.beginPeriodCashFlow = e["beginPeriodCashFlow"].as_number();
        sheet.endPeriodCashFlow = e["endPeriodCashFlow"].as_number();
        sheet.totalCashFromOperatingActivities = e["totalCashFromOperatingActivities"].as_number();
        sheet.issuanceOfCapitalStock = e["issuanceOfCapitalStock"].as_number();
        sheet.depreciation = e["depreciation"].as_number();
        sheet.otherCashflowsFromInvestingActivities = e["otherCashflowsFromInvestingActivities"].as_number();
        sheet.dividendsPaid = e["dividendsPaid"].as_number();
        sheet.changeToInventory = e["changeToInventory"].as_number();
        sheet.changeToAccountReceivables = e["changeToAccountReceivables"].as_number();
        sheet.salePurchaseOfStock = e["salePurchaseOfStock"].as_number();
        sheet.otherCashflowsFromFinancingActivities = e["otherCashflowsFromFinancingActivities"].as_number();
        sheet.changeToNetincome = e["changeToNetincome"].as_number();
        sheet.capitalExpenditures = e["capitalExpenditures"].as_number();
        sheet.changeReceivables = e["changeReceivables"].as_number();
        sheet.cashFlowsOtherOperating = e["cashFlowsOtherOperating"].as_number();
        sheet.exchangeRateChanges = e["exchangeRateChanges"].as_number();
        sheet.cashAndCashEquivalentsChanges = e["cashAndCashEquivalentsChanges"].as_number();
        sheet.changeInWorkingCapital = e["changeInWorkingCapital"].as_number();
        sheet.stockBasedCompensation = e["stockBasedCompensation"].as_number();
        sheet.otherNonCashItems = e["otherNonCashItems"].as_number();
        sheet.freeCashFlow = e["freeCashFlow"].as_number();
        
        array_push_memcpy(sheets, &sheet);
    }
    
    // Sort by date
    array_sort(sheets, ARRAY_COMPARE_EXPRESSION(a.date - b.date));
    return sheets;
}

FOUNDATION_STATIC financial_income_sheet_t* financials_fetch_incomes(const json_object_t& json)
{
    const auto CashFlow = json["Financials"]["Income_Statement"]["quarterly"];
    if (!CashFlow.is_valid())
        return nullptr;

    financial_income_sheet_t* sheets = nullptr;
    for (auto e : CashFlow)
    {
        financial_income_sheet_t sheet{};

        string_const_t date_string = e["date"].as_string();
        if (!string_try_convert_date(STRING_ARGS(date_string), sheet.date))
            continue;

        sheet.researchDevelopment = e["researchDevelopment"].as_number();
        sheet.effectOfAccountingCharges = e["effectOfAccountingCharges"].as_number();
        sheet.incomeBeforeTax = e["incomeBeforeTax"].as_number();
        sheet.minorityInterest = e["minorityInterest"].as_number();
        sheet.netIncome = e["netIncome"].as_number();
        sheet.sellingGeneralAdministrative = e["sellingGeneralAdministrative"].as_number();
        sheet.sellingAndMarketingExpenses = e["sellingAndMarketingExpenses"].as_number();
        sheet.grossProfit = e["grossProfit"].as_number();
        sheet.reconciledDepreciation = e["reconciledDepreciation"].as_number();
        sheet.ebit = e["ebit"].as_number();
        sheet.ebitda = e["ebitda"].as_number();
        sheet.depreciationAndAmortization = e["depreciationAndAmortization"].as_number();
        sheet.nonOperatingIncomeNetOther = e["nonOperatingIncomeNetOther"].as_number();
        sheet.operatingIncome = e["operatingIncome"].as_number();
        sheet.otherOperatingExpenses = e["otherOperatingExpenses"].as_number();
        sheet.interestExpense = e["interestExpense"].as_number();
        sheet.taxProvision = e["taxProvision"].as_number();
        sheet.interestIncome = e["interestIncome"].as_number();
        sheet.netInterestIncome = e["netInterestIncome"].as_number();
        sheet.extraordinaryItems = e["extraordinaryItems"].as_number();
        sheet.nonRecurring = e["nonRecurring"].as_number();
        sheet.otherItems = e["otherItems"].as_number();
        sheet.incomeTaxExpense = e["incomeTaxExpense"].as_number();
        sheet.totalRevenue = e["totalRevenue"].as_number();
        sheet.totalOperatingExpenses = e["totalOperatingExpenses"].as_number();
        sheet.costOfRevenue = e["costOfRevenue"].as_number();
        sheet.totalOtherIncomeExpenseNet = e["totalOtherIncomeExpenseNet"].as_number();
        sheet.discontinuedOperations = e["discontinuedOperations"].as_number();
        sheet.netIncomeFromContinuingOps = e["netIncomeFromContinuingOps"].as_number();
        sheet.netIncomeApplicableToCommonShares = e["netIncomeApplicableToCommonShares"].as_number();
        sheet.preferredStockAndOtherAdjustments = e["preferredStockAndOtherAdjustments"].as_number();

        array_push_memcpy(sheets, &sheet);
    }

    // Sort by date
    array_sort(sheets, ARRAY_COMPARE_EXPRESSION(a.date - b.date));
    return sheets;
}


FOUNDATION_STATIC void financials_fetch_data(financials_window_t* window, const json_object_t& json)
{
    FOUNDATION_ASSERT(window);

    MEMORY_TRACKER(HASH_FINANCIALS);

    window->balances = financials_fetch_balance_sheets(json);
    window->cash_flows = financials_fetch_cash_flows(json);
    window->incomes = financials_fetch_incomes(json);
    
    // Compute sheets min and max dates
    const unsigned int num_sheets = array_size(window->balances);
    if (num_sheets > 0)
    {
        window->min_date = window->balances[0].date;
        window->max_date = window->balances[num_sheets - 1].date;
        window->auto_fit = true;
    }
}

FOUNDATION_STATIC financials_window_t* financials_window_allocate(const char* symbol, size_t symbol_length)
{
    financials_window_t* window = MEM_NEW(HASH_FINANCIALS, financials_window_t);

    string_copy(STRING_BUFFER(window->symbol), symbol, symbol_length);

    string_const_t stock_name = stock_get_name(symbol, symbol_length);
    tr_format(STRING_BUFFER(window->title), "{0} - Financials", stock_name);
    
    if (!eod_fetch_async("fundamentals", symbol, FORMAT_JSON_CACHE, L1(financials_fetch_data(window, _1))))
    {
        log_warnf(HASH_FINANCIALS, WARNING_RESOURCE, 
            STRING_CONST("Failed to fetch %*.s financials data"), (int)symbol_length, symbol);
    }

    return window;
}

FOUNDATION_STATIC void financials_window_deallocate(window_handle_t win)
{
    auto* window = (financials_window_t*)window_get_user_data(win);
    FOUNDATION_ASSERT(window);

    array_deallocate(window->balances);
    array_deallocate(window->cash_flows);
    array_deallocate(window->incomes);

    MEM_DELETE(window);
}

template<typename T>
FOUNDATION_STATIC bool financials_sheet_has_data_for_field(T* sheets, size_t field_offset)
{
    // For each sheet
    for (size_t i = 0, end = array_size(sheets); i != end; ++i)
    {
        T* sheet = sheets + i;
        const double value = *(double*)(((const uint8_t*)sheet) + field_offset);
        if (math_real_is_finite(value))
            return true;
    }

    return false;
}

template<typename T>
FOUNDATION_STATIC bool financials_field_selected(const T& c)
{
    char keyname_buffer[64];
    string_t keyname = string_format(keyname_buffer, sizeof(keyname_buffer), STRING_CONST("financial##%.*s"), STRING_FORMAT(c.name));
    return session_get_bool(keyname.str, c.selected);
}

template<typename T>
FOUNDATION_STATIC void financials_field_set_selected(T& c, bool selected)
{
    char keyname_buffer[64];
    string_t keyname = string_format(keyname_buffer, sizeof(keyname_buffer), STRING_CONST("financial##%.*s"), STRING_FORMAT(c.name));
    c.selected = selected;
    session_set_bool(keyname.str, c.selected);
}

template<typename S, typename T, size_t N>
FOUNDATION_STATIC bool financials_render_sheet_selector(const char* label, const S* sheets, T (&indicators)[N])
{
    bool updated = false;

    char preview_buffer[64]{ "None" };
    string_t preview{ preview_buffer, 0 };

    for (size_t i = 0, added = 0; i < N; ++i)
    {
        const auto& c = indicators[i];
        if (!financials_field_selected(c))
            continue;

        string_const_t nametr = tr(STRING_ARGS(c.name), false);
        if (added == 0)
        {
            preview = string_copy(STRING_BUFFER(preview_buffer), STRING_ARGS(nametr));
        }
        else
        {
            preview = string_concat(STRING_BUFFER(preview_buffer), STRING_ARGS(preview), STRING_CONST(", "));
            preview = string_concat(STRING_BUFFER(preview_buffer), STRING_ARGS(preview), STRING_ARGS(nametr));
        }

        added++;

        if (preview.length >= ARRAY_COUNT(preview_buffer) - 1)
            break;
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    ImGui::SameLine();
    if (ImGui::BeginCombo(string_format_static_const("##%s", label), preview.str, ImGuiComboFlags_None))
    {
        bool focused = false;
        for (int i = 0; i != N; ++i)
        {
            auto& c = indicators[i];
            if (c.plot_fn == nullptr)
                continue;

            // Check if we have any data for that indicator
            if (!financials_sheet_has_data_for_field(sheets, c.field_offset))
                continue;

            bool selected = financials_field_selected(c);
            string_const_t ex_id = tr(STRING_ARGS(c.name), false);
            if (ImGui::Checkbox(ex_id.str, &selected))
            {
                updated = true;
                financials_field_set_selected(c, selected);
            }

            if (!focused && selected)
            {
                ImGui::SetItemDefaultFocus();
                focused = true;
            }
        }
        ImGui::EndCombo();
    }

    return updated;
}

FOUNDATION_STATIC void financials_window_render(window_handle_t win)
{
    auto* window = (financials_window_t*)window_get_user_data(win);
    FOUNDATION_ASSERT(window);

    if (array_empty(window->balances))
        return ImGui::TrTextWrapped("No financial sheets to display");

    if (ImGui::Checkbox("##BalanceCheck", &window->show_balance_values))
        window->auto_fit = true;
    ImGui::SameLine();
    if (financials_render_sheet_selector(tr("Balance"), window->balances, BALANCE_FIELDS))
        window->auto_fit = true;

    if (ImGui::Checkbox("##IncomeCheck", &window->show_income_values))
        window->auto_fit = true;
    ImGui::SameLine();
    if (financials_render_sheet_selector(tr("Incomes"), window->incomes, INCOME_FIELDS))
        window->auto_fit = true;

    if (ImGui::Checkbox("##CashFlowCheck", &window->show_cash_flow_values))
        window->auto_fit = true;
    ImGui::SameLine();
    if (financials_render_sheet_selector(tr("Cash Flow"), window->cash_flows, CASH_FLOW_FIELDS))
        window->auto_fit = true;

    if (window->auto_fit && window->rendered_once)
    {
        ImPlot::SetNextAxesToFit();
        window->auto_fit = false;
    }

    
    if (!ImPlot::BeginPlot(window->title, ImVec2(-1, -1), ImPlotFlags_NoChild | ImPlotFlags_NoFrame))
        return;

    ImPlot::SetupAxis(ImAxis_X1, "##Date", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, (double)window->min_date, (double)window->max_date);
    ImPlot::SetupAxisFormat(ImAxis_X1, [](double value, char* buff, int size, void* user_data)
    {
        if (size > 0)
            buff[0] = 0;

        time_t d = (time_t)value;
        if (d == 0 || d == -1)
            return 0;

        string_const_t date_str = string_from_date(d);
        if (date_str.length == 0)
            return 0;

        return (int)string_copy(buff, size, STRING_ARGS(date_str)).length;
    }, nullptr);
    
    if (window->show_balance_values)
    {
        ImPlot::SetupAxis(ImAxis_Y1, "##Currency", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Opposite);
        ImPlot::SetupAxisFormat(ImAxis_Y1, [](double value, char* buff, int size, void* user_data)
        {
            double abs_value = math_abs(value);
            if (abs_value >= 1e12)
                return (int)string_format(buff, size, STRING_CONST("%.2gT $"), value / 1e12).length;
            if (abs_value >= 1e9)
                return (int)string_format(buff, size, STRING_CONST("%.3gB $"), value / 1e9).length;
            else if (abs_value >= 1e6)
                return (int)string_format(buff, size, STRING_CONST("%.3gM $"), value / 1e6).length;
            else if (abs_value >= 1e3)
                return (int)string_format(buff, size, STRING_CONST("%.3gK $"), value / 1e3).length;

            return (int)string_format(buff, size, STRING_CONST("%.2lf $"), value).length;
        }, nullptr);
        ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0.0, INFINITY);
    }
    else
    {
        ImPlot::SetupAxis(ImAxis_Y1, nullptr, ImPlotAxisFlags_NoDecorations);
        ImPlot::SetupAxisFormat(ImAxis_Y1, "-");
    }

    if (window->show_cash_flow_values || window->show_income_values)
    {
        ImPlot::SetupAxis(ImAxis_Y2, "##CashFlow", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
        ImPlot::SetupAxisFormat(ImAxis_Y2, [](double value, char* buff, int size, void* user_data)
        {
            double abs_value = math_abs(value);
            if (abs_value >= 1e12)
                return (int)string_format(buff, size, STRING_CONST("%.2gT $"), value / 1e12).length;
            if (abs_value >= 1e9)
                return (int)string_format(buff, size, STRING_CONST("%.3gB $"), value / 1e9).length;
            else if (abs_value >= 1e6)
                return (int)string_format(buff, size, STRING_CONST("%.3gM $"), value / 1e6).length;
            else if (abs_value >= 1e3)
                return (int)string_format(buff, size, STRING_CONST("%.3gK $"), value / 1e3).length;

            return (int)string_format(buff, size, STRING_CONST("%.2lf $"), value).length;
        }, nullptr);
    }
    else
    {
        ImPlot::SetupAxis(ImAxis_Y2, nullptr, ImPlotAxisFlags_NoDecorations);
        ImPlot::SetupAxisFormat(ImAxis_Y2, "-");
    }

    if (window->show_cash_flow_values)
    {
        ImPlot::SetAxis(ImAxis_Y2);
        const double bar_size = time_one_day() * 45.0;
        for (int i = 0; i < ARRAY_COUNT(CASH_FLOW_FIELDS); ++i)
        {
            const auto& c = CASH_FLOW_FIELDS[i];
            if (!financials_field_selected(c) || c.plot_fn == nullptr)
                continue;

            const auto record_count = array_size(window->cash_flows);
            ImPlot::PlotBarsG(c.name.str, c.plot_fn, (void*)window->cash_flows, to_int(record_count), bar_size, ImPlotBarsFlags_None);
        }
    }

    if (window->show_income_values)
    {
        ImPlot::SetAxis(ImAxis_Y2);
        for (int i = 0; i < ARRAY_COUNT(INCOME_FIELDS); ++i)
        {
            const auto& c = INCOME_FIELDS[i];
            if (!financials_field_selected(c) || c.plot_fn == nullptr)
                continue;

            const auto record_count = array_size(window->incomes);
            ImPlot::PlotLineG(c.name.str, c.plot_fn, (void*)window->incomes, to_int(record_count), ImPlotLineFlags_SkipNaN);
        }
    }

    if (window->show_balance_values)
    {
        ImPlot::SetAxis(ImAxis_Y1);
        for (int i = 0; i < ARRAY_COUNT(BALANCE_FIELDS); ++i)
        {
            const auto& c = BALANCE_FIELDS[i];
            if (!financials_field_selected(c) || c.plot_fn == nullptr)
                continue;

            const auto record_count = array_size(window->balances);
            ImPlot::PlotLineG(c.name.str, c.plot_fn, (void*)window->balances, to_int(record_count), ImPlotLineFlags_SkipNaN);
        }
    }
    
    window->rendered_once = true;
    ImPlot::EndPlot();
}

//
// # PUBLIC API
//

void financials_open_window(const char* symbol, size_t symbol_length)
{
    auto* window = financials_window_allocate(symbol, symbol_length);
    window_open("financials", STRING_LENGTH(window->title), 
        financials_window_render, financials_window_deallocate, window);
}

//
// # SYSTEM
//

DEFINE_MODULE(FINANCIALS, [](){}, nullptr, MODULE_PRIORITY_UI);
