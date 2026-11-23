#include "core/project.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace kin {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr const char* kTitle = "Kinetics analysis";

// Every digit, so that the values read back are the values saved.
std::string number(double v) {
    if (std::isnan(v)) return {};
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return s;
}

// A name or unit on one line, as the format needs.
std::string one_line(const std::string& s) {
    std::string out;
    for (char c : s) out += (c == '\n' || c == '\r') ? ' ' : c;
    return out;
}

std::optional<double> to_number(std::string_view s) {
    s = trim(s);
    if (s.empty()) return std::nullopt;
    const std::string tmp(s);
    char* end = nullptr;
    const double v = std::strtod(tmp.c_str(), &end);
    if (end == tmp.c_str() || *end != '\0') return std::nullopt;
    return v;
}

std::optional<int> to_int(std::string_view s) {
    const auto v = to_number(s);
    if (!v || !(std::fabs(*v) < 1e9)) return std::nullopt;
    return static_cast<int>(*v);
}

// "key = value", or nothing for a line without '='.
bool split_key(std::string_view line, std::string& key, std::string_view& value) {
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) return false;
    key.clear();
    for (char c : trim(line.substr(0, eq))) key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    value = trim(line.substr(eq + 1));
    return true;
}

}  // namespace

std::string format_project(const Project& p) {
    const auto& def = model_def(p.model);
    std::string s = std::string(kTitle) + "\nformat = 1\n";
    s += "equation = " + std::string(1, def.letter) + "\n";
    s += "method = " + std::to_string(static_cast<int>(p.method)) + "\n";
    s += "estimates = " + std::to_string(static_cast<int>(p.guess)) + "\n";
    s += "iteration limit = " + std::to_string(p.maxIterations) + "\n";
    s += "x unit = " + one_line(p.xUnit) + "\n";
    s += "y unit = " + one_line(p.yUnit) + "\n";
    for (size_t j = 0; j < p.params.size() && j < def.params.size(); ++j) {
        const auto& q = p.params[j];
        s += "parameter = " + std::string(def.params[j].name) + "; fixed = " + (q.fixed ? "1" : "0") +
             "; value = " + number(q.value) + "; min = " + number(q.min) + "; max = " + number(q.max) + "\n";
    }
    for (const auto& r : p.runs) {
        s += "\nrun = " + one_line(r.name) + "\n";
        s += std::string("fitted = ") + (r.fitted ? "1" : "0") + "\n";
        for (const auto& pt : r.points) s += number(pt.x) + ", " + number(pt.y) + "\n";
    }
    return s;
}

ProjectParse parse_project(std::string_view text) {
    ProjectParse out;
    // The lines, without a byte-order mark or carriage returns.
    if (text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    std::vector<std::string_view> lines;
    for (size_t start = 0; start < text.size();) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        start = end + 1;
    }
    size_t i = 0;
    while (i < lines.size() && trim(lines[i]).empty()) ++i;
    if (i >= lines.size() || trim(lines[i]) != kTitle) {
        out.error = "Not a Kinetics analysis file: it should start with the line 'Kinetics analysis'.";
        return out;
    }
    ++i;

    Project p;
    std::vector<std::pair<std::string, ParamSetting>> params;
    ProjectRun* run = nullptr;
    for (; i < lines.size(); ++i) {
        const auto line = trim(lines[i]);
        if (line.empty() || line.front() == '#') continue;
        std::string key;
        std::string_view value;
        if (split_key(line, key, value)) {
            if (key == "run") {
                p.runs.push_back(ProjectRun{std::string(value), {}, false});
                run = &p.runs.back();
            } else if (run) {
                if (key == "fitted") run->fitted = to_int(value).value_or(0) != 0;
            } else if (key == "format") {
                if (to_int(value).value_or(0) > 1) {
                    out.error = "This analysis was saved by a newer version of Kinetics (format " + std::string(value) + ").";
                    return out;
                }
            } else if (key == "equation") {
                if (value.empty() || !model_from_letter(value[0])) {
                    out.error = "Unknown equation '" + std::string(value) + "' (line " + std::to_string(i + 1) + ").";
                    return out;
                }
                p.model = *model_from_letter(value[0]);
            } else if (key == "method") {
                p.method = to_int(value).value_or(1) == 2 ? Method::GaussNewton : Method::Marquardt;
            } else if (key == "estimates") {
                p.guess = to_int(value).value_or(1) == 2 ? GuessMode::Manual : GuessMode::Automatic;
            } else if (key == "iteration limit") {
                p.maxIterations = std::max(0, to_int(value).value_or(0));
            } else if (key == "x unit") {
                p.xUnit = std::string(value);
            } else if (key == "y unit") {
                p.yUnit = std::string(value);
            } else if (key == "parameter") {
                // "Km; fixed = 0; value = 0; min = 0; max = 1e+30"
                std::string name;
                ParamSetting q;
                size_t start = 0;
                bool first = true;
                while (start <= value.size()) {
                    size_t semi = value.find(';', start);
                    if (semi == std::string_view::npos) semi = value.size();
                    const auto field = trim(value.substr(start, semi - start));
                    if (first) {
                        name = std::string(field);
                        first = false;
                    } else {
                        std::string k;
                        std::string_view v;
                        if (split_key(field, k, v)) {
                            const auto num = to_number(v);
                            if (k == "fixed") q.fixed = num.value_or(0.0) != 0.0;
                            else if (k == "value") q.value = num.value_or(0.0);
                            else if (k == "min") q.min = num.value_or(0.0);
                            else if (k == "max") q.max = num.value_or(0.0);
                        }
                    }
                    start = semi + 1;
                }
                params.emplace_back(name, q);
            }
            continue;  // other settings are for later versions
        }
        if (!run) continue;
        // A point: "x, y", either of which may be empty.
        const size_t comma = line.find(',');
        const auto xs = comma == std::string_view::npos ? line : line.substr(0, comma);
        const auto ys = comma == std::string_view::npos ? std::string_view() : line.substr(comma + 1);
        const auto x = to_number(xs), y = to_number(ys);
        if ((!x && !trim(xs).empty()) || (!y && !trim(ys).empty())) {
            out.error = "Line " + std::to_string(i + 1) + " is not a point (x, y): '" + std::string(line) + "'.";
            return out;
        }
        run->points.push_back({x.value_or(kNaN), y.value_or(kNaN)});
    }

    // The parameters as saved, when they are the equation's.
    const auto& def = model_def(p.model);
    p.params = default_param_settings(p.model);
    bool match = params.size() == def.params.size();
    for (size_t j = 0; match && j < params.size(); ++j) match = params[j].first == def.params[j].name;
    if (match)
        for (size_t j = 0; j < params.size(); ++j) p.params[j] = params[j].second;
    if (p.runs.empty()) p.runs.push_back(ProjectRun{"Run 1", {}, false});
    out.project = std::move(p);
    return out;
}

}  // namespace kin
