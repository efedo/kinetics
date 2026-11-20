// Validation against published analyses.
//
// Each case refits a published dataset and compares the result with the
// published numbers, to the precision they were printed with. Sources:
//
// - Puromycin (Treloar 1974): the standard Michaelis-Menten example of
//   Bates & Watts (1988), "Nonlinear Regression Analysis and Its
//   Applications", Wiley, Appendix A1.3. The data and estimates are as
//   distributed with R (datasets::Puromycin and its reference output);
//   L. Tierney's XLISP-STAT tutorial prints the treated fit to six digits.
// - NIST Statistical Reference Datasets for linear regression, with
//   certified values to 15 digits: "Norris", and "NoInt1" and "NoInt2"
//   (regression through the origin).
//   https://www.itl.nist.gov/div898/strd/lls/lls.shtml
// - Bell, Smith & Storey (2014), PeerJ 2:e634 (PMC4217184): GAPDH from
//   ground squirrel muscle. A paper from the Storey lab, which used Brooks's
//   program; the raw Arrhenius rates are in its supplementary Data S1.
//
// We found no paper citing Brooks (1992) that prints its raw kinetic data in
// a table next to parameters fitted with his program: the citing papers that
// could be read give only the fitted parameters, with the curves in figures.
// The Bell et al. supplement is the closest case.
//
// The independent reference fits quoted in comments were computed with
// numpy (Gauss-Newton to convergence, or numpy.polyfit for Arrhenius, which
// is linear in 1/T), not with this library.

#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include "core/analysis.hpp"
#include "core/models.hpp"

#include "check.hpp"
#include "helpers.hpp"

using namespace kin;
using namespace test;

namespace {

std::vector<Point> points(const std::vector<double>& x, const std::vector<double>& y) {
    std::vector<Point> pts;
    for (size_t i = 0; i < x.size() && i < y.size(); ++i) pts.push_back({x[i], y[i]});
    return pts;
}

// Half a unit in the last printed digit.
double half_unit(int decimals) { return 0.5 * std::pow(10.0, -decimals); }

const std::vector<double> kPuromycinConc = {0.02, 0.02, 0.06, 0.06, 0.11, 0.11, 0.22, 0.22, 0.56, 0.56, 1.10, 1.10};
const std::vector<double> kPuromycinTreated = {76, 47, 97, 107, 123, 139, 159, 152, 191, 201, 207, 200};
// The untreated series has a single measurement at 1.10 ppm.
const std::vector<double> kPuromycinUntreatedConc = {0.02, 0.02, 0.06, 0.06, 0.11, 0.11, 0.22, 0.22, 0.56, 0.56, 1.10};
const std::vector<double> kPuromycinUntreated = {67, 51, 84, 86, 98, 115, 131, 124, 144, 158, 160};

}  // namespace

// Published (Tierney's six-digit printout of the Bates & Watts fit):
//   Vmax = 212.684 (SE 6.94715), Km = 0.0641213 (SE 0.00828094),
//   residual SE = 10.9337 on 10 df, residual sum of squares = 1195.45.
// numpy: Vmax 212.6837431, Km 0.06412128168, SEs 6.94715516 and
//   0.008280949498, s 10.93365819, SSE 1195.448814.
// Brooks's "+/- 1 SD" is the same asymptotic standard error. The printout
// truncates rather than rounds (6.947155 appears as 6.94715), so the
// published values are allowed one unit in the last digit, and the numpy
// fit is checked to a part in a million as well.
TEST_CASE(validation_puromycin_treated, "validation: Puromycin, treated (Bates & Watts 1988)") {
    for (Method method : {Method::Marquardt, Method::GaussNewton}) {
        const Analysis a = must_analyse(request(ModelId::MichaelisMenten,
                                                points(kPuromycinConc, kPuromycinTreated), method));
        const auto rows = a.parameter_rows();  // Km, Vmax
        CHECK(a.fit.status == FitStatus::Converged);
        CHECK_NEAR(rows[1].value, 212.684, 2 * half_unit(3));
        CHECK_NEAR(rows[1].sd, 6.94715, 2 * half_unit(5));
        CHECK_NEAR(rows[0].value, 0.0641213, 2 * half_unit(7));
        CHECK_NEAR(rows[0].sd, 0.00828094, 2 * half_unit(8));
        CHECK_NEAR(a.fit.stdDev, 10.9337, 2 * half_unit(4));
        CHECK_NEAR(a.fit.sse, 1195.45, 2 * half_unit(2));
        CHECK_REL(rows[1].value, 212.6837431, 1e-6);
        CHECK_REL(rows[1].sd, 6.94715516, 1e-6);
        CHECK_REL(rows[0].value, 0.06412128168, 1e-6);
        CHECK_REL(rows[0].sd, 0.008280949498, 1e-6);
        CHECK_REL(a.fit.sse, 1195.448814, 1e-6);
        CHECK(a.fit.dof == 10);
    }
}

