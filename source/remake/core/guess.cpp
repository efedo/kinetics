#include "core/guess.hpp"

#include <algorithm>
#include <cmath>

#include "core/stats.hpp"

namespace kin {
namespace {

struct Range {
    double yMin = 0.0;
    double yMax = 0.0;
};

Range y_range(std::span<const Point> data) {
    Range r{data.front().y, data.front().y};
    for (const auto& pt : data) {
        r.yMin = std::min(r.yMin, pt.y);
        r.yMax = std::max(r.yMax, pt.y);
    }
    return r;
}

// Half-saturation estimate for Michaelis-Menten-like curves with a known
// plateau: the X where the data first cross the half-way level.
double crossing_x(std::span<const Point> data, double level, bool rising) {
    for (size_t i = 0; i < data.size(); ++i) {
        const bool crossed = rising ? data[i].y > level : data[i].y < level;
        if (crossed) return i == 0 ? data[0].x : 0.5 * (data[i - 1].x + data[i].x);
    }
    return data.back().x;
}

double typical_x(std::span<const Point> data) {
    // Middle of the positive X values (fallback when no estimate is possible).
    std::vector<double> xs;
    for (const auto& pt : data)
        if (pt.x > 0.0) xs.push_back(pt.x);
    if (xs.empty()) return 1.0;
    return xs[xs.size() / 2];
}

}  // namespace

namespace {

// Free parameters that could not be estimated become 1; all free ones are
// then clamped to their bounds.
std::vector<double> clamp_to_bounds(std::vector<double> p, const std::vector<ParamSetting>& settings) {
    for (size_t j = 0; j < p.size(); ++j) {
        if (settings[j].fixed) continue;
        if (!std::isfinite(p[j])) p[j] = 1.0;
        p[j] = std::clamp(p[j], settings[j].min, settings[j].max);
    }
    return p;
}

}  // namespace

std::vector<double> automatic_guess(ModelId model, std::span<const Point> data,
                                    const std::vector<ParamSetting>& settings) {
    const auto& def = model_def(model);
    const size_t np = def.params.size();
    std::vector<double> p(np, 0.0);
    for (size_t j = 0; j < np; ++j)
        if (settings[j].fixed) p[j] = settings[j].value;
    if (data.empty()) return clamp_to_bounds(p, settings);

    auto idx = [&](Role r) { return param_index(model, r); };
    auto isFree = [&](std::optional<int> i) { return i && !settings[static_cast<size_t>(*i)].fixed; };
    auto setFree = [&](std::optional<int> i, double v) {
        if (isFree(i)) p[static_cast<size_t>(*i)] = v;
    };
    auto get = [&](std::optional<int> i) { return i ? p[static_cast<size_t>(*i)] : 0.0; };

    const auto iK = idx(Role::HalfSat), ih = idx(Role::HillCoeff), iV = idx(Role::Vmax),
               iV0 = idx(Role::V0), iVmin = idx(Role::Vmin);
    const Range yr = y_range(data);
    const Family fam = def.family;

    // v0: velocity without effector.
    if (fam == Family::Inhibition) setFree(iV0, yr.yMax);
    else if (fam == Family::Activation) setFree(iV0, yr.yMin != 0.0 ? yr.yMin : yr.yMax / 100.0);

    // Vmax for the Hill and activator equations (h free).
    if ((fam == Family::Hill || fam == Family::Activation) && model != ModelId::ActivatorEq1)
        setFree(iV, yr.yMax * 1.1);

    // vmin for the non-linear inhibition equations.
    if (iVmin) setFree(iVmin, yr.yMin != 0.0 ? yr.yMin * 0.99 : yr.yMax / 100.0);

    switch (model) {
        case ModelId::MichaelisMenten:
        case ModelId::ActivatorEq1: {
            const double base = (model == ModelId::ActivatorEq1) ? get(iV0) : 0.0;
            if (isFree(iV)) {
                // Weighted linearisation used by v3.51 (weights b^4).
                double a1 = 0, a2 = 0, a3 = 0, a4 = 0, a5 = 0;
                for (const auto& pt : data) {
                    if (pt.x == 0.0) continue;
                    const double b = pt.y - base;
                    const double b3 = b * b * b, b4 = b3 * b;
                    a1 += b4 / (pt.x * pt.x);
                    a2 += b4;
                    a3 += b4 / pt.x;
                    a4 += b3;
                    a5 += b3 / pt.x;
                }
                const double a6 = a1 * a4 - a3 * a5;
                if (a6 != 0.0 && std::isfinite(a6)) {
                    setFree(iK, (a5 * a2 - a3 * a4) / a6);
                    setFree(iV, base + (a1 * a2 - a3 * a3) / a6);
                } else {
                    setFree(iK, typical_x(data));
                    setFree(iV, yr.yMax * 1.05);
                }
            } else {
                const double level = base + 0.5 * (get(iV) - base);
                setFree(iK, crossing_x(data, level, true));
            }
            break;
        }
        case ModelId::InhibitionLinear:
            setFree(iK, crossing_x(data, 0.5 * get(iV0), false));
            break;
        case ModelId::Arrhenius: {
            // log10(v) is linear in 1/T, so a straight-line fit gives the
            // answer directly (v3.51 took logs of the already-logged data).
            std::vector<Point> lin;
            for (const auto& pt : data)
                if (pt.x + kKelvinOffset > 0.0) lin.push_back({1.0 / (pt.x + kKelvinOffset), pt.y});
            const auto lr = linear_regression(lin);
            if (lr.ok) {
                p[0] = lr.intercept;
                p[1] = -lr.slope * kLn10 * kGasConstantKcal;
            } else {
                p[0] = 10.0;
                p[1] = 20.0;
            }
            break;
        }
        case ModelId::Linear:
            break;
        default: {
            // Hill-type: log10((v - bottom)/(top - v)) against log10(x) has
            // slope h and intercept -h*log10(K).
            const bool inhibition = fam == Family::Inhibition;
            const double top = inhibition ? get(iVmin) : get(iV);
            const double bottom = (fam == Family::Hill) ? 0.0 : get(iV0);
            std::vector<Point> lin;
            for (const auto& pt : data) {
                if (pt.x <= 0.0 || top == pt.y) continue;
                const double ratio = (pt.y - bottom) / (top - pt.y);
                if (ratio <= 0.0 || !std::isfinite(ratio)) continue;
                lin.push_back({std::log10(pt.x), std::log10(ratio)});
            }
            const auto lr = linear_regression(lin);
            double h = 1.0, K = typical_x(data);
            if (lr.ok && lr.slope != 0.0 && std::isfinite(lr.slope)) {
                h = lr.slope;
                K = std::pow(10.0, -lr.intercept / h);
                if (model == ModelId::InhibitionI50) {
                    const double z5 = 1.0 - 2.0 * get(iVmin) / get(iV0);
                    if (z5 > 0.0) K /= std::pow(z5, 1.0 / h);
                }
            }
            if (!std::isfinite(K) || K <= 0.0) K = typical_x(data);
            setFree(ih, h);
            setFree(iK, K);
            break;
        }
    }

    return clamp_to_bounds(p, settings);
}

}  // namespace kin
