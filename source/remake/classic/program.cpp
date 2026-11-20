// Port of KINETICS.CPP: the title screen, the main menu loop and the Alt-key
// dispatcher, the header and message lines, error boxes, and sorting.

#include <cmath>
#include <cstring>

#include "classic/kinetics.hpp"

namespace classic {
namespace {

int convert_key(int MENU) {
    if (MENU == M_FILE) return altF;
    if (MENU == M_EDIT) return altE;
    if (MENU == M_CALCULATE) return altC;
    if (MENU == M_VIEW) return altV;
    if (MENU == M_HELP) return altH;
    return 0x00;
}

void initialize() {
    screen title(11, 10, 69, 18, BLACK, THICK);
    screen scr(1, 4, 80, 23, BLUE);

    textattr(0x07);
    clrscr();
    _setcursortype(_NOCURSOR);
    // v3.51 started empty; here the program starts from the shared session.
    load_from_session();
    scr.print_screen();
    title.put_str(19, 2, "SB'S KINETICS PROGRAM", GREEN);
    title.put_str(19, 4, "     Version 3.51", LIGHTRED);
    title.put_str(14, 6, "BioTechniques 13:906-911 (1992)", LIGHTBLUE);
    title.print_screen();
}

void close() {
    textattr(0x07);
    _setcursortype(_NORMALCURSOR);
    window(1, 1, 80, 25);
    clrscr();
}

Task<int> evaluate_key(int oldMENU, int& ALTKEY, int& key) {
    int ans = 0;

    ALTKEY = NO;
    if (!is_altkey(key)) co_return oldMENU;

    if ((key == altR || key == altX || key == altY) && !DATA_ENTERED) {
        co_await print_error("Data NOT entered!!");
        co_return M_EDIT;
    }
    if ((is_altkey(key) == M_CALCULATE || is_altkey(key) == M_VIEW) && !EQN_CHOSEN) {
        co_await print_error("Equation type NOT chosen!!");
        co_return M_EDIT;
    }
    if ((key == altP || is_altkey(key) == M_VIEW) && !PARMS_CALCULATED) {
        co_await print_error("Parameters NOT calculated!!");
        co_return M_CALCULATE;
    }

    // FILE menu speed keys
    if ((ans = is_altkey(key)) == M_FILE) {
        if (key == altF) ans = co_await file_option(key);
        else if (key == altO) {
            key = ESC;
            co_await printer_options();
        } else if (key == altP) {
            co_await print_results(YES);
            key = ESC;
        } else if (key == altQ) ans = 0;
    }
    // EDIT menu speed keys
    else if (ans == M_EDIT) {
        if (key == altE) ans = co_await edit_option(key);
        else if (key == altT) {
            co_await choose_eq();
            key = ESC;
        } else if (key == altD) ans = co_await edit_data(key, 2);
        else if (key == altM) ans = co_await edit_data(key, 3);
    }
    // CALCULATE menu speed keys
    else if (ans == M_CALCULATE) {
        if (key == altC) ans = co_await calculate_option(key);
        else if (key == altR) {
            co_await calculate_parameters();
            key = ESC;
        } else if (key == altS) {
            co_await single_analysis();
            key = ESC;
        } else if (key == altA) {
            co_await automatic_analysis();
            key = ESC;
        } else if (key == altW) {
            co_await automatic_preview();
            key = ESC;
        } else if (key == altX) ans = co_await calculate_new_x(key);
        else if (key == altY) ans = co_await calculate_new_y(key);
    }
    // VIEW menu speed keys
    else if (ans == M_VIEW) {
        if (key == altV) ans = co_await view_option(key);
        else if (key == altL) co_await show_results();
        else if (key == altG) ans = co_await plot_data(key);
        else if (key == altI) ans = co_await print_residuals(key);
    }
    // HELP menu key
    else if (ans == M_HELP || key == altH) ans = co_await help_option(key);

    update_header();
    if (key == altQ && !ans) co_return 0;
    if (ans < 0) {
        ans = is_altkey(key);
        ALTKEY = YES;
    }
    if (!ans) co_return oldMENU;
    co_return ans;
}

Task<void> process_commands() {
    int MENU = 1, oldMENU, ALTKEY = NO, key = 0;
    static const int pos[] = {0, 0, 0, 0, 0, 0};
    static const char* const s[] = {"FILE", "EDIT", "CALCULATE", "VIEW", "HELP"};
    single_response_window srw(1, 1, 80, 1, s, pos, 0, 5, 5, USER_DEF, LIGHTGRAY, WHITE, BLACK, LIGHTRED);

    for (;;) {
        oldMENU = MENU;
        update_header();
        print_message("Press HIGHLIGHTED letter or ALT+KEY");
        if (!ALTKEY) {
            for (;;) {
                if ((MENU = co_await srw.get_single_response(oldMENU, key)) > 0) {
                    // v3.51: `if((key=convert_key(MENU))!=0x00); break;` -- the
                    // stray semicolon makes the break unconditional.
                    key = convert_key(MENU);
                    break;
                }
                if (MENU < 0 && is_altkey(key)) break;
            }
        }
        srw.show_window(is_altkey(key), WHITE);
        MENU = co_await evaluate_key(oldMENU, ALTKEY, key);
        if (!MENU) co_return;
    }
}

}  // namespace

Task<void> run_program() {
    initialize();
    co_await process_commands();
    close();
}

int is_altkey(int key) {
    if (key == altF || key == altO || key == altP || key == altQ) return M_FILE;
    if (key == altE || key == altT || key == altD || key == altM) return M_EDIT;
    if (key == altC || key == altR || key == altS || key == altA || key == altW || key == altX || key == altY)
        return M_CALCULATE;
    if (key == altV || key == altL || key == altG || key == altI) return M_VIEW;
    if (key == altH) return M_HELP;
    return NO;
}

void update_header() {
    screen header_screen(1, 2, 80, 2, MAGENTA);

    if (!EQN_CHOSEN) std::strcpy(HEADER, "NO equation selected");
    else {
        if (eqn_type == MM) std::strcpy(HEADER, "Michaelis-Menten");
        else if (eqn_type == HILL_G0) std::strcpy(HEADER, "Hill eqn (>0)");
        else if (eqn_type == HILL_G1) std::strcpy(HEADER, "Hill eqn (≥1)");
        else if (eqn_type == ACT_G0) std::strcpy(HEADER, "Activation eqn (>0)");
        else if (eqn_type == ACT_G1) std::strcpy(HEADER, "Activation eqn (≥1)");
        else if (eqn_type == ACT_E1) std::strcpy(HEADER, "Activation eqn (=1)");
        else if (eqn_type == INH_L) std::strcpy(HEADER, "Linear inhibition");
        else if (eqn_type == INH_I50) std::strcpy(HEADER, "Non-linear inhibition (I50)");
        else if (eqn_type == INH_K50) std::strcpy(HEADER, "Non-linear inhibition (K50)");
        else if (eqn_type == ARR) std::strcpy(HEADER, "Arrhenius eqn");
        else if (eqn_type == LINEAR) std::strcpy(HEADER, "Linear regression");

        if (eqn_type == LINEAR && INTERCEPT == FIXED) std::strcat(HEADER, "/FIXED intercept");
        // v3.51: `eqn_type == MM || HILL || ACTIVATION`, true for the
        // Michaelis-Menten, Hill and activation equations.
        if (eqn_type == MM || HILL || ACTIVATION) {
            if (Vmax == FIXED) std::strcat(HEADER, "/FIXED Vmax");
            else std::strcat(HEADER, "/VARIABLE Vmax");
        }
        if (INHIBITION && eqn_type != INH_L) {
            if (vmin == FIXED) std::strcat(HEADER, "/FIXED vmin");
            else std::strcat(HEADER, "/VARIABLE vmin");
        }
        if (ACTIVATION || INHIBITION) {
            if (v0 == FIXED) std::strcat(HEADER, "/FIXED v0");
            else std::strcat(HEADER, "/VARIABLE v0");
        }
    }
    header_screen.put_str(1, 1, HEADER, WHITE);
    header_screen.print_screen();
}

void print_message(const char* str) {
    screen message(1, 25, 80, 25, BLACK);
    message.put_str(1, 1, str, WHITE);
    message.print_screen();
}

Task<void> print_error(const char* s) {
    screen scr(16, 10, 65, 16, RED, THICK, YELLOW);
    scr.put_str(2, 2, "ERROR  ERROR  ERROR", WHITE);
    scr.put_str(2, 3, s, WHITE);
    scr.put_str(2, 4, "ERROR  ERROR  ERROR", WHITE);
    scr.print_screen();
    beep();
    print_message("press any key to continue");
    co_await getch();
    scr.restore_previous_screen();
}

// Sorting routine for xy data pairs (a Shell sort, as in v3.51).
void sort() {
    if (num_vals < 1) return;
    int gap = num_vals / 2;
    do {
        int flag;
        do {
            flag = 1;
            for (int i = 1; i <= num_vals - gap; i++) {
                if ((val[1][i] > val[1][i + gap]) || ((val[1][i] == val[1][i + gap]) && (val[2][i] > val[2][i + gap]))) {
                    flag = 0;
                    const float tempx = val[1][i], tempy = val[2][i];
                    val[1][i] = val[1][i + gap];
                    val[2][i] = val[2][i + gap];
                    val[1][i + gap] = tempx;
                    val[2][i + gap] = tempy;
                }
            }
        } while (flag == 0);
        gap = gap / 2;
    } while (gap > 0);

    Y_max = Y_min = val[2][1];
    for (int i = 2; i <= num_vals; i++) {
        if (val[2][i] > Y_max) Y_max = val[2][i];
        if (val[2][i] < Y_min) Y_min = val[2][i];
    }
}

}  // namespace classic
