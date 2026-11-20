#pragma once
// Data files: the two import formats of v3.51 (KIN_FILE.CPP) and a plain
// two-column file for saving and reopening the data table.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/models.hpp"

namespace kin {

// ---- Spreadsheet file (Lotus 1-2-3 / Quattro Pro "print to file") ---------
//
//   S=NS
//   #     S1   S2   S3 ...
//   RN1   v11  v12  v13 ...
//   RN2   v21  v22  v23 ...
//   #
//
// Letters and quotes are ignored, numbers may be separated by spaces, commas
// or tabs. A velocity of -1 (any negative value in v3.51) marks a missing
// measurement.
struct SpreadsheetRun {
    int number = 0;
    std::vector<double> y;  // one per substrate column
};

struct SpreadsheetFile {
    std::vector<double> x;  // substrate concentrations (column headings)
    std::vector<SpreadsheetRun> runs;

    const SpreadsheetRun* find_run(int number) const;
    // Points of a run, skipping missing values. With negativeIsMissing (the
    // v3.51 rule) every negative value is skipped; otherwise only exactly -1,
    // so that negative log10(v) or Y values can be analysed.
    std::vector<Point> points(const SpreadsheetRun& run, bool negativeIsMissing) const;
};

struct SpreadsheetParse {
    std::optional<SpreadsheetFile> file;
    std::string error;
};
SpreadsheetParse parse_spreadsheet(std::string_view text);

// ---- MPA file (MicroPlate Analysis program) -------------------------------
//
// Three header lines, then one "cell, slope" record per line. Dataset k
// (1-based) is the block of `count` records starting at record
// (k - 1) * count. Only velocities are read; X values come from the table.
struct MpaParse {
    std::vector<double> y;
    std::string error;
};
MpaParse parse_mpa_dataset(std::string_view text, int dataset, int count);
int mpa_dataset_count(std::string_view text, int count);

// ---- Two-column data file --------------------------------------------------
// Comma, semicolon, tab or space separated; non-numeric lines (headings) are
// skipped; an empty cell is read as missing (NaN).
std::vector<Point> parse_xy_table(std::string_view text);
std::string format_xy_table(const std::vector<Point>& rows, std::string_view xName,
                            std::string_view yName);

// ---- File helpers -----------------------------------------------------------
std::optional<std::string> read_text_file(const std::filesystem::path& path);
bool write_text_file(const std::filesystem::path& path, std::string_view text, bool append);

}  // namespace kin
