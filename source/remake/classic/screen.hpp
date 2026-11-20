#pragma once
// Port of the original program's text-window library (SCREEN.HPP/.CPP) and
// keyboard helpers (KEYSTROK.HPP/.CPP).
//
// The classes, their arguments and their drawing behaviour are the
// original's. Functions that waited for a key are coroutines here: call
// them with `co_await`. Keys are ints 0..255 rather than (signed) chars.
//
// Strings passed to put_str, put_char and the windows are UTF-8 and are
// converted to code page 437, so box-drawing characters and symbols such as
// "≥" can be written as they look. Use cp437() for the converted bytes (for
// example to measure a string that contains such characters).

#include <string>
#include <string_view>
#include <vector>

#include "classic/dos.hpp"
#include "classic/task.hpp"

namespace classic {

// ---- KEYSTROK.HPP -------------------------------------------------------------
inline constexpr int F1 = 0x3B, F2 = 0x3C, F3 = 0x3D, F4 = 0x3E, F5 = 0x3F, F6 = 0x40, F7 = 0x41, F8 = 0x42,
                     F9 = 0x43, F10 = 0x44;
inline constexpr int sF1 = 0x54, sF10 = 0x5D, aF1 = 0x68, aF10 = 0x71, cF1 = 0x5E, cF10 = 0x67;
inline constexpr int HOME = 0x47, UP = 0x48, PGUP = 0x49, LEFT = 0x4B, MIDDLE = 0x4C, RIGHT = 0x4D, END = 0x4F,
                     DOWN = 0x50, PGDN = 0x51, INSERT = 0x52, DELETE_KEY = 0x53, CTL_HOME = 0x77, CTL_END = 0x75,
                     CTL_PGUP = 0x84, CTL_RIGHT = 0x74, CTL_LEFT = 0x73, CTL_PGDN = 0x76;
inline constexpr int RETURN = 0x0D, BKSPACE = 0x08, TAB = 0x09, ESC = 0x1B, BELL = 0x07, SPECIAL = 0x00;
inline constexpr int altA = 0x1E, altB = 0x30, altC = 0x2E, altD = 0x20, altE = 0x12, altF = 0x21, altG = 0x22,
                     altH = 0x23, altI = 0x17, altJ = 0x24, altK = 0x25, altL = 0x26, altM = 0x32, altN = 0x31,
                     altO = 0x18, altP = 0x19, altQ = 0x10, altR = 0x13, altS = 0x1F, altT = 0x14, altU = 0x16,
                     altV = 0x2F, altW = 0x11, altX = 0x2D, altY = 0x15, altZ = 0x2C;
inline constexpr int ON = 1, OFF = 0, YES = 1, NO = 0, UPPER = 1, LOWER = 0;

struct keystroke {
    Task<int> get_special_key();
    Task<int> get_non_special_key();
    Task<int> get_keystroke(int& special);
    Task<int> get_number(int low, int high);
    Task<int> get_letter(int low, int high);
    Task<int> get_alt();
    static int is_direction(int c);
    static int is_edit(int c);
    static int is_return(int c);
    static int is_function(int c);
    static int is_sfunction(int c);
    static int is_afunction(int c);
    static int is_cfunction(int c);
    static int is_digit(int c);
    static int is_alpha(int c);
    static int is_alt(int c);
    static int is_number_ass(int c);
    static int char_to_int(int c);
    static int int_to_char(int i, int CASE);
    static int ichar_to_int(int c);
};

// ---- SCREEN.HPP -----------------------------------------------------------------
inline constexpr int REVERSE = -1;
inline constexpr int MONO_MODE = 7;
inline constexpr int NONE = 0, SINGLE = 1, DOUBLE = 2, THICK = 3;  // border types
inline constexpr int NO_PROMPTS = 0, FN_KEYS = 1, USER_DEF = 2;   // single_response_window prompts

// UTF-8 to code page 437 (bytes that are not valid UTF-8 pass through).
std::string cp437(std::string_view utf8);

class colours {
    int video_mode = 3;  // 80x25 colour text
protected:
    int set_colour(int bkg_col, int tx_col);
};

class screen : public colours {
    int left, right, top, bottom;
    int SIZE, width, num_lines;
    int bkgd_col, boarder_type, boarder_colour;
    std::vector<unsigned char> buf_n, buf_o;
    int blink = OFF;
    void draw_boarder();
    int get_locations(int x, int y, int& place, int& last_loc);
    Task<int> get_number(int x, int y, const char* prompt, double& v, int size, int& flag, int prompt_colour,
                         int number_colour, int flag2);
    void print_error(const char* s);

public:
    screen(int l, int t, int r, int b, int sc, int bt = NONE, int bc = WHITE);
    int put_str(int x, int y, const char* str, int c);
    int put_char(int x, int y, int ch, int c);
    int put_int(int x, int y, const char* format, int i, int c);
    int put_float(int x, int y, const char* format, double f, int c);
    void change_colour(int x_start, int x_end, int y, int bc, int fc);
    void restore_previous_screen();
    void blank_screen();
    void fill_symbol(int symbol, int c);
    void change_screen_colour(int foreground, int bkgd, int bc = WHITE);
    void print_screen();
    void delete_line(int x_start, int x_end, int y);
    Task<int> get_int(int x, int y, const char* prompt, int& i, int size, int& flag, int prompt_colour, int int_colour);
    Task<int> get_float(int x, int y, const char* prompt, float& v, int size, int& flag, int prompt_colour,
                        int float_colour);
    Task<int> get_double(int x, int y, const char* prompt, double& d, int size, int& flag, int prompt_colour,
                         int double_colour);
    Task<int> get_char(int x, int y, const char* prompt, char& c, int& flag, int prompt_colour, int char_colour);
    // `s` must hold at least size + 1 characters.
    Task<int> get_string(int x, int y, const char* prompt, char* s, int size, int& flag, int prompt_colour,
                         int string_colour);
    int add_cursor(int x, int y, int c, int co);
    int remove_cursor(int x, int y, int co);
    void set_blink(int how);
    void put_subscreen(int startX, int startY, int stopX, int stopY, const char* block);
    void get_subscreen(int startX, int startY, int stopX, int stopY, char* block);
};

class single_line_window {
    int top, bottom, left, right;
    std::vector<unsigned char> buf_o;
    void print_error(const char*);

public:
    single_line_window(int l, int t, int r, int b, const char* s, int screen_colour, int str_colour,
                       int boarder_type = NONE, int boarder_colour = WHITE);
    void restore_previous_screen();
};

class no_response_window {
    int top, bottom, left, right;
    std::vector<unsigned char> buf_o;
    void print_error(const char*);

public:
    no_response_window(int l, int t, int r, int b, const char* const s[], int num, int col, int str_colour,
                       int boarder_type = NONE, int boarder_colour = WHITE);
    void restore_previous_screen();
};

// The strings and the highlight-position array must outlive the window.
class single_response_window {
    int top, bottom, left, right;
    int how, boarder_type, screen_colour, prompt_colour, option_colour, highlight_colour, boarder_colour;
    int num_options_lines, num_options, num_prompts, options_per_line, option_length, line_length;
    int start_options, start_prompts, space;
    int hl[51] = {};
    int opt_locx[52] = {};
    int opt_locy[52] = {};
    const int* opt_hl_loc;
    int p_locx;
    std::vector<std::string> prompt;  // 1-based, UTF-8 as given
    std::vector<std::string> option;  // 1-based, UTF-8 as given
    std::vector<unsigned char> buf_o;
    void print_error(const char*);

public:
    single_response_window(int l, int t, int r, int b, const char* const s1[], const int* hll, int np, int no,
                           int opl, int h, int sc, int pc, int oc, int hc, int bt = NONE, int bc = WHITE);
    void show_window(int choice, int boarder_colour);
    Task<int> get_single_response(int choice, int& key);
    void restore_previous_screen();
};

// Set when the original would have printed an error and exited to DOS; the
// host shows it and restarts the program.
inline std::string g_fatal_error;

}  // namespace classic