// Published (R reference output, 4 significant digits):
//   Vmax = 160.3 (SE 6.480), Km = 0.04771 (SE 0.007782), residual SE 9.773 on 9 df.
// numpy: Vmax 160.2800462, Km 0.04770818451, SEs 6.480245505 and 0.007781875242.
TEST_CASE(validation_puromycin_untreated, "validation: Puromycin, untreated (Bates & Watts 1988)") {
    for (Method method : {Method::Marquardt, Method::GaussNewton}) {
        const Analysis a = must_analyse(request(ModelId::MichaelisMenten,
                                                points(kPuromycinUntreatedConc, kPuromycinUntreated), method));
        const auto rows = a.parameter_rows();
        CHECK(a.fit.status == FitStatus::Converged);
        CHECK_NEAR(rows[1].value, 160.3, half_unit(1));
        CHECK_NEAR(rows[1].sd, 6.480, half_unit(3));
        CHECK_NEAR(rows[0].value, 0.04771, half_unit(5));
        CHECK_NEAR(rows[0].sd, 0.007782, half_unit(6));
        CHECK_NEAR(a.fit.stdDev, 9.773, half_unit(3));
        CHECK(a.fit.dof == 9);
    }
}

// NIST StRD "Norris" (calibration of ozone monitors), 36 observations.
// Certified values, 15 significant digits. Every certified value is
// reproduced, including the standard deviations, which use the ordinary
// residual mean square (df = n - 2) like NIST's.
//
// Norris repeats one X value (0.3, twice), so the program also splits the
// residual into lack of fit and pure error and reports the lack-of-fit F:
//   residual SS 26.6173985294224 = deviations SS + pure-error SS 0.045,
//   F = (26.5723985294224 / 33) / (0.045 / 1) = 17.8938710635841.
// (Earlier versions, like v3.51, based the SDs on the deviations mean square
// alone when X values were repeated, which made them 1.4% larger here.)
TEST_CASE(validation_nist_norris, "validation: NIST StRD Norris linear regression") {
    const std::vector<double> x = {0.2,   337.4, 118.2, 884.6, 10.1,  226.5, 666.3, 996.3, 448.6, 777.0, 558.2, 0.4,
                                   0.6,   775.5, 666.9, 338.0, 447.5, 11.6,  556.0, 228.1, 995.8, 887.6, 120.2, 0.3,
                                   0.3,   556.8, 339.1, 887.2, 999.0, 779.0, 11.1,  118.3, 229.2, 669.1, 448.9, 0.5};
    const std::vector<double> y = {0.1,   338.8, 118.1, 888.0, 9.2,   228.1, 668.5, 998.5, 449.1, 778.9, 559.2, 0.3,
                                   0.1,   778.1, 668.8, 339.3, 448.9, 10.8,  557.7, 228.3, 998.0, 888.8, 119.6, 0.3,
                                   0.6,   557.6, 339.3, 888.0, 998.5, 778.9, 10.2,  117.6, 228.9, 668.4, 449.2, 0.2};
    const Analysis a = must_analyse(request(ModelId::Linear, points(x, y)));
    CHECK(a.linear.has_value());
    if (!a.linear) return;
    const LinearRegression& L = *a.linear;
    const double rel = 1e-9;  // at least 9 correct significant digits

    // Certified by NIST.
    CHECK_REL(L.intercept, -0.262323073774029, rel);
    CHECK_REL(L.slope, 1.00211681802045, rel);
    CHECK_REL(L.r * L.r, 0.999993745883712, rel);
    CHECK_REL(L.ssRegression, 4255954.13232369, rel);
    CHECK_REL(a.fit.sse, 26.6173985294224, rel);
    CHECK_REL(a.fit.stdDev, 0.884796396144373, rel);  // residual SD, used to flag large residuals

    // Certified standard deviations and ANOVA.
    CHECK(L.df == 34);
    CHECK_REL(L.sdIntercept, 0.232818234301152, rel);
    CHECK_REL(L.sdSlope, 0.429796848199937e-3, rel);
    CHECK_REL(L.msError, 0.782864662630069, rel);
    CHECK_REL(L.fRegression, 5436385.54079785, rel);

    // The lack-of-fit split for the repeated X value.
    CHECK(L.replicates);
    CHECK(L.groups == 35);
    CHECK(L.dfDeviation == 33 && L.dfPureError == 1);
    CHECK_NEAR(L.ssPureError, 0.045, 1e-9);
    CHECK_REL(L.ssDeviation, 26.6173985294224 - 0.045, rel);
    CHECK_REL(L.fLackOfFit, (26.5723985294224 / 33.0) / 0.045, 1e-8);

    const auto rows = a.parameter_rows();  // Intercept, Slope
    CHECK_REL(rows[0].value, -0.262323073774029, rel);
    CHECK_REL(rows[0].sd, 0.232818234301152, rel);
    CHECK_REL(rows[1].value, 1.00211681802045, rel);
    CHECK_REL(rows[1].sd, 0.429796848199937e-3, rel);
}

