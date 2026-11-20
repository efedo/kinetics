// Tests for core/guess: the automatic initial estimates. Noise-free data
// must give back the parameters they were made from; noisy data are checked
// against numpy versions of the same linearisations; fallbacks, fixed
// parameters and bounds are checked against the documented rules.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

#include "core/fit.hpp"
#include "core/guess.hpp"
#include "core/models.hpp"

#include "check.hpp"

using namespace kin;

namespace {

using Vec = std::vector<double>;
using Settings = std::vector<ParamSetting>;

std::vector<Point> make_data(ModelId m, const Vec& p, const Vec& xs) {
    std::vector<Point> pts;
    for (double x : xs) pts.push_back({x, evaluate(m, x, p)});
    return pts;
}

std::vector<Point> points(const Vec& xs, const Vec& ys) {
    std::vector<Point> pts;
    for (size_t i = 0; i < xs.size(); ++i) pts.push_back({xs[i], ys[i]});
    return pts;
}

Settings fixed(ModelId m, std::initializer_list<std::pair<size_t, double>> values) {
    auto s = default_param_settings(m);
    for (const auto& [j, v] : values) {
        s[j].fixed = true;
        s[j].value = v;
    }
    return s;
}

Vec guess(ModelId m, const std::vector<Point>& data, const Settings& s) { return automatic_guess(m, data, s); }
Vec guess(ModelId m, const std::vector<Point>& data) { return guess(m, data, default_param_settings(m)); }

void check_params(const Vec& got, const Vec& want, double rel, const char* what) {
    CHECK(got.size() == want.size());
    for (size_t j = 0; j < got.size() && j < want.size(); ++j) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%s p%zu: %.12g vs %.12g", what, j, got[j], want[j]);
        ::test::check(std::fabs(got[j] - want[j]) <= rel * std::fabs(want[j]) + 1e-300, "guess == reference",
                      __FILE__, __LINE__, buf);
    }
}

const Vec kConc = {0, 0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16, 24};

}  // namespace

// ---------------------------------------------------------------------------
// Michaelis-Menten and the h = 1 activator equation

// The v^4-weighted Lineweaver-Burk fit of v3.51 is exact for noise-free data
// because 1/v is exactly linear in 1/[S].
TEST_CASE(guess_mm_exact, "guess: Michaelis-Menten linearisation recovers noise-free parameters") {
    check_params(guess(ModelId::MichaelisMenten, make_data(ModelId::MichaelisMenten, {2.5, 10}, kConc)), {2.5, 10},
                 1e-9, "MM");
    // h = 1 activator: v0 is the smallest velocity (measured at [A] = 0),
    // and the same fit on v - v0 gives Ka and Vmax.
    const auto act = make_data(ModelId::ActivatorEq1, {3, 12, 2}, kConc);
    check_params(guess(ModelId::ActivatorEq1, act), {3, 12, 2}, 1e-9, "activator h=1");
}

TEST_CASE(guess_mm_numpy, "guess: Michaelis-Menten weighted fit matches numpy on noisy data") {
    // python:
    //   x = np.array([0.5,1,2,4,8,16.]); y = np.array([1.62,2.95,4.48,6.21,7.55,8.70])
    //   w = y**2; A = np.column_stack([np.ones_like(x), 1/x]) * w[:,None]
    //   a, c = np.linalg.lstsq(A, w/y, rcond=None)[0]; K = c/a; V = 1/a
    const auto data = points({0.5, 1, 2, 4, 8, 16}, {1.62, 2.95, 4.48, 6.21, 7.55, 8.70});
    check_params(guess(ModelId::MichaelisMenten, data), {2.4517978054168754, 9.977567958545336}, 1e-10, "MM noisy");
}

