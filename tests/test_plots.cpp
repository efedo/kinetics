// Tests for core/plots: which plots suit which equation, labels and default
// options, the data transforms, the fitted curve in plot coordinates (the
// straight lines of the linearised plots, with their slopes and intercepts
// worked out by hand), the X-intercept, and the axis extents.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "core/analysis.hpp"
#include "core/models.hpp"
#include "core/plots.hpp"

#include "check.hpp"

using namespace kin;

namespace {

using Vec = std::vector<double>;
using P = PlotType;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kLn10R = 2.302585092994046 * 1.98720425864083e-3;  // ln(10) * R in kcal/(mol K)

// An analysis with the given parameters, as if fitted; yhat and resid follow
// from the parameters.
Analysis analysis(ModelId m, const Vec& p, const std::vector<Point>& data) {
    Analysis a;
    a.request.model = m;
    a.fit.p = p;
    a.data = data;
    for (const auto& pt : data) {
        a.yhat.push_back(evaluate(m, pt.x, p));
        a.resid.push_back(pt.y - a.yhat.back());
    }
    return a;
}

std::vector<Point> on_curve(ModelId m, const Vec& p, const Vec& xs) {
    std::vector<Point> pts;
    for (double x : xs) pts.push_back({x, evaluate(m, x, p)});
    return pts;
}

PlotOptions opts(bool origin, bool toYAxis, bool toXIntercept) {
    PlotOptions o;
    o.includeOrigin = origin;
    o.extendToYAxis = toYAxis;
    o.extendToXIntercept = toXIntercept;
    return o;
}

PlotData plot(const Analysis& a, PlotType t) { return make_plot(a, t, default_plot_options(t, a.model())); }

double finite_min(const Vec& v) {
    double r = INFINITY;
    for (double x : v)
        if (std::isfinite(x)) r = std::min(r, x);
    return r;
}

double finite_max(const Vec& v) {
    double r = -INFINITY;
    for (double x : v)
        if (std::isfinite(x)) r = std::max(r, x);
    return r;
}

// Checks every finite curve sample against Y = f(X); returns how many there are.
template <class F>
int check_curve(const PlotData& pd, F f, double tol, const char* what) {
    int n = 0;
    CHECK(pd.cx.size() == pd.cy.size());
    for (size_t k = 0; k < pd.cx.size(); ++k) {
        if (!std::isfinite(pd.cx[k])) {
            CHECK(std::isnan(pd.cy[k]));  // gaps are NaN in both
            continue;
        }
        ++n;
        const double want = f(pd.cx[k]);
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s: X=%.10g Y=%.12g want %.12g", what, pd.cx[k], pd.cy[k], want);
        ::test::check(std::fabs(pd.cy[k] - want) <= tol * (1.0 + std::fabs(want)), "curve on the line", __FILE__,
                      __LINE__, buf);
    }
    return n;
}

// Both NaN-or-infinite, or close.
bool same(double got, double want, double rel = 1e-12) {
    if (!std::isfinite(want)) return std::isnan(got);
    return std::fabs(got - want) <= rel * (1.0 + std::fabs(want));
}

const Vec kSubstrate = {0, 0.5, 1, 2, 4, 8};

// ---- Independent references for the sweep over every model and plot -------

// The equations as displayed in the model table.
double ref_v(ModelId m, const Vec& p, double x) {
    const auto spow = [](double b, double e) { return b < 0 ? -std::pow(-b, e) : std::pow(b, e); };
    switch (m) {
        case ModelId::MichaelisMenten: return p[1] * x / (p[0] + x);
        case ModelId::HillGt0:
        case ModelId::HillGe1: return p[2] * spow(x, p[1]) / (std::pow(p[0], p[1]) + spow(x, p[1]));
        case ModelId::ActivatorGt0:
        case ModelId::ActivatorGe1:
            return p[3] + (p[2] - p[3]) * spow(x, p[1]) / (std::pow(p[0], p[1]) + spow(x, p[1]));
        case ModelId::ActivatorEq1: return p[2] + (p[1] - p[2]) * x / (p[0] + x);
        case ModelId::InhibitionLinear: return p[1] * p[0] / (p[0] + x);
        case ModelId::InhibitionI50:
            return p[3] - (p[3] - p[2]) * spow(x, p[1]) /
                              (spow(x, p[1]) + std::pow(p[0], p[1]) * (1.0 - 2.0 * p[2] / p[3]));
        case ModelId::InhibitionK50:
            return p[3] - (p[3] - p[2]) * spow(x, p[1]) / (spow(x, p[1]) + std::pow(p[0], p[1]));
        case ModelId::Arrhenius: return p[0] - p[1] / (kLn10R * (x + 273.15));
        case ModelId::Linear: return p[0] + p[1] * x;
    }
    return kNaN;
}

// The transforms as described in plots.hpp. K is the half-saturation
// constant, V is Vmax (v0 for the inhibition equations).
void ref_transform(PlotType t, double x, double v, double K, double V, double& X, double& Y) {
    X = Y = kNaN;
    switch (t) {
        case P::Normal: X = x; Y = v; break;
        case P::LineweaverBurk: X = 1 / x; Y = 1 / v; break;
        case P::HanesWoolf: X = x; Y = x / v; break;
        case P::EadieHofstee: X = v; Y = v / x; break;
        case P::Hill:
            X = std::log10(x);
            if (v > 0 && v < V) Y = std::log10((V - v) / v);
            break;
        case P::LogLog: X = std::log10(x); Y = std::log10(v); break;
        case P::Saturation: X = std::log10(x / K); Y = v / V; break;
        case P::Job: X = x; Y = V / v; break;
        case P::Arrhenius: X = 1 / (x + 273.15); Y = v; break;
        case P::Residual: break;
    }
}

// Raw x for a plot X (all plots except Eadie-Hofstee, whose X is v).
double ref_raw_x(PlotType t, double X, double K) {
    switch (t) {
        case P::LineweaverBurk: return 1 / X;
        case P::Hill:
        case P::LogLog: return std::pow(10.0, X);
        case P::Saturation: return K * std::pow(10.0, X);
        case P::Arrhenius: return 1 / X - 273.15;
        default: return X;
    }
}

struct SweepCase {
    ModelId m;
    Vec p;
    Vec x;
    double K, V;
};

std::vector<SweepCase> sweep_cases() {
    const Vec conc = {0, 0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16, 24};
    const Vec inh = {0, 0.5, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32};
    return {
        {ModelId::MichaelisMenten, {2.5, 10}, conc, 2.5, 10},
        {ModelId::HillGt0, {3, 1.8, 12}, conc, 3, 12},
        {ModelId::HillGe1, {3, 1.8, 12}, conc, 3, 12},
        {ModelId::ActivatorGt0, {3, 1.6, 12, 2}, inh, 3, 12},
        {ModelId::ActivatorGe1, {3, 1.6, 12, 2}, inh, 3, 12},
        {ModelId::ActivatorEq1, {3, 12, 2}, inh, 3, 12},
        {ModelId::InhibitionLinear, {4, 20}, inh, 4, 20},
        {ModelId::InhibitionI50, {4, 1.5, 3, 20}, inh, 4, 20},
        {ModelId::InhibitionK50, {4, 1.5, 3, 20}, inh, 4, 20},
        {ModelId::Arrhenius, {9, 12}, {5, 10, 15, 20, 25, 30, 35, 40}, 1, 1},
        {ModelId::Linear, {1.5, 2}, {0, 1, 2, 3, 4, 5}, 1, 1},
    };
}

}  // namespace

