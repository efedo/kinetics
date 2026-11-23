// Tests for core/report: SHORT, MEDIUM and LONG reports in both layouts,
// the numbering modes, and the name helpers.
//
// Most tests format a hand-built Analysis, so every number in the report is
// known exactly. The expected fixed-width lines were produced with Python's
// bytes %-formatting, which pads by bytes like C printf (the degree sign in
// "T (°C)" is two bytes in UTF-8), for example
//   b = lambda fmt, *a: (fmt.encode() % tuple(x.encode() if isinstance(x, str) else x for x in a)).decode()
//   head = b('%5s', '#') + ''.join(b('  %12.12s  %12s', n, 'sd') for n in ['Km', 'Vmax'])
//   b(' %-9d%-12s%-12s%-14s%-14s%s', 1, '0.5', '1.6', '1.7', '%+.5e' % -0.1, '   *')

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "core/analysis.hpp"
#include "core/models.hpp"
#include "core/report.hpp"
#include "core/stats.hpp"

#include "check.hpp"
#include "helpers.hpp"

namespace {

using namespace kin;

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
const std::string kDegC = "T (\xC2\xB0" "C)";

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
}

void check_text(const std::string& got, const std::vector<std::string>& want, const char* file, int line) {
    const auto lines = split(got);
    ::test::check(lines.size() == want.size(), "same number of lines", file, line,
                  std::to_string(lines.size()) + " vs " + std::to_string(want.size()));
    for (size_t i = 0; i < lines.size() && i < want.size(); ++i)
        ::test::check(lines[i] == want[i], "line matches", file, line,
                      "line " + std::to_string(i + 1) + "\n  got:  [" + lines[i] + "]\n  want: [" + want[i] + "]");
}
// Expected text as lines; the output ends with '\n', so the last element is "".
#define CHECK_TEXT(got, ...) check_text((got), std::vector<std::string>__VA_ARGS__, __FILE__, __LINE__)

// The hand-built analyses start from a real one (so that this file never
// default-constructs Analysis, FitResult or LinearRegression itself: the
// linker would then keep this file's copy of those inline constructors and
// the header lines would drop out of the core library's coverage counters)
// and then overwrite every field the report reads.
Analysis base(ModelId m, std::vector<Point> data, Method method = Method::Marquardt) {
    auto out = analyse(test::request(m, std::move(data), method));
    if (!out.analysis) {
        std::printf("analyse() failed: %s\n", out.error.c_str());
        std::abort();
    }
    return std::move(*out.analysis);
}

void set_too_few(ResidualTests& rt, const std::string& seq, int pos, int neg) {
    rt.sequence = seq;
    rt.positives = pos;
    rt.negatives = neg;
    rt.runs = 2;
    rt.runsTestable = false;
    rt.runsP = 1.0;
    rt.runsRandom = true;
    rt.signP = 1.0;
    rt.signsRandom = true;
}

// Michaelis-Menten, Km 2.5 +/- 0.125, Vmax 10 +/- 0.25, three points.
Analysis make_mm() {
    Analysis a = base(ModelId::MichaelisMenten, {{0.5, 1.6}, {1, 2.8}, {2, 4.5}});
    a.xMin = 0.5;
    a.xMax = 2;
    a.yMin = 1.6;
    a.yMax = 4.5;
    a.fit.status = FitStatus::Converged;
    a.fit.message = "Run has converged.";
    a.fit.method = Method::Marquardt;
    a.fit.iterations = 5;
    a.fit.p = {2.5, 10};
    a.fit.sd = {0.125, 0.25};
    a.fit.fixed = {false, false};
    a.fit.stdDev = 0.05;
    a.yhat = {1.7, 2.75, 4.5};
    a.resid = {-0.1, 0.05, 0.0};
    a.largeResidual = {true, false, false};
    set_too_few(a.residualTests, "-+0", 1, 1);
    a.notes = {"Only 3 points for 2 free parameters: a reliable fit needs more points than parameters + 1."};
    a.description = "Michaelis-Menten/VARIABLE Vmax";
    return a;
}

