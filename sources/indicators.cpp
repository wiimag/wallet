/*
 * Copyright 2023 equals-forty-two.com All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 */

#include "indicators.h"

#include "eod.h"

#include <framework/tabs.h>
#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/session.h>
#include <framework/service.h>
#include <framework/database.h>
#include <framework/query.h>
#include <framework/dispatcher.h>

#define HASH_INDICATORS static_hash_string("indicators", 10, 0x80f942b8e488d9c0ULL)

// List all countries from the ISO 3166-1 alpha 3 list (from https://en.wikipedia.org/wiki/ISO_3166-1_alpha-3)
struct {
    string_const_t code;
    string_const_t name;
    
    hash_t key;
    bool selected{ false };
    
} COUNTRIES[] = {

    { CTEXT("CAN"), CTEXT("Canada") },
    { CTEXT("USA"), CTEXT("United States of America") },
    { CTEXT("GBR"), CTEXT("United Kingdom of Great Britainand Northern Ireland") },
    { CTEXT("JPN"), CTEXT("Japan") },
    { CTEXT("FRA"), CTEXT("France") },
    { CTEXT("DEU"), CTEXT("Germany") },
    { CTEXT("AUS"), CTEXT("Australia") },
    { CTEXT("HKG"), CTEXT("Hong Kong") },
    { CTEXT("CHN"), CTEXT("China") },
    { CTEXT("IND"), CTEXT("India") },
    { CTEXT("CHE"), CTEXT("Switzerland") },
    { CTEXT("KOR"), CTEXT("Korea, Republic of") },
    { CTEXT("TWN"), CTEXT("Taiwan, Province of China") },
    { CTEXT("IRN"), CTEXT("Iran(Islamic Republic of)") },
    { CTEXT("BRA"), CTEXT("Brazil") },

    { CTEXT("ARG"), CTEXT("Argentina") },    
    { CTEXT("IDN"), CTEXT("Indonesia") },
    { CTEXT("ITA"), CTEXT("Italy") },
    { CTEXT("MEX"), CTEXT("Mexico") },
    { CTEXT("RUS"), CTEXT("Russian Federation") },
    { CTEXT("SAU"), CTEXT("Saudi Arabia") },
    { CTEXT("ARE"), CTEXT("United Arab Emirates") },
    { CTEXT("ZAF"), CTEXT("South Africa") },

    { CTEXT("ABW"), CTEXT("Aruba") }, { CTEXT("AFG"), CTEXT("Afghanistan") }, { CTEXT("AGO"), CTEXT("Angola") }, { CTEXT("AIA"), CTEXT("Anguilla") }, { CTEXT("ALA"), CTEXT("Åland Islands") },
    { CTEXT("ALB"), CTEXT("Albania") }, { CTEXT("AND"), CTEXT("Andorra") }, { CTEXT("ARM"), CTEXT("Armenia") },
    { CTEXT("ASM"), CTEXT("American Samoa") }, { CTEXT("ATA"), CTEXT("Antarctica") }, { CTEXT("ATF"), CTEXT("French Southern Territories") }, { CTEXT("ATG"), CTEXT("Antiguaand Barbuda") },
    { CTEXT("AUT"), CTEXT("Austria") }, { CTEXT("AZE"), CTEXT("Azerbaijan") }, { CTEXT("BDI"), CTEXT("Burundi") }, { CTEXT("BEL"), CTEXT("Belgium") },
    { CTEXT("BEN"), CTEXT("Benin") }, { CTEXT("BES"), CTEXT("Bonaire, Sint Eustatiusand Saba") }, { CTEXT("BFA"), CTEXT("Burkina Faso") }, { CTEXT("BGD"), CTEXT("Bangladesh") }, { CTEXT("BGR"), CTEXT("Bulgaria") },
    { CTEXT("BHR"), CTEXT("Bahrain") }, { CTEXT("BHS"), CTEXT("Bahamas") }, { CTEXT("BIH"), CTEXT("Bosniaand Herzegovina") }, { CTEXT("BLM"), CTEXT("Saint Barthélemy") }, { CTEXT("BLR"), CTEXT("Belarus") },
    { CTEXT("BLZ"), CTEXT("Belize") }, { CTEXT("BMU"), CTEXT("Bermuda") }, { CTEXT("BOL"), CTEXT("Bolivia(Plurinational State of)") }, { CTEXT("BRB"), CTEXT("Barbados") },
    { CTEXT("BRN"), CTEXT("Brunei Darussalam") }, { CTEXT("BTN"), CTEXT("Bhutan") }, { CTEXT("BVT"), CTEXT("Bouvet Island") }, { CTEXT("BWA"), CTEXT("Botswana") }, { CTEXT("CAF"), CTEXT("Central African Republic") },
    { CTEXT("CCK"), CTEXT("Cocos(Keeling) Islands") }, { CTEXT("CHL"), CTEXT("Chile") }, 
    { CTEXT("CIV"), CTEXT("Côte d'Ivoire") }, { CTEXT("CMR"), CTEXT("Cameroon") }, { CTEXT("COD"), CTEXT("Congo, Democratic Republic of the") }, { CTEXT("COG"), CTEXT("Congo") }, { CTEXT("COK"), CTEXT("Cook Islands") },
    { CTEXT("COL"), CTEXT("Colombia") }, { CTEXT("COM"), CTEXT("Comoros") }, { CTEXT("CPV"), CTEXT("Cabo Verde") },
    { CTEXT("CRI"), CTEXT("Costa Rica") }, { CTEXT("CUB"), CTEXT("Cuba") }, { CTEXT("CUW"), CTEXT("Curaçao") }, { CTEXT("CXR"), CTEXT("Christmas Island") }, { CTEXT("CYM"), CTEXT("Cayman Islands") },
    { CTEXT("CYP"), CTEXT("Cyprus") }, { CTEXT("CZE"), CTEXT("Czechia") }, { CTEXT("DJI"), CTEXT("Djibouti") }, { CTEXT("DMA"), CTEXT("Dominica") }, { CTEXT("DNK"), CTEXT("Denmark") },
    { CTEXT("DOM"), CTEXT("Dominican Republic") }, { CTEXT("DZA"), CTEXT("Algeria") }, { CTEXT("ECU"), CTEXT("Ecuador") }, { CTEXT("EGY"), CTEXT("Egypt") }, { CTEXT("ERI"), CTEXT("Eritrea") },
    { CTEXT("ESH"), CTEXT("Western Sahara") }, { CTEXT("ESP"), CTEXT("Spain") }, { CTEXT("EST"), CTEXT("Estonia") }, { CTEXT("ETH"), CTEXT("Ethiopia") }, { CTEXT("FIN"), CTEXT("Finland") },
    { CTEXT("FJI"), CTEXT("Fiji") }, { CTEXT("FLK"), CTEXT("Falkland Islands(Malvinas)") }, { CTEXT("FRO"), CTEXT("Faroe Islands") }, { CTEXT("FSM"), CTEXT("Micronesia(Federated States of)") },
    { CTEXT("GAB"), CTEXT("Gabon") }, { CTEXT("GEO"), CTEXT("Georgia") }, { CTEXT("GGY"), CTEXT("Guernsey") }, { CTEXT("GHA"), CTEXT("Ghana") }, { CTEXT("GIB"), CTEXT("Gibraltar") },
    { CTEXT("GIN"), CTEXT("Guinea") }, { CTEXT("GLP"), CTEXT("Guadeloupe") }, { CTEXT("GMB"), CTEXT("Gambia") }, { CTEXT("GNB"), CTEXT("Guinea - Bissau") }, { CTEXT("GNQ"), CTEXT("Equatorial Guinea") },
    { CTEXT("GRC"), CTEXT("Greece") }, { CTEXT("GRD"), CTEXT("Grenada") }, { CTEXT("GRL"), CTEXT("Greenland") }, { CTEXT("GTM"), CTEXT("Guatemala") },
    { CTEXT("GUF"), CTEXT("French Guiana") }, { CTEXT("GUM"), CTEXT("Guam") }, { CTEXT("GUY"), CTEXT("Guyana") }, { CTEXT("HMD"), CTEXT("Heard Island and McDonald Islands") }, { CTEXT("HND"), CTEXT("Honduras") },
    { CTEXT("HRV"), CTEXT("Croatia") }, { CTEXT("HTI"), CTEXT("Haiti") }, { CTEXT("HUN"), CTEXT("Hungary") }, { CTEXT("IMN"), CTEXT("Isle of Man") }, { CTEXT("IOT"), CTEXT("British Indian Ocean Territory") },
    { CTEXT("IRL"), CTEXT("Ireland") }, { CTEXT("IRQ"), CTEXT("Iraq") }, { CTEXT("ISL"), CTEXT("Iceland") }, { CTEXT("ISR"), CTEXT("Israel") }, { CTEXT("JAM"), CTEXT("Jamaica") }, { CTEXT("JEY"), CTEXT("Jersey") },
    { CTEXT("JOR"), CTEXT("Jordan") }, { CTEXT("KAZ"), CTEXT("Kazakhstan") }, { CTEXT("KEN"), CTEXT("Kenya") }, { CTEXT("KGZ"), CTEXT("Kyrgyzstan") }, { CTEXT("KHM"), CTEXT("Cambodia") },
    { CTEXT("KIR"), CTEXT("Kiribati") }, { CTEXT("KNA"), CTEXT("Saint Kitts and Nevis") }, { CTEXT("KWT"), CTEXT("Kuwait") }, { CTEXT("LAO"), CTEXT("Lao People's Democratic Republic") }, { CTEXT("LBN"), CTEXT("Lebanon") },
    { CTEXT("LBR"), CTEXT("Liberia") }, { CTEXT("LBY"), CTEXT("Libya") }, { CTEXT("LCA"), CTEXT("Saint Lucia") }, { CTEXT("LIE"), CTEXT("Liechtenstein") }, { CTEXT("LKA"), CTEXT("Sri Lanka") },
    { CTEXT("LSO"), CTEXT("Lesotho") },{ CTEXT("LTU"), CTEXT("Lithuania") },{ CTEXT("LUX"), CTEXT("Luxembourg") },{ CTEXT("LVA"), CTEXT("Latvia") },{ CTEXT("MAC"), CTEXT("Macao") },
    { CTEXT("MAF"), CTEXT("Saint Martin(French part)") },{ CTEXT("MAR"), CTEXT("Morocco") },{ CTEXT("MCO"), CTEXT("Monaco") },{ CTEXT("MDA"), CTEXT("Moldova, Republic of") },
    { CTEXT("MDG"), CTEXT("Madagascar") },{ CTEXT("MDV"), CTEXT("Maldives") },{ CTEXT("MHL"), CTEXT("Marshall Islands") },{ CTEXT("MKD"), CTEXT("North Macedonia") },    { CTEXT("MLI"), CTEXT("Mali") },
    { CTEXT("MLT"), CTEXT("Malta") }, { CTEXT("MMR"), CTEXT("Myanmar") },    { CTEXT("MNE"), CTEXT("Montenegro") },    { CTEXT("MNG"), CTEXT("Mongolia") },    { CTEXT("MNP"), CTEXT("Northern Mariana Islands") },
    { CTEXT("MOZ"), CTEXT("Mozambique") },    { CTEXT("MRT"), CTEXT("Mauritania") },    { CTEXT("MSR"), CTEXT("Montserrat") },    { CTEXT("MTQ"), CTEXT("Martinique") },    { CTEXT("MUS"), CTEXT("Mauritius") },
    { CTEXT("MWI"), CTEXT("Malawi") },    { CTEXT("MYS"), CTEXT("Malaysia") },    { CTEXT("MYT"), CTEXT("Mayotte") },    { CTEXT("NAM"), CTEXT("Namibia") },    { CTEXT("NCL"), CTEXT("New Caledonia") },
    { CTEXT("NER"), CTEXT("Niger") },    { CTEXT("NFK"), CTEXT("Norfolk Island") },    { CTEXT("NGA"), CTEXT("Nigeria") },    { CTEXT("NIC"), CTEXT("Nicaragua") },    { CTEXT("NIU"), CTEXT("Niue") },
    { CTEXT("NLD"), CTEXT("Netherlands") },    { CTEXT("NOR"), CTEXT("Norway") },    { CTEXT("NPL"), CTEXT("Nepal") },    { CTEXT("NRU"), CTEXT("Nauru") },    { CTEXT("NZL"), CTEXT("New Zealand") },
    { CTEXT("OMN"), CTEXT("Oman") },    { CTEXT("PAK"), CTEXT("Pakistan") },    { CTEXT("PAN"), CTEXT("Panama") },    { CTEXT("PCN"), CTEXT("Pitcairn") },    { CTEXT("PER"), CTEXT("Peru") },    { CTEXT("PHL"), CTEXT("Philippines") },
    { CTEXT("PLW"), CTEXT("Palau") },    { CTEXT("PNG"), CTEXT("Papua New Guinea") },    { CTEXT("POL"), CTEXT("Poland") },    { CTEXT("PRI"), CTEXT("Puerto Rico") },    { CTEXT("PRK"), CTEXT("Korea(Democratic People's Republic of)") },    { CTEXT("PRT"), CTEXT("Portugal") },
    { CTEXT("PRY"), CTEXT("Paraguay") },    { CTEXT("PSE"), CTEXT("Palestine, State of") },    { CTEXT("PYF"), CTEXT("French Polynesia") },    { CTEXT("QAT"), CTEXT("Qatar") },    { CTEXT("REU"), CTEXT("Réunion") },
    { CTEXT("ROU"), CTEXT("Romania") },    { CTEXT("RWA"), CTEXT("Rwanda") },    { CTEXT("SDN"), CTEXT("Sudan") },    { CTEXT("SEN"), CTEXT("Senegal") },    { CTEXT("SGP"), CTEXT("Singapore") },
    { CTEXT("SGS"), CTEXT("South Georgiaand the South Sandwich Islands") },    { CTEXT("SHN"), CTEXT("Saint Helena, Ascensionand Tristan da Cunha") },    { CTEXT("SJM"), CTEXT("Svalbardand Jan Mayen") },    { CTEXT("SLB"), CTEXT("Solomon Islands") },
    { CTEXT("SLE"), CTEXT("Sierra Leone") },    { CTEXT("SLV"), CTEXT("El Salvador") },    { CTEXT("SMR"), CTEXT("San Marino") },    { CTEXT("SOM"), CTEXT("Somalia") },    { CTEXT("SPM"), CTEXT("Saint Pierreand Miquelon") },
    { CTEXT("SRB"), CTEXT("Serbia") },    { CTEXT("SSD"), CTEXT("South Sudan") },    { CTEXT("STP"), CTEXT("Sao Tomeand Principe") },    { CTEXT("SUR"), CTEXT("Suriname") },    { CTEXT("SVK"), CTEXT("Slovakia") },
    { CTEXT("SVN"), CTEXT("Slovenia") },    { CTEXT("SWE"), CTEXT("Sweden") },    { CTEXT("SWZ"), CTEXT("Eswatini") },    { CTEXT("SXM"), CTEXT("Sint Maarten(Dutch part)") },    { CTEXT("SYC"), CTEXT("Seychelles") },
    { CTEXT("SYR"), CTEXT("Syrian Arab Republic") },    { CTEXT("TCA"), CTEXT("Turksand Caicos Islands") },    { CTEXT("TCD"), CTEXT("Chad") },    { CTEXT("TGO"), CTEXT("Togo") },    { CTEXT("THA"), CTEXT("Thailand") },
    { CTEXT("TJK"), CTEXT("Tajikistan") },    { CTEXT("TKL"), CTEXT("Tokelau") },    { CTEXT("TKM"), CTEXT("Turkmenistan") },    { CTEXT("TLS"), CTEXT("Timor - Leste") },    { CTEXT("TON"), CTEXT("Tonga") },
    { CTEXT("TTO"), CTEXT("Trinidad and Tobago") },    { CTEXT("TUN"), CTEXT("Tunisia") },    { CTEXT("TUR"), CTEXT("Türkiye") },    { CTEXT("TUV"), CTEXT("Tuvalu") },    { CTEXT("TZA"), CTEXT("Tanzania, United Republic of") },
    { CTEXT("UGA"), CTEXT("Uganda") },    { CTEXT("UKR"), CTEXT("Ukraine") },    { CTEXT("UMI"), CTEXT("United States Minor Outlying Islands") },    { CTEXT("URY"), CTEXT("Uruguay") },    { CTEXT("UZB"), CTEXT("Uzbekistan") },
    { CTEXT("VAT"), CTEXT("Holy See") },    { CTEXT("VCT"), CTEXT("Saint Vincentand the Grenadines") },    { CTEXT("VEN"), CTEXT("Venezuela(Bolivarian Republic of)") },    { CTEXT("VGB"), CTEXT("Virgin Islands(British)") },
    { CTEXT("VIR"), CTEXT("Virgin Islands(U.S.)") },    { CTEXT("VNM"), CTEXT("Viet Nam") },    { CTEXT("VUT"), CTEXT("Vanuatu") },    { CTEXT("WLF"), CTEXT("Wallisand Futuna") },
    { CTEXT("WSM"), CTEXT("Samoa") },    { CTEXT("YEM"), CTEXT("Yemen") },    { CTEXT("ZMB"), CTEXT("Zambia") },    { CTEXT("ZWE"), CTEXT("Zimbabwe") },    
};

