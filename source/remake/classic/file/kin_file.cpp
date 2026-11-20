// Port of KIN_FILE.CPP: the File menu, importing spreadsheet and MPA files
// (choosing them with the browser of DIRECT.CPP), the printer (output)
// options, and printing results. Comments marked "v3.51" note behaviour
// kept on purpose although it looks odd.
//
// What stands in for v3.51's files and devices:
//  - INPUT.DAT and OUTPUT.DAT, which kept the last import and the output
//    options between runs, are the shared session's import and report
//    settings. v3.51 read OUTPUT.DAT at the start of each function here and
//    wrote it back where noted; changes it did not write back were lost at
//    the next read, and are undone here.
//  - The imported file is read by the session when it is chosen; import_data
//    then takes one run or dataset from it.
//  - Reports are formatted by the shared core (kin::format_report), which
//    has v3.51's three formats with its defects fixed.
//  - There is no printer. PRINTER output is appended to PRINTOUT.PRN in the
//    report folder (in the browser it is downloaded), and the printer is
//    always on line.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "classic/file/direct.hpp"
#include "classic/file/fs_util.hpp"
#include "classic/file/get_date.hpp"
#include "classic/kinetics.hpp"
#include "core/import.hpp"
#include "core/report.hpp"

namespace classic {
namespace fs = std::filesystem;

namespace {

// The printer's output file. (Not LPT1.PRN: Windows reserves LPT1 as a
// device name, with any extension.)
constexpr const char* kPrinterFile = "PRINTOUT.PRN";

// The printer port's status, as biosprint(2, ...) returned it: 0x80 not
// busy, 0x10 selected. The emulated printer is always ready.
volatile int g_printer_status = 0x90;

int biosprint(int, int, int) { return g_printer_status; }

// The folder the last import was chosen from (v3.51 kept it in INPUT.DAT).
std::string g_import_dir;

// Browser only: the file of an automatic analysis, downloaded when it ends.
std::optional<fs::path> g_pending_download;

void copy_string(char* dest, size_t size, const std::string& s) { std::snprintf(dest, size, "%s", s.c_str()); }

// v3.51's delay(). It ignored the keyboard; here a key typed meanwhile ends
// the pause early (the key is still read afterwards), so typing ahead, and
// scripted keys, need not wait.
Task<void> delay(int ms) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end && !kbhit()) co_await next_frame();
}

void flush_download() {
    if (!g_pending_download) return;
    const fs::path file = *g_pending_download;
    g_pending_download.reset();
    session().write_text(file, "", true);
}

// Writes report text. In the browser, write_text() downloads the whole file
// each time; during an automatic analysis (defer) that would be once per
// run, so the download waits for the end of the analysis.
bool write_output(const fs::path& path, const std::string& text, bool append, bool defer) {
    if (defer && session().is_web()) {
        if (g_pending_download && *g_pending_download != path) flush_download();
        if (!kin::write_text_file(path, text, append)) return false;
        g_pending_download = path;
        return true;
    }
    if (g_pending_download && *g_pending_download != path) flush_download();
    g_pending_download.reset();
    return session().write_text(path, text, append);
}

std::string web_output_dir() {
    std::error_code ec;
    fs::create_directories("/downloads", ec);
    return "/downloads";
}

// Where get_output_file_name() starts browsing: output.path, as in v3.51.
std::string output_start_dir() {
    if (output.path[0]) return output.path;
    if (session().is_web()) return web_output_dir();
    return fsu::dir_string(session().default_dir());
}

// The file print_results() writes to: v3.51's output.path\output.name. The
// session keeps the full path (output.path and output.name are fixed-size).
fs::path output_target() {
    if (!output.name[0]) return {};  // v3.51 then opened "path\" and failed
    const auto& rs = session().report_state();
    if (!rs.file.empty()) return rs.file;
    return fsu::from_utf8(std::string(output.path) + fsu::kSep + output.name);
}

void set_output_file(const std::string& dap, const std::string& name) {
    copy_string(output.path, sizeof output.path, dap);
    copy_string(output.name, sizeof output.name, name);
    auto& rs = session().report_state();
    if (name.empty()) rs.file.clear();
    else rs.file = fsu::from_utf8(dap + fsu::kSep + name);
}

