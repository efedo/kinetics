// Port of KIN_VIEW.CPP: the VIEW menu. The calculated parameters (shown by
// the result screens of the CALCULATE group), the graphs (PLOT.CPP, here
// view/plot.cpp) and the residual table with the residual tests.
//
// v3.51's axis transforms (get_xplot, get_yplot, invert_xplot,
// invert_yplot, return_x_at_y0, check_xaxis_flag) and STAT.CPP's residuals
// and runs/sign tests are not ported: the points, the curve, the residuals
// and the tests come from the session's result (kin::make_plot and
// kin::Analysis), which includes the core's fixes. STAT.CPP itself drew
// nothing; the residual screen is print_residuals below.

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "classic/d_data.hpp"
#include "classic/kinetics.hpp"
#include "classic/view/pager.hpp"
#include "classic/view/plot.hpp"
#include "core/plots.hpp"

namespace classic {
namespace {

// The session's result, if it is for the chosen equation. v3.51 kept p[]
// when the equation changed and plotted it with the new one; the session's
// result carries its own equation, so a result for another one is treated
// as not calculated.
const kin::Analysis* current_result() {
    const kin::Analysis* a = session().result();
    if (!a || a->model() != model_of(eqn_type) || a->data.empty()) return nullptr;
    return a;
}

double param_of(const kin::Analysis& a, kin::Role role, double fallback) {
    const auto i = kin::param_index(a.model(), role);
    if (!i || static_cast<size_t>(*i) >= a.fit.p.size()) return fallback;
    return a.fit.p[static_cast<size_t>(*i)];
}

Task<void> plot_residuals() {
    const kin::Analysis* res = current_result();
    if (!res) {
        co_await print_error("Parameters NOT calculated!!");  // not in v3.51
        co_return;
    }
    // v3.51: yhat[i] = return_yhat(val[1][i], p), resid[i] = val[2][i] - yhat[i].
    const kin::PlotData pd = kin::make_plot(*res, kin::PlotType::Residual, kin::PlotOptions{});
    const int num = static_cast<int>(pd.px.size());
    std::vector<float> yhat(static_cast<size_t>(num) + 1), resid(static_cast<size_t>(num) + 1);
    for (int i = 1; i <= num; i++) {
        yhat[static_cast<size_t>(i)] = plot_value(pd.px[static_cast<size_t>(i - 1)]);
        resid[static_cast<size_t>(i)] = plot_value(pd.py[static_cast<size_t>(i - 1)]);
    }
    plot pt;
    co_await pt.residual_plot(yhat.data(), resid.data(), num);
}

}  // namespace

Task<int> view_option(int& ch) {
    static int oldans = 1;
    int ans;
    const int pos[] = {0, 2, 0, 3};
    const char* const s[] = {"caLculated parameters  -altL", "Graph                  -altG",
                             "resIduals              -altI"};
    single_response_window srw(37, 2, 0, 0, s, pos, 0, 3, 1, USER_DEF, GREEN, WHITE, BLACK, WHITE, DOUBLE, WHITE);

    if (!(ans = co_await srw.get_single_response(oldans, ch))) co_return 0;
    else if (ans == -1) co_return -1;
    oldans = ans;
    if (ans == 1) {
        ch = ESC;
        ans = M_VIEW;
        co_await show_results();
    } else if (ans == 2) ans = co_await plot_data(ch);
    else if (ans == 3) ans = co_await print_residuals(ch);
    co_return ans;
}

Task<void> show_results() {
    if (eqn_type == LINEAR) co_await print_linear_results();
    else {
        if (MINIMIZATION_TYPE == MARQUARDT) co_await print_mrq_results();
        else co_await print_gn_results();
    }
}

Task<int> plot_data(int& ch) {
    int plot_type = 1;
    const int pos[] = {0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0};
    unsigned char buf[330];
    const char* const s[] = {"NORMAL plot (v vs. [S] or Y vs. X)",
                             "DOUBLE-RECIPROCAL plot (1/v vs 1/[S])",
                             "HANES-WOLF plot ([S]/v vs. [S])",
                             "EADIE-HOFSTEE plot (v/[S] vs. v)",
                             "HILL plot ((Vmax-v)/v vs. log[S])",
                             "LOG-LOG plot (log(v) vs. log[S])",
                             "SATURATION plot (v/Vmax vs. log([S]/Km))",
                             "JOB plot for inhibition (v0/v vs. [I])",
                             "ARRHENIUS plot (log10(v) vs. 1/T (°K))",
                             "RESIDUAL plot (residual vs. yhat)"};

    screen scr(1, 4, 80, 23, BLUE);
    scr.put_str(2, 2, "SELECT TYPE OF PLOT DESIRED", YELLOW);
    single_response_window srw(0, 0, 0, 0, s, pos, 0, 10, 1, USER_DEF, BLUE, YELLOW, WHITE, LIGHTRED);

    gettext(1, 1, 80, 2, buf);
    while (plot_type > 0) {
        puttext(1, 1, 80, 2, buf);
        scr.print_screen();
        print_message("Press ESC or alt + MENU key");
        // v3.51: the highlight goes back to the first plot type each time.
        plot_type = co_await srw.get_single_response(1, ch);
        if (plot_type > 0) {
            if (plot_type != P_RESIDUAL) co_await show_plot(plot_type);
            else co_await plot_residuals();
        }
        _setcursortype(_NOCURSOR);
    }
    srw.show_window(0, WHITE);
    if (plot_type < 0) co_return -1;
    else co_return M_VIEW;
}

Task<int> print_residuals(int& ch) {
    const kin::Analysis* res = current_result();
    if (!res) {
        // Not in v3.51, which could not get here without parameters.
        co_await print_error("Parameters NOT calculated!!");
        ch = ESC;
        co_return M_CALCULATE;
    }
    const kin::Analysis a = *res;
    // v3.51: the pairs val[1..num_vals]; the result's pairs are the same,
    // sorted and without incomplete rows.
    const int n = static_cast<int>(std::min<size_t>(a.data.size(), MAX_VALUES));
    std::vector<int> listd(static_cast<size_t>(n) + 1, 0), colour(static_cast<size_t>(n) + 1, WHITE);
    const auto str = std::make_unique<char[][80]>(static_cast<size_t>(n) + 1);
    int num, seq, l, i, START = 1, item = 1, special = NO;
    screen scr(1, 21, 80, 23, CYAN);
    d_data dd(2, 5, 80, 20, BLUE, YELLOW);

    // v3.51: st.residuals(), st.is_num_of_signs_random() and
    // st.is_sequence_random() (STAT.CPP).
    const kin::ResidualTests& rt = a.residualTests;
    num = rt.signsRandom ? 1 : 0;
    seq = !rt.runsTestable ? -1 : (rt.runsRandom ? 1 : 0);
    l = scr.put_str(2, 1, "Sequence of residuals = ", WHITE);
    scr.put_str(l, 1, rt.sequence.c_str(), YELLOW);
    if (seq < 0) scr.put_str(2, 2, "Too few data points to test sequence randomness.", WHITE);
    else if (seq > 0) scr.put_str(2, 2, "The sequence of residuals is random at the P < 0.05 level.", WHITE);
    else scr.put_str(2, 2, "The sequence of residuals is NOT random at the P < 0.05 level.", WHITE);
    if (num) scr.put_str(2, 3, "The # of +ve and -ve residuals is random at the P < 0.05 level.", WHITE);
    else scr.put_str(2, 3, "The # of +ve and -ve residuals is NOT random at the P < 0.05 level.", WHITE);
    std::snprintf(str[0], sizeof str[0], "%s",
                  "Pair #    [S]         v           vhat         v-vhat        > 2 S.D. ?");
    if (ACTIVATION) str[0][11] = 'A';
    else if (INHIBITION) str[0][11] = 'I';
    else if (eqn_type == ARR)
        std::snprintf(str[0], sizeof str[0], "%s",
                      "Pair #    T (°C)      log10(v)    log10(vhat)   v-vhat        > 2 S.D. ?");
    else if (eqn_type == LINEAR)
        std::snprintf(str[0], sizeof str[0], "%s",
                      "Pair #    X           Y           Yhat          Y-Yhat        > 2 S.D. ?");
    for (i = 1; i <= n; i++) {
        const auto k = static_cast<size_t>(i - 1);
        int mark;  // v3.51 used the ch argument for this
        // v3.51: fabs(resid[i]) >= 2.0*std_dev
        if (k < a.largeResidual.size() && a.largeResidual[k]) {
            colour[static_cast<size_t>(i)] = YELLOW;
            mark = '*';
        } else {
            colour[static_cast<size_t>(i)] = WHITE;
            mark = ' ';
        }
        // v3.51 held the values as floats.
        std::snprintf(str[i], sizeof str[i], " %-8i%-12g%-12g%-12g%-+7.5e%8c", i,
                      static_cast<double>(static_cast<float>(a.data[k].x)),
                      static_cast<double>(static_cast<float>(a.data[k].y)),
                      static_cast<double>(static_cast<float>(a.yhat[k])),
                      static_cast<double>(static_cast<float>(a.resid[k])), mark);
    }
    scr.print_screen();
    print_message("Press DIRECTION keys or ESC to activate MENU");
    for (;;) {
        ch = co_await display_wait(dd, str.get(), colour.data(), listd.data(), n, START, item, special);
        if (ends_list(ch, special)) break;
    }
    if (special) co_return -1;  // an Alt key
    else co_return M_VIEW;
}

Task<void> show_plot(int plot_type) {
    int origin, txal, tyal;
    std::string xlabel[] = {"",       "T (°C)",       "1/[S]", "[S]",      "v",   "log[S]",
                            "log[S]", "log([S]/Km) ", "[I]",   "1/T (°K)", "Yhat"};
    std::string ylabel[] = {"",       "log10(v)", "1/v",  "[S]/v",    "v/[S]",   "log10(Vmax-v)/v",
                            "log(v)", "v/Vmax",   "v0/v", "log10(v)", "Residual"};

    // v3.51 made these changes in the string literals themselves, so they
    // stayed after the equation changed (defect 9 in the README's Changes
    // from v3.51: "1/[A]" on a Michaelis-Menten plot). Here they start
    // afresh each time.
    if (eqn_type != ARR) {
        xlabel[1] = "[S]";
        ylabel[1] = "v";
    }

    if (ACTIVATION) xlabel[1][1] = xlabel[2][3] = xlabel[3][1] = xlabel[5][4] = xlabel[6][4] = xlabel[7][5] = 'A';
    else if (INHIBITION) {
        xlabel[1][1] = xlabel[2][3] = xlabel[3][1] = xlabel[5][4] = xlabel[6][4] = 'I';
        xlabel[7] = "log([I]/I50)";
        ylabel[7] = "v/v0";
    }

    if (plot_type < P_NORMAL || plot_type > P_ARRHENIUS) co_return;  // not in v3.51 (no such plot)

    txal = tyal = origin = 1;
    if (eqn_type == MM) {
        if (plot_type == P_JOB || plot_type == P_ARRHENIUS) co_return;
    } else if (HILL || ACTIVATION) {
        if (plot_type == P_JOB || plot_type == P_ARRHENIUS) co_return;
        else if (plot_type == P_HW) txal = 0;
        else if (plot_type == P_EH) txal = tyal = 0;
    } else if (INHIBITION) {
        if (plot_type != P_NORMAL && plot_type != P_SATURATION && plot_type != P_JOB) co_return;
        if (plot_type == P_JOB) txal = 0;
    } else if (eqn_type == ARR) {
        txal = tyal = 0;
        if (plot_type != P_NORMAL && plot_type != P_ARRHENIUS) co_return;
    } else if (eqn_type == LINEAR) {
        if (plot_type != P_NORMAL && plot_type != P_LL) co_return;
    }
    if (plot_type == P_SATURATION) txal = 0;

    const kin::Analysis* res = current_result();
    if (!res) {
        co_await print_error("Parameters NOT calculated!!");  // not in v3.51
        co_return;
    }
    const kin::Analysis a = *res;

    plot pl;
    if (plot_type == P_NORMAL && eqn_type == LINEAR) {
        // v3.51: pl.linear_plot(val[1], val[2], num_vals, p[1], p[2]).
        const kin::PlotData pd = kin::make_plot(a, kin::PlotType::Normal, kin::PlotOptions{});
        const int num = static_cast<int>(pd.px.size());
        std::vector<float> x(static_cast<size_t>(num) + 1), y(static_cast<size_t>(num) + 1);
        for (int i = 1; i <= num; i++) {
            x[static_cast<size_t>(i)] = plot_value(pd.px[static_cast<size_t>(i - 1)]);
            y[static_cast<size_t>(i)] = plot_value(pd.py[static_cast<size_t>(i - 1)]);
        }
        co_await pl.linear_plot(x.data(), y.data(), num, static_cast<float>(param_of(a, kin::Role::Intercept, p[1])),
                                static_cast<float>(param_of(a, kin::Role::Slope, p[2])));
    } else {
        co_await pl.non_linear_plot(a, plot_type, xlabel[plot_type].c_str(), ylabel[plot_type].c_str(), origin, txal,
                                    tyal);
    }
    _setcursortype(_NOCURSOR);
}

}  // namespace classic
