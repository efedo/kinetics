// Tests for core/fit: Marquardt-Levenberg and Gauss-Newton with bounds and
// fixed parameters, the covariance-based SDs, the iteration limits and the
// failure paths.
//
// Least-squares references were computed independently in Python 3.12 with
// numpy (scipy is not available), by plain Newton iteration on the normal
// equations to a gradient of ~1e-15, then the Jacobian covariance:
//
//   def gn(f, J, p, x, y, it=100):
//       for _ in range(it):
//           r = y - f(p, x); Jm = J(p, x)
//           p = p + np.linalg.solve(Jm.T @ Jm, Jm.T @ r)
//       r = y - f(p, x); Jm = J(p, x); sse = r @ r; s2 = sse / (len(x) - len(p))
//       return p, np.sqrt(np.diag(s2 * np.linalg.inv(Jm.T @ Jm))), sse, np.sqrt(s2)
//
// with f and J the textbook Michaelis-Menten / Hill formulas, e.g.
//   mm(p, x) = p[1]*x/(p[0]+x),  J = [-p[1]*x/(p[0]+x)**2, x/(p[0]+x)].
// Problems that are linear in their parameters (Arrhenius, straight lines)
// use np.linalg.lstsq or closed forms instead.

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "core/fit.hpp"
#include "core/models.hpp"

#include "check.hpp"

using namespace kin;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

std::vector<Point> points(const std::vector<double>& x, const std::vector<double>& y) {
    std::vector<Point> out;
    for (size_t i = 0; i < x.size(); ++i) out.push_back({x[i], y[i]});
    return out;
}

FitResult run(ModelId model, const std::vector<Point>& data, const std::vector<double>& start,
              const std::vector<ParamSetting>& settings, Method method, int maxIterations = 0) {
    SolverOptions o;
    o.method = method;
    o.maxIterations = maxIterations;
    return fit_nonlinear(model, data, start, settings, o);
}

bool converged_or_minimum(const FitResult& r) {
    return r.status == FitStatus::Converged || r.status == FitStatus::NoImprovement;
}

// Accepted iterations never increase the sum of squares.
bool sse_monotone(const FitResult& r) {
    double last = kInf;
    for (const auto& h : r.history)
        if (h.accepted) {
            if (h.sse > last) return false;
            last = h.sse;
        }
    return true;
}

bool contains(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }

// Noisy Michaelis-Menten data. numpy reference (see the header):
//   Km = 2.100782307443681, Vmax = 9.256932724555304,
//   sd = 0.08112834183182588, 0.1133462636693531,
//   SSE = 0.033330588587615576, s = 0.09128333444229507.
const std::vector<double> kMmX = {0.5, 1, 2, 4, 8, 16};
const std::vector<double> kMmY = {1.7, 2.9, 4.6, 6.1, 7.4, 8.1};

}  // namespace

// ---------------------------------------------------------------------------

TEST_CASE(fit_default_settings, "fit: default settings copy the model bounds; h starts at 1, the rest at 0") {
    for (const auto& def : all_models()) {
        const auto s = default_param_settings(def.id);
        CHECK(s.size() == def.params.size());
        for (size_t j = 0; j < s.size() && j < def.params.size(); ++j) {
            CHECK(!s[j].fixed);
            CHECK(s[j].min == def.params[j].min && s[j].max == def.params[j].max);
            CHECK(s[j].value == (def.params[j].role == Role::HillCoeff ? 1.0 : 0.0));
        }
    }
}