// Straight line through a fixed intercept of 1, slope 2 +/- 0.1.
Analysis make_linear() {
    Analysis a = base(ModelId::Linear, {{1, 3}, {2, 5.2}, {3, 6.8}});
    a.request.params[0].fixed = true;
    a.xMin = 1;
    a.xMax = 3;
    a.yMin = 3;
    a.yMax = 6.8;
    a.fit.p = {1, 2};
    a.fit.sd = {0, 0.1};
    a.fit.fixed = {true, false};
    a.fit.stdDev = 0.2;
    auto& lr = *a.linear;
    lr.interceptFixed = true;
    lr.intercept = 1;
    lr.slope = 2;
    lr.sdSlope = 0.1;
    lr.r = 0.99;
    lr.sdR = 0.05;
    lr.df = 2;
    lr.tSlopeVs0 = 20;
    lr.pSlopeVs0 = 0.0025;
    lr.tSlopeVs1 = 10;
    lr.pSlopeVs1 = 0.0099;
    lr.fRegression = 400;
    lr.pRegression = 0.0025;
    lr.replicates = false;
    a.yhat = {3, 5, 7};
    a.resid = {0, 0.2, -0.2};
    a.largeResidual = {false, false, false};
    set_too_few(a.residualTests, "0+-", 1, 1);
    a.notes.clear();
    a.description = "Linear regression/FIXED intercept";
    return a;
}

// Arrhenius, Gauss-Newton, log10(A) 9 +/- 0.09, Ea 12 +/- 0.12 kcal/mol, two points.
Analysis make_arrhenius() {
    Analysis a = base(ModelId::Arrhenius, {{5, -0.42}, {10, -0.26}}, Method::GaussNewton);
    a.xMin = 5;
    a.xMax = 10;
    a.yMin = -0.42;
    a.yMax = -0.26;
    a.fit.status = FitStatus::Converged;
    a.fit.message = "Run has converged.";
    a.fit.method = Method::GaussNewton;
    a.fit.iterations = 4;
    a.fit.p = {9, 12};
    a.fit.sd = {0.09, 0.12};
    a.fit.fixed = {false, false};
    a.fit.stdDev = 0.02;
    a.yhat = {-0.4, -0.28};
    a.resid = {-0.02, 0.02};
    a.largeResidual = {false, false};
    set_too_few(a.residualTests, "-+", 1, 1);
    a.notes.clear();
    a.description = "Arrhenius eqn";
    return a;
}

ReportOptions options(ReportFormat f, ReportLayout l, Numbering n = Numbering::Sequential) {
    ReportOptions o;
    o.format = f;
    o.layout = l;
    o.numbering = n;
    return o;
}

ReportContext context(std::optional<int> seq, std::optional<int> run, bool first = true) {
    ReportContext c;
    c.sequenceNumber = seq;
    c.runNumber = run;
    c.firstInFile = first;
    c.today = "Friday, November 13, 2026";
    return c;
}

const std::string kTableRule = "    " + std::string(72, '-');
const std::string kEndRule = std::string(76, '=');

}  // namespace

// ---------------------------------------------------------------------------
// Numbering

TEST_CASE(report_numbering, "report: report_number follows the numbering mode") {
    const auto ctx = context(4, 9);
    CHECK(!report_number(options(ReportFormat::Long, ReportLayout::Csv, Numbering::Off), ctx));
    CHECK(report_number(options(ReportFormat::Long, ReportLayout::Csv, Numbering::Sequential), ctx) == 4);
    CHECK(report_number(options(ReportFormat::Long, ReportLayout::Csv, Numbering::RunNumber), ctx) == 9);
    // No number available in the context.
    CHECK(!report_number(options(ReportFormat::Long, ReportLayout::Csv, Numbering::Sequential), context({}, 9)));
    CHECK(!report_number(options(ReportFormat::Long, ReportLayout::Csv, Numbering::RunNumber), context(4, {})));
    // An out-of-range mode (e.g. read from a damaged settings file) gives no number.
    CHECK(!report_number(options(ReportFormat::Long, ReportLayout::Csv, static_cast<Numbering>(7)), ctx));
}

