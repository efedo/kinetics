#pragma once
// A saved analysis: the runs' data, the equation, the fitting options and
// the units, in a small text file that a person can read. The results are
// not stored; they are fitted again when the file is opened, from the same
// data and settings.
//
//   Kinetics analysis
//   format = 1
//   equation = a
//   method = 1
//   estimates = 1
//   iteration limit = 0
//   x unit = uM
//   y unit =
//   parameter = Km; fixed = 0; value = 0; min = 0; max = 1e+30
//   parameter = Vmax; fixed = 0; value = 0; min = 0; max = 1e+30
//
//   run = WT
//   fitted = 1
//   0.5, 0.474
//   1, 1.464
//   ...
//
// The equation is v3.51's menu letter; method and estimates use the
// numbers of kinetics.ini. A point's empty cell is left empty.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/analysis.hpp"

namespace kin {

struct ProjectRun {
    std::string name;
    std::vector<Point> points;  // as entered; NaN for an empty cell
    bool fitted = false;        // had results when saved: fit again on opening
};

struct Project {
    ModelId model = ModelId::MichaelisMenten;
    std::vector<ParamSetting> params;  // one per parameter of the equation
    Method method = Method::Marquardt;
    GuessMode guess = GuessMode::Automatic;
    int maxIterations = 0;
    std::string xUnit, yUnit;
    std::vector<ProjectRun> runs;
};

std::string format_project(const Project& project);

struct ProjectParse {
    std::optional<Project> project;
    std::string error;
};
// Settings missing from the file keep their defaults; parameters that do
// not match the equation are replaced by its defaults. A file without runs
// gets one empty run.
ProjectParse parse_project(std::string_view text);

}  // namespace kin