TEST_CASE(fit_mm_matches_numpy, "fit: Michaelis-Menten fit and SDs match an independent numpy solution") {
    const auto d = points(kMmX, kMmY);
    const auto s = default_param_settings(ModelId::MichaelisMenten);
    const std::vector<double> start = {6, 10};
    for (Method m : {Method::Marquardt, Method::GaussNewton}) {
        const auto r = run(ModelId::MichaelisMenten, d, start, s, m);
        // Marquardt stops on a relative SSE change of 1e-10, Gauss-Newton on
        // parameter steps of 1e-5 relative, hence the different tolerances.
        const double tol = (m == Method::Marquardt) ? 1e-8 : 1e-6;
        CHECK(r.status == FitStatus::Converged && r.message == "Run has converged." && r.usable());
        CHECK(r.method == m);
        CHECK_REL(r.p[0], 2.100782307443681, tol);
        CHECK_REL(r.p[1], 9.256932724555304, tol);
        CHECK_REL(r.sd[0], 0.08112834183182588, 10 * tol);
        CHECK_REL(r.sd[1], 0.1133462636693531, 10 * tol);
        CHECK_REL(r.sse, 0.033330588587615576, 1e-10);
        CHECK_REL(r.stdDev, 0.09128333444229507, 1e-10);
        CHECK(r.n == 6 && r.nFree == 2 && r.dof == 4);
        CHECK(r.fixed.size() == 2 && !r.fixed[0] && !r.fixed[1]);
        CHECK(r.initial == start);
        // The history starts at the initial estimates (SSE from numpy) and
        // never goes uphill.
        CHECK(!r.history.empty() && r.history[0].iteration == 0 && r.history[0].p == start);
        CHECK_REL(r.history[0].sse, 15.377446220113548, 1e-13);
        CHECK(sse_monotone(r));
        CHECK(r.iterations == r.history.back().iteration);
    }
}

TEST_CASE(fit_gn_step_halving, "fit: Gauss-Newton halves a step that leaves the feasible region") {
    // From (6, 10) the Gauss-Newton step is (-8.61114052, -2.94035301)
    // (numpy). The full step drives Km below its bound, so it is clamped to
    // 1e-6 and SSE rises to 54.197; the half step, (1.69442974, 8.52982349),
    // gives SSE 0.4220778888749658 < 15.377.
    const auto r = run(ModelId::MichaelisMenten, points(kMmX, kMmY), {6, 10},
                       default_param_settings(ModelId::MichaelisMenten), Method::GaussNewton);
    CHECK(r.history.size() >= 2);
    if (r.history.size() < 2) return;
    CHECK(r.history[1].accepted && r.history[1].lambdaOrStep == 0.5);
    CHECK_REL(r.history[1].sse, 0.4220778888749658, 1e-12);
    CHECK_REL(r.history[1].p[0], 6 - 8.61114052 / 2, 1e-8);
    CHECK_REL(r.history[1].p[1], 10 - 2.94035301 / 2, 1e-8);
}

TEST_CASE(fit_hill_matches_numpy, "fit: three-parameter Hill fit and SDs match numpy") {
    // numpy: Km 4.761992817536292, h 1.73314955480539, Vmax 9.971608903600266,
    // sd 0.03756578008118264, 0.01598215985775221, 0.04556000362857867,
    // SSE 0.0033000623009488543, s 0.02569070766230021.
    const auto d = points({0.5, 1, 2, 3, 5, 8, 12, 20}, {0.21, 0.62, 1.85, 3.05, 5.2, 7.1, 8.3, 9.2});
    const double want[] = {4.761992817536292, 1.73314955480539, 9.971608903600266};
    const double wantSd[] = {0.03756578008118264, 0.01598215985775221, 0.04556000362857867};
    for (Method m : {Method::Marquardt, Method::GaussNewton}) {
        const auto r = run(ModelId::HillGt0, d, {4, 1.5, 10}, default_param_settings(ModelId::HillGt0), m);
        const double tol = (m == Method::Marquardt) ? 1e-8 : 1e-6;
        CHECK(r.status == FitStatus::Converged);
        for (int j = 0; j < 3; ++j) {
            CHECK_REL(r.p[static_cast<size_t>(j)], want[j], tol);
            CHECK_REL(r.sd[static_cast<size_t>(j)], wantSd[j], 10 * tol);
        }
        CHECK_REL(r.sse, 0.0033000623009488543, 1e-9);
        CHECK_REL(r.stdDev, 0.02569070766230021, 1e-9);
        CHECK(r.dof == 5);
        CHECK(sse_monotone(r));
    }
}

TEST_CASE(fit_arrhenius_linear_lstsq, "fit: Arrhenius is linear in its parameters: both methods give the lstsq answer") {
    // log10 v = logA - Ea u with u = 1/(ln10 R (T + 273.15)), so least squares
    // is np.linalg.lstsq([1, -u], y): logA 12.133694552033075,
    // Ea 15.299795200260734, sd 0.22059802197319175, 0.29538634277197434,
    // SSE 0.00198483191680541, s 0.019924015241940617.
    const auto d = points({5, 10, 15, 20, 25, 30, 35}, {0.12, 0.31, 0.55, 0.71, 0.93, 1.08, 1.30});
    for (Method m : {Method::Marquardt, Method::GaussNewton}) {
        const auto r = run(ModelId::Arrhenius, d, {10, 10}, default_param_settings(ModelId::Arrhenius), m);
        CHECK(converged_or_minimum(r));
        CHECK_REL(r.p[0], 12.133694552033075, 1e-9);
        CHECK_REL(r.p[1], 15.299795200260734, 1e-9);
        CHECK_REL(r.sd[0], 0.22059802197319175, 1e-8);
        CHECK_REL(r.sd[1], 0.29538634277197434, 1e-8);
        CHECK_REL(r.sse, 0.00198483191680541, 1e-9);
        CHECK_REL(r.stdDev, 0.019924015241940617, 1e-9);
    }
}

