#include "core/stats.hpp"

#include <algorithm>
#include <cmath>

namespace kin {
namespace {

// Continued fraction for the incomplete beta function (modified Lentz).
double beta_cf(double a, double b, double x) {
    constexpr int kMaxIter = 300;
    constexpr double kEps = 3e-16;
    constexpr double kFpMin = 1e-300;
    const double qab = a + b, qap = a + 1.0, qam = a - 1.0;
    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (std::fabs(d) < kFpMin) d = kFpMin;
    d = 1.0 / d;
    double h = d;
    for (int m = 1; m <= kMaxIter; ++m) {
        const int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < kFpMin) d = kFpMin;
        c = 1.0 + aa / c;
        if (std::fabs(c) < kFpMin) c = kFpMin;
        d = 1.0 / d;
        h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d;
        if (std::fabs(d) < kFpMin) d = kFpMin;
        c = 1.0 + aa / c;
        if (std::fabs(c) < kFpMin) c = kFpMin;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::fabs(del - 1.0) < kEps) break;
    }
    return h;
}

struct Group {
    double x;
    double meanY;
    int count;
};

std::vector<Group> group_by_x(std::span<const Point> sorted) {
    std::vector<Group> out;
    for (size_t i = 0; i < sorted.size();) {
        size_t j = i;
        double sum = 0.0;
        while (j < sorted.size() && sorted[j].x == sorted[i].x) sum += sorted[j++].y;
        const int n = static_cast<int>(j - i);
        out.push_back({sorted[i].x, sum / n, n});
        i = j;
    }
    return out;
}

std::vector<Point> sorted_copy(std::span<const Point> data) {
    std::vector<Point> v(data.begin(), data.end());
    std::sort(v.begin(), v.end(), [](const Point& a, const Point& b) {
        return a.x < b.x || (a.x == b.x && a.y < b.y);
    });
    return v;
}

// Regression F. With no residual variance the line is exact: F is infinite
// if it explains anything, and 0 (P = 1) if Y is constant.
double regression_f(const LinearRegression& lr) {
    if (lr.msError > 0.0) return lr.ssRegression / lr.msError;
    return lr.ssRegression > 0.0 ? INFINITY : 0.0;
}

void fill_t_tests(LinearRegression& lr) {
    // With a zero SD the estimate is exact: a non-zero difference is as
    // significant as it gets, but a difference of exactly 0 is no evidence
    // at all (0/0), so t = 0 and P = 1.
    auto t = [](double num, double sd) { return sd > 0.0 ? std::fabs(num) / sd : (num != 0.0 ? 1e6 : 0.0); };
    lr.tSlopeVs0 = t(lr.slope, lr.sdSlope);
    lr.tSlopeVs1 = t(lr.slope - 1.0, lr.sdSlope);
    lr.pSlopeVs0 = t_two_tailed_p(lr.tSlopeVs0, lr.df);
    lr.pSlopeVs1 = t_two_tailed_p(lr.tSlopeVs1, lr.df);
    if (!lr.interceptFixed) {
        lr.tIntercept = t(lr.intercept, lr.sdIntercept);
        lr.pIntercept = t_two_tailed_p(lr.tIntercept, lr.df);
    }
}

}  // namespace

double incomplete_beta(double a, double b, double x) {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    const double lbt = std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) + a * std::log(x) +
                       b * std::log(1.0 - x);
    const double bt = std::exp(lbt);
    if (x < (a + 1.0) / (a + b + 2.0)) return bt * beta_cf(a, b, x) / a;
    return 1.0 - bt * beta_cf(b, a, 1.0 - x) / b;
}

double t_two_tailed_p(double t, int df) {
    if (df <= 0 || !std::isfinite(t)) return std::isinf(t) ? 0.0 : 1.0;
    const double v = df;
    return incomplete_beta(v / 2.0, 0.5, v / (v + t * t));
}

