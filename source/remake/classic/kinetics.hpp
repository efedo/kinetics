#pragma once
// Port of KINETICS.HPP: the program's constants, its global state and the
// functions each module provides.
//
// Porting rules for the module files (kin_*.cpp and their helpers):
//  - Keep the original's screens, colours, positions, wording and key
//    handling. Copy text from the original source (it is code page 437; write
//    it here as UTF-8, which put_str, put_char and the windows convert back;
//    the BGI text functions take code page 437, so use cp437() there).
//  - A function that waits for a key becomes a coroutine returning Task<T>,
//    and every call to it becomes `co_await f(...)`. Keys are ints.
//  - The calculations come from the shared core (namespace kin) through the
//    Session (session()), not from the original MATHF/STAT code, so results
//    match the modern interface and include its fixes. Show them in the
//    original's formats.
//  - After changing the equation, data, results or options, write them back
//    with the store_* functions below, so the modern interface sees them.
//  - The original's globals are kept with their names. They are filled from
//    the session by load_from_session() each time the program starts.

#include <optional>
#include <string>
#include <vector>

#include "classic/screen.hpp"
#include "classic/session.hpp"
#include "classic/task.hpp"

namespace classic {

// ---- constants (KINETICS.HPP) -------------------------------------------------
// v3.51 allowed 50 data pairs; the shared session has no limit, so the
// classic data grid holds more (it scrolls).
inline constexpr int MAX_VALUES = 500;
inline constexpr int MAX_PARMS = 4;

inline constexpr int M_FILE = 1, M_EDIT = 2, M_CALCULATE = 3, M_VIEW = 4, M_HELP = 5;
inline constexpr int VARIABLE = 0, FIXED = 1;
inline constexpr int MARQUARDT = 1, GAUSS_N = 2;
inline constexpr int AUTOMATIC = 1, MANUAL = 2;
inline constexpr int MM = 1, HILL_G0 = 2, HILL_G1 = 3, ACT_G0 = 4, ACT_G1 = 5, ACT_E1 = 6, INH_L = 7, INH_I50 = 8,
                     INH_K50 = 9, ARR = 10, LINEAR = 11;
inline constexpr int P_NORMAL = 1, P_LB = 2, P_HW = 3, P_EH = 4, P_HILL = 5, P_LL = 6, P_SATURATION = 7, P_JOB = 8,
                     P_ARRHENIUS = 9, P_RESIDUAL = 10;
inline constexpr int DBASE = 1, MPA = 2;  // import types (NONE = 0)
inline constexpr int TO_PRINTER = 1, TO_FILE = 2;
inline constexpr int SHORT = 0, MEDIUM = 1, LONG = 2;
inline constexpr int SEQUENTIAL = 1, USE_FILE = 2;

struct output_file {
    int format = LONG;
    int location = NONE;  // NONE, TO_PRINTER or TO_FILE
    char header[50] = "";
    char expt_date[50] = "";
    int numbering = SEQUENTIAL;
    int newfile = YES;
    char path[260] = "";  // v3.51: 70 characters
    char name[80] = "";   // v3.51: 30
};

struct import_file {
    int type = NONE;  // NONE, DBASE (spreadsheet) or MPA
    char path[260] = "";
    char name[80] = "";
};

// ---- global state (the externs of KINETICS.HPP) ---------------------------------
inline output_file output;
inline import_file import;
inline int listp[MAX_PARMS + 1];
inline int DF[5];
inline float p[MAX_PARMS + 1], p_max[MAX_PARMS + 1], p_min[MAX_PARMS + 1], sd[MAX_PARMS + 1], T[5];
// v3.51 allocated float **val with val[1][i] = X and val[2][i] = Y, i from 1.
inline float val[3][MAX_VALUES + 1];
inline int num_per_column[3];
inline char HEADER[80];
inline char p_name[MAX_PARMS + 1][30];
inline int num_vals, eqn_type, v0, Vmax, vmin, num_parameters, tot_num_parameters, reverse, GUESS_TYPE,
    MINIMIZATION_TYPE, HILL, ACTIVATION, INHIBITION, N_ITERS, INTERCEPT, FILENUM, EQN_CHOSEN, DATA_ENTERED,
    PARMS_CALCULATED;
inline float corr, std_dev, Y_min, Y_max;

// The shared session, set by the host while the program runs.
Session& session();
void set_session(Session* s);

// ---- sync.cpp: between these globals and the session --------------------------
kin::ModelId model_of(int eqnType);
int eqn_type_of(kin::ModelId id);
// Fills the globals from the session (equation, fixed flags and values,
// data, results, guess and minimization types, import and output settings).
void load_from_session();
// p_name, p_min/p_max, num_parameters, HILL/ACTIVATION/INHIBITION for
// eqn_type (KIN_EDIT select_parameters).
void select_parameters();
// Writes eqn_type and the fixed flags/values (Vmax, v0, vmin, INTERCEPT and
// p[]) to the session. Changing the equation resets the session's other
// parameter settings, as the modern interface does.
void store_equation();
// Writes val[1..2][1..num_vals] to the session's data table.
void store_data(const std::string& source = "Entered by hand", std::optional<int> run = std::nullopt);
// Stores a fit in the session and copies its values into p, sd, std_dev,
// N_ITERS, DF, T and corr. Sets PARMS_CALCULATED.
void store_results(const kin::Analysis& a, std::optional<int> run = std::nullopt);
void store_guess_and_minimization();  // GUESS_TYPE, MINIMIZATION_TYPE
void store_output_options();          // output.format, header, numbering, path...

// ---- program.cpp (KINETICS.CPP) ------------------------------------------------------
Task<void> run_program();  // main()
Task<void> print_error(const char* s);
void print_message(const char* str);
void update_header();
void sort();
int is_altkey(int key);

// ---- KIN_EDIT (kin_edit.cpp) ----------------------------------------------------------
Task<int> edit_option(int& ch);
Task<void> choose_eq();
Task<int> edit_data(int& ch, int what);

// ---- KIN_CD (kin_cd.cpp; result screens from MATHF.CPP) --------------------------------
Task<int> calculate_option(int& ch);
Task<void> calculate_parameters();
Task<void> single_analysis();
Task<void> automatic_analysis();
Task<void> automatic_preview();
Task<int> calculate_new_x(int& ch);
Task<int> calculate_new_y(int& ch);
// The result screens (KIN_CD/MATHF print_*_results), for the session's
// current result; View > caLculated parameters shows them again.
Task<void> print_linear_results();
Task<void> print_mrq_results();
Task<void> print_gn_results();

// ---- KIN_FILE (kin_file.cpp; DIRECT.CPP, GET_DATE.CPP, D_DATA) --------------------------
Task<int> file_option(int& ch);
Task<void> printer_options();
Task<void> print_results(int ASK);
Task<void> import_data(int ASK);
Task<int> get_output_file_name();

// ---- KIN_VIEW (kin_view.cpp; PLOT.CPP, residual tests from STAT.CPP) ---------------------
Task<int> view_option(int& ch);
Task<void> show_results();
Task<int> plot_data(int& ch);
Task<int> print_residuals(int& ch);
Task<void> show_plot(int plot_type);

// ---- KIN_HLP (kin_hlp.cpp) --------------------------------------------------------------
Task<int> help_option(int& ch);

}  // namespace classic
