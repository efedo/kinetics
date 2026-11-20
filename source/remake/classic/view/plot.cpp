// Port of PLOT.CPP. Comments marked "v3.51" note behaviour kept on purpose;
// "Not in v3.51" marks the guards added where the original would have
// divided by zero or looped forever.

#include "classic/view/plot.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>

#include "classic/dos.hpp"
#include "classic/screen.hpp"
#include "core/plots.hpp"

namespace classic {
namespace {

// v3.51 tested coordinates with `x < 1e9`.
bool defined(float v) { return v < 1e9; }

// A float-to-int cast as v3.51 did it (truncation), kept within a range
// that cannot overflow.
int to_int(double v, double limit = 1e6) {
    if (!(v > -limit)) return static_cast<int>(-limit);
    if (v > limit) return static_cast<int>(limit);
    return static_cast<int>(v);
}

// The emulated outtextxy takes code page 437 bytes; the labels here are
// UTF-8.
void text_xy(int x, int y, const char* utf8) { outtextxy(x, y, cp437(utf8).c_str()); }

kin::PlotOptions plot_options(int origin, int txal, int tyal) {
    kin::PlotOptions o;
    o.includeOrigin = origin != 0;
    o.extendToXIntercept = txal != 0;  // v3.51 txal: extend the line to the x-axis (y = 0)
    o.extendToYAxis = tyal != 0;       // v3.51 tyal: extend the line to the y-axis (x = 0)
    return o;
}

// Not in v3.51: a loop that steps along an axis gives up after this many
// steps, or when the step is lost in float rounding (an axis far from zero
// with a tiny extent), where v3.51 would hang.
constexpr int kMaxSteps = 10000;

}  // namespace

// v3.51 marked an undefined coordinate with 1e10.
float plot_value(double v) {
    if (!std::isfinite(v) || std::fabs(v) >= 1e30) return 1e10f;
    return static_cast<float>(v);
}

void plot::set_frame() {
    left = 4 * getmaxx() / 24;
    right = 20 * getmaxx() / 24;
    top = 2 * getmaxy() / 17;
    bottom = 12 * getmaxy() / 17;
}

Task<void> plot::linear_plot(const float* x, const float* y, int num, float intercept, float slope) {
    int key = '1';
    int driver = DETECT, mode = 0, origin = 1;

    initgraph(&driver, &mode, "");
    set_frame();
    while (key != ESC) {
        setgraphmode(getgraphmode());
        get_min_max_point(num, x, y, origin);
        get_linear_min_max_line(slope, intercept);
        get_min_max_plot();
        draw_axes();
        plot_points(num, x, y);
        if (slope > 0) draw_straight_line(xmin_line, ymin_line, xmax_line, ymax_line);
        else draw_straight_line(xmax_line, ymin_line, xmin_line, ymax_line);
        label_graph("X", "Y", HORIZ_DIR, HORIZ_DIR);
        // v3.51: a special key is read as two keys (0, then its scan code),
        // each redrawing the plot; Alt+D's scan code is a space.
        key = std::tolower(co_await getch());
        if (key == ' ') origin = (origin ? 0 : 1);
        restorecrtmode();
    }
    closegraph();
}

Task<void> plot::non_linear_plot(const kin::Analysis& a, int pt, const char* xl, const char* yl, int origin,
                                 int txal, int tyal) {
    int key = '1';
    int driver = DETECT, mode = 0, orig_origin = origin, orig_txal = txal, orig_tyal = tyal, ydir = HORIZ_DIR;
    const auto type = static_cast<kin::PlotType>(pt);
    std::vector<float> x_line, y_line;

    initgraph(&driver, &mode, "");
    set_frame();
    // The transformed points (v3.51: get_xplot and get_yplot of each pair).
    const kin::PlotData points = kin::make_plot(a, type, plot_options(origin, txal, tyal));
    const int num = static_cast<int>(points.px.size());
    std::vector<float> x_point(static_cast<size_t>(num) + 1), y_point(static_cast<size_t>(num) + 1);
    for (int i = 1; i <= num; i++) {
        x_point[static_cast<size_t>(i)] = plot_value(points.px[static_cast<size_t>(i - 1)]);
        y_point[static_cast<size_t>(i)] = plot_value(points.py[static_cast<size_t>(i - 1)]);
    }
    while (key != ESC) {
        setgraphmode(getgraphmode());
        get_min_max_point(num, x_point.data(), y_point.data(), origin);
        // The fitted curve for this display mode.
        const kin::PlotData curve = kin::make_plot(a, type, plot_options(origin, txal, tyal));
        calculate_xy_line(curve.cx, curve.cy, x_line, y_line);
        get_min_max_plot();
        draw_axes();
        plot_points(num, x_point.data(), y_point.data());
        draw_curve(x_line, y_line);
        if (cp437(yl).size() > 3) ydir = VERT_DIR;
        label_graph(xl, yl, HORIZ_DIR, ydir);
        key = std::tolower(co_await getch());
        // SPACE cycles: line to both axes with the origin, then without the
        // lines' extension, then the same two without the origin.
        if (key == ' ') {
            if (origin && (txal || tyal)) txal = tyal = 0;
            else if (origin && (!txal || !tyal)) {
                origin = 0;
                txal = orig_txal;
                tyal = orig_tyal;
            } else if (!origin && (txal || tyal)) txal = tyal = 0;
            else {
                origin = orig_origin;
                txal = orig_txal;
                tyal = orig_tyal;
            }
        }
        restorecrtmode();
    }
    closegraph();
}

Task<void> plot::residual_plot(const float* yhat, const float* resid, int num) {
    int key = '1';
    int driver = DETECT, mode = 0, origin = 1;

    initgraph(&driver, &mode, "");
    set_frame();
    while (key != ESC) {
        setgraphmode(getgraphmode());
        get_min_max_point(num, yhat, resid, origin);
        xmin_line = xmin_point;
        xmax_line = xmax_point;
        ymin_line = ymin_point;
        ymax_line = ymax_point;
        get_min_max_plot();
        draw_axes();
        plot_points(num, yhat, resid);
        label_graph("Yhat", "Y - Yhat", HORIZ_DIR, VERT_DIR);
        key = std::tolower(co_await getch());
        if (key == ' ') origin = (origin ? 0 : 1);
        restorecrtmode();
    }
    closegraph();
}

void plot::draw_axes() {
    int xc, yc;
    float i;
    char l[20];
    int steps;

    settextstyle(0, HORIZ_DIR, 1);
    // Draw the x-axis
    if (ymax_plot * ymin_plot <= 0.0) yc = y_coord(0.0f);
    else if (ymax_plot < 0.0) yc = y_coord(ymax_plot);
    else yc = y_coord(ymin_plot);
    line(left - getmaxx() / 24, yc, right + getmaxx() / 24, yc);
    steps = 0;
    for (i = xmin_plot; i <= xmax_plot; i += xfactor * xincrement) {
        settextjustify(CENTER_TEXT, CENTER_TEXT);
        text_xy(x_coord(i), yc, "┴");
        std::snprintf(l, sizeof l, "%i", to_int(i / xfactor + i / xfactor / 100.0, 2e9));
        settextjustify(CENTER_TEXT, TOP_TEXT);
        outtextxy(x_coord(i), yc + textheight("A") / 2, l);
        if (++steps >= kMaxSteps || static_cast<float>(i + xfactor * xincrement) == i) break;
    }

    // Draw the y-axis
    if (xmax_plot * xmin_plot <= 0.0) xc = x_coord(0.0f);
    else if (xmax_plot <= 0.0) xc = x_coord(xmax_plot);
    else xc = x_coord(xmin_plot);
    line(xc, bottom + getmaxy() / 17, xc, top - getmaxy() / 17);
    steps = 0;
    for (i = ymin_plot; i <= ymax_plot; i += yfactor * yincrement) {
        settextjustify(LEFT_TEXT, CENTER_TEXT);
        outtextxy(xc, y_coord(i), "-");
        std::snprintf(l, sizeof l, "%i", to_int(i / yfactor + i / yfactor / 100.0, 2e9));
        settextjustify(RIGHT_TEXT, CENTER_TEXT);
        outtextxy(xc - textwidth("A") / 2, y_coord(i), l);
        if (++steps >= kMaxSteps || static_cast<float>(i + yfactor * yincrement) == i) break;
    }
}

void plot::plot_points(int num, const float* x_point, const float* y_point) {
    char s[20];

    settextstyle(0, HORIZ_DIR, 1);
    settextjustify(CENTER_TEXT, CENTER_TEXT);
    for (int i = 1; i <= num; i++) {
        if (defined(x_point[i]) && defined(y_point[i])) {
            std::snprintf(s, sizeof s, "%d", i);  // v3.51: itoa(i, s, 10)
            outtextxy(x_coord(x_point[i]), y_coord(y_point[i]), s);
        }
    }
}

int plot::y_coord(float z) {
    const float bmt = static_cast<float>(bottom - top);
    const float a = static_cast<float>(bmt * (1.0 - (z - ymin_plot) / (ymax_plot - ymin_plot)));
    return to_int(static_cast<float>(top) + a);
}

int plot::x_coord(float z) {
    const float rml = static_cast<float>(right - left);
    const float a = rml * (z - xmin_plot) / (xmax_plot - xmin_plot);
    return to_int(static_cast<float>(left) + a);
}

void plot::draw_straight_line(float x1, float y1, float x2, float y2) {
    line(x_coord(x1), y_coord(y1), x_coord(x2), y_coord(y2));
}

// v3.51 joined each defined point to the next defined one, skipping any
// undefined points in between.
void plot::draw_curve(const std::vector<float>& x_line, const std::vector<float>& y_line) {
    const int n = static_cast<int>(x_line.size()) - 1;
    int i = 1, j = 0;  // v3.51 left j uninitialised when point 1 was undefined
    float xs, ys;

    if (n < 2) return;
    do {
        if (defined(x_line[static_cast<size_t>(i)]) && defined(y_line[static_cast<size_t>(i)])) {
            xs = x_line[static_cast<size_t>(i)];
            ys = y_line[static_cast<size_t>(i)];
            for (j = i + 1; j <= n; j++) {
                if (defined(x_line[static_cast<size_t>(j)]) && defined(y_line[static_cast<size_t>(j)])) {
                    draw_straight_line(xs, ys, x_line[static_cast<size_t>(j)], y_line[static_cast<size_t>(j)]);
                    i = j;
                    break;
                }
            }
        } else {
            i++;
        }
    } while (j < n && i < n);
}

// v3.51 computed 100 points of the curve here, from the transforms passed
// in. The curve is now the core's (kin::make_plot); what is kept is how its
// extent sets the axes: every defined point counts.
void plot::calculate_xy_line(const std::vector<double>& cx, const std::vector<double>& cy, std::vector<float>& x_line,
                             std::vector<float>& y_line) {
    const int n = static_cast<int>(cx.size() < cy.size() ? cx.size() : cy.size());
    int i;

    x_line.assign(static_cast<size_t>(n) + 1, 1e10f);
    y_line.assign(static_cast<size_t>(n) + 1, 1e10f);
    for (i = 1; i <= n; i++) {
        x_line[static_cast<size_t>(i)] = plot_value(cx[static_cast<size_t>(i - 1)]);
        y_line[static_cast<size_t>(i)] = plot_value(cy[static_cast<size_t>(i - 1)]);
    }

    for (i = 1; i <= n; i++) {
        if (defined(x_line[static_cast<size_t>(i)]) && defined(y_line[static_cast<size_t>(i)])) {
            xmax_line = xmin_line = x_line[static_cast<size_t>(i)];
            ymax_line = ymin_line = y_line[static_cast<size_t>(i)];
            break;
        }
    }
    if (i > n) {
        // Not in v3.51 (which kept stale extents): no curve, so the points
        // alone set the axes.
        xmax_line = xmax_point;
        xmin_line = xmin_point;
        ymax_line = ymax_point;
        ymin_line = ymin_point;
    }

    for (++i; i <= n; i++) {
        if (defined(x_line[static_cast<size_t>(i)]) && defined(y_line[static_cast<size_t>(i)])) {
            xmax_line = MAX(xmax_line, x_line[static_cast<size_t>(i)]);
            xmin_line = MIN(xmin_line, x_line[static_cast<size_t>(i)]);
            ymax_line = MAX(ymax_line, y_line[static_cast<size_t>(i)]);
            ymin_line = MIN(ymin_line, y_line[static_cast<size_t>(i)]);
        }
    }
}

void plot::get_min_max_point(int num, const float* xp, const float* yp, int origin) {
    int i;

    for (i = 1; i <= num; i++) {
        if (defined(xp[i]) && defined(yp[i])) {
            xmax_point = xmin_point = xp[i];
            ymax_point = ymin_point = yp[i];
            break;
        }
    }
    // Not in v3.51, which left the extents undefined without a defined point.
    if (i > num) xmax_point = xmin_point = ymax_point = ymin_point = 0.0f;

    for (++i; i <= num; i++) {
        if (defined(xp[i]) && defined(yp[i])) {
            xmax_point = MAX(xmax_point, xp[i]);  // v3.51: xap = xmax_point = ...
            xmin_point = MIN(xmin_point, xp[i]);  // v3.51: xip = xmin_point = ...
            ymax_point = MAX(ymax_point, yp[i]);
            ymin_point = MIN(ymin_point, yp[i]);
        }
    }

    if (origin) {
        ymax_point = MAX(ymax_point, 0.0f);
        ymin_point = MIN(ymin_point, 0.0f);
        xmax_point = MAX(xmax_point, 0.0f);
        xmin_point = MIN(xmin_point, 0.0f);
    }
}

void plot::get_linear_min_max_line(float slope, float intercept) {
    xmin_line = xmin_point;
    xmax_line = xmax_point;
    if (slope > 0) {
        ymin_line = slope * xmin_point + intercept;
        ymax_line = slope * xmax_point + intercept;
    } else {
        ymin_line = slope * xmax_point + intercept;
        ymax_line = slope * xmin_point + intercept;
    }
}

void plot::get_min_max_plot() {
    xmax_plot = MAX(xmax_line, xmax_point);
    xmin_plot = MIN(xmin_line, xmin_point);
    ymax_plot = MAX(ymax_line, ymax_point);
    ymin_plot = MIN(ymin_line, ymin_point);

    xmax_plot = get_max_number(xmax_plot);
    xmin_plot = get_min_number(xmin_plot);
    ymax_plot = get_max_number(ymax_plot);
    ymin_plot = get_min_number(ymin_plot);

    // Not in v3.51, which divided by zero (and stepped by zero) when an
    // axis had no extent, for example when every residual is 0.
    if (!(xmax_plot > xmin_plot)) {
        xmin_plot = 0.0f;
        xmax_plot = 1.0f;
    }
    if (!(ymax_plot > ymin_plot)) {
        ymin_plot = 0.0f;
        ymax_plot = 1.0f;
    }

    get_step_size(xmin_plot, xmax_plot, xfactor, xincrement);
    get_step_size(ymin_plot, ymax_plot, yfactor, yincrement);
    xmin_plot = get_start_value(xmin_plot, xfactor, xincrement);
    xmax_plot = get_stop_value(xmax_plot, xfactor, xincrement);
    ymin_plot = get_start_value(ymin_plot, yfactor, yincrement);
    ymax_plot = get_stop_value(ymax_plot, yfactor, yincrement);
    if (!(xmax_plot > xmin_plot)) xmax_plot = xmin_plot + xfactor * xincrement;  // not in v3.51
    if (!(ymax_plot > ymin_plot)) ymax_plot = ymin_plot + yfactor * yincrement;  // not in v3.51
}

void plot::label_graph(const char* xl, const char* yl, int xdir, int ydir) {
    char lx[80], ly[80];
    int gap;

    settextstyle(0, HORIZ_DIR, 1);
    settextjustify(LEFT_TEXT, CENTER_TEXT);
    outtextxy(textwidth("A") * 10, textheight("A") * 2, "Press ESC to QUIT, SPACE BAR to change display mode");
    std::snprintf(lx, sizeof lx, "X-axis scaling factor: %g", static_cast<double>(xfactor));
    std::snprintf(ly, sizeof ly, "Y-axis scaling factor: %g", static_cast<double>(yfactor));
    gap = (getmaxx() - textwidth(lx) - textwidth(ly)) / 3;
    outtextxy(gap, getmaxy() - 3 * textheight("A"), lx);
    outtextxy(gap * 2 + textwidth(lx), getmaxy() - 3 * textheight("A"), ly);
    settextjustify(CENTER_TEXT, CENTER_TEXT);
    settextstyle(0, ydir, 2);
    text_xy(left / 2, top + (bottom - top) / 2, yl);
    settextstyle(0, xdir, 2);
    text_xy(left + 9 * (right - left) / 10, bottom + textheight("A") * 2, xl);
}

float plot::get_max_number(float max) {
    double sign, decl, intl;

    if (max == 0) return 0;
    sign = max / std::fabs(static_cast<double>(max));
    decl = std::modf(std::log10(std::fabs(static_cast<double>(max))), &intl);
    if (std::log10(std::fabs(static_cast<double>(max))) < 0.0) {
        decl++;
        intl--;
    }
    max = static_cast<float>(std::pow(10.0, decl));

    if (sign > 0) max = get_next_big_num(max);
    else max = get_next_small_num(max);
    max = static_cast<float>(max * (sign * std::pow(10.0, intl)));
    return max;
}

float plot::get_min_number(float min) {
    double sign, decl, intl;

    if (min == 0) return 0;
    sign = min / std::fabs(static_cast<double>(min));
    decl = std::modf(std::log10(std::fabs(static_cast<double>(min))), &intl);
    if (std::log10(std::fabs(static_cast<double>(min))) < 0.0) {
        decl++;
        intl--;
    }
    min = static_cast<float>(std::pow(10.0, decl));

    if (sign < 0) min = get_next_big_num(min);
    else min = get_next_small_num(min);
    min = static_cast<float>(min * (sign * std::pow(10.0, intl)));
    return min;
}

float plot::get_next_big_num(float num) {
    double n;
    for (n = 1.0; n <= 10.0; n += 0.1) {
        if ((n + n / 1e3) >= num) break;
    }
    return static_cast<float>(n);
}

float plot::get_next_small_num(float num) {
    double n;
    for (n = 10.0; n >= 1.0; n -= 0.1) {
        if ((n + n / 1e3) <= num) break;
    }
    return static_cast<float>(n);
}

void plot::get_step_size(float min, float max, float& factor, float& increment) {
    float span, test_step, logts, declts;
    double intlts;

    span = max - min;
    test_step = static_cast<float>(span / 10.0);
    logts = static_cast<float>(std::log10(static_cast<double>(test_step)));
    declts = static_cast<float>(std::modf(static_cast<double>(logts), &intlts));
    if (logts < 0.0) {
        declts++;
        intlts--;
    }

    factor = static_cast<float>(std::pow(10.0, intlts));
    if (declts == 0.0) increment = 1.0f;
    else if (declts < std::log10(2.0)) increment = 2.0f;
    else if (declts < std::log10(5.0)) increment = 5.0f;
    else {
        factor = static_cast<float>(factor * 10.0);
        increment = 1.0f;
    }
}

float plot::get_start_value(float min, float factor, float increment) {
    const double step_size = increment * factor;
    float sv = static_cast<float>(to_int(min / step_size + min / step_size / 100.0, 2e9) * step_size);
    for (int steps = 0; sv > min + min / 100.0 && steps < kMaxSteps; ++steps) {
        const float next = static_cast<float>(sv - step_size);
        if (next == sv) break;  // not in v3.51
        sv = next;
    }
    return sv;
}

float plot::get_stop_value(float max, float factor, float increment) {
    const double step_size = increment * factor;
    float sv = static_cast<float>(to_int(max / step_size + max / step_size / 100.0, 2e9) * step_size);
    for (int steps = 0; sv < max - max / 100.0 && steps < kMaxSteps; ++steps) {
        const float next = static_cast<float>(sv + step_size);
        if (next == sv) break;  // not in v3.51
        sv = next;
    }
    return sv;
}

}  // namespace classic
