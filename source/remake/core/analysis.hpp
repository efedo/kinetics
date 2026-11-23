#pragma once
// One complete analysis: validate the data, build initial estimates, fit,
// and compute the statistics shown on screen and in reports
// (KIN_CD.CPP calculate_parameters and the print_*_results routines).

#include <optional>
#include <string>
#include <vector>

#include "core/fit.hpp"
#include "core/models.hpp"
#include "core/stats.hpp"

namespace kin {

enum class GuessMode { Automatic = 1, Manual = 2 };

struct AnalysisRequest {
    ModelId model = ModelId::MichaelisMenten;
    std::vector<Point> data;  // any order; rows with a NaN coordinate are ignored
    std::vector<ParamSetting> params;
    Method method = Method::Marquardt;
    GuessMode guess = GuessMode::Automatic;
    int maxIterations = 0;    // 0: method default
};

struct ParamRow {
    std::string name;
    double value = 0.0;
    double sd = 0.0;
    bool fixed = false;
    bool derived = false;  // e.g. Ea in kJ/mol, computed from Ea in kcal/mol
};

// A free parameter that ended the fit at one of its bounds.
struct BoundHit {
    int index = 0;      // into the model's parameters
    bool upper = false;
    double bound = 0.0;
};

struct Analysis {
    AnalysisRequest request;      // parameters as actually used
    std::vector<Point> data;      // valid points sorted by x, then y
    double xMin = 0, xMax = 0, yMin = 0, yMax = 0;
    FitResult fit;                // for linear regression: filled from `linear`
    std::optional<LinearRegression> linear;
    std::vector<double> yhat;
    std::vector<double> resid;
    std::vector<bool> largeResidual;  // |resid| >= 2 standard deviations
    ResidualTests residualTests;
    std::vector<std::string> notes;
    std::string description;      // e.g. "Michaelis-Menten/VARIABLE Vmax"

    ModelId model() const { return request.model; }
    std::vector<ParamRow> parameter_rows() const;
    // Title of the result block, e.g. "MARQUARDT-LEVENBERG minimization".
    std::string method_title() const;
    // Interpretation lines from v3.51: substrate and velocity range relative
    // to the fitted constants, fold activation, Hill-coefficient test, and
    // for linear regression the t and F tests.
    std::vector<std::string> interpretation() const;
    std::vector<std::string> residual_test_lines() const;
    // Free parameters that ended at (within a relative 1e-9 of) a bound: the
    // data did not determine them. v3.51 did not report this, so the reports
    // leave it out; the modern interface shows it.
    std::vector<BoundHit> parameters_at_bounds() const;
};

struct AnalysisOutcome {
    std::optional<Analysis> analysis;
    std::string error;
};

AnalysisOutcome analyse(const AnalysisRequest& request);

// Valid points sorted by x then y.
std::vector<Point> clean_data(const std::vector<Point>& data);

int free_param_count(ModelId model, const std::vector<ParamSetting>& params);

// v3.51 warned when #points <= #free parameters + 1.
bool too_few_points(ModelId model, const std::vector<ParamSetting>& params, size_t points);

std::string model_description(ModelId model, const std::vector<ParamSetting>& params);

// "%g"-style number formatting used throughout the reports.
std::string fmt_g(double v, int precision = 6);

}  // namespace kin