// The printer's file: in the report file's folder, else the default folder.
fs::path printer_target() {
    if (session().is_web()) return fs::path(web_output_dir()) / kPrinterFile;
    const auto& rs = session().report_state();
    if (!rs.file.empty() && fsu::is_directory(rs.file.parent_path())) return rs.file.parent_path() / kPrinterFile;
    return fsu::from_utf8(fsu::dir_string(session().default_dir()) + fsu::kSep + kPrinterFile);
}

// v3.51: rewind(fp1); fwrite(&output, sizeof(output), 1, fp1) on OUTPUT.DAT.
void save_output() {
    session().report_state().newFile = output.newfile == YES;
    store_output_options();
}

// ---- import ---------------------------------------------------------------------

// The session reads the file when it is chosen and explains at length what
// is wrong with it; v3.51 found out later, and said less. Show the session's
// explanation when it fits the error box, else v3.51's wording.
std::string import_error_text(const std::string& err, const fs::path& path, int type) {
    if (err.rfind("Can't open", 0) == 0) return "Can't open file";
    std::string s = err;
    const std::string prefix = fsu::to_utf8(path.filename()) + ": ";
    if (s.rfind(prefix, 0) == 0) s.erase(0, prefix.size());
    if (s.rfind("Incorrect file format", 0) == 0) return "Incorrect file format";
    if (cp437(s).size() <= 47) return s;
    return type == DBASE ? "Incorrect file format" : "Can't open file";
}

// v3.51 read the runs one by one: it reported "End of File encountered" on
// reaching the closing '#', and "Run # not found" at the end of a file
// without one.
bool has_end_marker(const fs::path& path) {
    const auto text = kin::read_text_file(path);
    if (!text) return true;
    const size_t first = text->find('#');
    return first != std::string::npos && text->find('#', first + 1) != std::string::npos;
}

// Lines as the core's MPA reader counts them.
size_t count_lines(std::string_view t) {
    if (t.empty()) return 0;
    size_t n = static_cast<size_t>(std::count(t.begin(), t.end(), '\n'));
    if (t.back() != '\n') n++;
    return n;
}

// After a run or dataset is in val[][]: share it.
void loaded(const std::string& source) {
    // v3.51 left Y_min and Y_max to sort(), which Calculate > Single never
    // called (defect 2); keep them up to date.
    if (num_vals > 0) {
        Y_min = Y_max = val[2][1];
        for (int i = 2; i <= num_vals; i++) {
            Y_min = std::min(Y_min, val[2][i]);
            Y_max = std::max(Y_max, val[2][i]);
        }
    }
    session().import_state().selectedRun = FILENUM;
    store_data(source, FILENUM);
}

Task<void> import_file_name(int type) {
    directory dty;
    const auto& im = session().import_state();
    // v3.51 started from the folder kept in INPUT.DAT.
    std::string dap;
    if (!g_import_dir.empty()) dap = g_import_dir;
    else if (!im.path.empty() && fsu::is_directory(im.path.parent_path())) dap = fsu::dir_string(im.path.parent_path());
    else dap = fsu::dir_string(session().default_dir());
    std::string name = import.name;

    // get_new_file. v3.51 set import.type = NONE first, so cancelling (or a
    // file that would not open) left no file selected; the session keeps the
    // earlier import in both cases.
    if (co_await dty.find_a_file(dap, "***", name, 16, 6, 65, 20, CYAN, WHITE, BLACK, YES) > 0) {
        g_import_dir = dap;
        const fs::path path = fsu::from_utf8(dap + fsu::kSep + name);
        const auto err = type == DBASE ? session().import_spreadsheet(path) : session().import_mpa(path);
        if (err) {
            co_await print_error(import_error_text(*err, path, type).c_str());
        } else {
            import.type = type;
            copy_string(import.path, sizeof import.path, dap);
            copy_string(import.name, sizeof import.name, name);
        }
    }
    FILENUM = 1;
}

