#include "core/fit.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/linalg.hpp"

namespace kin {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

struct Problem {
    ModelId model;
    const std::vector<Point>& data;
    const std::vector<ParamSetting>& settings;
    std::vector<int> free;  // indices of the parameters being fitted
};

double clamp_param(const Problem& pr, int j, double v) {
    const auto& s = pr.settings[static_cast<size_t>(j)];
    return std::clamp(v, s.min, s.max);
}

double sum_of_squares(const Problem& pr, const std::vector<double>& p) {
    double sse = 0.0;
    for (const auto& pt : pr.data) {
        const double r = pt.y - evaluate(pr.model, pt.x, p);
        if (!std::isfinite(r)) return INFINITY;
        sse += r * r;
    }
    return sse;
}

// Normal equations over the free parameters: A = J'J, g = J'r.
// Returns the sum of squares, or +inf if the model can't be evaluated.
double normal_equations(const Problem& pr, const std::vector<double>& p, Matrix& a,
                        std::vector<double>& g) {
    const int m = static_cast<int>(pr.free.size());
    a = Matrix(m, m);
    g.assign(static_cast<size_t>(m), 0.0);
    std::vector<double> d(p.size());
    double sse = 0.0;
    for (const auto& pt : pr.data) {
        const double r = pt.y - evaluate(pr.model, pt.x, p);
        if (!std::isfinite(r) || !gradient(pr.model, pt.x, p, d)) return INFINITY;
        for (int j = 0; j < m; ++j) {
            const double dj = d[static_cast<size_t>(pr.free[static_cast<size_t>(j)])];
            g[static_cast<size_t>(j)] += r * dj;
            for (int k = 0; k <= j; ++k)
                a(j, k) += dj * d[static_cast<size_t>(pr.free[static_cast<size_t>(k)])];
        }
        sse += r * r;
    }
    for (int j = 0; j < m; ++j)
        for (int k = 0; k < j; ++k) a(k, j) = a(j, k);
    return sse;
}

// Scale factors that give A a unit diagonal (1 where a parameter has no
// effect, so its zero row is left alone).
std::vector<double> unit_diagonal_scale(const Matrix& a) {
    std::vector<double> d(static_cast<size_t>(a.rows()));
    for (int j = 0; j < a.rows(); ++j) d[static_cast<size_t>(j)] = a(j, j) > 0.0 ? std::sqrt(a(j, j)) : 1.0;
    return d;
}

bool finite_diagonal(const Matrix& a) {
    for (int j = 0; j < a.rows(); ++j)
        if (!std::isfinite(a(j, j))) return false;
    return true;
}

Matrix scaled(const Matrix& a, const std::vector<double>& d) {
    Matrix s(a.rows(), a.cols());
    for (int j = 0; j < a.rows(); ++j)
        for (int k = 0; k < a.cols(); ++k)
            s(j, k) = a(j, k) / (d[static_cast<size_t>(j)] * d[static_cast<size_t>(k)]);
    return s;
}

// Solves (A + lambda diag(A)) x = g after scaling A to a unit diagonal, which
// is Marquardt's own formulation. Without the scaling, parameters in very
// different units (Km = 1e-4 M with Vmax = 1e4) give pivots about 1e16
// apart and the system looks singular. A parameter with no effect (zero
// diagonal) gets lambda itself on the diagonal, so Gauss-Newton (lambda 0)
// still reports it as singular.
bool solve_scaled(const Matrix& a, const std::vector<double>& g, double lambda, std::vector<double>& x) {
    if (!finite_diagonal(a)) return false;  // J'J overflowed: not representable
    const auto d = unit_diagonal_scale(a);
    Matrix s = scaled(a, d);
    for (int j = 0; j < a.rows(); ++j) s(j, j) = a(j, j) > 0.0 ? 1.0 + lambda : lambda;
    x.resize(g.size());
    for (size_t j = 0; j < g.size(); ++j) x[j] = g[j] / d[j];
    if (!solve(s, x)) return false;
    for (size_t j = 0; j < x.size(); ++j) x[j] /= d[j];
    return true;
}

// The step for the free parameters. A parameter at one of its bounds whose
// step would push it further out is held there, and the step is solved again
// for the others. v3.51 clamped the whole step instead, which stalls short
// of the constrained minimum when the parameters are correlated (for
// example a Hill coefficient held at h = 1 while Km and Vmax still move).
bool constrained_step(const Problem& pr, const std::vector<double>& p, const Matrix& a,
                      const std::vector<double>& g, double lambda, std::vector<double>& delta) {
    const int m = a.rows();
    std::vector<bool> held(static_cast<size_t>(m), false);
    for (;;) {
        std::vector<int> moving;
        for (int j = 0; j < m; ++j)
            if (!held[static_cast<size_t>(j)]) moving.push_back(j);
        delta.assign(static_cast<size_t>(m), 0.0);
        if (moving.empty()) return true;
        const int k = static_cast<int>(moving.size());
        Matrix sub(k, k);
        std::vector<double> sg(static_cast<size_t>(k)), x;
        for (int i = 0; i < k; ++i) {
            sg[static_cast<size_t>(i)] = g[static_cast<size_t>(moving[static_cast<size_t>(i)])];
            for (int l = 0; l < k; ++l) sub(i, l) = a(moving[static_cast<size_t>(i)], moving[static_cast<size_t>(l)]);
        }
        if (!solve_scaled(sub, sg, lambda, x)) return false;
        bool heldMore = false;
        for (int i = 0; i < k; ++i) {
            const size_t j = static_cast<size_t>(moving[static_cast<size_t>(i)]);
            const auto& s = pr.settings[static_cast<size_t>(pr.free[j])];
            const double v = p[static_cast<size_t>(pr.free[j])];
            delta[j] = x[static_cast<size_t>(i)];
            if ((v <= s.min && delta[j] < 0.0) || (v >= s.max && delta[j] > 0.0)) {
                held[j] = true;
                heldMore = true;
            }
        }
        if (!heldMore) return true;
    }
}

// Parameter standard deviations from the inverse curvature matrix at the
// solution, scaled by the residual variance.
void finish(const Problem& pr, FitResult& res) {
    const int m = static_cast<int>(pr.free.size());
    res.n = static_cast<int>(pr.data.size());
    res.nFree = m;
    res.dof = res.n - m;
    res.sd.assign(res.p.size(), 0.0);

    Matrix a;
    std::vector<double> g;
    res.sse = normal_equations(pr, res.p, a, g);
    const double s2 = (res.dof > 0 && std::isfinite(res.sse)) ? res.sse / res.dof : kNaN;
    res.stdDev = std::sqrt(s2);

    // Invert with A scaled to a unit diagonal (see solve_scaled), then undo
    // the scaling.
    Matrix inv;
    const auto d = unit_diagonal_scale(a);
    const bool invertible = m > 0 && std::isfinite(res.sse) && finite_diagonal(a) && invert(scaled(a, d), inv);
    if (invertible)
        for (int j = 0; j < m; ++j)
            for (int k = 0; k < m; ++k) inv(j, k) /= d[static_cast<size_t>(j)] * d[static_cast<size_t>(k)];
    for (int j = 0; j < m; ++j) {
        const size_t idx = static_cast<size_t>(pr.free[static_cast<size_t>(j)]);
        const double var = invertible ? inv(j, j) * s2 : kNaN;
        res.sd[idx] = (var >= 0.0) ? std::sqrt(var) : kNaN;
    }
}

void record(FitResult& res, int it, double sse, double lambda, bool accepted,
            const std::vector<double>& p) {
    res.history.push_back({it, sse, lambda, accepted, p});
}

// Marquardt-Levenberg. Differs from v3.51 in its stopping rule: the original
// stopped after two steps that changed chi-square by less than an absolute
// 0.1 and had no iteration limit; this uses a relative change (or negligible
// parameter steps) and a limit.
void marquardt(const Problem& pr, FitResult& res, int maxIter) {
    constexpr double kRelTol = 1e-10;
    constexpr double kStepTol = 1e-10;
    const int m = static_cast<int>(pr.free.size());

    std::vector<double> p = res.p;
    Matrix a;
    std::vector<double> g;
    double sse = normal_equations(pr, p, a, g);
    if (!std::isfinite(sse)) {
        res.status = FitStatus::Failed;
        res.message = "The equation cannot be evaluated at the initial estimates.";
        return;
    }
    record(res, 0, sse, 0.0, true, p);

    double lambda = 1e-3;
    int small = 0;
    int it = 0;
    res.status = FitStatus::MaxIterations;
    while (it < maxIter) {
        ++it;
        std::vector<double> delta;
        if (!constrained_step(pr, p, a, g, lambda, delta)) {
            lambda *= 10.0;
            record(res, it, sse, lambda, false, p);
            if (lambda > 1e12) {
                res.status = FitStatus::Failed;
                res.message = "The normal equations are singular: the parameters cannot be estimated from these data.";
                break;
            }
            continue;
        }
        std::vector<double> trial = p;
        double maxRelStep = 0.0;
        for (int j = 0; j < m; ++j) {
            const int idx = pr.free[static_cast<size_t>(j)];
            const size_t u = static_cast<size_t>(idx);
            trial[u] = clamp_param(pr, idx, p[u] + delta[static_cast<size_t>(j)]);
            maxRelStep = std::max(maxRelStep, std::fabs(trial[u] - p[u]) / (std::fabs(p[u]) + 1e-30));
        }
        const double sseTry = sum_of_squares(pr, trial);
        if (sseTry < sse) {
            const double relDecrease = (sse - sseTry) / std::max(sse, 1e-300);
            p = trial;
            sse = normal_equations(pr, p, a, g);
            lambda = std::max(lambda * 0.1, 1e-15);
            record(res, it, sse, lambda, true, p);
            if (relDecrease < kRelTol || maxRelStep < kStepTol) ++small;
            else small = 0;
            if (small >= 2 || sse == 0.0) {
                res.status = FitStatus::Converged;
                break;
            }
        } else {
            lambda *= 10.0;
            record(res, it, sse, lambda, false, p);
            if (maxRelStep == 0.0 || lambda > 1e10) {
                // No downhill step exists (at the minimum, or pinned at a bound).
                res.status = FitStatus::Converged;
                break;
            }
        }
    }
    res.iterations = it;
    res.p = p;
    if (res.status == FitStatus::Converged) res.message = "Run has converged.";
    else if (res.status == FitStatus::MaxIterations)
        res.message = "Maximum of " + std::to_string(maxIter) + " iterations reached before convergence.";
}

// Gauss-Newton with step halving (MATHF.CPP gnmatrix / solve_gn). Fixes from
// v3.51: the bound clamp is applied to the parameter being moved (it was
// applied to the wrong index when fixing a parameter reordered the list), the
// convergence test is applied per parameter, the reported count is the
// number of iterations (not the line-search count of the last one), and the
// covariance is evaluated at the final estimates.
void gauss_newton(const Problem& pr, FitResult& res, int maxIter) {
    constexpr double kEps = 1e-5;
    constexpr double kTau = 1e-3;
    constexpr int kMaxHalvings = 21;
    const int m = static_cast<int>(pr.free.size());

    std::vector<double> p = res.p;
    double sse = sum_of_squares(pr, p);
    if (!std::isfinite(sse)) {
        res.status = FitStatus::Failed;
        res.message = "The equation cannot be evaluated at the initial estimates.";
        return;
    }
    record(res, 0, sse, 0.0, true, p);

    int it = 0;
    res.status = FitStatus::MaxIterations;
    while (it < maxIter) {
        ++it;
        Matrix a;
        std::vector<double> g, delta;
        if (!std::isfinite(normal_equations(pr, p, a, g)) || !constrained_step(pr, p, a, g, 0.0, delta)) {
            res.status = FitStatus::Failed;
            res.message = "The normal equations are singular: the parameters cannot be estimated from these data.";
            break;
        }
        if (std::all_of(delta.begin(), delta.end(), [](double v) { return v == 0.0; })) {
            // Every parameter that could move is held at a bound: this is
            // the constrained minimum.
            record(res, it, sse, 0.0, false, p);
            res.status = FitStatus::Converged;
            res.message = "Run has converged.";
            break;
        }
        double step = 1.0;
        bool improved = false;
        std::vector<double> trial = p;
        double sseTry = sse;
        for (int c = 0; c < kMaxHalvings; ++c) {
            for (int j = 0; j < m; ++j) {
                const int idx = pr.free[static_cast<size_t>(j)];
                const size_t u = static_cast<size_t>(idx);
                trial[u] = clamp_param(pr, idx, p[u] + step * delta[static_cast<size_t>(j)]);
            }
            sseTry = sum_of_squares(pr, trial);
            if (sseTry < sse) {
                improved = true;
                break;
            }
            step *= 0.5;
        }
        if (!improved) {
            record(res, it, sse, step, false, p);
            res.status = FitStatus::NoImprovement;
            res.message = "Local minimum found: no step reduced the sum of squares further.";
            break;
        }
        const double phi2 = std::fabs(sse - sseTry) - kEps * (sse + kTau);
        double phi1 = -INFINITY;
        for (int j = 0; j < m; ++j) {
            const size_t u = static_cast<size_t>(pr.free[static_cast<size_t>(j)]);
            phi1 = std::max(phi1, std::fabs(trial[u] - p[u]) - kEps * (std::fabs(trial[u]) + kTau));
        }
        p = trial;
        sse = sseTry;
        record(res, it, sse, step, true, p);
        if (phi1 < 0.0 && phi2 < 0.0) {
            res.status = FitStatus::Converged;
            res.message = "Run has converged.";
            break;
        }
    }
    res.iterations = it;
    res.p = p;
    if (res.status == FitStatus::MaxIterations)
        res.message = "Maximum of " + std::to_string(maxIter) + " iterations reached before convergence.";
}

}  // namespace

