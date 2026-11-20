#pragma once
// Port of EDIT.HPP: a spreadsheet-like grid of numbers, used by KIN_EDIT to
// enter and edit the data pairs.
//
//   edit es(left, top, right, bottom, c, s, soe, nc, mnv, cee, ns);
//   - left, top, right, bottom: the grid's screen area (the column headings
//     go on the row above `top`);
//   - c: the background colour;
//   - s: the column titles, s[1..nc];
//   - soe: the width of a column (size of entry);
//   - nc: the number of columns;
//   - mnv: the most values a column can hold;
//   - cee: YES to check, on leaving, that the columns hold equal numbers of
//     entries;
//   - ns: LETTERS or NUMBERS for the row labels. With LETTERS and mnv > 26
//     the constructor returns without setting up the grid.
//
// enter_values(v, npc, ...) edits v[1..nc][1..mnv]; npc[1..nc] are the
// numbers of values in each column. Like the original, it keeps pointers to
// the titles, the values and the counts, which must outlive the call.

#include "classic/screen.hpp"

namespace classic {

inline constexpr int LETTERS = 1, NUMBERS = 2;

class edit {
    int left = 0, top = 0, right = 0, bottom = 0;
    const char* const* col_title = nullptr;
    int size_of_entry = 0;
    int n_cols = 0;
    int max_n_values = 0;
    int bkg_colour = 0;
    int check_equal_entries = 0;
    int top_row = 1;
    int left_col = 1;
    int num_screen_cols = 0;
    int num_screen_rows = 0;
    int numbering_system = 0;
    int row = 1;
    int col = 1;
    int yvar = 1;
    int xvar = 1;
    int insert = ON;
    int c_pos[31] = {};

    float* const* value = nullptr;
    int* n_per_col = nullptr;
    const char* message = "";

    Task<int> process_commands(screen& es);
    Task<void> process_key_commands(int key, screen& es);
    void display_column_headings();
    Task<int> check_row_cols();
    void add_edit_block(screen& es);
    void clear_edit_block(screen& es);
    void redraw_screen(screen& es);
    void insert_a_number(float number, screen& es);
    void replace_a_number(float number);
    void delete_a_number();
    Task<int> clear_data(int& key, int& flag);

public:
    edit(int l, int t, int r, int b, int c, const char* const* s, int soe, int nc, int mnv, int cee, int ns);
    // Returns the key that ended the editing: ESC, an Alt key, or the key
    // that answered one of its windows.
    Task<int> enter_values(float* const* v, int* npc, int data_entered, const char* m, int verify, int& r, int& tr,
                           int& c, int& lc, int& xv, int& yv, int& flag);
    void show_screen(float* const* v, int* npc, int r, int c, int tr, int lc, int xv, int yv);
};

}  // namespace classic