// ---------------------------------------------------------------------------
// Plot menus, labels and options

TEST_CASE(plots_available, "plots: available plot types for each family") {
    const std::vector<PlotType> saturable = {P::Normal, P::LineweaverBurk, P::HanesWoolf, P::EadieHofstee,
                                             P::Hill,   P::LogLog,         P::Saturation, P::Residual};
    for (ModelId m : {ModelId::MichaelisMenten, ModelId::HillGt0, ModelId::HillGe1, ModelId::ActivatorGt0,
                      ModelId::ActivatorGe1, ModelId::ActivatorEq1})
        CHECK(available_plots(m) == saturable);
    for (ModelId m : {ModelId::InhibitionLinear, ModelId::InhibitionI50, ModelId::InhibitionK50})
        CHECK(available_plots(m) == (std::vector<PlotType>{P::Normal, P::Saturation, P::Job, P::Residual}));
    CHECK(available_plots(ModelId::Arrhenius) == (std::vector<PlotType>{P::Normal, P::Arrhenius, P::Residual}));
    CHECK(available_plots(ModelId::Linear) == (std::vector<PlotType>{P::Normal, P::LogLog, P::Residual}));
}

TEST_CASE(plots_labels, "plots: axis labels") {
    const ModelId mm = ModelId::MichaelisMenten;
    const struct {
        PlotType t;
        const char* x;
        const char* y;
    } expect[] = {
        {P::Normal, "[S]", "v"},
        {P::LineweaverBurk, "1/[S]", "1/v"},
        {P::HanesWoolf, "[S]", "[S]/v"},
        {P::EadieHofstee, "v", "v/[S]"},
        {P::Hill, "log10[S]", "log10((Vmax - v)/v)"},
        {P::LogLog, "log10[S]", "log10(v)"},
        {P::Saturation, "log10([S]/Km)", "v/Vmax"},
        {P::Job, "[S]", "v0/v"},
        {P::Arrhenius, "1/T (1/K)", "v"},
        {P::Residual, "Fitted v", "Residual (v - fitted)"},
    };
    for (const auto& e : expect) {
        CHECK(plot_x_label(e.t, mm) == e.x);
        CHECK(plot_y_label(e.t, mm) == e.y);
    }
    // The half-saturation constant's own name, and v0 for inhibition.
    CHECK(plot_x_label(P::Saturation, ModelId::ActivatorGt0) == "log10([A]/Ka)");
    CHECK(plot_x_label(P::Saturation, ModelId::InhibitionLinear) == "log10([I]/I50)");
    CHECK(plot_x_label(P::Saturation, ModelId::InhibitionK50) == "log10([I]/K50)");
    CHECK(plot_y_label(P::Saturation, ModelId::InhibitionI50) == "v/v0");
    CHECK(plot_x_label(P::Job, ModelId::InhibitionK50) == "[I]");
    // Names without brackets get parentheses.
    CHECK(plot_x_label(P::LogLog, ModelId::Linear) == "log10(X)");
    CHECK(plot_y_label(P::LogLog, ModelId::Linear) == "log10(Y)");
    CHECK(plot_x_label(P::Arrhenius, ModelId::Arrhenius) == "1/T (1/K)");
    CHECK(plot_y_label(P::Arrhenius, ModelId::Arrhenius) == "log10(v)");
    CHECK(plot_x_label(P::Residual, ModelId::Arrhenius) == "Fitted log10(v)");
    for (const auto& def : all_models()) {
        CHECK(plot_x_label(P::Normal, def.id) == std::string(def.xName));
        CHECK(plot_y_label(P::Normal, def.id) == std::string(def.yName));
    }
    // Without a half-saturation constant the label falls back to "Km".
    CHECK(plot_x_label(P::Saturation, ModelId::Linear) == "log10(X/Km)");
    // An unknown plot type shows the plain variable names.
    CHECK(plot_x_label(static_cast<PlotType>(0), mm) == "[S]");
    CHECK(plot_y_label(static_cast<PlotType>(0), mm) == "v");
}

TEST_CASE(plots_names, "plots: menu names") {
    const ModelId mm = ModelId::MichaelisMenten;
    CHECK(plot_name(P::Normal, mm) == "Normal (v vs. [S])");
    CHECK(plot_name(P::LineweaverBurk, mm) == "Double-reciprocal (1/v vs. 1/[S])");
    CHECK(plot_name(P::HanesWoolf, mm) == "Hanes-Woolf ([S]/v vs. [S])");
    CHECK(plot_name(P::EadieHofstee, mm) == "Eadie-Hofstee (v/[S] vs. v)");
    CHECK(plot_name(P::Hill, mm) == "Hill (log10((Vmax - v)/v) vs. log10[S])");
    CHECK(plot_name(P::LogLog, mm) == "Log-log (log10(v) vs. log10[S])");
    CHECK(plot_name(P::Saturation, mm) == "Saturation (v/Vmax vs. log10([S]/Km))");
    CHECK(plot_name(P::Job, ModelId::InhibitionLinear) == "Job plot for inhibition (v0/v vs. [I])");
    CHECK(plot_name(P::Arrhenius, ModelId::Arrhenius) == "Arrhenius (log10(v) vs. 1/T (1/K))");
    CHECK(plot_name(P::Residual, mm) == "Residuals (Residual (v - fitted) vs. Fitted v)");
    CHECK(plot_name(static_cast<PlotType>(0), mm) == "Plot");
}

