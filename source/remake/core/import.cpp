#include "core/import.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>

namespace kin {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// Tokenizer with the rules of v3.51 read_in_number(): skip anything that is
// not a digit, '.', '-' or '#'; '#' is a marker; a number ends at a comma,
// space, tab, line break or '#'.
class Scanner {
public:
    explicit Scanner(std::string_view text) : t_(text) {}

    enum class Kind { Number, Hash, End };
    struct Token {
        Kind kind;
        double value;
    };

    Token next() {
        while (pos_ < t_.size()) {
            const char c = t_[pos_];
            if (c == '#') {
                ++pos_;
                return {Kind::Hash, 0.0};
            }
            if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == '-') break;
            ++pos_;
        }
        if (pos_ >= t_.size()) return {Kind::End, 0.0};
        const size_t start = pos_;
        while (pos_ < t_.size()) {
            const char c = t_[pos_];
            if (c == ',' || c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '#') break;
            ++pos_;
        }
        const std::string tok(t_.substr(start, pos_ - start));
        return {Kind::Number, std::strtod(tok.c_str(), nullptr)};  // like atof(): junk reads as 0
    }

    // Moves just past the next occurrence of ch (case-insensitive); false at end.
    bool find(char ch) {
        while (pos_ < t_.size()) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(t_[pos_++])));
            if (c == ch) return true;
        }
        return false;
    }

private:
    std::string_view t_;
    size_t pos_ = 0;
};

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.push_back(line);
        if (end == text.size()) break;
        start = end + 1;
    }
    // A trailing newline does not start another line.
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

std::optional<double> parse_number(std::string_view s) {
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.front())) || s.front() == '"')) s.remove_prefix(1);
    while (!s.empty() && (std::isspace(static_cast<unsigned char>(s.back())) || s.back() == '"')) s.remove_suffix(1);
    if (s.empty()) return std::nullopt;
    const std::string tmp(s);
    char* end = nullptr;
    const double v = std::strtod(tmp.c_str(), &end);
    if (end == tmp.c_str() || *end != '\0' || !std::isfinite(v)) return std::nullopt;
    return v;
}

}  // namespace

const SpreadsheetRun* SpreadsheetFile::find_run(int number) const {
    for (const auto& r : runs)
        if (r.number == number) return &r;
    return nullptr;
}

std::vector<Point> SpreadsheetFile::points(const SpreadsheetRun& run, bool negativeIsMissing) const {
    std::vector<Point> pts;
    for (size_t i = 0; i < x.size() && i < run.y.size(); ++i) {
        const double v = run.y[i];
        const bool missing = negativeIsMissing ? v < 0.0 : v == -1.0;
        if (!missing) pts.push_back({x[i], v});
    }
    return pts;
}

SpreadsheetParse parse_spreadsheet(std::string_view text) {
    SpreadsheetParse out;
    Scanner sc(text);
    auto badFormat = [&] {
        out.error = "Incorrect file format: expected 'S=<number of columns>' followed by '#' and the substrate values.";
        return out;
    };
    if (!sc.find('s') || !sc.find('=')) return badFormat();
    const auto nTok = sc.next();
    // The column count must fit an int (written so that NaN is rejected too).
    if (nTok.kind != Scanner::Kind::Number || !(nTok.value >= 1 && nTok.value <= 1e6)) return badFormat();
    const int ns = static_cast<int>(nTok.value);
    if (!sc.find('#')) return badFormat();

    SpreadsheetFile file;
    for (int i = 0; i < ns; ++i) {
        const auto t = sc.next();
        if (t.kind != Scanner::Kind::Number) {
            out.error = "The file declares S=" + std::to_string(ns) + " but lists only " + std::to_string(i) +
                        " substrate values.";
            return out;
        }
        file.x.push_back(t.value);
    }
    for (;;) {
        const auto rn = sc.next();
        if (rn.kind != Scanner::Kind::Number) break;  // '#' or end of file
        if (!(std::fabs(rn.value) < 1e9)) {
            out.error = "A run number in the file is too large.";
            return out;
        }
        SpreadsheetRun run;
        run.number = static_cast<int>(rn.value);
        for (int i = 0; i < ns; ++i) {
            const auto t = sc.next();
            if (t.kind != Scanner::Kind::Number) {
                out.error = "Run #" + std::to_string(run.number) + " has fewer than " + std::to_string(ns) +
                            " values.";
                return out;
            }
            run.y.push_back(t.value);
        }
        file.runs.push_back(std::move(run));
    }
    if (file.runs.empty()) {
        out.error = "The file contains no runs.";
        return out;
    }
    out.file = std::move(file);
    return out;
}

