#pragma once
// Non-linear least squares: Marquardt-Levenberg and Gauss-Newton, with
// parameter bounds and fixed parameters (ports of MATHF.CPP solve_mrq /
// solve_gn, in double precision and with the original's defects fixed).

#include <string>
#include <vector>

#include "core/models.hpp"

namespace kin {

enum class Method { Marquardt = 1, GaussNewton = 2 };

struct ParamSetting {
    bool fixed = false;
    double value = 0.0;  // fixed value, or the manual initial estimate
    double min = 0.0;
    double max = 0.0;
};

std::vector<ParamSetting> default_param_settings(ModelId id);

struct SolverOptions {
    Method method = Method::Marquardt;
    int maxIterations = 0;  // 0: 200 for Marquardt, 30 for Gauss-Newton (as in v3.51)
};

enum class FitStatus {
    Converged,      // convergence criteria met
    NoImprovement,  // no step reduces the sum of squares any further
    MaxIterations,  // iteration limit reached
    Failed,         // model or normal equations could not be evaluated
};

struct IterationRecord {
    int iteration = 0;
    double sse = 0.0;
    double lambdaOrStep = 0.0;  // Marquardt lambda, or Gauss-Newton step fraction
    bool accepted = true;
    std::vector<double> p;
};

struct FitResult {
    FitStatus status = FitStatus::Failed;
    std::string message;
    Method method = Method::Marquardt;
    int iterations = 0;
    std::vector<double> initial;
    std::vector<double> p;
    std::vector<double> sd;   // 0 for fixed parameters, NaN if not estimable
    std::vector<bool> fixed;
    double sse = 0.0;         // residual sum of squares
    double stdDev = 0.0;      // sqrt(sse / dof); NaN when dof <= 0
    int n = 0;
    int nFree = 0;
    int dof = 0;
    std::vector<IterationRecord> history;

    bool usable() const { return status != FitStatus::Failed; }
};

// Fits `model` to `data` starting from `start` (full parameter vector; fixed
// entries are kept at their start value). Bounds come from `settings`.
FitResult fit_nonlinear(ModelId model, const std::vector<Point>& data,
                        const std::vector<double>& start,
                        const std::vector<ParamSetting>& settings, const SolverOptions& options);

}  // namespace kin
