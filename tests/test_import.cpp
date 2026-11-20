// Tests for core/import: spreadsheet ("S=n" print-to-file) and MPA files,
// the two-column data table, and the file helpers. Every file is written by
// hand here, including malformed ones, so the expected values are known.

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#include "core/import.hpp"
#include "core/models.hpp"

#include "check.hpp"
#include "helpers.hpp"

namespace {

using namespace kin;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

const std::string kBadFormat =
    "Incorrect file format: expected 'S=<number of columns>' followed by '#' and the substrate values.";

std::string spreadsheet_error(const std::string& text) {
    const auto p = parse_spreadsheet(text);
    return p.file ? std::string("(parsed)") : p.error;
}

bool same(double a, double b) { return (std::isnan(a) && std::isnan(b)) || a == b; }

bool same_points(const std::vector<Point>& got, const std::vector<Point>& want) {
    if (got.size() != want.size()) return false;
    for (size_t i = 0; i < got.size(); ++i)
        if (!same(got[i].x, want[i].x) || !same(got[i].y, want[i].y)) return false;
    return true;
}

// Scratch folder relative to the working directory (the build folder under
// ctest); removed again by each test that uses it.
const std::filesystem::path kTmp = std::filesystem::path("test_tmp") / "import_c";

void clean_tmp() {
    std::error_code ec;
    std::filesystem::remove_all(kTmp, ec);
    // Remove test_tmp too if nothing else is in it (remove_all, because a
    // plain remove of a directory fails under Node.js on Windows).
    if (std::filesystem::is_empty(kTmp.parent_path(), ec) && !ec) std::filesystem::remove_all(kTmp.parent_path(), ec);
}

// Three header lines, then "cell, slope" records.
const std::string kMpa =
    "MicroPlate Analysis\r\n"
    "Plate 7\r\n"
    "cell, slope\r\n"
    "1, 10.5\r\n"
    "2, 9.25\r\n"
    "3,8\r\n"
    "4,\t7.125 mOD/min\r\n"
    "5, 6.5\r\n"
    "6, .75\r\n"
    "7, 5\r\n";

}  // namespace

// ---------------------------------------------------------------------------
// Spreadsheet files

TEST_CASE(import_spreadsheet_layout, "import: spreadsheet with quotes, commas, tabs, CRLF and letters") {
    const std::string text =
        "\"Kinetics export\"\r\n"
        "\"S=3\"\r\n"
        "\"#\",0.5,\t1,2\r\n"
        "RN7\t.5, 0.8 ,9e-1\r\n"
        "\"run 8\",-1,x1.25,-0.5\r\n"
        "#\r\n"
        "99 1 2 3\r\n";  // after the closing '#': ignored
    const auto p = parse_spreadsheet(text);
    CHECK(p.file.has_value() && p.error.empty());
    if (!p.file) return;
    const auto& f = *p.file;
    CHECK(f.x == std::vector<double>({0.5, 1, 2}));
    CHECK(f.runs.size() == 2);
    if (f.runs.size() != 2) return;
    CHECK(f.runs[0].number == 7 && f.runs[0].y == std::vector<double>({0.5, 0.8, 0.9}));
    CHECK(f.runs[1].number == 8 && f.runs[1].y == std::vector<double>({-1, 1.25, -0.5}));
    CHECK(f.find_run(7) == &f.runs[0]);
    CHECK(f.find_run(8) == &f.runs[1]);
    CHECK(f.find_run(99) == nullptr);
    // v3.51: any negative value is missing; otherwise only -1 is.
    CHECK(same_points(f.points(f.runs[1], true), {{1, 1.25}}));
    CHECK(same_points(f.points(f.runs[1], false), {{1, 1.25}, {2, -0.5}}));
    CHECK(same_points(f.points(f.runs[0], true), {{0.5, 0.5}, {1, 0.8}, {2, 0.9}}));
}

TEST_CASE(import_spreadsheet_hash_ends_number, "import: spreadsheet numbers end at '#'") {
    const auto p = parse_spreadsheet("s=2\n#1 2\n9 0.1 0.2#");
    CHECK(p.file && p.file->x == std::vector<double>({1, 2}));
    CHECK(p.file && p.file->runs.size() == 1 && p.file->runs[0].number == 9 &&
          p.file->runs[0].y == std::vector<double>({0.1, 0.2}));
}

