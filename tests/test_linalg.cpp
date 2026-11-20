// Tests for core/linalg: LU solve and inverse with scaled partial pivoting.
//
// References are systems with exactly known solutions, inverses checked
// against closed forms and by A * inv(A) = I, and singular, near-singular and
// overflowing systems.

#include <cmath>
#include <initializer_list>
#include <limits>
#include <vector>

#include "core/linalg.hpp"

#include "check.hpp"

using kin::Matrix;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Square matrix from row-major values.
Matrix square(int n, std::initializer_list<double> values) {
    Matrix m(n, n);
    int k = 0;
    for (double v : values) {
        m(k / n, k % n) = v;
        ++k;
    }
    return m;
}

// Largest |(A * B - I)_ij|.
double identity_error(const Matrix& a, const Matrix& b) {
    const int n = a.rows();
    double worst = 0.0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            double s = 0.0;
            for (int k = 0; k < n; ++k) s += a(i, k) * b(k, j);
            worst = std::fmax(worst, std::fabs(s - (i == j ? 1.0 : 0.0)));
        }
    return worst;
}

// Largest |A x - b|.
double residual(const Matrix& a, const std::vector<double>& x, const std::vector<double>& b) {
    double worst = 0.0;
    for (int i = 0; i < a.rows(); ++i) {
        double s = 0.0;
        for (int j = 0; j < a.cols(); ++j) s += a(i, j) * x[static_cast<size_t>(j)];
        worst = std::fmax(worst, std::fabs(s - b[static_cast<size_t>(i)]));
    }
    return worst;
}

}  // namespace

TEST_CASE(linalg_matrix_storage, "linalg: Matrix has the given shape and fill and stores entries row-major") {
    const Matrix empty;
    CHECK(empty.rows() == 0 && empty.cols() == 0);
    Matrix m(2, 3, 1.5);
    CHECK(m.rows() == 2 && m.cols() == 3);
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 3; ++j) CHECK(m(i, j) == 1.5);
    m(1, 2) = 7.0;
    m(0, 1) = -2.0;
    const Matrix& c = m;
    CHECK(c(1, 2) == 7.0 && c(0, 1) == -2.0);
    CHECK(c(0, 2) == 1.5 && c(1, 0) == 1.5 && c(1, 1) == 1.5);  // neighbours untouched
}

TEST_CASE(linalg_solve_3x3, "linalg: solve recovers the exact solution of a 3x3 system") {
    // 2x + y - z = 8, -3x - y + 2z = -11, -2x + y + 2z = -3  =>  (2, 3, -1).
    const Matrix a = square(3, {2, 1, -1, -3, -1, 2, -2, 1, 2});
    std::vector<double> b = {8, -11, -3};
    CHECK(kin::solve(a, b));
    CHECK_NEAR(b[0], 2.0, 1e-14);
    CHECK_NEAR(b[1], 3.0, 1e-14);
    CHECK_NEAR(b[2], -1.0, 1e-14);
    CHECK(a(0, 0) == 2.0 && a(2, 2) == 2.0);  // A is taken by value: the caller's copy is unchanged
}

TEST_CASE(linalg_solve_4x4, "linalg: solve recovers x = (1, -2, 3, -4) of a 4x4 system") {
    // b = A x computed by hand: (-1, -29, 44, -15).
    const Matrix a = square(4, {4, -2, 1, 3, 3, 6, -4, 2, 2, 1, 8, -5, 1, -3, 2, 7});
    const std::vector<double> rhs = {-1, -29, 44, -15};
    std::vector<double> x = rhs;
    CHECK(kin::solve(a, x));
    const double want[] = {1, -2, 3, -4};
    for (size_t i = 0; i < 4; ++i) CHECK_NEAR(x[i], want[i], 1e-13);
    CHECK(residual(a, x, rhs) < 1e-13);
}

