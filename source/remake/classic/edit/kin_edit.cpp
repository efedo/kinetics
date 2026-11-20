// Port of KIN_EDIT.CPP: the EDIT menu, the equation menu with its
// fixed/variable windows, and data entry with the grid editor (edit.cpp).
// select_parameters() is in sync.cpp. Comments marked "v3.51" note where the
// original's behaviour is kept on purpose although it looks odd.

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "classic/edit/edit.hpp"
#include "classic/kinetics.hpp"
#include "core/models.hpp"

namespace classic {
namespace {

Task<int> get_number();
Task<void> fixed_or_not();
Task<void> fixed_Vmax_window();
Task<void> fixed_v0_Vmax_window();
Task<void> fixed_v0_window();
Task<void> fixed_v0_vmin_window();
Task<void> fixed_intercept_window();
void clean_up();

// ---- writing back to the session ----------------------------------------------------

int role_flag(kin::Role role) {
    switch (role) {
        case kin::Role::Vmax: return Vmax;
        case kin::Role::V0: return v0;
        case kin::Role::Vmin: return vmin;
        case kin::Role::Intercept: return INTERCEPT;
        default: return -1;
    }
}

// Whether the equation or a fixed/variable choice differs from the session's
// (or the session's fixed intercept is not the "=0" chosen here).
bool equation_differs() {
    auto& s = session();
    const auto m = model_of(eqn_type);
    if (s.model() != m) return true;
    const auto& def = kin::model_def(m);
    const auto& ps = s.params();
    for (size_t j = 0; j < def.params.size() && j < ps.size(); ++j) {
        const int flag = role_flag(def.params[j].role);
        if (flag >= 0 && ps[j].fixed != (flag == FIXED)) return true;
        if (def.params[j].role == kin::Role::Intercept && flag == FIXED && ps[j].value != 0.0) return true;
    }
    return false;
}

// The bounds the fit uses are the session's (as load_from_session shows).
void bounds_from_session() {
    const auto& ps = session().params();
    for (size_t j = 0; j < ps.size() && j + 1 <= MAX_PARMS; ++j) {
        p_min[j + 1] = static_cast<float>(ps[j].min);
        p_max[j + 1] = static_cast<float>(ps[j].max);
    }
}

using Pairs = std::vector<std::pair<float, float>>;

Pairs grid_pairs(int n) {
    Pairs out;
    for (int i = 1; i <= n; ++i) out.emplace_back(val[1][i], val[2][i]);
    return out;
}

Pairs session_pairs() {
    Pairs out;
    for (const auto& pt : session().table()) out.emplace_back(static_cast<float>(pt.x), static_cast<float>(pt.y));
    return out;
}

Pairs sorted(Pairs v) {
    std::sort(v.begin(), v.end());
    return v;
}

// The label the modern interface shows for data from the imported file.
std::string imported_source(bool edited) {
    std::string s = "Run #" + std::to_string(FILENUM) + " of " + import.name;
    if (import.type == MPA) s = "Dataset #" + std::to_string(FILENUM) + " of " + import.name + " (velocities)";
    if (edited) s += " (edited)";
    return s;
}

// store_data() with the grid's floats turned back into the numbers they
// stand for. store_data() widens them as they are, so 0.1 typed here would
// reach the modern table as 0.1000000015 and every untouched value would
// change too. Here a value keeps the session's own number when that rounds
// to the same float, and otherwise becomes the shortest decimal that does.
void store_grid_data(const std::string& source, std::optional<int> run) {
    std::map<float, double> known;
    for (const auto& pt : session().table()) {
        known.emplace(static_cast<float>(pt.x), pt.x);
        known.emplace(static_cast<float>(pt.y), pt.y);
    }
    const auto widen = [&known](float f) {
        if (const auto it = known.find(f); it != known.end()) return it->second;
        char buf[32] = {};
        if (std::to_chars(buf, buf + sizeof buf - 1, f).ec != std::errc()) return static_cast<double>(f);
        return std::strtod(buf, nullptr);
    };
    std::vector<kin::Point> pts;
    for (int i = 1; i <= num_vals; ++i) pts.push_back({widen(val[1][i]), widen(val[2][i])});
    session().set_table(pts, source, run);
    DATA_ENTERED = num_vals > 0 ? YES : NO;
}

// ---- equation menu ------------------------------------------------------------------

Task<int> get_number() {
    static int ans = 1;
    const int pos[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    int ch = 0;
    const char* const s[] = {"a. MICHAELIS-MENTEN equation",
                             "b. HILL equation with h > 0",
                             "c. HILL equation with h ≥ 1",
                             "d. ACTIVATOR equation with h > 0",
                             "e. ACTIVATOR equation with h ≥ 1",
                             "f. ACTIVATOR equation with h = 1",
                             "g. Linear INHIBITION equation",
                             "h. Non-linear INHIBITION equation: gives I50",
                             "i. Non-linear INHIBITION equation: gives K50",
                             "j. Arrhenius Equation (Temperature curve)",
                             "k. Linear regression (Y vs. X)"};

    single_response_window srw(0, 0, 0, 0, s, pos, 0, 11, 1, USER_DEF, BLUE, YELLOW, WHITE, YELLOW, NONE);
    print_message("Select an equation type or press ESC to enable MENU commands");
    if (!ans) ans = 1;
    for (;;) {
        // v3.51 passed an Alt key's -1 back in as the highlighted choice,
        // which stopped the program with "Improper input value"; here the
        // Alt key is ignored, as the loop intended.
        const int r = co_await srw.get_single_response(ans, ch);
        if (r >= 0) {
            ans = r;
            break;
        }
    }
    srw.show_window(0, LIGHTGRAY);
    co_return ans;
}

Task<void> fixed_or_not() {
    Vmax = VARIABLE;
    v0 = VARIABLE;
    vmin = VARIABLE;
    INTERCEPT = VARIABLE;
    if (eqn_type == MM || HILL) co_await fixed_Vmax_window();
    else if (ACTIVATION) co_await fixed_v0_Vmax_window();
    else if (eqn_type == INH_L) co_await fixed_v0_window();
    else if (eqn_type == INH_I50 || eqn_type == INH_K50) co_await fixed_v0_vmin_window();
    else if (eqn_type == LINEAR) co_await fixed_intercept_window();
    clean_up();
}

Task<void> fixed_Vmax_window() {
    int ch = 0;
    const int pos[] = {0, 0, 0};
    const char* const s[3] = {"PRESS RETURN TO ACCEPT Vmax STATUS.", "VARIABLE", "FIXED"};

    single_response_window srw(0, 0, 0, 0, s, pos, 1, 2, 2, USER_DEF, GREEN, WHITE, BLACK, WHITE, DOUBLE);
    const int ans = co_await srw.get_single_response(1, ch);
    if (ans < 1) co_return;
    if (ans == 1) Vmax = VARIABLE;
    else Vmax = FIXED;
}

// v3.51: the toggle windows read single bytes with getch(), so an Alt key
// whose scan code is '1' or '2' (Alt+N, Alt+M) also toggles, and only
// RETURN closes them.
Task<void> fixed_v0_Vmax_window() {
    int a = ' ';
    screen scr(21, 9, 58, 17, GREEN, DOUBLE);

    scr.put_str(2, 2, "PRESS # TO TOGGLE PARAMETER STATUS", WHITE);
    scr.put_str(2, 3, "PRESS RETURN TO ACCEPT", WHITE);
    const int l1 = scr.put_str(2, 5, "1. Vmax: ", WHITE);
    const int l2 = scr.put_str(2, 6, "2. v0: ", WHITE);
    while (a != RETURN) {
        if (Vmax == FIXED) scr.put_str(l1, 5, "FIXED   ", BLACK);
        else scr.put_str(l1, 5, "VARIABLE ", BLACK);
        if (v0 == FIXED) scr.put_str(l2, 6, "FIXED    ", BLACK);
        else scr.put_str(l2, 6, "VARIABLE ", BLACK);
        scr.print_screen();
        a = co_await getch();
        if (a == '1') Vmax = ((Vmax == FIXED) ? VARIABLE : FIXED);
        else if (a == '2') v0 = ((v0 == FIXED) ? VARIABLE : FIXED);
    }
    scr.restore_previous_screen();
}

Task<void> fixed_v0_window() {
    int ch = 0;
    const int pos[] = {0, 0, 0};
    const char* const s[3] = {"PRESS RETURN TO ACCEPT v0 STATUS.", "VARIABLE", "FIXED"};

    single_response_window srw(0, 0, 0, 0, s, pos, 1, 2, 2, USER_DEF, GREEN, WHITE, BLACK, WHITE, DOUBLE);
    const int ans = co_await srw.get_single_response(1, ch);
    if (ans < 1) co_return;
    if (ans == 1) v0 = VARIABLE;
    else v0 = FIXED;
}

Task<void> fixed_v0_vmin_window() {
    int a = ' ';
    screen scr(21, 9, 58, 17, GREEN, DOUBLE, WHITE);

    scr.put_str(2, 2, "PRESS # TO TOGGLE PARAMETER STATUS", WHITE);
    scr.put_str(2, 3, "PRESS RETURN TO ACCEPT", WHITE);
    const int l1 = scr.put_str(2, 5, "1. v0: ", WHITE);
    const int l2 = scr.put_str(2, 6, "2. vmin: ", WHITE);
    while (a != RETURN) {
        if (v0 == FIXED) scr.put_str(l1, 5, "FIXED   ", BLACK);
        else scr.put_str(l1, 5, "VARIABLE", BLACK);
        if (vmin == FIXED) scr.put_str(l2, 6, "FIXED    ", BLACK);
        else scr.put_str(l2, 6, "VARIABLE", BLACK);
        scr.print_screen();
        a = co_await getch();
        if (a == '1') v0 = ((v0 == FIXED) ? VARIABLE : FIXED);
        else if (a == '2') vmin = ((vmin == FIXED) ? VARIABLE : FIXED);
    }
    scr.restore_previous_screen();
}

Task<void> fixed_intercept_window() {
    int ch = 0;
    const int pos[] = {0, 0, 0};
    const char* const s[3] = {"PRESS RETURN TO ACCEPT INTERCEPT STATUS.", "VARIABLE", "FIXED & =0"};

    single_response_window srw(0, 0, 0, 0, s, pos, 1, 2, 2, USER_DEF, GREEN, WHITE, BLACK, WHITE, DOUBLE);
    const int ans = co_await srw.get_single_response(1, ch);
    if (ans < 1) co_return;
    if (ans == 1) INTERCEPT = VARIABLE;
    else INTERCEPT = FIXED;
}

void clean_up() {
    int i;

    tot_num_parameters = num_parameters;
    if (eqn_type == ARR) tot_num_parameters++;
    for (i = 1; i <= tot_num_parameters; i++) listp[i] = i;
    if (v0 == FIXED) num_parameters--;
    if (vmin == FIXED) num_parameters--;
    if (Vmax == FIXED) num_parameters--;
    if ((Vmax == FIXED && v0 == VARIABLE && ACTIVATION) || (vmin == FIXED && v0 == VARIABLE && INHIBITION)) {
        i = listp[tot_num_parameters];
        listp[tot_num_parameters] = listp[tot_num_parameters - 1];
        listp[tot_num_parameters - 1] = i;
    }
}

}  // namespace

// ---- entry points -----------------------------------------------------------------------

Task<int> edit_option(int& ch) {
    static int oldans = 1;
    const char* const s[] = {"edit equation Type   -altT", "enter/edit Data      -altD", "edit iMported data   -altM"};
    int ans;
    const int pos[] = {0, 14, 11, 6};
    single_response_window srw(15, 2, 0, 0, s, pos, 0, 3, 1, USER_DEF, GREEN, WHITE, BLACK, WHITE, DOUBLE, WHITE);

    if (!(ans = co_await srw.get_single_response(oldans, ch))) co_return 0;
    else if (ans == -1) co_return -1;
    oldans = ans;
    if (ans == 1) {
        ans = M_EDIT;
        ch = ESC;
        co_await choose_eq();
    } else {
        ans = co_await edit_data(ch, ans);
    }
    co_return ans;
}

// choose equation type - under EDIT option
Task<void> choose_eq() {
    screen scr(1, 4, 80, 23, BLUE);

    scr.put_str(2, 2, "SELECT EQUATION TYPE DESIRED", YELLOW);
    scr.print_screen();
    if (!(eqn_type = co_await get_number())) {
        // v3.51: ESC leaves no equation chosen. The session cannot be
        // without one, so it keeps its equation until the next choice.
        EQN_CHOSEN = NO;
        co_return;
    }
    select_parameters();
    co_await fixed_or_not();
    EQN_CHOSEN = YES;

    // "FIXED & =0": v3.51 always regressed through the origin; the shared
    // core uses the fixed value, which store_equation takes from p[1].
    if (eqn_type == LINEAR && INTERCEPT == FIXED) p[1] = 0.0f;
    // Choosing the same equation and fixed parameters again leaves the
    // session (and its results) as they are.
    if (equation_differs()) store_equation();
    bounds_from_session();
}

Task<int> edit_data(int& ch, int what) {
    // v3.51 strcpy'd the headings into these string literals, so after
    // another equation an Arrhenius grid kept the old headings; here they
    // are chosen afresh each time.
    const char* s[3] = {" ", "temp (°C)", "log10(v)"};
    int r = 1, tr = 1, c = 1, lc = 1, xv = 1, yv = 1, flag = 0, verify = YES;
    edit ed(1, 5, 80, 23, BLUE, s, 10, 2, MAX_VALUES, YES, NUMBERS);

    if (eqn_type != ARR) s[2] = "v";
    if (eqn_type == LINEAR || !eqn_type) {
        s[1] = "X";
        s[2] = "Y";
    } else if (eqn_type == MM || HILL) {
        s[1] = "[S]";
    } else if (ACTIVATION) {
        s[1] = "[A]";
    } else if (INHIBITION) {
        s[1] = "[I]";
    }

    if (what == 3) {
        verify = NO;
        co_await import_data(YES);
        if (!DATA_ENTERED) {
            ch = ESC;
            co_return M_EDIT;
        }
    }
    const Pairs before = sorted(grid_pairs(num_per_column[1]));
    float* const columns[3] = {val[0], val[1], val[2]};
    for (;;) {
        // v3.51: an Alt key that is not a menu key comes back here, and with
        // data already entered the KEEP/START window is shown again.
        ch = co_await ed.enter_values(columns, num_per_column, DATA_ENTERED, "Press ESC to activate MENU", verify, r, tr,
                                      c, lc, xv, yv, flag);
        if (ch == ESC || is_altkey(ch)) break;
    }
    if (!(num_vals = num_per_column[1])) DATA_ENTERED = NO;
    else {
        sort();
        DATA_ENTERED = YES;
    }

    // Write the data back unless only their order changed (sort() above),
    // so that looking at the data leaves the session's results current.
    const Pairs after = grid_pairs(num_vals);
    if (sorted(after) != sorted(session_pairs())) {
        if (what == 3) store_grid_data(imported_source(sorted(after) != before), FILENUM);
        else store_grid_data("Entered by hand", std::nullopt);
    }

    if (is_altkey(ch)) co_return -1;
    co_return M_EDIT;
}

}  // namespace classic
