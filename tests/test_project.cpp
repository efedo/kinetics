// Tests for core/project (saved analyses) and format_run_table (runs saved
// as a spreadsheet): what is written reads back the same.

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "core/import.hpp"
#include "core/project.hpp"

#include "check.hpp"

namespace {

using namespace kin;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

bool same(double a, double b) { return a == b || (std::isnan(a) && std::isnan(b)); }

bool same_points(const std::vector<Point>& a, const std::vector<Point>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (!same(a[i].x, b[i].x) || !same(a[i].y, b[i].y)) return false;
    return true;
}

std::vector<Point> sorted(std::vector<Point> v) {
    std::stable_sort(v.begin(), v.end(), [](const Point& a, const Point& b) { return a.x < b.x; });
    return v;
}

}  // namespace

TEST_CASE(project_round_trip, "project: an analysis reads back as it was saved") {
    Project p;
    p.model = ModelId::HillGt0;
    p.params = default_param_settings(p.model);
    p.params[1].fixed = true;
    p.params[1].value = 1.5;
    p.params[0].max = 123.456;
    p.method = Method::GaussNewton;
    p.guess = GuessMode::Manual;
    p.maxIterations = 75;
    p.xUnit = "\xC2\xB5M";
    p.yUnit = "nmol/min";
    p.runs = {{"WT", {{0.5, 0.474}, {1, 1.464}, {2, kNaN}, {kNaN, 3.0}, {0.1 + 0.2, 1.0 / 3.0}}, true},
              {"Mutant, with = sign", {}, false}};
    const auto text = format_project(p);
    const auto back = parse_project(text);
    CHECK(back.project.has_value());
    if (!back.project) return;
    const auto& q = *back.project;
    CHECK(q.model == ModelId::HillGt0 && q.method == Method::GaussNewton && q.guess == GuessMode::Manual);
    CHECK(q.maxIterations == 75 && q.xUnit == "\xC2\xB5M" && q.yUnit == "nmol/min");
    CHECK(q.params.size() == p.params.size());
    for (size_t j = 0; j < q.params.size() && j < p.params.size(); ++j)
        CHECK(q.params[j].fixed == p.params[j].fixed && q.params[j].value == p.params[j].value &&
              q.params[j].min == p.params[j].min && q.params[j].max == p.params[j].max);
    CHECK(q.runs.size() == 2);
    if (q.runs.size() != 2) return;
    CHECK(q.runs[0].name == "WT" && q.runs[0].fitted && same_points(q.runs[0].points, p.runs[0].points));
    CHECK(q.runs[1].name == "Mutant, with = sign" && !q.runs[1].fitted && q.runs[1].points.empty());
    CHECK(format_project(q) == text);
}

TEST_CASE(project_defaults_and_errors, "project: missing settings, other parameters and bad lines") {
    // Only the title: the defaults and one empty run.
    const auto bare = parse_project("\xEF\xBB\xBFKinetics analysis\r\n");
    CHECK(bare.project && bare.project->model == ModelId::MichaelisMenten && bare.project->runs.size() == 1 &&
          bare.project->runs[0].points.empty());
    // Parameters of another equation are replaced by this one's defaults.
    const auto other = parse_project("Kinetics analysis\nequation = a\nparameter = h; fixed = 1; value = 2\n");
    CHECK(other.project && other.project->params.size() == 2 && !other.project->params[0].fixed &&
          !other.project->params[1].fixed);
    CHECK(parse_project("x,y\n1,2\n").error ==
          "Not a Kinetics analysis file: it should start with the line 'Kinetics analysis'.");
    CHECK(parse_project("Kinetics analysis\nequation = z\n").error == "Unknown equation 'z' (line 2).");
    CHECK(parse_project("Kinetics analysis\nformat = 2\n").error ==
          "This analysis was saved by a newer version of Kinetics (format 2).");
    CHECK(parse_project("Kinetics analysis\nrun = A\n1, 2\none, two\n").error ==
          "Line 4 is not a point (x, y): 'one, two'.");
    // Unknown settings and points before any run are ignored.
    const auto loose = parse_project("Kinetics analysis\ncolour = blue\n1, 2\nrun = A\n3, 4\n");
    CHECK(loose.project && loose.project->runs.size() == 1 &&
          same_points(loose.project->runs[0].points, {{3, 4}}));
}

TEST_CASE(run_table_round_trip, "import: runs saved as a spreadsheet read back, replicates and gaps included") {
    const std::vector<NamedPoints> runs = {
        {"WT", {{1, 1.5}, {0.5, 1.0}, {2, 1.8}, {2, 1.9}}},  // a replicate at 2, out of order
        {"Mutant, 2", {{0.5, 0.5}, {4, 2.5}, {kNaN, 9}, {1, kNaN}}},  // other concentrations; incomplete points
    };
    const auto text = format_run_table(runs, "[S]");
    CHECK(text.rfind("[S],WT,Mutant  2\n", 0) == 0);  // separators in a name become spaces
    const auto back = parse_run_table(text);
    CHECK(back.file.has_value());
    if (!back.file) return;
    CHECK(back.file->runs.size() == 2);
    if (back.file->runs.size() != 2) return;
    CHECK(back.file->runs[0].name == "WT" && back.file->runs[1].name == "Mutant  2");
    CHECK(same_points(back.file->points(back.file->runs[0], true), sorted(runs[0].points)));
    CHECK(same_points(back.file->points(back.file->runs[1], true), {{0.5, 0.5}, {4, 2.5}}));
    // Nothing to write but the headings.
    CHECK(format_run_table({}, "x") == "x\n");
}
