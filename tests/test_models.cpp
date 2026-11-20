// Tests for core/models: the equation table, values worked out by hand,
// limits and special cases, analytic gradients against finite differences,
// and inverses (round trips and the values that cannot be inverted).

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/models.hpp"

#include "check.hpp"

using namespace kin;

namespace {

using Vec = std::vector<double>;

double ev(ModelId m, double x, const Vec& p) { return evaluate(m, x, p); }
std::optional<double> inv(ModelId m, double y, const Vec& p) { return inverse(m, y, p); }
// inverse() as a number, NaN when there is none.
double invv(ModelId m, double y, const Vec& p) {
    return inverse(m, y, p).value_or(std::numeric_limits<double>::quiet_NaN());
}

// Central difference of evaluate() with respect to parameter j.
double fd(ModelId m, double x, Vec p, size_t j) {
    const double step = 1e-6 * std::max(std::fabs(p[j]), 1e-3);
    Vec lo = p, hi = p;
    lo[j] -= step;
    hi[j] += step;
    return (ev(m, x, hi) - ev(m, x, lo)) / (2.0 * step);
}

// Checks every analytic derivative against a central difference.
void check_gradient(ModelId m, double x, const Vec& p) {
    Vec d(p.size());
    CHECK(gradient(m, x, p, d));
    for (size_t j = 0; j < p.size(); ++j) {
        const double ref = fd(m, x, p, j);
        char what[96];
        std::snprintf(what, sizeof what, "model %c x=%g dp%zu: %.10g vs %.10g", model_def(m).letter, x, j, d[j], ref);
        ::test::check(std::fabs(d[j] - ref) <= 1e-6 * (std::fabs(ref) + 1.0), "gradient == finite difference",
                      __FILE__, __LINE__, what);
    }
}

// Typical parameters for each equation (same order as ModelId).
struct Sample {
    ModelId m;
    Vec p;
};

std::vector<Sample> samples() {
    return {
        {ModelId::MichaelisMenten, {2.5, 10}},
        {ModelId::HillGt0, {3, 1.8, 12}},
        {ModelId::HillGe1, {3, 1.8, 12}},
        {ModelId::ActivatorGt0, {3, 1.6, 12, 2}},
        {ModelId::ActivatorGe1, {3, 1.6, 12, 2}},
        {ModelId::ActivatorEq1, {3, 12, 2}},
        {ModelId::InhibitionLinear, {4, 20}},
        {ModelId::InhibitionI50, {4, 1.5, 3, 20}},
        {ModelId::InhibitionK50, {4, 1.5, 3, 20}},
        {ModelId::Arrhenius, {9, 12}},
        {ModelId::Linear, {1.5, 2}},
    };
}

}  // namespace

// ---------------------------------------------------------------------------
// The equation table

TEST_CASE(models_table_order, "models: table lists the eleven equations in menu order") {
    const auto all = all_models();
    CHECK(all.size() == 11);
    CHECK(kModelCount == 11);
    const int counts[] = {2, 3, 3, 4, 4, 3, 2, 4, 4, 2, 2};
    for (int i = 0; i < kModelCount; ++i) {
        const auto id = static_cast<ModelId>(i + 1);
        const auto& def = model_def(id);
        CHECK(&def == &all[static_cast<size_t>(i)]);
        CHECK(def.id == id);
        CHECK(def.letter == static_cast<char>('a' + i));
        CHECK(param_count(id) == counts[i]);
        const auto back = model_from_letter(def.letter);
        CHECK(back.has_value() && *back == id);
        CHECK(!def.menuName.empty() && !def.shortName.empty() && !def.equation.empty());
    }
}

TEST_CASE(models_unknown_ids, "models: unknown ids and letters are rejected") {
#ifndef __EMSCRIPTEN__
    // The Emscripten builds do not enable exception catching (a throw
    // aborts), so this part runs in the native builds only.
    bool low = false, high = false;
    try {
        (void)model_def(static_cast<ModelId>(0));
    } catch (const std::out_of_range&) {
        low = true;
    }
    try {
        (void)model_def(static_cast<ModelId>(kModelCount + 1));
    } catch (const std::out_of_range&) {
        high = true;
    }
    CHECK(low);
    CHECK(high);
#endif
    CHECK(!model_from_letter('l').has_value());
    CHECK(!model_from_letter('A').has_value());  // letters are lower case
    CHECK(!model_from_letter('\0').has_value());
    // evaluate() and inverse() have no table lookup: an unknown id is
    // simply undefined.
    const Vec p = {1, 2, 3, 4};
    CHECK(std::isnan(evaluate(static_cast<ModelId>(99), 1.0, p)));
    CHECK(!inverse(static_cast<ModelId>(99), 1.0, p).has_value());
}

