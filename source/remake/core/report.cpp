#include "core/report.hpp"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <sstream>

namespace kin {
namespace {

std::string printf_str(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    return buf;
}

std::string csv_quote(std::string_view s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

std::string csv_num(double v) {
    if (!std::isfinite(v)) return "";
    return fmt_g(v, 8);
}

std::string number_text(std::optional<int> n) { return n ? std::to_string(*n) : std::string(); }

std::string format_short(const Analysis& a, const ReportOptions& opt, const ReportContext& ctx) {
    const auto rows = a.parameter_rows();
    std::ostringstream os;
    const auto num = report_number(opt, ctx);
    if (opt.layout == ReportLayout::Csv) {
        if (ctx.firstInFile) {
            os << csv_quote("#");
            for (const auto& r : rows) os << ',' << csv_quote(r.name) << ',' << csv_quote("sd");
            os << '\n';
        }
        os << number_text(num);
        for (const auto& r : rows) os << ',' << csv_num(r.value) << ',' << csv_num(r.sd);
        os << '\n';
    } else {
        if (ctx.firstInFile) {
            std::string head = printf_str("%5s", "#");
            for (const auto& r : rows) head += printf_str("  %12.12s  %12s", r.name.c_str(), "sd");
            os << head << '\n' << std::string(head.size(), '-') << '\n';
        }
        os << printf_str("%5s", number_text(num).c_str());
        for (const auto& r : rows)
            os << printf_str("  %12.5g  %12.5g", r.value, r.sd);
        os << '\n';
    }
    return os.str();
}

std::string format_medium(const Analysis& a, const ReportOptions& opt, const ReportContext& ctx) {
    const auto& def = model_def(a.model());
    const auto rows = a.parameter_rows();
    const auto num = report_number(opt, ctx);
    const size_t lines = std::max(a.data.size(), rows.size());
    std::ostringstream os;
    if (opt.layout == ReportLayout::Csv) {
        if (ctx.firstInFile)
            os << csv_quote("#") << ',' << csv_quote(def.xName) << ',' << csv_quote(def.yName) << ','
               << csv_quote("PARM") << ',' << csv_quote("VALUE") << ',' << csv_quote("SD") << '\n';
        for (size_t i = 0; i < lines; ++i) {
            os << (i == 0 ? number_text(num) : std::string("\" \""));
            if (i < a.data.size()) os << ',' << csv_num(a.data[i].x) << ',' << csv_num(a.data[i].y);
            else os << ",,";
            if (i < rows.size())
                os << ',' << csv_quote(rows[i].name) << ',' << csv_num(rows[i].value) << ',' << csv_num(rows[i].sd);
            os << '\n';
        }
        os << '\n';
    } else {
        if (ctx.firstInFile) {
            const std::string head = printf_str("%5s  %10s  %10s  %14s  %12s  %12s", "#", std::string(def.xName).c_str(),
                                                std::string(def.yName).c_str(), "PARM", "VALUE", "SD");
            os << head << '\n' << std::string(head.size(), '-') << '\n';
        }
        for (size_t i = 0; i < lines; ++i) {
            std::string line = printf_str("%5s", i == 0 ? number_text(num).c_str() : "");
            if (i < a.data.size()) line += printf_str("  %10.5g  %10.5g", a.data[i].x, a.data[i].y);
            else line += std::string(24, ' ');
            if (i < rows.size())
                line += printf_str("  %14.14s  %12.5g  %12.5g", rows[i].name.c_str(), rows[i].value, rows[i].sd);
            while (!line.empty() && line.back() == ' ') line.pop_back();
            os << line << '\n';
        }
        os << '\n';
    }
    return os.str();
}

std::string blank_or(const std::string& s) { return s.empty() ? std::string(40, '_') : s; }

std::string format_long(const Analysis& a, const ReportOptions& opt, const ReportContext& ctx) {
    const auto& def = model_def(a.model());
    const std::string in = "    ";
    std::ostringstream os;
    const auto num = report_number(opt, ctx);

    os << in << "Experiment type: " << blank_or(opt.header);
    if (num) os << ", #" << *num;
    os << "\n\n";
    os << in << "Date of experiment: " << blank_or(opt.experimentDate) << "\n";
    os << in << "Today's date: " << ctx.today << "\n\n";
    os << in << a.description << "\n";
    if (ctx.runNumber) os << in << "Data: run #" << *ctx.runNumber << " of the imported file\n";
    os << "\n";

    const std::string title = "RESULTS of " + a.method_title() + ":";
    os << in << title << "\n" << in << std::string(title.size(), '-') << "\n";
    if (!a.linear) {
        os << in << a.fit.message << "  # of iterations = " << a.fit.iterations << "\n";
        os << in << "1 Standard deviation = " << fmt_g(a.fit.stdDev) << "\n";
    }
    os << "\n";
    for (const auto& r : a.parameter_rows()) {
        os << in << r.name << ": " << fmt_g(r.value) << " +/- " << fmt_g(r.sd);
        if (r.value != 0.0 && std::isfinite(r.sd)) os << printf_str("  (%.2f%%)", std::fabs(r.sd * 100.0 / r.value));
        if (r.derived) os << "  (derived)";
        os << "\n";
    }
    os << "\n" << in << "A std. dev. of +/- 0 indicates that this parameter was fixed.\n";

    const auto interp = a.interpretation();
    if (!interp.empty()) os << "\n";
    for (const auto& line : interp) os << in << line << "\n";
    if (!a.notes.empty()) os << "\n";
    for (const auto& note : a.notes) os << in << "Note: " << note << "\n";

    const std::string x(def.xName), y(def.yName);
    const bool arrhenius = a.model() == ModelId::Arrhenius;
    const std::string yhat = arrhenius ? "log10(vhat)" : y + "hat";
    const std::string residName = arrhenius ? "v-vhat" : y + "-" + yhat;  // v3.51's heading
    os << "\n" << in
       << printf_str("%-10s%-12s%-12s%-14s%-14s%s", "Pair #", x.c_str(), y.c_str(), yhat.c_str(),
                     residName.c_str(), "> 2 S.D. ?")
       << "\n";
    os << in << std::string(72, '-') << "\n";
    for (size_t i = 0; i < a.data.size(); ++i) {
        std::string line = printf_str(" %-9zu%-12s%-12s%-14s%-14s%s", i + 1, fmt_g(a.data[i].x).c_str(),
                                      fmt_g(a.data[i].y).c_str(), fmt_g(a.yhat[i]).c_str(),
                                      printf_str("%+.5e", a.resid[i]).c_str(), a.largeResidual[i] ? "   *" : "");
        while (!line.empty() && line.back() == ' ') line.pop_back();
        os << in << line << "\n";
    }
    os << "\n";
    for (const auto& line : a.residual_test_lines()) os << in << line << "\n";
    os << "\n" << std::string(76, '=') << "\n\n";
    return os.str();
}

}  // namespace

std::optional<int> report_number(const ReportOptions& opt, const ReportContext& ctx) {
    switch (opt.numbering) {
        case Numbering::Off: return std::nullopt;
        case Numbering::Sequential: return ctx.sequenceNumber;
        case Numbering::RunNumber: return ctx.runNumber;
    }
    return std::nullopt;
}

std::string format_report(const Analysis& a, const ReportOptions& opt, const ReportContext& ctx) {
    switch (opt.format) {
        case ReportFormat::Short: return format_short(a, opt, ctx);
        case ReportFormat::Medium: return format_medium(a, opt, ctx);
        case ReportFormat::Long: return format_long(a, opt, ctx);
    }
    return {};
}

std::string today_string() {
    static const char* const days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* const months[] = {"January", "February", "March",     "April",   "May",      "June",
                                         "July",    "August",   "September", "October", "November", "December"};
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    return printf_str("%s, %s %d, %d", days[tm.tm_wday], months[tm.tm_mon], tm.tm_mday, tm.tm_year + 1900);
}

std::string report_format_name(ReportFormat f) {
    switch (f) {
        case ReportFormat::Short: return "Short (one line per run)";
        case ReportFormat::Medium: return "Medium (data and parameters)";
        case ReportFormat::Long: return "Long (full report with residuals)";
    }
    return {};
}

std::string numbering_name(Numbering n) {
    switch (n) {
        case Numbering::Off: return "Off";
        case Numbering::Sequential: return "Sequential";
        case Numbering::RunNumber: return "Use file run #";
    }
    return {};
}

}  // namespace kin
