#include "classic/d_data.hpp"

#include <algorithm>
#include <vector>

namespace classic {

d_data::d_data(int l, int t, int r, int b, int bkgcol, int hlightcol)
    : left(l), top(t), right(r), bottom(b), bkgd_colour(bkgcol), high_colour(hlightcol) {
    // As v3.51 draws it: the window takes in the column left of `left` and
    // the row above `top`, which holds the heading.
    winLeft = std::max(1, left - 1);
    winTop = std::max(1, top - 1);
    Width = right - winLeft + 1;
    num_screen_rows = std::max(1, bottom - top + 1);
    underneath.assign(static_cast<size_t>(2 * Width * (bottom - winTop + 1)), 0);
    gettext(winLeft, winTop, right, bottom, underneath.data());
}

Task<int> d_data::display_data(const char s[][80], const int* entcol, int* listd, int ne, int& top_item, int& item,
                               int show_f, int restore) {
    const bool bar = show_f == SHOW_BAR || show_f == TAG_ON;
    if (ne < 1) ne = 0;
    item = std::clamp(item, 1, std::max(1, ne));
    top_item = std::clamp(top_item, 1, std::max(1, ne - num_screen_rows + 1));
    if (bar) {
        if (item < top_item) top_item = item;
        if (item >= top_item + num_screen_rows) top_item = item - num_screen_rows + 1;
    }

    // The heading, black on light grey across the window; the entries from
    // column `left`, and the bar black on light grey across the window.
    screen scr(winLeft, winTop, right, bottom, bkgd_colour);
    const int textCol = left - winLeft + 1, firstRow = top - winTop + 1;
    scr.change_colour(1, Width, 1, bkgd_colour, REVERSE);
    scr.put_str(textCol, 1, s[0], REVERSE);
    for (int row = 0; row < num_screen_rows; ++row) {
        const int i = top_item + row;
        if (i > ne) break;
        const int colour = (show_f == TAG_ON && listd && listd[i]) ? high_colour : entcol[i];
        scr.put_str(textCol, firstRow + row, s[i], colour);
        if (bar && i == item) scr.change_colour(1, Width, firstRow + row, bkgd_colour, REVERSE);
    }
    scr.print_screen();
    if (show_f == DISPLAY_ONLY) co_return 0;

    keystroke ks;
    int special = 0;
    const int key = co_await ks.get_keystroke(special);
    if (special) {
        // As v3.51 does it (checked in DOSBox): pages are a whole screen.
        // With a bar, PGUP/PGDN keep the bar on its screen row, HOME/END
        // move it to the top/bottom row of the screen, and the Ctrl keys go
        // to the first/last entry. Without a bar, the list scrolls and
        // HOME/END do nothing.
        const int page = num_screen_rows;
        const int last_top = std::max(1, ne - num_screen_rows + 1);
        if (bar) {
            const int row = item - top_item;
            if (key == UP && item > 1) item--;
            else if (key == DOWN && item < ne) item++;
            else if (key == PGUP) {
                top_item = std::max(1, top_item - page);
                item = std::min(ne, top_item + row);
            } else if (key == PGDN) {
                top_item = std::min(last_top, top_item + page);
                item = std::min(ne, top_item + row);
            } else if (key == HOME) item = top_item;
            else if (key == END) item = std::min(ne, top_item + num_screen_rows - 1);
            else if (key == CTL_HOME || key == CTL_PGUP) top_item = item = 1;
            else if (key == CTL_END || key == CTL_PGDN) {
                top_item = last_top;
                item = ne;
            }
        } else {
            if (key == UP && top_item > 1) top_item--;
            else if (key == DOWN && top_item < last_top) top_item++;
            else if (key == PGUP) top_item = std::max(1, top_item - page);
            else if (key == PGDN) top_item = std::min(last_top, top_item + page);
            else if (key == CTL_HOME || key == CTL_PGUP) top_item = 1;
            else if (key == CTL_END || key == CTL_PGDN) top_item = last_top;
        }
    } else if (bar && key == RETURN && listd) {
        for (int i = 1; i <= ne; ++i) listd[i] = 0;
        if (ne > 0) listd[item] = 1;
    } else if (show_f == TAG_ON && key == ' ' && listd && ne > 0) {
        listd[item] = !listd[item];
    }

    const bool ends = (!special && (key == ESC || key == RETURN)) || (special && keystroke::is_alt(key));
    if (restore && ends) puttext(winLeft, winTop, right, bottom, underneath.data());
    // Special keys come back as scan codes, which overlap ordinary
    // characters (Space is 0x20, as is Alt+D). Callers act only on ESC,
    // RETURN and special keys, so other ordinary keys come back as 0.
    if (!special && key != ESC && key != RETURN && !(show_f == TAG_ON && key == ' ')) co_return 0;
    co_return key;
}

}  // namespace classic