std::vector<ParamSetting> default_param_settings(ModelId id) {
    std::vector<ParamSetting> out;
    for (const auto& def : model_def(id).params) {
        ParamSetting s;
        s.min = def.min;
        s.max = def.max;
        s.value = (def.role == Role::HillCoeff) ? 1.0 : 0.0;
        out.push_back(s);
    }
    return out;
}

FitResult fit_nonlinear(ModelId model, const std::vector<Point>& data,
                        const std::vector<double>& start,
                        const std::vector<ParamSetting>& settings, const SolverOptions& options) {
    FitResult res;
    res.method = options.method;
    res.fixed.resize(settings.size());
    Problem pr{model, data, settings, {}};
    for (size_t j = 0; j < settings.size(); ++j) {
        res.fixed[j] = settings[j].fixed;
        if (!settings[j].fixed) pr.free.push_back(static_cast<int>(j));
    }
    res.initial = start;
    res.p = start;
    for (int idx : pr.free) res.p[static_cast<size_t>(idx)] = clamp_param(pr, idx, res.p[static_cast<size_t>(idx)]);

    if (pr.free.empty()) {
        res.status = FitStatus::Converged;
        res.message = "All parameters are fixed; nothing to fit.";
    } else {
        int maxIter = options.maxIterations;
        if (maxIter <= 0) maxIter = (options.method == Method::Marquardt) ? 200 : 30;
        if (options.method == Method::Marquardt) marquardt(pr, res, maxIter);
        else gauss_newton(pr, res, maxIter);
    }
    finish(pr, res);
    return res;
}

}  // namespace kin