TEST_CASE(guess_mm_fixed_vmax, "guess: fixed Vmax gives Km where the data cross half of it") {
    const auto data = make_data(ModelId::MichaelisMenten, {2.5, 10}, {0.5, 1, 2, 3, 4, 8});
    // v(2) = 4.44 < 5 < v(3) = 5.45: Km is taken half-way between, 2.5.
    check_params(guess(ModelId::MichaelisMenten, data, fixed(ModelId::MichaelisMenten, {{1, 10.0}})), {2.5, 10}, 1e-15,
                 "Vmax fixed");
    // Already above half of Vmax at the first point: the first X.
    check_params(guess(ModelId::MichaelisMenten, data, fixed(ModelId::MichaelisMenten, {{1, 2.0}})), {0.5, 2}, 0.0,
                 "first point");
    // Never reaching half of Vmax: the last X.
    check_params(guess(ModelId::MichaelisMenten, data, fixed(ModelId::MichaelisMenten, {{1, 100.0}})), {8, 100}, 0.0,
                 "never crosses");
    // Activator h = 1: the crossing level is v0 + (Vmax - v0)/2 = 7, met
    // between v(2) = 6 and v(4) = 7.71.
    const auto act = make_data(ModelId::ActivatorEq1, {3, 12, 2}, {0, 1, 2, 4, 8});
    check_params(guess(ModelId::ActivatorEq1, act, fixed(ModelId::ActivatorEq1, {{1, 12.0}})), {3, 12, 2}, 1e-15,
                 "activator Vmax fixed");
    check_params(guess(ModelId::ActivatorEq1, act, fixed(ModelId::ActivatorEq1, {{1, 12.0}, {2, 2.0}})), {3, 12, 2},
                 1e-15, "activator Vmax and v0 fixed");
}

TEST_CASE(guess_mm_degenerate, "guess: degenerate Michaelis-Menten data fall back to typical values") {
    // Every point at [S] = 0: no linearisation, no positive X; Km = 1 and
    // Vmax = 1.05 * max(v).
    check_params(guess(ModelId::MichaelisMenten, points({0, 0}, {1, 2})), {1.0, 2.1}, 1e-15, "all at zero");
    // One point: the weighted sums give a zero determinant.
    check_params(guess(ModelId::MichaelisMenten, points({2}, {4})), {2.0, 4.2}, 1e-15, "single point");
    // All velocities zero: Km is the middle positive X (of 1, 2, 3, 4 the
    // third) and Vmax = 0 is clamped up to its lower bound 1e-6.
    check_params(guess(ModelId::MichaelisMenten, points({0, 1, 2, 3, 4}, {0, 0, 0, 0, 0})), {3.0, 1e-6}, 0.0,
                 "zero velocities");
    // v^7 overflows: the determinant is not finite. Vmax is clamped to 1e6.
    check_params(guess(ModelId::MichaelisMenten, points({1, 2}, {1e50, 1e50})), {2.0, 1e6}, 0.0, "overflow");
}

// ---------------------------------------------------------------------------
// Linear inhibition

TEST_CASE(guess_linear_inhibition, "guess: linear inhibition I50 where v falls to v0/2") {
    // v = 20*4/(4 + [I]): 11.43 at [I] = 3 and 8.89 at [I] = 5, so I50 = 4.
    const auto data = make_data(ModelId::InhibitionLinear, {4, 20}, {0, 1, 2, 3, 5, 8});
    check_params(guess(ModelId::InhibitionLinear, data), {4, 20}, 1e-15, "I50");
    const auto noZero = make_data(ModelId::InhibitionLinear, {4, 20}, {1, 2, 3, 5, 8});
    // v0 = 100: already below 50 at the first point.
    check_params(guess(ModelId::InhibitionLinear, noZero, fixed(ModelId::InhibitionLinear, {{1, 100.0}})), {1, 100},
                 0.0, "first point");
    // v0 = 10: never below 5 (v(8) = 6.67), so the last X.
    check_params(guess(ModelId::InhibitionLinear, noZero, fixed(ModelId::InhibitionLinear, {{1, 10.0}})), {8, 10}, 0.0,
                 "never crosses");
}

// ---------------------------------------------------------------------------
// Hill-type equations: log10((v - bottom)/(top - v)) against log10(x)