typedef enum MacroIndicatorFormat {
    MIF_UNDEFINED,
    MIF_NUMBER,
    MIF_PERCENTAGE,
    MIF_CURRENCY
} macro_indicator_format_t;

struct {
    string_const_t code;
    string_const_t name;
    string_const_t description;
    macro_indicator_format_t format{ MIF_UNDEFINED };
    
    hash_t key;
    bool selected{ false };
    
} MACRO_INDICATORS[] = {

    { CTEXT("RIR"), CTEXT("real_interest_rate"), CTEXT("Real interest rate (%)"), MIF_PERCENTAGE},
    { CTEXT("PTT"), CTEXT("population_total"), CTEXT("Population, total"), MIF_NUMBER },
    { CTEXT("PGA"), CTEXT("population_growth_annual"), CTEXT("Population growth (annual%)"), MIF_PERCENTAGE },
    { CTEXT("ICP"), CTEXT("inflation_consumer_prices_annual"), CTEXT("Inflation, consumer prices (annual%)"), MIF_PERCENTAGE },
    { CTEXT("CPI"), CTEXT("consumer_price_index"), CTEXT("Consumer Price Index (2010 = 100)"), MIF_NUMBER },
    { CTEXT("GDP"), CTEXT("gdp_current_usd"), CTEXT("GDP (current US$)"), MIF_CURRENCY },
    { CTEXT("GDPC"), CTEXT("gdp_per_capita_usd"), CTEXT("GDP per capita (current US$)"), MIF_CURRENCY },
    { CTEXT("GDPG"), CTEXT("gdp_growth_annual"), CTEXT("GDP growth (annual%)"), MIF_PERCENTAGE },
    { CTEXT("GDPD"), CTEXT("debt_percent_gdp"), CTEXT("Debt in percent of GDP (annual%)"), MIF_PERCENTAGE },
    { CTEXT("NTGS"), CTEXT("net_trades_goods_services"), CTEXT("Net trades in goodsand services (current US$)"), MIF_CURRENCY },
    { CTEXT("IDA"), CTEXT("inflation_gdp_deflator_annual"), CTEXT("Inflation, GDP deflator (annual%)"), MIF_PERCENTAGE },
    { CTEXT("AVA"), CTEXT("agriculture_value_added_percent_gdp"), CTEXT("Agriculture, value added (% of GDP)"), MIF_PERCENTAGE },
    { CTEXT("IVA"), CTEXT("industry_value_added_percent_gdp"), CTEXT("Industry, value added (% of GDP)"), MIF_PERCENTAGE },
    { CTEXT("SVA"), CTEXT("services_value_added_percent_gdp"), CTEXT("Services, etc., value added (% of GDP)"), MIF_PERCENTAGE },
    { CTEXT("EGS"), CTEXT("exports_of_goods_services_percent_gdp"), CTEXT("Exports of goodsand services (% of GDP)"), MIF_PERCENTAGE },
    { CTEXT("IGS"), CTEXT("imports_of_goods_services_percent_gdp"), CTEXT("Imports of goodsand services (% of GDP)"), MIF_PERCENTAGE },
    { CTEXT("GCF"), CTEXT("gross_capital_formation_percent_gdp"), CTEXT("Gross capital formation (% of GDP)"), MIF_PERCENTAGE },
    { CTEXT("NMV"), CTEXT("net_migration"), CTEXT("Net migration (absolute value)"), MIF_NUMBER },
    { CTEXT("GNI"), CTEXT("gni_usd"), CTEXT("GNI, Atlas method (current US$)"), MIF_CURRENCY },
    { CTEXT("GNIC"), CTEXT("gni_per_capita_usd"), CTEXT("GNI per capita, Atlas method (current US$)"), MIF_CURRENCY },
    { CTEXT("GNIP"), CTEXT("gni_ppp_usd"), CTEXT("GNI, PPP (current international $)"), MIF_CURRENCY },
    { CTEXT("GNICP"), CTEXT("gni_per_capita_ppp_usd"), CTEXT("GNI per capita, PPP (current international $)"), MIF_CURRENCY },
    { CTEXT("ISLT"), CTEXT("income_share_lowest_twenty"), CTEXT("Income share held by lowest 20 % (in%)"), MIF_PERCENTAGE },
    { CTEXT("LE"), CTEXT("life_expectancy"), CTEXT("Life expectancy at birth, total (years)"), MIF_NUMBER },
    { CTEXT("FE"), CTEXT("fertility_rate"), CTEXT("Fertility rate, total (births per woman)"), MIF_NUMBER },
    { CTEXT("PHIV"), CTEXT("prevalence_hiv_total"), CTEXT("Prevalence of HIV, total (% of population ages 15 - 49)"), MIF_PERCENTAGE },
    { CTEXT("CO2"), CTEXT("co2_emissions_tons_per_capita"), CTEXT("CO2 emissions (metric tons per capita)"), MIF_NUMBER },
    { CTEXT("SA"), CTEXT("surface_area_km"), CTEXT("Surface area (sq.km)"), MIF_NUMBER },
    { CTEXT("PVL"), CTEXT("poverty_poverty_lines_percent_population"), CTEXT("Poverty headcount ratio at national poverty lines (% of population)"), MIF_PERCENTAGE },
    { CTEXT("REGDP"), CTEXT("revenue_excluding_grants_percent_gdp"), CTEXT("Revenue, excluding grants (% of GDP)"), MIF_PERCENTAGE },
    { CTEXT("CSD"), CTEXT("cash_surplus_deficit_percent_gdp"), CTEXT("Cash surplus / deficit (% of GDP)"), MIF_PERCENTAGE },
    { CTEXT("SPB"), CTEXT("startup_procedures_register"), CTEXT("Start - up procedures to register a business (number)"), MIF_NUMBER },
    { CTEXT("MCDC"), CTEXT("market_cap_domestic_companies_percent_gdp"), CTEXT("Market capitalization of listed domestic companies (% of GDP)"), MIF_PERCENTAGE },
    { CTEXT("MCS"), CTEXT("mobile_subscriptions_per_hundred"), CTEXT("Mobile cellular subscriptions (per 100 people)"), MIF_NUMBER },
    { CTEXT("IU"), CTEXT("internet_users_per_hundred"), CTEXT("Internet users (per 100 people)"), MIF_NUMBER },
    { CTEXT("HTE"), CTEXT("high_technology_exports_percent_total"), CTEXT("High - technology exports (% of manufactured exports)"), MIF_NUMBER },
    { CTEXT("MT"), CTEXT("merchandise_trade_percent_gdp"), CTEXT("Merchandise trade (% of GDP)"), MIF_PERCENTAGE },
    { CTEXT("TDS"), CTEXT("total_debt_service_percent_gni"), CTEXT("Total debt service (% of GNI)"), MIF_PERCENTAGE },
    { CTEXT("UT"), CTEXT("unemployment_total_percent"), CTEXT("Unemployment total (% of labor force)"), MIF_PERCENTAGE },
    
};

