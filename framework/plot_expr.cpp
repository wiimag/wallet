/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wiimag.com/LICENSE
 *
 * Examples:
 *     PLOT('Graph', [0, 1, 2, 3], [100, 200, 300, 400], options...)
 */

#include "plot_expr.h"

#include <framework/expr.h>
#include <framework/window.h>

struct plot_expr_graph_t
{
    string_t title{};
    double* xset{ nullptr };
    double* yset{ nullptr };
};

struct plot_expr_t
{
    string_t id{};
    string_t* options{ nullptr };
    plot_expr_graph_t* graphs{ nullptr };
};

static plot_expr_t** _plot_exprs = nullptr;

FOUNDATION_STATIC bool plot_expr_has_option(plot_expr_t* plot, const char* option_name)
{
    const size_t option_name_length = string_length(option_name);
    for (unsigned i = 0, end = array_size(plot->options); i < end; ++i)
    {
        string_t option = plot->options[i];
        if (string_equal_nocase(STRING_ARGS(option), option_name, option_name_length))
            return true;
    }

    return false;
}

FOUNDATION_STATIC void plot_expr_render_window(window_handle_t win)
{
    plot_expr_t* plot = (plot_expr_t*)window_get_user_data(win);

    const size_t graph_count = array_size(plot->graphs);
    ImPlotFlags flags = ImPlotFlags_NoChild | ImPlotFlags_NoFrame;
    if (graph_count <= 1)
        flags |= ImPlotFlags_NoTitle | ImPlotFlags_NoLegend;

    if (!ImPlot::BeginPlot(plot->id.str, ImGui::GetContentRegionAvail(), flags))
        return;

    // Check if we have the option "xtime" to plot the x-axis as time scale
    const bool xtime = plot_expr_has_option(plot, "xtime");
    if (xtime)
        ImPlot::SetupAxisScale(ImAxis_X1, ImPlotScale_Time);

    for (unsigned i = 0, end = array_size(plot->graphs); i < end; ++i)
    {
        const plot_expr_graph_t* graph = plot->graphs + i;
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 4.0f, ImVec4(1, 0, 0, 1), 2);
        ImPlot::PlotLine(graph->title.str, graph->xset, graph->yset, array_size(graph->xset));
    }

    ImPlot::EndPlot();
}

FOUNDATION_STATIC void plot_expr_deallocate(plot_expr_t* plot)
{
    if (plot == nullptr)
        return;

    for (unsigned i = 0, end = array_size(plot->graphs); i < end; ++i)
    {
        plot_expr_graph_t* graph = plot->graphs + i;
        string_deallocate(graph->title);
        array_deallocate(graph->xset);
        array_deallocate(graph->yset);
    }

    string_array_deallocate(plot->options);
    array_deallocate(plot->graphs);
    string_deallocate(plot->id);
    memory_deallocate(plot);
}

FOUNDATION_STATIC void plot_expr_close_window(window_handle_t win)
{
    plot_expr_t* plot = (plot_expr_t*)window_get_user_data(win);
    plot_expr_deallocate(plot);
}

FOUNDATION_STATIC plot_expr_t* plot_expr_allocate(const char* id, size_t length)
{
    plot_expr_t* plot = memory_allocate<plot_expr_t>();
    plot->id = string_clone(id, length);
    return plot;
}

FOUNDATION_STATIC plot_expr_t* plot_expr_find(const char* id, size_t length)
{
    for (unsigned i = 0, end = array_size(_plot_exprs); i < end; ++i)
    {
        plot_expr_t* plot = _plot_exprs[i];
        if (string_equal(STRING_ARGS(plot->id), id, length))
            return plot;
    }

    return nullptr;
}

FOUNDATION_STATIC expr_result_t plot_expr_eval(const expr_func_t* f, vec_expr_t* args, void* c)
{
    if (args->len < 3)
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "PLOT requires at least 3 parameters, i.e. PLOT(title, xset, yset)");

    string_const_t id = expr_eval_get_string_arg(args, 0, "Invalid PLOT ID");
    expr_result_t xset = expr_eval_get_set_arg(args, 1, "Invalid x data set");
    expr_result_t yset = expr_eval_get_set_arg(args, 2, "Invalid y data set");

    string_const_t title = id;
    const size_t id_title_split_pos = string_find(STRING_ARGS(id), '#', 0);
    if (id_title_split_pos != STRING_NPOS)
    {
        id.length = id_title_split_pos;
        title.str += id_title_split_pos + 1;
        title.length -= id_title_split_pos + 1;
    }

    if (xset.element_count() != yset.element_count())
        throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "PLOT x and y data sets must have the same number of elements");

    plot_expr_graph_t graph{};
    graph.title = string_clone(title.str, title.length);

    for (auto e : xset)
        array_push(graph.xset, e.as_number());

    for (auto e : yset)
        array_push(graph.yset, e.as_number());

    // Check if we already have a plot with this ID
    plot_expr_t* plot = plot_expr_find(STRING_ARGS(id));
    if (!plot)
    {
        plot = plot_expr_allocate(STRING_ARGS(id));
        array_push(plot->graphs, graph);
        array_push(_plot_exprs, plot);

        if (!window_open("plot_expr_window", STRING_ARGS(plot->id), 
            plot_expr_render_window, plot_expr_close_window, plot))
        {
            plot_expr_deallocate(plot);
            throw ExprError(EXPR_ERROR_INVALID_ARGUMENT, "Failed to open plot window");
        }
    }
    else
    {
        array_push(plot->graphs, graph);
    }

    // Get the plotting options
    for (int i = 3, end = (int)args->len; i < end; ++i)
    {
        string_const_t option_arg = expr_eval_get_string_arg(args, i, "Invalid plotting option");
        string_t option = string_clone(option_arg.str, option_arg.length);
        array_push(plot->options, option);
    }

    return expr_eval_pair(xset, yset);
}

void plot_expr_initialize()
{
    expr_register_function("PLOT", plot_expr_eval);
}

void plot_expr_shutdown()
{
    array_deallocate(_plot_exprs);
}

