// Tests for core/stats: incomplete beta, Student t and F probabilities,
// significance bins, binomial coefficients, linear regression (with the
// lack-of-fit split) and the runs and sign tests on residuals.
//
// Independent references, computed in Python 3.12 (no scipy):
//  * Student t, integer df: Abramowitz & Stegun 26.7.3 (odd df) and 26.7.4
//    (even df) finite sums, evaluated with decimal at 60 digits:
//      th = atan(t/sqrt(n)), c2 = n/(n+t^2), s = t/sqrt(n+t^2)
//      n even: A = s * (1 + 1/2 c2 + 1*3/(2*4) c2^2 + ...)        (n/2 terms)
//      n odd:  A = 2/pi * (th + s*(c + 2/3 c^3 + ...)),  c = sqrt(c2)
//      p = 1 - A
//    Critical values come from bisection on the same sums; they agree with
//    the printed tables (e.g. t(0.975, 10) = 2.228138852).
//  * I_x(a, b) and F upper tails with even df: the exact binomial sum
//      I_x(a, b) = sum_{j=a}^{a+b-1} C(a+b-1, j) x^j (1-x)^(a+b-1-j)
//    in fractions.Fraction, with P(F(d1, d2) > f) = I_{d2/(d2+d1 f)}(d2/2, d1/2).
//  * Regression: numpy.polyfit / lstsq and hand-derived sums.
//  * Runs and sign tests: exhaustive enumeration of all arrangements of the
//    observed numbers of + and - residuals (itertools.combinations), and of
//    all 2^n sign patterns for the sign test.

#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "core/models.hpp"
#include "core/stats.hpp"

#include "check.hpp"

using namespace kin;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kPi = std::numbers::pi;

std::vector<Point> points(const std::vector<double>& x, const std::vector<double>& y) {
    std::vector<Point> out;
    for (size_t i = 0; i < x.size(); ++i) out.push_back({x[i], y[i]});
    return out;
}

// Data whose residuals about the line y = 0 follow `signs` ('+', '-', '0'),
// one point per X.
std::vector<Point> signed_points(const std::string& signs) {
    std::vector<Point> out;
    for (size_t i = 0; i < signs.size(); ++i)
        out.push_back({static_cast<double>(i + 1), signs[i] == '+' ? 1.0 : signs[i] == '-' ? -1.0 : 0.0});
    return out;
}

const std::vector<double> kZeroLine = {0.0, 0.0};  // Linear model: intercept 0, slope 0

}  // namespace

// ---------------------------------------------------------------------------
// Incomplete beta

TEST_CASE(stats_ibeta_integer, "stats: incomplete beta equals the exact binomial sum for integer a, b") {
    // Points on both sides of the switch-over x = (a+1)/(a+b+2).
    CHECK_NEAR(incomplete_beta(2, 3, 0.2), 113.0 / 625.0, 1e-15);
    CHECK_NEAR(incomplete_beta(2, 3, 0.6), 513.0 / 625.0, 1e-15);
    CHECK_NEAR(incomplete_beta(3, 2, 0.3), 837.0 / 10000.0, 1e-15);
    CHECK_NEAR(incomplete_beta(5, 4, 0.75), 58077.0 / 65536.0, 1e-15);
    CHECK_NEAR(incomplete_beta(10, 10, 0.4), 709884725248.0 / 3814697265625.0, 1e-14);
    CHECK_NEAR(incomplete_beta(1, 7, 0.9), 1.0 - 1e-7, 1e-15);
}

TEST_CASE(stats_ibeta_closed_forms, "stats: incomplete beta matches closed forms for non-integer parameters") {
    // I_x(1/2, 1/2) = (2/pi) asin(sqrt x); I_x(a, 1) = x^a; I_x(1, b) = 1 - (1-x)^b.
    for (double x : {0.05, 0.3, 0.5, 0.9, 0.999})
        CHECK_NEAR(incomplete_beta(0.5, 0.5, x), 2.0 / kPi * std::asin(std::sqrt(x)), 1e-14);
    CHECK_NEAR(incomplete_beta(2.5, 1, 0.7), std::pow(0.7, 2.5), 1e-15);
    CHECK_NEAR(incomplete_beta(1, 3.5, 0.2), 1.0 - std::pow(0.8, 3.5), 1e-15);
    CHECK_NEAR(incomplete_beta(1, 3.5, 0.95), 1.0 - std::pow(0.05, 3.5), 1e-15);
}