TEST_CASE(fit_fixed_parameters, "fit: fixed parameters keep their value, even outside the bounds, and get SD 0") {
    // Intercept fixed at 0.5: slope = sum x (y - 0.5) / sum x^2 and
    // sd = sqrt(s2 / sum x^2) with df n - 1 (np.linalg.lstsq):
    // 1.8654545454545448 and 0.02972324131685782.
    const auto d = points({1, 2, 3, 4, 5}, {2.2, 3.9, 6.1, 8.2, 9.8});
    auto s = default_param_settings(ModelId::Linear);
    s[0].fixed = true;
    for (Method m : {Method::Marquardt, Method::GaussNewton}) {
        const auto r = run(ModelId::Linear, d, {0.5, 1.0}, s, m);
        CHECK(converged_or_minimum(r));
        CHECK(r.p[0] == 0.5 && r.sd[0] == 0.0 && r.fixed[0] && !r.fixed[1]);
        CHECK_REL(r.p[1], 1.8654545454545448, 1e-9);
        CHECK_REL(r.sd[1], 0.02972324131685782, 1e-7);
        CHECK(r.nFree == 1 && r.dof == 4);
        for (const auto& h : r.history) CHECK(h.p[0] == 0.5);
    }
    // A fixed value is not clamped: intercept 5e6 lies above its bound 1e6.
    // Exact data y = 5e6 + 2x then give slope 2.
    const auto big = points({1, 2, 3, 4}, {5e6 + 2, 5e6 + 4, 5e6 + 6, 5e6 + 8});
    for (Method m : {Method::Marquardt, Method::GaussNewton}) {
        const auto r = run(ModelId::Linear, big, {5e6, 1.0}, s, m);
        CHECK(converged_or_minimum(r));
        CHECK(r.p[0] == 5e6);
        CHECK_REL(r.p[1], 2.0, 1e-10);
    }
}

TEST_CASE(fit_all_fixed, "fit: with every parameter fixed nothing is fitted and SSE is taken at the given values") {
    const auto d = points(kMmX, kMmY);
    auto s = default_param_settings(ModelId::MichaelisMenten);
    s[0].fixed = s[1].fixed = true;
    double sse = 0.0;
    for (size_t i = 0; i < kMmX.size(); ++i) sse += std::pow(kMmY[i] - 9.0 * kMmX[i] / (2.0 + kMmX[i]), 2);
    for (Method m : {Method::Marquardt, Method::GaussNewton}) {
        const auto r = run(ModelId::MichaelisMenten, d, {2, 9}, s, m);
        CHECK(r.status == FitStatus::Converged && contains(r.message, "All parameters are fixed"));
        CHECK(r.iterations == 0 && r.history.empty());
        CHECK(r.p[0] == 2.0 && r.p[1] == 9.0);
        CHECK(r.sd[0] == 0.0 && r.sd[1] == 0.0);
        CHECK(r.n == 6 && r.nFree == 0 && r.dof == 6);
        CHECK_REL(r.sse, sse, 1e-14);
        CHECK_REL(r.stdDev, std::sqrt(sse / 6.0), 1e-14);
    }
}

