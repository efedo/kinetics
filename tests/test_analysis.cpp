// Tests for core/analysis: validation, fitting through analyse(), the
// statistics stored in an Analysis, and the interpretation text.
//
// Reference values come from independent calculations in Python 3.12 with
// numpy (a separate Gauss-Newton fit with a numerical Jacobian, numpy least
// squares for straight lines, and Student t / F tail probabilities from
// Simpson integration of the densities). The snippets are in the comments.
// Shared Python helpers used below:
//
//   import numpy as np; from math import lgamma, exp, sqrt, pi
//   def simpson(f, a, b, n=200000):
//       x = np.linspace(a, b, n + 1); y = f(x); h = (b - a) / n
//       return h / 3 * (y[0] + y[-1] + 4 * y[1:-1:2].sum() + 2 * y[2:-1:2].sum())
//   def t_p(t, df):   # two-tailed P(|T| >= t), substituting u = t / s^2
//       c = exp(lgamma((df + 1) / 2) - lgamma(df / 2)) / sqrt(df * pi)
//       f = lambda u: c * (1 + u * u / df) ** (-(df + 1) / 2)
//       def g(s):
//           out = np.zeros_like(s); m = s > 0
//           out[m] = f(t / s[m] ** 2) * 2 * t / s[m] ** 3; return out
//       return 2 * simpson(g, 0.0, 1.0)
//   def f_p(F, d1, d2):   # upper tail of F(d1, d2), substituting x = F / s^2
//       c = exp(lgamma((d1 + d2) / 2) - lgamma(d1 / 2) - lgamma(d2 / 2)) * (d1 / d2) ** (d1 / 2)
//       dens = lambda x: c * x ** (d1 / 2 - 1) * (1 + d1 * x / d2) ** (-(d1 + d2) / 2)
//       def g(s):
//           out = np.zeros_like(s); m = s > 0
//           out[m] = dens(F / s[m] ** 2) * 2 * F / s[m] ** 3; return out
//       return simpson(g, 0.0, 1.0)
//   def jac(f, x, p, h=1e-6):   # central differences
//       J = np.zeros((len(x), len(p)))
//       for j in range(len(p)):
//           dp = h * max(abs(p[j]), 1e-3); p1 = p.copy(); p2 = p.copy(); p1[j] += dp; p2[j] -= dp
//           J[:, j] = (f(x, p1) - f(x, p2)) / (2 * dp)
//       return J
//   def gn(f, x, y, p):   # Gauss-Newton with step halving; SDs from s^2 (J'J)^-1
//       p = np.array(p, float)
//       for _ in range(200):
//           r = y - f(x, p); d = np.linalg.lstsq(jac(f, x, p), r, rcond=None)[0]; lam = 1.0
//           while lam > 1e-12 and ((y - f(x, p + lam * d)) ** 2).sum() > (r ** 2).sum(): lam /= 2
//           p = p + lam * d
//           if np.max(np.abs(lam * d) / np.abs(p)) < 1e-14: break
//       r = y - f(x, p); J = jac(f, x, p); sse = (r ** 2).sum(); dof = len(x) - len(p)
//       return p, np.sqrt(np.diag(sse / dof * np.linalg.inv(J.T @ J))), sse, dof, r
//
// Sanity checks of the Python helpers: t_p(2.228139, 10) = 0.0500000,
// t_p(12.7062, 1) = 0.0500000, f_p(4.9646, 1, 10) = 0.0500001.

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "core/analysis.hpp"
#include "core/fit.hpp"
#include "core/models.hpp"
#include "core/stats.hpp"

#include "check.hpp"
#include "helpers.hpp"

namespace {

using namespace kin;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

bool contains(const std::vector<std::string>& lines, const std::string& s) {
    for (const auto& l : lines)
        if (l == s) return true;
    return false;
}

// Prints the lines that differ, so that a failure shows what was produced.
void check_lines(const std::vector<std::string>& got, const std::vector<std::string>& want, const char* file,
                 int line) {
    ::test::check(got.size() == want.size(), "same number of lines", file, line,
                  std::to_string(got.size()) + " vs " + std::to_string(want.size()));
    for (size_t i = 0; i < got.size() && i < want.size(); ++i)
        ::test::check(got[i] == want[i], "line matches", file, line, "\n  got:  " + got[i] + "\n  want: " + want[i]);
}
#define CHECK_LINES(got, ...) check_lines((got), std::vector<std::string>__VA_ARGS__, __FILE__, __LINE__)

std::string error_of(const AnalysisRequest& req) {
    const auto out = analyse(req);
    return out.analysis ? std::string("(no error)") : out.error;
}

AnalysisRequest manual(ModelId m, std::vector<Point> data, std::vector<double> start, Method method) {
    auto req = ::test::request(m, std::move(data), method);
    req.guess = GuessMode::Manual;
    for (size_t j = 0; j < start.size(); ++j) req.params[j].value = start[j];
    return req;
}

// examples/michaelis_menten.csv
const std::vector<Point> kMmExample = {{0.5, 1.6583}, {1, 2.7949},  {1.5, 3.5981}, {2, 4.4805},  {3, 5.5706},
                                       {4, 5.8148},   {6, 6.8661},  {8, 7.6587},   {12, 8.4348}, {16, 8.8326}};

}  // namespace

// ---------------------------------------------------------------------------
// Helpers: formatting, cleaning, counting, descriptions

TEST_CASE(analysis_fmt_g, "analysis: fmt_g formats like %g, with n/a and inf") {
    CHECK(fmt_g(kNaN) == "n/a");
    CHECK(fmt_g(kInf) == "inf");
    CHECK(fmt_g(-kInf) == "-inf");
    CHECK(fmt_g(2.5) == "2.5");
    CHECK(fmt_g(1234567.0) == "1.23457e+06");
    CHECK(fmt_g(0.0001234567) == "0.000123457");
    CHECK(fmt_g(2.0 / 3.0, 4) == "0.6667");
    CHECK(fmt_g(-12.26, 3) == "-12.3");
    CHECK(fmt_g(0.0) == "0");
}

