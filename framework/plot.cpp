/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 */

#include "plot.h"

//
// PRIVATE
//

FOUNDATION_STATIC void plot_render_graph_trend(const char* label, double x1, double x2, double a, double b, const plot_context_t& context)
{
    // Y = a + bX
    const double x_diff = x2 - x1;
    const double range[]{ x1, x2 };
    const double trend[]{ a + b * x1, a + b * x2};
    double y_diff = trend[1] - trend[0];
    if (math_real_is_nan(trend[0]) || math_real_is_nan(trend[1]))
        return;

    ImColor pc = ImPlot::GetLastItemColor();
    ImPlot::SetNextLineStyle(pc);

    if (context.x_axis_inverted)
    {
        b *= -1.0;
        a += y_diff;
        y_diff *= -1.0;
    }

    const char* tag = string_format_static_const("%s %s", label, b > 0 ? ICON_MD_TRENDING_UP : ICON_MD_TRENDING_DOWN);
    ImPlot::TagY(a + b * (context.flipped || context.x_axis_inverted ? x2 : x1), pc, "%s", tag);
    ImPlot::PlotLine(tag, range, trend, ARRAY_COUNT(trend), ImPlotLineFlags_NoClip);

    if (context.show_equation)
    {
        ImPlot::Annotation(context.x_axis_inverted ? x1 : x2, context.x_axis_inverted ? trend[0] : trend[1], ImVec4(0.3f, 0.3f, 0.5f, 1.0f),
            ImVec2(0, 10.0f * (b > 0 ? -1.0f : 1.0f)), true,
            "%s = %.2g %s %.1gx (" ICON_MD_CHANGE_HISTORY  "%.2g)", label, a, b < 0 ? "-" : "+", math_abs(b), y_diff);
    }
}

//
// PUBLIC API
//

void plot_compute_trend(plot_context_t& c)
{
    // Trend
    // Y = a + bX
    //        c      d e         f         d  
    // b = (Σ(xy) - (ΣxΣy)/n) / (Σ(x^2) - (Σx)^2/n)
    //      e          d
    // a = (Σy)/n - b((Σx)/n)

    c.b = (c.c - (c.d * c.e) / c.n) / (c.f - math_pow(c.d, 2) / c.n);
    c.a = (c.e / c.n) - c.b * (c.d / c.n);
}

bool plot_build_trend(plot_context_t& c, double x, double y)
{
    if (math_real_is_nan(x) || math_real_is_nan(y))
        return false;
    c.n++;
    c.x_min = min(c.x_min, x);
    c.x_max = max(x, c.x_max);
    c.c += x * y;
    c.d += x;
    c.e += y;
    c.f += math_pow(x, 2);
    return true;
}

void plot_render_trend(const char* label, const plot_context_t& c)
{
    if (c.n <= 0)
        return;
    ImPlot::PushStyleVar(ImPlotStyleVar_LineWeight, 1.5f);
    plot_render_graph_trend(label, c.x_min, c.x_max, c.a, c.b, c);
    ImPlot::PopStyleVar(1);
}

int plot_value_format_date(double value, char* buff, int size, void* user_data)
{
    time_t d = (time_t)value;
    if (d == 0 || d == -1)
        return 0;

    string_const_t date_str = string_from_date(d);
    return (int)string_copy(buff, size, STRING_ARGS(date_str)).length;
}

int plot_value_format_year(double value, char* buff, int size, void* user_data)
{
    time_t time = (time_t)math_trunc(value);
    string_const_t date_str = string_from_date(time);
    return (int)string_format(buff, size, STRING_CONST("%.*s"), min((int)date_str.length, 4), date_str.str).length;
}

int plot_value_format_elapsed_time_short(double value, char* buff, int size, void* user_data)
{
    if (math_real_is_nan(value))
        return 0;

    if (value <= 0)
        return (int)string_copy(buff, size, STRING_CONST("MAX")).length;

    if (value >= 365)
    {
        value = math_round(value / 365);
        return (int)tr_format(buff, to_size(size), "{0,round}Y", value).length;
    }
    else if (value >= 30)
    {
        value = math_round(value / 30);
        return (int)tr_format(buff, to_size(size), "{0,round}M", value).length;
    }
    else if (value >= 7)
    {
        value = math_round(value / 7);
        return (int)tr_format(buff, to_size(size), "{0,round}W", value).length;
    }

    value = math_round(value);
    return (int)tr_format(buff, to_size(size), "{0,round}D", value).length;
}

void plot_render_limit(const char* label, double min, double max, double value)
{
    const double range[]{ min, max };
    const double limit[]{ value, value };
    ImPlot::PlotLine(label, range, limit, ARRAY_COUNT(limit), ImPlotLineFlags_NoClip);
}

int plot_value_format_currency_short(double value, char* buff, int size, void* user_data)
{
    double abs_value = math_abs(value);
    if (abs_value >= 1e12)
        return (int)string_format(buff, size, STRING_CONST("%.2gT $"), value / 1e12).length;
    if (abs_value >= 1e9)
        return (int)string_format(buff, size, STRING_CONST("%.2gB $"), value / 1e9).length;
    else if (abs_value >= 1e6)
        return (int)string_format(buff, size, STRING_CONST("%.3gM $"), value / 1e6).length;
    else if (abs_value >= 1e3)
        return (int)string_format(buff, size, STRING_CONST("%.3gK $"), value / 1e3).length;

    return (int)string_format(buff, size, STRING_CONST("%.2lf $"), value).length;
}

int plot_value_format_date_monthly(double value, char* buff, int size, void* user_data)
{
    FOUNDATION_ASSERT(user_data);

    time_t d = (time_t)value;
    if (d == 0 || d == -1)
        return 0;

    double day_space = *(double*)user_data;
    string_const_t date_str = string_from_date(d);
    if (date_str.length == 0)
        return 0;

    if (day_space <= 5)
        return (int)string_copy(buff, size, date_str.str + 5, 5).length;

    return (int)string_copy(buff, size, date_str.str, min(date_str.length, (size_t)7)).length;
}

void plot_render_line_with_trend(plot_context_t& context, PlotGetter getter)
{
    FOUNDATION_ASSERT(getter);
    FOUNDATION_ASSERT(context.title);
    context.getter = getter;
    context.plotted = false;
    ImPlot::SetAxis(context.axis_y);
    ImPlot::PlotLineG(context.title, [](int idx, void* user_data)->ImPlotPoint
    {
        plot_context_t* c = (plot_context_t*)user_data;
        const ImPlotPoint p = c->getter(idx, c);

        plot_build_trend(*c, p.x, p.y);

        c->plotted = true;
        return p;
    }, &context, (int)context.range, ImPlotLineFlags_SkipNaN);

    if (context.plotted)
    {
        plot_compute_trend(context);
        ImPlot::HideNextItem(true, ImPlotCond_Once);
        plot_render_trend(context.title, context);
    }
}
