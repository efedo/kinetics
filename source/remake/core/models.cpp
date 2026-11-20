#include "core/models.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kin {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Bounds used by the original program (KIN_EDIT.CPP select_parameters).
constexpr double kLo = 1e-6;
constexpr double kHi = 1e6;

const std::vector<ModelDef>& models() {
    static const std::vector<ModelDef> defs = {
        {ModelId::MichaelisMenten, 'a', "Michaelis-Menten equation", "Michaelis-Menten",
         "v = Vmax·[S] / (Km + [S])", Family::MichaelisMenten, "[S]", "v",
         {{"Km", Role::HalfSat, kLo, kHi, true}, {"Vmax", Role::Vmax, kLo, kHi, true}}},

        {ModelId::HillGt0, 'b', "Hill equation with h > 0", "Hill eqn (>0)",
         "v = Vmax·[S]^h / (Km^h + [S]^h)", Family::Hill, "[S]", "v",
         {{"Km", Role::HalfSat, kLo, kHi, true},
          {"h", Role::HillCoeff, 0.01, 20.0, true},
          {"Vmax", Role::Vmax, kLo, kHi, true}}},

        {ModelId::HillGe1, 'c', "Hill equation with h ≥ 1", "Hill eqn (≥1)",
         "v = Vmax·[S]^h / (Km^h + [S]^h),  h ≥ 1", Family::Hill, "[S]", "v",
         {{"Km", Role::HalfSat, kLo, kHi, true},
          {"h", Role::HillCoeff, 1.0, 20.0, true},
          {"Vmax", Role::Vmax, kLo, kHi, true}}},

        {ModelId::ActivatorGt0, 'd', "Activator equation with h > 0", "Activation eqn (>0)",
         "v = v0 + (Vmax − v0)·[A]^h / (Ka^h + [A]^h)", Family::Activation, "[A]", "v",
         {{"Ka", Role::HalfSat, kLo, kHi, true},
          {"h", Role::HillCoeff, 0.01, 20.0, true},
          {"Vmax", Role::Vmax, kLo, kHi, true},
          {"v0", Role::V0, kLo, kHi, true}}},

        {ModelId::ActivatorGe1, 'e', "Activator equation with h ≥ 1", "Activation eqn (≥1)",
         "v = v0 + (Vmax − v0)·[A]^h / (Ka^h + [A]^h),  h ≥ 1", Family::Activation, "[A]", "v",
         {{"Ka", Role::HalfSat, kLo, kHi, true},
          {"h", Role::HillCoeff, 1.0, 20.0, true},
          {"Vmax", Role::Vmax, kLo, kHi, true},
          {"v0", Role::V0, kLo, kHi, true}}},

        {ModelId::ActivatorEq1, 'f', "Activator equation with h = 1", "Activation eqn (=1)",
         "v = v0 + (Vmax − v0)·[A] / (Ka + [A])", Family::Activation, "[A]", "v",
         {{"Ka", Role::HalfSat, kLo, kHi, true},
          {"Vmax", Role::Vmax, kLo, kHi, true},
          {"v0", Role::V0, kLo, kHi, true}}},

        {ModelId::InhibitionLinear, 'g', "Linear inhibition equation", "Linear inhibition",
         "v = v0·I50 / (I50 + [I])", Family::Inhibition, "[I]", "v",
         {{"I50", Role::HalfSat, kLo, kHi, true}, {"v0", Role::V0, kLo, kHi, true}}},

        {ModelId::InhibitionI50, 'h', "Non-linear inhibition equation: gives I50",
         "Non-linear inhibition (I50)",
         "v = v0 − (v0 − vmin)·[I]^h / ([I]^h + I50^h·(1 − 2·vmin/v0))", Family::Inhibition,
         "[I]", "v",
         {{"I50", Role::HalfSat, kLo, kHi, true},
          {"h", Role::HillCoeff, 0.01, 20.0, true},
          {"vmin", Role::Vmin, kLo, kHi, true},
          {"v0", Role::V0, kLo, kHi, true}}},

        {ModelId::InhibitionK50, 'i', "Non-linear inhibition equation: gives K50",
         "Non-linear inhibition (K50)",
         "v = v0 − (v0 − vmin)·[I]^h / (K50^h + [I]^h)", Family::Inhibition, "[I]", "v",
         {{"K50", Role::HalfSat, kLo, kHi, true},
          {"h", Role::HillCoeff, 0.01, 20.0, true},
          {"vmin", Role::Vmin, kLo, kHi, true},
          {"v0", Role::V0, kLo, kHi, true}}},

        // The original lower bound of 1e-6 on log10(A) rejected valid fits
        // whenever velocities were measured in large units; widened here.
        {ModelId::Arrhenius, 'j', "Arrhenius equation (temperature curve)", "Arrhenius eqn",
         "log10(v) = log10(A) − Ea / (2.303·R·(T + 273.15))", Family::Arrhenius, "T (°C)",
         "log10(v)",
         {{"log10(A)", Role::LogA, -100.0, 100.0, false},
          {"Ea (kcal/mol)", Role::Ea, kLo, 200.0, false}}},

        {ModelId::Linear, 'k', "Linear regression (Y vs. X)", "Linear regression",
         "Y = Intercept + Slope·X", Family::Linear, "X", "Y",
         {{"Intercept", Role::Intercept, -kHi, kHi, true},
          {"Slope", Role::Slope, -kHi, kHi, false}}},
    };
    return defs;
}