TEST_CASE(analysis_clean_data, "analysis: clean_data drops non-finite rows and sorts by x, then y") {
    const std::vector<Point> raw = {{3, 1},    {kNaN, 2}, {1, 5},     {1, 4},   {2, kInf},
                                    {-kInf, 0}, {3, 0.5}, {0, kNaN}, {-2, 7}, {1, -1}};
    const auto c = clean_data(raw);
    const std::vector<Point> want = {{-2, 7}, {1, -1}, {1, 4}, {1, 5}, {3, 0.5}, {3, 1}};
    CHECK(c.size() == want.size());
    for (size_t i = 0; i < c.size() && i < want.size(); ++i) CHECK(c[i].x == want[i].x && c[i].y == want[i].y);
    CHECK(clean_data({}).empty());
    CHECK(clean_data({{kNaN, kNaN}}).empty());
}

TEST_CASE(analysis_free_params, "analysis: free parameter count and the too-few-points rule") {
    auto hill = default_param_settings(ModelId::HillGt0);
    CHECK(free_param_count(ModelId::HillGt0, hill) == 3);
    hill[1].fixed = true;
    CHECK(free_param_count(ModelId::HillGt0, hill) == 2);
    // Missing settings count as free parameters.
    CHECK(free_param_count(ModelId::HillGt0, {}) == 3);
    auto fixedFirst = default_param_settings(ModelId::ActivatorGt0)[0];
    fixedFirst.fixed = true;
    CHECK(free_param_count(ModelId::ActivatorGt0, {fixedFirst}) == 3);
    CHECK(free_param_count(ModelId::Arrhenius, default_param_settings(ModelId::Arrhenius)) == 2);

    // v3.51 warned when #points <= #free parameters + 1.
    const auto mm = default_param_settings(ModelId::MichaelisMenten);
    CHECK(too_few_points(ModelId::MichaelisMenten, mm, 2));
    CHECK(too_few_points(ModelId::MichaelisMenten, mm, 3));
    CHECK(!too_few_points(ModelId::MichaelisMenten, mm, 4));
    CHECK(too_few_points(ModelId::HillGt0, hill, 3));  // h fixed: 2 free
    CHECK(!too_few_points(ModelId::HillGt0, hill, 4));
}

TEST_CASE(analysis_descriptions, "analysis: model descriptions list the fixed and variable parameters") {
    auto d = [](ModelId m, std::vector<int> fixed) {
        auto s = default_param_settings(m);
        for (int j : fixed) s[static_cast<size_t>(j)].fixed = true;
        return model_description(m, s);
    };
    CHECK(d(ModelId::MichaelisMenten, {}) == "Michaelis-Menten/VARIABLE Vmax");
    CHECK(d(ModelId::MichaelisMenten, {1}) == "Michaelis-Menten/FIXED Vmax");
    CHECK(d(ModelId::MichaelisMenten, {0}) == "Michaelis-Menten/VARIABLE Vmax/FIXED Km");
    CHECK(d(ModelId::HillGt0, {}) == "Hill eqn (>0)/VARIABLE Vmax");
    CHECK(d(ModelId::HillGt0, {0, 1}) == "Hill eqn (>0)/VARIABLE Vmax/FIXED Km/FIXED h");
    CHECK(d(ModelId::ActivatorGt0, {}) == "Activation eqn (>0)/VARIABLE Vmax/VARIABLE v0");
    CHECK(d(ModelId::ActivatorGt0, {2, 3}) == "Activation eqn (>0)/FIXED Vmax/FIXED v0");
    CHECK(d(ModelId::ActivatorEq1, {2}) == "Activation eqn (=1)/VARIABLE Vmax/FIXED v0");
    CHECK(d(ModelId::InhibitionLinear, {}) == "Linear inhibition/VARIABLE v0");  // no vmin
    CHECK(d(ModelId::InhibitionK50, {2}) == "Non-linear inhibition (K50)/FIXED vmin/VARIABLE v0");
    CHECK(d(ModelId::InhibitionI50, {1}) == "Non-linear inhibition (I50)/VARIABLE vmin/VARIABLE v0/FIXED h");
    CHECK(d(ModelId::Arrhenius, {}) == "Arrhenius eqn");
    CHECK(d(ModelId::Linear, {}) == "Linear regression");
    CHECK(d(ModelId::Linear, {0}) == "Linear regression/FIXED intercept");
    // Missing settings read as "not fixed".
    CHECK(model_description(ModelId::Linear, {}) == "Linear regression");
    CHECK(model_description(ModelId::HillGt0, {}) == "Hill eqn (>0)/VARIABLE Vmax");
}

// ---------------------------------------------------------------------------
// Validation

TEST_CASE(analysis_validation, "analysis: validation errors for unusable data") {
    using test::request;
    CHECK(error_of(request(ModelId::MichaelisMenten, {})) == "No data entered.");
    CHECK(error_of(request(ModelId::MichaelisMenten, {{kNaN, 1}, {2, kNaN}})) == "No data entered.");
    CHECK(error_of(request(ModelId::MichaelisMenten, {{1, 2}})) == "1 data points cannot determine 2 parameters.");
    auto act = request(ModelId::ActivatorGt0, {{1, 2}, {2, 3}});
    act.params[2].fixed = true;
    act.params[2].value = 5;
    CHECK(error_of(act) == "2 data points cannot determine 3 parameters.");

    // Linear regression.
    CHECK(error_of(request(ModelId::Linear, {{1, 2}, {2, 3}})) == "Linear regression needs at least 3 data pairs.");
    CHECK(error_of(request(ModelId::Linear, {{1, 2}, {1, 3}, {1, 4}})) ==
          "Linear regression needs at least 2 different X values.");
    auto through = request(ModelId::Linear, {{1, 2}});
    through.params[0].fixed = true;
    CHECK(error_of(through) == "Regression with a fixed intercept needs at least 2 data pairs.");
    through.data = {{0, 2}, {0, 3}};
    CHECK(error_of(through) == "All X values are zero.");

    // The fit itself fails: v = Vmax [S] / (Km + [S]) is undefined at [S] = -Km.
    for (Method m : {Method::Marquardt, Method::GaussNewton})
        CHECK(error_of(manual(ModelId::MichaelisMenten, {{-1, 1}, {1, 2}, {2, 3}}, {1, 4}, m)) ==
              "The equation cannot be evaluated at the initial estimates.");
    // All [S] = 0: every derivative is zero, so Gauss-Newton has no solution.
    CHECK(error_of(request(ModelId::MichaelisMenten, {{0, 1}, {0, 2}, {0, 3}}, Method::GaussNewton)) ==
          "The normal equations are singular: the parameters cannot be estimated from these data.");
}

