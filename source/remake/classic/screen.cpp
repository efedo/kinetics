// Port of SCREEN.CPP and KEYSTROK.CPP. Comments marked "v3.51" note where
// the original's behaviour is kept on purpose although it looks odd.

#include "classic/screen.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace classic {
namespace {

// Unicode code points of code page 437 bytes 0x80..0xFF.
constexpr char32_t kCp437High[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE,
    0x00EC, 0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6,
    0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA,
    0x00BA, 0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502,
    0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514,
    0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550,
    0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C,
    0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
    0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229, 0x2261, 0x00B1, 0x2265, 0x2264, 0x2320,
    0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0,
};

int to_lower(int c) { return std::tolower(c & 0xFF); }
int cp437_len(const std::string& utf8);
bool is_alpha_c(int c) { return c < 0x80 && std::isalpha(c); }
bool is_digit_c(int c) { return c >= '0' && c <= '9'; }
bool is_print_c(int c) { return c >= 0x20 && c < 0x7F; }

}  // namespace

namespace {
int cp437_len(const std::string& utf8) { return static_cast<int>(cp437(utf8).size()); }
}  // namespace

std::string cp437(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        if (b0 < 0x80) {
            out.push_back(static_cast<char>(b0));
            ++i;
            continue;
        }
        char32_t cp = 0;
        size_t len = 0;
        if ((b0 & 0xE0) == 0xC0) { cp = b0 & 0x1F; len = 2; }
        else if ((b0 & 0xF0) == 0xE0) { cp = b0 & 0x0F; len = 3; }
        else if ((b0 & 0xF8) == 0xF0) { cp = b0 & 0x07; len = 4; }
        bool ok = len && i + len <= s.size();
        for (size_t k = 1; ok && k < len; ++k) {
            const auto b = static_cast<unsigned char>(s[i + k]);
            if ((b & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (b & 0x3F);
        }
        if (!ok) {  // not UTF-8: already a code page 437 byte
            out.push_back(static_cast<char>(b0));
            ++i;
            continue;
        }
        char mapped = '?';
        for (int k = 0; k < 128; ++k)
            if (kCp437High[k] == cp) {
                mapped = static_cast<char>(0x80 + k);
                break;
            }
        out.push_back(mapped);
        i += len;
    }
    return out;
}

// ---- keystroke ------------------------------------------------------------------

Task<int> keystroke::get_special_key() {
    for (;;) {
        int k = co_await getch();
        if (k == SPECIAL) {
            k = co_await getch();
            co_return k;
        }
        beep();
    }
}

Task<int> keystroke::get_non_special_key() {
    for (;;) {
        const int k = co_await getch();
        if (k == SPECIAL) {
            co_await getch();
            beep();
        } else {
            co_return k;
        }
    }
}

Task<int> keystroke::get_keystroke(int& special) {
    int k = co_await getch();
    if (k != SPECIAL) {
        special = NO;
        co_return k;
    }
    k = co_await getch();
    special = YES;
    co_return k;
}

Task<int> keystroke::get_number(int low, int high) {
    int i = -1;
    do {
        const int c = co_await getch();
        if (is_return(c)) {
            if (c == ESC) co_return -1;
            if (c == RETURN) co_return 10;
            if (c == TAB) co_return 20;
        } else if (is_digit(c)) {
            i = c - '0';
        }
    } while (i < low || i > high);
    co_return i;
}

Task<int> keystroke::get_letter(int low, int high) {
    int k;
    do {
        k = to_lower(co_await getch());
        if (k == ESC) co_return ESC;
    } while (k < low || k > high);
    co_return k;
}

Task<int> keystroke::get_alt() {
    int k;
    do {
        k = co_await get_special_key();
    } while (!is_alt(k));
    co_return k;
}

int keystroke::is_direction(int c) {
    return c == HOME || c == UP || c == PGUP || c == LEFT || c == MIDDLE || c == RIGHT || c == END || c == DOWN ||
           c == PGDN || c == CTL_HOME || c == CTL_END || c == CTL_PGUP || c == CTL_RIGHT || c == CTL_LEFT ||
           c == CTL_PGDN;
}
int keystroke::is_edit(int c) { return c == INSERT || c == DELETE_KEY; }
int keystroke::is_return(int c) { return c == RETURN || c == TAB || c == ESC; }
int keystroke::is_function(int c) { return c >= F1 && c <= F10; }
int keystroke::is_sfunction(int c) { return c >= sF1 && c <= sF10; }
int keystroke::is_afunction(int c) { return c >= aF1 && c <= aF10; }
int keystroke::is_cfunction(int c) { return c >= cF1 && c <= cF10; }
int keystroke::is_digit(int c) { return c >= '0' && c <= '9'; }
int keystroke::is_number_ass(int c) { return c == '+' || c == '-' || c == '.'; }
int keystroke::is_alpha(int c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

int keystroke::is_alt(int c) {
    for (uint8_t a : key::kAlt)
        if (c == a) return 1;
    return 0;
}

int keystroke::char_to_int(int c) {
    const int b = to_lower(c);
    return (b >= 'a' && b <= 'z') ? b - 'a' + 1 : 0;
}

int keystroke::int_to_char(int i, int CASE) {
    if (i >= 1 && i <= 26) return CASE == LOWER ? 'a' + i - 1 : 'A' + i - 1;
    if (i == 27) return 0;  // v3.51 indexed one past 'z' (the terminating NUL)
    return '0';
}

int keystroke::ichar_to_int(int c) { return is_digit(c) ? c - '0' : 0; }

// ---- colours ----------------------------------------------------------------------

int colours::set_colour(int bkg_col, int tx_col) {
    if (bkg_col < REVERSE || bkg_col > LIGHTGRAY || tx_col < REVERSE || tx_col > WHITE) {
        g_fatal_error = "...ERROR in colours.cpp... bkg_col or tx_col out of range";
        return 0x07;
    }
    int col;
    if (video_mode == MONO_MODE) {
        if (bkg_col == REVERSE || tx_col == REVERSE) col = bkg_col == LIGHTGRAY ? 0x07 : 0x70;
        else if (bkg_col <= BROWN) col = tx_col <= LIGHTGRAY ? 0x07 : 0x0F;
        else col = tx_col <= LIGHTGRAY ? 0x70 : 0x0F;
    } else {
        if (bkg_col == REVERSE || tx_col == REVERSE) col = bkg_col == LIGHTGRAY ? 0x07 : 0x70;
        else col = tx_col + (bkg_col << 4);
    }
    return col;
}

// ---- screen -------------------------------------------------------------------------

screen::screen(int l, int t, int r, int b, int sc, int bt, int bc)
    : left(l), right(r), top(t), bottom(b), bkgd_col(sc), boarder_type(bt), boarder_colour(bc) {
    if (boarder_type < NONE || boarder_type > THICK) print_error("Bad boarder type in screen module");
    width = 2 * (right - left + 1);
    num_lines = bottom - top + 1;
    SIZE = width * num_lines;
    if (SIZE <= 0 || SIZE > 4000 || top < 1 || bottom > 25 || left < 1 || right > 80) {
        print_error("WRONG screen dimensions in SCREEN()");
        left = top = 1;
        right = 80;
        bottom = 25;
        width = 160;
        num_lines = 25;
        SIZE = 4000;
    }
    buf_n.assign(static_cast<size_t>(SIZE), 0);
    buf_o.assign(static_cast<size_t>(SIZE), 0);
    blank_screen();
    gettext(left, top, right, bottom, buf_o.data());
}

void screen::draw_boarder() {
    if (boarder_type == NONE) return;
    const auto col = static_cast<unsigned char>(set_colour(bkgd_col, boarder_colour));
    unsigned char ch[8];
    if (boarder_type == SINGLE) {
        const unsigned char c[8] = {0xC4, 0xC4, 0xB3, 0xB3, 0xDA, 0xBF, 0xC0, 0xD9};  // ─ ─ │ │ ┌ ┐ └ ┘
        std::memcpy(ch, c, 8);
    } else if (boarder_type == DOUBLE) {
        const unsigned char c[8] = {0xCD, 0xCD, 0xBA, 0xBA, 0xC9, 0xBB, 0xC8, 0xBC};  // ═ ═ ║ ║ ╔ ╗ ╚ ╝
        std::memcpy(ch, c, 8);
    } else {
        const unsigned char c[8] = {0xDF, 0xDC, 0xDB, 0xDB, 0xDB, 0xDB, 0xDB, 0xDB};  // ▀ ▄ █ ...
        std::memcpy(ch, c, 8);
    }
    for (int i = 2; i <= width - 4; i += 2) {
        buf_n[i] = ch[0];
        buf_n[i + 1] = col;
    }
    for (int i = 2 + width * (num_lines - 1); i <= SIZE - 4; i += 2) {
        buf_n[i] = ch[1];
        buf_n[i + 1] = col;
    }
    for (int i = width; i <= (num_lines - 2) * width; i += width) {
        buf_n[i] = ch[2];
        buf_n[i + 1] = col;
    }
    for (int i = 2 * width - 2; i <= SIZE - 2 - width; i += width) {
        buf_n[i] = ch[3];
        buf_n[i + 1] = col;
    }
    buf_n[0] = ch[4];
    buf_n[1] = col;
    buf_n[width - 2] = ch[5];
    buf_n[width - 1] = col;
    buf_n[(num_lines - 1) * width] = ch[6];
    buf_n[(num_lines - 1) * width + 1] = col;
    buf_n[SIZE - 2] = ch[7];
    buf_n[SIZE - 1] = col;
}

int screen::put_str(int x, int y, const char* str_utf8, int c) {
    const std::string str = cp437(str_utf8);
    int place, last_loc;
    int col = set_colour(bkgd_col, c);
    const int len = static_cast<int>(str.size());
    if (!x) x = 1 + (right - left + 1 - len) / 2;
    if (!get_locations(x, y, place, last_loc)) return 0;
    std::string s = str;
    if (place + 2 * (len - 1) > last_loc - 1) s = str.substr(0, static_cast<size_t>((last_loc - 1 - place + 2) / 2));
    if (blink) col += 0x80;
    for (char ch : s) {
        buf_n[static_cast<size_t>(place++)] = static_cast<unsigned char>(ch);
        buf_n[static_cast<size_t>(place++)] = static_cast<unsigned char>(col);
    }
    return x + static_cast<int>(s.size());
}

int screen::put_char(int x, int y, int ch, int c) {
    int place, last_loc;
    const int col = set_colour(bkgd_col, c);
    if (!get_locations(x, y, place, last_loc)) return 0;
    buf_n[static_cast<size_t>(place++)] = static_cast<unsigned char>(ch);
    buf_n[static_cast<size_t>(place)] = static_cast<unsigned char>(blink ? col + 0x80 : col);
    return x + 1;
}

int screen::put_int(int x, int y, const char* format, int i, int c) {
    char str[81];
    std::snprintf(str, sizeof str, format, i);
    return put_str(x, y, str, c);
}

int screen::put_float(int x, int y, const char* format, double f, int c) {
    char str[81];
    // v3.51 stored and printed single-precision floats.
    std::snprintf(str, sizeof str, format, static_cast<double>(static_cast<float>(f)));
    return put_str(x, y, str, c);
}

void screen::change_colour(int x_start, int x_end, int y, int bc, int fc) {
    int place, end, last_loc;
    int col = set_colour(bc, fc);
    if (!get_locations(x_start, y, place, last_loc)) return;
    if (!get_locations(x_end, y, end, last_loc)) return;
    place++;
    end++;
    if (blink) col += 0x80;
    for (int i = place; i <= end; i += 2) buf_n[static_cast<size_t>(i)] = static_cast<unsigned char>(col);
}

void screen::blank_screen() {
    fill_symbol(' ', LIGHTGRAY);
    draw_boarder();
}

void screen::fill_symbol(int symbol, int c) {
    int col = set_colour(bkgd_col, c);
    if (blink) col += 0x80;
    for (int i = 0; i < SIZE; i += 2) {
        buf_n[static_cast<size_t>(i)] = static_cast<unsigned char>(symbol);
        buf_n[static_cast<size_t>(i + 1)] = static_cast<unsigned char>(col);
    }
    draw_boarder();
}

void screen::change_screen_colour(int foreground, int bkgd, int bc) {
    int col = set_colour(bkgd, foreground);
    bkgd_col = bkgd;
    boarder_colour = bc;
    if (blink) col += 0x80;
    for (int i = 1; i < SIZE; i += 2) buf_n[static_cast<size_t>(i)] = static_cast<unsigned char>(col);
    draw_boarder();
}

void screen::get_subscreen(int startX, int startY, int stopX, int stopY, char* block) {
    int place, end, last_loc, i = 0;
    if (!get_locations(startX, startY, place, last_loc)) return;
    if (!get_locations(stopX, stopY, end, last_loc)) return;
    while (place <= end + 1) block[i++] = static_cast<char>(buf_n[static_cast<size_t>(place++)]);
}

void screen::put_subscreen(int startX, int startY, int stopX, int stopY, const char* block) {
    int place, end, last_loc, i = 0;
    if (!get_locations(startX, startY, place, last_loc)) return;
    if (!get_locations(stopX, stopY, end, last_loc)) return;
    while (place <= end + 1) buf_n[static_cast<size_t>(place++)] = static_cast<unsigned char>(block[i++]);
}

void screen::print_screen() { puttext(left, top, right, bottom, buf_n.data()); }

void screen::delete_line(int x_start, int x_end, int y) {
    int place, end, last_loc;
    const int col = set_colour(bkgd_col, LIGHTGRAY);
    if (!get_locations(x_start, y, place, last_loc)) return;
    if (!get_locations(x_end, y, end, last_loc)) return;
    while (place <= end) {
        buf_n[static_cast<size_t>(place++)] = ' ';
        buf_n[static_cast<size_t>(place++)] = static_cast<unsigned char>(col);
    }
}

void screen::restore_previous_screen() { puttext(left, top, right, bottom, buf_o.data()); }

Task<int> screen::get_int(int x, int y, const char* prompt, int& i, int size, int& flag, int prompt_colour,
                          int int_colour) {
    double temp = i;
    const int ans = co_await get_number(x, y, prompt, temp, size, flag, prompt_colour, int_colour, 0);
    i = static_cast<int>(temp);
    co_return ans;
}

Task<int> screen::get_float(int x, int y, const char* prompt, float& f, int size, int& flag, int prompt_colour,
                            int float_colour) {
    double temp = f;
    const int ans = co_await get_number(x, y, prompt, temp, size, flag, prompt_colour, float_colour, 1);
    f = static_cast<float>(temp);
    co_return ans;
}

Task<int> screen::get_double(int x, int y, const char* prompt, double& d, int size, int& flag, int prompt_c,
                             int int_c) {
    co_return co_await get_number(x, y, prompt, d, size, flag, prompt_c, int_c, 1);
}

Task<int> screen::get_number(int x, int y, const char* prompt, double& v, int size, int& flag, int prompt_colour,
                             int number_colour, int flag2) {
    char number[] = "                                                       ";
    char blank[] = "                                                       ";
    int x1 = x, c = 0, special = 0;
    keystroke ks;

    if (prompt[0]) x1 = put_str(x, y, prompt, prompt_colour);
    number[size - 1] = blank[size - 1] = '\0';  // v3.51: the field is size - 1 wide
    put_str(x1, y, blank, REVERSE);
    if (flag) put_float(x1, y, "%g", v, REVERSE);
    print_screen();
    for (;;) {
        if (!flag && c < size) {
            add_cursor(x1 + c, y, 0xB3, REVERSE);  // │
            print_screen();
        }
        const int key = co_await ks.get_keystroke(special);
        if (special || ks.is_return(key)) {
            if (c < size && !flag) remove_cursor(x1 + c, y, REVERSE);
            flag = 0;
            if (c) {
                if (key != ESC) {
                    flag = 1;
                    if (c > size) c = size;
                    number[c] = '\0';
                    v = std::atof(number);
                }
                put_str(x1, y, blank, number_colour);
                put_float(x1, y, "%-g", v, number_colour);
            }
            change_colour(x1, x1 + size - 1, y, bkgd_col, number_colour);
            print_screen();
            co_return key;
        }
        if (key == BKSPACE && c > 0) {
            if (c < size) remove_cursor(x1 + c, y, REVERSE);
            c--;
            put_char(c + x1, y, ' ', REVERSE);
            number[c] = '\0';
            print_screen();
        } else if (c < size) {
            if ((flag2 && (key == '-' || key == '.' || key == 'e' || (key >= '0' && key <= '9'))) ||
                (!flag2 && (key == '-' || (key >= '0' && key <= '9')))) {
                flag = 0;
                remove_cursor(x1 + c, y, REVERSE);
                if (!c) put_str(x1, y, blank, REVERSE);
                number[c] = static_cast<char>(key);
                put_char(c + x1, y, key, REVERSE);
                print_screen();
                c++;
            }
        }
    }
}

Task<int> screen::get_char(int x, int y, const char* prompt, char& c, int& flag, int prompt_colour, int char_colour) {
    int x1 = x, special = 0;
    keystroke ks;
    if (prompt[0]) x1 = put_str(x, y, prompt, prompt_colour);
    if (!flag) c = ' ';
    put_char(x1, y, static_cast<unsigned char>(c), REVERSE);
    print_screen();
    flag = 0;
    for (;;) {
        const int key = co_await ks.get_keystroke(special);
        if (is_alpha_c(key) || is_digit_c(key) || special || ks.is_return(key)) {
            if (is_alpha_c(key) || is_digit_c(key)) {
                c = static_cast<char>(key);
                flag = 1;
                put_char(x1, y, key, char_colour);
            }
            change_colour(x1, x1, y, bkgd_col, char_colour);
            print_screen();
            co_return key;
        }
    }
}

Task<int> screen::get_string(int x, int y, const char* prompt, char* s, int size, int& flag, int prompt_colour,
                             int string_colour) {
    char blank[] = "                                                  ";
    char temp[51];
    int x1 = x, c = 0, special = 0, CursorON = NO;
    keystroke ks;

    if (size > 50) {
        print_error("Size too big in get_string");
        size = 50;
    }
    if (prompt[0]) x1 = put_str(x, y, prompt, prompt_colour);
    blank[size] = '\0';
    put_str(x1, y, blank, REVERSE);
    std::snprintf(temp, sizeof temp, "%s", s);
    if (flag) {
        put_str(x1, y, s, REVERSE);
    } else {
        add_cursor(x1, y, 0xB3, REVERSE);
        CursorON = YES;
    }
    print_screen();
    for (;;) {
        const int key = co_await ks.get_keystroke(special);
        if (!special && key == BKSPACE && c > 0) {
            if (c < size && CursorON) remove_cursor(x1 + c, y, REVERSE);
            c--;
            temp[c] = '\0';
            put_str(x1, y, blank, REVERSE);
            put_str(x1, y, temp, REVERSE);
            add_cursor(x1 + c, y, 0xB3, REVERSE);
            print_screen();
        } else if (!special && is_print_c(key) && c < size) {
            if (CursorON) remove_cursor(x1 + c, y, REVERSE);
            if (!c) put_str(x1, y, blank, REVERSE);
            CursorON = YES;
            temp[c] = static_cast<char>(key);
            put_char(x1 + c, y, key, REVERSE);
            c++;
            temp[c] = '\0';
            if (c < size) add_cursor(x1 + c, y, 0xB3, REVERSE);
            print_screen();
        } else if (special || ks.is_return(key)) {
            if (c < size && CursorON) remove_cursor(x1 + c, y, REVERSE);
            flag = 0;
            if (c) {
                if (key != ESC) {
                    std::strcpy(s, temp);
                    flag = 1;
                }
                put_str(x1, y, blank, string_colour);
                put_str(x1, y, s, string_colour);
            }
            change_colour(x1, x1 + size - 1, y, bkgd_col, string_colour);
            print_screen();
            co_return key;
        }
    }
}

int screen::get_locations(int x, int y, int& place, int& last_loc) {
    if (boarder_type == NONE) {
        if (x < 1 || x > width / 2 || y < 1 || y > num_lines) return 0;
        place = (x - 1) * 2 + (y - 1) * width;
        last_loc = y * width - 1;
        if (place < 0 || last_loc > SIZE - 1) return 0;
        return place > last_loc ? 0 : 1;
    }
    if (x < 1 || x > width / 2 - 2 || y < 1 || y > num_lines - 2) return 0;
    place = 2 * x + y * width;
    last_loc = (y + 1) * width - 3;
    if (place < 2 + width || last_loc > SIZE - width - 3) return 0;
    return place > last_loc ? 0 : 1;
}

void screen::print_error(const char* s) { g_fatal_error = s; }

int screen::add_cursor(int x, int y, int c, int co) {
    blink = ON;
    return put_char(x, y, c, co);
}

int screen::remove_cursor(int x, int y, int co) {
    blink = OFF;
    return put_char(x, y, ' ', co);
}

void screen::set_blink(int how) { blink = how ? ON : OFF; }

// ---- single_line_window ------------------------------------------------------------------

single_line_window::single_line_window(int l, int t, int r, int b, const char* s_utf8, int screen_colour,
                                       int str_colour, int boarder_type, int boarder_colour) {
    const std::string s = cp437(s_utf8);
    int a1, a2, a3, line_length, space, start;
    const int length = static_cast<int>(s.size());
    if (l && r) {
        a1 = boarder_type ? 2 : 0;
        line_length = r - l + 1 - a1;
        if (length + a1 > line_length) print_error("Bad left or right in NRW");
        left = l;
        right = r;
    } else {
        if (!boarder_type) { a1 = 0; a2 = 1; a3 = -1; }
        else { a1 = 2; a2 = 0; a3 = 1; }
        line_length = length + a1;
        left = l ? l : a2 + (80 - line_length) / 2;
        right = left + line_length + a3;
        if (right > 80) print_error("BAD right value in NRW");
        if (left < 1) print_error("BAD left value in NRW");
    }
    space = (line_length - length) / 2;
    if (!boarder_type) { a1 = 0; start = 1; }
    else { a1 = 4; start = 2; }
    if (t && b) {
        if (t + a1 > b) print_error("Bad top or bottom in NRW");
        top = t;
        bottom = b;
    } else {
        top = t ? t : (24 - a1) / 2;
        bottom = top + a1;
        if (bottom > 25) print_error("BAD bottom value in NRW");
        if (top < 1) print_error("BAD top value in NRW");
    }
    start += (bottom - top - a1) / 2;
    buf_o.assign(static_cast<size_t>(2 * (right - left + 1) * (bottom - top + 1)), 0);
    gettext(left, top, right, bottom, buf_o.data());
    screen scr(left, top, right, bottom, screen_colour, boarder_type, boarder_colour);
    scr.put_str(1 + space, start, s_utf8, str_colour);
    scr.print_screen();
}

void single_line_window::restore_previous_screen() { puttext(left, top, right, bottom, buf_o.data()); }
void single_line_window::print_error(const char* str) { g_fatal_error = str; }

// ---- no_response_window --------------------------------------------------------------------

no_response_window::no_response_window(int l, int t, int r, int b, const char* const s[], int num, int col,
                                       int str_colour, int boarder_type, int boarder_colour) {
    int a1, a2, a3, line_length, space, start, length = 0;
    for (int i = 0; i < num; i++) length = std::max(length, static_cast<int>(cp437(s[i]).size()));
    if (l && r) {
        a1 = boarder_type ? 2 : 0;
        line_length = r - l + 1 - a1;
        if (length + a1 > line_length) print_error("Bad left or right in NRW");
        left = l;
        right = r;
    } else {
        if (!boarder_type) { a1 = 0; a2 = 1; a3 = -1; }
        else { a1 = 2; a2 = 0; a3 = 1; }
        line_length = length + a1;
        left = l ? l : a2 + (80 - line_length) / 2;
        right = left + line_length + a3;
        if (right > 80) print_error("BAD right value in NRW");
        if (left < 1) print_error("BAD left value in NRW");
    }
    space = (line_length - length) / 2;
    if (!boarder_type) { a1 = 0; start = 1; }
    else { a1 = 4; start = 2; }
    if (t && b) {
        if (t + num + a1 - 1 > b) print_error("Bad top or bottom in NRW");
        top = t;
        bottom = b;
    } else {
        top = t ? t : (25 - num - a1) / 2;
        bottom = top + a1 + num - 1;
        if (bottom > 25) print_error("BAD bottom value in NRW");
        if (top < 1) print_error("BAD top value in NRW");
    }
    start += (bottom - top + 1 - num - a1) / 2;
    buf_o.assign(static_cast<size_t>(2 * (right - left + 1) * (bottom - top + 1)), 0);
    gettext(left, top, right, bottom, buf_o.data());
    screen scr(left, top, right, bottom, col, boarder_type, boarder_colour);
    for (int i = 0; i < num; i++) scr.put_str(1 + space, i + start, s[i], str_colour);
    scr.print_screen();
}

void no_response_window::restore_previous_screen() { puttext(left, top, right, bottom, buf_o.data()); }
void no_response_window::print_error(const char* str) { g_fatal_error = str; }

// ---- single_response_window ----------------------------------------------------------------

single_response_window::single_response_window(int l, int t, int r, int b, const char* const s1[], const int* hll,
                                               int np, int no, int opl, int h, int sc, int pc, int oc, int hc,
                                               int bt, int bc)
    : how(h), boarder_type(bt), screen_colour(sc), prompt_colour(pc), option_colour(oc), highlight_colour(hc),
      boarder_colour(bc), num_options(no), num_prompts(np), options_per_line(opl), opt_hl_loc(hll) {
    int a1, a2, a3, denom, prompt_length, nom_ll, k;
    if ((how == NO_PROMPTS && num_options > 50) || (how == FN_KEYS && num_options > 10) ||
        (how == USER_DEF && num_options > 26)) {
        print_error("Too many prompts or options");
        num_options = 0;
    }
    prompt.assign(1, std::string());
    for (int i = 0; i < num_prompts; ++i) prompt.push_back(s1[i]);
    option.assign(1, std::string());
    for (int i = 0; i < num_options; ++i) option.push_back(s1[num_prompts + i]);

    for (int i = 1; i <= num_options; i++) {
        if (how == NO_PROMPTS || how == USER_DEF) hl[i] = 0;
        else if (how == FN_KEYS) {
            hl[i] = 2;
            hl[10] = 3;
        }
    }
    prompt_length = 0;
    for (int i = 1; i <= num_prompts; i++) prompt_length = std::max(prompt_length, cp437_len(prompt[static_cast<size_t>(i)]));
    option_length = 0;
    for (int i = 1; i <= num_options; i++) {
        k = cp437_len(option[static_cast<size_t>(i)]) + hl[i];
        option_length = std::max(option_length, k);
    }

    nom_ll = option_length * options_per_line + options_per_line - 1;
    if (l && r) {
        a1 = boarder_type ? 2 : 0;
        line_length = r - l + 1 - a1;
        if (prompt_length + a1 > line_length || nom_ll + a1 > line_length) print_error("Bad left or right in SRW");
        left = l;
        right = r;
    } else {
        if (!boarder_type) { a1 = 0; a2 = 1; a3 = -1; }
        else { a1 = 2; a2 = 0; a3 = 1; }
        line_length = prompt_length + a1;
        if (nom_ll + a1 > line_length) line_length = nom_ll + a1;
        left = l ? l : a2 + (80 - line_length) / 2;
        right = left + line_length + a3;
        if (right > 80) print_error("BAD right value in SRW");
        if (left < 1) print_error("BAD left value in SRW");
    }
    denom = options_per_line + 1;
    if (line_length == nom_ll && !boarder_type) denom = options_per_line > 1 ? options_per_line - 1 : 1;
    space = (line_length - options_per_line * option_length) / denom;

    num_options_lines = num_options / options_per_line;
    if (num_options % options_per_line) num_options_lines++;
    k = num_prompts + num_options_lines;
    if (num_prompts) k++;
    if (!boarder_type) { a1 = 0; start_prompts = 1; }
    else { a1 = 4; start_prompts = 2; }
    if (t && b) {
        if (t + k + a1 - 1 > b) print_error("Bad top or bottom in SRW");
        top = t;
        bottom = b;
    } else {
        top = t ? t : (25 - k - a1) / 2;
        bottom = top + a1 + k - 1;
        if (bottom > 25) print_error("BAD bottom value in SRW");
        if (top < 1) print_error("BAD top value in SRW");
    }
    start_prompts += (bottom - top + 1 - k - a1) / 2;
    start_options = 1 + start_prompts + num_prompts;
    if (!num_prompts) start_options--;

    k = start_options;
    int j = 1;
    while (j <= num_options) {
        for (int i = 1; i <= options_per_line; i++) {
            a1 = (line_length == nom_ll && !boarder_type) ? i - 1 : i;
            opt_locx[j] = 1 + a1 * space + (i - 1) * option_length;
            opt_locy[j++] = k;
            if (j > num_options) break;
        }
        k++;
    }

    if (!boarder_type) a1 = a2 = 0;
    else { a1 = 2; a2 = 1; }
    p_locx = (prompt_length + a1 == line_length) ? a2 + 1 : (line_length - prompt_length) / 2 + a2 + 1;
    buf_o.assign(static_cast<size_t>(2 * (right - left + 1) * (bottom - top + 1)), 0);
}

Task<int> single_response_window::get_single_response(int choice, int& key) {
    int special = 0, a1, a2;
    char str[80];
    int let[30] = {};
    const int fun_key[] = {'0', F1, F2, F3, F4, F5, F6, F7, F8, F9, F10};
    keystroke ks;
    screen scr(left, top, right, bottom, screen_colour, boarder_type, boarder_colour);

    gettext(left, top, right, bottom, buf_o.data());
    if (choice > num_options || choice < 0) {
        print_error("Improper input value");
        co_return 0;
    }
    const auto opt = [&](int i) -> const std::string& { return option[static_cast<size_t>(i)]; };
    if (how == USER_DEF)
        for (int i = 1; i <= num_options; i++) let[i] = to_lower(static_cast<unsigned char>(cp437(opt(i))[static_cast<size_t>(opt_hl_loc[i])]));
    for (int i = 1; i <= num_prompts; i++)
        scr.put_str(p_locx, start_prompts + i - 1, prompt[static_cast<size_t>(i)].c_str(), prompt_colour);
    for (int i = 1; i <= num_options; i++) {
        if (how == NO_PROMPTS || how == USER_DEF) std::snprintf(str, sizeof str, "%s", opt(i).c_str());
        else std::snprintf(str, sizeof str, "F%i%s", i, opt(i).c_str());
        scr.put_str(opt_locx[i], opt_locy[i], str, option_colour);
    }

    for (;;) {
        for (int i = start_options; i <= start_options + num_options_lines - 1; i++)
            scr.change_colour(1, line_length, i, screen_colour, option_colour);
        if (how) {
            for (int i = 1; i <= num_options; i++) {
                if (how == USER_DEF) a1 = a2 = opt_locx[i] + opt_hl_loc[i];
                else {
                    a1 = opt_locx[i];
                    a2 = a1 + hl[i] - 1;
                }
                scr.change_colour(a1, a2, opt_locy[i], screen_colour, highlight_colour);
            }
        }
        if (choice)
            scr.change_colour(opt_locx[choice], opt_locx[choice] + hl[choice] + cp437_len(opt(choice)) - 1,
                              opt_locy[choice], screen_colour, REVERSE);
        scr.print_screen();
        key = co_await ks.get_keystroke(special);
        if (!special) {
            if (key == ESC) {
                scr.restore_previous_screen();
                co_return 0;
            }
            if (how == USER_DEF && ks.is_alpha(key)) {
                for (int i = 1; i <= num_options; i++)
                    if (let[i] == to_lower(key)) {
                        scr.restore_previous_screen();
                        co_return i;
                    }
            } else if (key == RETURN && choice) {
                scr.restore_previous_screen();
                co_return choice;
            }
        } else {
            if (how == FN_KEYS && ks.is_function(key)) {
                for (int i = 1; i <= num_options; i++)
                    if (fun_key[i] == key) {
                        scr.restore_previous_screen();
                        co_return i;
                    }
            } else if (ks.is_alt(key)) {
                scr.restore_previous_screen();
                co_return -1;
            } else if (choice && ks.is_direction(key)) {
                if (key == HOME) choice = 1;
                else if (key == END) choice = num_options;
                else if (key == RIGHT && choice < num_options) choice++;
                else if (key == LEFT && choice > 1) choice--;
                else if (key == UP && choice > options_per_line) choice -= options_per_line;
                else if (key == DOWN) {
                    choice += options_per_line;
                    if (choice > num_options) choice = num_options;
                }
            }
        }
    }
}

void single_response_window::show_window(int choice, int bc) {
    char str[80];
    int a1, a2;
    screen scr(left, top, right, bottom, screen_colour, boarder_type, bc);
    if (choice > num_options || choice < 0) {
        print_error("Improper input value");
        return;
    }
    for (int i = 1; i <= num_prompts; i++)
        scr.put_str(p_locx, start_prompts + i - 1, prompt[static_cast<size_t>(i)].c_str(), prompt_colour);
    for (int i = 1; i <= num_options; i++) {
        const std::string& o = option[static_cast<size_t>(i)];
        if (how == NO_PROMPTS || how == USER_DEF) std::snprintf(str, sizeof str, "%s", o.c_str());
        else std::snprintf(str, sizeof str, "F%i%s", i, o.c_str());
        scr.put_str(opt_locx[i], opt_locy[i], str, option_colour);
        if (how) {
            if (how == USER_DEF) a1 = a2 = opt_locx[i] + opt_hl_loc[i];
            else {
                a1 = opt_locx[i];
                a2 = a1 + hl[i] - 1;
            }
            scr.change_colour(a1, a2, opt_locy[i], screen_colour, highlight_colour);
        }
        if (choice == i)
            scr.change_colour(opt_locx[i], opt_locx[i] + hl[i] + cp437_len(o) - 1, opt_locy[i],
                              screen_colour, REVERSE);
    }
    scr.print_screen();
}

void single_response_window::restore_previous_screen() { puttext(left, top, right, bottom, buf_o.data()); }
void single_response_window::print_error(const char* str) { g_fatal_error = str; }

}  // namespace classic