TEST_CASE(report_names, "report: format and numbering names") {
    CHECK(report_format_name(ReportFormat::Short) == "Short (one line per run)");
    CHECK(report_format_name(ReportFormat::Medium) == "Medium (data and parameters)");
    CHECK(report_format_name(ReportFormat::Long) == "Long (full report with residuals)");
    CHECK(report_format_name(static_cast<ReportFormat>(9)).empty());
    CHECK(numbering_name(Numbering::Off) == "Off");
    CHECK(numbering_name(Numbering::Sequential) == "Sequential");
    CHECK(numbering_name(Numbering::RunNumber) == "Use file run #");
    CHECK(numbering_name(static_cast<Numbering>(-1)).empty());
}

TEST_CASE(report_unknown_format, "report: an out-of-range format gives an empty report") {
    CHECK(format_report(make_mm(), options(static_cast<ReportFormat>(5), ReportLayout::Csv), context(1, 1)).empty());
}

TEST_CASE(report_today, "report: today_string is a consistent 'Weekday, Month D, YYYY' date") {
    static const char* const days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char* const months[] = {"January", "February", "March",     "April",   "May",      "June",
                                         "July",    "August",   "September", "October", "November", "December"};
    const std::string s = today_string();
    int wday = -1, mon = -1;
    for (int i = 0; i < 7; ++i)
        if (s.rfind(std::string(days[i]) + ", ", 0) == 0) wday = i;
    CHECK(wday >= 0);
    if (wday < 0) return;
    const std::string rest = s.substr(std::string(days[wday]).size() + 2);
    for (int i = 0; i < 12; ++i)
        if (rest.rfind(std::string(months[i]) + " ", 0) == 0) mon = i;
    CHECK(mon >= 0);
    if (mon < 0) return;
    const std::string nums = rest.substr(std::string(months[mon]).size() + 1);
    char* end = nullptr;
    const int day = static_cast<int>(std::strtol(nums.c_str(), &end, 10));
    CHECK(end && end[0] == ',' && end[1] == ' ');
    if (!end || end[0] != ',') return;
    const int year = static_cast<int>(std::strtol(end + 1, nullptr, 10));
    CHECK(year >= 2024 && year < 2200 && day >= 1 && day <= 31);
    CHECK(nums == std::to_string(day) + ", " + std::to_string(year));
    // Day of the week from the date (Sakamoto's method) must match the name.
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year - (mon < 2 ? 1 : 0);
    CHECK((y + y / 4 - y / 100 + y / 400 + t[mon] + day) % 7 == wday);
}

// ---------------------------------------------------------------------------
// SHORT

TEST_CASE(report_short_csv, "report: SHORT comma-separated, with headings and numbering") {
    const auto a = make_mm();
    const auto opt = options(ReportFormat::Short, ReportLayout::Csv);
    CHECK_TEXT(format_report(a, opt, context(3, 7)), {"\"#\",\"Km\",\"sd\",\"Vmax\",\"sd\"", "3,2.5,0.125,10,0.25", ""});
    CHECK_TEXT(format_report(a, opt, context(4, 7, false)), {"4,2.5,0.125,10,0.25", ""});
    CHECK_TEXT(format_report(a, options(ReportFormat::Short, ReportLayout::Csv, Numbering::Off), context(4, 7, false)),
               {",2.5,0.125,10,0.25", ""});
    CHECK_TEXT(
        format_report(a, options(ReportFormat::Short, ReportLayout::Csv, Numbering::RunNumber), context(4, 7, false)),
        {"7,2.5,0.125,10,0.25", ""});
    // Eight significant digits; a missing SD is an empty cell.
    auto b = a;
    b.fit.p[0] = 2.123456789;
    b.fit.sd[1] = kNaN;
    CHECK_TEXT(format_report(b, opt, context(1, {}, false)), {"1,2.1234568,0.125,10,", ""});
    // Arrhenius adds the derived Ea in kJ/mol: 12 * 4.184 = 50.208, 0.12 * 4.184 = 0.50208.
    CHECK_TEXT(format_report(make_arrhenius(), opt, context(1, {})),
               {"\"#\",\"log10(A)\",\"sd\",\"Ea (kcal/mol)\",\"sd\",\"Ea (kJ/mol)\",\"sd\"",
                "1,9,0.09,12,0.12,50.208,0.50208", ""});
}

