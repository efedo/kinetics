// Tests for core/batch: "automatic analysis" of every run of a spreadsheet
// file and of consecutive MPA datasets. The runs are exact Michaelis-Menten
// curves with Vmax 6 and Km 2 or 3 at [S] = 1, 2, 4, 8:
//   Km 2: 6x/(2+x) = 2, 3, 4, 4.8
//   Km 3: 6x/(3+x) = 1.5, 2.4, 24/7 = 3.428571429, 48/11 = 4.363636364

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/analysis.hpp"
#include "core/batch.hpp"
#include "core/import.hpp"
#include "core/models.hpp"

#include "check.hpp"
#include "helpers.hpp"

namespace {

using namespace kin;

const char* const kSheet =
    "Batch test\n"
    "S=4\n"
    "#   1     2     4            8\n"
    "3   2     3     4            4.8\n"
    "5   1.5   2.4   3.428571429  4.363636364\n"
    "9   -1    -1    -1           -1\n"
    "11  -1    -1    -0.5         4\n"
    "#\n";

const char* const kPlate =
    "MPA\n"
    "plate\n"
    "cell, slope\n"
    "1, 2\n2, 3\n3, 4\n4, 4.8\n"                                  // dataset 1: Km 2
    "5, 1.5\n6, 2.4\n7, 3.428571429\n8, 4.363636364\n"            // dataset 2: Km 3
    "9, 1\n10, oops\n11, 3\n12, 4\n"                              // dataset 3: line 13 is broken
    "13, 2\n14, 3\n15, 4\n16, 4.8\n";                             // dataset 4: never reached

SpreadsheetFile sheet() {
    auto p = parse_spreadsheet(kSheet);
    if (!p.file) {
        std::printf("spreadsheet did not parse: %s\n", p.error.c_str());
        std::abort();
    }
    return *p.file;
}

AnalysisRequest mm_template() {
    auto t = test::request(ModelId::MichaelisMenten, {{100, 100}});  // template data are replaced
    return t;
}

void check_mm(const BatchItem& item, int run, double km, const char* file, int line) {
    ::test::check(item.runNumber == run, "run number", file, line, std::to_string(item.runNumber));
    ::test::check(item.analysis.has_value() && item.error.empty(), "analysed", file, line, item.error);
    if (!item.analysis) return;
    ::test::check(std::fabs(item.analysis->fit.p[0] - km) < 1e-6 * km, "Km", file, line,
                  fmt_g(item.analysis->fit.p[0], 10));
    ::test::check(std::fabs(item.analysis->fit.p[1] - 6.0) < 1e-6 * 6.0, "Vmax", file, line,
                  fmt_g(item.analysis->fit.p[1], 10));
}
#define CHECK_MM(item, run, km) check_mm((item), (run), (km), __FILE__, __LINE__)

}  // namespace

TEST_CASE(batch_spreadsheet_all_runs, "batch: every spreadsheet run from the first one, errors kept per run") {
    const auto f = sheet();
    const auto items = analyse_spreadsheet_runs(f, 3, mm_template(), true);
    CHECK(items.size() == 4);
    if (items.size() != 4) return;
    CHECK_MM(items[0], 3, 2.0);
    CHECK_MM(items[1], 5, 3.0);
    CHECK(items[0].analysis && items[0].analysis->data.size() == 4);
    // Run 9 has no values; run 11 keeps only [S] = 8 (negatives are missing).
    CHECK(items[2].runNumber == 9 && !items[2].analysis && items[2].error == "No data entered.");
    CHECK(items[3].runNumber == 11 && !items[3].analysis &&
          items[3].error == "1 data points cannot determine 2 parameters.");
}

TEST_CASE(batch_spreadsheet_first_run, "batch: starting part-way, or at a run that does not exist") {
    const auto f = sheet();
    const auto from5 = analyse_spreadsheet_runs(f, 5, mm_template(), true);
    CHECK(from5.size() == 3 && from5[0].runNumber == 5 && from5[1].runNumber == 9 && from5[2].runNumber == 11);
    CHECK(analyse_spreadsheet_runs(f, 4, mm_template(), true).empty());
    CHECK(analyse_spreadsheet_runs(SpreadsheetFile{}, 1, mm_template(), true).empty());
}