TEST_CASE(stats_ibeta_limits, "stats: incomplete beta limits, reflection and the symmetric median") {
    CHECK(incomplete_beta(2, 3, 0.0) == 0.0);
    CHECK(incomplete_beta(2, 3, -0.5) == 0.0);
    CHECK(incomplete_beta(2, 3, 1.0) == 1.0);
    CHECK(incomplete_beta(2, 3, 1.5) == 1.0);
    // I_x(a, b) = 1 - I_{1-x}(b, a).
    for (double x : {0.1, 0.35, 0.62, 0.97})
        CHECK_NEAR(incomplete_beta(3.5, 1.25, x) + incomplete_beta(1.25, 3.5, 1.0 - x), 1.0, 1e-14);
    // I_{1/2}(a, a) = 1/2 by symmetry.
    for (double a : {0.5, 3.0, 40.0, 1e3}) CHECK_NEAR(incomplete_beta(a, a, 0.5), 0.5, 1e-12);
}

TEST_CASE(stats_ibeta_huge_parameters, "stats: incomplete beta stays accurate when the continued fraction hits its cap") {
    // A Python replica of beta_cf shows that at x = 1/2 the 300-term cap is
    // reached once a = b > ~1.54e5. At a = b = 5e5 the truncated fraction is
    // still within 5e-10 of the exact value 1/2.
    CHECK_NEAR(incomplete_beta(5e5, 5e5, 0.5), 0.5, 1e-7);
}

// ---------------------------------------------------------------------------
// Student t

TEST_CASE(stats_t_closed_forms, "stats: two-tailed t p for df 1 and 2 matches the closed forms") {
    // df = 1 (Cauchy): p = (2/pi) atan(1/t).  df = 2: p = 1 - t / sqrt(2 + t^2).
    for (double t : {0.3, 1.0, 3.0, 25.0}) CHECK_REL(t_two_tailed_p(t, 1), 2.0 / kPi * std::atan(1.0 / t), 1e-13);
    CHECK_NEAR(t_two_tailed_p(1.0, 1), 0.5, 1e-15);
    for (double t : {0.5, std::sqrt(2.0), 3.0}) CHECK_NEAR(t_two_tailed_p(t, 2), 1.0 - t / std::sqrt(2.0 + t * t), 1e-15);
    CHECK_NEAR(t_two_tailed_p(std::sqrt(2.0), 2), 1.0 - std::sqrt(0.5), 1e-15);
}

TEST_CASE(stats_t_finite_sums, "stats: two-tailed t p matches the A&S finite sums for df 3 to 10") {
    struct Row { double t; int df; double p; };
    const Row rows[] = {
        {0.5, 3, 6.51447964848150994e-1}, {1.5, 3, 2.30583865244823052e-1}, {2.5, 4, 6.67665448119881450e-2},
        {0.8, 5, 4.60014066903737125e-1}, {1.2, 6, 2.75367162780152188e-1}, {3.0, 7, 1.99421261319925379e-2},
        {0.25, 9, 8.08200369704903140e-1}, {2.0, 10, 7.33880347707403656e-2},
    };
    for (const auto& r : rows) CHECK_REL(t_two_tailed_p(r.t, r.df), r.p, 1e-12);
}

TEST_CASE(stats_t_critical_values, "stats: textbook t critical values give the nominal two-tailed p") {
    CHECK_NEAR(t_two_tailed_p(12.706204736174705, 1), 0.05, 1e-13);
    CHECK_NEAR(t_two_tailed_p(4.302652729749464, 2), 0.05, 1e-13);
    CHECK_NEAR(t_two_tailed_p(2.131846786326650, 4), 0.10, 1e-13);
    CHECK_NEAR(t_two_tailed_p(4.032142983555228, 5), 0.01, 1e-13);
    CHECK_NEAR(t_two_tailed_p(2.228138851986275, 10), 0.05, 1e-13);
    CHECK_NEAR(t_two_tailed_p(2.042272456301238, 30), 0.05, 1e-13);
}

TEST_CASE(stats_t_extremes, "stats: two-tailed t p in the far tails keeps its relative accuracy") {
    // Cancellation-free forms: df 1: (2/pi) atan(1/t); df 2: 2 / (s (s + t)) with
    // s = sqrt(2 + t^2); df 4 and 5 from the 60-digit A&S sums.
    CHECK_REL(t_two_tailed_p(1e6, 1), 2.0 / kPi * std::atan(1e-6), 1e-12);
    const double s = std::sqrt(2.0 + 1e8);
    CHECK_REL(t_two_tailed_p(1e4, 2), 2.0 / (s * (s + 1e4)), 1e-12);
    CHECK_REL(t_two_tailed_p(1000, 4), 5.99996000020999899e-12, 1e-10);
    CHECK_REL(t_two_tailed_p(50, 5), 6.04775762660122523e-8, 1e-10);
    // t -> 0: p -> 1 - (2/pi) t for df = 1.
    CHECK_NEAR(t_two_tailed_p(1e-8, 1), 1.0, 1e-8);
}