MpaParse parse_mpa_dataset(std::string_view text, int dataset, int count) {
    MpaParse out;
    if (dataset < 1 || count < 1) {
        out.error = "The dataset number and the number of velocities must be at least 1.";
        return out;
    }
    const auto lines = split_lines(text);
    const size_t first = 3 + static_cast<size_t>(dataset - 1) * static_cast<size_t>(count);
    for (int i = 0; i < count; ++i) {
        const size_t li = first + static_cast<size_t>(i);
        if (li >= lines.size()) {
            out.error = "End of file: dataset " + std::to_string(dataset) + " has only " + std::to_string(i) +
                        " of " + std::to_string(count) + " velocities.";
            out.y.clear();
            return out;
        }
        const auto line = lines[li];
        const size_t comma = line.find(',');
        std::optional<double> v;
        if (comma != std::string_view::npos) {
            Scanner sc(line.substr(comma + 1));
            const auto t = sc.next();
            if (t.kind == Scanner::Kind::Number) v = t.value;
        }
        if (!v || *v < 0.0) {
            out.error = "Line " + std::to_string(li + 1) + " has no 'cell, velocity' record: dataset " +
                        std::to_string(dataset) + " has only " + std::to_string(i) + " of " +
                        std::to_string(count) + " velocities.";
            out.y.clear();
            return out;
        }
        out.y.push_back(*v);
    }
    return out;
}

int mpa_dataset_count(std::string_view text, int count) {
    if (count < 1) return 0;
    const auto lines = split_lines(text);
    if (lines.size() <= 3) return 0;
    return static_cast<int>((lines.size() - 3) / static_cast<size_t>(count));
}

std::vector<Point> parse_xy_table(std::string_view text) {
    std::vector<Point> rows;
    for (auto line : split_lines(text)) {
        // Split on , ; or tab; if none present, on runs of spaces.
        std::vector<std::string_view> cells;
        const bool delimited = line.find_first_of(",;\t") != std::string_view::npos;
        size_t start = 0;
        for (size_t i = 0; i <= line.size(); ++i) {
            const bool sep = i == line.size() || (delimited ? (line[i] == ',' || line[i] == ';' || line[i] == '\t')
                                                            : line[i] == ' ');
            if (!sep) continue;
            auto cell = line.substr(start, i - start);
            if (delimited || !cell.empty()) cells.push_back(cell);
            start = i + 1;
        }
        if (cells.size() < 2) continue;
        auto blank = [](std::string_view s) {
            for (char ch : s)
                if (!std::isspace(static_cast<unsigned char>(ch)) && ch != '"') return false;
            return true;
        };
        const auto x = parse_number(cells[0]);
        const auto y = parse_number(cells[1]);
        const bool xOk = x || blank(cells[0]);
        const bool yOk = y || blank(cells[1]);
        if (!xOk || !yOk || (!x && !y)) continue;  // heading or empty row
        rows.push_back({x.value_or(kNaN), y.value_or(kNaN)});
    }
    return rows;
}

std::string format_xy_table(const std::vector<Point>& rows, std::string_view xName, std::string_view yName) {
    std::ostringstream os;
    os << '"' << xName << "\",\"" << yName << "\"\n";
    char buf[64];
    for (const auto& r : rows) {
        if (std::isfinite(r.x)) {
            std::snprintf(buf, sizeof buf, "%.10g", r.x);
            os << buf;
        }
        os << ',';
        if (std::isfinite(r.y)) {
            std::snprintf(buf, sizeof buf, "%.10g", r.y);
            os << buf;
        }
        os << '\n';
    }
    return os.str();
}

std::optional<std::string> read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_text_file(const std::filesystem::path& path, std::string_view text, bool append) {
    std::ofstream out(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!out) return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

}  // namespace kin