TEST_CASE(models_roles_and_bounds, "models: parameter order, roles, families and bounds") {
    using M = ModelId;
    // Parameter order follows the original a[1..n] arrays.
    CHECK(param_index(M::MichaelisMenten, Role::HalfSat) == 0);
    CHECK(param_index(M::MichaelisMenten, Role::Vmax) == 1);
    CHECK(!param_index(M::MichaelisMenten, Role::HillCoeff));
    CHECK(param_index(M::HillGt0, Role::HillCoeff) == 1);
    CHECK(param_index(M::HillGe1, Role::Vmax) == 2);
    CHECK(param_index(M::ActivatorGt0, Role::V0) == 3);
    CHECK(param_index(M::ActivatorEq1, Role::Vmax) == 1);
    CHECK(param_index(M::ActivatorEq1, Role::V0) == 2);
    CHECK(param_index(M::InhibitionLinear, Role::V0) == 1);
    CHECK(!param_index(M::InhibitionLinear, Role::Vmax));
    CHECK(param_index(M::InhibitionI50, Role::Vmin) == 2);
    CHECK(param_index(M::InhibitionK50, Role::V0) == 3);
    CHECK(param_index(M::Arrhenius, Role::LogA) == 0);
    CHECK(param_index(M::Arrhenius, Role::Ea) == 1);
    CHECK(!param_index(M::Arrhenius, Role::HalfSat));
    CHECK(param_index(M::Linear, Role::Intercept) == 0);
    CHECK(param_index(M::Linear, Role::Slope) == 1);

    // h > 0, h >= 1 and h = 1 variants.
    const bool hasH[] = {false, true, true, true, true, false, false, true, true, false, false};
    for (int i = 0; i < kModelCount; ++i) CHECK(has_hill_coefficient(static_cast<ModelId>(i + 1)) == hasH[i]);
    CHECK(model_def(M::HillGt0).params[1].min == 0.01);
    CHECK(model_def(M::HillGe1).params[1].min == 1.0);
    CHECK(model_def(M::ActivatorGt0).params[1].min == 0.01);
    CHECK(model_def(M::ActivatorGe1).params[1].min == 1.0);
    CHECK(model_def(M::HillGt0).params[1].max == 20.0);

    // Concentration-like constants share the original bounds.
    for (const auto& def : all_models())
        for (const auto& pd : def.params)
            if (pd.role == Role::HalfSat || pd.role == Role::Vmax || pd.role == Role::V0 || pd.role == Role::Vmin) {
                CHECK(pd.min == 1e-6);
                CHECK(pd.max == 1e6);
                CHECK(pd.fixable);
            }
    // Arrhenius parameters cannot be fixed; linear regression can fix
    // only the intercept, which may be negative.
    CHECK(!model_def(M::Arrhenius).params[0].fixable && !model_def(M::Arrhenius).params[1].fixable);
    CHECK(model_def(M::Arrhenius).params[0].min == -100.0);
    CHECK(model_def(M::Linear).params[0].fixable && !model_def(M::Linear).params[1].fixable);
    CHECK(model_def(M::Linear).params[1].min == -1e6);

    const Family fam[] = {Family::MichaelisMenten, Family::Hill,       Family::Hill,       Family::Activation,
                          Family::Activation,      Family::Activation, Family::Inhibition, Family::Inhibition,
                          Family::Inhibition,      Family::Arrhenius,  Family::Linear};
    for (int i = 0; i < kModelCount; ++i) CHECK(model_def(static_cast<ModelId>(i + 1)).family == fam[i]);
    CHECK(model_def(M::Linear).xName == "X" && model_def(M::Linear).yName == "Y");
    CHECK(model_def(M::Arrhenius).yName == "log10(v)");
    CHECK(model_def(M::InhibitionK50).xName == "[I]");
}

// ---------------------------------------------------------------------------
// Values