Task<int> import_spreadsheet_data(int ASK) {
    int flag;

    if (ASK) {
        screen scr(21, 8, 60, 12, MAGENTA, SINGLE, WHITE);
        flag = 1;
        const int ch = co_await scr.get_int(2, 2, "Get entry number: ", FILENUM, 5, flag, WHITE, BLACK);
        scr.restore_previous_screen();
        if (ch == ESC) co_return 0;
    }

    auto& im = session().import_state();
    if (!im.sheet) {
        co_await print_error("Incorrect file format");
        co_return 0;
    }
    const kin::SpreadsheetFile& sheet = *im.sheet;
    if (sheet.x.size() > static_cast<size_t>(MAX_VALUES)) {
        co_await print_error("Too many substrate values for program");
        co_return 0;
    }
    // first find the correct run number if ASK==YES or
    // just read in the next set of data if ASK==NO
    const kin::SpreadsheetRun* run = nullptr;
    for (const auto& r : sheet.runs)
        if (ASK == YES ? r.number == FILENUM : r.number > FILENUM) {
            run = &r;
            break;
        }
    if (!run) {
        co_await print_error(has_end_marker(im.path) ? "End of File encountered" : "Run # not found");
        co_return 0;
    }
    if (ASK != YES) FILENUM = run->number;

    // make sure that all velocities are > 0.0 (otherwise a blank is found)
    int ns = 0;
    for (const auto& pt : sheet.points(*run, im.negativeIsMissing)) {
        ns++;
        val[1][ns] = static_cast<float>(pt.x);
        val[2][ns] = static_cast<float>(pt.y);
    }
    num_vals = num_per_column[1] = num_per_column[2] = ns;
    loaded("Run #" + std::to_string(run->number) + " of " + fsu::to_utf8(im.path.filename()));
    co_return 1;
}

Task<int> import_MPA_data(int ASK) {
    auto& im = session().import_state();
    int nvel = im.mpaCount;  // v3.51: static int nvel = 1, kept for the run of the program
    int flag;
    screen scr(20, 7, 61, 13, MAGENTA, SINGLE, WHITE);

    const int l = scr.put_str(2, 4, "Enter the # of velocity values: ", WHITE);
    scr.put_int(l, 4, "%i", nvel, BLACK);
    if (!ASK) {
        FILENUM++;
    } else {
        flag = 1;
        int ch = co_await scr.get_int(2, 2, "Enter the starting \"dataset\" #: ", FILENUM, 5, flag, WHITE, BLACK);
        if (ch == ESC) {
            scr.restore_previous_screen();
            co_return 0;
        }
        flag = 1;
        ch = co_await scr.get_int(2, 4, "Enter the # of velocity values: ", nvel, 5, flag, WHITE, BLACK);
        scr.restore_previous_screen();
        if (ch == ESC) co_return 0;
        if (nvel < 1 || nvel > 30 || FILENUM < 1 || FILENUM > 20) {
            co_await print_error("Bad values entered - returning to MENU");
            co_return 0;
        }
        im.mpaCount = nvel;
    }
    // The count can also come from the modern interface.
    if (nvel < 1 || nvel > MAX_VALUES) {
        co_await print_error("Bad values entered - returning to MENU");
        co_return 0;
    }

    const auto ds = kin::parse_mpa_dataset(im.mpaText, FILENUM, nvel);
    if (!ds.error.empty()) {
        // v3.51 skipped 3 + (dataset - 1) * n lines, then read n "cell, v"
        // lines: running out of lines gave the first message, a line without
        // a velocity (or a negative one) the second.
        const long long first = 3LL + static_cast<long long>(FILENUM - 1) * nvel;
        if (FILENUM < 1 || first + nvel > static_cast<long long>(count_lines(im.mpaText)))
            co_await print_error("End of file encountered");
        else
            co_await print_error("Too few data points found - File ended too soon");
        co_return 0;
    }
    // v3.51 replaced only the velocities: the X values stay as they were.
    const int testnvel = static_cast<int>(ds.y.size());
    for (int i = 1; i <= testnvel; i++) val[2][i] = static_cast<float>(ds.y[static_cast<size_t>(i - 1)]);
    num_vals = num_per_column[1] = num_per_column[2] = nvel = testnvel;
    loaded("Dataset #" + std::to_string(FILENUM) + " of " + fsu::to_utf8(im.path.filename()) + " (velocities)");
    co_return 1;
}

// ---- printer options ---------------------------------------------------------------