TEST_CASE(stats_t_degenerate, "stats: t p symmetry and degenerate input") {
    for (double t : {0.4, 2.5, 7.0}) CHECK(t_two_tailed_p(-t, 6) == t_two_tailed_p(t, 6));
    CHECK(t_two_tailed_p(0.0, 5) == 1.0);
    CHECK(t_two_tailed_p(kInf, 5) == 0.0);   // limit t -> infinity
    CHECK(t_two_tailed_p(-kInf, 5) == 0.0);
    CHECK(t_two_tailed_p(kNaN, 5) == 1.0);   // no evidence
    CHECK(t_two_tailed_p(3.0, 0) == 1.0);    // no degrees of freedom
    CHECK(t_two_tailed_p(3.0, -2) == 1.0);
}

TEST_CASE(stats_t_normal_limit, "stats: t p approaches the normal limit with the 1/df correction") {
    // P(|T| > t) = erfc(t/sqrt 2) + phi(t) (t^3 + t) / (2 df) + O(df^-2).
    const double z = 1.959963984540054;  // two-tailed 5% point of N(0, 1)
    const double phi = std::exp(-0.5 * z * z) / std::sqrt(2.0 * kPi);
    for (double df : {1e4, 1e6}) {
        const double approx = std::erfc(z / std::sqrt(2.0)) + phi * (z * z * z + z) / (2.0 * df);
        CHECK_NEAR(t_two_tailed_p(z, static_cast<int>(df)), approx, 1e-8);
    }
}

// ---------------------------------------------------------------------------
// F

TEST_CASE(stats_f_exact, "stats: F upper-tail p equals exact rationals for even df") {
    CHECK_NEAR(f_upper_p(3.0, 2, 10), 3125.0 / 32768.0, 1e-15);  // (1 + 2f/d)^(-d/2)
    CHECK_NEAR(f_upper_p(2.0, 4, 2), 9.0 / 25.0, 1e-15);         // 1 - (d f / (2 + d f))^(d/2)
    CHECK_NEAR(f_upper_p(1.5, 4, 6), 5.0 / 16.0, 1e-15);
    CHECK_NEAR(f_upper_p(2.5, 6, 8), 17035264.0 / 148035889.0, 1e-15);
    CHECK_NEAR(f_upper_p(0.5, 8, 4), 13.0 / 16.0, 1e-15);
    CHECK_REL(f_upper_p(1e12, 2, 2), 1.0 / (1e12 + 1.0), 1e-10);  // far tail
    // Reciprocal symmetry: P(F(d1,d2) > f) = 1 - P(F(d2,d1) > 1/f); 1 - 5/16 = 11/16.
    CHECK_NEAR(f_upper_p(2.0 / 3.0, 6, 4), 11.0 / 16.0, 1e-15);
}

TEST_CASE(stats_f_critical_and_t2, "stats: F critical values and F(1, d) = t^2") {
    CHECK_NEAR(f_upper_p(4.102821015130401, 2, 10), 0.05, 1e-13);   // 5 (0.05^(-1/5) - 1)
    CHECK_NEAR(f_upper_p(2.866081402015659, 4, 20), 0.05, 1e-13);
    CHECK_NEAR(f_upper_p(4.8205735018803075, 6, 12), 0.01, 1e-13);
    CHECK_NEAR(f_upper_p(4.964602743730714, 1, 10), 0.05, 1e-13);   // t(0.975, 10)^2
    // F(1, 5) at 0.8^2 equals the A&S t value p(0.8, df 5).
    CHECK_REL(f_upper_p(0.64, 1, 5), 4.60014066903737125e-1, 1e-12);
}

TEST_CASE(stats_f_degenerate, "stats: F p for degenerate input") {
    CHECK(f_upper_p(0.0, 3, 7) == 1.0);
    CHECK(f_upper_p(kInf, 3, 7) == 0.0);
    CHECK(f_upper_p(-1.0, 3, 7) == 1.0);
    CHECK(f_upper_p(kNaN, 3, 7) == 1.0);
    CHECK(f_upper_p(2.0, 0, 7) == 1.0);
    CHECK(f_upper_p(2.0, 3, 0) == 1.0);
    CHECK(f_upper_p(2.0, -1, -1) == 1.0);
}

// ---------------------------------------------------------------------------
// Bins and binomial coefficients