TEST_CASE(models_signed_pow, "models: signed_pow keeps the sign of a negative base") {
    CHECK(signed_pow(0.0, 2.5) == 0.0);
    CHECK(signed_pow(-0.0, 0.5) == 0.0);
    CHECK_NEAR(signed_pow(2.0, 3.0), 8.0, 1e-15);
    CHECK_NEAR(signed_pow(-2.0, 3.0), -8.0, 1e-15);
    CHECK_NEAR(signed_pow(-4.0, 0.5), -2.0, 1e-15);  // std::pow would give NaN
    CHECK_NEAR(signed_pow(9.0, 0.5), 3.0, 1e-15);
    for (double b : {0.3, 1.7, 5.0})
        for (double e : {0.4, 1.0, 1.8}) CHECK_NEAR(signed_pow(-b, e), -signed_pow(b, e), 1e-15);
}

TEST_CASE(models_hand_values, "models: values worked out by hand") {
    using M = ModelId;
    CHECK_NEAR(ev(M::MichaelisMenten, 7.5, {2.5, 10}), 7.5, 1e-14);              // 10*7.5/10
    CHECK_NEAR(ev(M::HillGt0, 6, {3, 2, 12}), 9.6, 1e-13);                       // 12*36/45
    CHECK_NEAR(ev(M::HillGe1, 6, {3, 2, 12}), 9.6, 1e-13);
    CHECK_NEAR(ev(M::ActivatorGt0, 4, {2, 2, 10, 1}), 8.2, 1e-13);               // 1 + 9*16/20
    CHECK_NEAR(ev(M::ActivatorGe1, 4, {2, 2, 10, 1}), 8.2, 1e-13);
    CHECK_NEAR(ev(M::ActivatorEq1, 6, {2, 10, 1}), 7.75, 1e-13);                 // 1 + 9*6/8
    CHECK_NEAR(ev(M::InhibitionLinear, 12, {4, 20}), 5.0, 1e-13);                // 20*4/16
    CHECK_NEAR(ev(M::InhibitionK50, 4, {2, 2, 2, 10}), 3.6, 1e-13);              // 10 - 8*16/20
    CHECK_NEAR(ev(M::InhibitionI50, 6, {2, 1, 2, 10}), 10.0 / 3.0, 1e-13);       // 10 - 8*6/(6 + 2*0.6)
    CHECK_NEAR(ev(M::Linear, -2, {1.5, 2}), -2.5, 1e-15);
    CHECK_NEAR(ev(M::Linear, 0, {1.5, 2}), 1.5, 1e-15);
    // Hill with a negative concentration: z1 = -(4^0.5) = -2 (signed power).
    CHECK_NEAR(ev(M::HillGt0, -4, {3, 0.5, 6}), 6.0 * -2.0 / (std::sqrt(3.0) - 2.0), 1e-12);
}

TEST_CASE(models_arrhenius_values, "models: Arrhenius values and slope against 1/T") {
    const Vec p = {9, 12};
    // python: 9 - 12 / (2.302585092994046 * 1.98720425864083e-3 * (T + 273.15))
    CHECK_NEAR(ev(ModelId::Arrhenius, 0.0, p), -0.6011187955823285, 1e-12);
    CHECK_NEAR(ev(ModelId::Arrhenius, 25.0, p), 0.2039389602102517, 1e-12);
    CHECK_NEAR(ev(ModelId::Arrhenius, 37.0, p), 0.5442669707776453, 1e-12);
    // log10(v) is linear in 1/T with slope -Ea/(ln10 R) = -2622.5455990133128.
    const double t1 = 10.0, t2 = 30.0;
    const double slope = (ev(ModelId::Arrhenius, t1, p) - ev(ModelId::Arrhenius, t2, p)) /
                         (1.0 / (t1 + 273.15) - 1.0 / (t2 + 273.15));
    CHECK_REL(slope, -2622.5455990133128, 1e-9);
    // As T -> infinity log10(v) -> log10(A).
    CHECK_NEAR(ev(ModelId::Arrhenius, 1e12, p), 9.0, 1e-8);
}