double sq(double v) { return v * v; }

// Shared pieces of the Hill-type equations: x^h, K^h and the logarithms that
// appear in d/dh. At x == 0 the limits x^h -> 0 and x^h*ln(x) -> 0 are used,
// so derivatives stay finite where the original substituted a tiny x.
struct HillTerms {
    double z1;    // x^h (sign preserved)
    double z2;    // K^h
    double lnx;   // ln|x|, 0 when x == 0
    double lnK;   // ln K
    double dz2;   // d(K^h)/dK = h K^(h-1)
};

HillTerms hill_terms(double x, double K, double h) {
    HillTerms t{};
    t.z1 = signed_pow(x, h);
    t.z2 = std::pow(K, h);
    t.lnx = (x == 0.0) ? 0.0 : std::log(std::fabs(x));
    t.lnK = std::log(K);
    t.dz2 = h * std::pow(K, h - 1.0);
    return t;
}

double arrhenius_factor(double tCelsius) {
    // d(log10 v)/dEa = -1 / (ln10 R T)
    const double tK = tCelsius + kKelvinOffset;
    return 1.0 / (kLn10 * kGasConstantKcal * tK);
}

}  // namespace

std::span<const ModelDef> all_models() { return models(); }

const ModelDef& model_def(ModelId id) {
    const int i = static_cast<int>(id) - 1;
    if (i < 0 || i >= kModelCount) throw std::out_of_range("unknown model");
    return models()[static_cast<size_t>(i)];
}

std::optional<ModelId> model_from_letter(char letter) {
    for (const auto& m : models())
        if (m.letter == letter) return m.id;
    return std::nullopt;
}

std::optional<int> param_index(ModelId id, Role role) {
    const auto& ps = model_def(id).params;
    for (size_t i = 0; i < ps.size(); ++i)
        if (ps[i].role == role) return static_cast<int>(i);
    return std::nullopt;
}

bool has_hill_coefficient(ModelId id) { return param_index(id, Role::HillCoeff).has_value(); }

double signed_pow(double base, double exponent) {
    if (base == 0.0) return 0.0;
    if (base > 0.0) return std::pow(base, exponent);
    return -std::pow(-base, exponent);
}

double evaluate(ModelId id, double x, std::span<const double> p) {
    switch (id) {
        case ModelId::MichaelisMenten: {
            const double den = x + p[0];
            return den != 0.0 ? x * p[1] / den : kNaN;
        }
        case ModelId::ActivatorEq1: {
            const double den = x + p[0];
            return den != 0.0 ? p[2] + (p[1] - p[2]) * x / den : kNaN;
        }
        case ModelId::InhibitionLinear: {
            const double den = x + p[0];
            return den != 0.0 ? p[1] * p[0] / den : kNaN;
        }
        case ModelId::HillGt0:
        case ModelId::HillGe1: {
            const double z1 = signed_pow(x, p[1]);
            const double den = z1 + std::pow(p[0], p[1]);
            return den != 0.0 ? p[2] * z1 / den : kNaN;
        }
        case ModelId::ActivatorGt0:
        case ModelId::ActivatorGe1: {
            const double z1 = signed_pow(x, p[1]);
            const double den = z1 + std::pow(p[0], p[1]);
            return den != 0.0 ? p[3] + (p[2] - p[3]) * z1 / den : kNaN;
        }
        case ModelId::InhibitionK50: {
            const double z1 = signed_pow(x, p[1]);
            const double den = z1 + std::pow(p[0], p[1]);
            return den != 0.0 ? p[3] - (p[3] - p[2]) * z1 / den : kNaN;
        }
        case ModelId::InhibitionI50: {
            if (p[3] == 0.0) return kNaN;
            const double z1 = signed_pow(x, p[1]);
            const double den = z1 + std::pow(p[0], p[1]) * (1.0 - 2.0 * p[2] / p[3]);
            return den != 0.0 ? p[3] - (p[3] - p[2]) * z1 / den : kNaN;
        }
        case ModelId::Arrhenius: {
            if (x + kKelvinOffset <= 0.0) return kNaN;
            return p[0] - p[1] * arrhenius_factor(x);
        }
        case ModelId::Linear:
            return p[0] + p[1] * x;
    }
    return kNaN;
}