TEST_CASE(linalg_solve_pivoting, "linalg: solve pivots past zero diagonals and uses row-scaled pivots") {
    // Zero on the diagonal: needs a row exchange.
    std::vector<double> b = {3, 7};
    CHECK(kin::solve(square(2, {0, 1, 1, 0}), b));
    CHECK(b[0] == 7.0 && b[1] == 3.0);
    std::vector<double> c = {2, 3, 4};
    CHECK(kin::solve(square(3, {0, 0, 2, 0, 3, 0, 4, 0, 0}), c));
    CHECK_NEAR(c[0], 1.0, 1e-15);
    CHECK_NEAR(c[1], 1.0, 1e-15);
    CHECK_NEAR(c[2], 1.0, 1e-15);

    // Tiny leading pivot: elimination without pivoting gives x0 = 0 here;
    // the exact solution is 1/(1 - 1e-17) ~ 1 and (1 - 2e-17)/(1 - 1e-17) ~ 1.
    std::vector<double> d = {1, 2};
    CHECK(kin::solve(square(2, {1e-17, 1, 1, 1}), d));
    CHECK_NEAR(d[0], 1.0, 1e-15);
    CHECK_NEAR(d[1], 1.0, 1e-15);

    // Row scaling: plain partial pivoting would pick row 0 (|2| > |1|) and
    // lose x0 to cancellation (relative error 1e-10, checked in Python with
    // fractions); scaled pivoting picks row 1. Exact solution:
    //   x0 = 1e10 / (1e10 - 1), x1 = (1e10 - 2) / (1e10 - 1).
    std::vector<double> e = {2e10, 2};
    CHECK(kin::solve(square(2, {2, 2e10, 1, 1}), e));
    CHECK_REL(e[0], 1e10 / (1e10 - 1), 1e-15);
    CHECK_REL(e[1], (1e10 - 2) / (1e10 - 1), 1e-15);
}

TEST_CASE(linalg_invert_exact, "linalg: invert reproduces the integer inverse of a unimodular matrix") {
    // det = 1; inverse from the adjugate.
    const Matrix a = square(3, {1, 2, 3, 0, 1, 4, 5, 6, 0});
    const double want[3][3] = {{-24, 18, 5}, {20, -15, -4}, {-5, 4, 1}};
    Matrix inv;
    CHECK(kin::invert(a, inv));
    CHECK(inv.rows() == 3 && inv.cols() == 3);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) CHECK_NEAR(inv(i, j), want[i][j], 1e-12);
    CHECK(identity_error(a, inv) < 1e-12);
    CHECK(identity_error(inv, a) < 1e-12);
}

TEST_CASE(linalg_invert_hilbert, "linalg: invert the 4x4 Hilbert matrix (known integer inverse)") {
    // H_ij = 1/(i+j+1); its inverse has the classical integer entries below
    // (condition number about 1.6e4).
    Matrix h(4, 4);
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) h(i, j) = 1.0 / (i + j + 1);
    const double want[4][4] = {
        {16, -120, 240, -140}, {-120, 1200, -2700, 1680}, {240, -2700, 6480, -4200}, {-140, 1680, -4200, 2800}};
    Matrix inv;
    CHECK(kin::invert(h, inv));
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) CHECK_REL(inv(i, j), want[i][j], 1e-10);
    CHECK(identity_error(h, inv) < 1e-10);
}

TEST_CASE(linalg_ill_conditioned, "linalg: an ill-conditioned but representable system is solved exactly") {
    // A = [[1, 1], [1, 1 + e]] with e = 2^-30 (condition ~4.3e9). Every LU
    // step is exact in binary, so x = (1, 1) and
    // inv(A) = (1/e) [[1 + e, -1], [-1, 1]] = [[1 + 2^30, -2^30], [-2^30, 2^30]].
    const double e = std::ldexp(1.0, -30);
    const Matrix a = square(2, {1, 1, 1, 1 + e});
    std::vector<double> b = {2, 2 + e};
    CHECK(kin::solve(a, b));
    CHECK(b[0] == 1.0 && b[1] == 1.0);
    Matrix inv;
    CHECK(kin::invert(a, inv));
    const double big = std::ldexp(1.0, 30);
    CHECK(inv(0, 0) == 1 + big && inv(0, 1) == -big && inv(1, 0) == -big && inv(1, 1) == big);
}

