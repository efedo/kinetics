#pragma once
// The key loop of the view group's lists (the residual table and the help
// topics): d_data's DISPLAY_WAIT, but also saying whether the key that came
// back was a special one.
//
// display_data returns a special key's scan code and an ordinary key's
// character alike, and several Alt scan codes are printable characters too
// (Alt+D is 0x20, a space; Alt+X is '-'; Alt+H is '#'). The original's loops
// ended on `ch == ESC || is_altkey(ch)`, so with only the returned key a
// space in a help page would act as Alt+D. display_wait reads the key
// first, puts it back, and lets display_data take it as usual.

#include "classic/d_data.hpp"
#include "classic/kinetics.hpp"

namespace classic {

Task<int> display_wait(d_data& dd, const char s[][80], const int* entcol, int* listd, int ne, int& top_item,
                       int& item, int& special);

// The keys that end a DISPLAY_WAIT loop: ESC, or an Alt key (a menu).
inline bool ends_list(int key, int special) { return special ? is_altkey(key) != 0 : key == ESC; }

}  // namespace classic
