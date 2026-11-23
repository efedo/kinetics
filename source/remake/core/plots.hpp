#pragma once
// The ten plot types of v3.51 (KIN_VIEW.CPP / PLOT.CPP): data transforms,
// which plots suit which equation, and the fitted curve in plot coordinates.

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/analysis.hpp"

namespace kin {

enum class PlotType : int {
    Normal = 1,      // v vs [S]
    LineweaverBurk,  // 1/v vs 1/[S]
    HanesWoolf,      // [S]/v vs [S]
    EadieHofstee,    // v/[S] vs v
    Hill,            // log10((Vmax - v)/v) vs log[S]
    LogLog,          // log(v) vs log[S]
    Saturation,      // v/Vmax vs log([S]/Km)
    Job,             // v0/v vs [I]
    Arrhenius,       // log10(v) vs 1/T (K)
    Residual,        // residual vs fitted value
};

std::vector<PlotType> available_plots(ModelId model);
std::string plot_name(PlotType type, ModelId model);  // menu text
std::string plot_x_label(PlotType type, ModelId model);
std::string plot_y_label(PlotType type, ModelId model);

struct PlotOptions {
    bool includeOrigin = true;       // axes include (0, 0)
    bool extendToYAxis = true;       // curve drawn to X = 0
    bool extendToXIntercept = true;  // curve drawn to where it meets Y = 0
};

// The starting display mode v3.51 used for each plot.
PlotOptions default_plot_options(PlotType type, ModelId model);

struct PlotData {
    std::string xLabel, yLabel;
    std::vector<double> px, py;   // transformed data (NaN where undefined)
    std::vector<int> labels;      // 1-based point numbers
    std::vector<double> cx, cy;   // fitted curve; NaN separates segments
    std::optional<double> xIntercept;  // where the curve meets Y = 0, in plot units
    bool zeroLine = false;        // draw Y = 0 (residual plot)
    // Axis extents with a margin (including the origin when requested).
    double xmin = 0, xmax = 1, ymin = 0, ymax = 1;
};

PlotData make_plot(const Analysis& analysis, PlotType type, const PlotOptions& options);

// A point given in the data's units (x, y) in the plot's coordinates, as
// make_plot transforms the data; NaN where the transform is undefined, and
// on the residual plot, whose axes are not the data's.
std::pair<double, double> plot_coordinates(const Analysis& analysis, PlotType type, double x, double y);

}  // namespace kin
