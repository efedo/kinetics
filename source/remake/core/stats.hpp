#pragma once
// Statistics from STAT.CPP: Student t and F probabilities, linear regression
// (with a lack-of-fit split when X values are replicated), and the two
// non-parametric residual tests (runs test and sign test).

#include <span>
#include <string>
#include <vector>

#include "core/models.hpp"

namespace kin {

// Regularized incomplete beta function I_x(a, b).
double incomplete_beta(double a, double b, double x);

// Two-tailed probability of |T| >= t for Student's t with df degrees of freedom.
double t_two_tailed_p(double t, int df);

// Upper-tail probability of F >= f with (dfn, dfd) degrees of freedom.
double f_upper_p(double f, int dfn, int dfd);

// The original program reports significance in bins: P < 0.001, 0.005, 0.01,
// 0.025, 0.05, 0.1, otherwise "not significant". Returns the bin (or 1.0).
double p_bin(double p);

// n choose r (0 when r < 0 or r > n).
double combinations(int n, int r);

struct LinearRegression {
    bool ok = false;
    std::string error;
    bool interceptFixed = false;

    double intercept = 0.0, sdIntercept = 0.0;
    double slope = 0.0, sdSlope = 0.0;
    double r = 0.0, sdR = 0.0;   // correlation coefficient
    double msError = 0.0;        // residual mean square, used for the SDs and tests
    int df = 0;                  // its degrees of freedom, n - 2 (n - 1 through a point)

    // t statistics (absolute values) and two-tailed probabilities.
    double tSlopeVs0 = 0.0, pSlopeVs0 = 1.0;
    double tSlopeVs1 = 0.0, pSlopeVs1 = 1.0;
    double tIntercept = 0.0, pIntercept = 1.0;

    // Analysis of variance.
    int n = 0;
    int groups = 0;              // distinct X values
    bool replicates = false;     // some X values repeated
    double ssTotal = 0.0, ssRegression = 0.0, ssDeviation = 0.0;
    double ssGroups = 0.0, ssPureError = 0.0;
    int dfTotal = 0, dfRegression = 1, dfDeviation = 0, dfGroups = 0, dfPureError = 0;
    double fRegression = 0.0, pRegression = 1.0;
    double fLackOfFit = 0.0, pLackOfFit = 1.0;  // only with replicates
};

LinearRegression linear_regression(std::span<const Point> data);
LinearRegression regression_through_point(std::span<const Point> data, double intercept);

struct ResidualTests {
    std::string sequence;    // '+', '-' or '0' per distinct X (replicates averaged)
    int positives = 0;
    int negatives = 0;
    int runs = 0;
    bool runsTestable = false;  // needs at least 8 non-zero residuals
    double runsP = 1.0;         // P(R <= observed runs)
    bool runsRandom = true;     // runsP > 0.05
    double signP = 1.0;         // two-tailed sign (binomial) test
    bool signsRandom = true;    // signP >= 0.05
};

// data must be sorted by x.
ResidualTests residual_tests(ModelId model, std::span<const Point> data, std::span<const double> p);

}  // namespace kin