TEST_CASE(plots_default_options, "plots: default display options") {
    struct Row {
        ModelId m;
        PlotType t;
        bool origin, toY, toX;
    };
    using M = ModelId;
    const Row rows[] = {
        // Michaelis-Menten: every straight-line plot is extended to both axes.
        {M::MichaelisMenten, P::Normal, true, true, true},
        {M::MichaelisMenten, P::LineweaverBurk, true, true, true},
        {M::MichaelisMenten, P::HanesWoolf, true, true, true},
        {M::MichaelisMenten, P::EadieHofstee, true, true, true},
        {M::MichaelisMenten, P::Hill, true, true, true},
        {M::MichaelisMenten, P::Saturation, true, true, false},
        {M::MichaelisMenten, P::Residual, true, false, false},
        // Hill and activator: Hanes-Woolf and Eadie-Hofstee are curved.
        {M::HillGt0, P::LineweaverBurk, true, true, true},
        {M::HillGt0, P::HanesWoolf, true, true, false},
        {M::HillGe1, P::EadieHofstee, true, false, false},
        {M::ActivatorGt0, P::HanesWoolf, true, true, false},
        {M::ActivatorEq1, P::EadieHofstee, true, false, false},
        {M::ActivatorGe1, P::Hill, true, true, true},
        // Inhibition: the Job plot is not extended to an X-intercept.
        {M::InhibitionLinear, P::Normal, true, true, true},
        {M::InhibitionK50, P::Job, true, true, false},
        {M::InhibitionI50, P::Saturation, true, true, false},
        {M::InhibitionI50, P::Residual, true, false, false},
        // Arrhenius: the origin is far from the 1/T data.
        {M::Arrhenius, P::Normal, false, false, false},
        {M::Arrhenius, P::Arrhenius, false, false, false},
        {M::Arrhenius, P::Residual, false, false, false},
        {M::Linear, P::Normal, true, true, true},
        {M::Linear, P::LogLog, true, true, true},
        {M::Linear, P::Residual, true, false, false},
    };
    for (const auto& r : rows) {
        const auto o = default_plot_options(r.t, r.m);
        char buf[96];
        std::snprintf(buf, sizeof buf, "model %c plot %d", model_def(r.m).letter, static_cast<int>(r.t));
        ::test::check(o.includeOrigin == r.origin && o.extendToYAxis == r.toY && o.extendToXIntercept == r.toX,
                      "default options", __FILE__, __LINE__, buf);
    }
}

// ---------------------------------------------------------------------------
// The linearised plots of Michaelis-Menten (Km = 2.5, Vmax = 10)

// 1/v = 1/Vmax + (Km/Vmax)(1/[S]): intercepts 1/Vmax = 0.1 and -1/Km = -0.4.
TEST_CASE(plots_lineweaver_burk, "plots: Lineweaver-Burk line has intercepts 1/Vmax and -1/Km") {
    const auto a = analysis(ModelId::MichaelisMenten, {2.5, 10}, on_curve(ModelId::MichaelisMenten, {2.5, 10}, kSubstrate));
    const auto line = [](double X) { return 0.1 + 0.25 * X; };

    const auto pd = plot(a, P::LineweaverBurk);
    CHECK(pd.xLabel == "1/[S]" && pd.yLabel == "1/v");
    CHECK(pd.px.size() == 6 && pd.labels.size() == 6);
    CHECK(pd.labels.front() == 1 && pd.labels.back() == 6);
    CHECK(std::isnan(pd.px[0]) && std::isnan(pd.py[0]));  // [S] = 0, v = 0
    for (size_t i = 1; i < kSubstrate.size(); ++i) {
        const double s = kSubstrate[i];
        CHECK_NEAR(pd.px[i], 1.0 / s, 1e-15);
        CHECK_NEAR(pd.py[i], (2.5 + s) / (10.0 * s), 1e-15);
    }
    CHECK(pd.xIntercept && std::fabs(*pd.xIntercept + 0.4) < 1e-15);
    CHECK(!pd.zeroLine);
    // Extended to the X-intercept: from -1/Km (where v is undefined) to 1/0.5.
    CHECK(pd.cx.size() == 400);
    CHECK(std::isnan(pd.cx.front()));
    CHECK_NEAR(pd.cx.back(), 2.0, 1e-12);
    CHECK(check_curve(pd, line, 1e-12, "LB") == 399);
    const double cxlo = -0.4 + 2.4 / 399;  // first defined sample
    CHECK_NEAR(finite_min(pd.cx), cxlo, 1e-12);
    CHECK_NEAR(pd.xmin, cxlo - 0.05 * (2.0 - cxlo), 1e-12);
    CHECK_NEAR(pd.xmax, 2.0 + 0.05 * (2.0 - cxlo), 1e-12);
    CHECK_NEAR(pd.ymin, -0.03, 1e-12);  // origin to 1/v(0.5) = 0.6
    CHECK_NEAR(pd.ymax, 0.63, 1e-12);

    // Extended to the Y axis only: the sample at 1/[S] = 0 is undefined,
    // the next is just right of it, and the intercept is still reported.
    const auto toY = make_plot(a, P::LineweaverBurk, opts(true, true, false));
    CHECK(std::isnan(toY.cx.front()));
    CHECK_NEAR(toY.cx[1], 2.0 / 399, 1e-15);
    CHECK(toY.xIntercept && std::fabs(*toY.xIntercept + 0.4) < 1e-15);
    CHECK(check_curve(toY, line, 1e-12, "LB to Y axis") == 399);

    // Not extended, no origin: axes are the data range plus 5%.
    const auto bare = make_plot(a, P::LineweaverBurk, opts(false, false, false));
    CHECK_NEAR(bare.cx.front(), 0.125, 1e-15);
    CHECK_NEAR(bare.cx.back(), 2.0, 1e-15);
    CHECK(check_curve(bare, line, 1e-12, "LB bare") == 400);
    CHECK_NEAR(bare.xmin, 0.125 - 0.05 * 1.875, 1e-12);
    CHECK_NEAR(bare.xmax, 2.0 + 0.05 * 1.875, 1e-12);
    CHECK_NEAR(bare.ymin, 0.13125 - 0.05 * 0.46875, 1e-12);  // 1/v(8) = 10.5/80
    CHECK_NEAR(bare.ymax, 0.6 + 0.05 * 0.46875, 1e-12);
}