void draw_printer_screen(screen& scr, int* lx, int* ly, int st_num) {
    static const int pos[] = {0, 0, 0, 0, 0, 1, 0, 0};
    const int nstrs = 8;
    static const char ch[] = "FSHDNTRQ";
    static const char* const fmt[] = {"SHORT ", "MEDIUM", "LONG  "};
    static const char* const where[] = {"NONE   ", "PRINTER", "FILE   "};
    static const char* const numb[] = {"OFF       ", "Sequential", "Use File #"};
    static const char* const s[] = {"File format: ",
                                    "Send output to: ",
                                    "Header string: ",
                                    "Date of expt.: ",
                                    "Numbering: ",
                                    "sTarting # for numbering: ",
                                    "Reset all values to original defaults",
                                    "Quit and save"};

    scr.put_str(2, 2, "HARDCOPY OPTIONS:", WHITE);
    for (int i = 0; i < nstrs; i++) {
        lx[i] = scr.put_str(2, 4 + i, s[i], BLACK);
        ly[i] = 4 + i;
        scr.put_char(2 + pos[i], ly[i], static_cast<unsigned char>(ch[i]), WHITE);
    }

    scr.put_str(lx[0], ly[0], fmt[std::clamp(output.format, 0, 2)], WHITE);
    scr.put_str(lx[1], ly[1], where[std::clamp(output.location, 0, 2)], WHITE);
    scr.put_str(lx[2], ly[2], output.header, WHITE);
    scr.put_str(lx[3], ly[3], output.expt_date, WHITE);
    scr.put_str(lx[4], ly[4], numb[std::clamp(output.numbering, 0, 2)], WHITE);
    scr.put_int(lx[5], ly[5], "%i", st_num, WHITE);
    scr.print_screen();
}

// ---- printing -----------------------------------------------------------------------

Task<int> verify_printer() {
    int ans, ch = 0;
    const int abyte = 0;
    static const int pos[] = {0, 0, 0};
    static const char* const s[] = {"YOU indicated that you wanted to print to the",
                                    "printer but it is OFF LINE.  What do you want to do ?", "RETRY the printer",
                                    "TURN OFF hardcopy printing"};
    single_response_window srw(0, 0, 0, 0, s, pos, 2, 2, 2, USER_DEF, RED, YELLOW, WHITE, YELLOW, THICK, YELLOW);

    for (;;) {
        const int bp = biosprint(2, abyte, 0);
        if (!(bp & 0x10) || !(bp & 0x80)) {
            if (!(ans = co_await srw.get_single_response(1, ch))) co_return NO;
            if (ans == 1) {
                single_line_window slw(0, 0, 0, 0, "Check the printer and press any key", MAGENTA, WHITE, THICK, WHITE);
                co_await getch();
                slw.restore_previous_screen();
            } else if (ans == 2) {
                output.location = NONE;
                co_return NO;
            }
        } else {
            co_return YES;
        }
    }
}

// print_short(), print_medium() and print_long(): the shared core's report
// in the chosen format.
Task<std::string> report_text(const kin::Analysis& a, const fs::path& target) {
    const auto& rs = session().report_state();
    kin::ReportOptions opt = rs.options;
    opt.format = static_cast<kin::ReportFormat>(std::clamp(output.format, 0, 2));
    opt.header = output.header;
    opt.experimentDate = output.expt_date;
    opt.numbering = static_cast<kin::Numbering>(std::clamp(output.numbering, 0, 2));
    // v3.51 wrote SHORT and MEDIUM reports comma-separated to files and in
    // columns to the printer.
    opt.layout = output.location == TO_FILE ? kin::ReportLayout::Csv : kin::ReportLayout::Text;

    kin::ReportContext ctx;
    // v3.51 numbered both ways with FILENUM, and added 1 to it for each
    // sequential LONG report, so that an automatic analysis skipped runs
    // (defect 1). The sequence has its own counter now.
    ctx.sequenceNumber = rs.nextNumber;
    ctx.runNumber = session().result_run();
    if (!ctx.runNumber && session().import_state().type != app::ImportType::None) ctx.runNumber = FILENUM;
    ctx.firstInFile = output.newfile == YES || fsu::is_empty_or_missing(target);
    if (output.format == LONG) {
        int day, month, year;
        char s[80];
        get_date GD;
        co_await GD.get_current_date(20, 10, day, month, year, RED, 0);
        GD.date_to_lstring(s, day, month, year);
        ctx.today = s;
    }
    co_return kin::format_report(a, opt, ctx);
}

}  // namespace