TEST_CASE(stats_p_bin, "stats: p_bin returns the first significance bin the p value is below") {
    CHECK(p_bin(0.0) == 0.001);
    CHECK(p_bin(0.0005) == 0.001);
    CHECK(p_bin(0.001) == 0.005);  // bins are strict upper limits
    CHECK(p_bin(0.004) == 0.005);
    CHECK(p_bin(0.007) == 0.01);
    CHECK(p_bin(0.02) == 0.025);
    CHECK(p_bin(0.04) == 0.05);
    CHECK(p_bin(0.05) == 0.1);
    CHECK(p_bin(0.0999) == 0.1);
    CHECK(p_bin(0.1) == 1.0);
    CHECK(p_bin(0.7) == 1.0);
    CHECK(p_bin(kNaN) == 1.0);
}

TEST_CASE(stats_combinations, "stats: combinations equal exact binomial coefficients") {
    CHECK(combinations(0, 0) == 1.0);
    CHECK(combinations(5, 0) == 1.0 && combinations(5, 5) == 1.0);
    CHECK(combinations(10, 3) == 120.0 && combinations(10, 7) == 120.0);
    CHECK(combinations(52, 5) == 2598960.0);
    CHECK_REL(combinations(60, 30), 118264581564861424.0, 1e-14);  // math.comb(60, 30)
    CHECK(combinations(-1, 0) == 0.0);
    CHECK(combinations(3, -1) == 0.0);
    CHECK(combinations(3, 4) == 0.0);
}

// ---------------------------------------------------------------------------
// Linear regression

TEST_CASE(stats_regression_numpy, "stats: linear regression matches numpy (no replicates, df 6)") {
    // x = 1..8; numpy.polyfit(x, y, 1), residual mean square sse/(n-2);
    // t p values from the A&S sums with df = 6.
    const auto d = points({1, 2, 3, 4, 5, 6, 7, 8}, {2.3, 2.9, 4.4, 4.6, 6.3, 6.2, 7.9, 8.6});
    const auto lr = linear_regression(d);
    CHECK(lr.ok && lr.error.empty() && !lr.interceptFixed);
    CHECK(lr.n == 8 && lr.groups == 8 && !lr.replicates);
    CHECK(lr.df == 6 && lr.dfTotal == 7 && lr.dfRegression == 1 && lr.dfDeviation == 6);
    CHECK_REL(lr.slope, 0.9071428571428571, 1e-13);
    CHECK_REL(lr.intercept, 1.3178571428571415, 1e-12);
    CHECK_REL(lr.sdSlope, 0.05902169169638356, 1e-12);
    CHECK_REL(lr.sdIntercept, 0.2980449333790549, 1e-12);
    CHECK_REL(lr.r, 0.987537221876759, 1e-13);
    CHECK_REL(lr.sdR, 0.06425241293514831, 1e-11);
    CHECK_REL(lr.msError, 0.146309523809524, 1e-11);
    CHECK_REL(lr.ssTotal, 35.44, 1e-13);
    CHECK_REL(lr.ssRegression, 34.56214285714285, 1e-13);
    CHECK_REL(lr.ssDeviation, 0.877857142857144, 1e-11);
    CHECK_REL(lr.tSlopeVs0, 15.36965192067581, 1e-12);
    CHECK_REL(lr.pSlopeVs0, 4.7942020636227625e-06, 1e-10);
    CHECK_REL(lr.tSlopeVs1, 1.5732714564471302, 1e-11);
    CHECK_REL(lr.pSlopeVs1, 0.16671905547457624, 1e-10);
    CHECK_REL(lr.tIntercept, 4.42167269181887, 1e-11);
    CHECK_REL(lr.pIntercept, 0.004462898339891459, 1e-10);
    // F(1, df) = t^2 for the slope, so the regression F test equals the t test.
    CHECK_REL(lr.fRegression, 236.2262001627336, 1e-11);
    CHECK_REL(lr.pRegression, lr.pSlopeVs0, 1e-9);
    CHECK(lr.fLackOfFit == 0.0 && lr.pLackOfFit == 1.0);
}

TEST_CASE(stats_regression_order_free, "stats: linear regression does not depend on the order of the data") {
    const auto sorted = points({1, 2, 3, 4, 5, 6, 7, 8}, {2.3, 2.9, 4.4, 4.6, 6.3, 6.2, 7.9, 8.6});
    const auto shuffled = points({5, 2, 8, 1, 7, 3, 6, 4}, {6.3, 2.9, 8.6, 2.3, 7.9, 4.4, 6.2, 4.6});
    const auto a = linear_regression(sorted), b = linear_regression(shuffled);
    CHECK_REL(b.slope, a.slope, 1e-13);
    CHECK_REL(b.intercept, a.intercept, 1e-12);
    CHECK_REL(b.sdSlope, a.sdSlope, 1e-11);
    CHECK(b.groups == 8 && !b.replicates);
}