TEST_CASE(models_half_saturation, "models: half-saturation constants, zero and saturating limits") {
    using M = ModelId;
    for (double h : {0.5, 1.0, 1.8, 3.0}) {
        CHECK_NEAR(ev(M::HillGt0, 3, {3, h, 12}), 6.0, 1e-12);  // v(Km) = Vmax/2 for any h
        CHECK_NEAR(ev(M::ActivatorGt0, 3, {3, h, 12, 2}), 7.0, 1e-12);
        CHECK_NEAR(ev(M::InhibitionK50, 4, {4, h, 3, 20}), 11.5, 1e-12);
        // I50 is where v falls to v0/2, whatever vmin and h are.
        CHECK_NEAR(ev(M::InhibitionI50, 4, {4, h, 3, 20}), 10.0, 1e-12);
        CHECK_NEAR(ev(M::InhibitionI50, 4, {4, h, 7, 20}), 10.0, 1e-12);
    }
    CHECK_NEAR(ev(M::MichaelisMenten, 2.5, {2.5, 10}), 5.0, 1e-14);
    CHECK_NEAR(ev(M::ActivatorEq1, 3, {3, 12, 2}), 7.0, 1e-14);
    CHECK_NEAR(ev(M::InhibitionLinear, 4, {4, 20}), 10.0, 1e-14);

    // At zero concentration: no product, or v0 with an effector.
    CHECK(ev(M::MichaelisMenten, 0, {2.5, 10}) == 0.0);
    CHECK(ev(M::HillGt0, 0, {3, 1.8, 12}) == 0.0);
    CHECK(ev(M::ActivatorGe1, 0, {3, 1.6, 12, 2}) == 2.0);
    CHECK(ev(M::ActivatorEq1, 0, {3, 12, 2}) == 2.0);
    CHECK(ev(M::InhibitionLinear, 0, {4, 20}) == 20.0);
    CHECK(ev(M::InhibitionK50, 0, {4, 1.5, 3, 20}) == 20.0);
    CHECK(ev(M::InhibitionI50, 0, {4, 1.5, 3, 20}) == 20.0);

    // Saturating concentrations reach the plateau.
    const double big = 1e9;
    CHECK_NEAR(ev(M::MichaelisMenten, big, {2.5, 10}), 10.0, 1e-6);
    CHECK_NEAR(ev(M::HillGe1, big, {3, 1.8, 12}), 12.0, 1e-6);
    CHECK_NEAR(ev(M::ActivatorGt0, big, {3, 1.6, 12, 2}), 12.0, 1e-6);
    CHECK_NEAR(ev(M::ActivatorEq1, big, {3, 12, 2}), 12.0, 1e-6);
    CHECK_NEAR(ev(M::InhibitionLinear, big, {4, 20}), 0.0, 1e-6);
    CHECK_NEAR(ev(M::InhibitionK50, big, {4, 1.5, 3, 20}), 3.0, 1e-6);
    CHECK_NEAR(ev(M::InhibitionI50, big, {4, 1.5, 3, 20}), 3.0, 1e-6);
}

TEST_CASE(models_special_cases, "models: special cases reduce to the simpler equations") {
    using M = ModelId;
    for (double x : {0.0, 0.3, 1.0, 2.5, 7.0, 40.0, -0.7}) {
        // Hill with h = 1 is Michaelis-Menten.
        CHECK_NEAR(ev(M::HillGt0, x, {2.5, 1, 10}), ev(M::MichaelisMenten, x, {2.5, 10}), 1e-12);
        CHECK_NEAR(ev(M::HillGe1, x, {2.5, 1, 10}), ev(M::MichaelisMenten, x, {2.5, 10}), 1e-12);
        // The two Hill variants differ only in their bounds.
        CHECK_NEAR(ev(M::HillGt0, x, {3, 1.8, 12}), ev(M::HillGe1, x, {3, 1.8, 12}), 0.0);
        // Activator with h = 1 is the h = 1 activator equation.
        CHECK_NEAR(ev(M::ActivatorGt0, x, {3, 1, 12, 2}), ev(M::ActivatorEq1, x, {3, 12, 2}), 1e-12);
        // Activator with v0 = 0 is the Hill equation.
        CHECK_NEAR(ev(M::ActivatorGe1, x, {3, 1.6, 12, 0}), ev(M::HillGe1, x, {3, 1.6, 12}), 1e-12);
        // K50 inhibition with vmin = 0 and h = 1 is linear inhibition.
        CHECK_NEAR(ev(M::InhibitionK50, x, {4, 1, 0, 20}), ev(M::InhibitionLinear, x, {4, 20}), 1e-12);
        // With vmin = 0, I50 and K50 coincide (1 - 2 vmin/v0 = 1).
        CHECK_NEAR(ev(M::InhibitionI50, x, {4, 1.5, 0, 20}), ev(M::InhibitionK50, x, {4, 1.5, 0, 20}), 1e-12);
        // Linear regression is a straight line.
        CHECK_NEAR(ev(M::Linear, x, {-3, 0.5}), -3 + 0.5 * x, 1e-15);
    }
}