double f_upper_p(double f, int dfn, int dfd) {
    if (dfn <= 0 || dfd <= 0 || !(f >= 0.0)) return 1.0;
    if (std::isinf(f)) return 0.0;
    const double v1 = dfn, v2 = dfd;
    return incomplete_beta(v2 / 2.0, v1 / 2.0, v2 / (v2 + v1 * f));
}

double p_bin(double p) {
    for (double bin : {0.001, 0.005, 0.01, 0.025, 0.05, 0.1})
        if (p < bin) return bin;
    return 1.0;
}

double combinations(int n, int r) {
    if (r < 0 || n < 0 || r > n) return 0.0;
    r = std::min(r, n - r);
    double c = 1.0;
    for (int i = 1; i <= r; ++i) c = c * (n - r + i) / i;
    return c;
}

LinearRegression linear_regression(std::span<const Point> data) {
    LinearRegression lr;
    lr.n = static_cast<int>(data.size());
    if (lr.n <= 2) {
        lr.error = "Linear regression needs at least 3 data pairs.";
        return lr;
    }
    const auto sorted = sorted_copy(data);
    const auto groups = group_by_x(sorted);
    lr.groups = static_cast<int>(groups.size());
    if (lr.groups < 2) {
        lr.error = "Linear regression needs at least 2 different X values.";
        return lr;
    }

    const double n = lr.n;
    double sx = 0, sx2 = 0, sy = 0, sy2 = 0, sxy = 0;
    for (const auto& pt : data) {
        sx += pt.x;
        sx2 += pt.x * pt.x;
        sy += pt.y;
        sy2 += pt.y * pt.y;
        sxy += pt.x * pt.y;
    }
    const double sxx = sx2 - sx * sx / n;
    const double syy = sy2 - sy * sy / n;
    const double sxyc = sxy - sx * sy / n;

    lr.ssTotal = syy;
    lr.dfTotal = lr.n - 1;
    lr.ssRegression = sxyc * sxyc / sxx;
    lr.replicates = lr.groups < lr.n;

    // The standard deviations, t tests and regression F use the ordinary
    // residual mean square with df = n - 2, as NIST and statistics packages
    // do. (With replicated X values, v3.51 used the deviations-from-
    // regression mean square below, df = groups - 2, and an arbitrary 1e5
    // when there were only two distinct X values.)
    lr.msError = (syy - lr.ssRegression) / (lr.n - 2);
    lr.df = lr.n - 2;

    if (!lr.replicates) {
        lr.ssDeviation = syy - lr.ssRegression;
        lr.dfDeviation = lr.n - 2;
    } else {
        // Sokal & Rohlf: split the residual into deviations of the group
        // means from the line (lack of fit) and variation within groups,
        // and test the first against the second.
        double ssGroupMeans = 0.0;
        for (const auto& g : groups) ssGroupMeans += g.count * g.meanY * g.meanY;
        lr.ssGroups = ssGroupMeans - sy * sy / n;
        lr.ssPureError = lr.ssTotal - lr.ssGroups;
        lr.ssDeviation = lr.ssGroups - lr.ssRegression;
        lr.dfGroups = lr.groups - 1;
        lr.dfDeviation = lr.groups - 2;
        lr.dfPureError = lr.n - lr.groups;
        if (lr.dfDeviation > 0 && lr.ssPureError > 0.0) {
            lr.fLackOfFit = (lr.ssDeviation / lr.dfDeviation) / (lr.ssPureError / lr.dfPureError);
            lr.pLackOfFit = f_upper_p(lr.fLackOfFit, lr.dfDeviation, lr.dfPureError);
        }
    }

    lr.slope = sxyc / sxx;
    lr.intercept = (sy - lr.slope * sx) / n;
    lr.sdSlope = std::sqrt(lr.msError / sxx);
    const double meanX = sx / n;
    lr.sdIntercept = std::sqrt(lr.msError * (1.0 / n + meanX * meanX / sxx));
    lr.r = (syy > 0.0) ? sxyc / std::sqrt(sxx * syy) : 0.0;
    lr.sdR = std::sqrt(std::max(0.0, 1.0 - lr.r * lr.r) / (n - 2.0));
    lr.fRegression = regression_f(lr);
    lr.pRegression = f_upper_p(lr.fRegression, 1, lr.df);
    fill_t_tests(lr);
    lr.ok = true;
    return lr;
}