TEST_CASE(stats_regression_lack_of_fit, "stats: replicated X splits the residual into lack of fit and pure error") {
    // Four X values in duplicate. By hand (exact decimals): group means
    // 1.2, 2.7, 3.4, 5.0; SS pure error = 0.26 (df 4), SS deviations = 0.294
    // (df 2), so F = (0.294/2)/(0.26/4) = 147/65 and, since F(2, 4) has
    // P(F > f) = I_x(2, 1) = x^2 with x = 4/(4 + 2f), P = (130/277)^2.
    // The SDs and tests use the whole residual, 0.554 with df 6:
    // MS = 277/3000, Sxx = 10, mean x 2.5, so var(slope) = 277/30000,
    // var(intercept) = MS (1/8 + 6.25/10) = 277/4000, F = 14.641/MS = 43923/277.
    const auto d = points({1, 1, 2, 2, 3, 3, 4, 4}, {1.0, 1.4, 2.9, 2.5, 3.2, 3.6, 5.1, 4.9});
    const auto lr = linear_regression(d);
    CHECK(lr.ok && lr.replicates);
    CHECK(lr.n == 8 && lr.groups == 4);
    CHECK(lr.dfGroups == 3 && lr.dfDeviation == 2 && lr.dfPureError == 4 && lr.df == 6);
    CHECK_NEAR(lr.slope, 1.21, 1e-13);
    CHECK_NEAR(lr.intercept, 0.05, 1e-13);
    CHECK_NEAR(lr.ssTotal, 15.195, 1e-12);
    CHECK_NEAR(lr.ssRegression, 14.641, 1e-12);
    CHECK_NEAR(lr.ssGroups, 14.935, 1e-12);
    CHECK_NEAR(lr.ssPureError, 0.26, 1e-12);
    CHECK_NEAR(lr.ssDeviation, 0.294, 1e-12);
    CHECK_NEAR(lr.msError, 277.0 / 3000.0, 1e-12);  // residual mean square, df 6
    CHECK_REL(lr.fLackOfFit, 147.0 / 65.0, 1e-11);
    CHECK_REL(lr.pLackOfFit, 16900.0 / 76729.0, 1e-11);
    CHECK_REL(lr.sdSlope, std::sqrt(277.0 / 30000.0), 1e-11);
    CHECK_REL(lr.sdIntercept, std::sqrt(277.0 / 4000.0), 1e-11);
    // df 6 t test (A&S 26.7.4): p = 1 - s (1 + c/2 + 3c^2/8), c = 6/(6 + t^2), s = t/sqrt(6 + t^2).
    const double t = 1.21 / std::sqrt(277.0 / 30000.0);
    const double c = 6.0 / (6.0 + t * t), s = t / std::sqrt(6.0 + t * t);
    CHECK_REL(lr.pSlopeVs0, 1.0 - s * (1.0 + c / 2.0 + 3.0 * c * c / 8.0), 1e-9);
    CHECK_REL(lr.fRegression, 43923.0 / 277.0, 1e-10);
}

TEST_CASE(stats_regression_two_groups, "stats: with only two distinct X values the residual is pooled") {
    // Groups x = 1: (2, 3, 4), mean 3; x = 3: (7, 9), mean 8. The line passes
    // through both means: slope 2.5, intercept 0.5, no lack-of-fit term. The
    // pooled residual is the pure error: (2 + 2)/(5 - 2) = 4/3 with df 3.
    // Sxx = 4.8, mean x = 1.8; F = 30/(4/3) = 22.5 = t^2, p from A&S df 3.
    const auto d = points({1, 1, 1, 3, 3}, {2, 3, 4, 7, 9});
    const auto lr = linear_regression(d);
    CHECK(lr.ok && lr.replicates && lr.groups == 2);
    CHECK(lr.dfDeviation == 0 && lr.dfPureError == 3 && lr.df == 3);
    CHECK_NEAR(lr.slope, 2.5, 1e-14);
    CHECK_NEAR(lr.intercept, 0.5, 1e-14);
    CHECK_NEAR(lr.msError, 4.0 / 3.0, 1e-13);
    CHECK_NEAR(lr.ssPureError, 4.0, 1e-13);
    CHECK_NEAR(lr.ssDeviation, 0.0, 1e-12);
    CHECK_NEAR(lr.sdSlope, std::sqrt((4.0 / 3.0) / 4.8), 1e-14);
    CHECK_NEAR(lr.sdIntercept, std::sqrt(4.0 / 3.0 * (0.2 + 1.8 * 1.8 / 4.8)), 1e-14);
    CHECK_NEAR(lr.fRegression, 22.5, 1e-12);
    CHECK_REL(lr.pRegression, 0.017771901794031483, 1e-10);
    CHECK(lr.fLackOfFit == 0.0 && lr.pLackOfFit == 1.0);
}

