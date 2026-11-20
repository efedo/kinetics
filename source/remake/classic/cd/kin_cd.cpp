// Port of KIN_CD.CPP: the CALCULATE menu, the fitting driver with its
// dialogs (too few points, fixed values, manual initial estimates, guess
// and minimization types), the single / automatic / preview analysis of
// imported files, and the standard curve (new X for a Y, new Y for an X).
//
// v3.51 fitted with its own MATHF/STAT code, using the automatic guesses,
// model functions, inverses and derivatives at the end of KIN_CD.CPP. Here
// the fit is the shared core's (kin::analyse, kin::inverse, kin::evaluate),
// so those routines are not ported; the screens around them are. The
// result and progress screens are in results.cpp.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "classic/cd/cd.hpp"
#include "classic/kinetics.hpp"
#include "core/analysis.hpp"
#include "core/batch.hpp"
#include "core/models.hpp"

namespace classic {
namespace {

// Why the last fit stored no result (for the batch list of the modern
// interface).
std::string g_fit_error;

bool equation_valid() { return eqn_type >= MM && eqn_type <= LINEAR; }

// KIN_EDIT clean_up for the current equation and fixed flags:
// tot_num_parameters, listp and the number of variable parameters.
// load_from_session() does not set them, so they are worked out again here.
void clean_up() {
    num_parameters = kin::param_count(model_of(eqn_type));
    tot_num_parameters = num_parameters;
    if (eqn_type == ARR) tot_num_parameters++;
    for (int i = 1; i <= tot_num_parameters; i++) listp[i] = i;
    if (v0 == FIXED) num_parameters--;
    if (vmin == FIXED) num_parameters--;
    if (Vmax == FIXED) num_parameters--;
    if ((Vmax == FIXED && v0 == VARIABLE && ACTIVATION) || (vmin == FIXED && v0 == VARIABLE && INHIBITION)) {
        const int i = listp[tot_num_parameters];
        listp[tot_num_parameters] = listp[tot_num_parameters - 1];
        listp[tot_num_parameters - 1] = i;
    }
}

// An error box like print_error's, made taller for the core's longer
// messages. v3.51 had no such messages: its fits always "succeeded".
Task<void> show_error(const std::string& msg) {
    constexpr size_t kWidth = 46;
    if (cp437(msg).size() <= kWidth) {
        co_await print_error(msg.c_str());
        co_return;
    }
    std::vector<std::string> lines;
    std::string line, word;
    auto flush_word = [&] {
        if (word.empty()) return;
        if (!line.empty() && cp437(line).size() + 1 + cp437(word).size() > kWidth) {
            lines.push_back(line);
            line.clear();
        }
        line += (line.empty() ? "" : " ") + word;
        word.clear();
    };
    for (char c : msg) {
        if (c == ' ') flush_word();
        else word += c;
    }
    flush_word();
    if (!line.empty()) lines.push_back(line);
    const int n = static_cast<int>(lines.size());
    const int top = std::max(1, 10 - (n - 1) / 2);
    screen scr(16, top, 65, std::min(25, top + 5 + n), RED, THICK, YELLOW);
    scr.put_str(2, 2, "ERROR  ERROR  ERROR", WHITE);
    for (int i = 0; i < n; ++i) scr.put_str(2, 3 + i, lines[static_cast<size_t>(i)].c_str(), WHITE);
    scr.put_str(2, 3 + n, "ERROR  ERROR  ERROR", WHITE);
    scr.print_screen();
    beep();
    print_message("press any key to continue");
    co_await getch();
    scr.restore_previous_screen();
}

// The data to fit: val[1..2][1..num_vals]. The session's table holds the
// same numbers as doubles (these globals are floats, as in v3.51); when it
// is the same data, fit its values, so that both interfaces give the same
// result to the last digit.
std::vector<kin::Point> fit_data() {
    std::vector<kin::Point> pts;
    for (int i = 1; i <= num_vals; i++) pts.push_back({val[1][i], val[2][i]});
    const auto table = kin::clean_data(session().table());
    const auto mine = kin::clean_data(pts);
    if (table.size() != mine.size()) return pts;
    for (size_t k = 0; k < table.size(); ++k)
        if (static_cast<float>(table[k].x) != static_cast<float>(mine[k].x) ||
            static_cast<float>(table[k].y) != static_cast<float>(mine[k].y))
            return pts;
    return table;
}

bool has_zero_x() {
    for (int i = 1; i <= num_vals; i++)
        if (val[1][i] == 0.0f) return true;
    return false;
}

// check out if there are enough data pairs
Task<int> check_numbers() {
    static const int pos[] = {0, 0, 0};
    static const char* const s[] = {"Dear curve-fitting illiterate: ",
                                    "   ",
                                    "If you haven't realized it yet, you can't fit a curve",
                                    "containing more parameters (or an equal number of ",
                                    "parameters) than the number of points you have. Actually,",
                                    "you require that: #points > #parameters + 1. If this isn't",
                                    "true, you are (in the final analysis) making up a curve, ",
                                    "so why are you bothering me, I could be napping in the sun,",
                                    "or checking out nubile, young components, ...",
                                    "Continue with the fit (I can't guarantee the results)",
                                    "Get out of this horrible mess."};
    int ch = 0;

    // v3.51: num_vals <= num_parameters+1, counting the variable parameters.
    if (kin::too_few_points(session().model(), session().params(), static_cast<size_t>(num_vals))) {
        single_response_window srw(0, 0, 0, 0, s, pos, 9, 2, 1, USER_DEF, MAGENTA, YELLOW, WHITE, YELLOW, DOUBLE,
                                   YELLOW);
        const int ans = co_await srw.get_single_response(1, ch);
        // v3.51: only "Get out" stops; ESC or an Alt key continues the fit.
        if (ans == 2) co_return NO;
    }
    co_return YES;
}

// subroutine to enter initial fixed vals
Task<int> enter_fixed_parameters() {
    int a = 0, flag, z;
    char str[80];

    if (v0 != FIXED && Vmax != FIXED && vmin != FIXED) co_return 1;
    if (vmin == FIXED) a++;
    if (Vmax == FIXED) a++;
    // v3.51 tested val[1][1] == 0 on sorted data and took v0 = val[2][1];
    // the core takes the mean velocity at zero concentration.
    const bool zero = has_zero_x();
    if (v0 == FIXED) {
        if (zero) {
            double sum = 0.0;
            int n = 0;
            for (int i = 1; i <= num_vals; i++)
                if (val[1][i] == 0.0f) {
                    sum += val[2][i];
                    ++n;
                }
            p[tot_num_parameters] = static_cast<float>(sum / n);
        } else a++;
        if (!a) co_return 1;
    }

    screen scr(20, 6, 60, 11 + a, GREEN, DOUBLE);
    scr.put_str(2, 2, "Enter the following fixed values:", WHITE);
    a = 0;
    std::strcpy(str, "Velocity at 0 [A]: ");
    if (INHIBITION) str[15] = 'I';
    if (v0 == FIXED && !zero) scr.put_str(2, 4 + (a++), str, WHITE);
    if (vmin == FIXED) scr.put_str(2, 4 + (a++), "Velocity at infinite [I]: ", WHITE);
    if (Vmax == FIXED) scr.put_str(2, 4 + a, "Maximal velocity (Vmax): ", WHITE);
    scr.print_screen();

    // v3.51: each refused (<= 0) entry moves the following prompts down a
    // line, since the row counter is advanced on every try.
    a = flag = 0;
    if (v0 == FIXED && !zero) {
        for (;;) {
            p[tot_num_parameters] = 0.0f;
            if (ESC == co_await scr.get_float(2, 4 + (a++), str, p[tot_num_parameters], 10, flag, WHITE, BLACK)) {
                scr.restore_previous_screen();
                co_return 0;
            } else if (p[tot_num_parameters] > 0.0f) break;
        }
    }
    flag = 0;
    if (vmin == FIXED) {
        for (;;) {
            p[3] = 0.0f;
            if (ESC == co_await scr.get_float(2, 4 + (a++), "Velocity at infinite [I]: ", p[3], 10, flag, WHITE, BLACK)) {
                scr.restore_previous_screen();
                co_return 0;
            } else if (p[3] > 0.0f) break;
        }
    }
    flag = 0;
    z = 3;
    if (eqn_type == MM || eqn_type == ACT_E1) z = 2;
    if (Vmax == FIXED) {
        for (;;) {
            p[z] = 0.0f;
            if (ESC == co_await scr.get_float(2, 4 + a, "Maximal velocity (Vmax): ", p[z], 10, flag, WHITE, BLACK)) {
                scr.restore_previous_screen();
                co_return 0;
            } else if (p[z] > 0.0f) break;
        }
    }
    scr.restore_previous_screen();
    co_return 1;
}

// determine what type of initial guess
Task<int> initial_estimate() {
    int i, nl, top, flag = 0;
    char str[80];
    auto& ps = session().params();

    // listp[1..num_parameters]: the variable parameters, in order (the
    // session's flags also cover parameters only the modern interface can fix).
    std::vector<int> list;
    for (size_t j = 0; j < ps.size() && j < MAX_PARMS; ++j)
        if (!ps[j].fixed) list.push_back(static_cast<int>(j) + 1);
    const int n = static_cast<int>(list.size());
    auto lp = [&](int k) { return list[static_cast<size_t>(k - 1)]; };

    nl = 5 + n;
    top = 4 + (20 - nl) / 2;
    if (GUESS_TYPE == MANUAL) {
        // manual guess
        screen scr(20, top, 51, top + nl, CYAN, DOUBLE, WHITE);
        scr.put_str(2, 2, "Enter initial estimates for:", BLACK);
        for (i = 1; i <= n; i++) {
            std::snprintf(str, sizeof str, "%s: ", p_name[lp(i)]);
            scr.put_str(2, 3 + i, str, WHITE);
        }
        for (i = 1; i <= n; i++) {
            flag = 0;
            std::snprintf(str, sizeof str, "%s: ", p_name[lp(i)]);
            if (ESC == co_await scr.get_float(2, 3 + i, str, p[lp(i)], 12, flag, WHITE, BLACK)) {
                scr.restore_previous_screen();
                co_return 0;
            }
        }
        scr.restore_previous_screen();
        // The estimates go to the session, whose fit starts from them.
        for (i = 1; i <= n; i++) {
            const auto& s = ps[static_cast<size_t>(lp(i) - 1)];
            if (p[lp(i)] < s.min) p[lp(i)] = static_cast<float>(s.min);
            if (p[lp(i)] > s.max) p[lp(i)] = static_cast<float>(s.max);
            ps[static_cast<size_t>(lp(i) - 1)].value = p[lp(i)];
        }
        session().changed();
    }
    // AUTOMATIC: v3.51's automatic_guess() is the core's (kin::analyse).
    co_return 1;
}

// calculate_parameters, for data that came from run `run` of an imported
// file (or from the table). `batch`: one run of the automatic analysis.
Task<void> calculate(std::optional<int> run, bool batch = false) {
    auto& ses = session();
    g_fit_error.clear();
    if (!equation_valid()) {
        g_fit_error = "Equation type NOT chosen!!";
        co_await print_error(g_fit_error.c_str());
        PARMS_CALCULATED = NO;
        co_return;
    }
    if (ses.model() != model_of(eqn_type)) store_equation();
    clean_up();

    if (eqn_type == LINEAR) {
        // regression_thru_point(..., 0.0): "FIXED & =0".
        if (INTERCEPT == FIXED) p[1] = 0.0f;
        store_equation();
    } else {
        if (!co_await check_numbers()) {
            g_fit_error = "Too few data points.";
            PARMS_CALCULATED = NO;
            co_return;
        }
        for (int i = 1; i <= 4; i++) p[i] = sd[i] = 0.0f;
        if (!co_await enter_fixed_parameters()) {
            g_fit_error = "Cancelled.";
            PARMS_CALCULATED = NO;
            co_return;
        }
        store_equation();  // the fixed values just entered
        if (!co_await initial_estimate()) {
            g_fit_error = "Cancelled.";
            PARMS_CALCULATED = NO;
            co_return;
        }
    }

    auto request = ses.request(fit_data());
    request.method = MINIMIZATION_TYPE == GAUSS_N ? kin::Method::GaussNewton : kin::Method::Marquardt;
    request.guess = GUESS_TYPE == MANUAL ? kin::GuessMode::Manual : kin::GuessMode::Automatic;
    auto outcome = kin::analyse(request);
    if (!outcome.analysis) {
        // v3.51 had no failure path here (it showed whatever its solver
        // left, or exited on a singular matrix).
        g_fit_error = outcome.error;
        PARMS_CALCULATED = NO;
        co_await show_error(outcome.error);
        co_return;
    }

    // The solvers drew the result screen after every iteration; ESC
    // stopped them there. In the automatic analysis ESC is left to stop
    // the whole analysis instead: v3.51's fits took long enough for its
    // check between runs to catch the key, but replaying them takes a few
    // frames, so nearly every ESC would only cut a run's fit short.
    bool aborted = false;
    if (const int stop = co_await cd::show_progress(*outcome.analysis, !batch); stop > 0) {
        auto again = outcome.analysis->request;
        again.maxIterations = stop;
        if (auto partial = kin::analyse(again); partial.analysis) {
            outcome = std::move(partial);
            aborted = true;
        }
    }

    const kin::Analysis& a = *outcome.analysis;
    store_results(a, run);
    if (a.linear) {
        // stat::linear_regression's outputs.
        p[0] = static_cast<float>(a.linear->r);
        sd[0] = static_cast<float>(a.linear->sdR);
        DF[3] = a.linear->df;  // STAT_DEV_REG
        T[1] = static_cast<float>(a.linear->tIntercept);
        T[2] = static_cast<float>(a.linear->tSlopeVs1);
    }
    cd::draw_final_results(*ses.result(), aborted);
    PARMS_CALCULATED = YES;
}

// ---- automatic analysis: one entry per run in the session's batch list ----

void begin_batch() {
    auto& bs = session().batch_state();
    bs.items.clear();
    bs.firstRun = FILENUM;
    bs.selected = -1;
    bs.status.clear();
}

void record_run() {
    auto& bs = session().batch_state();
    kin::BatchItem item;
    item.runNumber = FILENUM;
    if (PARMS_CALCULATED && session().result()) item.analysis = *session().result();
    else item.error = g_fit_error.empty() ? "Not fitted." : g_fit_error;
    bs.items.push_back(std::move(item));
    int ok = 0;
    for (const auto& it : bs.items) ok += it.analysis.has_value();
    bs.status = "Analysed " + std::to_string(bs.items.size()) + " run(s): " + std::to_string(ok) + " fitted";
    if (ok < static_cast<int>(bs.items.size())) bs.status += ", " + std::to_string(bs.items.size() - ok) + " failed";
    bs.status += ".";
}

void show_run_number(screen& scr) {
    scr.put_int(7, 1, "Run #: %i", FILENUM, BLACK);
    scr.print_screen();
}

// The result of the current equation, for the standard curve. v3.51 used
// whatever p[] held; with no result there is nothing to compute from.
const kin::Analysis* curve_result() {
    const kin::Analysis* a = session().result();
    if (!a || !equation_valid() || a->model() != model_of(eqn_type)) return nullptr;
    return a;
}

Task<void> change_guess_type() {
    static const int pos[] = {0, 0, 0};
    static const char* const s[3] = {"Select method of determining initial estimates.", "Automatic guess",
                                     "Enter initial estimates"};
    int ans, ch = 0;

    single_response_window srw(0, 0, 0, 0, s, pos, 1, 2, 1, USER_DEF, CYAN, WHITE, BLACK, WHITE, DOUBLE);
    if ((ans = co_await srw.get_single_response(GUESS_TYPE, ch)) > 0) {
        GUESS_TYPE = ans;
        PARMS_CALCULATED = NO;
        store_guess_and_minimization();
    }
}

Task<void> change_minimization_type() {
    static const int pos[] = {0, 0, 0};
    static const char* const s[3] = {"Select method of least squares minimization.",
                                     "Marquardt-Levenberg (quicker but doesn't always work)",
                                     "Gauss-Newton (slower but fits more types)"};
    int ans, ch = 0;

    single_response_window srw(0, 0, 0, 0, s, pos, 1, 2, 1, USER_DEF, GREEN, WHITE, BLACK, WHITE, DOUBLE);
    if ((ans = co_await srw.get_single_response(MINIMIZATION_TYPE, ch)) > 0) {
        MINIMIZATION_TYPE = ans;
        PARMS_CALCULATED = NO;
        store_guess_and_minimization();
    }
}

}  // namespace

Task<int> calculate_option(int& ch) {
    static int oldans = 1;
    int ans, rvalue = M_CALCULATE;
    static const int pos[] = {0, 16, 27, 27, 16, 4, 4, 7, 7};
    static const char* const s[] = {"parameters from Raw/edited data       -altR",
                                    "parameters from file data: Single     -altS",
                                    "parameters from file data: Automatic  -altA",
                                    "automatic previeW (does not print)    -altW",
                                    "new X value for entered y value       -altX",
                                    "new Y value for entered x value       -atlY",  // v3.51: "atlY"
                                    "change Guess type",
                                    "change Minimization procedure"};
    single_response_window srw(20, 2, 0, 0, s, pos, 0, 8, 1, USER_DEF, GREEN, WHITE, BLACK, WHITE, DOUBLE, WHITE);

    if (!(ans = co_await srw.get_single_response(oldans, ch))) co_return 0;
    else if (ans == -1) co_return -1;

    ch = ESC;
    oldans = ans;
    if (ans == 1) {
        if (DATA_ENTERED) co_await calculate_parameters();
        else co_await print_error("NO data entered!!");
    } else if (ans == 2) co_await single_analysis();
    else if (ans == 3) co_await automatic_analysis();
    else if (ans == 4) co_await automatic_preview();
    else if (ans == 5 || ans == 6) {
        if (!PARMS_CALCULATED) co_await print_error("Parameters NOT calculated!!");
        else if (ans == 5) rvalue = co_await calculate_new_x(ch);
        else if (ans == 6) rvalue = co_await calculate_new_y(ch);
    } else if (ans == 7) co_await change_guess_type();
    else if (ans == 8) co_await change_minimization_type();
    co_return rvalue;
}

Task<void> calculate_parameters() { co_await calculate(std::nullopt); }

Task<void> single_analysis() {
    co_await import_data(YES);
    if (!DATA_ENTERED) PARMS_CALCULATED = NO;
    else co_await calculate(FILENUM);  // v3.51: not sorted here (the core sorts)
}

Task<void> automatic_analysis() {
    screen scr(30, 4, 50, 4, CYAN);

    // v3.51 read the output settings from OUTPUT.DAT here (and exited to
    // DOS if it was missing); they are the session's report settings.
    if (output.location == TO_FILE) {
        if (!co_await get_output_file_name()) {
            PARMS_CALCULATED = NO;
            co_return;
        }
    }
    co_await import_data(YES);
    if (!DATA_ENTERED) {
        PARMS_CALCULATED = NO;
        co_return;
    }
    begin_batch();
    sort();
    co_await calculate(FILENUM, true);
    record_run();
    if (!PARMS_CALCULATED) co_return;
    show_run_number(scr);
    co_await print_results(NO);
    if (!PARMS_CALCULATED) co_return;
    for (;;) {
        co_await import_data(NO);
        if (!DATA_ENTERED) co_return;
        sort();
        co_await calculate(FILENUM, true);
        record_run();
        if (PARMS_CALCULATED) {
            show_run_number(scr);
            co_await print_results(NO);
        }
        co_await next_frame();  // let the screen be drawn and a key arrive
        if (kbhit()) {
            if (ESC == co_await getch()) {
                co_await print_error("Analysis interupted by USER");  // v3.51 spelling
                co_return;
            }
        }
    }
}

Task<void> automatic_preview() {
    unsigned char buf[330];
    screen scr(30, 4, 50, 4, CYAN);

    gettext(1, 1, 80, 2, buf);
    co_await import_data(YES);
    if (!DATA_ENTERED) {
        PARMS_CALCULATED = NO;
        co_return;
    }
    begin_batch();
    sort();
    co_await calculate(FILENUM, true);
    record_run();
    if (!PARMS_CALCULATED) co_return;
    print_message("Press any key to view plot, ESC to quit");
    show_run_number(scr);
    // v3.51: getch() reads one byte, so a special key leaves its scan code
    // for the plot screen.
    if (ESC == co_await getch()) {
        co_await print_error("Analysis interupted by USER");
        co_return;
    }
    co_await show_plot(P_NORMAL);
    puttext(1, 1, 80, 2, buf);
    for (;;) {
        co_await import_data(NO);
        if (!DATA_ENTERED) {
            PARMS_CALCULATED = NO;
            co_return;
        }
        sort();
        co_await calculate(FILENUM, true);
        record_run();
        if (PARMS_CALCULATED) {
            show_run_number(scr);
            print_message("Press any key to view plot, ESC to quit");
            if (ESC == co_await getch()) {
                co_await print_error("Analysis interupted by USER");
                co_return;
            }
            co_await show_plot(P_NORMAL);
            puttext(1, 1, 80, 2, buf);
        } else co_return;
    }
}

Task<int> calculate_new_x(int& ch) {
    int l, flag = 0;
    float x, v = 0.0f;
    char strx[30] = "[S] = ";
    char stry[30] = "Enter velocity: ";
    char strw[50] = "velocity outside curve range !!";

    const kin::Analysis* a = curve_result();
    if (!a) {
        co_await print_error("Parameters NOT calculated!!");
        ch = ESC;
        co_return 0;
    }
    screen scr(22, 8, 55, 16, GREEN, DOUBLE);

    if (ACTIVATION) strx[1] = 'A';
    else if (INHIBITION) strx[1] = 'I';
    else if (eqn_type == LINEAR) {
        std::strcpy(strx, "X = ");
        std::strcpy(stry, "Enter Y value: ");
        std::strcpy(strw, "Y value outside curve range !!");
    } else if (eqn_type == ARR) {
        std::strcpy(strx, "T (°C) = ");
        std::strcpy(stry, "Enter log10(v): ");
        std::strcpy(strw, "log10(v) outside curve range !!");
    }

    scr.put_str(2, 2, "Enter a value to calculate or", WHITE);
    scr.put_str(2, 3, "Press ESCAPE to quit", WHITE);
    scr.put_str(2, 6, strx, WHITE);
    for (;;) {
        flag = 0;
        ch = co_await scr.get_float(2, 5, stry, v, 10, flag, WHITE, BLACK);
        if (ch == ESC || is_altkey(ch)) break;
        // return_x_val; v3.51 returned 1e10 where the curve has no X.
        const auto inv = kin::inverse(a->model(), v, a->fit.p);
        x = inv ? static_cast<float>(*inv) : 1e10f;
        l = scr.put_str(2, 6, strx, WHITE);
        scr.delete_line(l, 32, 6);  // v3.51 left the end of a longer previous answer
        scr.put_float(l, 6, "%-g", x, BLACK);
        // v3.51: Y_min/Y_max; the range of the fitted data.
        if (v < a->yMin || v > a->yMax) scr.put_str(2, 7, strw, RED);
        else scr.delete_line(2, 32, 7);
    }
    scr.restore_previous_screen();
    if (ch == ESC) co_return 0;
    else if (is_altkey(ch)) co_return -1;
    else co_return M_CALCULATE;
}

Task<int> calculate_new_y(int& ch) {
    int l, flag = 0;
    float x = 0.0f, v;
    char strx[30] = "Enter [S]: ";
    char stry[30] = "velocity = ";
    char strw[50] = "[S] outside curve range !!";

    const kin::Analysis* a = curve_result();
    if (!a) {
        co_await print_error("Parameters NOT calculated!!");
        ch = ESC;
        co_return 0;
    }
    screen scr(21, 8, 56, 16, GREEN, DOUBLE);

    if (ACTIVATION) strx[7] = strw[1] = 'A';
    else if (INHIBITION) strx[7] = strw[1] = 'I';
    else if (eqn_type == LINEAR) {
        std::strcpy(strx, "Enter X value: ");
        std::strcpy(stry, "Y value = ");
        std::strcpy(strw, "X value outside curve range !!");
    } else if (eqn_type == ARR) {
        std::strcpy(strx, "Enter T (°C): ");
        std::strcpy(stry, "log10(v) = ");
        std::strcpy(strw, "Temperature outside curve range !!");  // v3.51: one character too long for the box
    }

    scr.put_str(2, 2, "Enter a value to calculate or", WHITE);
    scr.put_str(2, 3, "Press ESCAPE to exit", WHITE);
    scr.put_str(2, 6, stry, WHITE);
    for (;;) {
        // v3.51: flag is not reset, so the field shows the previous value.
        ch = co_await scr.get_float(2, 5, strx, x, 10, flag, WHITE, BLACK);
        if (ch == ESC || is_altkey(ch)) break;
        // return_yhat; v3.51 returned 1e10 where the curve is undefined.
        const double y = kin::evaluate(a->model(), x, a->fit.p);
        v = std::isfinite(y) ? static_cast<float>(y) : 1e10f;
        l = scr.put_str(2, 6, stry, WHITE);
        scr.delete_line(l, 34, 6);  // v3.51 left the end of a longer previous answer
        scr.put_float(l, 6, "%-g", v, BLACK);
        // v3.51: val[1][1] and val[1][num_vals]; the range of the fitted data.
        if (x < a->xMin || x > a->xMax) scr.put_str(2, 7, strw, RED);
        else scr.delete_line(2, 34, 7);
    }
    scr.restore_previous_screen();
    if (ch == ESC) co_return 0;
    else if (is_altkey(ch)) co_return -1;
    else co_return M_CALCULATE;
}

}  // namespace classic
