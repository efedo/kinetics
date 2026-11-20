#include "core/linalg.hpp"

#include <cmath>
#include <utility>

namespace kin {
namespace {

// In-place LU decomposition (Doolittle, scaled partial pivoting).
bool lu_decompose(Matrix& a, std::vector<int>& perm) {
    const int n = a.rows();
    perm.resize(static_cast<size_t>(n));
    std::vector<double> scale(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        double big = 0.0;
        for (int j = 0; j < n; ++j) big = std::fmax(big, std::fabs(a(i, j)));
        if (big == 0.0) return false;
        scale[static_cast<size_t>(i)] = 1.0 / big;
        perm[static_cast<size_t>(i)] = i;
    }
    for (int k = 0; k < n; ++k) {
        int pivot = k;
        double best = 0.0;
        for (int i = k; i < n; ++i) {
            const double v = scale[static_cast<size_t>(i)] * std::fabs(a(i, k));
            if (v > best) {
                best = v;
                pivot = i;
            }
        }
        if (best == 0.0) return false;
        if (pivot != k) {
            for (int j = 0; j < n; ++j) std::swap(a(pivot, j), a(k, j));
            std::swap(scale[static_cast<size_t>(pivot)], scale[static_cast<size_t>(k)]);
            std::swap(perm[static_cast<size_t>(pivot)], perm[static_cast<size_t>(k)]);
        }
        for (int i = k + 1; i < n; ++i) {
            a(i, k) /= a(k, k);
            for (int j = k + 1; j < n; ++j) a(i, j) -= a(i, k) * a(k, j);
        }
    }
    // Reject near-singular systems (relative to the largest pivot).
    double maxPivot = 0.0, minPivot = INFINITY;
    for (int k = 0; k < n; ++k) {
        maxPivot = std::fmax(maxPivot, std::fabs(a(k, k)));
        minPivot = std::fmin(minPivot, std::fabs(a(k, k)));
    }
    return minPivot > maxPivot * 1e-15;
}

void lu_solve(const Matrix& lu, const std::vector<int>& perm, std::vector<double>& b) {
    const int n = lu.rows();
    std::vector<double> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        double s = b[static_cast<size_t>(perm[static_cast<size_t>(i)])];
        for (int j = 0; j < i; ++j) s -= lu(i, j) * x[static_cast<size_t>(j)];
        x[static_cast<size_t>(i)] = s;
    }
    for (int i = n - 1; i >= 0; --i) {
        double s = x[static_cast<size_t>(i)];
        for (int j = i + 1; j < n; ++j) s -= lu(i, j) * x[static_cast<size_t>(j)];
        x[static_cast<size_t>(i)] = s / lu(i, i);
    }
    b = std::move(x);
}

}  // namespace

bool solve(Matrix a, std::vector<double>& b) {
    std::vector<int> perm;
    if (!lu_decompose(a, perm)) return false;
    lu_solve(a, perm, b);
    for (double v : b)
        if (!std::isfinite(v)) return false;
    return true;
}

bool invert(const Matrix& a, Matrix& out) {
    const int n = a.rows();
    Matrix lu = a;
    std::vector<int> perm;
    if (!lu_decompose(lu, perm)) return false;
    out = Matrix(n, n);
    for (int j = 0; j < n; ++j) {
        std::vector<double> col(static_cast<size_t>(n), 0.0);
        col[static_cast<size_t>(j)] = 1.0;
        lu_solve(lu, perm, col);
        for (int i = 0; i < n; ++i) {
            out(i, j) = col[static_cast<size_t>(i)];
            if (!std::isfinite(out(i, j))) return false;  // as solve() does
        }
    }
    return true;
}

}  // namespace kin