TEST_CASE(analysis_identifiability_note, "analysis: Marquardt on unidentifiable data notes the missing SDs") {
    // Same all-zero data: Marquardt's damping keeps the system solvable, the
    // step is zero, and no standard deviation can be estimated.
    const auto a = test::must_analyse(test::request(ModelId::MichaelisMenten, {{0, 1}, {0, 2}, {0, 3}}));
    CHECK(std::isnan(a.fit.sd[0]) && std::isnan(a.fit.sd[1]));
    CHECK(contains(a.notes, "Some standard deviations could not be estimated (too few points, or the parameters "
                            "are not identifiable from these data)."));
    CHECK(contains(a.notes, "Only 3 points for 2 free parameters: a reliable fit needs more points than "
                            "parameters + 1."));
}

TEST_CASE(analysis_default_params, "analysis: a parameter list of the wrong length is replaced by the defaults") {
    // y = 6 x / (2 + x) at x = 1, 2, 4, 8, 16
    auto req = test::request(ModelId::MichaelisMenten, {{1, 2}, {2, 3}, {4, 4}, {8, 4.8}, {16, 6.0 * 16 / 18}});
    req.params.clear();
    const auto a = test::must_analyse(req);
    CHECK(a.request.params.size() == 2);
    for (const auto& s : a.request.params) CHECK(!s.fixed && s.min == 1e-6 && s.max == 1e6);
    CHECK_REL(a.fit.p[0], 2.0, 1e-8);
    CHECK_REL(a.fit.p[1], 6.0, 1e-8);
    CHECK(a.notes.empty());
}

// ---------------------------------------------------------------------------
// A noisy Michaelis-Menten fit checked against numpy

// p, sd, sse, dof, r = gn(lambda x, p: p[1] * x / (p[0] + x), x, y, [2, 10])
//   with x, y from examples/michaelis_menten.csv
// -> p = [2.713761066135603, 10.242584560747876], sd = [0.13776548506228808, 0.17853547638343334]
//    sse = 0.19310595206116202, sqrt(sse / 8) = 0.15536487378955788
//    r = [0.06474896276248332, 0.03689150716879208, -0.048019606684738925, 0.13467745318819624,
//         0.19274934671292154, -0.28764210948271884, -0.18659594817357839, 0.010529018757595487,
//         0.08132639277527254, 0.07533563220140138]  (none >= 2 SD)
//    '%.2f' % (0.5 / p[0]) = 0.18, 16 / p[0] = 5.90, 1.6583 / p[1] = 0.16, 8.8326 / p[1] = 0.86
// Residual signs ++-++--+++: 5 runs; enumerating all C(10, 3) arrangements
// gives P(R <= 5) = 0.5833; sign test 2 * sum(comb(10, i), i = 7..10) / 2**10 = 0.34375.
TEST_CASE(analysis_mm_example_vs_numpy, "analysis: Michaelis-Menten example matches an independent fit") {
    const std::vector<double> r = {0.06474896276248332, 0.03689150716879208, -0.048019606684738925,
                                   0.13467745318819624, 0.19274934671292154, -0.28764210948271884,
                                   -0.18659594817357839, 0.010529018757595487, 0.08132639277527254,
                                   0.07533563220140138};
    for (Method method : {Method::Marquardt, Method::GaussNewton}) {
        // Rows given in reverse order: analyse() sorts them.
        std::vector<Point> data(kMmExample.rbegin(), kMmExample.rend());
        const auto a = test::must_analyse(test::request(ModelId::MichaelisMenten, data, method));
        CHECK(a.fit.status == FitStatus::Converged);
        // Gauss-Newton stops on v3.51's per-parameter rule (|step| < 1e-5 |p|), so it is less exact.
        const double tol = method == Method::Marquardt ? 1e-8 : 1e-6;
        CHECK_REL(a.fit.p[0], 2.713761066135603, tol);
        CHECK_REL(a.fit.p[1], 10.242584560747876, tol);
        CHECK_REL(a.fit.sd[0], 0.13776548506228808, tol);
        CHECK_REL(a.fit.sd[1], 0.17853547638343334, tol);
        CHECK_REL(a.fit.sse, 0.19310595206116202, 1e-9);
        CHECK_REL(a.fit.stdDev, 0.15536487378955788, 1e-9);
        CHECK(a.fit.n == 10 && a.fit.nFree == 2 && a.fit.dof == 8);
        CHECK(a.xMin == 0.5 && a.xMax == 16 && a.yMin == 1.6583 && a.yMax == 8.8326);
        CHECK(a.data.size() == 10 && a.data.front().x == 0.5 && a.data.back().x == 16);
        CHECK(a.yhat.size() == 10 && a.resid.size() == 10 && a.largeResidual.size() == 10);
        for (size_t i = 0; i < r.size() && i < a.resid.size(); ++i) {
            CHECK_NEAR(a.resid[i], r[i], tol);
            CHECK_NEAR(a.yhat[i], a.data[i].y - r[i], tol);
            CHECK(!a.largeResidual[i]);
        }
        CHECK(a.notes.empty());
        CHECK(a.description == "Michaelis-Menten/VARIABLE Vmax");
        CHECK(a.method_title() ==
              (method == Method::Marquardt ? "MARQUARDT-LEVENBERG minimization" : "GAUSS-NEWTON minimization"));
        CHECK_LINES(a.interpretation(), {"[S] varies from 0.18 to 5.90 of Km value.",
                                         "v varies from 0.16 to 0.86 of Vmax value."});
        CHECK_LINES(a.residual_test_lines(),
                    {"Sequence of residuals = ++-++--+++",
                     "The sequence of residuals is random at the P < 0.05 level (5 runs, P = 0.583).",
                     "The # of +ve and -ve residuals is random at the P < 0.05 level (7 +, 3 -, P = 0.344)."});
        const auto rows = a.parameter_rows();
        CHECK(rows.size() == 2 && rows[0].name == "Km" && rows[1].name == "Vmax");
        CHECK(rows.size() == 2 && !rows[0].fixed && !rows[0].derived && rows[1].value == a.fit.p[1] &&
              rows[1].sd == a.fit.sd[1]);
    }
}

// ---------------------------------------------------------------------------
// Interpretation: substrate and velocity ranges