TEST_CASE(import_spreadsheet_example, "import: the spreadsheet example file") {
    const auto text = read_text_file(test::kExamples / "spreadsheet_runs.txt");
    CHECK(text.has_value());
    if (!text) return;
    const auto p = parse_spreadsheet(*text);
    CHECK(p.file.has_value());
    if (!p.file) return;
    CHECK(p.file->x == std::vector<double>({0.5, 1, 2, 3, 4, 6, 8, 12}));
    CHECK(p.file->runs.size() == 4);
    if (p.file->runs.size() != 4) return;
    for (int i = 0; i < 4; ++i) CHECK(p.file->runs[static_cast<size_t>(i)].number == i + 1);
    CHECK(p.file->runs[0].y == std::vector<double>({0.474, 1.464, 3.862, 5.902, 7.492, 9.122, 10.080, 11.354}));
    CHECK(p.file->runs[3].y.back() == 10.031);
    const auto run3 = p.file->points(*p.file->find_run(3), true);
    CHECK(run3.size() == 7 && run3[1].x == 1 && run3[2].x == 3 && run3[2].y == 5.913);
}

TEST_CASE(import_spreadsheet_errors, "import: spreadsheet format errors") {
    CHECK(spreadsheet_error("") == kBadFormat);
    CHECK(spreadsheet_error("no header here") == kBadFormat);      // no 's'
    CHECK(spreadsheet_error("Results 3\n# 1 2 3\n") == kBadFormat);  // 's' but no '='
    CHECK(spreadsheet_error("S=") == kBadFormat);                  // no column count
    CHECK(spreadsheet_error("S=#\n1 2") == kBadFormat);
    CHECK(spreadsheet_error("S=0\n#\n1\n#") == kBadFormat);
    CHECK(spreadsheet_error("S=-2\n# 1 2\n1 3 4\n#") == kBadFormat);
    CHECK(spreadsheet_error("S=2\n1 2\n3 4 5\n") == kBadFormat);  // no '#'
    CHECK(spreadsheet_error("S=3\n# 1 2\n") == "The file declares S=3 but lists only 2 substrate values.");
    CHECK(spreadsheet_error("S=3\n# 1 # 2 3\n") == "The file declares S=3 but lists only 1 substrate values.");
    CHECK(spreadsheet_error("S=2\n# 1 2\n#\n") == "The file contains no runs.");
    CHECK(spreadsheet_error("S=2\n# 1 2\n") == "The file contains no runs.");
    CHECK(spreadsheet_error("S=3\n# 1 2 3\n7 0.5 0.6\n") == "Run #7 has fewer than 3 values.");
    CHECK(spreadsheet_error("S=3\n# 1 2 3\n7 0.5 0.6 0.7\n8 0.5 #\n") == "Run #8 has fewer than 3 values.");
}

TEST_CASE(import_spreadsheet_points, "import: run points stop at the shorter of the X and Y lists") {
    SpreadsheetFile f;
    f.x = {1, 2, 3};
    SpreadsheetRun shortRun{1, {0.5, -1}};
    SpreadsheetRun longRun{2, {0.5, 0.6, 0.7, 0.8}};
    CHECK(same_points(f.points(shortRun, false), {{1, 0.5}}));
    CHECK(same_points(f.points(longRun, true), {{1, 0.5}, {2, 0.6}, {3, 0.7}}));
    CHECK(f.find_run(1) == nullptr);  // runs not added to the file
}

// ---------------------------------------------------------------------------
// MPA files