struct indicator_record_t {
    time_t date{0};
    double value{ NAN };
};

struct macro_indicator_t {
    string_const_t code{};
    string_const_t country{};
    
    string_t name{};
    string_t period{};
    string_t country_name{};

    macro_indicator_format_t format { MIF_UNDEFINED };

    indicator_record_t* records{ nullptr };
};

FOUNDATION_FORCEINLINE FOUNDATION_CONSTCALL hash_t hash(const macro_indicator_t& ind)
{
    return hash_combine(string_hash(STRING_ARGS(ind.code)), string_hash(STRING_ARGS(ind.country)));
}

static struct INDICATORS_MODULE {

    bool show_tab{ false };
    time_t min_d{ 0 };
    time_t max_d{ INT64_MIN };

    database<macro_indicator_t> macros{};

} *_indicators;

FOUNDATION_STATIC bool indicators_render_indicators_selector()
{
    bool updated = false;

    char preview_buffer[64]{ "None" };
    char preview_single_code[16]{ '\0' };
    string_t preview{ preview_buffer, 0 };

    for (int i = 0, added = 0, end = ARRAY_COUNT(MACRO_INDICATORS); i != end; ++i)
    {
        const auto& c = MACRO_INDICATORS[i];
        if (!c.selected)
            continue;

        if (added == 0)
        {
            string_copy(STRING_CONST_CAPACITY(preview_single_code), STRING_ARGS(c.code));
            preview = string_concat(STRING_CONST_CAPACITY(preview_buffer), STRING_ARGS(preview), STRING_ARGS(c.description));
        }
        else if (added == 1)
        {
            preview = string_copy(STRING_CONST_CAPACITY(preview_buffer), preview_single_code, string_length(preview_single_code));
        }
        
        if (added > 0)
        {
            preview = string_concat(STRING_CONST_CAPACITY(preview_buffer), STRING_ARGS(preview), STRING_CONST(", "));
            preview = string_concat(STRING_CONST_CAPACITY(preview_buffer), STRING_ARGS(preview), STRING_ARGS(c.code));
        }
        
        added++;
        
        if (preview.length >= ARRAY_COUNT(preview_buffer)-1)
            break;
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.8f);
    if (ImGui::BeginCombo("##MacroIndicator", preview.str, ImGuiComboFlags_None))
    {
        bool focused = false;
        for (int i = 0, end = ARRAY_COUNT(MACRO_INDICATORS); i != end; ++i)
        {
            auto& c = MACRO_INDICATORS[i];

            string_const_t ex_id = string_format_static(STRING_CONST("%.*s (%.*s)"), STRING_FORMAT(c.description), STRING_FORMAT(c.code));
            if (ImGui::Checkbox(ex_id.str, &c.selected))
                updated = true;

            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%.*s", STRING_FORMAT(c.name));

            if (!focused && c.selected)
            {
                ImGui::SetItemDefaultFocus();
                focused = true;
            }
        }
        ImGui::EndCombo();
    }

    return updated;
}