TEST_CASE(analysis_range_lines_exact, "analysis: [S]/Km and v/Vmax range lines for exact data") {
    // Model case 0: Km 2.5, Vmax 10, [S] 0.25..24 -> 0.25/2.5 = 0.10, 24/2.5 = 9.60,
    // v = 10*0.25/2.75 = 0.909 -> 0.09 of Vmax, 10*24/26.5 = 9.057 -> 0.91 of Vmax.
    const auto mm = test::must_analyse(test::request(ModelId::MichaelisMenten, test::exact_data(test::model_cases()[0])));
    CHECK_LINES(mm.interpretation(),
                {"[S] varies from 0.10 to 9.60 of Km value.", "v varies from 0.09 to 0.91 of Vmax value."});

    // Linear inhibition, I50 4, v0 20, [I] 0..32: v falls from 20 (1.00 of v0)
    // to 20*4/36 = 2.22 (0.11 of v0).
    const auto inh = test::must_analyse(test::request(ModelId::InhibitionLinear, test::exact_data(test::model_cases()[6])));
    CHECK_LINES(inh.interpretation(),
                {"[I] varies from 0.00 to 8.00 of I50 value.", "v varies from 1.00 to 0.11 of v0 value."});

    // k50 = lambda x, p: p[3] - (p[3] - p[2]) * x**p[1] / (p[0]**p[1] + x**p[1])
    // y = k50(x, [4, 1.5, 3, 20]) -> y.max() / 20 = 1.00, y.min() / 20 = 0.19
    // i50 = lambda x, p: p[3] - (p[3] - p[2]) * x**p[1] / (x**p[1] + p[0]**p[1] * (1 - 2 * p[2] / p[3]))
    // y = i50(x, [4, 1.5, 3, 20]) -> 1.00 and 0.18
    const auto k50 = test::must_analyse(test::request(ModelId::InhibitionK50, test::exact_data(test::model_cases()[8])));
    const auto k50Lines = k50.interpretation();
    CHECK(k50Lines.size() >= 2);
    if (k50Lines.size() >= 2) {
        CHECK(k50Lines[0] == "[I] varies from 0.00 to 8.00 of K50 value.");
        CHECK(k50Lines[1] == "v varies from 1.00 to 0.19 of v0 value.");
    }
    const auto i50 = test::must_analyse(test::request(ModelId::InhibitionI50, test::exact_data(test::model_cases()[7])));
    const auto i50Lines = i50.interpretation();
    CHECK(i50Lines.size() >= 2);
    if (i50Lines.size() >= 2) {
        CHECK(i50Lines[0] == "[I] varies from 0.00 to 8.00 of I50 value.");
        CHECK(i50Lines[1] == "v varies from 1.00 to 0.18 of v0 value.");
    }
}

// ---------------------------------------------------------------------------
// Interpretation: fold activation and the Hill coefficient

// act = lambda x, p: p[3] + (p[2] - p[3]) * x**p[1] / (p[0]**p[1] + x**p[1])
// x = [0, 0, 0.5, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32], y = act(x, [3, 1.6, 12, 2]), y[0:2] = 1.9, 2.1
// The true parameters are the least-squares solution (only the x = 0 rows
// have residuals, +/-0.1, and the model's derivatives there are 0 except
// d/dv0 = 1, where they cancel), so the SDs follow from J at the true values:
//   J = jac(act, x, ptrue)[:, free]; cov = 0.02 / dof * inv(J.T @ J); t = 0.6 / sqrt(cov[1, 1])
//   all four free:   dof 9,  t = 25.31457504422434, t_p = 1.126559754338329e-09
//   v0 fixed at 2:   dof 10, t = 30.10950027140279, t_p = 3.8214450233986525e-11
//   y.min() / 12 = 1.9 / 12 = 0.16, y.max() / 12 = 0.98; 32 / 3 = 10.67; Vmax / v0 = 6
TEST_CASE(analysis_activation_hill_test, "analysis: fold activation and the Hill coefficient tested against 1") {
    auto act = [](double x) {
        const double z = std::pow(x, 1.6);
        return 2.0 + 10.0 * z / (std::pow(3.0, 1.6) + z);
    };
    std::vector<Point> data = {{0, 1.9}, {0, 2.1}};
    for (double x : {0.5, 1.0, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0}) data.push_back({x, act(x)});

    for (bool fixV0 : {false, true}) {
        auto req = test::request(ModelId::ActivatorGt0, data);
        if (fixV0) {
            req.params[3].fixed = true;
            req.params[3].value = 99.0;  // replaced by the mean at [A] = 0
        }
        const auto a = test::must_analyse(req);
        CHECK_REL(a.fit.p[0], 3.0, 1e-7);
        CHECK_REL(a.fit.p[1], 1.6, 1e-7);
        CHECK_REL(a.fit.p[2], 12.0, 1e-7);
        CHECK_REL(a.fit.p[3], 2.0, 1e-7);
        CHECK_REL(a.fit.sse, 0.02, 1e-6);
        CHECK(a.fit.dof == (fixV0 ? 10 : 9));
        const std::string hillLine =
            fixV0 ? "Hill coeff. is significantly different from 1 at the P<0.001 level (t = 30.11, df = 10, P = 3.82e-11)."
                  : "Hill coeff. is significantly different from 1 at the P<0.001 level (t = 25.31, df = 9, P = 1.13e-09).";
        CHECK_LINES(a.interpretation(), {"[A] varies from 0.00 to 10.67 of Ka value.",
                                         "v varies from 0.16 to 0.98 of Vmax value.", "Fold activation: 6", hillLine});
        if (fixV0) {
            CHECK(a.fit.p[3] == 2.0 && a.fit.fixed[3] && a.fit.sd[3] == 0.0);
            CHECK_LINES(a.notes, {"v0 was fixed at the mean velocity measured at [A] = 0 (2)."});
            CHECK(a.description == "Activation eqn (>0)/VARIABLE Vmax/FIXED v0");
        } else {
            CHECK(a.notes.empty());
        }
    }
}

TEST_CASE(analysis_fold_activation_infinite, "analysis: fold activation is infinite when v0 is fixed at 0") {
    // v0 fixed at 0 with no [A] = 0 rows: the fixed value is kept, and the
    // activator equation becomes Michaelis-Menten (Ka 2.5, Vmax 10).
    auto req = test::request(ModelId::ActivatorEq1, test::exact_data(test::model_cases()[0]));
    req.params[2].fixed = true;
    req.params[2].value = 0.0;
    const auto a = test::must_analyse(req);
    CHECK(a.fit.p[2] == 0.0);
    CHECK_REL(a.fit.p[0], 2.5, 1e-7);
    CHECK_REL(a.fit.p[1], 10.0, 1e-7);
    CHECK(a.notes.empty());  // no "v0 was fixed at the mean" note without [A] = 0
    CHECK(a.description == "Activation eqn (=1)/VARIABLE Vmax/FIXED v0");
    CHECK_LINES(a.interpretation(), {"[A] varies from 0.10 to 9.60 of Ka value.",
                                     "v varies from 0.09 to 0.91 of Vmax value.", "Fold activation: infinite"});
}