TEST_CASE(stats_regression_identical_replicates, "stats: identical replicates give no lack-of-fit test") {
    // x = (1, 1, 2, 3), y = (2, 2, 3, 5): pure error is exactly 0, so the F
    // ratio would divide by zero and is left at 0 (P = 1). By hand:
    // Sxx = 2.75, Sxy = 4, slope 16/11, intercept 5/11, SS dev = 2/11 (df 1).
    // The SDs use the residual 2/11 with df 2: MS = 1/11, so t(slope) =
    // (16/11) / sqrt((1/11)/2.75) = 8 exactly, and for df 2 p = 1 - t/sqrt(2 + t^2).
    const auto d = points({1, 1, 2, 3}, {2, 2, 3, 5});
    const auto lr = linear_regression(d);
    CHECK(lr.ok && lr.replicates && lr.groups == 3);
    CHECK(lr.dfDeviation == 1 && lr.dfPureError == 1);
    CHECK(lr.ssPureError == 0.0);
    CHECK(lr.fLackOfFit == 0.0 && lr.pLackOfFit == 1.0);
    CHECK_NEAR(lr.slope, 16.0 / 11.0, 1e-14);
    CHECK_NEAR(lr.intercept, 5.0 / 11.0, 1e-14);
    CHECK_NEAR(lr.msError, 1.0 / 11.0, 1e-14);
    CHECK(lr.df == 2);
    CHECK_NEAR(lr.tSlopeVs0, 8.0, 1e-12);
    CHECK_REL(lr.pSlopeVs0, 1.0 - 8.0 / std::sqrt(66.0), 1e-12);
}

TEST_CASE(stats_regression_perfect_line, "stats: points exactly on a line: zero residual, every test significant") {
    // y = 1 + 2x; all sums are exact integers, so the residual is exactly 0.
    const auto d = points({1, 2, 3, 4, 5}, {3, 5, 7, 9, 11});
    const auto lr = linear_regression(d);
    CHECK(lr.ok);
    CHECK(lr.slope == 2.0 && lr.intercept == 1.0);
    CHECK(lr.msError == 0.0 && lr.sdSlope == 0.0 && lr.sdIntercept == 0.0);
    CHECK(lr.r == 1.0 && lr.sdR == 0.0);
    CHECK(std::isinf(lr.fRegression) && lr.pRegression == 0.0);
    CHECK(lr.tSlopeVs0 >= 1e6 && lr.pSlopeVs0 < 1e-5);
    CHECK(lr.tSlopeVs1 >= 1e6 && lr.pSlopeVs1 < 1e-5);
    CHECK(lr.tIntercept >= 1e6 && lr.pIntercept < 1e-5);
}

TEST_CASE(stats_regression_flat, "stats: constant Y has zero slope and zero correlation") {
    const auto d = points({1, 2, 3, 4}, {5, 5, 5, 5});
    const auto lr = linear_regression(d);
    CHECK(lr.ok);
    CHECK(lr.slope == 0.0 && lr.intercept == 5.0);
    CHECK(lr.r == 0.0);  // undefined 0/0 correlation reported as 0
    CHECK(lr.ssTotal == 0.0 && lr.ssRegression == 0.0);
}

TEST_CASE(stats_regression_too_little_data, "stats: linear regression rejects too few points or a single X value") {
    for (size_t n : {0u, 1u, 2u}) {
        const std::vector<Point> d(n, Point{1.0, 2.0});
        const auto lr = linear_regression(d);
        CHECK(!lr.ok && lr.error.find("at least 3") != std::string::npos);
        CHECK(lr.n == static_cast<int>(n));
    }
    const auto same = linear_regression(points({2, 2, 2}, {1, 2, 3}));
    CHECK(!same.ok && same.groups == 1 && same.error.find("2 different X") != std::string::npos);
}

