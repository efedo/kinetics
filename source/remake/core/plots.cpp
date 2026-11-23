#include "core/plots.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kin {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr int kCurveSamples = 400;

std::string log_label(std::string_view name) {
    if (!name.empty() && name.front() == '[') return "log10" + std::string(name);
    return "log10(" + std::string(name) + ")";
}

std::string half_sat_name(ModelId m) {
    const auto i = param_index(m, Role::HalfSat);
    return i ? std::string(model_def(m).params[static_cast<size_t>(*i)].name) : "Km";
}

bool is_inhibition(ModelId m) { return model_def(m).family == Family::Inhibition; }

struct Ctx {
    ModelId m;
    const std::vector<double>& p;
    double K;  // half-saturation constant
    double V;  // Vmax, or v0 for the inhibition equations (v3.51 get_yplot)
};

Ctx make_ctx(const Analysis& a) {
    const ModelId m = a.model();
    const auto& p = a.fit.p;
    const auto iK = param_index(m, Role::HalfSat);
    auto iV = param_index(m, Role::Vmax);
    if (!iV) iV = param_index(m, Role::V0);
    return {m, p, iK ? p[static_cast<size_t>(*iK)] : 1.0, iV ? p[static_cast<size_t>(*iV)] : 1.0};
}

double tx(PlotType t, const Ctx& c, double x, double y) {
    switch (t) {
        case PlotType::Normal:
        case PlotType::HanesWoolf:
        case PlotType::Job: return x;
        case PlotType::LineweaverBurk: return x != 0.0 ? 1.0 / x : kNaN;
        case PlotType::EadieHofstee: return y;
        case PlotType::Hill:
        case PlotType::LogLog: return x > 0.0 ? std::log10(x) : kNaN;
        case PlotType::Saturation: return (x > 0.0 && c.K > 0.0) ? std::log10(x / c.K) : kNaN;
        case PlotType::Arrhenius: return x > -kKelvinOffset ? 1.0 / (x + kKelvinOffset) : kNaN;
        case PlotType::Residual: return kNaN;  // GCOV_EXCL_LINE: make_plot draws residuals itself
    }
    return kNaN;
}

double ty(PlotType t, const Ctx& c, double x, double y) {
    switch (t) {
        case PlotType::Normal:
        case PlotType::Arrhenius: return y;
        case PlotType::LineweaverBurk: return y != 0.0 ? 1.0 / y : kNaN;
        case PlotType::HanesWoolf: return y != 0.0 ? x / y : kNaN;
        case PlotType::EadieHofstee: return x != 0.0 ? y / x : kNaN;
        case PlotType::Hill: return (y > 0.0 && y < c.V) ? std::log10((c.V - y) / y) : kNaN;
        case PlotType::LogLog: return y > 0.0 ? std::log10(y) : kNaN;
        case PlotType::Saturation: return c.V > 0.0 ? y / c.V : kNaN;
        case PlotType::Job: return y > 0.0 ? c.V / y : kNaN;
        case PlotType::Residual: return kNaN;  // GCOV_EXCL_LINE: make_plot draws residuals itself
    }
    return kNaN;
}

// Raw x for a plot X coordinate (not used for Eadie-Hofstee, whose X is v).
double inverse_x(PlotType t, const Ctx& c, double X) {
    switch (t) {
        case PlotType::Normal:
        case PlotType::HanesWoolf:
        case PlotType::Job: return X;
        case PlotType::LineweaverBurk: return X != 0.0 ? 1.0 / X : kNaN;
        case PlotType::Hill:
        case PlotType::LogLog: return std::pow(10.0, X);
        case PlotType::Saturation: return c.K * std::pow(10.0, X);
        case PlotType::Arrhenius: return X != 0.0 ? 1.0 / X - kKelvinOffset : kNaN;
        default: return kNaN;  // GCOV_EXCL_LINE: not used for Eadie-Hofstee or residual plots
    }
}

// Where the fitted curve meets Y = 0, in plot units (v3.51 return_x_at_y0,
// which transformed some of these values twice).
std::optional<double> x_intercept(PlotType t, const Ctx& c) {
    if (is_inhibition(c.m) || c.m == ModelId::Arrhenius) return std::nullopt;
    std::optional<double> raw;
    switch (t) {
        case PlotType::Normal:
            raw = inverse(c.m, 0.0, c.p);
            return raw;
        case PlotType::LineweaverBurk:
            if (c.K != 0.0) return -1.0 / c.K;
            return std::nullopt;
        case PlotType::HanesWoolf: return -c.K;
        case PlotType::EadieHofstee: return c.V;
        case PlotType::Hill:
            raw = inverse(c.m, 0.5 * c.V, c.p);
            break;
        case PlotType::LogLog:
            raw = inverse(c.m, 1.0, c.p);
            break;
        default: return std::nullopt;
    }
    if (raw && *raw > 0.0) return std::log10(*raw);
    return std::nullopt;
}