TEST_CASE(guess_hill_exact, "guess: Hill-type linearisation recovers parameters when the plateaus are known") {
    using M = ModelId;
    for (M m : {M::HillGt0, M::HillGe1}) {
        const auto data = make_data(m, {3, 1.8, 12}, kConc);
        check_params(guess(m, data, fixed(m, {{2, 12.0}})), {3, 1.8, 12}, 1e-9, "Hill, Vmax fixed");
        check_params(guess(m, data, fixed(m, {{1, 1.8}, {2, 12.0}})), {3, 1.8, 12}, 1e-9, "Hill, h and Vmax fixed");
        // A free Vmax starts 10% above the largest velocity.
        double vmax = 0;
        for (const auto& pt : data) vmax = std::max(vmax, pt.y);
        CHECK_REL(guess(m, data)[2], 1.1 * vmax, 1e-15);
    }
    for (M m : {M::ActivatorGt0, M::ActivatorGe1}) {
        // v0 is the velocity at [A] = 0 (the smallest).
        const auto data = make_data(m, {3, 1.6, 12, 2}, kConc);
        check_params(guess(m, data, fixed(m, {{2, 12.0}})), {3, 1.6, 12, 2}, 1e-9, "activator, Vmax fixed");
    }
    // Inhibition: v0 is the largest velocity, top is vmin.
    const auto k50 = make_data(M::InhibitionK50, {4, 1.5, 3, 20}, kConc);
    check_params(guess(M::InhibitionK50, k50, fixed(M::InhibitionK50, {{2, 3.0}})), {4, 1.5, 3, 20}, 1e-9, "K50");
    // I50: the fit gives I50*(1 - 2 vmin/v0)^(1/h), which is corrected.
    const auto i50 = make_data(M::InhibitionI50, {4, 1.5, 3, 20}, kConc);
    check_params(guess(M::InhibitionI50, i50, fixed(M::InhibitionI50, {{2, 3.0}})), {4, 1.5, 3, 20}, 1e-9, "I50");
}

TEST_CASE(guess_hill_numpy, "guess: Hill and K50 estimates match numpy on noisy data") {
    using M = ModelId;
    // python:
    //   top = 1.1*y.max(); r = y/(top - y)
    //   h, b = np.polyfit(np.log10(x), np.log10(r), 1); K = 10**(-b/h)
    const auto hill = points({0.5, 1, 2, 3, 4, 6, 8, 12},
                             {0.4398, 1.2462, 3.6436, 5.9400, 7.5891, 9.3209, 10.6232, 10.9930});
    check_params(guess(M::HillGt0, hill), {3.0822376372773124, 1.8328483948130387, 12.092300000000002}, 1e-10,
                 "Hill noisy");
    // K50 with v0 = max(v) = 20.3 (at [I] = 0.5, whose ratio is then 0 and
    // is skipped) and vmin = 0.99*min(v); [I] = 0 is skipped too.
    // python:
    //   v0 = y.max(); vmin = 0.99*y.min(); m = x > 0
    //   r = (y[m] - v0)/(vmin - y[m]); k = r > 0
    //   h, b = np.polyfit(np.log10(x[m][k]), np.log10(r[k]), 1); K = 10**(-b/h)
    const auto k50 = points({0, 0.5, 1, 2, 4, 8, 16, 32}, {20.0, 20.3, 18.6, 15.2, 11.4, 7.1, 4.9, 3.6});
    check_params(guess(M::InhibitionK50, k50), {3.2965492840910158, 2.1679943292125508, 3.564, 20.3}, 1e-10,
                 "K50 noisy");
}

TEST_CASE(guess_i50_correction, "guess: the I50 correction applies only when vmin < v0/2") {
    // python:
    //   rr = (y - 20)/(vmin - y); h, b = np.polyfit(np.log10(x), np.log10(rr), 1)
    //   K = 10**(-b/h); z5 = 1 - 2*vmin/20; K /= z5**(1/h) if z5 > 0
    const auto data = points({1, 2, 4, 8}, {19, 17, 15, 13.5});
    check_params(guess(ModelId::InhibitionI50, data, fixed(ModelId::InhibitionI50, {{2, 4.0}, {3, 20.0}})),
                 {15.025443385469755, 1.105618209458808, 4, 20}, 1e-10, "z5 = 0.6");
    check_params(guess(ModelId::InhibitionI50, data, fixed(ModelId::InhibitionI50, {{2, 12.0}, {3, 20.0}})),
                 {3.045105787602409, 1.6242427606765033, 12, 20}, 1e-10, "z5 = -0.2");
}

TEST_CASE(guess_h_bounds, "guess: h > 0 and h >= 1 variants clamp the Hill coefficient") {
    using M = ModelId;
    // Data with h = 0.5: the h > 0 equation keeps it, h >= 1 is clamped to 1;
    // Km comes from the same regression in both.
    const auto hill = make_data(M::HillGt0, {3, 0.5, 12}, kConc);
    check_params(guess(M::HillGt0, hill, fixed(M::HillGt0, {{2, 12.0}})), {3, 0.5, 12}, 1e-9, "Hill h>0");
    check_params(guess(M::HillGe1, hill, fixed(M::HillGe1, {{2, 12.0}})), {3, 1.0, 12}, 1e-9, "Hill h>=1");
    const auto act = make_data(M::ActivatorGt0, {3, 0.6, 12, 2}, kConc);
    check_params(guess(M::ActivatorGt0, act, fixed(M::ActivatorGt0, {{2, 12.0}})), {3, 0.6, 12, 2}, 1e-9,
                 "activator h>0");
    check_params(guess(M::ActivatorGe1, act, fixed(M::ActivatorGe1, {{2, 12.0}})), {3, 1.0, 12, 2}, 1e-9,
                 "activator h>=1");
}