FOUNDATION_STATIC bool indicators_render_country_selector()
{
    bool updated = false;

    char preview_buffer[64]{ '\0' };
    string_t preview{ preview_buffer, 0 };
    
    for (int i = 0, added = 0, end = ARRAY_COUNT(COUNTRIES); i != end; ++i)
    {
        const auto& c = COUNTRIES[i];
        
        if (!c.selected)
            continue;
        if (added > 0)
            preview = string_concat(STRING_CONST_CAPACITY(preview_buffer), STRING_ARGS(preview), STRING_CONST(", "));
        preview = string_concat(STRING_CONST_CAPACITY(preview_buffer), STRING_ARGS(preview), STRING_ARGS(c.code));
        added++;
    }
    
    ImGui::SetNextItemWidth(400.0f);
    if (ImGui::BeginCombo("##Country", preview.str, ImGuiComboFlags_None))
    {
        bool focused = false;
        for (int i = 0, end = ARRAY_COUNT(COUNTRIES); i != end; ++i)
        {
            auto& c = COUNTRIES[i];
            
            string_const_t ex_id = string_format_static(STRING_CONST("%.*s (%.*s)"), STRING_FORMAT(c.code), STRING_FORMAT(c.name));
            if (ImGui::Checkbox(ex_id.str, &c.selected))
                updated = true;

            if (!focused && c.selected)
            {
                ImGui::SetItemDefaultFocus();
                focused = true;
            }
        }
        ImGui::EndCombo();
    }

    return updated;
}

