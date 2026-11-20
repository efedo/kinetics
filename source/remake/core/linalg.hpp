#pragma once
// Small dense linear algebra for the normal equations (at most 4 x 4).

#include <vector>

namespace kin {

class Matrix {
public:
    Matrix() = default;
    Matrix(int rows, int cols, double fill = 0.0)
        : rows_(rows), cols_(cols), data_(static_cast<size_t>(rows * cols), fill) {}

    int rows() const { return rows_; }
    int cols() const { return cols_; }
    double& operator()(int r, int c) { return data_[static_cast<size_t>(r * cols_ + c)]; }
    double operator()(int r, int c) const { return data_[static_cast<size_t>(r * cols_ + c)]; }

private:
    int rows_ = 0;
    int cols_ = 0;
    std::vector<double> data_;
};

// Solves A x = b by LU decomposition with partial pivoting (A is copied).
// Returns false if A is singular to working precision.
bool solve(Matrix a, std::vector<double>& b);

// Inverse of a square matrix; returns false if singular.
bool invert(const Matrix& a, Matrix& out);

}  // namespace kin