// NIST StRD "NoInt1" and "NoInt2": y = B1*x, fitted with the intercept
// fixed at 0 (regression through the origin). No replicated X values, so the
// standard deviations are the ordinary ones and match NIST exactly.
TEST_CASE(validation_nist_noint, "validation: NIST StRD NoInt1 and NoInt2 (through the origin)") {
    struct Case {
        std::vector<double> x, y;
        double b1, sdB1, residualSd;
    };
    std::vector<double> x1, y1;
    for (int i = 0; i <= 10; ++i) {
        x1.push_back(60.0 + i);
        y1.push_back(130.0 + i);
    }
    const Case cases[] = {
        {x1, y1, 2.07438016528926, 0.165289256198347e-1, 3.56753034006338},
        {{4, 5, 6}, {3, 4, 4}, 0.727272727272727, 0.420827318078432e-1, 0.369274472937998},
    };
    for (const auto& c : cases) {
        AnalysisRequest r = request(ModelId::Linear, points(c.x, c.y));
        r.params[0].fixed = true;
        r.params[0].value = 0.0;
        const Analysis a = must_analyse(r);
        CHECK(a.linear.has_value() && a.linear->interceptFixed);
        const auto rows = a.parameter_rows();
        CHECK(rows[0].fixed && rows[0].value == 0.0);
        CHECK_REL(rows[1].value, c.b1, 1e-12);
        CHECK_REL(rows[1].sd, c.sdB1, 1e-12);
        CHECK_REL(a.fit.stdDev, c.residualSd, 1e-12);
        CHECK(a.fit.dof == static_cast<int>(c.x.size()) - 1);
    }
}

