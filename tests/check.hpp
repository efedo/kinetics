#pragma once
// A minimal test harness: CHECK macros and self-registering test cases.
//
//   TEST_CASE(michaelis_menten_values, "Michaelis-Menten: values") {
//       CHECK_NEAR(kin::evaluate(...), 5.0, 1e-12);
//   }
//
// main.cpp runs every registered case (or those whose name contains the
// first command-line argument) and prints one line per case.

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace test {

using Fn = void (*)();

inline std::vector<std::pair<std::string, Fn>>& registry() {
    static std::vector<std::pair<std::string, Fn>> r;
    return r;
}

inline int g_checks = 0;
inline int g_failures = 0;

inline void check(bool ok, const char* expr, const char* file, int line, const std::string& detail = {}) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("%s:%d: FAILED: %s %s\n", file, line, expr, detail.c_str());
    }
}

struct Registrar {
    Registrar(const char* name, Fn fn) { registry().emplace_back(name, fn); }
};

}  // namespace test

#define TEST_CASE(fn, name)                                     \
    static void fn();                                           \
    static const ::test::Registrar fn##_registrar_(name, fn);   \
    static void fn()

#define CHECK(c) ::test::check((c), #c, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol)                                                                   \
    do {                                                                                        \
        const double a_ = (a), b_ = (b);                                                        \
        char buf_[160];                                                                         \
        std::snprintf(buf_, sizeof buf_, "(%.10g vs %.10g, tol %.3g)", a_, b_, (double)(tol)); \
        ::test::check(std::fabs(a_ - b_) <= (tol), #a " ~ " #b, __FILE__, __LINE__, buf_);     \
    } while (0)
#define CHECK_REL(a, b, rel) CHECK_NEAR(a, b, std::fabs(b) * (rel) + 1e-300)
