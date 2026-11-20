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

struct StdCurveEntry {
    bool xFromY = true;
    double input = 0.0;
    std::optional<double> output;
    bool outOfRange = false;
};

}  // namespace app