TEST_CASE(stats_regression_through_point, "stats: regression through a fixed intercept matches least squares") {
    // numpy.linalg.lstsq(x[:, None], y - 0.5): slope = sum x (y - c) / sum x^2,
    // residual mean square with df n - 1 = 4; t p values from A&S with df 4.
    const auto d = points({1, 2, 3, 4, 5}, {2.2, 3.9, 6.1, 8.2, 9.8});
    const auto lr = regression_through_point(d, 0.5);
    CHECK(lr.ok && lr.interceptFixed);
    CHECK(lr.n == 5 && lr.groups == 5 && lr.df == 4 && lr.dfTotal == 5 && lr.dfDeviation == 4);
    CHECK(lr.intercept == 0.5 && lr.sdIntercept == 0.0);
    CHECK(lr.tIntercept == 0.0 && lr.pIntercept == 1.0);  // no test for a fixed intercept
    CHECK_REL(lr.slope, 1.8654545454545448, 1e-13);
    CHECK_REL(lr.sdSlope, 0.02972324131685782, 1e-11);
    CHECK_REL(lr.msError, 0.048590909090909004, 1e-11);
    CHECK_REL(lr.ssTotal, 191.59, 1e-13);
    CHECK_REL(lr.ssRegression, 191.39563636363633, 1e-13);
    CHECK_REL(lr.r, 0.9994926328207626, 1e-13);
    CHECK_REL(lr.sdR, 0.0183890994615485, 1e-10);
    CHECK_REL(lr.tSlopeVs0, 62.76080477119884, 1e-11);
    CHECK_REL(lr.pSlopeVs0, 3.860668782520555e-07, 1e-9);
    CHECK_REL(lr.tSlopeVs1, 29.11709850983493, 1e-11);
    CHECK_REL(lr.pSlopeVs1, 8.282310646851968e-06, 1e-9);
    CHECK_REL(lr.pRegression, lr.pSlopeVs0, 1e-9);  // F(1, df) = t^2
}

TEST_CASE(stats_regression_through_point_edges, "stats: regression through a fixed point: degenerate input") {
    CHECK(!regression_through_point(points({}, {}), 0.0).ok);
    const auto one = regression_through_point(points({1}, {2}), 0.0);
    CHECK(!one.ok && one.error.find("at least 2") != std::string::npos);
    const auto zeros = regression_through_point(points({0, 0, 0}, {1, 2, 3}), 0.0);
    CHECK(!zeros.ok && zeros.error.find("zero") != std::string::npos);

    // Two points: df 1, the SD of r needs n > 2 and is undefined.
    const auto two = regression_through_point(points({1, 2}, {1.1, 1.9}), 0.0);
    CHECK(two.ok && two.df == 1 && std::isnan(two.sdR));
    CHECK_NEAR(two.slope, (1.1 + 3.8) / 5.0, 1e-15);

    // Exactly on y = 1 + 3x through the fixed intercept 1: zero residual.
    const auto exact = regression_through_point(points({1, 2, 4}, {4, 7, 13}), 1.0);
    CHECK(exact.ok && exact.slope == 3.0 && exact.msError == 0.0);
    CHECK(std::isinf(exact.fRegression) && exact.pRegression == 0.0);
    CHECK(exact.r == 1.0);

    // Every y equals the intercept: slope 0 and r reported as 0.
    const auto flat = regression_through_point(points({1, 2, 3}, {2, 2, 2}), 2.0);
    CHECK(flat.ok && flat.slope == 0.0 && flat.r == 0.0 && flat.ssTotal == 0.0);
}

// With zero residual variance and a coefficient that is exactly 0, the t
// statistic is 0/0. (Found by this test and fixed; the rest of this comment
// describes the old behaviour.) fill_t_tests() substituted the "infinite t" sentinel
// 1e6 whenever sd == 0, so a slope of exactly 0 is reported as significantly
// different from 0 (P ~ 6e-7 for df 1), and the regression F (0/0) is set to
// infinity (P = 0). For constant Y there is no evidence of any slope: the
// expected P is 1 (or undefined), not ~0. The same happens to the intercept
// of exact data through the origin (y = 2x gives tIntercept = 1e6).
TEST_CASE(stats_zero_over_zero, "stats: a coefficient of exactly 0 with zero SD is not significant") {
    const auto lr = linear_regression(points({1, 2, 3}, {5, 5, 5}));
    CHECK(lr.ok && lr.slope == 0.0 && lr.sdSlope == 0.0);
    CHECK(lr.pSlopeVs0 == 1.0);    // actual: t = 1e6, P = 6.4e-7
    CHECK(lr.pRegression == 1.0);  // actual: F = inf, P = 0
}

// ---------------------------------------------------------------------------
// Runs and sign tests