// [S]/v = Km/Vmax + [S]/Vmax: slope 0.1, intercept 0.25, X-intercept -Km.
TEST_CASE(plots_hanes_woolf, "plots: Hanes-Woolf line has slope 1/Vmax and X-intercept -Km") {
    const auto a = analysis(ModelId::MichaelisMenten, {2.5, 10}, on_curve(ModelId::MichaelisMenten, {2.5, 10}, kSubstrate));
    const auto pd = plot(a, P::HanesWoolf);
    CHECK(pd.px[0] == 0.0 && std::isnan(pd.py[0]));
    for (size_t i = 1; i < kSubstrate.size(); ++i) {
        CHECK(pd.px[i] == kSubstrate[i]);
        CHECK_NEAR(pd.py[i], 0.25 + 0.1 * kSubstrate[i], 1e-15);
    }
    CHECK(pd.xIntercept && *pd.xIntercept == -2.5);
    // From -Km (undefined) through [S] = 0 (undefined) to 8.
    CHECK(std::isnan(pd.cx.front()));
    CHECK(check_curve(pd, [](double X) { return 0.25 + 0.1 * X; }, 1e-12, "HW") == 398);
    CHECK(finite_min(pd.cx) < -2.4);

    // For the Hill equation the plot is curved, so it is drawn from the Y
    // axis, but -Km is still reported.
    const auto h = analysis(ModelId::HillGt0, {3, 1.8, 12}, on_curve(ModelId::HillGt0, {3, 1.8, 12}, kSubstrate));
    const auto hw = plot(h, P::HanesWoolf);
    CHECK(hw.xIntercept && *hw.xIntercept == -3.0);
    CHECK(finite_min(hw.cx) > 0.0);
    CHECK_NEAR(finite_max(hw.cx), 8.0, 1e-12);
}

// v/[S] = Vmax/Km - v/Km: slope -0.4, intercept 4, X-intercept Vmax.
TEST_CASE(plots_eadie_hofstee, "plots: Eadie-Hofstee line has slope -1/Km and X-intercept Vmax") {
    const auto a = analysis(ModelId::MichaelisMenten, {2.5, 10}, on_curve(ModelId::MichaelisMenten, {2.5, 10}, kSubstrate));
    const auto pd = plot(a, P::EadieHofstee);
    CHECK(pd.xLabel == "v" && pd.yLabel == "v/[S]");
    CHECK(pd.px[0] == 0.0 && std::isnan(pd.py[0]));
    for (size_t i = 1; i < kSubstrate.size(); ++i) {
        const double s = kSubstrate[i], v = 10 * s / (2.5 + s);
        CHECK_NEAR(pd.px[i], v, 1e-14);
        CHECK_NEAR(pd.py[i], v / s, 1e-14);
    }
    CHECK(pd.xIntercept && *pd.xIntercept == 10.0);
    // From v = 0 ([S] = 0: undefined) to v = Vmax (no [S]: undefined).
    CHECK(std::isnan(pd.cx.front()) && std::isnan(pd.cx.back()));
    CHECK(check_curve(pd, [](double X) { return 4.0 - 0.4 * X; }, 1e-12, "EH") == 398);
    CHECK_NEAR(finite_max(pd.cx), 10.0 * 398 / 399, 1e-12);

    // Hill equation: not extended, so the curve spans the data velocities.
    const auto h = analysis(ModelId::HillGt0, {3, 1.8, 12}, on_curve(ModelId::HillGt0, {3, 1.8, 12}, kSubstrate));
    const auto eh = plot(h, P::EadieHofstee);
    CHECK_NEAR(finite_min(eh.cx), h.data[1].y, 1e-12);
    CHECK_NEAR(finite_max(eh.cx), h.data.back().y, 1e-12);
    CHECK(eh.xIntercept && *eh.xIntercept == 12.0);
}

// log10((Vmax - v)/v) = h log10(Km) - h log10[S].
TEST_CASE(plots_hill, "plots: Hill plot has slope -h and crosses zero at log10(Km)") {
    auto data = on_curve(ModelId::HillGt0, {3, 1.8, 12}, kSubstrate);
    data.push_back({50, 12.5});  // above Vmax: no Hill transform
    const auto a = analysis(ModelId::HillGt0, {3, 1.8, 12}, data);
    const auto pd = plot(a, P::Hill);
    CHECK(std::isnan(pd.px[0]) && std::isnan(pd.py[0]));  // log of 0
    CHECK_NEAR(pd.px[6], std::log10(50.0), 1e-15);
    CHECK(std::isnan(pd.py[6]));
    for (size_t i = 1; i < kSubstrate.size(); ++i) {
        CHECK_NEAR(pd.px[i], std::log10(kSubstrate[i]), 1e-15);
        CHECK_NEAR(pd.py[i], 1.8 * std::log10(3.0 / kSubstrate[i]), 1e-12);
    }
    CHECK(pd.xIntercept && std::fabs(*pd.xIntercept - std::log10(3.0)) < 1e-12);
    const double c = 1.8 * std::log10(3.0);
    CHECK(check_curve(pd, [c](double X) { return c - 1.8 * X; }, 1e-10, "Hill") == 400);

    // Michaelis-Menten has h = 1.
    const auto mm = analysis(ModelId::MichaelisMenten, {2.5, 10}, on_curve(ModelId::MichaelisMenten, {2.5, 10}, kSubstrate));
    const auto pm = plot(mm, P::Hill);
    CHECK(pm.xIntercept && std::fabs(*pm.xIntercept - std::log10(2.5)) < 1e-12);
    const double cm = std::log10(2.5);
    CHECK(check_curve(pm, [cm](double X) { return cm - X; }, 1e-10, "Hill MM") == 400);

    // Activator: zero where v = Vmax/2.
    // python: np.log10(((6 - 2) * 3**1.6 / (12 - 6)) ** (1/1.6))
    const Vec act = {3, 1.6, 12, 2};
    const auto pa = plot(analysis(ModelId::ActivatorGt0, act, on_curve(ModelId::ActivatorGt0, act, kSubstrate)), P::Hill);
    CHECK(pa.xIntercept && std::fabs(*pa.xIntercept - 0.36706421780986165) < 1e-12);
    // With v0 above Vmax/2 the Hill transform never reaches zero.
    const Vec high = {3, 1.6, 12, 8};
    const auto ph = plot(analysis(ModelId::ActivatorGt0, high, on_curve(ModelId::ActivatorGt0, high, kSubstrate)), P::Hill);
    CHECK(!ph.xIntercept.has_value());
    CHECK(finite_max(ph.cy) < 0.0);
}