TEST_CASE(linalg_singular, "linalg: singular matrices are rejected by solve and invert") {
    Matrix inv;
    // A zero row.
    const Matrix zeroRow = square(2, {1, 2, 0, 0});
    std::vector<double> b = {1, 1};
    CHECK(!kin::solve(zeroRow, b));
    CHECK(!kin::invert(zeroRow, inv));
    std::vector<double> z = {1, 1, 1};
    CHECK(!kin::solve(Matrix(3, 3), z));  // the zero matrix
    // Dependent rows: elimination leaves an exact zero pivot (4 - 2*2).
    const Matrix dependent = square(2, {1, 2, 2, 4});
    std::vector<double> c = {1, 2};
    CHECK(!kin::solve(dependent, c));
    CHECK(!kin::invert(dependent, inv));
    // Rank 2 of 3: the last pivot is zero or rounding-sized.
    const Matrix rank2 = square(3, {1, 2, 3, 4, 5, 6, 7, 8, 9});
    std::vector<double> d = {1, 2, 3};
    CHECK(!kin::solve(rank2, d));
    CHECK(!kin::invert(rank2, inv));
}

TEST_CASE(linalg_near_singular_threshold, "linalg: pivots below 1e-15 of the largest count as singular") {
    // [[1, 1], [1, 1 + e]]: the second pivot is exactly e. cond(A) ~ 4/e, so
    // e = 2^-52 (cond ~1.8e16 > 1/eps) is singular to working precision,
    // while e = 2^-49 (pivot 1.8e-15) is still solved exactly.
    const double tiny = std::ldexp(1.0, -52);
    std::vector<double> b = {2, 2 + tiny};
    CHECK(!kin::solve(square(2, {1, 1, 1, 1 + tiny}), b));
    Matrix inv;
    CHECK(!kin::invert(square(2, {1, 1, 1, 1 + tiny}), inv));
    const double small = std::ldexp(1.0, -49);
    std::vector<double> c = {2, 2 + small};
    CHECK(kin::solve(square(2, {1, 1, 1, 1 + small}), c));
    CHECK(c[0] == 1.0 && c[1] == 1.0);
}

TEST_CASE(linalg_nonfinite_solution, "linalg: solve fails when the solution overflows or b is not finite") {
    // diag(1e-300, 1e-300) is perfectly conditioned, but x0 = 1e300 / 1e-300
    // exceeds the double range.
    std::vector<double> b = {1e300, 1};
    CHECK(!kin::solve(square(2, {1e-300, 0, 0, 1e-300}), b));
    // The same matrix with a representable solution is fine: x = (1e299, 1e300).
    std::vector<double> ok = {1e-1, 1};
    CHECK(kin::solve(square(2, {1e-300, 0, 0, 1e-300}), ok));
    CHECK_REL(ok[0], 1e299, 1e-15);
    CHECK_REL(ok[1], 1e300, 1e-15);
    // Non-finite right-hand sides.
    const Matrix id = square(2, {1, 0, 0, 1});
    std::vector<double> withNaN = {kNaN, 1};
    CHECK(!kin::solve(id, withNaN));
    std::vector<double> withInf = {kInf, 0};
    CHECK(!kin::solve(id, withInf));
}

// The matrix below is triangular with equal pivots, so it passes the pivot
// test, but its inverse has the entry -1e300 / (1e-10 * 1e-10) = -1e320,
// which overflows to -inf. solve() checks its result and rejects the
// equivalent system; invert() used to report success (found by this test,
// now fixed).
TEST_CASE(linalg_invert_overflow, "linalg: invert rejects an inverse with an infinite entry, as solve does") {
    const Matrix a = square(2, {1e-10, 1e300, 0, 1e-10});
    std::vector<double> e1 = {0, 1};  // second column of inv(A)
    CHECK(!kin::solve(a, e1));        // solve: not representable -> false
    Matrix inv;
    const bool ok = kin::invert(a, inv);
    bool finite = true;
    for (int i = 0; ok && i < 2; ++i)
        for (int j = 0; j < 2; ++j) finite = finite && std::isfinite(inv(i, j));
    // Expected: invert agrees with solve (returns false), or every entry is finite.
    CHECK(!ok || finite);
}
