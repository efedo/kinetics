#pragma once
// Session state shared by the modern interface (App) and the classic one:
// the data table, the imported file, batch results and report settings.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/batch.hpp"
#include "core/import.hpp"
#include "core/report.hpp"

namespace app {

// One row of the data table, kept as text so that blanks and partly typed
// numbers behave naturally while editing.
struct DataRow {
    std::string x;
    std::string y;
};

enum class ImportType { None, Spreadsheet, Mpa };

struct ImportState {
    ImportType type = ImportType::None;
    std::filesystem::path path;
    std::optional<kin::SpreadsheetFile> sheet;
    // A table of runs (a spreadsheet saved as CSV or text, rather than in
    // v3.51's print-to-file format): its text, read again when the user says
    // whether the runs are in its columns or its rows.
    bool table = false;
    bool runsInRows = false;
    std::string tableText;
    std::string mpaText;
    int mpaCount = 8;          // velocities per MPA dataset
    int selectedRun = 1;       // spreadsheet run number or MPA dataset number
    bool negativeIsMissing = true;
};

struct BatchState {
    std::vector<kin::BatchItem> items;
    int firstRun = 1;
    bool writeReports = false;
    int selected = -1;         // item shown in the results tabs
    std::string status;
};

struct ReportState {
    kin::ReportOptions options;
    std::filesystem::path file;  // output file for "Write report"
    bool newFile = true;         // next write starts the file afresh
    int nextNumber = 1;          // sequential counter
    std::string status;
};

}  // namespace app
