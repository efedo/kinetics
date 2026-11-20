#include "core/analysis.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/guess.hpp"

namespace kin {
namespace {

std::string format(const char* fmt, double a) {
    char buf[256];
    std::snprintf(buf, sizeof buf, fmt, a);
    return buf;
}

std::string format(const char* fmt, double a, double b) {
    char buf[256];
    std::snprintf(buf, sizeof buf, fmt, a, b);
    return buf;
}

// "significantly different from X at the P<bin level" in v3.51's wording,
// followed by the statistic and the exact probability.
std::string significance(std::string_view subject, std::string_view versus, double t, int df,
                         double p) {
    const double bin = p_bin(p);
    std::string s(subject);
    if (bin >= 1.0) s += " is NOT significantly different from " + std::string(versus);
    else s += " is significantly different from " + std::string(versus) + " at the P<" + fmt_g(bin) + " level";
    s += " (t = " + fmt_g(t, 4) + ", df = " + std::to_string(df) + ", P = " + fmt_g(p, 3) + ").";
    return s;
}

}  // namespace

std::string fmt_g(double v, int precision) {
    if (std::isnan(v)) return "n/a";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*g", precision, v);
    return buf;
}

std::vector<Point> clean_data(const std::vector<Point>& data) {
    std::vector<Point> out;
    for (const auto& pt : data)
        if (std::isfinite(pt.x) && std::isfinite(pt.y)) out.push_back(pt);
    std::sort(out.begin(), out.end(), [](const Point& a, const Point& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    return out;
}

int free_param_count(ModelId model, const std::vector<ParamSetting>& params) {
    int n = 0;
    for (int j = 0; j < param_count(model); ++j)
        if (j >= static_cast<int>(params.size()) || !params[static_cast<size_t>(j)].fixed) ++n;
    return n;
}

bool too_few_points(ModelId model, const std::vector<ParamSetting>& params, size_t points) {
    return static_cast<int>(points) <= free_param_count(model, params) + 1;
}

std::string model_description(ModelId model, const std::vector<ParamSetting>& params) {
    const auto& def = model_def(model);
    std::string s(def.shortName);
    auto status = [&](Role role) -> std::string {
        const auto i = param_index(model, role);
        if (!i) return {};
        const bool fixed = static_cast<size_t>(*i) < params.size() && params[static_cast<size_t>(*i)].fixed;
        return std::string(fixed ? "/FIXED " : "/VARIABLE ") + std::string(def.params[static_cast<size_t>(*i)].name);
    };
    if (def.family == Family::Linear) {
        if (!params.empty() && params[0].fixed) s += "/FIXED intercept";
        return s;
    }
    if (def.family == Family::MichaelisMenten || def.family == Family::Hill || def.family == Family::Activation)
        s += status(Role::Vmax);
    if (def.family == Family::Inhibition) s += status(Role::Vmin);
    if (def.family == Family::Activation || def.family == Family::Inhibition) s += status(Role::V0);
    // Parameters v3.51 could not fix, listed only when fixed.
    for (Role r : {Role::HalfSat, Role::HillCoeff}) {
        const auto i = param_index(model, r);
        if (i && static_cast<size_t>(*i) < params.size() && params[static_cast<size_t>(*i)].fixed)
            s += "/FIXED " + std::string(def.params[static_cast<size_t>(*i)].name);
    }
    return s;
}

AnalysisOutcome analyse(const AnalysisRequest& request) {
    AnalysisOutcome out;
    Analysis a;
    a.request = request;
    const ModelId model = request.model;
    const auto& def = model_def(model);
    if (a.request.params.size() != def.params.size()) a.request.params = default_param_settings(model);
    auto& params = a.request.params;

    a.data = clean_data(request.data);
    if (a.data.empty()) {
        out.error = "No data entered.";
        return out;
    }
    a.xMin = a.data.front().x;
    a.xMax = a.data.back().x;
    a.yMin = a.yMax = a.data.front().y;
    for (const auto& pt : a.data) {
        a.yMin = std::min(a.yMin, pt.y);
        a.yMax = std::max(a.yMax, pt.y);
    }

    // A fixed v0 is taken from the velocity measured without effector when
    // the data include [A] = 0 or [I] = 0 (as in v3.51).
    if (const auto iv0 = param_index(model, Role::V0); iv0 && params[static_cast<size_t>(*iv0)].fixed) {
        double sum = 0.0;
        int count = 0;
        for (const auto& pt : a.data)
            if (pt.x == 0.0) {
                sum += pt.y;
                ++count;
            }
        if (count > 0) {
            params[static_cast<size_t>(*iv0)].value = sum / count;
            a.notes.push_back("v0 was fixed at the mean velocity measured at " + std::string(def.xName) +
                              " = 0 (" + fmt_g(sum / count) + ").");
        }
    }

    const int nFree = free_param_count(model, params);
    const int n = static_cast<int>(a.data.size());

    if (def.family == Family::Linear) {
        const bool fixedIntercept = params[0].fixed;
        auto lr = fixedIntercept ? regression_through_point(a.data, params[0].value) : linear_regression(a.data);
        if (!lr.ok) {
            out.error = lr.error;
            return out;
        }
        FitResult& f = a.fit;
        f.status = FitStatus::Converged;
        f.message.clear();
        f.p = {lr.intercept, lr.slope};
        f.initial = f.p;
        f.sd = {lr.sdIntercept, lr.sdSlope};
        f.fixed = {fixedIntercept, false};
        f.n = n;
        f.nFree = fixedIntercept ? 1 : 2;
        f.dof = n - f.nFree;
        double sse = 0.0;
        for (const auto& pt : a.data) sse += std::pow(pt.y - (lr.intercept + lr.slope * pt.x), 2);
        f.sse = sse;
        // Residual SD for flagging large residuals (v3.51 reused a stale
        // value from the last non-linear fit here).
        f.stdDev = f.dof > 0 ? std::sqrt(sse / f.dof) : NAN;
        a.linear = lr;
    } else {
        if (n < nFree) {
            out.error = std::to_string(n) + " data points cannot determine " + std::to_string(nFree) +
                        " parameters.";
            return out;
        }
        std::vector<double> start;
        if (request.guess == GuessMode::Automatic) {
            start = automatic_guess(model, a.data, params);
        } else {
            for (const auto& s : params) start.push_back(std::clamp(s.value, s.min, s.max));
        }
        for (size_t j = 0; j < params.size(); ++j)
            if (params[j].fixed) start[j] = params[j].value;
        SolverOptions so;
        so.method = request.method;
        so.maxIterations = request.maxIterations;
        a.fit = fit_nonlinear(model, a.data, start, params, so);
        if (a.fit.status == FitStatus::Failed) {
            out.error = a.fit.message;
            return out;
        }
    }

    // Residuals.
    a.yhat.resize(a.data.size());
    a.resid.resize(a.data.size());
    a.largeResidual.resize(a.data.size());
    for (size_t i = 0; i < a.data.size(); ++i) {
        a.yhat[i] = evaluate(model, a.data[i].x, a.fit.p);
        a.resid[i] = a.data[i].y - a.yhat[i];
        // A perfect fit (SD 0) has no outliers; v3.51 flagged every point.
        a.largeResidual[i] = std::isfinite(a.fit.stdDev) && a.fit.stdDev > 0.0 &&
                             std::fabs(a.resid[i]) >= 2.0 * a.fit.stdDev;
    }
    a.residualTests = residual_tests(model, a.data, a.fit.p);
    a.description = model_description(model, params);

    if (too_few_points(model, params, a.data.size()))
        a.notes.push_back("Only " + std::to_string(n) + " points for " + std::to_string(nFree) +
                          " free parameters: a reliable fit needs more points than parameters + 1.");
    if (a.fit.status == FitStatus::MaxIterations || a.fit.status == FitStatus::NoImprovement)
        a.notes.push_back(a.fit.message);
    for (size_t j = 0; j < a.fit.sd.size(); ++j)
        if (!a.fit.fixed[j] && std::isnan(a.fit.sd[j])) {
            a.notes.push_back("Some standard deviations could not be estimated (too few points, or the "
                              "parameters are not identifiable from these data).");
            break;
        }

    out.analysis = std::move(a);
    return out;
}

std::vector<ParamRow> Analysis::parameter_rows() const {
    std::vector<ParamRow> rows;
    const auto& def = model_def(model());
    for (size_t j = 0; j < def.params.size(); ++j)
        rows.push_back({std::string(def.params[j].name), fit.p[j], fit.sd[j], fit.fixed[j], false});
    if (model() == ModelId::Arrhenius)
        rows.push_back({"Ea (kJ/mol)", fit.p[1] * kKjPerKcal, fit.sd[1] * kKjPerKcal, false, true});
    return rows;
}

std::string Analysis::method_title() const {
    if (linear) return "LINEAR REGRESSION";
    return fit.method == Method::GaussNewton ? "GAUSS-NEWTON minimization" : "MARQUARDT-LEVENBERG minimization";
}

std::vector<std::string> Analysis::interpretation() const {
    std::vector<std::string> out;
    const ModelId m = model();
    const auto& def = model_def(m);
    const auto& p = fit.p;

    if (linear) {
        const auto& lr = *linear;
        out.push_back("Correlation coeff. = " + fmt_g(lr.r) + " +/- " + fmt_g(lr.sdR));
        out.push_back("Degrees of freedom = " + std::to_string(lr.df));
        out.push_back(significance("The SLOPE", "0", lr.tSlopeVs0, lr.df, lr.pSlopeVs0));
        out.push_back(significance("The SLOPE", "1", lr.tSlopeVs1, lr.df, lr.pSlopeVs1));
        if (!lr.interceptFixed) out.push_back(significance("The INTERCEPT", "0", lr.tIntercept, lr.df, lr.pIntercept));
        out.push_back("Regression F = " + fmt_g(lr.fRegression, 4) + " (df 1, " + std::to_string(lr.df) +
                      "), P = " + fmt_g(lr.pRegression, 3) + ".");
        if (lr.replicates && lr.dfDeviation > 0 && lr.ssPureError > 0.0) {
            out.push_back("Replicated X values: lack-of-fit F = " + fmt_g(lr.fLackOfFit, 4) + " (df " +
                          std::to_string(lr.dfDeviation) + ", " + std::to_string(lr.dfPureError) +
                          "), P = " + fmt_g(lr.pLackOfFit, 3) + ".");
        }
        return out;
    }
    if (def.family == Family::Arrhenius) return out;

    const auto iK = param_index(m, Role::HalfSat);
    const auto iV = param_index(m, Role::Vmax);
    const auto iV0 = param_index(m, Role::V0);
    const auto ih = param_index(m, Role::HillCoeff);
    const bool inhibition = def.family == Family::Inhibition;
    const std::string xName(def.xName);

    if (iK) {
        const double K = p[static_cast<size_t>(*iK)];
        out.push_back(xName + " varies from " + format("%.2f", xMin / K) + " to " + format("%.2f", xMax / K) +
                      " of " + std::string(def.params[static_cast<size_t>(*iK)].name) + " value.");
    }
    if (!inhibition && iV) {
        const double V = p[static_cast<size_t>(*iV)];
        out.push_back(format("v varies from %.2f to %.2f of Vmax value.", yMin / V, yMax / V));
    } else if (inhibition && iV0) {
        const double v0 = p[static_cast<size_t>(*iV0)];
        out.push_back(format("v varies from %.2f to %.2f of v0 value.", yMax / v0, yMin / v0));
    }
    if (def.family == Family::Activation && iV && iV0) {
        const double v0 = p[static_cast<size_t>(*iV0)];
        out.push_back("Fold activation: " + (v0 != 0.0 ? fmt_g(p[static_cast<size_t>(*iV)] / v0) : std::string("infinite")));
    }
    if (ih) {
        const size_t j = static_cast<size_t>(*ih);
        if (fit.fixed[j]) {
            out.push_back("The Hill coefficient was fixed, so it was not tested against 1.");
        } else if (std::isfinite(fit.sd[j]) && fit.sd[j] > 0.0 && fit.dof > 0) {
            const double t = std::fabs(p[j] - 1.0) / fit.sd[j];
            out.push_back(significance("Hill coeff.", "1", t, fit.dof, t_two_tailed_p(t, fit.dof)));
        }
    }
    return out;
}

std::vector<std::string> Analysis::residual_test_lines() const {
    std::vector<std::string> out;
    const auto& rt = residualTests;
    out.push_back("Sequence of residuals = " + rt.sequence);
    if (!rt.runsTestable) out.push_back("Too few data points to test sequence randomness.");
    else
        out.push_back(std::string("The sequence of residuals is ") + (rt.runsRandom ? "" : "NOT ") +
                      "random at the P < 0.05 level (" + std::to_string(rt.runs) + " runs, P = " +
                      fmt_g(rt.runsP, 3) + ").");
    out.push_back(std::string("The # of +ve and -ve residuals is ") + (rt.signsRandom ? "" : "NOT ") +
                  "random at the P < 0.05 level (" + std::to_string(rt.positives) + " +, " +
                  std::to_string(rt.negatives) + " -, P = " + fmt_g(rt.signP, 3) + ").");
    return out;
}

}  // namespace kin