bool gradient(ModelId id, double x, std::span<const double> p, std::span<double> d) {
    switch (id) {
        case ModelId::MichaelisMenten: {
            const double den = x + p[0];
            d[0] = -p[1] * x / sq(den);
            d[1] = x / den;
            break;
        }
        case ModelId::ActivatorEq1: {
            const double den = x + p[0];
            d[0] = (p[2] - p[1]) * x / sq(den);
            d[1] = x / den;
            d[2] = 1.0 - d[1];
            break;
        }
        case ModelId::InhibitionLinear: {
            const double den = x + p[0];
            d[0] = p[1] * x / sq(den);
            d[1] = p[0] / den;
            break;
        }
        case ModelId::HillGt0:
        case ModelId::HillGe1: {
            const auto t = hill_terms(x, p[0], p[1]);
            const double den = t.z1 + t.z2;
            d[0] = -p[2] * t.z1 * t.dz2 / sq(den);
            d[1] = p[2] * t.z1 * t.z2 * (t.lnx - t.lnK) / sq(den);
            d[2] = t.z1 / den;
            break;
        }
        case ModelId::ActivatorGt0:
        case ModelId::ActivatorGe1: {
            const auto t = hill_terms(x, p[0], p[1]);
            const double den = t.z1 + t.z2;
            const double span = p[2] - p[3];
            d[0] = -span * t.z1 * t.dz2 / sq(den);
            d[1] = span * t.z1 * t.z2 * (t.lnx - t.lnK) / sq(den);
            d[2] = t.z1 / den;
            d[3] = 1.0 - d[2];
            break;
        }
        case ModelId::InhibitionK50: {
            const auto t = hill_terms(x, p[0], p[1]);
            const double den = t.z1 + t.z2;
            const double span = p[3] - p[2];
            d[0] = span * t.z1 * t.dz2 / sq(den);
            d[1] = -span * t.z1 * t.z2 * (t.lnx - t.lnK) / sq(den);
            d[2] = t.z1 / den;
            d[3] = 1.0 - d[2];
            break;
        }
        case ModelId::InhibitionI50: {
            const auto t = hill_terms(x, p[0], p[1]);
            const double v0 = p[3], vmin = p[2];
            const double z5 = 1.0 - 2.0 * vmin / v0;
            const double den = t.z2 * z5 + t.z1;
            const double span = v0 - vmin;
            d[0] = span * t.z1 * z5 * t.dz2 / sq(den);
            d[1] = -span * t.z1 * z5 * t.z2 * (t.lnx - t.lnK) / sq(den);
            d[2] = t.z1 / den - 2.0 * span * t.z1 * t.z2 / (v0 * sq(den));
            d[3] = 1.0 - t.z1 / den + 2.0 * vmin * span * t.z1 * t.z2 / (sq(v0) * sq(den));
            break;
        }
        case ModelId::Arrhenius:
            d[0] = 1.0;
            d[1] = -arrhenius_factor(x);
            break;
        case ModelId::Linear:
            d[0] = 1.0;
            d[1] = x;
            break;
    }
    for (double v : d)
        if (!std::isfinite(v)) return false;
    return true;
}

std::optional<double> inverse(ModelId id, double y, std::span<const double> p) {
    std::optional<double> r;
    switch (id) {
        case ModelId::MichaelisMenten:
            if (p[1] != y) r = y * p[0] / (p[1] - y);
            break;
        case ModelId::HillGt0:
        case ModelId::HillGe1:
            if (p[2] != y) r = signed_pow(y * std::pow(p[0], p[1]) / (p[2] - y), 1.0 / p[1]);
            break;
        case ModelId::ActivatorGt0:
        case ModelId::ActivatorGe1:
            if (p[2] != y)
                r = signed_pow((y - p[3]) * std::pow(p[0], p[1]) / (p[2] - y), 1.0 / p[1]);
            break;
        case ModelId::ActivatorEq1:
            if (p[1] != y) r = (y - p[2]) * p[0] / (p[1] - y);
            break;
        case ModelId::InhibitionLinear:
            if (y > 0.0) r = p[0] * (p[1] / y - 1.0);
            break;
        case ModelId::InhibitionI50:
            if (p[3] != 0.0 && y > p[2]) {
                const double v = std::pow(p[0], p[1]) * (p[3] - y) * (1.0 - 2.0 * p[2] / p[3]);
                r = signed_pow(v / (y - p[2]), 1.0 / p[1]);
            }
            break;
        case ModelId::InhibitionK50:
            if (y > p[2]) r = signed_pow(std::pow(p[0], p[1]) * (p[3] - y) / (y - p[2]), 1.0 / p[1]);
            break;
        case ModelId::Arrhenius:
            // Only rates on the curve have a temperature: T must be above
            // absolute zero, so for Ea > 0 the rate must be below log10(A).
            if (y != p[0]) {
                const double tK = -p[1] / (kLn10 * kGasConstantKcal * (y - p[0]));
                if (tK > 0.0) r = tK - kKelvinOffset;
            }
            break;
        case ModelId::Linear:
            if (p[1] != 0.0) r = (y - p[0]) / p[1];
            break;
    }
    if (r && !std::isfinite(*r)) r.reset();
    return r;
}

}  // namespace kin