TEST_CASE(guess_zero_baseline, "guess: a zero baseline is replaced by 1% of the largest velocity") {
    // Activation with v0 = 0: v0 cannot start at zero.
    const auto act = make_data(ModelId::HillGt0, {3, 1.6, 12}, {0, 1, 2, 4, 8});
    double vmax = 0;
    for (const auto& pt : act) vmax = std::max(vmax, pt.y);
    CHECK_REL(guess(ModelId::ActivatorGt0, act)[3], vmax / 100.0, 1e-15);
    // Inhibition reaching zero: vmin starts at 1% of v0.
    const auto inh = points({0, 1, 2, 4}, {20, 12, 5, 0});
    CHECK_REL(guess(ModelId::InhibitionK50, inh)[2], 0.2, 1e-15);
    CHECK_REL(guess(ModelId::InhibitionI50, inh)[2], 0.2, 1e-15);
    CHECK(guess(ModelId::InhibitionK50, inh)[3] == 20.0);
    // Otherwise vmin is just below the smallest velocity.
    CHECK_REL(guess(ModelId::InhibitionK50, points({0, 1, 2}, {20, 12, 5}))[2], 4.95, 1e-15);
}

TEST_CASE(guess_hill_fallbacks, "guess: Hill-type fallbacks when the regression cannot be used") {
    using M = ModelId;
    // Two points: no regression, so h = 1 and Km is the middle positive X.
    check_params(guess(M::HillGt0, points({1, 2}, {2, 4})), {2, 1, 4.4}, 1e-15, "two points");
    // Ratio v/(Vmax - v) = 1 everywhere: zero slope, same fallback.
    check_params(guess(M::HillGt0, points({1, 2, 4, 8}, {1, 1, 1, 1}), fixed(M::HillGt0, {{2, 2.0}})), {4, 1, 2}, 0.0,
                 "zero slope");
    // log10 ratio = +1 + 0.001 log10 x: Km = 10^-1000 underflows to 0, and
    // log10 ratio = -1 + 0.001 log10 x: Km = 10^+1000 overflows. Either way
    // Km is the middle X and the tiny h is clamped to the lower bound.
    for (double c : {1.0, -1.0}) {
        Vec ys;
        for (double lx : {0.0, 1.0, 2.0}) {
            const double r = std::pow(10.0, c + 0.001 * lx);
            ys.push_back(r / (1.0 + r));  // v with Vmax = 1
        }
        const auto data = points({1, 10, 100}, ys);
        check_params(guess(M::HillGt0, data, fixed(M::HillGt0, {{2, 1.0}})), {10, 0.01, 1}, 0.0, "Km out of range");
        check_params(guess(M::HillGe1, data, fixed(M::HillGe1, {{2, 1.0}})), {10, 1.0, 1}, 0.0, "Km out of range");
    }
    // Points that cannot be linearised are skipped: [S] = 0, v equal to the
    // fixed Vmax, v above it (negative ratio) and an undefined v. The other
    // points are exact, so Km and h are recovered.
    auto data = make_data(M::HillGt0, {3, 1.8, 12}, {0, 1, 2, 4, 6, 8, 10, 20});
    data[3].y = 12.0;
    data[5].y = 13.0;
    data[6].y = std::numeric_limits<double>::quiet_NaN();
    check_params(guess(M::HillGt0, data, fixed(M::HillGt0, {{2, 12.0}})), {3, 1.8, 12}, 1e-9, "skipped points");
    // A point at [S] = +infinity (sorted last; analyse() would drop it)
    // makes the regression sums undefined, so the slope is NaN: h = 1 and Km
    // is the middle positive X (of 1, 2, 4, inf the third).
    auto withInf = make_data(M::HillGt0, {3, 1.8, 12}, {1, 2, 4});
    withInf.push_back({std::numeric_limits<double>::infinity(), 11.0});
    check_params(guess(M::HillGt0, withInf, fixed(M::HillGt0, {{2, 12.0}})), {4, 1, 12}, 0.0, "infinite X");
}