FOUNDATION_STATIC void indicators_render_plot_macro_graph(const macro_indicator_t& macro)
{
    const auto record_count = array_size(macro.records);
    if (array_size(macro.records) == 0)
        return;

    if (macro.format == MIF_NUMBER || macro.format == MIF_UNDEFINED)
        ImPlot::SetAxis(ImAxis_Y1);
    else if (macro.format == MIF_PERCENTAGE)
        ImPlot::SetAxis(ImAxis_Y2);
    else if (macro.format == MIF_CURRENCY)
        ImPlot::SetAxis(ImAxis_Y3);
        
    const char* macro_plot_id = string_format_static_const("%.*s (%.*s)", STRING_FORMAT(macro.name), STRING_FORMAT(macro.country));
    ImPlot::PlotLineG(macro_plot_id, [](int idx, void* user_data)->ImPlotPoint
    {
        macro_indicator_t* c = (macro_indicator_t*)user_data;
        const indicator_record_t* r = &c->records[idx];

        const double x = (double)r->date;
        const double y = r->value;
        return ImPlotPoint(x, y);
    }, (void*)&macro, to_int(record_count), ImPlotLineFlags_SkipNaN);
}

FOUNDATION_STATIC macro_indicator_t indicators_query_macro_indicator(string_const_t country, string_const_t macro_code)
{
    macro_indicator_t macro{};
    macro.code = macro_code;
    macro.country = country;

    // Find indicator format
    for (unsigned i = 0; i < ARRAY_COUNT(MACRO_INDICATORS); ++i)
    {
        auto& c = MACRO_INDICATORS[i];
        c.key = string_hash(STRING_ARGS(c.name));
        if (string_equal(STRING_ARGS(macro_code), STRING_ARGS(c.name)))
        {
            macro.format = c.format;
            break;
        }
    }
    
    string_const_t url = eod_build_url("macro-indicator", country.str, FORMAT_JSON_CACHE, "indicator", macro_code.str);
    query_execute_json(url.str, FORMAT_JSON_CACHE, [&macro](const json_object_t& json) 
    {
        for (auto e : json)
        {
            string_const_t date_str = e["Date"].as_string();
            const time_t date = string_to_date(STRING_ARGS(date_str));
            const double value = e["Value"].as_number();

            if (date == 0 || math_real_is_zero(value))
                continue;

            if (date < _indicators->min_d || _indicators->min_d == 0)
                _indicators->min_d = date;
            if (date > _indicators->max_d)
                _indicators->max_d = date;
            
            if (macro.name.length == 0)
            {
                string_const_t name = e["Indicator"].as_string();
                macro.name = string_clone(STRING_ARGS(name));
            }

            if (macro.country_name.length == 0)
            {
                string_const_t country_name = e["CountryName"].as_string();
                macro.country_name = string_clone(STRING_ARGS(country_name));
            }

            if (macro.period.length == 0)
            {
                string_const_t period = e["Period"].as_string();
                macro.period = string_clone(STRING_ARGS(period));
            }

            indicator_record_t r;
            r.date = date;
            r.value = e["Value"].as_number();
            array_push_memcpy(macro.records, &r);
        }
    }, 90 * 24 * 3600ULL);

    return macro;
}