namespace {

// Bell, Smith & Storey (2014), Data S1, sheet "Arrhenius Data": GAPDH
// activity (U/mg) at 5-35 C, one row per enzyme preparation.
const std::vector<double> kGapdhTemps = {5, 10, 15, 20, 25, 30, 35};

const std::vector<std::vector<double>> kGapdhLongTorpor = {
    {11.84061697, 18.91002571, 10.94858612, 19.40874036, 98.99742931, 70.25706941, 166.2724936},
    {12.13367609, 19.7840617, 24.07712082, 35.03856041, 86.73521851, 131.1053985, 160.2827763},
    {19.63239075, 20.2159383, 22.84832905, 29.04884319, 96.19537275, 100.848329, 91.31105398},
    {17.07712082, 16.70951157, 23.97172237, 28.07197943, 104.5758355, 121.1053985, 61.1311054},
    {19.66323907, 19.6940874, 23.89460154, 39.40874036, 110.3856041, 68.27763496, 105.2699229},
};
// numpy.polyfit of log10(v) on 1/(T + 273.15), Ea = -slope * ln(10) * R,
// R = 8.314462618 / 4184 kcal/(mol K): {Ea in kcal/mol, log10(A)} per row.
const std::vector<std::pair<double, double>> kGapdhLongTorporRef = {
    {15.4035149145, 13.0311879749}, {15.5604945313, 13.2664403663}, {11.2446011853, 10.0167065098},
    {11.3012355023, 10.0310072025}, {11.0082553899, 9.8536438081},
};

const std::vector<std::vector<double>> kGapdhControl = {
    {38.24028269, 26.48763251, 43.4204947, 42.44522968, 63.87279152, 92.29681979, 111.8727915},
    {31.71024735, 35.32155477, 43.12367491, 61.01766784, 63.93639576, 97.24381625, 171.0954064},
    {46.65017668, 47.97879859, 55.66077739, 53.83745583, 72.86219081, 96.32508834, 156.3250883},
    {37.06713781, 24.33215548, 50.03533569, 50.0, 60.27561837, 127.3498233, 152.2968198},
};
const std::vector<std::pair<double, double>> kGapdhControlRef = {
    {7.35882816953, 7.21964486967}, {9.03562070557, 8.52915559373},
    {6.36095310094, 6.58578168459}, {9.33498612858, 8.74187893618},
};

// Fits each preparation with the Arrhenius equation and returns Ea in kJ/mol.
std::vector<double> fit_arrhenius_rows(const std::vector<std::vector<double>>& rows,
                                       const std::vector<std::pair<double, double>>& ref) {
    std::vector<double> eaKj;
    for (size_t i = 0; i < rows.size(); ++i) {
        std::vector<double> logv;
        for (double v : rows[i]) logv.push_back(std::log10(v));
        const Analysis a = must_analyse(request(ModelId::Arrhenius, points(kGapdhTemps, logv)));
        CHECK(a.fit.status == FitStatus::Converged);
        CHECK_REL(a.fit.p[1], ref[i].first, 1e-6);   // Ea, kcal/mol
        CHECK_REL(a.fit.p[0], ref[i].second, 1e-6);  // log10(A)
        eaKj.push_back(a.fit.p[1] * 4.184);
    }
    return eaKj;
}

}  // namespace

// Table 3 of the paper gives Ea = 54 +/- 2 kJ/mol (mean +/- SEM) for GAPDH
// from long-torpor animals. Fitting each of the five preparations in the
// supplement and averaging reproduces the mean, 53.99 kJ/mol, to the
// published precision. The SEM of these five fits is 4.4 rather than 2, so
// the published SEM may come from a different set of preparations.
TEST_CASE(validation_gapdh_torpor, "validation: GAPDH Arrhenius, long torpor (Bell et al. 2014)") {
    const auto ea = fit_arrhenius_rows(kGapdhLongTorpor, kGapdhLongTorporRef);
    const double mean = std::accumulate(ea.begin(), ea.end(), 0.0) / static_cast<double>(ea.size());
    CHECK_NEAR(mean, 54.0, 0.5);
}

// For the euthermic controls the paper reports Ea = 28 +/- 2 kJ/mol. The
// supplementary rates do not give that with any straightforward fit: the
// mean of the four per-preparation fits is 33.6 +/- 2.9 kJ/mol (numpy and
// this program agree), and the fit to the column means is 33.3. So only the
// fits themselves are checked here; the published value is not reproduced.
TEST_CASE(validation_gapdh_control, "validation: GAPDH Arrhenius, euthermic control fits (Bell et al. 2014)") {
    const auto ea = fit_arrhenius_rows(kGapdhControl, kGapdhControlRef);
    const double mean = std::accumulate(ea.begin(), ea.end(), 0.0) / static_cast<double>(ea.size());
    CHECK_NEAR(mean, 33.566546, 1e-4);
}