TEST_CASE(analysis_hill_fixed, "analysis: a fixed Hill coefficient is not tested") {
    // hill = lambda x, p: p[2] * x**p[1] / (p[0]**p[1] + x**p[1]); y = hill(x, [3, 1.8, 12])
    // -> 0.25/3 = 0.08, 24/3 = 8.00; y.min()/12 = 0.01, y.max()/12 = 0.98
    auto req = test::request(ModelId::HillGt0, test::exact_data(test::model_cases()[1]));
    req.params[1].fixed = true;
    req.params[1].value = 1.8;
    const auto a = test::must_analyse(req);
    CHECK(a.fit.p[1] == 1.8 && a.fit.sd[1] == 0.0 && a.fit.nFree == 2);
    CHECK(a.description == "Hill eqn (>0)/VARIABLE Vmax/FIXED h");
    CHECK_LINES(a.interpretation(), {"[S] varies from 0.08 to 8.00 of Km value.",
                                     "v varies from 0.01 to 0.98 of Vmax value.",
                                     "The Hill coefficient was fixed, so it was not tested against 1."});
}

// x = [0.5, 1, 2, 4, 8, 16, 32]
// y = np.round(hill(x, [2, 1, 10]) * (1 + 0.03 * np.sin(1.7 * np.arange(7) + 0.3)), 4)
//   = [2.0177, 3.4243, 4.9205, 6.5121, 8.175, 9.0449, 9.1634]
// p, sd, sse, dof, r = gn(hill, x, y, [2, 1, 10])
// -> p = [1.95317245, 1.01478581, 9.90823613], sd = [0.16826199, 0.07622279, 0.2904399]
//    t = |h - 1| / sd_h = 0.19398147, t_p(t, 4) = 0.8556
TEST_CASE(analysis_hill_not_significant, "analysis: a Hill coefficient near 1 is NOT significantly different") {
    const std::vector<Point> data = {{0.5, 2.0177}, {1, 3.4243}, {2, 4.9205}, {4, 6.5121},
                                     {8, 8.175},    {16, 9.0449}, {32, 9.1634}};
    for (Method method : {Method::Marquardt, Method::GaussNewton}) {
        const auto a = test::must_analyse(test::request(ModelId::HillGt0, data, method));
        CHECK_REL(a.fit.p[0], 1.95317245, 1e-6);
        CHECK_REL(a.fit.p[1], 1.01478581, 1e-6);
        CHECK_REL(a.fit.p[2], 9.90823613, 1e-6);
        CHECK_REL(a.fit.sd[1], 0.07622279, 1e-5);
        const auto lines = a.interpretation();
        CHECK(lines.size() == 3);
        if (lines.size() == 3)
            CHECK(lines[2] == "Hill coeff. is NOT significantly different from 1 (t = 0.194, df = 4, P = 0.856).");
    }
}

TEST_CASE(analysis_perfect_fit, "analysis: a perfect fit has zero SDs, so h is not tested") {
    // v = 2 x / (1 + x) (Km 1, h 1, Vmax 2): every value is exact in binary, so
    // starting at the answer gives a sum of squares of exactly 0.
    const std::vector<Point> data = {{0.25, 0.4}, {1, 1}, {3, 1.5}, {7, 1.75}, {31, 1.9375}};
    const auto gn = test::must_analyse(manual(ModelId::HillGt0, data, {1, 1, 2}, Method::GaussNewton));
    // The gradient is 0, so Gauss-Newton's step is 0 and it reports convergence.
    CHECK(gn.fit.status == FitStatus::Converged);
    CHECK(gn.fit.sse == 0.0 && gn.fit.stdDev == 0.0);
    CHECK(gn.fit.sd[0] == 0.0 && gn.fit.sd[1] == 0.0 && gn.fit.sd[2] == 0.0);
    CHECK(gn.notes.empty());
    // 0.25/1 = 0.25, 31/1 = 31.00, 0.4/2 = 0.20, 1.9375/2 = 0.96875 -> 0.97; no Hill line.
    CHECK_LINES(gn.interpretation(),
                {"[S] varies from 0.25 to 31.00 of Km value.", "v varies from 0.20 to 0.97 of Vmax value."});
    for (size_t i = 0; i < gn.data.size(); ++i) CHECK(gn.resid[i] == 0.0);
    CHECK(gn.residual_test_lines()[0] == "Sequence of residuals = 00000");

    const auto lm = test::must_analyse(manual(ModelId::HillGt0, data, {1, 1, 2}, Method::Marquardt));
    CHECK(lm.fit.status == FitStatus::Converged && lm.fit.message == "Run has converged.");
    CHECK(lm.fit.sse == 0.0 && lm.notes.empty());
    CHECK(lm.interpretation().size() == 2);
}

// ---------------------------------------------------------------------------
// Notes

TEST_CASE(analysis_too_few_points_notes, "analysis: notes for too few points and SDs that can't be estimated") {
    // Two points, two parameters: 6 x / (2 + x) passes through (1, 2) and (4, 4)
    // (solve V = 2K + 2 and 4V = 4K + 16: K = 2, V = 6), with no degrees of freedom.
    const auto two = test::must_analyse(test::request(ModelId::MichaelisMenten, {{4, 4}, {1, 2}}));
    CHECK_NEAR(two.fit.p[0], 2.0, 1e-6);
    CHECK_NEAR(two.fit.p[1], 6.0, 1e-6);
    CHECK(two.fit.dof == 0 && std::isnan(two.fit.stdDev));
    CHECK(std::isnan(two.fit.sd[0]) && std::isnan(two.fit.sd[1]));
    CHECK(!two.largeResidual[0] && !two.largeResidual[1]);  // no SD to compare with
    CHECK(contains(two.notes,
                   "Only 2 points for 2 free parameters: a reliable fit needs more points than parameters + 1."));
    CHECK(contains(two.notes, "Some standard deviations could not be estimated (too few points, or the parameters "
                              "are not identifiable from these data)."));
    CHECK(two.interpretation().size() == 2);

    // Three points: still "too few" (3 <= 2 + 1), but the SDs exist.
    const auto three = test::must_analyse(test::request(ModelId::MichaelisMenten, {{1, 2}, {4, 4}, {8, 4.8}}));
    CHECK(three.fit.dof == 1 && std::isfinite(three.fit.sd[0]) && std::isfinite(three.fit.sd[1]));
    CHECK_LINES(three.notes,
                {"Only 3 points for 2 free parameters: a reliable fit needs more points than parameters + 1."});
}