LinearRegression regression_through_point(std::span<const Point> data, double intercept) {
    LinearRegression lr;
    lr.interceptFixed = true;
    lr.n = static_cast<int>(data.size());
    if (lr.n < 2) {
        lr.error = "Regression with a fixed intercept needs at least 2 data pairs.";
        return lr;
    }
    double sx2 = 0, sy2 = 0, sxy = 0;
    for (const auto& pt : data) {
        const double ys = pt.y - intercept;
        sx2 += pt.x * pt.x;
        sy2 += ys * ys;
        sxy += pt.x * ys;
    }
    if (sx2 == 0.0) {
        lr.error = "All X values are zero.";
        return lr;
    }
    lr.groups = static_cast<int>(group_by_x(sorted_copy(data)).size());
    lr.dfTotal = lr.n;
    lr.dfDeviation = lr.n - 1;
    lr.df = lr.dfDeviation;
    lr.ssTotal = sy2;
    lr.ssRegression = sxy * sxy / sx2;
    lr.ssDeviation = lr.ssTotal - lr.ssRegression;
    lr.msError = lr.ssDeviation / lr.dfDeviation;
    lr.intercept = intercept;
    lr.sdIntercept = 0.0;
    lr.slope = sxy / sx2;
    lr.sdSlope = std::sqrt(lr.msError / sx2);
    lr.r = (sy2 > 0.0) ? sxy / std::sqrt(sx2 * sy2) : 0.0;
    lr.sdR = lr.n > 2 ? std::sqrt(std::max(0.0, 1.0 - lr.r * lr.r) / (lr.n - 2.0)) : NAN;
    lr.fRegression = regression_f(lr);
    lr.pRegression = f_upper_p(lr.fRegression, 1, lr.df);
    fill_t_tests(lr);
    lr.ok = true;
    return lr;
}

ResidualTests residual_tests(ModelId model, std::span<const Point> data, std::span<const double> p) {
    ResidualTests rt;
    char last = ' ';
    for (const auto& g : group_by_x(data)) {
        const double z = g.meanY - evaluate(model, g.x, p);
        if (z > 0.0) {
            ++rt.positives;
            rt.sequence += '+';
            if (last != '+') ++rt.runs;
            last = '+';
        } else if (z < 0.0) {
            ++rt.negatives;
            rt.sequence += '-';
            if (last != '-') ++rt.runs;
            last = '-';
        } else {
            rt.sequence += '0';
        }
    }

    // Sign test: are + and - residuals equally likely?
    const int total = rt.positives + rt.negatives;
    const int k = std::max(rt.positives, rt.negatives);
    double tail = 0.0;
    for (int i = k; i <= total; ++i) tail += combinations(total, i);
    rt.signP = total > 0 ? std::min(1.0, 2.0 * tail * std::pow(0.5, total)) : 1.0;
    rt.signsRandom = rt.signP >= 0.05;

    // Runs test (exact): probability of this few runs or fewer.
    rt.runsTestable = total >= 8;
    if (rt.runsTestable) {
        const int m = std::min(rt.positives, rt.negatives);
        const int n = std::max(rt.positives, rt.negatives);
        double prob = 0.0;
        if (m >= 1) {
            for (int u = 2; u <= rt.runs; ++u) {
                if (u % 2 == 0) {
                    const int kk = u / 2;
                    prob += 2.0 * combinations(m - 1, kk - 1) * combinations(n - 1, kk - 1);
                } else {
                    const int kk = (u - 1) / 2;
                    prob += combinations(m - 1, kk) * combinations(n - 1, kk - 1) +
                            combinations(m - 1, kk - 1) * combinations(n - 1, kk);
                }
            }
            prob /= combinations(m + n, m);
        }
        rt.runsP = prob;
        rt.runsRandom = prob > 0.05;
    }
    return rt;
}

}  // namespace kin