// ---- the File menu -------------------------------------------------------------------

Task<int> file_option(int& ch) {
    static const int pos[] = {0, 7, 7, 8, 0, 5, 0};
    static const char* const s[] = {"import Spreadsheet file ([S] & v)",
                                    "import MPA data file (v only)",
                                    "printer Options                    -altO",
                                    "Print to printer/file              -altP",
                                    "send Formfeed character to printer",
                                    "Quit                               -altQ"};
    single_response_window srw(2, 2, 0, 0, s, pos, 0, 6, 1, USER_DEF, GREEN, WHITE, BLACK, WHITE, DOUBLE, WHITE);

    flush_download();
    const int ans = co_await srw.get_single_response(1, ch);
    if (ans == 6) {
        ch = altQ;
        co_return 0;
    }
    if (ans == -1) co_return -1;
    if (ans == 1) co_await import_file_name(DBASE);
    else if (ans == 2) co_await import_file_name(MPA);
    else if (ans == 3) co_await printer_options();
    else if (ans == 4) {
        if (!PARMS_CALCULATED) co_await print_error("Parameters NOT calculated");
        else co_await print_results(YES);
    } else if (ans == 5) {
        write_output(printer_target(), "\f", true, false);  // v3.51: fprintf(stdprn, "\f")
    }
    ch = ESC;
    co_return M_FILE;
}

Task<void> import_data(int ASK) {
    if (ASK) flush_download();
    // v3.51: the data already in the grid count as not entered from here on,
    // even if the import fails.
    DATA_ENTERED = NO;
    const auto type = session().import_state().type;
    if (type == app::ImportType::None) {
        co_await print_error("File type not selected!!");
        co_return;
    }
    if (type == app::ImportType::Spreadsheet) DATA_ENTERED = co_await import_spreadsheet_data(ASK);
    else DATA_ENTERED = co_await import_MPA_data(ASK);
    // An automatic analysis ends when there is no next run.
    if (!ASK && !DATA_ENTERED) flush_download();
}

Task<void> printer_options() {
    int flag, lx[8], ly[8];
    keystroke ks;
    screen scr(10, 6, 71, 19, CYAN, SINGLE, WHITE);
    auto& rs = session().report_state();

    flush_download();
    // v3.51 read OUTPUT.DAT here and wrote it back on Quit, so ESC dropped
    // the changes.
    const output_file saved = output;
    // v3.51: STnum started at 1 each time and was never used (defect 3). It
    // is the session's first sequential number.
    int STnum = rs.options.startNumber;
    bool restart = false;

    for (;;) {
        draw_printer_screen(scr, lx, ly, STnum);
        const int ans = co_await ks.get_letter('d', 't');
        if (ans == ESC) {
            output = saved;
            scr.restore_previous_screen();
            co_return;
        } else if (ans == 'q') {
            break;
        } else if (ans == 'f') {
            output.format = (output.format > 1 ? 0 : output.format + 1);
        } else if (ans == 's') {
            output.location = (output.location > 1 ? 0 : output.location + 1);
        } else if (ans == 'h') {
            flag = 1;
            co_await scr.get_string(lx[2], ly[2], "", output.header, 40, flag, BLACK, WHITE);
        } else if (ans == 'd') {
            flag = 1;
            co_await scr.get_string(lx[3], ly[3], "", output.expt_date, 40, flag, BLACK, WHITE);
        } else if (ans == 'n') {
            output.numbering = (output.numbering > 1 ? 0 : output.numbering + 1);
        } else if (ans == 't') {
            flag = 1;
            if (co_await scr.get_int(lx[5], ly[5], "", STnum, 5, flag, BLACK, WHITE) != ESC) restart = true;
        } else if (ans == 'r') {
            output.format = LONG;
            output.location = TO_PRINTER;
            copy_string(output.header, sizeof output.header, "________________________________________");
            copy_string(output.expt_date, sizeof output.expt_date, output.header);
            output.numbering = SEQUENTIAL;
            STnum = 1;
            restart = true;
            // v3.51 also closed output.fp, a pointer read back from OUTPUT.DAT
            // (defect 7). There is no drive C: in the browser.
            copy_string(output.path, sizeof output.path, session().is_web() ? web_output_dir() : "C:");
            copy_string(output.name, sizeof output.name, "PRINT01.PRN");
        }
    }
    scr.restore_previous_screen();
    rs.options.startNumber = STnum;
    if (restart) rs.nextNumber = STnum;
    save_output();
}

