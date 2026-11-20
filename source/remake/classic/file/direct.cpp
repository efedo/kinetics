// Port of DIRECT.CPP. Comments marked "v3.51" note behaviour kept on purpose
// although it looks odd.

#include "classic/file/direct.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>

#include "classic/d_data.hpp"
#include "classic/file/fs_util.hpp"
#include "classic/kinetics.hpp"

namespace classic {
namespace fs = std::filesystem;
using fsu::kSep;

namespace {

// Room for the list. v3.51 had char fn[60][80], so at most 58 entries; a
// modern folder easily holds more.
constexpr int kSlots = 1001;
constexpr int kStop = kSlots - 1;

// The entry (browser only) that asks for a file from the user's computer.
constexpr const char* kUploadEntry = "{ Upload from your computer }";

// UTF-8 text into an 80-byte entry, without splitting a character.
void copy_entry(char* dst, const std::string& s) {
    size_t n = std::min<size_t>(s.size(), 79);
    if (n < s.size())
        while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

fs::path dir_path(const std::string& dap) { return fsu::from_utf8(dap + kSep); }

// The folder a browse starts in, in the browser's form.
std::string start_dir(const std::string& dap) {
    if (dap.size() == 2 && std::isalpha(static_cast<unsigned char>(dap[0])) && dap[1] == ':') return dap;
    return fsu::dir_string(dap.empty() ? fs::path() : fsu::from_utf8(dap));
}

bool ends_with_ext(const std::string& name, const char e[3]) {
    if (name.size() < 3) return false;  // v3.51 read before the start of the name
    for (size_t i = 0; i < 3; i++)
        if (std::tolower(static_cast<unsigned char>(name[name.size() - 3 + i])) != e[i]) return false;
    return true;
}

// DOS wildcards (* and ?), without regard to case.
bool wildcard_match(const char* pat, const char* s) {
    if (!*pat) return !*s;
    if (*pat == '*') return wildcard_match(pat + 1, s) || (*s && wildcard_match(pat, s + 1));
    if (!*s) return false;
    if (*pat != '?' && std::tolower(static_cast<unsigned char>(*pat)) != std::tolower(static_cast<unsigned char>(*s)))
        return false;
    return wildcard_match(pat + 1, s + 1);
}

// The browser's area, put back when the list closes. The list pager saves
// the screen on each call, so after the first key it would put back its own
// earlier picture; the browser keeps what was there before it started.
class saved_area {
    int l_, t_, r_, b_;
    std::vector<unsigned char> buf_;

public:
    saved_area(int l, int t, int r, int b)
        : l_(l), t_(t), r_(r), b_(b), buf_(static_cast<size_t>(2 * (r - l + 1) * (b - t + 1))) {
        gettext(l_, t_, r_, b_, buf_.data());
    }
    void restore() const { puttext(l_, t_, r_, b_, buf_.data()); }
};

}  // namespace

void directory::set_entry(char fn[][80], int i, const std::string& text, const std::string& name) {
    copy_entry(fn[i], text);
    if (entry_name.size() <= static_cast<size_t>(i)) entry_name.resize(static_cast<size_t>(i) + 1);
    entry_name[static_cast<size_t>(i)] = name;
}

int directory::find_single_file(const std::string& dap, const std::string& fname, const std::string& ext) {
    return fsu::exists(fsu::from_utf8(dap + kSep + fname + "." + ext)) ? 1 : 0;
}

int directory::compare_files(const std::string& ffn1, const std::string& ffn2) {
    // v3.51 returned 0 when findfirst() succeeded (it returns 0 on success)
    // and then compared uninitialised blocks; this compares the files.
    std::error_code e1, e2;
    const auto t1 = fs::last_write_time(fsu::from_utf8(ffn1), e1);
    const auto t2 = fs::last_write_time(fsu::from_utf8(ffn2), e2);
    if (e1 || e2) return 0;
    if (t1 > t2) return OLDER;  // v3.51's naming: the first file is the more recent
    if (t1 == t2) return SAME;
    return YOUNGER;
}

void directory::update_files(const std::string& daps, const std::string& fnames, const std::string& destination) {
    // v3.51 ran "copy <file> <destination>/v" for each file that was missing
    // from the destination or older there.
    for (const auto& name : fsu::list_names(dir_path(daps), false)) {
        if (!wildcard_match(fnames.c_str(), name.c_str())) continue;
        const fs::path src = fsu::from_utf8(daps + kSep + name);
        const fs::path dst = fsu::from_utf8(destination + kSep + name);
        std::error_code ec;
        if (fsu::exists(dst)) {
            const auto ts = fs::last_write_time(src, ec);
            const auto td = fs::last_write_time(dst, ec);
            if (ec || ts <= td) continue;
        }
        fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    }
}

int directory::search_for_a_current_directory_file(const char* ext, char fn[][80], int Stop) {
    std::string dap;
    extension = ext;
    get_current_directory(dap);
    drive_and_path = dap;
    return find_subfiles(fn, 0, Stop);
}

int directory::search_for_a_file(const std::string& dap, const char* ext, char fn[][80], int Stop) {
    extension = ext;
    drive_and_path = start_dir(dap);
    return find_subfiles(fn, 0, Stop);
}

Task<int> directory::find_a_file(std::string& dap, const char* ext, std::string& file_name, int left, int top,
                                 int right, int bottom, int sc, int hc, int ec, int restore) {
    auto fn = std::make_unique<char[][80]>(kSlots);
    std::vector<int> lista(kSlots, 0), colour(kSlots, ec);
    d_data dd(left, top, right, bottom, sc, hc);
    const saved_area under(left, top, right, bottom);
    // In the browser, files can also come from the user's computer.
    const int nup = session().is_web() ? 1 : 0;

    highlight_colour = hc;
    drive_and_path = start_dir(dap);
    extension = ext;
    for (;;) {
        std::fill(colour.begin(), colour.end(), ec);
        co_await test_drive_ready(' ');
        const int ndrives = list_drives(fn.get(), colour.data());
        if (nup) {
            set_entry(fn.get(), 2 + ndrives, kUploadEntry, "");
            colour[static_cast<size_t>(2 + ndrives)] = highlight_colour;
        }
        const int first_dir = 2 + ndrives + nup;
        const int ndirs = find_subdirectories(fn.get(), colour.data(), first_dir, kStop);
        const int nfiles = find_subfiles(fn.get(), first_dir + ndirs, kStop);
        const int ne = 1 + ndrives + nup + ndirs + nfiles;

        print_message(25, "Press DIRECTION keys, RETURN to accept, ESC to QUIT");
        int top_item = 1, item = 1, ans;
        // list files and directories found
        do {
            ans = co_await dd.display_data(fn.get(), colour.data(), lista.data(), ne, top_item, item, SHOW_BAR, restore);
        } while (ans != ESC && ans != RETURN);
        if (restore) under.restore();

        if (ans == ESC) co_return 0;
        int i;
        for (i = 1; i <= ne; i++)
            if (lista[static_cast<size_t>(i)]) break;
        if (i == 1) go_back_one_directory();
        else if (i >= 2 && i <= 1 + ndrives) drive_and_path = std::string(1, fn[i][2]) + ":";
        else if (i > 1 + ndrives && i < first_dir) {
            if (co_await upload_a_file(dap, file_name)) co_return 1;
        } else if (i >= first_dir && i < first_dir + ndirs) change_to_subdirectory(i);
        else if (i >= first_dir + ndirs && i <= ne) {
            dap = drive_and_path;
            file_name = entry_name[static_cast<size_t>(i)];
            co_return 1;
        }
    }
}

Task<int> directory::upload_a_file(std::string& dap, std::string& file_name) {
    // The browser's own file dialog chooses the file; it gives no notice when
    // the user cancels it, so ESC stops waiting.
    session().request_upload();
    single_line_window slw(0, 0, 0, 0, "Choose the file in the browser's dialog", MAGENTA, WHITE, THICK, WHITE);
    print_message(25, "Waiting for the file from your computer - press ESC to cancel");
    for (;;) {
        if (const auto up = session().take_uploaded_file()) {
            slw.restore_previous_screen();
            dap = fsu::dir_string(up->parent_path());
            file_name = fsu::to_utf8(up->filename());
            co_return 1;
        }
        while (kbhit()) {
            const int k = co_await getch();
            if (k == ESC) {
                slw.restore_previous_screen();
                co_return 0;
            }
            if (k == SPECIAL) co_await getch();
        }
        co_await next_frame();
    }
}

Task<int> directory::test_drive_ready(char drive) {
    // look for the current directory of the drive in question
    char check = drive == ' ' ? (drive_and_path.empty() ? '\0' : drive_and_path[0])
                              : static_cast<char>(std::toupper(static_cast<unsigned char>(drive)));
    if (check > 'Z' || check < 'A') co_return 0;
#ifdef _WIN32
    const int present = fsu::exists(fs::path(std::string(1, check) + ":\\")) ? 1 : 0;
#else
    const int present = 1;
#endif
    if (!present && drive == ' ') {
        co_await print_error("Drive not ready - returning to default dir", "Any key to continue");
        get_current_directory(drive_and_path);
    }
    co_return present;
}

int directory::list_drives(char fn[][80], int* colour) {
    int ndrives = 0;
    set_entry(fn, 0, "LIST OF FILES FROM: " + drive_and_path + kSep + "*." + extension, "");
    set_entry(fn, 1, "{ ..\\ }", "");
    colour[1] = highlight_colour;
    // v3.51 listed A:, B: and C: without testing them (its test began at D:,
    // where its loop stopped). These are the drives in use (none outside
    // Windows).
    for (const char ch : fsu::logical_drives()) {
        ndrives++;
        char s[16];
        std::snprintf(s, sizeof s, "{ %c: }", ch);
        set_entry(fn, ndrives + 1, s, "");
        colour[ndrives + 1] = highlight_colour;
    }
    return ndrives;
}

void directory::get_current_directory(std::string& dap) { dap = fsu::dir_string(fs::path()); }

int directory::find_subdirectories(char fn[][80], int* colour, int Start, int Stop) {
    int ndirs = 0;
    for (const auto& name : fsu::list_names(dir_path(drive_and_path), true)) {
        if (Start >= Stop) break;
        if (name.empty() || name[0] == '.') continue;
        set_entry(fn, Start, "[" + name + "]", name);
        colour[Start] = highlight_colour;
        Start++;
        ndirs++;
    }
    return ndirs;
}

int directory::find_subfiles(char fn[][80], int Start, int Stop) {
    int nfiles = 0;
    char e[3];
    for (size_t i = 0; i < 3; i++)
        e[i] = i < extension.size() ? static_cast<char>(std::tolower(static_cast<unsigned char>(extension[i]))) : '\0';
    // v3.51 compared the last three characters of the name, not the part
    // after the dot.
    for (const auto& name : fsu::list_names(dir_path(drive_and_path), false)) {
        if (Start >= Stop) break;
        if (e[0] == '*' || ends_with_ext(name, e)) {
            set_entry(fn, Start, name, name);
            Start++;
            nfiles++;
        }
    }
    return nfiles;
}

void directory::go_back_one_directory() {
#ifdef _WIN32
    // v3.51: cut at the previous '\', but not into the drive ("C:").
    if (drive_and_path.size() < 3) return;
    size_t j;
    for (j = drive_and_path.size() - 2; j > 2; j--)
        if (drive_and_path[j] == '\\') break;
    drive_and_path.resize(j);
#else
    if (drive_and_path.empty()) return;
    const size_t j = drive_and_path.rfind('/');
    drive_and_path.resize(j == std::string::npos ? 0 : j);
#endif
}

void directory::change_to_subdirectory(int entry) {
    // v3.51 copied the name from between the brackets of "[name]", up to
    // 78 characters of path.
    drive_and_path += kSep;
    drive_and_path += entry_name[static_cast<size_t>(entry)];
}

Task<int> directory::create_a_file(std::string& dap, const char* ext, std::string& file_name, int left, int top,
                                   int right, int bottom, int sc, int hc, int ec, int restore, int KEEP_FN) {
    auto fn = std::make_unique<char[][80]>(kSlots);
    std::vector<int> lista(kSlots, 0), colour(kSlots, ec);
    d_data dd(left, top, right, bottom, sc, hc);
    const saved_area under(left, top, right, bottom);

    highlight_colour = hc;
    if (!KEEP_FN) file_name.clear();
    drive_and_path = start_dir(dap);
    extension = ext;
    for (;;) {
        std::fill(colour.begin(), colour.end(), ec);
        co_await test_drive_ready(' ');
        const int ndrives = list_drives(fn.get(), colour.data());
        const int ndirs = find_subdirectories(fn.get(), colour.data(), 2 + ndrives, kStop);
        const int nfiles = find_subfiles(fn.get(), 2 + ndrives + ndirs, kStop);
        const int ne = 1 + ndrives + ndirs + nfiles;

        print_message(25, "Press DIR'N keys, RETURN to accept, INSERT to enter new file name, ESC to QUIT");
        int top_item = 1, item = 1, ans;
        // list files and directories found. v3.51: display_data() returned a
        // plain char, so 'R' (0x52, INSERT's scan code) also asks for a name.
        do {
            ans = co_await dd.display_data(fn.get(), colour.data(), lista.data(), ne, top_item, item, SHOW_BAR, restore);
        } while (ans != ESC && ans != INSERT && ans != RETURN);
        if (restore) under.restore();

        if (ans == ESC) co_return 0;
        if (ans == INSERT) {
            const int i = co_await get_file_name(file_name, 0);
            if (i > 0) dap = drive_and_path;
            co_return i;
        }
        int i;
        for (i = 1; i <= ne; i++)
            if (lista[static_cast<size_t>(i)]) break;
        if (i == 1) go_back_one_directory();
        else if (i >= 2 && i <= 1 + ndrives) drive_and_path = std::string(1, fn[i][2]) + ":";
        else if (i > 1 + ndrives && i <= 1 + ndrives + ndirs) change_to_subdirectory(i);
        else if (i > 1 + ndrives + ndirs && i <= ne) {
            dap = drive_and_path;
            file_name = entry_name[static_cast<size_t>(i)];
            co_return co_await get_file_name(file_name, 1);
        }
    }
}

Task<int> directory::get_file_name(std::string& name, int flag) {
    int length = 8, ext_flag = 0, ans, ch = 0;
    static const int pos[] = {0, 0, 0};
    char str[] = "Programme will automatically add .XXX extension";
    static const char* const s[] = {"Overwrite existing file ?", "No", "Yes"};
    char buf[256];
    std::string fname;
    screen scr(11, 8, 70, 14, CYAN, DOUBLE, WHITE);
    single_response_window srw(0, 0, 0, 0, s, pos, 1, 2, 2, USER_DEF, MAGENTA, WHITE, WHITE, YELLOW, SINGLE, YELLOW);

    for (;;) {
        if (extension[0] == '*') {
            ext_flag = length = 11;
            std::strcpy(str, "Add extension to file name");
        } else {
            for (size_t i = 0; i < 3 && i < extension.size(); i++) str[34 + i] = extension[i];
            // remove extension from file name if required
            if (flag) name = name.substr(0, name.find('.'));
        }
        scr.put_str(2, 2, str, WHITE);
        std::snprintf(buf, sizeof buf, "%s", name.c_str());
        const int key = co_await scr.get_string(2, 4, "File name: ", buf, length, flag, WHITE, BLACK);
        name = buf;
        if (key == ESC) {
            scr.restore_previous_screen();
            co_return 0;
        }

        // test for entered extension
        if (!ext_flag) name = name.substr(0, name.find('.')) + "." + extension;

        extract_file_name(name, fname, extension);
        if (find_single_file(drive_and_path, fname, extension)) {
            // v3.51: ESC here returns without taking this box off the screen.
            if (!(ans = co_await srw.get_single_response(1, ch))) co_return 0;
            else if (ans == 2) break;
        } else {
            break;
        }
    }
    scr.restore_previous_screen();
    co_return 1;
}

int directory::find_num_files(const std::string& dap, const char* ext, std::string& last_file_found) {
    int nfiles = 0;
    char e[3];
    const size_t n = std::strlen(ext);
    for (size_t i = 0; i < 3; i++) e[i] = i < n ? static_cast<char>(std::tolower(static_cast<unsigned char>(ext[i]))) : '\0';
    for (const auto& name : fsu::list_names(dir_path(start_dir(dap)), false)) {
        if (e[0] == '*' || ends_with_ext(name, e)) {
            last_file_found = name;
            nfiles++;
        }
    }
    return nfiles;
}

void directory::extract_file_name(const std::string& ffname, std::string& fname, std::string& ext) {
    const size_t i = std::min(ffname.find('.'), ffname.size());
    fname = ffname.substr(0, i);
    // v3.51: the loop meant to copy the extension wrote only ext[0] (with the
    // character after the dot), then cut ext at three characters.
    if (i + 1 < ffname.size()) {
        if (ext.empty()) ext.assign(1, ffname[i + 1]);
        else ext[0] = ffname[i + 1];
    }
    if (ext.size() > 3) ext.resize(3);
}

Task<int> directory::find_a_directory(std::string& dap, int left, int top, int right, int bottom, int sc, int hc,
                                      int ec, int restore) {
    auto fn = std::make_unique<char[][80]>(kSlots);
    std::vector<int> lista(kSlots, 0), colour(kSlots, ec);
    d_data dd(left, top, right, bottom, sc, hc);
    const saved_area under(left, top, right, bottom);

    highlight_colour = hc;
    drive_and_path = start_dir(dap);
    for (;;) {
        std::fill(colour.begin(), colour.end(), ec);
        co_await test_drive_ready(' ');
        const int ndrives = list_drives(fn.get(), colour.data());
        const int ndirs = find_subdirectories(fn.get(), colour.data(), 2 + ndrives, kStop);
        const int ne = 1 + ndrives + ndirs;

        print_message(25, "Press DIRECTION keys, altA to accept, ESC to QUIT");
        int top_item = 1, item = 1, ans;
        // list directories found
        do {
            ans = co_await dd.display_data(fn.get(), colour.data(), lista.data(), ne, top_item, item, SHOW_BAR, restore);
        } while (ans != ESC && ans != RETURN && ans != altA);
        if (restore) under.restore();

        if (ans == ESC) co_return 0;
        if (ans == altA) co_return 1;
        int i;
        for (i = 1; i <= ne; i++)
            if (lista[static_cast<size_t>(i)]) break;
        if (i == 1) go_back_one_directory();
        else if (i >= 2 && i <= 1 + ndrives) drive_and_path = std::string(1, fn[i][2]) + ":";
        else if (i > 1 + ndrives && i <= ne) {
            change_to_subdirectory(i);
            dap = drive_and_path;
        }
    }
}

void directory::print_message(int row, const char* s) {
    screen message(1, row, 80, row, BLACK);
    message.put_str(1, 1, s, WHITE);
    message.print_screen();
}

Task<void> directory::print_error(const char* s, const char* sm) {
    screen scr(11, 9, 70, 17, RED, THICK, YELLOW);
    scr.put_str(2, 2, "ERROR  ERROR  ERROR", WHITE);
    scr.put_str(2, 3, s, WHITE);
    scr.put_str(2, 4, "ERROR  ERROR  ERROR", WHITE);
    scr.put_str(2, 6, sm, WHITE);
    scr.print_screen();
    beep();
    co_await getch();
    scr.restore_previous_screen();
}

}  // namespace classic
