#pragma once
// Report output (KIN_FILE.CPP print_short / print_medium / print_long and the
// "printer options"). Reports are written to files; the printer port of
// v3.51 is replaced by text files that any editor can print.

#include <optional>
#include <string>

#include "core/analysis.hpp"

namespace kin {

enum class ReportFormat { Short = 0, Medium = 1, Long = 2 };

// SHORT and MEDIUM reports come in two layouts, as in v3.51: comma-separated
// with quoted headings (for spreadsheets; v3.51's file output) or fixed-width
// columns (v3.51's printer output). LONG is always text.
enum class ReportLayout { Csv = 0, Text = 1 };

enum class Numbering { Off = 0, Sequential = 1, RunNumber = 2 };

struct ReportOptions {
    ReportFormat format = ReportFormat::Long;
    ReportLayout layout = ReportLayout::Csv;
    std::string header;       // "Experiment type" line
    std::string experimentDate;
    Numbering numbering = Numbering::Sequential;
    int startNumber = 1;      // first sequential number
};

struct ReportContext {
    std::optional<int> sequenceNumber;  // used with Numbering::Sequential
    std::optional<int> runNumber;       // file run / dataset, if the data came from a file
    bool firstInFile = true;            // SHORT/MEDIUM write column headings first
    std::string today;                  // e.g. "Friday, November 6, 2026"
};

// The number shown for a report: sequential counter, run number, or none.
std::optional<int> report_number(const ReportOptions& opt, const ReportContext& ctx);

std::string format_report(const Analysis& a, const ReportOptions& opt, const ReportContext& ctx);

std::string today_string();

std::string report_format_name(ReportFormat f);
std::string numbering_name(Numbering n);

}  // namespace kin
