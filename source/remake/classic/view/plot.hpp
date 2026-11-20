#pragma once
// Port of PLOT.HPP/PLOT.CPP: the BGI graphs of the VIEW menu, drawn in VGA
// high resolution with the BIOS 8x8 font (classic/dos.hpp).
//
// The axis scaling, tick labels, scaling-factor captions, numbered points
// and keys are the original's. What is drawn comes from the shared core:
// the transformed points and the fitted curve are kin::make_plot's, for the
// session's result, so they include the core's fixes to v3.51's
// transforms. v3.51 passed the transforms in as function pointers
// (get_xplot, get_yplot, return_yhat, ...); here non_linear_plot takes the
// analysis and plot type and asks the core for each display mode.
//
// Arrays are 1-based, as in v3.51. A coordinate of 1e10 or more means
// "undefined" (v3.51's convention; the core's NaN is turned into 1e10).

#include <vector>

#include "classic/task.hpp"
#include "core/analysis.hpp"

namespace classic {

// A coordinate from the core as v3.51 held it: a float, 1e10 where undefined.
float plot_value(double v);

inline constexpr int LINEAR_PLOT = 0;
inline constexpr int NON_LINEAR_PLOT = 1;

class plot {
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;
    // v3.51 also had xap and xip, the points' x extent before the origin
    // was added, for calculate_xy_line; the core's curve does that itself.
    float xmax_point = 0;
    float xmin_point = 0;
    float ymax_point = 0;
    float ymin_point = 0;
    float ymax_line = 0;
    float ymin_line = 0;
    float xmax_line = 0;
    float xmin_line = 0;
    float xmax_plot = 0;
    float xmin_plot = 0;
    float ymax_plot = 0;
    float ymin_plot = 0;
    float xfactor = 1;
    float xincrement = 1;
    float yfactor = 1;
    float yincrement = 1;

    // v3.51 computed the curve itself (100 points); here it is the core's.
    void calculate_xy_line(const std::vector<double>& cx, const std::vector<double>& cy,
                           std::vector<float>& x_line, std::vector<float>& y_line);
    void draw_axes();
    void draw_curve(const std::vector<float>& x_line, const std::vector<float>& y_line);
    void draw_straight_line(float x1, float y1, float x2, float y2);
    void get_linear_min_max_line(float slope, float intercept);
    static float MAX(float a, float b) { return (a > b ? a : b); }
    static float MIN(float a, float b) { return (a > b ? b : a); }
    void get_min_max_plot();
    void get_min_max_point(int num, const float* xp, const float* yp, int origin);
    void label_graph(const char* xl, const char* yl, int xdir, int ydir);
    void plot_points(int num, const float* x_point, const float* y_point);
    void set_frame();
    int x_coord(float);
    int y_coord(float);
    float get_start_value(float, float, float);
    float get_stop_value(float, float, float);
    float get_next_big_num(float);
    float get_next_small_num(float);
    void get_step_size(float, float, float&, float&);
    float get_max_number(float);
    float get_min_number(float);
    // v3.51's print_error (an error box, then exit to DOS) was never called.

public:
    // x[1..num], y[1..num]: the data; the line is intercept + slope*x.
    Task<void> linear_plot(const float* x, const float* y, int num, float intercept, float slope);
    // pt is the plot type (P_NORMAL ... P_ARRHENIUS); xl and yl are the axis
    // labels (UTF-8); origin, txal and tyal the starting display mode.
    Task<void> non_linear_plot(const kin::Analysis& a, int pt, const char* xl, const char* yl, int origin, int txal,
                               int tyal);
    Task<void> residual_plot(const float* yhat, const float* resid, int num);
};

}  // namespace classic
