/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://wallet.wiimag.com/LICENSE
 *
 * ImPlot utility module
 */

#pragma once

#include <framework/imgui.h>

struct plot_context_t;

typedef ImPlotPoint (*PlotGetter)(int idx, plot_context_t* context);

struct plot_axis_format_t
{
    bool print_short_value{ true };
    int print_stage{ 0 };
    time_t last_year{ 0 };
    ImPlotRect limits;
};

struct plot_context_t
{
    // Plot data fields
    time_t ref;
    size_t range;
    size_t stride;
    const void* user_data;
    
    // Contextual data fields
    double acc{ 0 };
    double lx{ 0.0 }, ly{ 0 }, lz{ 0 };

    double x_min{ DBL_MAX }, x_max{ -DBL_MAX }, n{ 0 };
    double a{ 0 }, b{ 0 }, c{ 0 }, d{ 0 }, e{ 0 }, f{ 0 };

    // TODO: Change with enum flags
    bool plotted { false };
    bool relative_dates{ false };
    bool compacted{ false };
    bool show_equation{ false };
    bool x_axis_inverted{ false };
    bool flipped{ false };

    ImPlotPoint mouse_pos{};
    ImPlotPoint cursor_xy1{};
    ImPlotPoint cursor_xy2{};

    union {
        // Custom plotting
        struct {
            ImAxis axis_y;
            const char* title;
            PlotGetter getter;
        };
    };    

    template<typename T> T* get_user_data() const
    {
        return (T*)user_data;
    }
};

void plot_compute_trend(plot_context_t& c);

bool plot_build_trend(plot_context_t& c, double x, double y);

void plot_render_trend(const char* label, const plot_context_t& c);

void plot_render_limit(const char* label, double min, double max, double value);

void plot_render_line_with_trend(plot_context_t& context, PlotGetter getter);

int plot_value_format_date(double value, char* buff, int size, void* user_data);

int plot_value_format_year(double value, char* buff, int size, void* user_data);

int plot_value_format_elapsed_time_short(double value, char* buff, int size, void* user_data);

int plot_value_format_currency_short(double value, char* buff, int size, void* user_data);

int plot_value_format_date_monthly(double value, char* buff, int size, void* user_data);
