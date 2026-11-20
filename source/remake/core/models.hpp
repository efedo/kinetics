#pragma once
// The eleven equations of SB's Kinetics Program (menu letters a-k), with their
// parameters, default bounds, analytic derivatives and inverses.
//
// Parameter order follows the original program (its a[1..n] arrays), so for
// example the Hill equation is {Km, h, Vmax} and the activator equation is
// {Ka, h, Vmax, v0}.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kin {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

enum class ModelId : int {
    MichaelisMenten = 1,  // a
    HillGt0,              // b  Hill, h > 0
    HillGe1,              // c  Hill, h >= 1
    ActivatorGt0,         // d
    ActivatorGe1,         // e
    ActivatorEq1,         // f  (h = 1)
    InhibitionLinear,     // g
    InhibitionI50,        // h  non-linear, gives I50
    InhibitionK50,        // i  non-linear, gives K50
    Arrhenius,            // j
    Linear,               // k  linear regression
};
inline constexpr int kModelCount = 11;

enum class Family { MichaelisMenten, Hill, Activation, Inhibition, Arrhenius, Linear };

// What a parameter means; used for guesses, reports and plot transforms.
enum class Role { HalfSat, HillCoeff, Vmax, V0, Vmin, LogA, Ea, Intercept, Slope };

struct ParamDef {
    std::string_view name;
    Role role;
    double min;      // default lower bound
    double max;      // default upper bound
    bool fixable;    // may be held fixed during the fit
};

struct ModelDef {
    ModelId id;
    char letter;                 // original menu letter
    std::string_view menuName;   // e.g. "Michaelis-Menten equation"
    std::string_view shortName;  // e.g. "Michaelis-Menten" (report header)
    std::string_view equation;   // display formula
    Family family;
    std::string_view xName;      // "[S]", "[A]", "[I]", "T (°C)" or "X"
    std::string_view yName;      // "v", "log10(v)" or "Y"
    std::vector<ParamDef> params;
};

const ModelDef& model_def(ModelId id);
std::span<const ModelDef> all_models();
std::optional<ModelId> model_from_letter(char letter);

inline int param_count(ModelId id) { return static_cast<int>(model_def(id).params.size()); }
std::optional<int> param_index(ModelId id, Role role);
bool has_hill_coefficient(ModelId id);

// Physical constants used by the Arrhenius equation.
inline constexpr double kLn10 = 2.302585092994046;
inline constexpr double kGasConstantKcal = 1.98720425864083e-3;  // kcal mol^-1 K^-1
inline constexpr double kKelvinOffset = 273.15;
inline constexpr double kKjPerKcal = 4.184;

// "Raise y to the power x" from the original program: keeps the sign of a
// negative base so that the curve stays defined (and monotonic) below zero.
double signed_pow(double base, double exponent);

// Model value at x. Returns NaN where the equation is undefined.
double evaluate(ModelId id, double x, std::span<const double> p);

// Partial derivatives dy/dp_j (out.size() must equal param_count(id)).
// Returns false if any value is not finite.
bool gradient(ModelId id, double x, std::span<const double> p, std::span<double> out);

// x such that evaluate(x) == y (used for standard curves). nullopt where the
// equation cannot be inverted (e.g. y equal to Vmax).
std::optional<double> inverse(ModelId id, double y, std::span<const double> p);

}  // namespace kin