TEST_CASE(batch_spreadsheet_negatives, "batch: negative values other than -1 are kept when not treated as missing") {
    const auto items = analyse_spreadsheet_runs(sheet(), 11, test::request(ModelId::Linear, {}), false);
    CHECK(items.size() == 1);
    if (items.size() != 1) return;
    // Run 11 keeps (4, -0.5) and (8, 4): too few for a regression.
    CHECK(items[0].error == "Linear regression needs at least 3 data pairs.");
    // With the template's fixed intercept of -5 the two points suffice:
    // slope = sum(x (y + 5)) / sum(x^2) = (4*4.5 + 8*9) / 80 = 1.125.
    auto tmpl = test::request(ModelId::Linear, {});
    tmpl.params[0].fixed = true;
    tmpl.params[0].value = -5.0;
    const auto fixed = analyse_spreadsheet_runs(sheet(), 11, tmpl, false);
    CHECK(fixed.size() == 1 && fixed[0].analysis);
    if (fixed.size() == 1 && fixed[0].analysis) {
        CHECK(fixed[0].analysis->data.size() == 2);
        CHECK_NEAR(fixed[0].analysis->fit.p[1], 1.125, 1e-12);
        CHECK(fixed[0].analysis->description == "Linear regression/FIXED intercept");
    }
}

TEST_CASE(batch_template_settings, "batch: the template's settings apply to every run") {
    auto tmpl = mm_template();
    tmpl.params[1].fixed = true;  // Vmax fixed at 6
    tmpl.params[1].value = 6.0;
    tmpl.method = Method::GaussNewton;
    const auto items = analyse_spreadsheet_runs(sheet(), 3, tmpl, true);
    CHECK(items.size() == 4);
    if (items.size() < 2) return;
    for (int i = 0; i < 2; ++i) {
        const auto& a = items[static_cast<size_t>(i)].analysis;
        CHECK(a && a->fit.fixed[1] && a->fit.p[1] == 6.0 && a->fit.method == Method::GaussNewton);
        CHECK(a && a->description == "Michaelis-Menten/FIXED Vmax");
    }
    CHECK_MM(items[1], 5, 3.0);
}

TEST_CASE(batch_mpa, "batch: MPA datasets until the end of the file or the first broken record") {
    const std::vector<double> x = {1, 2, 4, 8};
    const auto items = analyse_mpa_datasets(kPlate, 1, 4, x, mm_template());
    CHECK(items.size() == 3);
    if (items.size() != 3) return;
    CHECK_MM(items[0], 1, 2.0);
    CHECK_MM(items[1], 2, 3.0);
    CHECK(items[2].runNumber == 3 && !items[2].analysis &&
          items[2].error == "Line 13 has no 'cell, velocity' record: dataset 3 has only 1 of 4 velocities.");

    const auto from2 = analyse_mpa_datasets(kPlate, 2, 4, x, mm_template());
    CHECK(from2.size() == 2 && from2[0].runNumber == 2 && from2[1].runNumber == 3);
    CHECK(analyse_mpa_datasets(kPlate, 5, 4, x, mm_template()).empty());  // past the last dataset
    CHECK(analyse_mpa_datasets(kPlate, 1, 0, x, mm_template()).empty());  // no datasets of 0 records

    // Dataset 0 is refused by the parser, which ends the batch.
    const auto zero = analyse_mpa_datasets(kPlate, 0, 4, x, mm_template());
    CHECK(zero.size() == 1 && zero[0].runNumber == 0 &&
          zero[0].error == "The dataset number and the number of velocities must be at least 1.");

    // The last, partial dataset is not attempted: 6 records hold one dataset of 4.
    const auto partial = analyse_mpa_datasets("a\nb\nc\n1, 2\n2, 3\n3, 4\n4, 4.8\n5, 1\n6, 2\n", 1, 4, x,
                                              mm_template());
    CHECK(partial.size() == 1);
    if (!partial.empty()) CHECK_MM(partial[0], 1, 2.0);
}

TEST_CASE(batch_mpa_short_x, "batch: velocities without an X value are dropped") {
    // Only three X values: the fourth velocity of each dataset has no [S].
    const auto items = analyse_mpa_datasets(kPlate, 1, 4, {1, 2, 4}, mm_template());
    CHECK(items.size() == 3);
    if (items.size() != 3) return;
    CHECK_MM(items[0], 1, 2.0);
    CHECK(items[0].analysis && items[0].analysis->data.size() == 3);
    // No X values at all: every dataset is empty, but the batch goes on.
    const auto none = analyse_mpa_datasets(kPlate, 1, 4, {}, mm_template());
    CHECK(none.size() == 3);
    if (none.size() == 3) {
        CHECK(none[0].error == "No data entered." && none[1].error == "No data entered.");
        CHECK(none[2].error.rfind("Line 13 ", 0) == 0);
    }
}