TEST_CASE(analysis_iteration_limit_note, "analysis: hitting the iteration limit adds the solver message") {
    for (Method method : {Method::Marquardt, Method::GaussNewton}) {
        auto req = manual(ModelId::MichaelisMenten, kMmExample, {40, 1}, method);
        req.maxIterations = 1;
        const auto a = test::must_analyse(req);
        CHECK(a.fit.status == FitStatus::MaxIterations && a.fit.iterations == 1);
        CHECK_LINES(a.notes, {"Maximum of 1 iterations reached before convergence."});
    }
}

TEST_CASE(analysis_v0_note_inhibition, "analysis: a fixed v0 is taken from the mean at [I] = 0") {
    // Replicates at [I] = 0 average to 19.5; the rest follow 19.5 * 4 / (4 + [I]).
    std::vector<Point> data = {{0, 19}, {0, 20}};
    for (double x : {1.0, 2.0, 4.0, 8.0, 16.0}) data.push_back({x, 19.5 * 4.0 / (4.0 + x)});
    auto req = test::request(ModelId::InhibitionLinear, data);
    req.params[1].fixed = true;
    const auto a = test::must_analyse(req);
    CHECK(a.fit.p[1] == 19.5);
    CHECK_REL(a.fit.p[0], 4.0, 1e-7);
    CHECK_LINES(a.notes, {"v0 was fixed at the mean velocity measured at [I] = 0 (19.5)."});
    CHECK(a.description == "Linear inhibition/FIXED v0");
}

// ---------------------------------------------------------------------------
// Linear regression

// x = np.arange(1, 11.); y = 1 + 2 * x + [0.1, -0.1, 0.05, -0.05, 3.0, 0.0, -0.1, 0.1, -0.05, 0.05]
// b, r = lstsq([1, x], y); ms = (r**2).sum() / 8; sxx = ((x - x.mean())**2).sum()
// -> b = [1.4, 1.9818181818181835], sd = [sqrt(ms * (1/10 + x.mean()**2 / sxx)), sqrt(ms / sxx)]
//    = [0.6883500254781413, 0.11093765156354836], sqrt(ms) = 1.007641260117364
//    |r| >= 2 sqrt(ms) only at x = 5 (r = 2.6909)
//    r_xy = 0.987697 +/- sqrt((1 - r**2) / 8) = 0.055289
//    t vs 0 = 17.86 (t_p 9.88e-08), t vs 1 = 8.85 (2.1e-05), intercept t = 2.034 (0.0764)
//    F = (syy - sse) / ms = 319.1, f_p(F, 1, 8) = 9.88e-08
// Residual signs ----+-----: 3 runs, P(R <= 3) = 1; sign test 2 * 11 / 1024 = 0.0215.
TEST_CASE(analysis_linear_outlier, "analysis: linear regression statistics and the residual beyond 2 SD") {
    const std::vector<Point> data = {{1, 3.1},  {2, 4.9},  {3, 7.05},  {4, 8.95},  {5, 14.0},
                                     {6, 13.0}, {7, 14.9}, {8, 17.1}, {9, 18.95}, {10, 21.05}};
    const auto a = test::must_analyse(test::request(ModelId::Linear, data));
    CHECK(a.linear.has_value() && a.method_title() == "LINEAR REGRESSION");
    CHECK(a.fit.status == FitStatus::Converged && a.fit.message.empty());
    CHECK_NEAR(a.fit.p[0], 1.4, 1e-12);
    CHECK_NEAR(a.fit.p[1], 1.9818181818181835, 1e-12);
    CHECK_REL(a.fit.sd[0], 0.6883500254781413, 1e-10);
    CHECK_REL(a.fit.sd[1], 0.11093765156354836, 1e-10);
    CHECK_REL(a.fit.stdDev, 1.007641260117364, 1e-10);
    CHECK(a.fit.n == 10 && a.fit.nFree == 2 && a.fit.dof == 8 && !a.fit.fixed[0] && !a.fit.fixed[1]);
    CHECK(a.fit.initial == a.fit.p);
    for (size_t i = 0; i < a.data.size(); ++i) CHECK(a.largeResidual[i] == (i == 4));
    CHECK_NEAR(a.resid[4], 2.690909090909088, 1e-10);
    CHECK_NEAR(a.resid[1], -0.46363636363636207, 1e-10);
    CHECK(a.description == "Linear regression");
    CHECK_LINES(a.interpretation(),
                {"Correlation coeff. = 0.987697 +/- 0.055289", "Degrees of freedom = 8",
                 "The SLOPE is significantly different from 0 at the P<0.001 level (t = 17.86, df = 8, P = 9.88e-08).",
                 "The SLOPE is significantly different from 1 at the P<0.001 level (t = 8.85, df = 8, P = 2.1e-05).",
                 "The INTERCEPT is significantly different from 0 at the P<0.1 level (t = 2.034, df = 8, P = 0.0764).",
                 "Regression F = 319.1 (df 1, 8), P = 9.88e-08."});
    CHECK_LINES(a.residual_test_lines(),
                {"Sequence of residuals = ----+-----",
                 "The sequence of residuals is random at the P < 0.05 level (3 runs, P = 1).",
                 "The # of +ve and -ve residuals is NOT random at the P < 0.05 level (1 +, 9 -, P = 0.0215)."});
}