TEST_CASE(report_short_text, "report: SHORT fixed-width columns") {
    const auto a = make_mm();
    const auto opt = options(ReportFormat::Short, ReportLayout::Text);
    CHECK_TEXT(format_report(a, opt, context(3, {})),
               {"    #            Km            sd          Vmax            sd",
                "-------------------------------------------------------------",
                "    3           2.5         0.125            10          0.25", ""});
    auto b = a;
    b.fit.sd[0] = kNaN;
    CHECK_TEXT(format_report(b, options(ReportFormat::Short, ReportLayout::Text, Numbering::Off), context(3, {}, false)),
               {"                2.5           nan            10          0.25", ""});
    // Headings are cut to the 12-character column ("Ea (kcal/mol)" has 13).
    const auto arr = split(format_report(make_arrhenius(), opt, context(1, {})));
    CHECK(arr.size() == 4);
    if (arr.size() == 4) {
        CHECK(arr[0] == "    #      log10(A)            sd  Ea (kcal/mol            sd   Ea (kJ/mol)            sd");
        CHECK(arr[1] == std::string(89, '-'));
        CHECK(arr[2] == "    1             9          0.09            12          0.12        50.208       0.50208");
    }
}

// ---------------------------------------------------------------------------
// MEDIUM

TEST_CASE(report_medium_csv, "report: MEDIUM comma-separated data and parameters") {
    const auto opt = options(ReportFormat::Medium, ReportLayout::Csv);
    // More data rows than parameters.
    CHECK_TEXT(format_report(make_mm(), opt, context(3, {})),
               {"\"#\",\"[S]\",\"v\",\"PARM\",\"VALUE\",\"SD\"", "3,0.5,1.6,\"Km\",2.5,0.125",
                "\" \",1,2.8,\"Vmax\",10,0.25", "\" \",2,4.5", "", ""});
    // More parameters than data rows, no number, no headings.
    CHECK_TEXT(format_report(make_arrhenius(), options(ReportFormat::Medium, ReportLayout::Csv, Numbering::Off),
                             context(3, {}, false)),
               {",5,-0.42,\"log10(A)\",9,0.09", "\" \",10,-0.26,\"Ea (kcal/mol)\",12,0.12",
                "\" \",,,\"Ea (kJ/mol)\",50.208,0.50208", "", ""});
    // Headings use the equation's axis names; NaN values are empty cells.
    auto arr = make_arrhenius();
    arr.fit.sd[0] = kNaN;
    const auto lines = split(format_report(arr, opt, context(1, {})));
    CHECK(lines.size() == 6);
    if (lines.size() == 6) {
        CHECK(lines[0] == "\"#\",\"" + kDegC + "\",\"log10(v)\",\"PARM\",\"VALUE\",\"SD\"");
        CHECK(lines[1] == "1,5,-0.42,\"log10(A)\",9,");
    }
}

TEST_CASE(report_medium_text, "report: MEDIUM fixed-width data and parameters") {
    const auto opt = options(ReportFormat::Medium, ReportLayout::Text);
    CHECK_TEXT(format_report(make_mm(), opt, context(3, {})),
               {"    #         [S]           v            PARM         VALUE            SD",
                "-------------------------------------------------------------------------",
                "    3         0.5         1.6              Km           2.5         0.125",
                "                1         2.8            Vmax            10          0.25",
                "                2         4.5", "", ""});
    CHECK_TEXT(format_report(make_arrhenius(), options(ReportFormat::Medium, ReportLayout::Text, Numbering::Off),
                             context(3, {})),
               {"    #     " + kDegC + "    log10(v)            PARM         VALUE            SD",
                std::string(73, '-'),  // the heading's length in bytes
                "                5       -0.42        log10(A)             9          0.09",
                "               10       -0.26   Ea (kcal/mol)            12          0.12",
                "                                  Ea (kJ/mol)        50.208       0.50208", "", ""});
    // Without headings only the rows are written.
    CHECK_TEXT(format_report(make_mm(), opt, context(8, {}, false)),
               {"    8         0.5         1.6              Km           2.5         0.125",
                "                1         2.8            Vmax            10          0.25",
                "                2         4.5", "", ""});
}

