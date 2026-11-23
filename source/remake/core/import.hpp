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
    std::vector<double> y;  // one per substrate column; NaN for an empty cell
    std::string name;       // the run's heading in a table of runs, if any
};

struct SpreadsheetFile {
    std::vector<double> x;  // substrate concentrations (column headings)
    std::vector<SpreadsheetRun> runs;

    const SpreadsheetRun* find_run(int number) const;
    // Points of a run, skipping missing values: empty cells, and with
    // negativeIsMissing (the v3.51 rule) every negative value; otherwise only
    // exactly -1, so that negative log10(v) or Y values can be analysed.
    std::vector<Point> points(const SpreadsheetRun& run, bool negativeIsMissing) const;
};

struct SpreadsheetParse {
    std::optional<SpreadsheetFile> file;
    std::string error;
};
SpreadsheetParse parse_spreadsheet(std::string_view text);

// ---- Table of runs (a spreadsheet saved as CSV or text) -------------------
//
//   [S],  Run 1,  Run 2, ...
//   0.5,  0.474,  0.727
//   1,    1.464,  1.870
//   ...
//
// One run per column: the first column holds the concentrations and each
// further column a run's velocities. With runsInRows the table is read the
// other way round: the first row holds the concentrations and each further
// row a run, its name in the first cell. Cells are separated as in
// parse_xy_table. A heading row (column) names the runs, which are numbered
// 1, 2, ... in order; a column (row) without a number is left out. An empty
// or non-numeric cell is a missing value, and so is -1.
SpreadsheetParse parse_run_table(std::string_view text, bool runsInRows = false);

// Whether the text is in v3.51's spreadsheet format (a line "S=n"), rather
// than a table of runs.
bool is_v351_spreadsheet(std::string_view text);

// A table of runs, as parse_run_table reads it, for saving as a CSV file: a
// heading row with the runs' names, then the concentrations in the first
// column and each run's values in its own column. Runs measured at
// different concentrations, or with replicates, get as many rows for a
// concentration as the run with most points there; the other cells are
// left empty. Points without both numbers are left out.
struct NamedPoints {
    std::string name;
    std::vector<Point> points;
};
std::string format_run_table(const std::vector<NamedPoints>& runs, std::string_view xHeading);

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