TEST_CASE(models_undefined_points, "models: undefined points give NaN") {
    using M = ModelId;
    CHECK(std::isnan(ev(M::MichaelisMenten, -2.5, {2.5, 10})));  // Km + [S] = 0
    CHECK(std::isnan(ev(M::ActivatorEq1, -3, {3, 12, 2})));
    CHECK(std::isnan(ev(M::InhibitionLinear, -4, {4, 20})));
    // Signed powers: (-K)^h + K^h = 0.
    CHECK(std::isnan(ev(M::HillGt0, -3, {3, 1.8, 12})));
    CHECK(std::isnan(ev(M::ActivatorGt0, -3, {3, 1.6, 12, 2})));
    CHECK(std::isnan(ev(M::InhibitionK50, -4, {4, 1.5, 3, 20})));
    // I50 equation: v0 = 0 divides by zero; with h = 1, I50 = 4, vmin = 5,
    // v0 = 20 the denominator [I] + 4*(1 - 0.5) vanishes at [I] = -2; with
    // vmin = v0/2 it vanishes at [I] = 0.
    CHECK(std::isnan(ev(M::InhibitionI50, 1, {4, 1.5, 3, 0})));
    CHECK(std::isnan(ev(M::InhibitionI50, -2, {4, 1, 5, 20})));
    CHECK(std::isnan(ev(M::InhibitionI50, 0, {4, 1.5, 10, 20})));
    CHECK(std::isfinite(ev(M::InhibitionI50, 1, {4, 1.5, 10, 20})));
    // Arrhenius: at or below absolute zero.
    CHECK(std::isnan(ev(M::Arrhenius, -273.15, {9, 12})));
    CHECK(std::isnan(ev(M::Arrhenius, -300, {9, 12})));
    CHECK(std::isfinite(ev(M::Arrhenius, -273.0, {9, 12})));
}

// ---------------------------------------------------------------------------
// Gradients

TEST_CASE(models_gradient_zero_negative, "models: gradients match finite differences at zero, negative and large x") {
    for (const auto& s : samples()) {
        const bool arrhenius = s.m == ModelId::Arrhenius;
        for (double x : {0.0, -2.0, 0.37, 3.0, 50.0}) {
            if (arrhenius && x <= 0.0) continue;
            check_gradient(s.m, x, s.p);
        }
    }
    // At [S] = 0 the limits x^h -> 0 and x^h ln x -> 0 give exact values.
    Vec d(3);
    CHECK(gradient(ModelId::HillGt0, 0.0, Vec{3, 0.6, 12}, d));
    CHECK(d[0] == 0.0 && d[1] == 0.0 && d[2] == 0.0);
    Vec da(4);
    CHECK(gradient(ModelId::ActivatorGt0, 0.0, Vec{3, 0.6, 12, 2}, da));
    CHECK(da[0] == 0.0 && da[1] == 0.0 && da[2] == 0.0 && da[3] == 1.0);  // v = v0
    Vec dk(4);
    CHECK(gradient(ModelId::InhibitionK50, 0.0, Vec{4, 1.5, 3, 20}, dk));
    CHECK(dk[2] == 0.0 && dk[3] == 1.0);
    // Linear and Arrhenius derivatives in closed form.
    Vec dl(2);
    CHECK(gradient(ModelId::Linear, 4.0, Vec{1.5, 2}, dl));
    CHECK(dl[0] == 1.0 && dl[1] == 4.0);
    Vec dr(2);
    CHECK(gradient(ModelId::Arrhenius, 25.0, Vec{9, 12}, dr));
    CHECK(dr[0] == 1.0);
    CHECK_REL(dr[1], -1.0 / (2.302585092994046 * 1.98720425864083e-3 * 298.15), 1e-14);
    // Michaelis-Menten in closed form: dv/dKm = -Vmax S/(Km+S)^2, dv/dVmax = S/(Km+S).
    Vec dm(2);
    CHECK(gradient(ModelId::MichaelisMenten, 7.5, Vec{2.5, 10}, dm));
    CHECK_NEAR(dm[0], -0.75, 1e-14);
    CHECK_NEAR(dm[1], 0.75, 1e-14);
}

