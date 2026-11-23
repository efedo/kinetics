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

// One run of an analysis in the modern interface: its data table and its
// fit. An analysis holds one or more runs, one of them shown at a time.
struct Run {
    std::string name;                       // "Run 1", or the heading it had in a file
    std::vector<DataRow> rows;
    std::string source = "Entered by hand";  // where its data came from
    std::optional<kin::Analysis> analysis;   // its results
    bool stale = false;                      // the equation or settings changed since the fit
    std::string error;                       // why its last fit failed
};

// The classic interface's imported file (v3.51's spreadsheet or MPA file,
// processed run by run). The modern interface reads files into runs.
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