// ---------------------------------------------------------------------------
// LONG

TEST_CASE(report_long_full, "report: LONG report, line by line") {
    auto opt = options(ReportFormat::Long, ReportLayout::Csv);
    opt.header = "Kinetics of enzyme X";
    opt.experimentDate = "1 Nov 2026";
    CHECK_TEXT(format_report(make_mm(), opt, context(3, 7)),
               {"    Experiment type: Kinetics of enzyme X, #3",
                "",
                "    Date of experiment: 1 Nov 2026",
                "    Today's date: Friday, November 13, 2026",
                "",
                "    Michaelis-Menten/VARIABLE Vmax",
                "    Data: run #7 of the imported file",
                "",
                "    RESULTS of MARQUARDT-LEVENBERG minimization:",
                "    --------------------------------------------",
                "    Run has converged.  # of iterations = 5",
                "    1 Standard deviation = 0.05",
                "",
                "    Km: 2.5 +/- 0.125  (5.00%)",
                "    Vmax: 10 +/- 0.25  (2.50%)",
                "",
                "    A std. dev. of +/- 0 indicates that this parameter was fixed.",
                "",
                "    [S] varies from 0.20 to 0.80 of Km value.",
                "    v varies from 0.16 to 0.45 of Vmax value.",
                "",
                "    Note: Only 3 points for 2 free parameters: a reliable fit needs more points than parameters + 1.",
                "",
                "    Pair #    [S]         v           vhat          v-vhat        > 2 S.D. ?",
                kTableRule,
                "     1        0.5         1.6         1.7           -1.00000e-01     *",
                "     2        1           2.8         2.75          +5.00000e-02",
                "     3        2           4.5         4.5           +0.00000e+00",
                "",
                "    Sequence of residuals = -+0",
                "    Too few data points to test sequence randomness.",
                "    The # of +ve and -ve residuals is random at the P < 0.05 level (1 +, 1 -, P = 1).",
                "",
                kEndRule,
                "",
                ""});
}

TEST_CASE(report_long_linear, "report: LONG report for a linear regression through a fixed intercept") {
    // No header or date: blanks to fill in by hand; numbering off; no run.
    CHECK_TEXT(format_report(make_linear(), options(ReportFormat::Long, ReportLayout::Text, Numbering::Off),
                             context(3, {})),
               {"    Experiment type: " + std::string(40, '_'),
                "",
                "    Date of experiment: " + std::string(40, '_'),
                "    Today's date: Friday, November 13, 2026",
                "",
                "    Linear regression/FIXED intercept",
                "",
                "    RESULTS of LINEAR REGRESSION:",
                "    -----------------------------",
                "",
                "    Intercept: 1 +/- 0  (0.00%)",
                "    Slope: 2 +/- 0.1  (5.00%)",
                "",
                "    A std. dev. of +/- 0 indicates that this parameter was fixed.",
                "",
                "    Correlation coeff. = 0.99 +/- 0.05",
                "    Degrees of freedom = 2",
                "    The SLOPE is significantly different from 0 at the P<0.005 level (t = 20, df = 2, P = 0.0025).",
                "    The SLOPE is significantly different from 1 at the P<0.01 level (t = 10, df = 2, P = 0.0099).",
                "    Regression F = 400 (df 1, 2), P = 0.0025.",
                "",
                "    Pair #    X           Y           Yhat          Y-Yhat        > 2 S.D. ?",
                kTableRule,
                "     1        1           3           3             +0.00000e+00",
                "     2        2           5.2         5             +2.00000e-01",
                "     3        3           6.8         7             -2.00000e-01",
                "",
                "    Sequence of residuals = 0+-",
                "    Too few data points to test sequence randomness.",
                "    The # of +ve and -ve residuals is random at the P < 0.05 level (1 +, 1 -, P = 1).",
                "",
                kEndRule,
                "",
                ""});
}