void include(double v, double& lo, double& hi) {
    if (!std::isfinite(v)) return;
    lo = std::min(lo, v);
    hi = std::max(hi, v);
}

void pad(double& lo, double& hi) {
    if (!(lo <= hi)) {
        lo = 0.0;
        hi = 1.0;
        return;
    }
    double span = hi - lo;
    if (span == 0.0) span = (lo != 0.0) ? std::fabs(lo) * 0.2 : 1.0;
    lo -= 0.05 * span;
    hi += 0.05 * span;
}

}  // namespace

std::vector<PlotType> available_plots(ModelId model) {
    using P = PlotType;
    switch (model_def(model).family) {
        case Family::MichaelisMenten:
        case Family::Hill:
        case Family::Activation:
            return {P::Normal, P::LineweaverBurk, P::HanesWoolf, P::EadieHofstee, P::Hill,
                    P::LogLog, P::Saturation, P::Residual};
        case Family::Inhibition: return {P::Normal, P::Saturation, P::Job, P::Residual};
        case Family::Arrhenius: return {P::Normal, P::Arrhenius, P::Residual};
        case Family::Linear: return {P::Normal, P::LogLog, P::Residual};
    }
    return {P::Normal};  // GCOV_EXCL_LINE: every family is handled above
}

std::string plot_x_label(PlotType t, ModelId m) {
    const auto& def = model_def(m);
    const std::string x(def.xName), y(def.yName);
    switch (t) {
        case PlotType::Normal:
        case PlotType::HanesWoolf:
        case PlotType::Job: return x;
        case PlotType::LineweaverBurk: return "1/" + x;
        case PlotType::EadieHofstee: return y;
        case PlotType::Hill:
        case PlotType::LogLog: return log_label(x);
        case PlotType::Saturation: return "log10(" + x + "/" + half_sat_name(m) + ")";
        case PlotType::Arrhenius: return "1/T (1/K)";
        case PlotType::Residual: return "Fitted " + y;
    }
    return x;
}

std::string plot_y_label(PlotType t, ModelId m) {
    const auto& def = model_def(m);
    const std::string x(def.xName), y(def.yName);
    switch (t) {
        case PlotType::Normal:
        case PlotType::Arrhenius: return y;
        case PlotType::LineweaverBurk: return "1/" + y;
        case PlotType::HanesWoolf: return x + "/" + y;
        case PlotType::EadieHofstee: return y + "/" + x;
        case PlotType::Hill: return "log10((Vmax - v)/v)";
        case PlotType::LogLog: return log_label(y);
        case PlotType::Saturation: return is_inhibition(m) ? "v/v0" : "v/Vmax";
        case PlotType::Job: return "v0/v";
        case PlotType::Residual: return "Residual (" + y + " - fitted)";
    }
    return y;
}

std::string plot_name(PlotType t, ModelId m) {
    const std::string axes = " (" + plot_y_label(t, m) + " vs. " + plot_x_label(t, m) + ")";
    switch (t) {
        case PlotType::Normal: return "Normal" + axes;
        case PlotType::LineweaverBurk: return "Double-reciprocal" + axes;
        case PlotType::HanesWoolf: return "Hanes-Woolf" + axes;
        case PlotType::EadieHofstee: return "Eadie-Hofstee" + axes;
        case PlotType::Hill: return "Hill" + axes;
        case PlotType::LogLog: return "Log-log" + axes;
        case PlotType::Saturation: return "Saturation" + axes;
        case PlotType::Job: return "Job plot for inhibition" + axes;
        case PlotType::Arrhenius: return "Arrhenius" + axes;
        case PlotType::Residual: return "Residuals" + axes;
    }
    return "Plot";
}

