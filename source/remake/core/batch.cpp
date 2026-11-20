#include "core/batch.hpp"

#include <cmath>

namespace kin {
namespace {

BatchItem run_one(int number, std::vector<Point> pts, const AnalysisRequest& tmpl) {
    BatchItem item;
    item.runNumber = number;
    AnalysisRequest req = tmpl;
    req.data = std::move(pts);
    auto outcome = analyse(req);
    if (outcome.analysis) item.analysis = std::move(outcome.analysis);
    else item.error = outcome.error;
    return item;
}

}  // namespace

std::vector<BatchItem> analyse_spreadsheet_runs(const SpreadsheetFile& file, int firstRun,
                                                const AnalysisRequest& tmpl, bool negativeIsMissing) {
    std::vector<BatchItem> out;
    bool started = false;
    for (const auto& run : file.runs) {
        if (!started && run.number != firstRun) continue;
        started = true;
        out.push_back(run_one(run.number, file.points(run, negativeIsMissing), tmpl));
    }
    return out;
}

std::vector<BatchItem> analyse_mpa_datasets(std::string_view text, int firstDataset, int count,
                                            const std::vector<double>& x, const AnalysisRequest& tmpl) {
    std::vector<BatchItem> out;
    const int last = mpa_dataset_count(text, count);
    for (int k = firstDataset; k <= last; ++k) {
        const auto parsed = parse_mpa_dataset(text, k, count);
        if (!parsed.error.empty()) {
            out.push_back({k, std::nullopt, parsed.error});
            break;
        }
        std::vector<Point> pts;
        for (size_t i = 0; i < parsed.y.size(); ++i)
            pts.push_back({i < x.size() ? x[i] : NAN, parsed.y[i]});
        out.push_back(run_one(k, std::move(pts), tmpl));
    }
    return out;
}

}  // namespace kin