TEST_CASE(report_long_arrhenius, "report: LONG report for Arrhenius: derived Ea and log10 headings") {
    auto a = make_arrhenius();
    a.notes = {"Only 2 points for 2 free parameters: a reliable fit needs more points than parameters + 1."};
    CHECK_TEXT(format_report(a, options(ReportFormat::Long, ReportLayout::Csv, Numbering::RunNumber), context(3, 12)),
               {"    Experiment type: " + std::string(40, '_') + ", #12",
                "",
                "    Date of experiment: " + std::string(40, '_'),
                "    Today's date: Friday, November 13, 2026",
                "",
                "    Arrhenius eqn",
                "    Data: run #12 of the imported file",
                "",
                "    RESULTS of GAUSS-NEWTON minimization:",
                "    -------------------------------------",
                "    Run has converged.  # of iterations = 4",
                "    1 Standard deviation = 0.02",
                "",
                "    log10(A): 9 +/- 0.09  (1.00%)",
                "    Ea (kcal/mol): 12 +/- 0.12  (1.00%)",
                "    Ea (kJ/mol): 50.208 +/- 0.50208  (1.00%)  (derived)",
                "",
                "    A std. dev. of +/- 0 indicates that this parameter was fixed.",
                // No interpretation lines for Arrhenius, so no blank line here.
                "",
                "    Note: Only 2 points for 2 free parameters: a reliable fit needs more points than parameters + 1.",
                "",
                "    Pair #    " + kDegC + "     log10(v)    log10(vhat)   v-vhat        > 2 S.D. ?",
                kTableRule,
                "     1        5           -0.42       -0.4          -2.00000e-02",
                "     2        10          -0.26       -0.28         +2.00000e-02",
                "",
                "    Sequence of residuals = -+",
                "    Too few data points to test sequence randomness.",
                "    The # of +ve and -ve residuals is random at the P < 0.05 level (1 +, 1 -, P = 1).",
                "",
                kEndRule,
                "",
                ""});
}

