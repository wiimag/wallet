/*
 * Copyright 2022-2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */
 
#include "report.h"
  
#include "title.h"
#include "wallet.h"

#include <framework/imgui.h>
#include <framework/common.h>
#include <framework/generics.h>
#include <framework/math.h>
#include <framework/string.h>
#include <framework/array.h>

#include <foundation/random.h>
 
FOUNDATION_STATIC void report_graph_limit(const char* label, double min, double max, double value)
{
    const double range[]{ min, max };
    const double limit[]{ value, value };
    ImPlot::PlotLine(label, range, limit, ARRAY_COUNT(limit), ImPlotLineFlags_NoClip);
}

void report_graph_show_transactions(report_t* report)
{
    if (shortcut_executed(ImGuiKey_F5) || ImGui::IsWindowAppearing())
    {
        array_clear(report->transactions);
        for (const auto& t : generics::fixed_array(report->titles))
        {
            for (auto corder : t->data["orders"])
            {
                string_const_t date = corder["date"].as_string();
                time_t order_date = string_to_date(STRING_ARGS(date));
                report_transaction_t transaction{};
                transaction.date = order_date;
                transaction.title = t;
                transaction.buy = corder["buy"].as_boolean();
                transaction.qty = corder["qty"].as_number(0);
                transaction.price = corder["price"].as_number(0);
                array_push(report->transactions, transaction);
            }
        }

        array_sort(report->transactions, [](const auto& a, const auto& b)
        {
            if (a.date == b.date)
                return (time_t)b.buy - (time_t)a.buy;
            return a.date - b.date;
        });

        double acc = 0;
        report->transaction_total_sells = 0;
        report->transaction_max_acc = 0;
        string_const_t preferred_currency = string_to_const(report->wallet->preferred_currency);
        for (size_t i = 0, end = array_size(report->transactions); i != end; ++i)
        {
            report_transaction_t& tx = report->transactions[i];
            const stock_t* s = tx.title->stock;
            string_const_t stock_currency = string_table_decode_const(s->currency);
            const double exchange_rate = math_ifzero(stock_exchange_rate(STRING_ARGS(stock_currency), STRING_ARGS(preferred_currency), tx.date), 1.0);

            if (tx.buy)
            {
                tx.rated = tx.price * tx.qty * exchange_rate;
                acc += tx.rated;
            }
            else
            {
                tx.rated = tx.price * tx.qty * exchange_rate;

                double cq = 0;
                double cp = 0;
                for (size_t j = 0; j != i; ++j)
                {
                    report_transaction_t& txr = report->transactions[j];
                    if (txr.title != tx.title)
                        continue;
                    if (txr.buy)
                    {
                        cq += txr.qty;
                        cp += txr.rated;
                    }
                    else
                    {
                        cq -= txr.qty;
                        cp -= txr.rated;
                    }
                }

                double avg = math_ifzero(cp / cq, 0);
                tx.adjusted = (tx.rated - (avg * tx.qty)) - (avg * tx.qty);
                acc += tx.adjusted;
                report->transaction_total_sells += tx.adjusted;
            }

            tx.acc = acc;
            report->transaction_max_acc = max(report->transaction_max_acc, acc);

            tx.rx = random32_gaussian_range(-180, 180);
            tx.ry = random32_gaussian_range(-180, acc < 20e3 ? 0 : 180);
        }

        report->transaction_max_acc = max(report->transaction_max_acc, report->total_value);
        report->transaction_max_acc = max(report->transaction_max_acc, wallet_total_funds(report->wallet));
    }

    if (array_size(report->transactions) == 0)
    {
        ImGui::TrTextUnformatted("No transaction to display");
        return ImGui::End();
    }

    const double min_d = (double)report->transactions[0].date;
    const double max_d = (double)array_last(report->transactions)->date;

    ImPlot::SetNextAxesLimits(min_d, max_d, 0, report->transaction_max_acc * 1.15);

    const ImVec2 graph_offset = ImVec2(-ImGui::GetStyle().CellPadding.x, -ImGui::GetStyle().CellPadding.y);
    if (!ImPlot::BeginPlot(tr("Transactions"), graph_offset, ImPlotFlags_NoChild | ImPlotFlags_NoFrame | ImPlotFlags_NoTitle))
        return ImGui::End();

    struct plot_axis_format_t
    {
        bool print_short_value{ true };
        int print_stage{ 0 };
        time_t last_year{ 0 };
        ImPlotRect limits;
    };
    plot_axis_format_t axis_format{};

    ImPlot::SetupLegend(ImPlotLocation_NorthWest, ImPlotLegendFlags_Horizontal | ImPlotLegendFlags_Outside);

    ImPlot::SetupAxis(ImAxis_X1, "##Date", ImPlotAxisFlags_PanStretch | ImPlotAxisFlags_NoHighlight);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, min_d, max_d);
    ImPlot::SetupAxisZoomConstraints(ImAxis_X1, (double)time_one_day() * 7.0, INFINITY);

    ImPlot::SetupAxisFormat(ImAxis_X1, [](double value, char* buff, int size, void* user_data)
    {
        time_t time = (time_t)value;
        plot_axis_format_t& f = *(plot_axis_format_t*)user_data;

        if (f.print_short_value)
        {
            tm tm_year = *localtime(&time);
            if (tm_year.tm_mon == 0 && tm_year.tm_mday < 5)
                return (int)string_copy(buff, size, "", 0).length;

            if (tm_year.tm_mon == 11 && tm_year.tm_mday > 26)
                return (int)string_copy(buff, size, "", 0).length;

            f.print_stage++;
            string_const_t value_str = string_from_date(time);
            return (int)string_format(buff, size, STRING_CONST("%.*s"), 5, value_str.str + 5).length;
        }

        string_const_t value_str = string_from_date(time);
        return (int)string_format(buff, size, STRING_CONST("%.*s"), STRING_FORMAT(value_str)).length;
    }, &axis_format);

    ImPlot::SetupAxis(ImAxis_Y1, "##Investments", ImPlotAxisFlags_NoHighlight | ImPlotAxisFlags_Opposite);
    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, 0, report->transaction_max_acc * 1.20);

    ImPlot::SetupAxisFormat(ImAxis_Y1, [](double value, char* buff, int size, void* user_data)
    {
        plot_axis_format_t& f = *(plot_axis_format_t*)user_data;
        if (f.print_short_value)
        {
            double abs_value = math_abs(value);
            if (abs_value >= 1e12)
                return (int)string_format(buff, size, STRING_CONST("$ %.3gT"), value / 1e12).length;
            if (abs_value >= 1e9)
                return (int)string_format(buff, size, STRING_CONST("$ %.3gB"), value / 1e9).length;
            else if (abs_value >= 1e6)
                return (int)string_format(buff, size, STRING_CONST("$ %.3gM"), value / 1e6).length;
            else if (abs_value >= 1e3)
                return (int)string_format(buff, size, STRING_CONST("$ %.3gK"), value / 1e3).length;

            return (int)string_format(buff, size, STRING_CONST("$ %.0lf"), value).length;
        }

        string_const_t value_str = string_from_currency(value, "9 999 999 $");
        return (int)string_format(buff, size, STRING_CONST("%.*s"), STRING_FORMAT(value_str)).length;
    }, &axis_format);

    ImPlot::SetupFinish();

    const ImPlotRect& limits = ImPlot::GetPlotLimits();
    axis_format.limits = limits;
    axis_format.print_short_value = false;

    struct plot_context_t
    {
        report_transaction_t* transactions{ nullptr };
        title_t* active_title{ nullptr };
        bool shown{ false };
    };

    report_graph_limit(tr("Value"), min_d, max_d, report->total_value);
    report_graph_limit(tr("Funds"), min_d, max_d, wallet_total_funds(report->wallet));

    if (array_size(report->wallet->history) > 0)
        report_graph_limit(tr("Broker"), min_d, max_d, array_last(report->wallet->history)->broker_value);

    plot_context_t c{ report->transactions };

    for (const auto& t : generics::fixed_array(report->titles))
    {
        if (title_is_index(t))
            continue;

        c.active_title = t;
        c.shown = false;

        for (const auto& x : generics::fixed_array(report->transactions))
        {
            const bool in_limits = limits.X.Contains((float)x.date) && limits.Y.Contains((float)x.acc);
            if (x.buy && x.title == t && in_limits)
            {
                const float cc = (float)t->code[0] + (float)t->code[t->code_length >= 1 ? 1 : 0] + (float)t->code[t->code_length >= 2 ? 2 : 0];
                ImPlot::Annotation((double)x.date, x.acc, ImColor::HSV(cc / 360.0f, 0.5f, 0.5f, 0.7f), ImVec2((float)x.rx, (float)x.ry), true, "%s", t->code);
                break;
            }
        }

        for (const auto& x : generics::fixed_array(report->transactions))
        {
            const bool in_limits = limits.X.Contains((float)x.date) && limits.Y.Contains((float)x.acc);
            if (x.buy == false && x.title == t && in_limits)
            {
                const float cc = (float)t->code[0] + (float)t->code[t->code_length >= 1 ? 1 : 0] + (float)t->code[t->code_length >= 2 ? 2 : 0];
                ImPlot::Annotation((double)x.date, x.acc, ImColor::HSV(350 / 360.0f, cc, cc, 0.7f), ImVec2((float)x.rx, (float)x.ry), true, "%s", t->code);
                break;
            }
        }
    }

    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 2.0f);
    ImPlot::PlotLineG(tr("Investments"), [](int idx, void* user_data)->ImPlotPoint
    {
        const plot_context_t* c = (plot_context_t*)user_data;
        const report_transaction_t& t = c->transactions[idx];
        const double x = (double)t.date;
        const double y = t.acc;
        return ImPlotPoint(x, y);
    }, &c, array_size(report->transactions), ImPlotLineFlags_SkipNaN);
    ImPlot::PopStyleVar(1);

    time_t min_time = (time_t)limits.X.Min + time_one_day() * 5;
    double year_range = time_elapsed_days((time_t)min_time, (time_t)max_d) / 365;

    tm tm_year = *localtime(&min_time);
    tm_year.tm_yday = 0;
    tm_year.tm_mday = 1;
    tm_year.tm_mon = 0;
    ImPlot::TagX((double)min_time, ImColor::HSV(155 / 360.0f, 0.75f, 0.5f), "%d", 1900 + tm_year.tm_year);
    for (int i = 0, end = math_round(year_range); i != end; ++i)
    {
        tm_year.tm_year++;
        double y = (double)mktime(&tm_year);
        ImPlot::TagX(y, ImColor::HSV(155 / 360.0f, 0.75f, 0.5f), "%d", 1900 + tm_year.tm_year);
    }

    ImPlot::EndPlot();
}