TEST_CASE(stats_residual_tests_enumeration, "stats: runs and sign test probabilities equal exact enumeration") {
    struct Row { const char* seq; int pos, neg, runs; double runsP, signP; };
    // runsP = P(R <= runs) over all C(n, m) arrangements; signP = 2 P(#majority >= k)
    // over all 2^n sign patterns, capped at 1 (enumerated in Python).
    const Row rows[] = {
        {"++++----", 4, 4, 2, 1.0 / 35.0, 1.0},
        {"++++-----+", 5, 5, 3, 5.0 / 126.0, 1.0},
        {"+++--+---++-", 6, 6, 6, 181.0 / 462.0, 1.0},
        {"+-+-+-+-+-", 5, 5, 10, 1.0, 1.0},
        {"++-++--+-+--", 6, 6, 8, 127.0 / 154.0, 1.0},
        {"+++++++++---", 9, 3, 2, 1.0 / 110.0, 299.0 / 2048.0},
        {"+++++++++-", 9, 1, 2, 1.0 / 5.0, 11.0 / 512.0},
        {"++++++++--", 8, 2, 2, 2.0 / 45.0, 7.0 / 64.0},
        {"++--++--++--++--", 8, 8, 8, 521.0 / 1287.0, 1.0},
        {"+++++----+++++----", 10, 8, 4, 8.0 / 2431.0, 53381.0 / 65536.0},
    };
    for (const auto& r : rows) {
        const auto rt = residual_tests(ModelId::Linear, signed_points(r.seq), kZeroLine);
        CHECK(rt.sequence == r.seq);
        CHECK(rt.positives == r.pos && rt.negatives == r.neg && rt.runs == r.runs);
        CHECK(rt.runsTestable);
        CHECK_NEAR(rt.runsP, r.runsP, 1e-14);
        CHECK_NEAR(rt.signP, r.signP, 1e-14);
        CHECK(rt.runsRandom == (r.runsP > 0.05));
        CHECK(rt.signsRandom == (r.signP >= 0.05));
    }
}

TEST_CASE(stats_residual_tests_zeros, "stats: zero residuals are shown as 0 and left out of both tests") {
    // Non-zero signs +++---++ : 5 positive, 3 negative, 3 runs (enumeration:
    // P(R <= 3) = 1/7, sign test 2 * 37/256 = 93/128).
    const auto rt = residual_tests(ModelId::Linear, signed_points("+++0---0++"), kZeroLine);
    CHECK(rt.sequence == "+++0---0++");
    CHECK(rt.positives == 5 && rt.negatives == 3 && rt.runs == 3 && rt.runsTestable);
    CHECK_NEAR(rt.runsP, 1.0 / 7.0, 1e-14);
    CHECK_NEAR(rt.signP, 93.0 / 128.0, 1e-14);

    // Nothing but zeros: nothing to test.
    const auto none = residual_tests(ModelId::Linear, signed_points("000"), kZeroLine);
    CHECK(none.sequence == "000" && none.positives == 0 && none.negatives == 0 && none.runs == 0);
    CHECK(none.signP == 1.0 && none.signsRandom);
    CHECK(!none.runsTestable && none.runsP == 1.0 && none.runsRandom);

    // Fewer than 8 non-zero residuals: the runs test is not applied.
    const auto few = residual_tests(ModelId::Linear, signed_points("++0+---"), kZeroLine);
    CHECK(!few.runsTestable && few.runsP == 1.0 && few.runsRandom);
    CHECK_NEAR(few.signP, 1.0, 1e-15);  // 3 vs 3
}

TEST_CASE(stats_residual_tests_one_sign, "stats: residuals all of one sign fail both tests") {
    // 9 positives: sign test 2/2^9; a single run is the least likely
    // arrangement, and with no negatives the runs probability is 0.
    const auto rt = residual_tests(ModelId::Linear, signed_points("+++++++++"), kZeroLine);
    CHECK(rt.runs == 1 && rt.runsTestable);
    CHECK_NEAR(rt.signP, 2.0 / 512.0, 1e-15);
    CHECK(rt.runsP == 0.0 && !rt.runsRandom && !rt.signsRandom);
}

TEST_CASE(stats_residual_tests_model_and_replicates, "stats: residual signs use the model curve and replicate means") {
    // Michaelis-Menten v = 10 S / (2 + S). Values exactly on the curve
    // (S = 2 -> 5, S = 6 -> 7.5, S = 18 -> 9) give '0'. At S = 1 the two
    // replicates deviate by +0.5 and -0.2 (mean +0.15 -> '+'); at S = 4 by
    // -0.6 and +0.2 (mean -0.2 -> '-').
    const std::vector<double> p = {2.0, 10.0};
    auto v = [](double s) { return 10.0 * s / (2.0 + s); };
    const std::vector<Point> d = {
        {1, v(1) + 0.5}, {1, v(1) - 0.2}, {2, 5.0}, {4, v(4) - 0.6}, {4, v(4) + 0.2},
        {6, 7.5}, {10, v(10) - 0.1}, {18, 9.0}, {30, v(30) + 0.3},
    };
    const auto rt = residual_tests(ModelId::MichaelisMenten, d, p);
    CHECK(rt.sequence == "+0-0-0+");
    CHECK(rt.positives == 2 && rt.negatives == 2 && rt.runs == 3);
    CHECK(!rt.runsTestable);
    CHECK_NEAR(rt.signP, 1.0, 1e-15);
}