TEST_CASE(fit_start_clamped, "fit: start values outside the bounds are clamped; the raw start stays in initial") {
    // y = 1 + 0.5 x exactly; slope bounded to [0, 1.5] (inactive at the answer).
    const auto d = points({0, 1, 2, 3, 4}, {1, 1.5, 2, 2.5, 3});
    auto s = default_param_settings(ModelId::Linear);
    s[1].min = 0.0;
    s[1].max = 1.5;
    for (double slope0 : {10.0, -3.0})
        for (Method m : {Method::Marquardt, Method::GaussNewton}) {
            const auto r = run(ModelId::Linear, d, {0.0, slope0}, s, m);
            CHECK(r.initial[1] == slope0);
            CHECK(!r.history.empty() && r.history[0].p[1] == (slope0 > 0 ? 1.5 : 0.0));
            CHECK(converged_or_minimum(r));
            CHECK_NEAR(r.p[0], 1.0, 1e-9);
            CHECK_NEAR(r.p[1], 0.5, 1e-9);
            for (const auto& h : r.history) CHECK(h.p[1] >= 0.0 && h.p[1] <= 1.5);
        }
}

TEST_CASE(fit_bound_at_optimum, "fit: started at a constrained optimum on a bound, both methods stay there") {
    // y = 2x - 1.25 with the slope bounded above by 1.5. With the slope at
    // 1.5 the residuals 0.5x - 1.25 sum to 0, so (0, 1.5) is the constrained
    // least-squares solution (SSE 1.25), and the gradient pushes the slope
    // out of bounds. The slope is held at its bound and the intercept's step
    // is exactly 0, so both methods stop at once.
    const auto d = points({1, 2, 3, 4}, {0.75, 2.75, 4.75, 6.75});
    auto s = default_param_settings(ModelId::Linear);
    s[1].max = 1.5;
    // Marquardt: the zero step is not an improvement, and a zero step means
    // no downhill direction exists, so it converges on the first iteration.
    const auto lm = run(ModelId::Linear, d, {0, 1.5}, s, Method::Marquardt);
    CHECK(lm.status == FitStatus::Converged && lm.iterations == 1);
    CHECK(lm.p[0] == 0.0 && lm.p[1] == 1.5);
    CHECK(lm.sse == 1.25);
    CHECK(!lm.history.back().accepted);
    // Gauss-Newton: every parameter that could move is held, which is the
    // constrained minimum.
    const auto gn = run(ModelId::Linear, d, {0, 1.5}, s, Method::GaussNewton);
    CHECK(gn.status == FitStatus::Converged && contains(gn.message, "converged"));
    CHECK(gn.iterations == 1 && gn.p[0] == 0.0 && gn.p[1] == 1.5);
    CHECK(gn.history.size() == 2 && !gn.history.back().accepted);
    CHECK(gn.sse == 1.25);
}

TEST_CASE(fit_exact_minimum, "fit: started at an exact zero-residual fit, neither method moves") {
    // y = 1 + 2x on integers: SSE is exactly 0 at (1, 2), the gradient is 0.
    const auto d = points({0, 1, 2, 3}, {1, 3, 5, 7});
    const auto s = default_param_settings(ModelId::Linear);
    const auto lm = run(ModelId::Linear, d, {1, 2}, s, Method::Marquardt);
    CHECK(lm.status == FitStatus::Converged && lm.iterations == 1);
    CHECK(lm.p[0] == 1.0 && lm.p[1] == 2.0 && lm.sse == 0.0);
    CHECK(lm.sd[0] == 0.0 && lm.sd[1] == 0.0);  // s2 = 0
    // The gradient is exactly 0, so the Gauss-Newton step is 0: converged.
    const auto gn = run(ModelId::Linear, d, {1, 2}, s, Method::GaussNewton);
    CHECK(gn.status == FitStatus::Converged && gn.iterations == 1);
    CHECK(gn.p[0] == 1.0 && gn.p[1] == 2.0 && gn.sse == 0.0);
}

TEST_CASE(fit_iteration_limit, "fit: an explicit iteration limit stops both methods early") {
    const auto d = points(kMmX, kMmY);
    const auto s = default_param_settings(ModelId::MichaelisMenten);
    for (Method m : {Method::Marquardt, Method::GaussNewton}) {
        const auto r = run(ModelId::MichaelisMenten, d, {6, 10}, s, m, 3);
        CHECK(r.status == FitStatus::MaxIterations && r.usable());
        CHECK(r.message == "Maximum of 3 iterations reached before convergence.");
        CHECK(r.iterations == 3 && r.history.size() == 4);
        // Not yet at the minimum (numpy SSE 0.033330588587615576), but
        // downhill from the start.
        CHECK(r.sse > 0.033330588587615576 * (1 + 1e-6) && r.sse < r.history[0].sse);
        CHECK(sse_monotone(r));
    }
}