FOUNDATION_STATIC int indicators_format_date_monthly(double value, char* buff, int size, void* user_data)
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
}

FOUNDATION_STATIC int indicators_format_currency(double value, char* buff, int size, void* user_data)
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
}

FOUNDATION_STATIC int indicators_format_large_number(double value, char* buff, int size, void* user_data)
{
    double abs_value = math_abs(value);
    if (abs_value >= 1e12)
        return (int)string_format(buff, size, STRING_CONST("%.2gT"), value / 1e12).length;
    if (abs_value >= 1e9)
        return (int)string_format(buff, size, STRING_CONST("%.3gB"), value / 1e9).length;
    else if (abs_value >= 1e6)
        return (int)string_format(buff, size, STRING_CONST("%.3gM"), value / 1e6).length;
    else if (abs_value >= 1e3)
        return (int)string_format(buff, size, STRING_CONST("%.3gK"), value / 1e3).length;

    return (int)string_format(buff, size, STRING_CONST("%.2lf"), value).length;
}

FOUNDATION_STATIC void indicators_render_graphs()
{
    if (!ImPlot::BeginPlot("MacroIndicators", ImVec2(-1, -1), ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle))
        return;

    auto& db = _indicators->macros;

    //ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal);

    ImPlot::SetupAxis(ImAxis_X1, "##Date", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, (double)_indicators->min_d, (double)_indicators->max_d);
    ImPlot::SetupAxisFormat(ImAxis_X1, indicators_format_date_monthly, nullptr);
    
    ImPlot::SetupAxis(ImAxis_Y1, "##Absolute", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Opposite);
    ImPlot::SetupAxisFormat(ImAxis_Y1, indicators_format_large_number, nullptr);
    
    ImPlot::SetupAxis(ImAxis_Y2, "##Percentage", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_PanStretch| ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y2, 0.0, INFINITY);
    ImPlot::SetupAxisFormat(ImAxis_Y2, "%.3g %%");

    ImPlot::SetupAxis(ImAxis_Y3, "##Currency", ImPlotAxisFlags_RangeFit | ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Opposite);
    ImPlot::SetupAxisFormat(ImAxis_Y3, indicators_format_currency, nullptr);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y3, 0.0, INFINITY);

    // Plot indicators for each selected countries and macro indicators.
    for (int i = 0; i < ARRAY_COUNT(COUNTRIES); ++i)
    {
        const auto& c = COUNTRIES[i];
        if (!c.selected)
            continue;

        for (int j = 0, end = ARRAY_COUNT(MACRO_INDICATORS); j != end; ++j)
        {
            const auto& m = MACRO_INDICATORS[j];
            if (!m.selected)
                continue;

           const hash_t mkey = hash_combine(m.key, c.key);
           if (!db.select(mkey, indicators_render_plot_macro_graph))
           {
               const hash_t added_hash = db.put(indicators_query_macro_indicator(c.code, m.name));
               log_infof(HASH_INDICATORS, STRING_CONST("[%" PRIhash "] Added macro indicator `% .*s` for country `%.*s`"), 
                   added_hash, STRING_FORMAT(m.name), STRING_FORMAT(c.code));

                dispatch(L0(ImPlot::SetNextAxesToFit()));
           }
        }
    }


    ImPlot::EndPlot();
}