TEST_CASE(plots_log_log, "plots: log-log plot and its X-intercept where v = 1") {
    const auto a = analysis(ModelId::MichaelisMenten, {2.5, 10}, on_curve(ModelId::MichaelisMenten, {2.5, 10}, kSubstrate));
    const auto pd = plot(a, P::LogLog);
    CHECK(std::isnan(pd.px[0]) && std::isnan(pd.py[0]));
    for (size_t i = 1; i < kSubstrate.size(); ++i) {
        CHECK_NEAR(pd.px[i], std::log10(kSubstrate[i]), 1e-15);
        CHECK_NEAR(pd.py[i], std::log10(10 * kSubstrate[i] / (2.5 + kSubstrate[i])), 1e-14);
    }
    // v = 1 at [S] = Km/(Vmax - 1). python: np.log10(2.5/9)
    CHECK(pd.xIntercept && std::fabs(*pd.xIntercept - -0.5563025007672873) < 1e-12);
    const auto curve = [](double X) {
        const double s = std::pow(10.0, X);
        return std::log10(10 * s / (2.5 + s));
    };
    CHECK(check_curve(pd, curve, 1e-12, "log-log") == 400);
    CHECK_NEAR(finite_min(pd.cx), -0.5563025007672873, 1e-12);

    // Linear regression: Y = 1 at X = -1 (no logarithm), or at X = 2.
    const auto l1 = plot(analysis(ModelId::Linear, {2, 1}, on_curve(ModelId::Linear, {2, 1}, {1, 2, 3})), P::LogLog);
    CHECK(!l1.xIntercept.has_value());
    const auto l2 = plot(analysis(ModelId::Linear, {-1, 1}, on_curve(ModelId::Linear, {-1, 1}, {1, 2, 3, 4})), P::LogLog);
    CHECK(l2.xIntercept && std::fabs(*l2.xIntercept - std::log10(2.0)) < 1e-15);
    CHECK(l2.px[0] == 0.0 && std::isnan(l2.py[0]));  // Y = 0 at X = 1
    // With Vmax = 1, v reaches 1 only as [S] -> infinity: no intercept.
    const auto m1 = plot(analysis(ModelId::MichaelisMenten, {2.5, 1}, on_curve(ModelId::MichaelisMenten, {2.5, 1}, kSubstrate)),
                         P::LogLog);
    CHECK(!m1.xIntercept.has_value());
    CHECK_NEAR(finite_max(m1.cy), std::log10(8.0 / 10.5), 1e-12);
}

// v/Vmax = 1/(1 + 10^(-h X)) with X = log10([S]/Km).
TEST_CASE(plots_saturation, "plots: saturation plot is v/Vmax against log10([S]/Km)") {
    const Vec hp = {3, 1.8, 12};
    const auto pd = plot(analysis(ModelId::HillGt0, hp, on_curve(ModelId::HillGt0, hp, kSubstrate)), P::Saturation);
    CHECK(pd.yLabel == "v/Vmax");
    CHECK(std::isnan(pd.px[0]));
    CHECK_NEAR(pd.px[3], std::log10(2.0 / 3.0), 1e-15);
    CHECK_NEAR(pd.py[3], 1.0 / (1.0 + std::pow(1.5, 1.8)), 1e-14);
    CHECK(!pd.xIntercept.has_value());
    CHECK(check_curve(pd, [](double X) { return 1.0 / (1.0 + std::pow(10.0, -1.8 * X)); }, 1e-12, "saturation") ==
          400);
    // Extended to the Y axis: X = 0 is [S] = Km, half saturation.
    CHECK_NEAR(pd.cx.front(), std::log10(0.5 / 3.0), 1e-12);
    CHECK(pd.cx.front() < 0.0 && finite_max(pd.cx) > 0.0);

    // Inhibition: v/v0 = 1/(1 + 10^X) for linear inhibition.
    const auto inh = plot(analysis(ModelId::InhibitionLinear, {4, 20}, on_curve(ModelId::InhibitionLinear, {4, 20}, kSubstrate)),
                          P::Saturation);
    CHECK(inh.yLabel == "v/v0");
    CHECK_NEAR(inh.py[0], 1.0, 1e-15);
    CHECK(!inh.xIntercept.has_value());
    CHECK(check_curve(inh, [](double X) { return 1.0 / (1.0 + std::pow(10.0, X)); }, 1e-12, "saturation inh") == 400);

    // Km <= 0 or Vmax <= 0: nothing can be plotted.
    const auto k0 = plot(analysis(ModelId::MichaelisMenten, {0, 10}, {{1, 10}, {2, 10}}), P::Saturation);
    CHECK(std::isnan(k0.px[0]) && std::isnan(k0.px[1]) && k0.cx.empty());
    const auto v0 = plot(analysis(ModelId::MichaelisMenten, {2.5, -10}, {{1, -2}, {2, -3}}), P::Saturation);
    CHECK(std::isnan(v0.py[0]) && std::isnan(v0.py[1]) && v0.cx.empty());
}

// v0/v = 1 + [I]/I50 for linear inhibition.
TEST_CASE(plots_job, "plots: Job plot of linear inhibition is a straight line") {
    auto data = on_curve(ModelId::InhibitionLinear, {4, 20}, {0, 1, 2, 4, 8});
    data.push_back({16, 0.0});
    data.push_back({32, -1.0});
    const auto pd = plot(analysis(ModelId::InhibitionLinear, {4, 20}, data), P::Job);
    CHECK(pd.xLabel == "[I]" && pd.yLabel == "v0/v");
    for (size_t i = 0; i < 5; ++i) {
        CHECK(pd.px[i] == data[i].x);
        CHECK_NEAR(pd.py[i], 1.0 + data[i].x / 4.0, 1e-14);
    }
    CHECK(std::isnan(pd.py[5]) && std::isnan(pd.py[6]));  // v <= 0
    CHECK(!pd.xIntercept.has_value());
    CHECK(check_curve(pd, [](double X) { return 1.0 + X / 4.0; }, 1e-12, "Job") == 400);
    // Drawn from the Y axis to the last point with v > 0.
    CHECK(pd.cx.front() == 0.0);
    CHECK_NEAR(pd.cx.back(), 8.0, 1e-12);

    // Non-linear K50 inhibition: v0/v = 20 / (20 - 17 x^1.5 / (x^1.5 + 8)).
    const Vec k50 = {4, 1.5, 3, 20};
    const auto pk = plot(analysis(ModelId::InhibitionK50, k50, on_curve(ModelId::InhibitionK50, k50, kSubstrate)), P::Job);
    const auto job = [](double X) {
        const double z = std::pow(X, 1.5);
        return 20.0 / (20.0 - 17.0 * z / (z + 8.0));
    };
    CHECK(check_curve(pk, job, 1e-12, "Job K50") == 400);
}

