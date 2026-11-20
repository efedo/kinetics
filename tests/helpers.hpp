#pragma once
// Shared helpers for the core tests: model cases with known parameters,
// exact and slightly perturbed data, and analysis requests.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/analysis.hpp"
#include "core/models.hpp"

namespace test {

using namespace kin;

inline const std::filesystem::path kExamples = KINETICS_EXAMPLES_DIR;

struct Case {
    ModelId model;
    std::vector<double> p;
    std::vector<double> x;
};

inline std::vector<Case> model_cases() {
    const std::vector<double> conc = {0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16, 24};
    const std::vector<double> inh = {0, 0.5, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32};
    return {
        {ModelId::MichaelisMenten, {2.5, 10}, conc},
        {ModelId::HillGt0, {3, 1.8, 12}, conc},
        {ModelId::HillGe1, {3, 1.8, 12}, conc},
        {ModelId::ActivatorGt0, {3, 1.6, 12, 2}, inh},
        {ModelId::ActivatorGe1, {3, 1.6, 12, 2}, inh},
        {ModelId::ActivatorEq1, {3, 12, 2}, inh},
        {ModelId::InhibitionLinear, {4, 20}, inh},
        {ModelId::InhibitionI50, {4, 1.5, 3, 20}, inh},
        {ModelId::InhibitionK50, {4, 1.5, 3, 20}, inh},
        {ModelId::Arrhenius, {9, 12}, {5, 10, 15, 20, 25, 30, 35, 40}},
        {ModelId::Linear, {1.5, 2}, {0, 1, 2, 3, 4, 5}},
    };
}

inline std::vector<Point> exact_data(const Case& c) {
    std::vector<Point> pts;
    for (double x : c.x) pts.push_back({x, evaluate(c.model, x, c.p)});
    return pts;
}

// Small deterministic "noise" so that residual SDs are non-zero.
inline std::vector<Point> wobbly_data(const Case& c, double rel) {
    auto pts = exact_data(c);
    for (size_t i = 0; i < pts.size(); ++i) pts[i].y *= 1.0 + rel * std::sin(1.7 * static_cast<double>(i) + 0.3);
    return pts;
}

inline Analysis must_analyse(const AnalysisRequest& req) {
    auto out = analyse(req);
    if (!out.analysis) {
        std::printf("analyse() failed: %s\n", out.error.c_str());
        std::abort();
    }
    return *out.analysis;
}

inline AnalysisRequest request(ModelId m, std::vector<Point> data, Method method = Method::Marquardt) {
    AnalysisRequest r;
    r.model = m;
    r.data = std::move(data);
    r.params = default_param_settings(m);
    r.method = method;
    return r;
}

}  // namespace test
