#pragma once
// "Automatic analysis" of v3.51: fit every run of an imported file in turn.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/analysis.hpp"
#include "core/import.hpp"

namespace kin {

struct BatchItem {
    int runNumber = 0;  // spreadsheet run number, or MPA dataset number
    std::optional<Analysis> analysis;
    std::string error;
};

// Runs in file order, starting with the run numbered `firstRun`. (v3.51
// shared one counter between this loop and the report numbering, so with
// sequential numbering it skipped every other run.)
std::vector<BatchItem> analyse_spreadsheet_runs(const SpreadsheetFile& file, int firstRun,
                                                const AnalysisRequest& tmpl, bool negativeIsMissing);

// Consecutive MPA datasets of `count` velocities from `firstDataset` to the
// end of the file, each paired with the X values `x`.
std::vector<BatchItem> analyse_mpa_datasets(std::string_view text, int firstDataset, int count,
                                            const std::vector<double>& x, const AnalysisRequest& tmpl);

}  // namespace kin