TEST_CASE(report_long_parameter_lines, "report: LONG parameter lines without a percentage") {
    auto a = make_mm();
    a.fit.p[0] = 0.0;      // value 0: no percentage
    a.fit.sd[1] = kNaN;    // SD unknown: "n/a", no percentage
    a.notes.clear();
    a.fit.message = "Maximum of 200 iterations reached before convergence.";
    a.fit.iterations = 200;
    a.fit.stdDev = kNaN;
    const auto lines = split(format_report(a, options(ReportFormat::Long, ReportLayout::Csv), context(1, {})));
    auto has = [&](const std::string& s) {
        for (const auto& l : lines)
            if (l == s) return true;
        return false;
    };
    CHECK(has("    Km: 0 +/- 0.125"));
    CHECK(has("    Vmax: 10 +/- n/a"));
    CHECK(has("    Maximum of 200 iterations reached before convergence.  # of iterations = 200"));
    CHECK(has("    1 Standard deviation = n/a"));
    for (const auto& l : lines) CHECK(l.find("Note:") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Reports of real analyses

TEST_CASE(report_long_flags_from_analysis, "report: LONG report marks the residual beyond 2 SD") {
    // The outlier data of the analysis tests: only pair 5 is beyond 2 SD.
    const std::vector<Point> data = {{1, 3.1},  {2, 4.9},  {3, 7.05},  {4, 8.95},  {5, 14.0},
                                     {6, 13.0}, {7, 14.9}, {8, 17.1}, {9, 18.95}, {10, 21.05}};
    const auto a = test::must_analyse(test::request(ModelId::Linear, data));
    const auto lines = split(format_report(a, ReportOptions{}, context(1, {})));
    int pairs = 0;
    for (const auto& l : lines) {
        if (l.size() < 6 || l.compare(0, 5, "     ") != 0 || l[5] < '1' || l[5] > '9') continue;
        ++pairs;
        const bool starred = l.size() >= 4 && l.compare(l.size() - 4, 4, "   *") == 0;
        CHECK(starred == (l.compare(0, 7, "     5 ") == 0));
    }
    CHECK(pairs == 10);
    CHECK(lines.size() > 8 && lines[7] == "    RESULTS of LINEAR REGRESSION:");
}

// numpy: straight line of log10(v) on 1/(T + 273.15) for examples/arrhenius.csv (see
// test_analysis.cpp): log10(A) 9.119053 +/- 0.0898123, Ea 12.158845 +/- 0.1212248 kcal/mol,
// '%.2f' % (0.0898123 / 9.119053 * 100) = 0.98, '%.2f' % (0.1212248 / 12.158845 * 100) = 1.00,
// Ea * 4.184 = 50.8726 +/- 0.507205, residual SD 0.00985309.
TEST_CASE(report_long_arrhenius_example, "report: LONG report of the Arrhenius example matches numpy") {
    const std::vector<Point> data = {{5, -0.4234},  {10, -0.2605}, {15, -0.1117}, {20, 0.0377},
                                     {25, 0.2069}, {30, 0.3520},  {35, 0.4990},  {40, 0.6413}};
    const auto a = test::must_analyse(test::request(ModelId::Arrhenius, data));
    const auto lines = split(format_report(a, ReportOptions{}, context(1, {})));
    auto has = [&](const std::string& s) {
        for (const auto& l : lines)
            if (l == s) return true;
        return false;
    };
    CHECK(has("    log10(A): 9.11905 +/- 0.0898123  (0.98%)"));
    CHECK(has("    Ea (kcal/mol): 12.1588 +/- 0.121225  (1.00%)"));
    CHECK(has("    Ea (kJ/mol): 50.8726 +/- 0.507205  (1.00%)  (derived)"));
    CHECK(has("    1 Standard deviation = 0.00985309"));
    CHECK(has("    Run has converged.  # of iterations = " + std::to_string(a.fit.iterations)));
}

// ---------------------------------------------------------------------------
// Units: only when the user gives some; otherwise the reports are v3.51's.

namespace {

ReportContext with_units(ReportContext c, const std::string& x, const std::string& y) {
    c.xUnit = x;
    c.yUnit = y;
    return c;
}

bool has_line(const std::string& text, const std::string& line) {
    for (const auto& l : split(text))
        if (l == line) return true;
    return false;
}

}  // namespace

TEST_CASE(report_parameter_unit, "report: parameter_unit gives each parameter the unit of X or Y") {
    const std::string x = "uM", y = "nmol/min";
    CHECK(parameter_unit(ModelId::MichaelisMenten, Role::HalfSat, x, y) == "uM");
    CHECK(parameter_unit(ModelId::MichaelisMenten, Role::Vmax, x, y) == "nmol/min");
    CHECK(parameter_unit(ModelId::HillGt0, Role::HillCoeff, x, y).empty());
    CHECK(parameter_unit(ModelId::InhibitionK50, Role::Vmin, x, y) == "nmol/min");
    CHECK(parameter_unit(ModelId::InhibitionK50, Role::V0, x, y) == "nmol/min");
    CHECK(parameter_unit(ModelId::Linear, Role::Intercept, x, y) == "nmol/min");
    CHECK(parameter_unit(ModelId::Linear, Role::Slope, x, y) == "nmol/min per uM");
    // A slope needs both units.
    CHECK(parameter_unit(ModelId::Linear, Role::Slope, x, "").empty());
    CHECK(parameter_unit(ModelId::Linear, Role::Slope, "", y).empty());
    // log10(A), and Ea (named with its unit), take none.
    CHECK(parameter_unit(ModelId::Arrhenius, Role::LogA, x, y).empty());
    CHECK(parameter_unit(ModelId::Arrhenius, Role::Ea, x, y).empty());
    CHECK(parameter_unit(ModelId::MichaelisMenten, Role::HalfSat, "", y).empty());
}

TEST_CASE(report_units_blank, "report: without units every format is unchanged") {
    const auto a = make_mm();
    for (auto f : {ReportFormat::Short, ReportFormat::Medium, ReportFormat::Long})
        for (auto l : {ReportLayout::Csv, ReportLayout::Text}) {
            const auto opt = options(f, l);
            CHECK(format_report(a, opt, with_units(context(3, 7), "", "")) == format_report(a, opt, context(3, 7)));
            // Arrhenius data have fixed units (degrees C, log10), so units are ignored.
            const auto arr = make_arrhenius();
            CHECK(format_report(arr, opt, with_units(context(3, 7), "uM", "nmol/min")) ==
                  format_report(arr, opt, context(3, 7)));
        }
}

TEST_CASE(report_units_short, "report: SHORT headings name the units, in columns wide enough for them") {
    const auto a = make_mm();
    CHECK_TEXT(format_report(a, options(ReportFormat::Short, ReportLayout::Csv), with_units(context(3, {}), "uM", "nmol/min")),
               {"\"#\",\"Km (uM)\",\"sd\",\"Vmax (nmol/min)\",\"sd\"", "3,2.5,0.125,10,0.25", ""});
    CHECK_TEXT(format_report(a, options(ReportFormat::Short, ReportLayout::Text), with_units(context(3, {}), "uM", "nmol/min")),
               {"    #       Km (uM)            sd  Vmax (nmol/min)            sd", std::string(64, '-'),
                "    3           2.5         0.125               10          0.25", ""});
    // Only the rows after the first: the values alone, as before.
    CHECK_TEXT(format_report(a, options(ReportFormat::Short, ReportLayout::Csv), with_units(context(4, {}, false), "uM", "")),
               {"4,2.5,0.125,10,0.25", ""});
}

TEST_CASE(report_units_medium, "report: MEDIUM headings and parameter names carry the units") {
    const auto a = make_mm();
    CHECK_TEXT(format_report(a, options(ReportFormat::Medium, ReportLayout::Csv), with_units(context(3, {}), "uM", "nmol/min")),
               {"\"#\",\"[S] (uM)\",\"v (nmol/min)\",\"PARM\",\"VALUE\",\"SD\"", "3,0.5,1.6,\"Km (uM)\",2.5,0.125",
                "\" \",1,2.8,\"Vmax (nmol/min)\",10,0.25", "\" \",2,4.5", "", ""});
    CHECK_TEXT(format_report(a, options(ReportFormat::Medium, ReportLayout::Text), with_units(context(3, {}), "uM", "nmol/min")),
               {"    #    [S] (uM)  v (nmol/min)             PARM         VALUE            SD", std::string(76, '-'),
                "    3         0.5           1.6          Km (uM)           2.5         0.125",
                "                1           2.8  Vmax (nmol/min)            10          0.25",
                "                2           4.5", "", ""});
}

TEST_CASE(report_units_long, "report: LONG gives a Units line and units with the values and headings") {
    const auto text =
        format_report(make_mm(), options(ReportFormat::Long, ReportLayout::Csv), with_units(context(3, 7), "uM", "nmol/min"));
    const auto lines = split(text);
    CHECK(lines.size() > 7 && lines[5] == "    Michaelis-Menten/VARIABLE Vmax" && lines[6] == "    Units: [S] in uM; v in nmol/min" &&
          lines[7] == "    Data: run #7 of the imported file");
    CHECK(has_line(text, "    1 Standard deviation = 0.05 nmol/min"));
    CHECK(has_line(text, "    Km: 2.5 +/- 0.125 uM  (5.00%)"));
    CHECK(has_line(text, "    Vmax: 10 +/- 0.25 nmol/min  (2.50%)"));
    CHECK(has_line(text, "    Pair #    [S] (uM)    v (nmol/min)  vhat          v-vhat        > 2 S.D. ?"));
    CHECK(has_line(text, "    " + std::string(74, '-')));
    CHECK(has_line(text, "     1        0.5         1.6           1.7           -1.00000e-01     *"));
    CHECK(has_line(text, "     2        1           2.8           2.75          +5.00000e-02"));

    // With only the X unit: no unit on Vmax or the SD. A UTF-8 unit is kept as it is.
    const auto mu = format_report(make_mm(), options(ReportFormat::Long, ReportLayout::Csv),
                                  with_units(context(3, {}), "\xC2\xB5M", ""));
    CHECK(has_line(mu, "    Units: [S] in \xC2\xB5M"));
    CHECK(has_line(mu, "    1 Standard deviation = 0.05"));
    CHECK(has_line(mu, "    Km: 2.5 +/- 0.125 \xC2\xB5M  (5.00%)"));
    CHECK(has_line(mu, "    Vmax: 10 +/- 0.25  (2.50%)"));
}