FOUNDATION_STATIC void indicators_render_toolbar()
{
    ImGui::BeginGroup();
    {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Country");
        ImGui::SameLine();
        indicators_render_country_selector();
    }

    {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Indicators");
        ImGui::SameLine();
        indicators_render_indicators_selector();
    }
    ImGui::EndGroup();
}

FOUNDATION_STATIC void indicators_tab_render()
{
    indicators_render_toolbar();
    indicators_render_graphs();
}

FOUNDATION_STATIC void indicators_render_tabs()
{
    if (!_indicators->show_tab)
        return;
        
    tab_set_color(TAB_COLOR_OTHER);
    tab_draw(ICON_MD_BATCH_PREDICTION " Indicators", &_indicators->show_tab, indicators_tab_render);
}

FOUNDATION_STATIC void indicators_render_menus()
{
    if (!ImGui::BeginMenuBar())
        return;

    if (ImGui::BeginMenu("Modules"))
    {
        ImGui::MenuItem(ICON_MD_BATCH_PREDICTION " Indicators", NULL, &_indicators->show_tab);
        ImGui::EndMenu();
    }

    ImGui::EndMenuBar();
}

FOUNDATION_STATIC void indicators_load_settings()
{
    _indicators->show_tab = session_get_bool("indicators_show_tab", _indicators->show_tab);
    
    {
        string_const_t selected_countries = session_get_string("indicators_country_codes", "CAN");
        string_t* codes = string_split(selected_countries, CTEXT(";"));
        for (unsigned i = 0; i < ARRAY_COUNT(COUNTRIES); ++i)
        {
            auto& c = COUNTRIES[i];
            c.key = string_hash(STRING_ARGS(c.code));
            if (array_contains(codes, c.code, LC2(string_equal(_1, _2))))
                c.selected = true;
        }
        string_array_deallocate(codes);
    }

    {
        string_const_t selected_macro_indicators = session_get_string("indicators_macro_indicators", "gdp_current_usd");
        string_t* codes = string_split(selected_macro_indicators, CTEXT(";"));
        for (unsigned i = 0; i < ARRAY_COUNT(MACRO_INDICATORS); ++i)
        {
            auto& c = MACRO_INDICATORS[i];
            c.key = string_hash(STRING_ARGS(c.name));
            if (array_contains(codes, c.code, LC2(string_equal(_1, _2))))
                c.selected = true;
        }
        string_array_deallocate(codes);
    }
}