TEST_CASE(models_gradient_non_finite, "models: gradient reports non-finite derivatives") {
    Vec d2(2), d3(3), d4(4);
    CHECK(!gradient(ModelId::MichaelisMenten, -2.5, Vec{2.5, 10}, d2));      // pole at -Km
    CHECK(!gradient(ModelId::InhibitionLinear, -4.0, Vec{4, 20}, d2));
    CHECK(!gradient(ModelId::ActivatorEq1, -3.0, Vec{3, 12, 2}, d3));
    CHECK(!gradient(ModelId::HillGe1, -3.0, Vec{3, 1.8, 12}, d3));
    CHECK(!gradient(ModelId::ActivatorGe1, -3.0, Vec{3, 1.6, 12, 2}, d4));
    CHECK(!gradient(ModelId::InhibitionK50, -4.0, Vec{4, 1.5, 3, 20}, d4));
    CHECK(!gradient(ModelId::InhibitionI50, 1.0, Vec{4, 1.5, 3, 0}, d4));    // v0 = 0
    CHECK(!gradient(ModelId::Arrhenius, -273.15, Vec{9, 12}, d2));           // T = 0 K
    CHECK(gradient(ModelId::MichaelisMenten, -2.0, Vec{2.5, 10}, d2));       // finite off the pole
}

// ---------------------------------------------------------------------------
// Inverses

TEST_CASE(models_inverse_half_values, "models: inverse of the half-way value is the half-saturation constant") {
    using M = ModelId;
    CHECK_NEAR(invv(M::MichaelisMenten, 5.0, {2.5, 10}), 2.5, 1e-13);
    CHECK_NEAR(invv(M::HillGt0, 6.0, {3, 1.8, 12}), 3.0, 1e-13);
    CHECK_NEAR(invv(M::HillGe1, 6.0, {3, 1.8, 12}), 3.0, 1e-13);
    CHECK_NEAR(invv(M::ActivatorGt0, 7.0, {3, 1.6, 12, 2}), 3.0, 1e-13);
    CHECK_NEAR(invv(M::ActivatorGe1, 7.0, {3, 1.6, 12, 2}), 3.0, 1e-13);
    CHECK_NEAR(invv(M::ActivatorEq1, 7.0, {3, 12, 2}), 3.0, 1e-13);
    CHECK_NEAR(invv(M::InhibitionLinear, 10.0, {4, 20}), 4.0, 1e-13);
    CHECK_NEAR(invv(M::InhibitionI50, 10.0, {4, 1.5, 3, 20}), 4.0, 1e-12);
    CHECK_NEAR(invv(M::InhibitionK50, 11.5, {4, 1.5, 3, 20}), 4.0, 1e-12);
    CHECK_NEAR(invv(M::Linear, 0.0, {1.5, 2}), -0.75, 1e-15);
    // Zero velocity, or the velocity without effector, means zero concentration.
    CHECK(invv(M::MichaelisMenten, 0.0, {2.5, 10}) == 0.0);
    CHECK(invv(M::HillGt0, 0.0, {3, 1.8, 12}) == 0.0);
    CHECK(invv(M::ActivatorGt0, 2.0, {3, 1.6, 12, 2}) == 0.0);
    CHECK(invv(M::InhibitionLinear, 20.0, {4, 20}) == 0.0);
    CHECK(invv(M::InhibitionK50, 20.0, {4, 1.5, 3, 20}) == 0.0);
}