PlotOptions default_plot_options(PlotType t, ModelId m) {
    PlotOptions o;  // v3.51: origin on, line extended to both axes
    const Family f = model_def(m).family;
    if (f == Family::Hill || f == Family::Activation) {
        if (t == PlotType::HanesWoolf) o.extendToXIntercept = false;
        if (t == PlotType::EadieHofstee) o.extendToXIntercept = o.extendToYAxis = false;
    } else if (f == Family::Inhibition) {
        if (t == PlotType::Job) o.extendToXIntercept = false;
    } else if (f == Family::Arrhenius) {
        o.extendToXIntercept = o.extendToYAxis = false;
        o.includeOrigin = false;  // the origin is far from 1/T data
    }
    if (t == PlotType::Saturation) o.extendToXIntercept = false;
    if (t == PlotType::Residual) o.extendToXIntercept = o.extendToYAxis = false;
    return o;
}

PlotData make_plot(const Analysis& a, PlotType t, const PlotOptions& opt) {
    PlotData pd;
    const ModelId m = a.model();
    pd.xLabel = plot_x_label(t, m);
    pd.yLabel = plot_y_label(t, m);
    const Ctx c = make_ctx(a);

    for (size_t i = 0; i < a.data.size(); ++i) {
        const auto& pt = a.data[i];
        if (t == PlotType::Residual) {
            pd.px.push_back(a.yhat[i]);
            pd.py.push_back(a.resid[i]);
        } else {
            pd.px.push_back(tx(t, c, pt.x, pt.y));
            pd.py.push_back(ty(t, c, pt.x, pt.y));
        }
        pd.labels.push_back(static_cast<int>(i) + 1);
    }

    double xlo = INFINITY, xhi = -INFINITY, ylo = INFINITY, yhi = -INFINITY;
    for (size_t i = 0; i < pd.px.size(); ++i) {
        if (!std::isfinite(pd.px[i]) || !std::isfinite(pd.py[i])) continue;
        include(pd.px[i], xlo, xhi);
        include(pd.py[i], ylo, yhi);
    }

    if (t == PlotType::Residual) {
        pd.zeroLine = true;
    } else if (xlo <= xhi) {
        double xs = xlo, xe = xhi;
        if (opt.extendToYAxis) {
            xs = std::min(xs, 0.0);
            xe = std::max(xe, 0.0);
        }
        pd.xIntercept = x_intercept(t, c);
        if (opt.extendToXIntercept && pd.xIntercept && std::isfinite(*pd.xIntercept)) {
            xs = std::min(xs, *pd.xIntercept);
            xe = std::max(xe, *pd.xIntercept);
        }
        const double dataYSpan = std::max(yhi - ylo, std::fabs(yhi) * 1e-3 + 1e-300);
        double cylo = INFINITY, cyhi = -INFINITY, cxlo = INFINITY, cxhi = -INFINITY;
        for (int k = 0; k < kCurveSamples; ++k) {
            const double X = xs + (xe - xs) * k / (kCurveSamples - 1);
            double x, y;
            if (t == PlotType::EadieHofstee) {
                y = X;
                const auto inv = inverse(m, y, c.p);
                x = inv ? *inv : kNaN;
            } else {
                x = inverse_x(t, c, X);
                y = std::isfinite(x) ? evaluate(m, x, c.p) : kNaN;
            }
            double cx = std::isfinite(x) && std::isfinite(y) ? tx(t, c, x, y) : kNaN;
            double cy = std::isfinite(cx) ? ty(t, c, x, y) : kNaN;
            if (!std::isfinite(cy)) cx = cy = kNaN;
            pd.cx.push_back(cx);
            pd.cy.push_back(cy);
            // Let the curve widen the axes, but not without limit where it
            // runs off towards an asymptote.
            if (std::isfinite(cy) && cy > ylo - 2.0 * dataYSpan && cy < yhi + 2.0 * dataYSpan) {
                include(cy, cylo, cyhi);
                include(cx, cxlo, cxhi);
            }
        }
        include(cylo, ylo, yhi);
        include(cyhi, ylo, yhi);
        include(cxlo, xlo, xhi);
        include(cxhi, xlo, xhi);
    }

    if (opt.includeOrigin) {
        include(0.0, xlo, xhi);
        include(0.0, ylo, yhi);
    }
    pad(xlo, xhi);
    pad(ylo, yhi);
    pd.xmin = xlo;
    pd.xmax = xhi;
    pd.ymin = ylo;
    pd.ymax = yhi;
    return pd;
}

std::pair<double, double> plot_coordinates(const Analysis& a, PlotType t, double x, double y) {
    if (t == PlotType::Residual) return {kNaN, kNaN};
    const Ctx c = make_ctx(a);
    return {tx(t, c, x, y), ty(t, c, x, y)};
}

}  // namespace kin