TEST_CASE(guess_non_finite, "guess: a non-finite estimate is replaced by 1") {
    // 1.1 * DBL_MAX overflows, so Vmax becomes 1. Every ratio v/(inf - v)
    // is 0, so h = 1 and Km is the middle positive X.
    const double big = std::numeric_limits<double>::max();
    check_params(guess(ModelId::HillGt0, points({1, 2}, {1, big})), {2, 1, 1}, 0.0, "overflow");
}

// ---------------------------------------------------------------------------
// Arrhenius and linear regression

TEST_CASE(guess_arrhenius, "guess: Arrhenius estimates from the straight line in 1/T") {
    const Vec temps = {5, 10, 15, 20, 25, 30, 35, 40};
    auto data = make_data(ModelId::Arrhenius, {9, 12}, temps);
    check_params(guess(ModelId::Arrhenius, data), {9, 12}, 1e-8, "exact");
    // A temperature below absolute zero is left out of the line.
    data.insert(data.begin(), Point{-300.0, 5.0});
    check_params(guess(ModelId::Arrhenius, data), {9, 12}, 1e-8, "below absolute zero");
    // Too few points for a line: the defaults log10(A) = 10, Ea = 20.
    check_params(guess(ModelId::Arrhenius, points({10, 20}, {1, 2})), {10, 20}, 0.0, "two points");
    // Rates falling with temperature imply Ea < 0, clamped to its bound.
    // python: s, i = np.polyfit(1/(T + 273.15), ly, 1)  -> i, -s*ln10*R = -3.93
    check_params(guess(ModelId::Arrhenius, points({10, 20, 30}, {3, 2.9, 2.8})), {-0.029226733142303413, 1e-6}, 1e-9,
                 "negative Ea");
}

TEST_CASE(guess_linear_model, "guess: linear regression needs no estimates") {
    // The regression is solved directly, so free parameters start at zero
    // and a fixed intercept keeps its value.
    const auto data = points({1, 2, 3}, {2, 4, 6});
    check_params(guess(ModelId::Linear, data), {0, 0}, 0.0, "linear");
    check_params(guess(ModelId::Linear, data, fixed(ModelId::Linear, {{0, -3.0}})), {-3, 0}, 0.0, "fixed intercept");
}

// ---------------------------------------------------------------------------
// Fixed parameters and bounds

TEST_CASE(guess_bounds, "guess: estimates are clamped to the bounds, fixed values are not") {
    const auto data = make_data(ModelId::MichaelisMenten, {2.5, 10}, kConc);
    auto s = default_param_settings(ModelId::MichaelisMenten);
    s[0].min = 5;
    s[0].max = 6;
    s[1].min = 1;
    s[1].max = 8;
    check_params(guess(ModelId::MichaelisMenten, data, s), {5, 8}, 0.0, "clamped");
    // A fixed Km outside its bounds is kept; the free Vmax from the
    // weighted fit (10) is still clamped.
    s[0].fixed = true;
    s[0].value = 100;
    check_params(guess(ModelId::MichaelisMenten, data, s), {100, 8}, 0.0, "fixed kept");
    s[1].max = 50;
    check_params(guess(ModelId::MichaelisMenten, data, s), {100, 10}, 1e-9, "fixed Km ignored by the weighted fit");
}

// The header promises a result clamped to the bounds. With no data,
// automatic_guess() used to return 0 for every free parameter, below every
// default lower bound (found by this test, now fixed). analyse() and the app
// never call it with empty data.
TEST_CASE(guess_empty_within_bounds, "guess: empty data still gives estimates within the bounds") {
    const auto s = fixed(ModelId::HillGe1, {{2, 7.0}});
    const auto g = guess(ModelId::HillGe1, {}, s);
    CHECK(g.size() == 3);
    CHECK(g.size() == 3 && g[2] == 7.0);  // fixed value kept
    for (size_t j = 0; j < g.size(); ++j)
        if (!s[j].fixed) {
            char buf[96];
            std::snprintf(buf, sizeof buf, "p%zu = %g, bounds [%g, %g]", j, g[j], s[j].min, s[j].max);
            ::test::check(g[j] >= s[j].min && g[j] <= s[j].max, "within bounds", __FILE__, __LINE__, buf);
        }
}
