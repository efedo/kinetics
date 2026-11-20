#include "classic/view/pager.hpp"

#include "classic/kinetics.hpp"

namespace classic {

Task<int> display_wait(d_data& dd, const char s[][80], const int* entcol, int* listd, int ne, int& top_item,
                       int& item, int& sp) {
    // Draw the list, read the key and learn what kind it is, then queue it
    // again in front of any others for display_data to read and act on.
    co_await dd.display_data(s, entcol, listd, ne, top_item, item, DISPLAY_ONLY, NO);
    keystroke ks;
    sp = NO;
    const int key = co_await ks.get_keystroke(sp);
    auto& keys = machine().keys;
    keys.push_front(static_cast<uint8_t>(key));
    if (sp) keys.push_front(key::SPECIAL);
    co_return co_await dd.display_data(s, entcol, listd, ne, top_item, item, DISPLAY_WAIT, NO);
}

}  // namespace classic
