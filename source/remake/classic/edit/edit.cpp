// Port of EDIT.CPP: the grid editor. Comments marked "v3.51" note where the
// original's behaviour is kept on purpose although it looks odd.

#include "classic/edit/edit.hpp"

namespace classic {

edit::edit(int l, int t, int r, int b, int c, const char* const* s, int soe, int nc, int mnv, int cee, int ns)
    : left(l), top(t), right(r), bottom(b), col_title(s), size_of_entry(soe), n_cols(nc), max_n_values(mnv),
      bkg_colour(c), check_equal_entries(cee) {
    if (top < 2 || bottom > 24 || right > 80 || left < 1) return;
    if ((numbering_system = ns) == LETTERS && max_n_values > 26) return;

    const int screen_width = right - left + 1 - 5;
    const int max_num_screen_cols = screen_width / (size_of_entry + 1);
    if (n_cols > max_num_screen_cols) num_screen_cols = max_num_screen_cols;
    else num_screen_cols = n_cols;
    // c_pos[] has room for 30 columns; v3.51 did not check.
    if (num_screen_cols > 30) num_screen_cols = 30;
    int sbc = (screen_width - num_screen_cols * size_of_entry) / (num_screen_cols + 1);
    if (!sbc) sbc = 1;
    for (int i = 1; i <= num_screen_cols; i++) c_pos[i] = 6 + i * sbc + (i - 1) * size_of_entry;
    num_screen_rows = bottom - top + 1;
    if (num_screen_rows > max_n_values) num_screen_rows = max_n_values;
}

Task<int> edit::enter_values(float* const* v, int* npc, int data_entered, const char* m, int verify, int& r,
                             int& tr, int& c, int& lc, int& xv, int& yv, int& flag) {
    int ans, key = 0;
    const int pos[] = {0, 0, 0};
    const char* const s[3] = {"NUMBER of entries in each column not equal", "Correct error",
                              "Ignore and exit this option"};

    message = m;
    single_response_window srw(0, 0, 0, 0, s, pos, 1, 2, 1, USER_DEF, MAGENTA, YELLOW, WHITE, YELLOW, DOUBLE, YELLOW);
    screen es(left, top, right, bottom, bkg_colour);
    screen mess(1, 25, 80, 25, BLACK);
    mess.put_str(1, 1, message, WHITE);
    value = v;
    n_per_col = npc;

    row = r;
    col = c;
    top_row = tr;
    left_col = lc;
    xvar = xv;
    yvar = yv;
    const auto save_position = [&] {
        r = row;
        c = col;
        tr = top_row;
        lc = left_col;
        xv = xvar;
        yv = yvar;
    };
    if (data_entered && verify) {
        if (co_await clear_data(key, flag) <= 0) {
            // Leaving the KEEP/START window with ESC or an Alt key keeps the
            // data. v3.51 set n_per_col[1] = 0 here, which dropped them, and
            // with the session shared that would also empty the modern
            // interface's table.
            save_position();
            co_return key;
        }
    }
    redraw_screen(es);
    for (;;) {
        ans = 2;
        mess.print_screen();
        key = co_await process_commands(es);
        if (check_equal_entries) {
            for (int i = 2; i <= n_cols; i++)
                if (n_per_col[i] != n_per_col[1]) {
                    // v3.51: the answer's key is returned, so "Ignore and
                    // exit" (RETURN or I) goes back to the caller, which
                    // calls this again; only ESC leaves.
                    ans = co_await srw.get_single_response(1, key);
                    break;
                }
            if (ans == 2 || ans <= 0) {
                save_position();
                co_return key;
            }
        } else {
            save_position();
            co_return key;
        }
    }
}

void edit::show_screen(float* const* v, int* npc, int r, int c, int tr, int lc, int xv, int yv) {
    value = v;
    n_per_col = npc;
    row = r;
    col = c;
    top_row = tr;
    left_col = lc;
    xvar = xv;
    yvar = yv;
    screen es(left, top, right, bottom, bkg_colour);
    redraw_screen(es);
}

Task<int> edit::clear_data(int& ch, int& flag) {
    const int pos[] = {0, 0, 0};
    const char* const s[2] = {"KEEP previously entered values", "START over - erase all values"};

    single_response_window srw(0, 0, 0, 0, s, pos, 0, 2, 1, USER_DEF, CYAN, WHITE, BLACK, WHITE, DOUBLE, WHITE);
    const int ans = co_await srw.get_single_response(1, ch);
    if (ans == 2) {
        for (int i = 1; i <= n_cols; i++) n_per_col[i] = 0;
        flag = 1;
    }
    co_return ans;
}

void edit::display_column_headings() {
    int b, c;
    screen title(left, (top - 1), right, (top - 1), LIGHTGRAY);

    title.blank_screen();
    title.put_str(2, 1, "#", BLACK);
    b = left_col + num_screen_cols - 1;
    if (b > n_cols) {
        b = n_cols;
        c = n_cols - num_screen_cols + 1;
    } else {
        c = left_col;
    }
    int j = 1;
    for (int i = c; i <= b; i++) {
        // On-screen length: the titles are UTF-8 ("temp (°C)").
        const int len = static_cast<int>(cp437(col_title[i]).size());
        const int a = c_pos[j++] + (size_of_entry - len) / 2;
        title.put_str(a, 1, col_title[i], BLACK);
    }
    title.print_screen();
}

Task<int> edit::process_commands(screen& es) {
    screen ins(70, 25, 73, 25, GREEN);
    screen no_ins(70, 25, 73, 25, BLACK);

    ins.put_str(1, 1, "INS", WHITE);
    no_ins.put_str(1, 1, "   ", WHITE);
    insert = ON;
    redraw_screen(es);
    for (;;) {
        float temp;
        int flag_e;
        if (yvar <= n_per_col[xvar]) {
            temp = value[xvar][yvar];
            flag_e = 1;
        } else {
            temp = 0.0f;
            flag_e = 0;
        }
        if (insert == ON) ins.print_screen();
        else no_ins.print_screen();
        const int key = co_await es.get_float(c_pos[col], row, "", temp, size_of_entry, flag_e, YELLOW, WHITE);
        if (key == ESC || keystroke::is_alt(key)) co_return key;
        // A number typed and then left with DEL, INS or TAB is dropped (v3.51).
        if (flag_e && (keystroke::is_direction(key) || key == RETURN)) {
            if (insert == ON) insert_a_number(temp, es);
            else replace_a_number(temp);
        }
        co_await process_key_commands(key, es);
    }
}

Task<void> edit::process_key_commands(int key, screen& es) {
    int flag = 0;

    switch (key) {
        case DELETE_KEY:
            delete_a_number();
            flag++;
            break;
        case INSERT:
            if (insert == ON) insert = OFF;
            else insert = ON;
            break;
        case CTL_HOME:
            col = left_col = 1;
            [[fallthrough]];
        case CTL_PGUP:
        case HOME:
            row = top_row = 1;
            flag++;
            break;
        case CTL_END:
            col = num_screen_cols;
            left_col = n_cols - num_screen_cols + 1;
            xvar = n_cols;
            [[fallthrough]];
        case CTL_PGDN:
        case END:
            row = n_per_col[xvar] + 1;
            if (n_per_col[xvar] == max_n_values) row--;
            top_row = row - num_screen_rows;
            flag++;
            break;
        case CTL_RIGHT:
            left_col = n_cols - num_screen_cols + 1;
            col = num_screen_cols;
            flag++;
            break;
        case CTL_LEFT:
            col = left_col = 1;
            flag++;
            break;
        case DOWN:
        case RETURN: row++; break;
        case UP: row--; break;
        case LEFT: col--; break;
        case RIGHT: col++; break;
        case PGDN:
            top_row += num_screen_rows;
            flag++;
            if ((top_row + num_screen_rows > n_per_col[xvar] + 1) || (top_row + num_screen_rows > max_n_values)) {
                row = num_screen_rows;
                top_row = n_per_col[xvar] - num_screen_rows + 1;
                if (top_row < 1) top_row = 1;
            }
            break;
        case PGUP:
            top_row -= num_screen_rows;
            flag++;
            break;
        default: break;
    }
    const int moved = co_await check_row_cols();
    if (moved || flag) redraw_screen(es);
}

Task<int> edit::check_row_cols() {
    int flag = 0;
    screen mess(1, 25, 60, 25, BLACK);

    if (col < 1) {
        left_col--;
        col = 1;
        flag++;
    }
    if (left_col < 1) {
        left_col = 1;
        flag++;
    }
    if (col > num_screen_cols) {
        left_col++;
        col = num_screen_cols;
        flag++;
    }
    if (left_col + num_screen_cols - 1 > n_cols) {
        left_col = n_cols - num_screen_cols + 1;
        if (left_col < 1) left_col = 1;
    }
    xvar = left_col + col - 1;

    if (row < 1) {
        top_row--;
        row = 1;
        flag++;
    }
    if (top_row < 1) {
        top_row = 1;
        flag++;
    }
    if (row > num_screen_rows) {
        top_row++;
        row = num_screen_rows;
        flag++;
    }
    if (row + top_row - 1 > max_n_values) {
        row = num_screen_rows;
        top_row = n_per_col[xvar] - num_screen_rows + 1;
        if (top_row < 1) top_row = 1;
        flag++;
        mess.put_int(1, 1, "MAX of %i points !! - any key to continue", max_n_values, WHITE);
        mess.print_screen();
        beep();
        // v3.51: a single getch(), so a special key leaves its scan code
        // to be read as the next key.
        co_await getch();
        mess.blank_screen();
        mess.put_str(1, 1, message, WHITE);
        mess.print_screen();
    }
    if (row + top_row - 1 > n_per_col[xvar] + 1) {
        top_row = n_per_col[xvar] - num_screen_rows + 2;
        if (top_row < 1) top_row = 1;
        if (n_per_col[xvar] + 1 > num_screen_rows) row = num_screen_rows;
        else row = n_per_col[xvar] + 1;
        flag++;
    }
    yvar = top_row + row - 1;
    co_return flag;
}

void edit::insert_a_number(float number, screen& es) {
    if (n_per_col[xvar] < max_n_values) {
        n_per_col[xvar]++;
        for (int i = n_per_col[xvar]; i > yvar; i--) value[xvar][i] = value[xvar][i - 1];
    }
    value[xvar][yvar] = number;
    if (n_per_col[xvar] != yvar) redraw_screen(es);
}

void edit::replace_a_number(float number) { value[xvar][yvar] = number; }

void edit::delete_a_number() {
    if (n_per_col[xvar] && (yvar <= n_per_col[xvar])) {
        for (int i = yvar; i < n_per_col[xvar]; i++) value[xvar][i] = value[xvar][i + 1];
        n_per_col[xvar]--;
    }
}

// v3.51 defined these two but never called them.
void edit::add_edit_block(screen& es) {
    es.change_colour(c_pos[col], c_pos[col] + size_of_entry, row, LIGHTGRAY, BLACK);
    es.print_screen();
}

void edit::clear_edit_block(screen& es) {
    es.change_colour(c_pos[col], c_pos[col] + size_of_entry, row, BLUE, WHITE);
    es.print_screen();
}

void edit::redraw_screen(screen& es) {
    static const char ns[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    display_column_headings();
    es.blank_screen();
    for (int i = 1; i <= num_screen_rows; i++) es.put_str(1, i, "     ", REVERSE);
    if (numbering_system == NUMBERS) {
        for (int i = 1; i <= num_screen_rows; i++) es.put_int(2, i, "%i", (i + top_row - 1), REVERSE);
    } else if (numbering_system == LETTERS) {
        for (int i = 1; i <= num_screen_rows; i++) es.put_char(2, i, ns[i + top_row - 1], REVERSE);
    }

    for (int c = 1; c <= num_screen_cols; c++) {
        for (int r = 1; r <= num_screen_rows; r++) {
            const int x = c + left_col - 1;
            const int y = r + top_row - 1;
            if (y > n_per_col[x]) break;
            es.put_float(c_pos[c], r, "%g", value[x][y], WHITE);
        }
    }
    es.print_screen();
}

}  // namespace classic