TEST_CASE(models_inverse_round_trip, "models: evaluate followed by inverse returns x") {
    for (const auto& s : samples()) {
        for (double x : {0.2, 1.0, 4.0, 15.0, 60.0}) {
            const double y = ev(s.m, x, s.p);
            const auto back = inv(s.m, y, s.p);
            CHECK(back.has_value());
            if (back) CHECK_REL(*back, x, 1e-9);
        }
    }
    // The other branch of the hyperbola: v above Vmax maps to [S] < -Km,
    // and evaluating there gives the same v back.
    const Vec mm = {2.5, 10};
    const auto neg = inv(ModelId::MichaelisMenten, 20.0, mm);
    CHECK(neg && *neg < -2.5);
    CHECK_NEAR(invv(ModelId::MichaelisMenten, 20.0, mm), -5.0, 1e-13);  // 20*2.5/(10-20)
    if (neg) CHECK_NEAR(ev(ModelId::MichaelisMenten, *neg, mm), 20.0, 1e-12);
    // Negative velocities give negative concentrations through signed powers.
    const Vec hill = {3, 2, 12};
    const auto hneg = inv(ModelId::HillGt0, -1.0, hill);
    CHECK(hneg && *hneg < 0.0);
    if (hneg) CHECK_NEAR(ev(ModelId::HillGt0, *hneg, hill), -1.0, 1e-12);
    const auto aneg = inv(ModelId::ActivatorGt0, 1.0, {3, 1.6, 12, 2});  // below v0
    CHECK(aneg && *aneg < 0.0);
    if (aneg) CHECK_NEAR(ev(ModelId::ActivatorGt0, *aneg, {3, 1.6, 12, 2}), 1.0, 1e-12);
}

TEST_CASE(models_inverse_rejected, "models: inverse rejects plateaus and values it cannot invert") {
    using M = ModelId;
    CHECK(!inv(M::MichaelisMenten, 10.0, {2.5, 10}));  // y = Vmax
    CHECK(!inv(M::HillGt0, 12.0, {3, 1.8, 12}));
    CHECK(!inv(M::ActivatorGe1, 12.0, {3, 1.6, 12, 2}));
    CHECK(!inv(M::ActivatorEq1, 12.0, {3, 12, 2}));
    CHECK(!inv(M::InhibitionLinear, 0.0, {4, 20}));  // v -> 0 only as [I] -> infinity
    CHECK(!inv(M::InhibitionLinear, -1.0, {4, 20}));
    CHECK(!inv(M::InhibitionI50, 5.0, {4, 1.5, 3, 0}));   // v0 = 0
    CHECK(!inv(M::InhibitionI50, 3.0, {4, 1.5, 3, 20}));  // y = vmin
    CHECK(!inv(M::InhibitionI50, 2.0, {4, 1.5, 3, 20}));  // below vmin
    CHECK(!inv(M::InhibitionK50, 3.0, {4, 1.5, 3, 20}));
    CHECK(!inv(M::InhibitionK50, 1.0, {4, 1.5, 3, 20}));
    CHECK(!inv(M::Arrhenius, 9.0, {9, 12}));  // log10(v) = log10(A) needs T = infinity
    CHECK(!inv(M::Linear, 3.0, {1.5, 0}));    // zero slope
    // A result that overflows is not returned.
    CHECK(!inv(M::Linear, 1.0, {0, std::numeric_limits<double>::denorm_min()}));
    CHECK(!inv(M::MichaelisMenten, 1e300, {1e300, 1e-300}));
}

// log10(v) = log10(A) - Ea/(ln10 R T) is always below log10(A) for Ea > 0,
// so y = 10 with log10(A) = 9 has no temperature. inverse() used to return
// -273.15 - 12/(ln10 R) = -2895.7 degrees C (found by this test, now fixed),
// which the standard-curve panel showed as the temperature for that rate.
TEST_CASE(models_arrhenius_inverse_above_limit, "models: Arrhenius inverse rejects rates above log10(A)") {
    const Vec p = {9, 12};
    const auto t = inv(ModelId::Arrhenius, 10.0, p);
    CHECK(!t.has_value());
    if (t) {
        std::printf("    inverse(Arrhenius, 10) = %.10g (python: -2895.695599013313)\n", *t);
        CHECK(std::isfinite(ev(ModelId::Arrhenius, *t, p)));
    }
    // Just below the limit there is a (very high) temperature, and it round-trips.
    const auto hot = inv(ModelId::Arrhenius, 8.9, p);
    CHECK(hot.has_value());
    if (hot) CHECK_NEAR(ev(ModelId::Arrhenius, *hot, p), 8.9, 1e-9);
}