// log10(v) = log10(A) - Ea/(ln10 R) * (1/T).
TEST_CASE(plots_arrhenius, "plots: Arrhenius plot is a straight line in 1/T") {
    const Vec p = {9, 12};
    auto data = on_curve(ModelId::Arrhenius, p, {5, 10, 15, 20, 25, 30, 35, 40});
    data.insert(data.begin(), Point{-300.0, 1.0});  // below absolute zero
    const auto a = analysis(ModelId::Arrhenius, p, data);
    const auto line = [](double X) { return 9.0 - 2622.5455990133128 * X; };  // python: -12/(ln10 R)

    const auto pd = plot(a, P::Arrhenius);
    CHECK(pd.xLabel == "1/T (1/K)" && pd.yLabel == "log10(v)");
    CHECK(std::isnan(pd.px[0]) && pd.py[0] == 1.0);
    for (size_t i = 1; i < data.size(); ++i) {
        CHECK_NEAR(pd.px[i], 1.0 / (data[i].x + 273.15), 1e-18);
        CHECK(pd.py[i] == data[i].y);
    }
    CHECK(!pd.xIntercept.has_value());
    CHECK(check_curve(pd, line, 1e-10, "Arrhenius") == 400);
    // No origin, no extension: the data range plus 5%.
    // python: X = 1/(T + 273.15); Y = 9 - 12/(ln10 R (T + 273.15)); X.min() - 0.05*(X.max() - X.min()), ...
    CHECK_NEAR(pd.xmin, 0.003173266583754936, 1e-15);
    CHECK_NEAR(pd.xmax, 0.0036152736874979358, 1e-15);
    CHECK_NEAR(pd.ymin, -0.481220098376341, 1e-12);
    CHECK_NEAR(pd.ymax, 0.6779636862774818, 1e-12);

    // Extended to 1/T = 0 (T infinite: undefined). The curve rises towards
    // log10(A) = 9, but the axes only follow it to 2 data spans above.
    const auto ext = make_plot(a, P::Arrhenius, opts(false, true, true));
    CHECK(std::isnan(ext.cx.front()));
    CHECK(!ext.xIntercept.has_value());
    CHECK(check_curve(ext, line, 1e-10, "Arrhenius extended") == 399);
    CHECK(finite_max(ext.cy) > 8.9);
    const double ylo = data[1].y, yhi = data.back().y, xhi = pd.px[1];
    const double band = yhi + 2.0 * (yhi - ylo);       // highest Y that widens the axes
    const double xb = (9.0 - band) / 2622.5455990133128;  // X where the line reaches it
    const double step = xhi / 399;                        // sample spacing in X
    CHECK(ext.ymax <= band + 0.05 * (band - ylo) + 1e-12);
    CHECK(ext.ymax > band - 2622.6 * step + 0.05 * (band - 2622.6 * step - ylo));
    CHECK(ext.xmin > xb - 0.05 * (xhi - xb) - 1e-12);
    CHECK(ext.xmin < xb + step);
}

// ---------------------------------------------------------------------------
// Normal plot, X-intercepts and axis extents

TEST_CASE(plots_normal_intercepts, "plots: normal plot is extended to its X-intercept") {
    const auto mm = plot(analysis(ModelId::MichaelisMenten, {2.5, 10}, on_curve(ModelId::MichaelisMenten, {2.5, 10}, kSubstrate)),
                         P::Normal);
    CHECK(mm.xIntercept && *mm.xIntercept == 0.0);
    CHECK(check_curve(mm, [](double X) { return 10 * X / (2.5 + X); }, 1e-12, "normal MM") == 400);

    // Activator: v = 0 at [A] = -(v0 Ka^h / Vmax)^(1/h) through signed powers.
    // python: -((2) * 3**1.6 / 12) ** (1/1.6)
    const Vec act = {3, 1.6, 12, 2};
    const auto pa = plot(analysis(ModelId::ActivatorGt0, act, on_curve(ModelId::ActivatorGt0, act, kSubstrate)), P::Normal);
    CHECK(pa.xIntercept && std::fabs(*pa.xIntercept - -0.9789865455507593) < 1e-12);
    CHECK_NEAR(pa.cx.front(), -0.9789865455507593, 1e-12);
    CHECK_NEAR(pa.cy.front(), 0.0, 1e-12);

    // Inhibition and Arrhenius curves have no X-intercept.
    const auto pi = plot(analysis(ModelId::InhibitionLinear, {4, 20}, on_curve(ModelId::InhibitionLinear, {4, 20}, kSubstrate)),
                         P::Normal);
    CHECK(!pi.xIntercept.has_value());
    const auto pr = make_plot(analysis(ModelId::Arrhenius, {9, 12}, on_curve(ModelId::Arrhenius, {9, 12}, {10, 20, 30})),
                              P::Normal, opts(true, true, true));
    CHECK(!pr.xIntercept.has_value());

    // Linear regression Y = 1.5 + 2X on X = 1..5 (Y = 3.5..11.5): the
    // X-intercept is -0.75 and the extents follow by hand.
    const auto lin = analysis(ModelId::Linear, {1.5, 2}, on_curve(ModelId::Linear, {1.5, 2}, {1, 2, 3, 4, 5}));
    const auto full = make_plot(lin, P::Normal, opts(true, true, true));
    CHECK(full.xIntercept && *full.xIntercept == -0.75);
    CHECK(full.cx.front() == -0.75 && full.cy.front() == 0.0);
    CHECK(check_curve(full, [](double X) { return 1.5 + 2 * X; }, 1e-14, "linear") == 400);
    CHECK_NEAR(full.xmin, -0.75 - 0.05 * 5.75, 1e-12);
    CHECK_NEAR(full.xmax, 5.0 + 0.05 * 5.75, 1e-12);
    CHECK_NEAR(full.ymin, -0.05 * 11.5, 1e-12);
    CHECK_NEAR(full.ymax, 11.5 * 1.05, 1e-12);
    const auto bare = make_plot(lin, P::Normal, opts(false, false, false));
    CHECK(bare.xIntercept && *bare.xIntercept == -0.75);  // reported even when not drawn
    CHECK(bare.cx.front() == 1.0 && bare.cx.back() == 5.0);
    CHECK_NEAR(bare.xmin, 0.8, 1e-12);
    CHECK_NEAR(bare.xmax, 5.2, 1e-12);
    CHECK_NEAR(bare.ymin, 3.1, 1e-12);
    CHECK_NEAR(bare.ymax, 11.9, 1e-12);
    const auto origin = make_plot(lin, P::Normal, opts(true, false, false));
    CHECK_NEAR(origin.xmin, -0.25, 1e-12);
    CHECK_NEAR(origin.xmax, 5.25, 1e-12);
    CHECK_NEAR(origin.ymin, -0.575, 1e-12);
    CHECK_NEAR(origin.ymax, 12.075, 1e-12);
}