Task<int> get_output_file_name() {
    static const char* const s[] = {"Is this a new file ?", "Yes", "No"};
    static const int loc[] = {0, 0, 0};
    int ch = 0;
    single_response_window srw(0, 0, 0, 0, s, loc, 1, 2, 2, USER_DEF, CYAN, WHITE, BLACK, WHITE, DOUBLE);
    directory dty;

    flush_download();
    const int ans = co_await srw.get_single_response(1, ch);
    if (!ans) co_return 0;
    std::string dap = output_start_dir(), name = output.name;
    if (ans == 1) {
        output.newfile = YES;
        const int found = co_await dty.create_a_file(dap, "PRN", name, 16, 6, 65, 20, CYAN, WHITE, BLACK, YES, NO);
        // v3.51 passed output.path and output.name themselves, and
        // create_a_file() clears the name first: after ESC in the list there
        // is none, and printing then fails.
        //
        // But ESC at "Overwrite existing file ?" leaves the name of that
        // file, and v3.51 went on to print, starting it afresh: the file the
        // user chose not to overwrite was overwritten. Stop there instead.
        if (found <= 0 && !name.empty() && fsu::exists(fsu::from_utf8(dap + fsu::kSep + name))) co_return 0;
        set_output_file(dap, name);
        if (found > 0) {
            if (!kin::write_text_file(output_target(), "", false)) {
                // v3.51 also set output.location = NONE, without saving it.
                co_await print_error("Can't open file");
                co_return 0;
            }
            // A new file starts the sequential numbering again.
            auto& rs = session().report_state();
            rs.nextNumber = rs.options.startNumber;
        }
    } else {
        // v3.51: an Alt key (ans == -1) also comes here.
        output.newfile = NO;
        if (co_await dty.find_a_file(dap, "PRN", name, 16, 6, 65, 20, CYAN, WHITE, BLACK, YES) > 0) {
            set_output_file(dap, name);
            if (!kin::write_text_file(output_target(), "", true)) {
                // v3.51 also set output.location = NONE, without saving it.
                co_await print_error("Can't open/append to file");
                co_return 0;
            }
        }
    }
    save_output();
    co_return 1;
}

Task<void> print_results(int ASK) {
    if (ASK) flush_download();
    // v3.51: with no destination nothing at all happens.
    if (output.location == NONE) co_return;

    fs::path target;
    bool append = true;
    if (output.location == TO_PRINTER) {
        if (!co_await verify_printer()) co_return;
        target = printer_target();
    } else if (output.location == TO_FILE) {
        if (ASK) {
            if (!co_await get_output_file_name()) co_return;
        }
        target = output_target();
        append = output.newfile != YES;
    }

    const kin::Analysis* a = session().result();
    if (!a) {
        co_await print_error("Parameters NOT calculated");
        co_return;
    }
    // v3.51 opened the file, showed "Writing to file" for 0.7 s (or
    // "Printing" for 1 s) and then wrote; here the writing comes first, as it
    // is also the test that the file opens.
    const std::string text = co_await report_text(*a, target);
    if (target.empty() || !write_output(target, text, append, !ASK)) {
        if (output.location == TO_PRINTER) {
            co_await print_error("Can't write the printer file PRINTOUT.PRN");
            co_return;
        }
        co_await print_error("Can't open the destination file??");
        output.location = NONE;
        save_output();
        co_return;
    }
    const bool printer = output.location == TO_PRINTER;
    single_line_window slw(0, 17, 0, 0, printer ? "Printing" : "Writing to file", RED, WHITE, THICK, YELLOW);
    co_await delay(printer ? 1000 : 700);
    slw.restore_previous_screen();

    ++session().report_state().nextNumber;
    output.newfile = NO;
    save_output();
}

}  // namespace classic