FOUNDATION_STATIC void indicators_save_settings()
{
    session_set_bool("indicators_show_tab", _indicators->show_tab);
    
    const range_view view(COUNTRIES, ARRAY_COUNT(COUNTRIES));
    string_const_t selected_countries = string_join(view.begin(), view.end(), LC1(_1.selected ? _1.code : string_null()), CTEXT(";"));
    session_set_string("indicators_country_codes", STRING_ARGS(selected_countries));

    const range_view misv(MACRO_INDICATORS, ARRAY_COUNT(MACRO_INDICATORS));
    string_const_t selected_macro_indicators = string_join(misv.begin(), misv.end(), LC1(_1.selected ? _1.code : string_null()), CTEXT(";"));
    session_set_string("indicators_macro_indicators", STRING_ARGS(selected_macro_indicators));
}

// 
// # SYSTEM
//

FOUNDATION_STATIC void indicators_initialize()
{
    _indicators = MEM_NEW(HASH_INDICATORS, INDICATORS_MODULE);
    _indicators->max_d = time_now();
    indicators_load_settings();

    service_register_tabs(HASH_INDICATORS, indicators_render_tabs);
    service_register_menu(HASH_INDICATORS, indicators_render_menus);
}

FOUNDATION_STATIC void indicators_shutdown()
{
    indicators_save_settings();

    for (auto& m : _indicators->macros)
    {
        string_deallocate(m.name.str);
        string_deallocate(m.period.str);
        string_deallocate(m.country_name.str);
        array_deallocate(m.records);
    }

    MEM_DELETE(_indicators);
}

DEFINE_SERVICE(INDICATORS, indicators_initialize, indicators_shutdown, SERVICE_PRIORITY_LOW);