// x = [1, 1, 2, 2, 3, 3, 4, 4, 5, 5]; y = [1.2, 0.8, 2.3, 1.9, 2.7, 3.4, 4.4, 3.9, 4.8, 5.3]
// b, r = lstsq([1, x], y) -> [0.025, 1.015]; sse = 0.6765
// sspure = sum over groups of ((y_g - mean_g)**2) ; sslack = sse - sspure; dfl = 5 - 2; dfp = 10 - 5
// ms = sslack / dfl; sd = [0.06278269400612449, 0.018929694486000896]
// Flof = (sslack / dfl) / (sspure / dfp) = 0.05471, f_p(Flof, 3, 5) = 0.981; F = ssreg / ms = 2875
// Group-mean residual signs -+-+-: 5 groups (< 8), sign test P = 1.
TEST_CASE(analysis_linear_replicates, "analysis: replicated X values add the lack-of-fit F line") {
    const std::vector<Point> data = {{1, 1.2}, {1, 0.8}, {2, 2.3}, {2, 1.9}, {3, 2.7},
                                     {3, 3.4}, {4, 4.4}, {4, 3.9}, {5, 4.8}, {5, 5.3}};
    const auto a = test::must_analyse(test::request(ModelId::Linear, data));
    CHECK_NEAR(a.fit.p[0], 0.025, 1e-12);
    CHECK_NEAR(a.fit.p[1], 1.015, 1e-12);
    // The SDs and tests use the residual mean square, 0.6765/8 (Python:
    // ordinary least squares; two-tailed t probabilities for df 8 from the
    // A&S 26.7.4 finite sum).
    CHECK_REL(a.fit.sd[0], 0.2156603231936742, 1e-9);
    CHECK_REL(a.fit.sd[1], 0.065024034018199769, 1e-9);
    CHECK_REL(a.fit.sse, 0.6765, 1e-10);
    CHECK_LINES(a.interpretation(),
                {"Correlation coeff. = 0.983977 +/- 0.0630366", "Degrees of freedom = 8",
                 "The SLOPE is significantly different from 0 at the P<0.001 level (t = 15.61, df = 8, P = 2.83e-07).",
                 "The SLOPE is NOT significantly different from 1 (t = 0.2307, df = 8, P = 0.823).",
                 "The INTERCEPT is NOT significantly different from 0 (t = 0.1159, df = 8, P = 0.911).",
                 "Regression F = 243.7 (df 1, 8), P = 2.83e-07.",
                 "Replicated X values: lack-of-fit F = 0.05471 (df 3, 5), P = 0.981."});
    CHECK_LINES(a.residual_test_lines(),
                {"Sequence of residuals = -+-+-", "Too few data points to test sequence randomness.",
                 "The # of +ve and -ve residuals is random at the P < 0.05 level (2 +, 3 -, P = 1)."});
}

// x = [1, 1, 1, 3, 3, 3]; y = [2.0, 2.2, 2.4, 5.9, 6.0, 6.4]: only two X values, so there is
// no lack-of-fit term and the pooled residual (df 4) gives the SDs:
// b = [0.25, 1.95]; sd_slope = sqrt(sse / 4 / sxx) = 0.09574271077563382
TEST_CASE(analysis_linear_two_groups, "analysis: two replicated X values give no lack-of-fit line") {
    const auto a =
        test::must_analyse(test::request(ModelId::Linear, {{1, 2.0}, {1, 2.2}, {1, 2.4}, {3, 5.9}, {3, 6.0}, {3, 6.4}}));
    CHECK_NEAR(a.fit.p[0], 0.25, 1e-12);
    CHECK_NEAR(a.fit.p[1], 1.95, 1e-12);
    CHECK_REL(a.fit.sd[1], 0.09574271077563382, 1e-10);
    const auto lines = a.interpretation();
    CHECK(lines.size() == 6);
    CHECK(contains(lines, "Degrees of freedom = 4"));
    for (const auto& l : lines) CHECK(l.find("lack-of-fit") == std::string::npos);
}

// x = [1, 2, 3, 4, 5]; y = [3.1, 4.9, 7.2, 8.8, 11.1]; intercept fixed at 1, ys = y - 1
// slope = (x*ys).sum() / (x*x).sum() = 2.003636363636364; ms = ((ys - slope*x)**2).sum() / 4
// sd = sqrt(ms / (x*x).sum()) = 0.02228663758569317; sqrt(ms) = 0.165282127945467
// r = (x*ys).sum() / sqrt((x*x).sum() * (ys*ys).sum()) = 0.999753, sd_r = sqrt((1 - r*r) / 3) = 0.0128407
// t vs 0 = 89.9 (t_p 9.18e-08), t vs 1 = 45.03 (1.45e-06); F = 8083 (f_p 9.18e-08)
TEST_CASE(analysis_linear_fixed_intercept, "analysis: regression through a fixed intercept") {
    auto req = test::request(ModelId::Linear, {{1, 3.1}, {2, 4.9}, {3, 7.2}, {4, 8.8}, {5, 11.1}});
    req.params[0].fixed = true;
    req.params[0].value = 1.0;
    const auto a = test::must_analyse(req);
    CHECK(a.fit.p[0] == 1.0 && a.fit.sd[0] == 0.0 && a.fit.fixed[0]);
    CHECK_REL(a.fit.p[1], 2.003636363636364, 1e-12);
    CHECK_REL(a.fit.sd[1], 0.02228663758569317, 1e-10);
    CHECK_REL(a.fit.stdDev, 0.165282127945467, 1e-10);
    CHECK(a.fit.nFree == 1 && a.fit.dof == 4);
    CHECK(a.description == "Linear regression/FIXED intercept");
    CHECK(a.notes.empty());
    CHECK_LINES(a.interpretation(),
                {"Correlation coeff. = 0.999753 +/- 0.0128407", "Degrees of freedom = 4",
                 "The SLOPE is significantly different from 0 at the P<0.001 level (t = 89.9, df = 4, P = 9.18e-08).",
                 "The SLOPE is significantly different from 1 at the P<0.001 level (t = 45.03, df = 4, P = 1.45e-06).",
                 "Regression F = 8083 (df 1, 4), P = 9.18e-08."});
    const auto rows = a.parameter_rows();
    CHECK(rows.size() == 2 && rows[0].name == "Intercept" && rows[0].fixed && rows[1].name == "Slope");
}

TEST_CASE(analysis_linear_too_few_note, "analysis: linear regression with 3 points notes too few points") {
    // y = 1 + 2x exactly at x = 1, 2, 3; 3 <= 2 free + 1.
    const auto a = test::must_analyse(test::request(ModelId::Linear, {{1, 3}, {2, 5}, {3, 7}}));
    CHECK_NEAR(a.fit.p[0], 1.0, 1e-12);
    CHECK_NEAR(a.fit.p[1], 2.0, 1e-12);
    CHECK_LINES(a.notes,
                {"Only 3 points for 2 free parameters: a reliable fit needs more points than parameters + 1."});
}