TEST_CASE(plots_distant_intercept, "plots: a curve running far from the data does not stretch the axes") {
    // Y = 100 + X on X = 0..1: the curve is drawn to X = -100, but the axes
    // follow it only while Y stays within 2 data spans (Y > 98).
    const auto a = analysis(ModelId::Linear, {100, 1}, on_curve(ModelId::Linear, {100, 1}, {0, 0.5, 1}));
    const auto pd = make_plot(a, P::Normal, opts(false, true, true));
    CHECK(pd.xIntercept && *pd.xIntercept == -100.0);
    CHECK(pd.cx.front() == -100.0 && pd.cy.front() == 0.0);
    const double cxlo = -100.0 + 101.0 * 388 / 399;  // first sample with Y > 98
    CHECK_NEAR(pd.xmin, cxlo - 0.05 * (1.0 - cxlo), 1e-12);
    CHECK_NEAR(pd.ymin, (100.0 + cxlo) - 0.05 * (101.0 - (100.0 + cxlo)), 1e-12);
    // Y = -X on X = 10..11, extended to the Y axis: above Y = -8 the curve
    // no longer widens the axes.
    const auto b = analysis(ModelId::Linear, {0, -1}, on_curve(ModelId::Linear, {0, -1}, {10, 11}));
    const auto pb = make_plot(b, P::Normal, opts(false, true, false));
    CHECK(pb.cx.front() == 0.0 && pb.cy.front() == 0.0);
    const double cyhi = -11.0 * 291 / 399;  // last sample with Y < -8
    CHECK_NEAR(pb.ymax, cyhi + 0.05 * (cyhi + 11.0), 1e-12);
    CHECK_NEAR(pb.xmin, -cyhi - 0.05 * (11.0 + cyhi), 1e-12);
    // A curve nowhere near the data (Y = X against points at Y = 100) is
    // drawn but leaves the axes to the data: X 1..2, Y 100..100.5, plus 5%.
    const auto far = analysis(ModelId::Linear, {0, 1}, {{1, 100}, {2, 100.5}});
    const auto pf = make_plot(far, P::Normal, opts(false, false, false));
    CHECK(check_curve(pf, [](double X) { return X; }, 1e-15, "far curve") == 400);
    CHECK_NEAR(pf.xmin, 0.95, 1e-12);
    CHECK_NEAR(pf.xmax, 2.05, 1e-12);
    CHECK_NEAR(pf.ymin, 99.975, 1e-12);
    CHECK_NEAR(pf.ymax, 100.525, 1e-12);
}

TEST_CASE(plots_residual, "plots: residual plot shows residuals against fitted values") {
    Analysis a = analysis(ModelId::MichaelisMenten, {2.5, 10}, {{1, 0}, {2, 0}, {3, 0}});
    a.yhat = {1, 2, 4};
    a.resid = {-0.5, 0.25, 0.5};
    const auto pd = plot(a, P::Residual);
    CHECK(pd.xLabel == "Fitted v" && pd.yLabel == "Residual (v - fitted)");
    CHECK(pd.px == a.yhat && pd.py == a.resid);
    CHECK(pd.labels == (std::vector<int>{1, 2, 3}));
    CHECK(pd.zeroLine);
    CHECK(pd.cx.empty() && pd.cy.empty() && !pd.xIntercept.has_value());
    CHECK_NEAR(pd.xmin, -0.2, 1e-15);  // origin to 4, plus 5%
    CHECK_NEAR(pd.xmax, 4.2, 1e-15);
    CHECK_NEAR(pd.ymin, -0.55, 1e-15);
    CHECK_NEAR(pd.ymax, 0.55, 1e-15);
    // One point, no origin: a zero span is widened by 20% of the value.
    Analysis one = analysis(ModelId::MichaelisMenten, {2.5, 10}, {{1, 0}});
    one.yhat = {5};
    one.resid = {2};
    const auto po = make_plot(one, P::Residual, opts(false, false, false));
    CHECK_NEAR(po.xmin, 4.95, 1e-15);
    CHECK_NEAR(po.xmax, 5.05, 1e-15);
    CHECK_NEAR(po.ymin, 1.98, 1e-15);
    CHECK_NEAR(po.ymax, 2.02, 1e-15);
}

TEST_CASE(plots_degenerate, "plots: plots without usable points or with odd constants") {
    // No data: unit axes, or a small square around the origin.
    const auto empty = analysis(ModelId::MichaelisMenten, {2.5, 10}, {});
    const auto e1 = make_plot(empty, P::Normal, opts(false, true, true));
    CHECK(e1.px.empty() && e1.cx.empty() && !e1.xIntercept.has_value());
    CHECK(e1.xmin == 0.0 && e1.xmax == 1.0 && e1.ymin == 0.0 && e1.ymax == 1.0);
    const auto e2 = make_plot(empty, P::Normal, opts(true, true, true));
    CHECK_NEAR(e2.xmin, -0.05, 1e-15);
    CHECK_NEAR(e2.xmax, 0.05, 1e-15);
    CHECK_NEAR(e2.ymin, -0.05, 1e-15);
    CHECK_NEAR(e2.ymax, 0.05, 1e-15);

    // Every velocity zero: no 1/v, so no curve and no intercept either.
    const auto zero = make_plot(analysis(ModelId::MichaelisMenten, {2.5, 10}, {{1, 0}, {2, 0}}), P::LineweaverBurk,
                                opts(true, true, true));
    CHECK(zero.px[0] == 1.0 && std::isnan(zero.py[0]));
    CHECK(zero.cx.empty() && !zero.xIntercept.has_value());

    // Km = 0: v = Vmax for any [S] > 0, a flat double-reciprocal line with
    // no X-intercept.
    const auto k0 = make_plot(analysis(ModelId::MichaelisMenten, {0, 10}, {{1, 10}, {2, 10}}), P::LineweaverBurk,
                              opts(true, true, true));
    CHECK(!k0.xIntercept.has_value());
    CHECK(check_curve(k0, [](double) { return 0.1; }, 1e-15, "Km = 0") == 399);

    // A tiny Km puts -1/Km at minus infinity: reported, but not drawn to.
    const double tiny = std::numeric_limits<double>::denorm_min();
    const auto kt = make_plot(analysis(ModelId::MichaelisMenten, {tiny, 10}, {{1, 10}, {2, 10}}), P::LineweaverBurk,
                              opts(true, true, true));
    CHECK(kt.xIntercept && std::isinf(*kt.xIntercept) && *kt.xIntercept < 0);
    CHECK(finite_min(kt.cx) > 0.0);
}