TEST_CASE(fit_default_limits, "fit: default iteration limits are 200 (Marquardt) and 30 (Gauss-Newton)") {
    // v proportional to [S] (y = x): the SSE keeps falling as Km and Vmax grow
    // together, so no finite minimum exists. Bounds are widened so nothing
    // stops the drift. Vmax/Km tends to the initial slope, 1.
    {
        const auto d = points({0.5, 1, 2, 3, 4}, {0.5, 1, 2, 3, 4});
        auto s = default_param_settings(ModelId::MichaelisMenten);
        for (auto& p : s) p.max = 1e300;
        const auto r = run(ModelId::MichaelisMenten, d, {1, 1}, s, Method::Marquardt);
        CHECK(r.status == FitStatus::MaxIterations && r.iterations == 200);
        CHECK(r.message == "Maximum of 200 iterations reached before convergence.");
        CHECK(r.p[0] > 1e6);
        CHECK_NEAR(r.p[1] / r.p[0], 1.0, 1e-6);
    }
    // A Hill fit to zig-zag data that drifts toward Km -> infinity (h < 1):
    // Gauss-Newton is still improving after 30 iterations (and after 60).
    {
        const auto d = points({1, 2, 3, 4, 5, 6, 7, 8}, {1, 9, 2, 10, 3, 11, 4, 12});
        const auto s = default_param_settings(ModelId::HillGt0);
        for (int limit : {0, -1}) {
            const auto r = run(ModelId::HillGt0, d, {3, 1, 8}, s, Method::GaussNewton, limit);
            CHECK(r.status == FitStatus::MaxIterations && r.iterations == 30);
            CHECK(r.message == "Maximum of 30 iterations reached before convergence.");
            CHECK(sse_monotone(r));
        }
        const auto r30 = run(ModelId::HillGt0, d, {3, 1, 8}, s, Method::GaussNewton);
        const auto r60 = run(ModelId::HillGt0, d, {3, 1, 8}, s, Method::GaussNewton, 60);
        CHECK(r60.status == FitStatus::MaxIterations && r60.iterations == 60);
        CHECK(r60.sse < r30.sse && r60.p[0] > r30.p[0]);
    }
}

TEST_CASE(fit_undefined_at_start, "fit: a model undefined at the initial estimates fails cleanly") {
    // Michaelis-Menten has a pole at [S] = -Km: the point at -2 with Km = 2.
    const auto d = points({-2, 1, 2, 4}, {1, 2, 3, 4});
    for (Method m : {Method::Marquardt, Method::GaussNewton}) {
        const auto r = run(ModelId::MichaelisMenten, d, {2, 5}, default_param_settings(ModelId::MichaelisMenten), m);
        CHECK(r.status == FitStatus::Failed && !r.usable());
        CHECK(contains(r.message, "cannot be evaluated at the initial estimates"));
        CHECK(r.iterations == 0 && r.history.empty());
        CHECK(std::isinf(r.sse) && std::isnan(r.stdDev));
        CHECK(std::isnan(r.sd[0]) && std::isnan(r.sd[1]));
        CHECK(r.p[0] == 2.0 && r.p[1] == 5.0);
    }
}

TEST_CASE(fit_undefined_derivative, "fit: a finite model with undefined derivatives (Hill, Km = 0) is reported as failure") {
    // With Km = 0 the Hill curve is the constant Vmax, but dv/dh contains
    // ln(Km) and is NaN. SSE at the start is sum (y - 12)^2 = 266.
    const auto d = points({1, 2, 4, 8}, {2, 3, 5, 6});
    auto s = default_param_settings(ModelId::HillGt0);
    s[0].min = 0.0;
    const auto lm = run(ModelId::HillGt0, d, {0, 1.8, 12}, s, Method::Marquardt);
    CHECK(lm.status == FitStatus::Failed && contains(lm.message, "initial estimates"));
    const auto gn = run(ModelId::HillGt0, d, {0, 1.8, 12}, s, Method::GaussNewton);
    CHECK(gn.status == FitStatus::Failed && contains(gn.message, "singular"));
    CHECK(gn.history.size() == 1 && gn.history[0].sse == 266.0);
    CHECK(std::isnan(gn.sd[0]) && std::isnan(gn.sd[1]) && std::isnan(gn.sd[2]));
}