TEST_CASE(import_mpa_datasets, "import: MPA datasets are consecutive blocks of records") {
    CHECK(mpa_dataset_count(kMpa, 3) == 2);  // 7 records: two full blocks of 3
    CHECK(mpa_dataset_count(kMpa, 7) == 1);
    CHECK(mpa_dataset_count(kMpa, 8) == 0);
    CHECK(mpa_dataset_count(kMpa, 1) == 7);
    CHECK(mpa_dataset_count(kMpa, 0) == 0);
    CHECK(mpa_dataset_count("a\nb\nc\n", 1) == 0);  // header only
    CHECK(mpa_dataset_count("a\nb\n", 1) == 0);
    CHECK(mpa_dataset_count("", 1) == 0);
    // A missing final line break does not change the count; a blank last line is dropped.
    CHECK(mpa_dataset_count("a\nb\nc\n1, 2\n2, 3", 1) == 2);
    CHECK(mpa_dataset_count("a\nb\nc\n1, 2\n2, 3\n", 1) == 2);

    const auto d1 = parse_mpa_dataset(kMpa, 1, 3);
    CHECK(d1.error.empty() && d1.y == std::vector<double>({10.5, 9.25, 8}));
    const auto d2 = parse_mpa_dataset(kMpa, 2, 3);
    CHECK(d2.error.empty() && d2.y == std::vector<double>({7.125, 6.5, 0.75}));
    const auto all = parse_mpa_dataset(kMpa, 1, 7);
    CHECK(all.error.empty() && all.y.size() == 7 && all.y[6] == 5);
}

TEST_CASE(import_mpa_errors, "import: MPA errors name the line and the count read") {
    CHECK(parse_mpa_dataset(kMpa, 0, 3).error == "The dataset number and the number of velocities must be at least 1.");
    CHECK(parse_mpa_dataset(kMpa, 1, 0).error == "The dataset number and the number of velocities must be at least 1.");
    auto e3 = parse_mpa_dataset(kMpa, 3, 3);
    CHECK(e3.error == "End of file: dataset 3 has only 1 of 3 velocities." && e3.y.empty());
    CHECK(parse_mpa_dataset(kMpa, 4, 3).error == "End of file: dataset 4 has only 0 of 3 velocities.");

    const std::string head = "h1\nh2\nh3\n1, 4\n";
    const std::string tail = "3, 2\n";
    // Line 5 (the second record) in several broken forms.
    for (const char* bad : {"2 3.5\n", "2, n/a\n", "2, #\n", "2, -0.5\n", "\n"}) {
        const auto r = parse_mpa_dataset(head + bad + tail, 1, 3);
        CHECK(r.error == "Line 5 has no 'cell, velocity' record: dataset 1 has only 1 of 3 velocities.");
        CHECK(r.y.empty());
    }
    // Zero is a valid velocity.
    const auto zero = parse_mpa_dataset(head + "2, 0\n" + tail, 1, 3);
    CHECK(zero.error.empty() && zero.y == std::vector<double>({4, 0, 2}));
}

TEST_CASE(import_mpa_example, "import: the MPA example file") {
    const auto text = read_text_file(test::kExamples / "mpa_plate.txt");
    CHECK(text.has_value());
    if (!text) return;
    CHECK(mpa_dataset_count(*text, 8) == 2);
    const auto d1 = parse_mpa_dataset(*text, 1, 8);
    CHECK(d1.error.empty() &&
          d1.y == std::vector<double>({19.901, 17.738, 15.116, 13.420, 9.748, 6.761, 4.364, 2.220}));
    const auto d2 = parse_mpa_dataset(*text, 2, 8);
    CHECK(d2.error.empty() && d2.y.size() == 8 && d2.y[7] == 2.836);
}

// ---------------------------------------------------------------------------
// Two-column tables

TEST_CASE(import_xy_table, "import: two-column table separators, headings and blank cells") {
    const std::string text =
        "\"[S]\",\"v\"\r\n"  // heading
        "1,2\r\n"
        "\"3\" , \"4.5\"\n"   // quoted, spaces around
        "5;6\n"
        "7\t8\n"
        "9 10\n"
        "  11   12  \n"       // runs of spaces
        "13,\n"               // missing v
        ",14\n"               // missing [S]
        ",\n"                 // both missing: skipped
        "\"\",15\n"           // quoted blank [S]
        "  ;15.5\n"            // blank (spaces) [S]
        " x ;15.75\n"          // text: skipped
        "16\n"                // one cell: skipped
        "abc,17\n"            // text: skipped
        "18,xyz\n"            // text: skipped
        "1e400,19\n"          // not finite: skipped
        "20.5abc,21\n"        // trailing junk: skipped
        "22,23,24\n"          // third column ignored
        "2.5e-1,-0.5\n"
        "\n"
        "   \n";
    const std::vector<Point> want = {{1, 2},       {3, 4.5},    {5, 6},  {7, 8},  {9, 10},   {11, 12},
                                     {13, kNaN},  {kNaN, 14},  {kNaN, 15}, {kNaN, 15.5}, {22, 23}, {0.25, -0.5}};
    const auto rows = parse_xy_table(text);
    CHECK(same_points(rows, want));
    CHECK(parse_xy_table("").empty());
    CHECK(parse_xy_table("x y\nnone here\n").empty());
}

