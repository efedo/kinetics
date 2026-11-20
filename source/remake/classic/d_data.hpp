#pragma once
// The scrolling list used for the file browser, the residual table and the
// help pages (D_DATA.HPP). v3.51 shipped only the header; this
// implementation is reconstructed from it and from the calls:
//  - the window spans columns left-1..right and rows top-1..bottom; s[0] is
//    a heading on its first row (top-1), black on light grey, and the
//    entries s[1..ne] fill rows top..bottom from column `left` (checked
//    against v3.51's help pages, residual table and file list in DOSBox);
//  - DISPLAY_ONLY draws and returns 0;
//  - DISPLAY_WAIT waits for one key: the direction keys scroll the list,
//    and every key is returned (callers loop until ESC or an Alt key);
//  - SHOW_BAR also moves a highlight bar (black on light grey, across the
//    window) with the direction keys; RETURN
//    tags the highlighted entry (listd[item] = 1, the others 0);
//  - TAG_ON is SHOW_BAR where SPACE toggles the tag of the highlighted entry;
//  - `restore` puts back what was underneath when ESC, RETURN or an Alt key
//    ends the display: what was there when the d_data was created, as the
//    screen class does.
// Keys are returned as get_keystroke() returns them (scan codes for special
// keys, so is_altkey() works on the result), except that ordinary keys other
// than ESC and RETURN (and SPACE with TAG_ON) come back as 0: their codes
// overlap the scan codes (Space = Alt+D).

#include <vector>

#include "classic/screen.hpp"

namespace classic {

inline constexpr int DISPLAY_ONLY = 0, DISPLAY_WAIT = 1, SHOW_BAR = 2, TAG_ON = 3;

class d_data {
    int left, top, right, bottom;
    int winLeft, winTop;  // the window: one column left of `left`, one row above `top`
    int bkgd_colour, high_colour;
    int Width, num_screen_rows;
    std::vector<unsigned char> underneath;

public:
    d_data(int l, int t, int r, int b, int bkgcol, int hlightcol);
    Task<int> display_data(const char s[][80], const int* entcol, int* listd, int ne, int& top_item, int& item,
                           int show_f, int restore);
};

}  // namespace classic