TEST_CASE(fit_overflowing_normal_equations, "fit: normal equations that overflow (x = 1e160) fail in both methods") {
    // The residuals (1, 2, 0) are finite but sum x^2 = 1e320 overflows, so
    // J'J cannot be factorised for any Marquardt lambda.
    const auto d = points({1, 2, 1e160}, {1, 2, 0});
    const auto s = default_param_settings(ModelId::Linear);
    const auto lm = run(ModelId::Linear, d, {0, 0}, s, Method::Marquardt);
    CHECK(lm.status == FitStatus::Failed && contains(lm.message, "singular"));
    CHECK(lm.p[0] == 0.0 && lm.p[1] == 0.0 && lm.sse == 5.0);
    double lambda = 1e-3;
    for (size_t i = 1; i < lm.history.size(); ++i) {
        lambda *= 10.0;
        CHECK(!lm.history[i].accepted && lm.history[i].lambdaOrStep == lambda);
    }
    CHECK(lm.history.back().lambdaOrStep > 1e12);
    const auto gn = run(ModelId::Linear, d, {0, 0}, s, Method::GaussNewton);
    CHECK(gn.status == FitStatus::Failed && gn.iterations == 1 && contains(gn.message, "singular"));
}

TEST_CASE(fit_parameter_without_effect, "fit: a parameter with no effect: Marquardt fits the rest, Gauss-Newton fails") {
    // All x = 0, so the slope has a zero column in J (zero row in J'J).
    // Least squares for the intercept is the mean of y, 2, with SSE 2.
    const auto d = points({0, 0, 0}, {1, 2, 3});
    const auto s = default_param_settings(ModelId::Linear);
    const auto lm = run(ModelId::Linear, d, {0.5, 0.7}, s, Method::Marquardt);
    CHECK(lm.status == FitStatus::Converged);
    CHECK_NEAR(lm.p[0], 2.0, 1e-9);
    CHECK(lm.p[1] == 0.7);  // untouched
    CHECK_NEAR(lm.sse, 2.0, 1e-12);
    CHECK_NEAR(lm.stdDev, std::sqrt(2.0), 1e-12);  // dof = 1
    CHECK(std::isnan(lm.sd[0]) && std::isnan(lm.sd[1]));  // J'J singular
    const auto gn = run(ModelId::Linear, d, {0.5, 0.7}, s, Method::GaussNewton);
    CHECK(gn.status == FitStatus::Failed && contains(gn.message, "singular"));
}

TEST_CASE(fit_too_few_points, "fit: as many points as parameters interpolate exactly, fewer leave the fit underdetermined") {
    // Through (1, 2) and (4, 5): 1/v = (Km/Vmax)(1/S) + 1/Vmax is a straight
    // line through (1, 0.5) and (0.25, 0.2), so Km/Vmax = 0.4 and
    // 1/Vmax = 0.1: Km = 4, Vmax = 10. dof = 0, so there are no SDs.
    const auto two = points({1, 4}, {2, 5});
    const auto s = default_param_settings(ModelId::MichaelisMenten);
    for (Method m : {Method::Marquardt, Method::GaussNewton}) {
        const auto r = run(ModelId::MichaelisMenten, two, {2, 8}, s, m);
        CHECK(converged_or_minimum(r));
        CHECK_REL(r.p[0], 4.0, 1e-7);
        CHECK_REL(r.p[1], 10.0, 1e-7);
        CHECK(r.dof == 0 && r.sse < 1e-12);
        CHECK(std::isnan(r.stdDev) && std::isnan(r.sd[0]) && std::isnan(r.sd[1]));
    }
    // One point, two parameters: Marquardt (damped) still passes a curve
    // through the point; Gauss-Newton's rank-1 normal equations are singular.
    const auto one = points({2}, {3});
    const auto lm = run(ModelId::MichaelisMenten, one, {1, 1}, s, Method::Marquardt);
    CHECK(lm.usable() && lm.dof == -1);
    CHECK_NEAR(lm.p[1] * 2.0 / (lm.p[0] + 2.0), 3.0, 1e-9);
    CHECK(std::isnan(lm.stdDev) && std::isnan(lm.sd[0]));
    const auto gn = run(ModelId::MichaelisMenten, one, {1, 1}, s, Method::GaussNewton);
    CHECK(gn.status == FitStatus::Failed);
}

// ---------------------------------------------------------------------------
// Defects found by these tests (fixed: bound parameters are now held while
// the others move, and the normal equations are scaled to a unit diagonal).
// The comments describe the behaviour before the fixes.

