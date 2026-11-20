// The result screens of KIN_CD.CPP (print_linear_results,
// print_mrq_results, print_gn_results) and the progress display that
// MATHF.CPP's solvers drew by calling them after every iteration.
//
// The numbers come from the shared core's result (kin::Analysis): its
// parameters, standard deviations, iteration count and statistics are shown
// in v3.51's layout and formats. The progress display replays the core's
// iteration history (FitResult::history).

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "classic/cd/cd.hpp"
#include "core/linalg.hpp"
#include "core/models.hpp"
#include "core/stats.hpp"

namespace classic {
namespace {

using kin::Analysis;
using kin::Family;
using kin::Role;

// put_float, but with "n/a" in place of the number when it is not finite
// (or too large for v3.51's floats). v3.51 printed whatever its float held,
// for example 1e+10 for a standard deviation it could not estimate; the core
// gives NaN for those.
int put_value(screen& scr, int x, int y, const char* fmt, double v, int c) {
    if (std::isfinite(v) && std::fabs(v) <= FLT_MAX) return scr.put_float(x, y, fmt, v, c);
    std::string out;
    bool done = false;
    for (const char* f = fmt; *f; ++f) {
        if (*f != '%') {
            out += *f;
            continue;
        }
        if (f[1] == '%') {
            out += '%';
            ++f;
            continue;
        }
        while (f[1] && !std::isalpha(static_cast<unsigned char>(f[1]))) ++f;  // flags, width, precision
        if (f[1]) ++f;
        if (!done) out += "n/a";
        done = true;
    }
    return scr.put_str(x, y, out.c_str(), c);
}

// What one result screen shows, 1-based as v3.51's arrays: a[1..na] and
// sd[1..na], where for the Arrhenius equation a[3] is Ea in kJ/mol.
struct Values {
    double a[MAX_PARMS + 1] = {};
    double sd[MAX_PARMS + 1] = {};
    int na = 0;
};

Values values_of(const Analysis& an, const std::vector<double>& pv, const std::vector<double>& sv) {
    Values v;
    const int n = kin::param_count(an.model());
    for (int j = 0; j < n && j < MAX_PARMS; ++j) {
        v.a[j + 1] = pv[static_cast<size_t>(j)];
        v.sd[j + 1] = sv[static_cast<size_t>(j)];
    }
    v.na = n;
    if (an.model() == kin::ModelId::Arrhenius) {
        // return_yhat: a[3] = a[2]*8.31431/1.98717; print_*_results: sd[3] likewise.
        v.a[3] = v.a[2] * kin::kKjPerKcal;
        v.sd[3] = v.sd[2] * kin::kKjPerKcal;
        v.na = 3;
    }
    return v;
}

// p_name[i] (KIN_EDIT select_parameters) for the result's equation.
std::string name_of(const Analysis& an, int i) {
    if (model_of(eqn_type) == an.model()) return p_name[i];
    const auto& def = kin::model_def(an.model());
    if (i >= 1 && i <= static_cast<int>(def.params.size())) return std::string(def.params[static_cast<size_t>(i - 1)].name);
    return "Ea (KJ/mol)";
}

// The parameter lines: "name: value +/-  sd  (sd%)".
void put_parameters(screen& scr, const Analysis& an, const Values& v, int x, int y0) {
    char str[80];
    for (int i = 1; i <= v.na; i++) {
        std::snprintf(str, sizeof str, "%s:", name_of(an, i).c_str());
        int l = scr.put_str(x, y0 + i, str, WHITE);
        l = put_value(scr, l, y0 + i, " %-g", v.a[i], YELLOW);
        l = scr.put_str(l, y0 + i, " +/-  ", WHITE);
        l = put_value(scr, l, y0 + i, "%-g", v.sd[i], YELLOW);
        // v3.51's format was "  (%.2f%)", an invalid conversion; it is meant
        // to show the SD as a percentage of the value.
        if (v.a[i] != 0.0 && std::isfinite(v.sd[i]))
            put_value(scr, l, y0 + i, "  (%.2f%%)", v.sd[i] * 100.0 / v.a[i], LIGHTGREEN);
    }
}

// The lines under the parameters of print_mrq_results / print_gn_results:
// substrate and velocity ranges, fold activation and the Hill-coefficient
// test.
void put_interpretation(screen& scr, const Analysis& an, const Values& v, int i) {
    const int L = 5;
    const kin::ModelId m = an.model();
    const auto& def = kin::model_def(m);
    if (def.family == Family::Arrhenius || def.family == Family::Linear) return;
    const bool activation = def.family == Family::Activation;
    const bool inhibition = def.family == Family::Inhibition;
    const auto& a = v.a;
    char str[80];

    // vmax = a[3], or a[2] for MM and ACT_E1; vzero = a[tot_num_parameters].
    const auto iV = kin::param_index(m, Role::Vmax);
    const auto iV0 = kin::param_index(m, Role::V0);
    const double vmax = iV ? a[*iV + 1] : 0.0;
    const double vzero = iV0 ? a[*iV0 + 1] : 0.0;

    int A = 9 + i;
    std::strcpy(str, "[S] varies from %-5.2f ");
    if (activation) str[1] = 'A';
    else if (inhibition) str[1] = 'I';
    int l = put_value(scr, L, A, str, an.xMin / a[1], WHITE);
    std::strcpy(str, "to %-5.2f of Km value.");
    if (activation) str[14] = 'a';
    else if (inhibition) {
        std::strcpy(str, "to %-5.2f of I50 value.");
        if (m == kin::ModelId::InhibitionK50) str[13] = 'K';
    }
    put_value(scr, l, A++, str, an.xMax / a[1], WHITE);

    double z1, z2;
    if (!inhibition) {
        z1 = an.yMin / vmax;
        z2 = an.yMax / vmax;
        std::strcpy(str, "%-5.2f of Vmax value.");
    } else {
        std::strcpy(str, "%-5.2f of v0 value.");
        z1 = an.yMax / vzero;
        z2 = an.yMin / vzero;
    }
    l = put_value(scr, L, A, "v varies from %-5.2f to ", z1, WHITE);
    put_value(scr, l, A++, str, z2, WHITE);

    if (activation) {
        double R = 1e10;
        if (vzero != 0.0) R = vmax / vzero;
        put_value(scr, L, A++, "fold activation: %-g", R, WHITE);
    }

    // Test the Hill coefficient for significance against 1.0.
    if (const auto ih = kin::param_index(m, Role::HillCoeff)) {
        const size_t j = static_cast<size_t>(*ih);
        if (j < an.fit.fixed.size() && an.fit.fixed[j]) {
            // v3.51 could not fix h (the modern interface can); it would
            // have reported a fixed h as significantly different from 1.
            scr.put_str(L, A, "The Hill coefficient was fixed, so it was not tested against 1.", WHITE);
            return;
        }
        const double s = v.sd[j + 1];
        if (std::isnan(s)) return;  // not estimable: nothing to test
        const double tstat = s != 0.0 ? std::fabs((a[j + 1] - 1.0) / s) : 1.0e5;
        const double t_test_result = an.fit.dof > 0 ? kin::p_bin(kin::t_two_tailed_p(tstat, an.fit.dof)) : 1.0;
        if (t_test_result > 0.6) scr.put_str(L, A, "Hill coeff. is NOT significantly different from 1.", WHITE);
        else {
            l = scr.put_str(L, A, "Hill coeff. is significantly different from 1 at the ", WHITE);
            l = scr.put_float(l, A, "P<%g", t_test_result, YELLOW);
            scr.put_str(l, A, " level.", WHITE);
        }
    }
}

// print_mrq_results
void draw_mrq(const Analysis& an, const Values& v, double sdev, int iteration) {
    const int L = 5;
    screen scr(1, 4, 80, 23, BLUE);

    scr.put_str(2, 2, "RESULTS of MARQUARDT_LEVENBERG minimization: ", YELLOW);
    int l = scr.put_int(2, 5, "# of iterations = %-i", iteration, WHITE);
    put_value(scr, 10 + l, 5, "1 Std. deviation = %g", sdev, WHITE);
    put_parameters(scr, an, v, 20, 6);
    const int i = v.na + 1;
    scr.put_str(L, 7 + i, "A std. dev. of +/- 0 indicates that this parameter was fixed.", WHITE);
    put_interpretation(scr, an, v, i);
    scr.print_screen();
}

// print_gn_results. `mess` is "go" while the fit runs, which shows the
// convergence numbers instead of a message.
void draw_gn(const Analysis& an, const Values& v, int niter, const char* mess, double sdev, double phi_1,
             double phi_2) {
    const int L = 5;
    screen scr(1, 4, 80, 23, BLUE);

    int l = scr.put_str(2, 2, "RESULTS of GAUSS-NEWTON minimization: ", YELLOW);
    if (std::strcmp(mess, "go")) scr.put_str(l, 2, mess, WHITE);
    else {
        l = put_value(scr, l, 2, "#'s to zero: Θ1 = %g", phi_1, WHITE);
        put_value(scr, l + 4, 2, "Θ2 = %g", phi_2, WHITE);
    }
    l = scr.put_int(2, 5, "# of iterations = %-i", niter, WHITE);
    put_value(scr, 10 + l, 5, "1 Std. deviation = %g", sdev, WHITE);
    put_parameters(scr, an, v, 20, 6);
    const int i = v.na + 1;
    scr.put_str(L, 7 + i, "A std. dev. of +/- 0 indicates that this parameter was fixed.", WHITE);
    put_interpretation(scr, an, v, i);
    scr.print_screen();
}

// print_linear_results
void draw_linear(const Analysis& an) {
    const auto& lr = *an.linear;
    const int L = 6;
    screen scr(1, 4, 80, 23, BLUE);
    const Values v = values_of(an, an.fit.p, an.fit.sd);

    scr.put_str(2, 2, "RESULTS of LINEAR REGRESSION: ", YELLOW);
    char str[80];
    for (int i = 1; i <= 2; i++) {
        std::snprintf(str, sizeof str, "%s:", name_of(an, i).c_str());
        int l = scr.put_str(10, 4 + i, str, WHITE);
        l = put_value(scr, l, 4 + i, " %-g", v.a[i], YELLOW);
        l = scr.put_str(l, 4 + i, " +/-  ", WHITE);
        l = put_value(scr, l, 4 + i, "%-g", v.sd[i], YELLOW);
        if (v.a[i] != 0.0 && std::isfinite(v.sd[i]))
            put_value(scr, l, 4 + i, "  (%.2f%%)", v.sd[i] * 100.0 / v.a[i], LIGHTGREEN);
    }
    scr.put_str(10, 8, "A std. dev. of +/- 0 indicates that this parameter was fixed.", WHITE);
    int l = scr.put_str(L, 10, "Correlation coeff. = ", WHITE);
    l = put_value(scr, l, 10, "%-g", lr.r, YELLOW);
    l = scr.put_str(l, 10, " +/-  ", WHITE);
    put_value(scr, l, 10, "%-g", lr.sdR, YELLOW);
    l = scr.put_str(L, 11, "Degrees of freedom = ", WHITE);
    scr.put_int(l, 11, "%i", lr.df, YELLOW);
    // v3.51 printed |slope|/SD, the test against 0, under wording about 1;
    // the core has both tests, so the slope lines use the test against 1
    // that the wording describes.
    const double ttrs = kin::p_bin(lr.pSlopeVs1);
    const double ttri = kin::p_bin(lr.pIntercept);
    l = scr.put_str(L, 12, "t-statistic (slope) = ", WHITE);
    put_value(scr, l, 12, "%-g", lr.tSlopeVs1, YELLOW);
    if (ttrs > 0.6) scr.put_str(L, 13, "The SLOPE is NOT significantly different from 1.0.", WHITE);
    else {
        l = scr.put_str(L, 13, "The SLOPE is significantly different from 1 at the ", WHITE);
        l = scr.put_float(l, 13, "P<%g", ttrs, YELLOW);
        scr.put_str(l, 13, " level.", WHITE);
    }
    if (!lr.interceptFixed) {
        l = scr.put_str(L, 14, "t-statistic (intercept) = ", WHITE);
        put_value(scr, l, 14, "%-g", lr.tIntercept, YELLOW);
        if (ttri > 0.6) scr.put_str(L, 15, "The INTERCEPT is NOT significantly different from 0.0.", WHITE);
        else {
            l = scr.put_str(L, 15, "The INTERCEPT is significantly different from 0 at the ", WHITE);
            l = scr.put_float(l, 15, "P<%g", ttri, YELLOW);
            scr.put_str(l, 15, " level.", WHITE);
        }
    }
    scr.print_screen();
}

// MATHF.CPP solve_gn's closing message for the core's outcome.
std::string gn_message(const Analysis& an, bool aborted) {
    if (aborted) return "Run aborted by user !!";
    switch (an.fit.status) {
        case kin::FitStatus::MaxIterations: return "Maximum of " + std::to_string(an.fit.iterations) + " iterations !!";
        case kin::FitStatus::NoImprovement: return "Local minimum found !!";
        default: return "Run has converged";
    }
}

// Parameter standard deviations at an intermediate estimate p, as v3.51's
// solvers recomputed them after every iteration: sqrt(s² · (J'J)⁻¹), with
// J'J scaled to a unit diagonal before inverting (as the core does).
std::vector<double> sds_at(const Analysis& an, const std::vector<double>& pv, double sse) {
    const kin::ModelId m = an.model();
    const int np = kin::param_count(m);
    std::vector<double> out(static_cast<size_t>(np), 0.0);
    std::vector<int> fr;
    for (int j = 0; j < np; ++j)
        if (static_cast<size_t>(j) >= an.fit.fixed.size() || !an.fit.fixed[static_cast<size_t>(j)]) fr.push_back(j);
    const int k = static_cast<int>(fr.size());
    if (k == 0) return out;
    auto unknown = [&] {
        for (int j : fr) out[static_cast<size_t>(j)] = NAN;
        return out;
    };
    kin::Matrix A(k, k);
    std::vector<double> d(static_cast<size_t>(np));
    for (const auto& pt : an.data) {
        if (!kin::gradient(m, pt.x, pv, d)) return unknown();
        for (int r = 0; r < k; ++r)
            for (int c = 0; c < k; ++c) A(r, c) += d[static_cast<size_t>(fr[static_cast<size_t>(r)])] * d[static_cast<size_t>(fr[static_cast<size_t>(c)])];
    }
    std::vector<double> s(static_cast<size_t>(k));
    for (int r = 0; r < k; ++r) {
        if (!std::isfinite(A(r, r))) return unknown();
        s[static_cast<size_t>(r)] = A(r, r) > 0.0 ? std::sqrt(A(r, r)) : 1.0;
    }
    kin::Matrix S(k, k), inv;
    for (int r = 0; r < k; ++r)
        for (int c = 0; c < k; ++c) S(r, c) = A(r, c) / (s[static_cast<size_t>(r)] * s[static_cast<size_t>(c)]);
    const int dof = static_cast<int>(an.data.size()) - k;
    if (dof <= 0 || !std::isfinite(sse) || !kin::invert(S, inv)) return unknown();
    const double s2 = sse / dof;
    for (int r = 0; r < k; ++r) {
        const double var = inv(r, r) / (s[static_cast<size_t>(r)] * s[static_cast<size_t>(r)]) * s2;
        out[static_cast<size_t>(fr[static_cast<size_t>(r)])] = var >= 0.0 ? std::sqrt(var) : NAN;
    }
    return out;
}

double std_dev_of(const Analysis& an, double sse) {
    const int dof = static_cast<int>(an.data.size()) - an.fit.nFree;
    return dof > 0 && std::isfinite(sse) ? std::sqrt(sse / dof) : NAN;
}


}  // namespace

namespace cd {

void draw_final_results(const Analysis& a, bool aborted) {
    if (a.linear) {
        draw_linear(a);
        return;
    }
    const Values v = values_of(a, a.fit.p, a.fit.sd);
    if (a.fit.method == kin::Method::GaussNewton)
        draw_gn(a, v, a.fit.iterations, gn_message(a, aborted).c_str(), a.fit.stdDev, 0.0, 0.0);
    else draw_mrq(a, v, a.fit.stdDev, a.fit.iterations);
}

Task<int> show_progress(const Analysis& a, bool abortable) {
    const auto& h = a.fit.history;
    std::vector<size_t> steps;
    for (size_t i = 0; i < h.size(); ++i)
        if (h[i].iteration >= 1) steps.push_back(i);
    if (steps.empty() || a.linear) co_return 0;

    // One frame per iteration, at most about two seconds in all.
    constexpr size_t kMaxFrames = 120;
    const size_t every = (steps.size() + kMaxFrames - 1) / kMaxFrames;
    const bool gn = a.fit.method == kin::Method::GaussNewton;
    const kin::IterationRecord* last = &h.front();  // the last accepted estimate
    double phi_1 = 0.0, phi_2 = 0.0;
    for (size_t k = 0; k < steps.size(); ++k) {
        const auto& r = h[steps[k]];
        if (gn && r.accepted) {
            // solve_gn's convergence numbers, both below zero at convergence
            // (phi_1 per parameter, as the core tests it).
            constexpr double tau = 1e-3, epsilon = 1e-5;
            phi_2 = std::fabs(last->sse - r.sse) - epsilon * (last->sse + tau);
            phi_1 = -INFINITY;
            for (size_t j = 0; j < r.p.size(); ++j)
                if (j >= a.fit.fixed.size() || !a.fit.fixed[j])
                    phi_1 = std::max(phi_1, std::fabs(r.p[j] - last->p[j]) - epsilon * (std::fabs(r.p[j]) + tau));
        }
        if (r.accepted) last = &r;
        if (k % every != 0 && k + 1 != steps.size()) continue;

        const Values v = values_of(a, r.p, sds_at(a, r.p, r.sse));
        if (gn) draw_gn(a, v, r.iteration, "go", std_dev_of(a, r.sse), phi_1, phi_2);
        else draw_mrq(a, v, std_dev_of(a, r.sse), r.iteration);
        co_await next_frame();
        // v3.51: `if(kbhit()) { if(getch()==ESC) ...stop... }`, which also
        // swallowed any other key typed during the fit. Here another key
        // only ends the display, and is left for the menu.
        if (kbhit()) {
            if (abortable && machine().keys.front() == ESC) {
                co_await getch();
                co_return r.iteration;
            }
            co_return 0;
        }
    }
    co_return 0;
}

}  // namespace cd

Task<void> print_linear_results() {
    const Analysis* a = session().result();
    if (!a) {
        co_await print_error("Parameters NOT calculated!!");
        co_return;
    }
    if (a->linear) draw_linear(*a);
    else cd::draw_final_results(*a, false);
}

Task<void> print_mrq_results() {
    const Analysis* a = session().result();
    if (!a) {
        co_await print_error("Parameters NOT calculated!!");
        co_return;
    }
    if (a->linear) {
        draw_linear(*a);
        co_return;
    }
    draw_mrq(*a, values_of(*a, a->fit.p, a->fit.sd), a->fit.stdDev, a->fit.iterations);
}

Task<void> print_gn_results() {
    const Analysis* a = session().result();
    if (!a) {
        co_await print_error("Parameters NOT calculated!!");
        co_return;
    }
    if (a->linear) {
        draw_linear(*a);
        co_return;
    }
    // KIN_VIEW show_results passed an empty message.
    draw_gn(*a, values_of(*a, a->fit.p, a->fit.sd), a->fit.iterations, "", a->fit.stdDev, 0.0, 0.0);
}

}  // namespace classic