// ---------------------------------------------------------------------------
// Arrhenius

// log10(v) = log10(A) - Ea / (ln10 R (T + 273.15)) is a straight line in u = 1/(T + 273.15):
// u = 1 / (T + 273.15); b, r = lstsq([1, u], y) with T, y from examples/arrhenius.csv
// Ea = -b[1] * ln10 * R (R = 1.98720425864083e-3 kcal/mol/K), sd likewise from the line's SDs
// -> log10(A) = 9.119053021108286 +/- 0.08981229245478778
//    Ea = 12.158845097307383 +/- 0.1212248252104751 kcal/mol = 50.872607887134095 +/- 0.5072046686806279 kJ/mol
//    sqrt(sse / 6) = 0.009853086446418849
TEST_CASE(analysis_arrhenius, "analysis: Arrhenius fit equals the straight line in 1/T, with Ea in kJ/mol") {
    const std::vector<Point> data = {{5, -0.4234},  {10, -0.2605}, {15, -0.1117}, {20, 0.0377},
                                     {25, 0.2069}, {30, 0.3520},  {35, 0.4990},  {40, 0.6413}};
    const auto a = test::must_analyse(test::request(ModelId::Arrhenius, data));
    CHECK_REL(a.fit.p[0], 9.119053021108286, 1e-8);
    CHECK_REL(a.fit.p[1], 12.158845097307383, 1e-8);
    CHECK_REL(a.fit.sd[0], 0.08981229245478778, 1e-6);
    CHECK_REL(a.fit.sd[1], 0.1212248252104751, 1e-6);
    CHECK_REL(a.fit.stdDev, 0.009853086446418849, 1e-7);
    const auto rows = a.parameter_rows();
    CHECK(rows.size() == 3);
    if (rows.size() == 3) {
        CHECK(rows[0].name == "log10(A)" && rows[1].name == "Ea (kcal/mol)" && rows[2].name == "Ea (kJ/mol)");
        CHECK(!rows[0].derived && !rows[1].derived && rows[2].derived && !rows[2].fixed);
        CHECK_REL(rows[2].value, 50.872607887134095, 1e-8);
        CHECK_REL(rows[2].sd, 0.5072046686806279, 1e-6);
    }
    CHECK(a.interpretation().empty());
    CHECK(a.description == "Arrhenius eqn");
    CHECK(a.method_title() == "MARQUARDT-LEVENBERG minimization");
}

// ---------------------------------------------------------------------------
// Residual test text from hand-set results

TEST_CASE(analysis_residual_lines_text, "analysis: residual test lines for each outcome") {
    // Start from a real analysis (see the note in test_report.cpp on not
    // default-constructing library structs here) and set the results by hand.
    auto a = test::must_analyse(test::request(ModelId::Linear, {{1, 1}, {2, 2.5}, {3, 2.9}}));
    auto& rt = a.residualTests;
    rt.sequence = "++++----";
    rt.positives = 4;
    rt.negatives = 4;
    rt.runs = 2;
    rt.runsTestable = true;
    rt.runsP = 2.0 / 70.0;  // 2 of the C(8, 4) = 70 orders have 2 runs
    rt.runsRandom = false;
    rt.signP = 1.0;
    rt.signsRandom = true;
    CHECK_LINES(a.residual_test_lines(),
                {"Sequence of residuals = ++++----",
                 "The sequence of residuals is NOT random at the P < 0.05 level (2 runs, P = 0.0286).",
                 "The # of +ve and -ve residuals is random at the P < 0.05 level (4 +, 4 -, P = 1)."});
    rt.sequence = "+++";
    rt.positives = 3;
    rt.negatives = 0;
    rt.runs = 1;
    rt.runsTestable = false;
    rt.signP = 0.25;
    CHECK_LINES(a.residual_test_lines(),
                {"Sequence of residuals = +++", "Too few data points to test sequence randomness.",
                 "The # of +ve and -ve residuals is random at the P < 0.05 level (3 +, 0 -, P = 0.25)."});
    rt.signP = 0.03;
    rt.signsRandom = false;
    CHECK(a.residual_test_lines()[2] ==
          "The # of +ve and -ve residuals is NOT random at the P < 0.05 level (3 +, 0 -, P = 0.03).");
}

TEST_CASE(analysis_hill_sd_unknown, "analysis: no Hill test when its SD cannot be estimated") {
    // Three points for three parameters: v = 6x / (2 + x) (Km 2, h 1, Vmax 6)
    // gives exactly 2, 3, 4 at x = 1, 2, 4, but leaves no degrees of freedom.
    const auto a =
        test::must_analyse(manual(ModelId::HillGt0, {{1, 2}, {2, 3}, {4, 4}}, {2, 1, 6}, Method::Marquardt));
    CHECK(a.fit.dof == 0 && std::isnan(a.fit.sd[1]));
    // 1/2 = 0.50, 4/2 = 2.00; 2/6 = 0.33, 4/6 = 0.67; and no "Hill coeff." line.
    CHECK_LINES(a.interpretation(),
                {"[S] varies from 0.50 to 2.00 of Km value.", "v varies from 0.33 to 0.67 of Vmax value."});
    CHECK_LINES(a.notes,
                {"Only 3 points for 3 free parameters: a reliable fit needs more points than parameters + 1.",
                 "Some standard deviations could not be estimated (too few points, or the parameters are not "
                 "identifiable from these data)."});
}

// ---------------------------------------------------------------------------
// Defect found by these tests (fixed)

// Y = 1 + 2X exactly at X = 1..4: the regression is exact (slope 10/5 = 2,
// intercept (24 - 20)/4 = 1), every residual is 0 and so is the residual SD.
// The flag test |resid| >= 2 SD used to read 0 >= 0 and mark every point as
// "> 2 S.D." in the LONG report and residual table, as v3.51 did. A residual
// of 0 is never beyond 2 SD.
TEST_CASE(analysis_perfect_fit_flags_nothing, "analysis: a perfect fit flags no residual as beyond 2 SD") {
    const auto a = test::must_analyse(test::request(ModelId::Linear, {{1, 3}, {2, 5}, {3, 7}, {4, 9}}));
    CHECK(a.fit.stdDev == 0.0);
    for (size_t i = 0; i < a.data.size(); ++i) {
        CHECK(a.resid[i] == 0.0);
        CHECK(!a.largeResidual[i]);  // was flagged because 0 >= 2 * 0 (fixed)
    }
}