TEST_CASE(plots_unavailable, "plots: plot types outside an equation's menu") {
    // Linear regression has no Km or Vmax: the saturation transform uses 1.
    const auto lin = analysis(ModelId::Linear, {1.5, 2}, on_curve(ModelId::Linear, {1.5, 2}, {1, 10, 100}));
    const auto ls = make_plot(lin, P::Saturation, opts(true, true, true));
    CHECK(ls.xLabel == "log10(X/Km)");
    CHECK_NEAR(ls.px[1], 1.0, 1e-15);
    CHECK_NEAR(ls.py[1], 21.5, 1e-14);
    CHECK(!ls.xIntercept.has_value());
    // Job plot of Michaelis-Menten: Vmax/v = 1 + Km/[S], no X-intercept.
    const auto mm = analysis(ModelId::MichaelisMenten, {2.5, 10}, on_curve(ModelId::MichaelisMenten, {2.5, 10}, kSubstrate));
    const auto mj = make_plot(mm, P::Job, opts(true, false, true));
    CHECK(!mj.xIntercept.has_value());
    CHECK(check_curve(mj, [](double X) { return 1.0 + 2.5 / X; }, 1e-12, "Job MM") > 390);
    // Double-reciprocal plot of an inhibition curve: no X-intercept.
    const auto inh = analysis(ModelId::InhibitionLinear, {4, 20}, on_curve(ModelId::InhibitionLinear, {4, 20}, {1, 2, 4}));
    CHECK(!make_plot(inh, P::LineweaverBurk, opts(true, true, true)).xIntercept.has_value());
    // An unknown plot type transforms nothing.
    const auto un = make_plot(mm, static_cast<PlotType>(0), opts(true, true, true));
    CHECK(un.xLabel == "[S]" && un.yLabel == "v");
    CHECK(un.px.size() == kSubstrate.size() && std::isnan(un.px[2]) && std::isnan(un.py[2]));
    CHECK(un.cx.empty() && !un.xIntercept.has_value());
    CHECK_NEAR(un.xmax, 0.05, 1e-15);
}

// Every available plot of every equation, with noise-free data: the
// transformed points and every fitted-curve sample satisfy the equation,
// checked with transforms written from the plot definitions.
TEST_CASE(plots_sweep, "plots: every available plot of every equation lies on the fitted curve") {
    for (const auto& c : sweep_cases()) {
        const auto data = on_curve(c.m, c.p, c.x);
        const auto a = analysis(c.m, c.p, data);
        for (PlotType t : available_plots(c.m)) {
            char what[64];
            std::snprintf(what, sizeof what, "model %c plot %d", model_def(c.m).letter, static_cast<int>(t));
            const auto pd = plot(a, t);
            CHECK(pd.px.size() == data.size() && pd.py.size() == data.size() && pd.labels.size() == data.size());
            CHECK(pd.xmin < pd.xmax && pd.ymin < pd.ymax);
            if (t == P::Residual) {
                CHECK(pd.zeroLine && pd.cx.empty());
                for (size_t i = 0; i < data.size(); ++i) {
                    CHECK_NEAR(pd.px[i], ref_v(c.m, c.p, data[i].x), 1e-12 * (1 + std::fabs(data[i].y)));
                    CHECK_NEAR(pd.py[i], 0.0, 1e-12 * (1 + std::fabs(data[i].y)));
                }
                continue;
            }
            for (size_t i = 0; i < data.size(); ++i) {
                double X, Y;
                ref_transform(t, data[i].x, ref_v(c.m, c.p, data[i].x), c.K, c.V, X, Y);
                if (!std::isfinite(Y)) X = kNaN;  // a point needs both coordinates
                char buf[128];
                std::snprintf(buf, sizeof buf, "%s point %zu: (%g, %g) want (%g, %g)", what, i, pd.px[i], pd.py[i], X, Y);
                ::test::check(same(pd.py[i], Y) && (std::isnan(X) || same(pd.px[i], X)), "data transform", __FILE__,
                              __LINE__, buf);
            }
            int finite = 0;
            for (size_t k = 0; k < pd.cx.size(); ++k) {
                if (!std::isfinite(pd.cx[k])) continue;
                ++finite;
                if (t == P::EadieHofstee) {
                    // X is v and Y is v/x, so the model at x = X/Y must give X.
                    const double v = ref_v(c.m, c.p, pd.cx[k] / pd.cy[k]);
                    ::test::check(same(pd.cx[k], v, 1e-9), "EH curve sample", __FILE__, __LINE__, what);
                    continue;
                }
                const double x = ref_raw_x(t, pd.cx[k], c.K);
                double X, Y;
                ref_transform(t, x, ref_v(c.m, c.p, x), c.K, c.V, X, Y);
                char buf[160];
                std::snprintf(buf, sizeof buf, "%s sample %zu: (%.10g, %.12g) want Y %.12g", what, k, pd.cx[k], pd.cy[k], Y);
                ::test::check(same(pd.cy[k], Y, 1e-9), "curve sample", __FILE__, __LINE__, buf);
            }
            ::test::check(finite >= 300, "curve drawn", __FILE__, __LINE__, what);
        }
    }
}

TEST_CASE(plots_coordinates, "plots: a point in plot coordinates, as the data are drawn") {
    const auto a = analysis(ModelId::MichaelisMenten, {2.0, 10.0}, {{0.5, 2.1}, {1.0, 3.4}, {4.0, 6.5}, {8.0, 8.1}});
    auto same_or_nan = [](double u, double v) { return (std::isnan(u) && std::isnan(v)) || u == v; };
    for (const auto t : available_plots(ModelId::MichaelisMenten)) {
        const auto pd = plot(a, t);
        for (size_t i = 0; i < a.data.size(); ++i) {
            const auto [X, Y] = plot_coordinates(a, t, a.data[i].x, a.data[i].y);
            if (t == P::Residual) {
                CHECK(std::isnan(X) && std::isnan(Y));  // the residual plot's axes are not the data's
                continue;
            }
            CHECK(same_or_nan(X, pd.px[i]));
            CHECK(same_or_nan(Y, pd.py[i]));
        }
    }
    // A point off the data, on a double-reciprocal plot.
    const auto [X, Y] = plot_coordinates(a, P::LineweaverBurk, 4.0, 5.0);
    CHECK(X == 0.25 && Y == 0.2);
}