TEST_CASE(import_xy_format, "import: format_xy_table writes quoted headings, 10 digits and blank cells") {
    const std::vector<Point> rows = {{1, 2}, {0.1, kNaN}, {kNaN, 3}, {1.23456789012345, -4e-7}, {kInf, 5}, {kNaN, kNaN}};
    const std::string text = format_xy_table(rows, "[S]", "v");
    CHECK(text == "\"[S]\",\"v\"\n1,2\n0.1,\n,3\n1.23456789,-4e-07\n,5\n,\n");
    // Reading it back gives the values to 10 digits; the all-blank row is dropped.
    CHECK(same_points(parse_xy_table(text), {{1, 2}, {0.1, kNaN}, {kNaN, 3}, {1.23456789, -4e-7}, {kNaN, 5}}));
    CHECK(format_xy_table({}, "X", "Y") == "\"X\",\"Y\"\n");
}

// ---------------------------------------------------------------------------
// File helpers

TEST_CASE(import_file_helpers, "import: reading, writing and appending text files") {
    clean_tmp();
    std::error_code ec;
    std::filesystem::create_directories(kTmp, ec);
    CHECK(!ec);
    const auto file = kTmp / "data.txt";
    CHECK(!read_text_file(file).has_value());  // not there yet

    CHECK(write_text_file(file, "line 1\r\nline 2\n", false));
    CHECK(read_text_file(file) == std::optional<std::string>("line 1\r\nline 2\n"));  // bytes kept
    CHECK(write_text_file(file, "more", true));
    CHECK(read_text_file(file) == std::optional<std::string>("line 1\r\nline 2\nmore"));
    CHECK(write_text_file(file, "new", false));
    CHECK(read_text_file(file) == std::optional<std::string>("new"));
    CHECK(write_text_file(file, "", false));
    CHECK(read_text_file(file) == std::optional<std::string>(""));

    CHECK(!write_text_file(kTmp / "missing_folder" / "x.txt", "x", false));
    CHECK(!read_text_file(kTmp / "missing_folder" / "x.txt").has_value());

    // A table written and read back through the file helpers.
    const std::vector<Point> rows = {{0.5, 1.25}, {2, kNaN}};
    CHECK(write_text_file(kTmp / "table.csv", format_xy_table(rows, "X", "Y"), false));
    const auto back = read_text_file(kTmp / "table.csv");
    CHECK(back && same_points(parse_xy_table(*back), rows));
    clean_tmp();
    CHECK(!std::filesystem::exists(kTmp, ec));
}

// Numbers too large for an int used to be cast regardless (undefined
// behaviour, found while writing these tests; now rejected).
TEST_CASE(import_spreadsheet_huge_numbers, "import: spreadsheet column count or run number too large") {
    CHECK(spreadsheet_error("S=1e10\n# 1 2\n1 0.1 0.2\n#") == kBadFormat);
    CHECK(spreadsheet_error("S=nan\n# 1 2\n1 0.1 0.2\n#") == kBadFormat);
    CHECK(spreadsheet_error("S=2\n# 1 2\n1e10 0.1 0.2\n#") == "A run number in the file is too large.");
    CHECK(spreadsheet_error("S=2\n# 1 2\n-1e10 0.1 0.2\n#") == "A run number in the file is too large.");
    // The largest accepted run numbers still parse.
    const auto ok = parse_spreadsheet("S=2\n# 1 2\n999999999 0.1 0.2\n#");
    CHECK(ok.error.empty() && ok.file && ok.file->runs.size() == 1 && ok.file->runs[0].number == 999999999);
}
