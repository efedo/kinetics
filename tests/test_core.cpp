// Tests for the Kinetics core: model derivatives and inverses, both fitting
// methods on every equation, statistics, file parsing, batch analysis and
// reports, plus regression tests for the defects fixed from v3.51.

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "core/analysis.hpp"
#include "core/batch.hpp"
#include "core/guess.hpp"
#include "core/import.hpp"
#include "core/models.hpp"
#include "core/plots.hpp"
#include "core/report.hpp"
#include "core/stats.hpp"

#include "check.hpp"
#include "helpers.hpp"

using namespace kin;
using namespace test;

namespace {

// ---------------------------------------------------------------------------

void test_gradients() {
    for (const auto& c : model_cases()) {
        std::vector<double> xs = c.x;
        xs.push_back(0.37);
        for (double x : xs) {
            std::vector<double> d(c.p.size());
            CHECK(gradient(c.model, x, c.p, d));
            for (size_t j = 0; j < c.p.size(); ++j) {
                auto p1 = c.p, p2 = c.p;
                const double h = 1e-6 * std::max(std::fabs(c.p[j]), 1e-3);
                p1[j] += h;
                p2[j] -= h;
                const double fd = (evaluate(c.model, x, p1) - evaluate(c.model, x, p2)) / (2 * h);
                char what[96];
                std::snprintf(what, sizeof what, "model %c, x=%g, dp%zu", model_def(c.model).letter, x, j);
                check(std::fabs(fd - d[j]) <= 1e-5 * (std::fabs(fd) + 1e-3), "analytic == numeric derivative",
                      __FILE__, __LINE__, what);
            }
        }
    }
}

void test_inverses() {
    for (const auto& c : model_cases()) {
        for (double x : {1.0, 5.0, 20.0}) {
            const double y = evaluate(c.model, x, c.p);
            const auto back = inverse(c.model, y, c.p);
            CHECK(back.has_value());
            if (back) CHECK_REL(*back, x, 1e-9);
        }
    }
}

void test_fit_every_model_both_methods() {
    for (const auto& c : model_cases()) {
        if (c.model == ModelId::Linear) continue;
        for (Method method : {Method::Marquardt, Method::GaussNewton}) {
            const auto a = must_analyse(request(c.model, exact_data(c), method));
            char what[64];
            std::snprintf(what, sizeof what, "model %c method %d", model_def(c.model).letter, static_cast<int>(method));
            check(a.fit.status == FitStatus::Converged || a.fit.status == FitStatus::NoImprovement,
                  "fit converged", __FILE__, __LINE__, std::string(what) + " " + a.fit.message);
            for (size_t j = 0; j < c.p.size(); ++j)
                check(std::fabs(a.fit.p[j] - c.p[j]) <= 1e-4 * std::fabs(c.p[j]), "recovers parameter", __FILE__,
                      __LINE__, std::string(what) + " p" + std::to_string(j) + "=" + fmt_g(a.fit.p[j]));
        }
    }
}

void test_noisy_fit_has_sensible_sds() {
    const auto c = model_cases()[0];
    const auto lm = must_analyse(request(c.model, wobbly_data(c, 0.03), Method::Marquardt));
    const auto gn = must_analyse(request(c.model, wobbly_data(c, 0.03), Method::GaussNewton));
    CHECK_REL(lm.fit.p[0], gn.fit.p[0], 1e-4);
    CHECK_REL(lm.fit.p[1], gn.fit.p[1], 1e-4);
    CHECK_REL(lm.fit.sd[0], gn.fit.sd[0], 1e-3);
    CHECK(lm.fit.sd[0] > 0 && lm.fit.sd[0] < 0.5 * lm.fit.p[0]);
    CHECK(lm.fit.dof == static_cast<int>(c.x.size()) - 2);
}

// v3.51 defect 5: absolute |delta chi^2| < 0.1 stop rule.
void test_small_values_still_converge() {
    Case c = model_cases()[0];
    c.p = {2.5, 1e-4};  // velocities around 1e-4
    auto req = request(c.model, wobbly_data(c, 0.02));
    req.guess = GuessMode::Manual;
    req.params[0].value = 40.0;  // poor starting values
    req.params[1].value = 1e-3;
    const auto a = must_analyse(req);
    CHECK(a.fit.status == FitStatus::Converged);
    CHECK(a.fit.iterations > 3);
    auto big = c;
    big.p[1] = 10.0;
    const auto ref = must_analyse(request(big.model, wobbly_data(big, 0.02)));
    CHECK_REL(a.fit.p[0], ref.fit.p[0], 1e-5);
    CHECK_REL(a.fit.p[1] / 1e-4, ref.fit.p[1] / 10.0, 1e-5);
}

// v3.51 defect 6: Gauss-Newton clamp applied to the wrong parameter after
// fixing Vmax reordered the list (activator, Vmax fixed, v0 free).
void test_fixed_parameter_reordering() {
    const auto c = model_cases()[3];  // ActivatorGt0 {Ka, h, Vmax, v0}
    for (Method method : {Method::GaussNewton, Method::Marquardt}) {
        auto req = request(c.model, exact_data(c), method);
        req.params[2].fixed = true;
        req.params[2].value = 12.0;
        const auto a = must_analyse(req);
        CHECK(a.fit.p[2] == 12.0);
        CHECK(a.fit.sd[2] == 0.0);
        CHECK_REL(a.fit.p[0], 3.0, 1e-4);
        CHECK_REL(a.fit.p[1], 1.6, 1e-4);
        CHECK_REL(a.fit.p[3], 2.0, 1e-4);
    }
    const auto ci = model_cases()[7];  // InhibitionI50, vmin fixed
    auto req = request(ci.model, exact_data(ci), Method::GaussNewton);
    req.params[2].fixed = true;
    req.params[2].value = 3.0;
    const auto a = must_analyse(req);
    CHECK_REL(a.fit.p[0], 4.0, 1e-4);
    CHECK_REL(a.fit.p[3], 20.0, 1e-4);
}

void test_v0_taken_from_zero_concentration() {
    const auto c = model_cases()[6];  // linear inhibition, data include [I] = 0
    auto req = request(c.model, exact_data(c));
    req.params[1].fixed = true;
    req.params[1].value = 999.0;  // overridden by the measurement at [I] = 0
    const auto a = must_analyse(req);
    CHECK_REL(a.fit.p[1], 20.0, 1e-12);
    CHECK_REL(a.fit.p[0], 4.0, 1e-6);
    CHECK(!a.notes.empty());
}

void test_hill_coefficient_test() {
    const auto c = model_cases()[1];  // h = 1.8
    const auto a = must_analyse(request(c.model, wobbly_data(c, 0.01)));
    bool found = false;
    for (const auto& line : a.interpretation())
        if (line.find("Hill coeff. is significantly different from 1") != std::string::npos) found = true;
    CHECK(found);
}

void test_arrhenius_example() {
    const auto text = read_text_file(kExamples / "arrhenius.csv");
    CHECK(text.has_value());
    const auto a = must_analyse(request(ModelId::Arrhenius, parse_xy_table(*text)));
    CHECK_NEAR(a.fit.p[0], 9.0, 0.3);
    CHECK_NEAR(a.fit.p[1], 12.0, 0.4);
    const auto rows = a.parameter_rows();
    CHECK(rows.size() == 3 && rows[2].derived);
    CHECK_REL(rows[2].value, a.fit.p[1] * 4.184, 1e-12);
}

void test_example_michaelis_menten() {
    const auto text = read_text_file(kExamples / "michaelis_menten.csv");
    CHECK(text.has_value());
    const auto pts = parse_xy_table(*text);
    CHECK(pts.size() == 10);
    const auto lm = must_analyse(request(ModelId::MichaelisMenten, pts, Method::Marquardt));
    const auto gn = must_analyse(request(ModelId::MichaelisMenten, pts, Method::GaussNewton));
    CHECK_NEAR(lm.fit.p[0], 2.5, 0.4);
    CHECK_NEAR(lm.fit.p[1], 10.0, 0.6);
    CHECK_REL(gn.fit.p[0], lm.fit.p[0], 1e-4);
}

// ---------------------------------------------------------------------------

void test_distributions() {
    CHECK_NEAR(t_two_tailed_p(2.228139, 10), 0.05, 1e-5);
    CHECK_NEAR(t_two_tailed_p(12.7062, 1), 0.05, 1e-4);
    CHECK_NEAR(t_two_tailed_p(0.0, 5), 1.0, 1e-12);
    CHECK_NEAR(f_upper_p(4.9646, 1, 10), 0.05, 1e-4);
    CHECK_NEAR(f_upper_p(3.0556, 3, 15), 0.06, 1e-3);
    CHECK(p_bin(0.0004) == 0.001);
    CHECK(p_bin(0.03) == 0.05);
    CHECK(p_bin(0.2) == 1.0);
    CHECK(combinations(8, 4) == 70.0);
    CHECK(combinations(3, 5) == 0.0);
    CHECK(combinations(3, -1) == 0.0);
}

void test_linear_regression() {
    const std::vector<Point> d = {{1, 2}, {2, 4}, {3, 5}, {4, 4}, {5, 5}};
    const auto lr = linear_regression(d);
    CHECK(lr.ok);
    CHECK_NEAR(lr.slope, 0.6, 1e-12);
    CHECK_NEAR(lr.intercept, 2.2, 1e-12);
    CHECK_NEAR(lr.sdSlope, std::sqrt(0.08), 1e-12);
    CHECK_NEAR(lr.sdIntercept, std::sqrt(0.88), 1e-12);
    CHECK_NEAR(lr.r, 6.0 / std::sqrt(60.0), 1e-12);
    CHECK(lr.df == 3);
    CHECK_NEAR(lr.tSlopeVs0, 0.6 / std::sqrt(0.08), 1e-12);
    CHECK_NEAR(lr.tSlopeVs1, 0.4 / std::sqrt(0.08), 1e-12);

    // Replicated X: ANOVA pieces add up.
    const std::vector<Point> rep = {{1, 1.1}, {1, 0.9}, {2, 2.2}, {2, 1.9}, {3, 2.8}, {3, 3.3}, {4, 4.1}, {4, 3.8}};
    const auto lr2 = linear_regression(rep);
    CHECK(lr2.replicates && lr2.groups == 4);
    CHECK_NEAR(lr2.ssRegression + lr2.ssDeviation + lr2.ssPureError, lr2.ssTotal, 1e-12);
    CHECK(lr2.df == 6 && lr2.dfDeviation == 2 && lr2.dfPureError == 4);  // SDs use the residual, df n - 2

    // Through a fixed intercept.
    const std::vector<Point> origin = {{1, 2.1}, {2, 3.9}, {3, 6.2}, {4, 7.9}};
    const auto lr3 = regression_through_point(origin, 0.0);
    CHECK(lr3.ok && lr3.interceptFixed);
    CHECK_NEAR(lr3.slope, (2.1 + 7.8 + 18.6 + 31.6) / 30.0, 1e-12);
    CHECK(lr3.df == 3);

    // Via analyse(): the linear model uses the regression.
    auto req = request(ModelId::Linear, d);
    const auto a = must_analyse(req);
    CHECK(a.linear.has_value());
    CHECK_NEAR(a.fit.p[1], 0.6, 1e-12);
    CHECK_NEAR(a.fit.stdDev, std::sqrt(2.4 / 3.0), 1e-12);
    bool vs0 = false, vs1 = false;
    for (const auto& line : a.interpretation()) {
        if (line.find("SLOPE is NOT significantly different from 0") != std::string::npos) vs0 = true;
        if (line.find("SLOPE is NOT significantly different from 1") != std::string::npos) vs1 = true;
    }
    CHECK(vs0 && vs1);  // v3.51 printed "vs 1.0" for the test against 0
}

void test_residual_tests() {
    // Line y = 0 so residual signs equal the data signs.
    const std::vector<double> zero = {0.0, 0.0};
    std::vector<Point> blocks, alternating, allPositive;
    for (int i = 0; i < 8; ++i) {
        blocks.push_back({double(i), i < 4 ? 1.0 : -1.0});
        alternating.push_back({double(i), i % 2 ? -1.0 : 1.0});
        allPositive.push_back({double(i), 1.0});
    }
    auto r1 = residual_tests(ModelId::Linear, blocks, zero);
    CHECK(r1.sequence == "++++----" && r1.runs == 2);
    CHECK_NEAR(r1.runsP, 2.0 / 70.0, 1e-12);
    CHECK(!r1.runsRandom && r1.signsRandom);
    auto r2 = residual_tests(ModelId::Linear, alternating, zero);
    CHECK(r2.runs == 8 && r2.runsRandom);
    auto r3 = residual_tests(ModelId::Linear, allPositive, zero);
    CHECK_NEAR(r3.signP, 2.0 / 256.0, 1e-12);
    CHECK(!r3.signsRandom && !r3.runsRandom);
    std::vector<Point> few(blocks.begin(), blocks.begin() + 6);
    CHECK(!residual_tests(ModelId::Linear, few, zero).runsTestable);
}

// ---------------------------------------------------------------------------

void test_spreadsheet_import() {
    const auto text = read_text_file(kExamples / "spreadsheet_runs.txt");
    CHECK(text.has_value());
    const auto parsed = parse_spreadsheet(*text);
    CHECK(parsed.file.has_value());
    if (!parsed.file) return;
    const auto& f = *parsed.file;
    CHECK(f.x.size() == 8 && f.x[0] == 0.5 && f.x[7] == 12);
    CHECK(f.runs.size() == 4);
    CHECK(f.points(*f.find_run(3), true).size() == 7);  // one value is -1
    CHECK(f.points(*f.find_run(1), true).size() == 8);

    // Format from the v3.51 help text, with commas and quotes.
    const auto p2 = parse_spreadsheet("\"S=3\"\n\"#\",1,2,3\n7,0.5,0.8,0.9\n8,0.4,-1,0.85\n#\n");
    CHECK(p2.file && p2.file->runs.size() == 2 && p2.file->runs[1].number == 8);
    CHECK(p2.file && p2.file->points(p2.file->runs[1], true).size() == 2);
    // Negative values other than -1 are kept when not treated as missing.
    const auto p3 = parse_spreadsheet("S=2\n# 10 20\n1 -0.5 -1\n#");
    CHECK(p3.file && p3.file->points(p3.file->runs[0], false).size() == 1);
    CHECK(p3.file && p3.file->points(p3.file->runs[0], true).empty());

    CHECK(!parse_spreadsheet("no header here").file);
    CHECK(!parse_spreadsheet("S=3\n# 1 2 3\n1 0.5 0.6\n").file);  // incomplete run
}

void test_mpa_import() {
    const auto text = read_text_file(kExamples / "mpa_plate.txt");
    CHECK(text.has_value());
    CHECK(mpa_dataset_count(*text, 8) == 2);
    const auto d2 = parse_mpa_dataset(*text, 2, 8);
    CHECK(d2.error.empty() && d2.y.size() == 8);
    CHECK_NEAR(d2.y[0], 17.591, 1e-12);
    CHECK(!parse_mpa_dataset(*text, 3, 8).error.empty());
}

void test_xy_table() {
    const auto rows = parse_xy_table("\"X\",\"Y\"\n1,2\n3,\n,4\n\n5 6\n");
    CHECK(rows.size() == 4);
    CHECK(rows[0].x == 1 && rows[0].y == 2);
    CHECK(rows.size() == 4 && rows[3].x == 5 && rows[3].y == 6);
    CHECK(std::isnan(rows[1].y) && std::isnan(rows[2].x));
    const auto spaced = parse_xy_table("1 2\n3   4\n");
    CHECK(spaced.size() == 2 && spaced[1].y == 4);
    const auto back = parse_xy_table(format_xy_table(rows, "[S]", "v"));
    CHECK(back.size() == 4 && back[0].x == 1 && std::isnan(back[1].y) && std::isnan(back[2].x));
}

// v3.51 defect 1: batch analysis skipped every other run when reports used
// sequential numbering. Here every run is analysed and numbered.
void test_batch_and_report_numbering() {
    const auto parsed = parse_spreadsheet(*read_text_file(kExamples / "spreadsheet_runs.txt"));
    AnalysisRequest tmpl = request(ModelId::HillGt0, {});
    const auto items = analyse_spreadsheet_runs(*parsed.file, 1, tmpl, true);
    CHECK(items.size() == 4);
    for (size_t i = 0; i < items.size(); ++i) {
        CHECK(items[i].runNumber == static_cast<int>(i) + 1);
        CHECK(items[i].analysis.has_value());
    }
    CHECK(analyse_spreadsheet_runs(*parsed.file, 3, tmpl, true).size() == 2);

    ReportOptions opt;
    opt.format = ReportFormat::Long;
    opt.numbering = Numbering::Sequential;
    opt.startNumber = 5;
    std::string all;
    for (size_t i = 0; i < items.size(); ++i) {
        ReportContext ctx;
        ctx.sequenceNumber = opt.startNumber + static_cast<int>(i);
        ctx.runNumber = items[i].runNumber;
        ctx.today = "today";
        all += format_report(*items[i].analysis, opt, ctx);
    }
    for (int n = 5; n <= 8; ++n) CHECK(all.find(", #" + std::to_string(n) + "\n") != std::string::npos);
    CHECK(all.find("Data: run #4") != std::string::npos);

    opt.format = ReportFormat::Short;
    ReportContext first;
    first.sequenceNumber = 1;
    const auto shortCsv = format_report(*items[0].analysis, opt, first);
    CHECK(shortCsv.find("\"#\",\"Km\",\"sd\",\"h\"") == 0);
    first.firstInFile = false;
    CHECK(format_report(*items[0].analysis, opt, first).find("\"#\"") == std::string::npos);

    opt.format = ReportFormat::Medium;
    opt.layout = ReportLayout::Text;
    const auto medium = format_report(*items[0].analysis, opt, first);
    CHECK(medium.find("Vmax") != std::string::npos);
}

void test_mpa_batch() {
    const auto text = *read_text_file(kExamples / "mpa_plate.txt");
    const std::vector<double> x = {0, 0.5, 1, 2, 4, 8, 16, 32};
    const auto items = analyse_mpa_datasets(text, 1, 8, x, request(ModelId::InhibitionLinear, {}));
    CHECK(items.size() == 2);
    CHECK(items[0].analysis && std::fabs(items[0].analysis->fit.p[0] - 4.0) < 0.6);
    CHECK(items[1].analysis && std::fabs(items[1].analysis->fit.p[0] - 6.0) < 0.9);
}

void test_plots() {
    const auto c = model_cases()[0];  // MM, Km 2.5
    const auto a = must_analyse(request(c.model, exact_data(c)));
    for (PlotType t : available_plots(c.model)) {
        const auto pd = make_plot(a, t, default_plot_options(t, c.model));
        CHECK(pd.px.size() == c.x.size());
        CHECK(pd.xmin < pd.xmax && pd.ymin < pd.ymax);
        if (t != PlotType::Residual) {
            int finite = 0;
            for (double v : pd.cy) finite += std::isfinite(v);
            CHECK(finite > 100);
        }
    }
    const auto lb = make_plot(a, PlotType::LineweaverBurk, default_plot_options(PlotType::LineweaverBurk, c.model));
    CHECK(lb.xIntercept && std::fabs(*lb.xIntercept + 1.0 / 2.5) < 1e-6);
    CHECK(lb.xmin < -1.0 / 2.5);
    // Points on a Lineweaver-Burk line: 1/v = (Km/Vmax)(1/S) + 1/Vmax.
    CHECK_NEAR(lb.py[0], 2.5 / 10.0 * lb.px[0] + 0.1, 1e-9);
    const auto hc = must_analyse(request(ModelId::HillGt0, exact_data(model_cases()[1])));
    const auto hill = make_plot(hc, PlotType::Hill, default_plot_options(PlotType::Hill, ModelId::HillGt0));
    CHECK(hill.xIntercept && std::fabs(*hill.xIntercept - std::log10(3.0)) < 1e-6);
    CHECK(available_plots(ModelId::InhibitionLinear).size() == 4);
}

void test_guess_quality() {
    // Automatic estimates should land near the answer for clean data.
    for (const auto& c : model_cases()) {
        if (c.model == ModelId::Linear) continue;
        const auto data = clean_data(exact_data(c));
        const auto g = automatic_guess(c.model, data, default_param_settings(c.model));
        for (size_t j = 0; j < c.p.size(); ++j) {
            char what[64];
            std::snprintf(what, sizeof what, "model %c p%zu guess %g true %g", model_def(c.model).letter, j, g[j], c.p[j]);
            check(g[j] > 0.2 * c.p[j] && g[j] < 5 * c.p[j], "guess within 5x", __FILE__, __LINE__, what);
        }
    }
}

// Registered in this order so the original suite runs first.
const ::test::Registrar kCoreTests[] = {
    {"core: gradients", test_gradients},
    {"core: inverses", test_inverses},
    {"core: fit every model, both methods", test_fit_every_model_both_methods},
    {"core: noisy fit SDs", test_noisy_fit_has_sensible_sds},
    {"core: small values converge (defect 5)", test_small_values_still_converge},
    {"core: fixed parameter reordering (defect 6)", test_fixed_parameter_reordering},
    {"core: v0 from zero concentration", test_v0_taken_from_zero_concentration},
    {"core: Hill coefficient test", test_hill_coefficient_test},
    {"core: Arrhenius example", test_arrhenius_example},
    {"core: Michaelis-Menten example", test_example_michaelis_menten},
    {"core: distributions", test_distributions},
    {"core: linear regression (defect 4)", test_linear_regression},
    {"core: residual tests", test_residual_tests},
    {"core: spreadsheet import", test_spreadsheet_import},
    {"core: MPA import", test_mpa_import},
    {"core: xy table", test_xy_table},
    {"core: batch + report numbering (defects 1, 3)", test_batch_and_report_numbering},
    {"core: MPA batch", test_mpa_batch},
    {"core: plots", test_plots},
    {"core: automatic guesses", test_guess_quality},
};

}  // namespace