// Both solvers clamp the full unconstrained step instead of solving for the
// free parameters with the bound-active ones held. When the parameter at its
// bound is correlated with the others, the clamped step points the wrong way.
//  * Straight line y = 2x - 1.25 with slope <= 1.5 (convex; the constrained
//    minimum is (0, 1.5) with SSE 1.25). From (1, 1), Marquardt reports "Run
//    has converged." at (-0.78125, 1.5), SSE 3.69, the point where its
//    lambda = 0.1 step has no intercept component; Gauss-Newton reports
//    "Local minimum found" at (-0.125, 1.5), SSE 1.3125.
//  * Hill with h >= 1 (models c and e) on data from h = 0.7 (rounded to 6
//    dp): the constrained minimum has h = 1, i.e. the Michaelis-Menten fit
//    (numpy: Km 1.7805522066697903, Vmax 8.46173374508674, SSE
//    0.729655797901473; profiling h over [1, 3] confirms h = 1 is optimal).
//    From (3, 1.5, 10), Marquardt stops at its 200-iteration limit with Km
//    1.956, SSE 0.829, and Gauss-Newton reports a local minimum at Km 2.668,
//    Vmax 9.433, SSE 1.92.
TEST_CASE(fit_active_bound, "fit: a parameter at its bound is held while the others reach the constrained minimum") {
    const auto line = points({1, 2, 3, 4}, {0.75, 2.75, 4.75, 6.75});
    auto ls = default_param_settings(ModelId::Linear);
    ls[1].max = 1.5;
    const auto hill = points({0.25, 0.5, 1, 2, 4, 8, 16, 32},
                             {1.493848, 2.219685, 3.166893, 4.295161, 5.501749, 6.652058, 7.634672, 8.398331});
    const auto hs = default_param_settings(ModelId::HillGe1);
    for (Method m : {Method::Marquardt, Method::GaussNewton}) {
        const auto a = run(ModelId::Linear, line, {1, 1}, ls, m);
        CHECK_NEAR(a.p[0], 0.0, 1e-6);   // actual: -0.78125 (Marquardt), -0.125 (Gauss-Newton)
        CHECK_NEAR(a.sse, 1.25, 1e-9);   // actual: 3.69, 1.3125
        const auto b = run(ModelId::HillGe1, hill, {3, 1.5, 10}, hs, m);
        CHECK(b.p[1] == 1.0);
        CHECK_REL(b.p[0], 1.7805522066697903, 1e-5);  // actual: 1.956, 2.668
        CHECK_REL(b.p[2], 8.46173374508674, 1e-5);    // actual: 8.739, 9.433
        CHECK(b.status == FitStatus::Converged);      // actual: MaxIterations, NoImprovement
    }
}

// The normal equations J'J are solved unscaled, and linalg rejects a system
// whose smallest LU pivot is below 1e-15 of the largest. With concentrations
// in molar units the Km column of J is ~1e8 times the Vmax column, so the
// pivots of J'J differ by ~1e16 even though the problem is perfectly
// conditioned once the columns are scaled (it is the same fit as Km = Vmax =
// 1 with [S]/Km = 0.25 ... 8). Exact data, a start 20% / 10% off, and both
// parameters well inside the default bounds [1e-6, 1e6]: Gauss-Newton fails
// at the first iteration ("normal equations are singular"), and Marquardt
// stalls at its 200-iteration limit with Km off by 2e-5. Km = 5e-5,
// Vmax = 2000 (pivot ratio ~1e-14) still works.
TEST_CASE(fit_scaled_normal_equations, "fit: a well-posed fit in molar units (Km 1e-4, Vmax 1e4) converges") {
    const double km = 1e-4, vmax = 1e4;
    std::vector<Point> d;
    for (double f : {0.25, 0.5, 1.0, 2.0, 4.0, 8.0}) d.push_back({f * km, vmax * f / (1 + f)});
    const auto s = default_param_settings(ModelId::MichaelisMenten);
    for (Method m : {Method::Marquardt, Method::GaussNewton}) {
        const auto r = run(ModelId::MichaelisMenten, d, {1.2 * km, 1.1 * vmax}, s, m);
        CHECK(converged_or_minimum(r));  // actual: MaxIterations (Marquardt), Failed (Gauss-Newton)
        CHECK_REL(r.p[0], km, 1e-7);
        CHECK_REL(r.p[1], vmax, 1e-7);
    }
}
