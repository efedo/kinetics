// Right panel: the Results tab (verdict, parameters, graph and residuals on
// one screen), the standard curve and report tabs (v3.51 View menu and
// Calculate > new X / new Y).

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cfloat>

#include "app/app.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "core/stats.hpp"

namespace app {
namespace {

struct PlainLine {
    std::string text;
    bool warn = false;  // shown in amber
};

std::string p_text(double p) { return p < 0.0001 ? std::string("P < 0.0001") : "P = " + kin::fmt_g(p, 2); }

std::string range_text(double lo, double hi) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.2f\xE2\x80\x93%.2f", lo, hi);
    return buf;
}

// "X differs significantly from 1 (P = 0.00014; t = 10.4, 5 df)."
PlainLine significance(const std::string& subject, const char* versus, double t, int df, double p) {
    const bool sig = p < 0.05;
    return {subject + (sig ? " differs significantly from " : " is not significantly different from ") + versus + " (" +
                p_text(p) + "; t = " + kin::fmt_g(t, 3) + ", " + std::to_string(df) + " df).",
            false};
}

// The v3.51 interpretation lines (Analysis::interpretation, which the
// report keeps) in plainer words for the screen.
std::vector<PlainLine> plain_interpretation(const kin::Analysis& a) {
    std::vector<PlainLine> out;
    const auto m = a.model();
    const auto& def = kin::model_def(m);
    const auto& p = a.fit.p;
    if (a.linear) {
        const auto& lr = *a.linear;
        out.push_back({"Correlation coefficient r = " + kin::fmt_g(lr.r, 4) + " \xC2\xB1 " + kin::fmt_g(lr.sdR, 2) + "."});
        out.push_back(significance("The slope", "0", lr.tSlopeVs0, lr.df, lr.pSlopeVs0));
        out.push_back(significance("The slope", "1", lr.tSlopeVs1, lr.df, lr.pSlopeVs1));
        if (!lr.interceptFixed) out.push_back(significance("The intercept", "0", lr.tIntercept, lr.df, lr.pIntercept));
        out.push_back({"Regression F = " + kin::fmt_g(lr.fRegression, 4) + " (1 and " + std::to_string(lr.df) + " df), " +
                       p_text(lr.pRegression) + "."});
        if (lr.replicates && lr.dfDeviation > 0 && lr.ssPureError > 0.0)
            out.push_back({"Lack of fit, from the replicated X values: F = " + kin::fmt_g(lr.fLackOfFit, 4) + " (" +
                               std::to_string(lr.dfDeviation) + " and " + std::to_string(lr.dfPureError) + " df), " +
                               p_text(lr.pLackOfFit) + ".",
                           lr.pLackOfFit < 0.05});
        return out;
    }
    if (def.family == kin::Family::Arrhenius) return out;

    const auto iK = kin::param_index(m, kin::Role::HalfSat);
    const auto iV = kin::param_index(m, kin::Role::Vmax);
    const auto iV0 = kin::param_index(m, kin::Role::V0);
    const auto ih = kin::param_index(m, kin::Role::HillCoeff);
    const bool inhibition = def.family == kin::Family::Inhibition;
    const std::string xName(def.xName);
    if (iK) {
        const double K = p[static_cast<size_t>(*iK)];
        const std::string k(def.params[static_cast<size_t>(*iK)].name);
        out.push_back({xName + " spans " + range_text(a.xMin / K, a.xMax / K) + " \xC3\x97 " + k +
                       "; about 0.2\xE2\x80\x93" "5 \xC3\x97 " + k + " is a good range."});
    }
    if (!inhibition && iV) {
        const double V = p[static_cast<size_t>(*iV)];
        out.push_back({"v spans " + range_text(a.yMin / V, a.yMax / V) + " \xC3\x97 Vmax."});
    } else if (inhibition && iV0) {
        const double v0 = p[static_cast<size_t>(*iV0)];
        out.push_back({"v spans " + range_text(a.yMin / v0, a.yMax / v0) + " \xC3\x97 v0."});
    }
    if (def.family == kin::Family::Activation && iV && iV0) {
        const double v0 = p[static_cast<size_t>(*iV0)];
        out.push_back({"Fold activation (Vmax / v0): " +
                       (v0 != 0.0 ? kin::fmt_g(p[static_cast<size_t>(*iV)] / v0, 3) : std::string("infinite")) + "."});
    }
    if (ih) {
        const size_t j = static_cast<size_t>(*ih);
        if (a.fit.fixed[j]) {
            out.push_back({"The Hill coefficient was fixed, so it was not tested against 1."});
        } else if (std::isfinite(a.fit.sd[j]) && a.fit.sd[j] > 0.0 && a.fit.dof > 0) {
            const double t = std::fabs(p[j] - 1.0) / a.fit.sd[j];
            const double pt = kin::t_two_tailed_p(t, a.fit.dof);
            auto line = significance("The Hill coefficient", "1", t, a.fit.dof, pt);
            if (pt >= 0.05 && def.family == kin::Family::Hill)
                line.text += " The Michaelis-Menten equation may describe these data as well.";
            else if (pt >= 0.05 && def.family == kin::Family::Activation)
                line.text += " The activator equation with h = 1 may describe these data as well.";
            out.push_back(line);
        }
    }
    return out;
}

std::string join_names(const std::vector<std::string>& names) {
    std::string s;
    for (size_t i = 0; i < names.size(); ++i)
        s += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    return s;
}

// Whether a parameter's relative SD says how well the data determine it. An
// offset that may well be near zero (an activator's v0, vmin, the intercept,
// log10(A)) has a large relative SD however good the fit.
bool judged_by_relative_sd(kin::Role role, kin::Family family) {
    switch (role) {
    case kin::Role::Vmin:
    case kin::Role::Intercept:
    case kin::Role::LogA: return false;
    case kin::Role::V0: return family == kin::Family::Inhibition;
    default: return true;
    }
}

// What data would pin a poorly determined parameter down, from where the
// data lie relative to the half-saturation constant.
std::string precision_advice(const kin::Analysis& a, kin::Role role) {
    const auto m = a.model();
    const auto& def = kin::model_def(m);
    const std::string x(def.xName);
    const auto iK = kin::param_index(m, kin::Role::HalfSat);
    const std::string generic = "More points, or replicates, would help.";
    if (!iK) return generic;
    const double K = a.fit.p[static_cast<size_t>(*iK)];
    if (!(K > 0.0)) return generic;
    const std::string k(def.params[static_cast<size_t>(*iK)].name);
    const std::string times = " \xC3\x97 ";
    const double hi = a.xMax / K, lo = a.xMin / K;
    switch (role) {
    case kin::Role::HalfSat:
    case kin::Role::HillCoeff:
        if (hi < 2.0)
            return "The highest " + x + " is only " + kin::fmt_g(hi, 2) + times + k + ": points at higher " + x +
                   " would help.";
        if (lo > 0.5)
            return "The lowest " + x + " is already " + kin::fmt_g(lo, 2) + times + k + ": points at lower " + x +
                   " would help.";
        if (lo > 0.2 || hi < 5.0) return "More points between about 0.2 and 5" + times + k + " would help.";
        return generic;
    case kin::Role::Vmax:
        if (hi < 5.0)
            return "The highest " + x + " is " + kin::fmt_g(hi, 2) + times + k + ": points at higher " + x +
                   " (up to about 5" + times + k + ") would help.";
        return generic;
    case kin::Role::V0:
        if (lo > 0.5) return "Points at lower " + x + ", or at " + x + " = 0, would help.";
        return generic;
    default: return generic;
    }
}

}  // namespace

std::vector<VerdictLine> fit_verdict(const kin::Analysis& a) {
    std::vector<VerdictLine> bad, caution;
    const auto m = a.model();
    const auto& def = kin::model_def(m);
    const auto& f = a.fit;
    auto name = [&](size_t j) { return std::string(def.params[j].name); };

    if (!a.linear && f.status != kin::FitStatus::Converged)
        bad.push_back({VerdictLevel::Bad, "The fit did not converge, so these may not be the best values.",
                       f.message + " Try a higher iteration limit or other initial estimates (in the Fit section)."});

    // Parameters held at a bound: the data did not determine them.
    std::vector<bool> atBound(def.params.size(), false);
    for (const auto& h : a.parameters_at_bounds()) {
        const auto j = static_cast<size_t>(h.index);
        atBound[j] = true;
        const bool hillGe1 = def.params[j].role == kin::Role::HillCoeff && !h.upper && h.bound == 1.0 &&
                             (m == kin::ModelId::HillGe1 || m == kin::ModelId::ActivatorGe1);
        if (hillGe1) {
            caution.push_back({VerdictLevel::Caution, name(j) + " is at its lower limit of 1, so the data suggest h < 1.",
                               std::string("Try the ") + (m == kin::ModelId::HillGe1 ? "Hill" : "activator") +
                                   " equation with h > 0."});
            continue;
        }
        bad.push_back({VerdictLevel::Bad,
                       name(j) + " could not be determined: it reached its " + (h.upper ? "upper" : "lower") +
                           " limit of " + kin::fmt_g(h.bound, 3) + ".",
                       "This equation may not suit these data; try another, or change the limit (Fit section "
                       "> Show bounds)."});
    }

    // Standard deviations that could not be estimated, and those too large.
    std::vector<std::string> noSd, judged, loose, loosePct;
    double worst = 0.0;
    kin::Role worstRole = kin::Role::HalfSat;
    for (size_t j = 0; j < def.params.size() && j < f.p.size(); ++j) {
        if (f.fixed[j] || atBound[j]) continue;
        if (!std::isfinite(f.sd[j])) {
            noSd.push_back(name(j));
            continue;
        }
        if (!judged_by_relative_sd(def.params[j].role, def.family)) continue;
        judged.push_back(name(j));
        const double pct = f.p[j] != 0.0 ? std::fabs(f.sd[j] * 100.0 / f.p[j]) : INFINITY;
        if (pct > 10.0) {
            loose.push_back(name(j));
            loosePct.push_back(std::isfinite(pct) ? format_percent(pct) : std::string(">1000%"));
            if (pct > worst) {
                worst = pct;
                worstRole = def.params[j].role;
            }
        }
    }
    if (!noSd.empty())
        bad.push_back({VerdictLevel::Bad, "The SD of " + join_names(noSd) + " could not be estimated.",
                       "There are too few points, or the data cannot tell the parameters apart."});
    if (!loose.empty()) {
        const bool many = loose.size() > 1;
        const bool none = worst > 100.0;
        VerdictLine v{none ? VerdictLevel::Bad : VerdictLevel::Caution,
                      join_names(loose) + (many ? " are" : " is") +
                          (none ? " not determined by these data" : " poorly determined") + " (relative SD " +
                          join_names(loosePct) + ").",
                      precision_advice(a, worstRole)};
        (none ? bad : caution).push_back(v);
    } else if (const auto iK = kin::param_index(m, kin::Role::HalfSat); iK && !atBound[static_cast<size_t>(*iK)]) {
        // Precise, but only because the curve is extrapolated beyond the data.
        const auto jK = static_cast<size_t>(*iK);
        const double K = f.p[jK];
        const std::string x(def.xName), k = name(jK), times = " \xC3\x97 ";
        const auto iV = kin::param_index(m, kin::Role::Vmax);
        const std::string both = (iV && !f.fixed[static_cast<size_t>(*iV)]) ? k + " and Vmax are" : k + " is";
        const std::string advice = "Points from about 0.2 to 5" + times + k + " would help.";
        if (K > 0.0 && a.xMax < K)
            caution.push_back({VerdictLevel::Caution,
                               "Every " + x + " is below " + k + " (the highest is " + kin::fmt_g(a.xMax / K, 2) + times +
                                   k + "), so " + both + " extrapolated.",
                               advice});
        else if (K > 0.0 && a.xMin > K)
            caution.push_back({VerdictLevel::Caution,
                               "Every " + x + " is above " + k + " (the lowest is " + kin::fmt_g(a.xMin / K, 2) + times +
                                   k + "), so " + k + " is extrapolated.",
                               advice});
    }

    // Too few points, and residuals that show a pattern.
    if (kin::too_few_points(m, a.request.params, a.data.size()) && noSd.empty())
        caution.push_back({VerdictLevel::Caution,
                           "Only " + std::to_string(a.data.size()) + " points for " + std::to_string(f.nFree) +
                               " parameters, so the SDs are not reliable.",
                           "A reliable fit needs at least " + std::to_string(f.nFree + 2) + " points."});
    const auto& rt = a.residualTests;
    if ((rt.runsTestable && !rt.runsRandom) || !rt.signsRandom)
        caution.push_back({VerdictLevel::Caution, "The residuals show a pattern, so another equation may fit better.",
                           "See the residuals under the graph, and the randomness tests below it."});
    if (a.linear && a.linear->replicates && a.linear->dfDeviation > 0 && a.linear->ssPureError > 0.0 &&
        a.linear->pLackOfFit < 0.05)
        caution.push_back({VerdictLevel::Caution,
                           "The line misses the replicated points by more than their scatter (lack of fit).", ""});

    std::vector<VerdictLine> out = std::move(bad);
    out.insert(out.end(), caution.begin(), caution.end());
    if (out.empty()) {
        if (judged.empty())
            out.push_back({VerdictLevel::Good, "Fitted. The relative SD says little about the parameters fitted here, "
                                               "which may well be near zero.",
                           ""});
        else
            out.push_back({VerdictLevel::Good,
                           "Good fit: " + join_names(judged) + (judged.size() > 1 ? " are" : " is") +
                               " determined to within 10%.",
                           ""});
    }
    return out;
}

VerdictLevel worst_level(const std::vector<VerdictLine>& lines) {
    VerdictLevel w = VerdictLevel::Good;
    for (const auto& l : lines) w = std::max(w, l.level);
    return w;
}

namespace {

ImVec4 verdict_colour(VerdictLevel level) {
    switch (level) {
    case VerdictLevel::Good: return palette().good;
    case VerdictLevel::Caution: return palette().warning;
    case VerdictLevel::Bad: return palette().bad;
    }
    return palette().text;
}

// A verdict line: a coloured dot, the headline in its colour and any advice
// in plain text after it.
void draw_verdict(const VerdictLine& v) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec4 col = verdict_colour(v.level);
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + fs * 0.35f, p.y + fs * 0.55f), fs * 0.3f,
                                                ImGui::GetColorU32(col));
    ImGui::Indent(fs);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::TextWrapped("%s", v.headline.c_str());
    ImGui::PopStyleColor();
    if (!v.advice.empty()) ImGui::TextWrapped("%s", v.advice.c_str());
    ImGui::Unindent(fs);
}

}  // namespace

void App::draw_right_panel() {
    const bool haveImport = import_.type != ImportType::None;
    // Before anything is fitted or imported, a getting-started page instead
    // of tabs that would all be empty.
    if (!analysis_ && !haveImport) {
        requestedTab_.reset();
        if (ImGui::BeginChild("##start")) draw_getting_started();
        ImGui::EndChild();
        return;
    }
    // Results from data that have since changed are greyed out, with a note
    // saying why.
    // (For the file's runs, the banner above says so.)
    const bool stale = results_stale();
    if (stale && !showing_run()) {
        warning_text("The data or settings have changed since this fit. Press Fit (F5) to update the results.", "");
        ImGui::Spacing();
    }
    if (!ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_DrawSelectedOverline)) return;
    auto tab = [&](const char* label, Tab t, void (App::*fn)(), bool* open = nullptr) {
        const ImGuiTabItemFlags f = (requestedTab_ && *requestedTab_ == t) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(label, open, f)) {
            const bool dim = stale && t != Tab::FileRuns;
            if (dim) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, kStaleAlpha);
            if (ImGui::BeginChild("##tabcontent", ImVec2(0, 0), ImGuiChildFlags_None)) (this->*fn)();
            ImGui::EndChild();
            if (dim) ImGui::PopStyleVar();
            ImGui::EndTabItem();
        }
    };
    // Each tab appears once it has something to show.
    const bool haveFit = analysis_.has_value();
    bool keepFile = true;
    tab("Results", Tab::Results, &App::draw_results_tab);
    if (haveFit) tab("Standard curve", Tab::StandardCurve, &App::draw_standard_curve_tab);
    if (haveImport) tab("File runs", Tab::FileRuns, &App::draw_file_runs_tab, &keepFile);
    if (haveFit) tab("Report", Tab::Report, &App::draw_report_tab);
    requestedTab_.reset();
    ImGui::EndTabBar();
    if (!keepFile) close_import();  // the tab's close button
}

void App::draw_getting_started() {
    const auto& def = kin::model_def(model_);
    const std::string xName(def.xName), yName(def.yName);
    const size_t n = kin::clean_data(table_points()).size();
    const float fs = ImGui::GetFontSize();
    const float wrapWidth = fs * 32.0f;

    ImGui::Indent(fs);
    ImGui::Dummy(ImVec2(0.0f, fs * 0.75f));
    push_heading_font(1.35f);
    ImGui::TextUnformatted("Getting started");
    ImGui::PopFont();
    ImGui::Spacing();

    // A numbered step: the number in a circle (filled once done), a title and
    // a short dimmed explanation.
    auto step = [&](int number, const char* title, const std::string& body, bool done) {
        const float r = fs * 0.75f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 c(p.x + r, p.y + r);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImU32 accent = ImGui::GetColorU32(palette().accent);
        char num[4];
        std::snprintf(num, sizeof num, "%d", number);
        const ImVec2 ts = ImGui::CalcTextSize(num);
        if (done) {
            dl->AddCircleFilled(c, r, accent);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ImGui::GetColorU32(ImGuiCol_WindowBg), num);
        } else {
            dl->AddCircle(c, r, accent, 0, 1.5f);
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), accent, num);
        }
        ImGui::Dummy(ImVec2(r * 2.0f + fs * 0.5f, r * 2.0f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextUnformatted(title);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrapWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
        ImGui::TextUnformatted(body.c_str());
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0.0f, fs * 0.5f));
    };

    step(1, "Enter your data",
         n == 0 ? "Type " + xName + " and " + yName + " values into the table on the left, or paste two columns "
                  "copied from a spreadsheet."
                : std::to_string(n) + " point" + (n == 1 ? "" : "s") + " entered. You can edit them in the table at "
                  "any time.",
         n > 0);
    step(2, "Check the equation",
         "Currently: " + std::string(equation_name(model_)) + ". Michaelis-Menten suits most single-substrate data; "
         "the list says when to use each of the others.",
         false);
    step(3, "Press Fit (F5)",
         "The results give each parameter with its standard deviation and a verdict on the fit, above a graph of "
         "the curve and its residuals.",
         false);

    if (n == 0) {
        if (primary_button("Paste from clipboard")) paste_table_from_clipboard();
        ImGui::SetItemTooltip("Two columns of numbers copied from a spreadsheet");
        ImGui::SameLine();
        if (ImGui::Button("Open a data table...")) show_open_dialog(FilePurpose::OpenTable, default_dir());
        ImGui::SameLine();
        if (ImGui::Button("Try an example")) open_example(Example::MichaelisMenten);
        ImGui::SetItemTooltip("Loads ten Michaelis-Menten points. More examples: File > Open example.");
    } else {
        ImGui::TextColored(palette().accent, "Next: press Fit (F5), below the table.");
    }

    ImGui::Dummy(ImVec2(0.0f, fs * 1.5f));
    ImGui::TextDisabled("Have a file with many runs?");
    if (ImGui::SmallButton("Import spreadsheet file...")) show_open_dialog(FilePurpose::ImportSpreadsheet, default_dir());
    item_tooltip("Substrate concentrations and one row of velocities per run. Help > Using file data "
                 "describes the format.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Import MPA file...")) show_open_dialog(FilePurpose::ImportMpa, default_dir());
    item_tooltip("Velocities only, from the MicroPlate Analysis program. Help > Using file data "
                 "describes the format.");
    ImGui::Dummy(ImVec2(0.0f, fs * 0.5f));
    ImGui::TextDisabled("New to the program?");
    ImGui::SameLine();
    if (ImGui::TextLink("Read Getting started in the help (F1)")) open_help(HelpId::GettingStarted);
    ImGui::Unindent(fs);
}

void App::draw_results_tab() {
    const float fs = ImGui::GetFontSize();
    if (!analysis_) {
        if (const auto* item = shown_item()) {
            const bool sheet = import_.type == ImportType::Spreadsheet;
            ImGui::PushStyleColor(ImGuiCol_Text, palette().bad);
            ImGui::TextWrapped("%s #%d could not be fitted: %s", sheet ? "Run" : "Dataset", item->runNumber,
                               item->error.c_str());
            ImGui::PopStyleColor();
        } else {
            // Only reachable with an imported file: the other ways in need a fit.
            ImGui::TextDisabled("No results yet. Analyse the runs on the File runs tab, or press Fit (F5) to fit the "
                                "data table.");
        }
        return;
    }
    const auto& a = *analysis_;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float controlsH = ImGui::GetFrameHeightWithSpacing();
    const float minPlotH = fs * 15.0f;
    // On a wide panel the numbers sit beside the graph, otherwise above it;
    // either way the graph takes the height left over by the folded sections
    // below it (measured on the last frame).
    if (avail.x >= fs * 64.0f) {
        const float summaryW = std::clamp(avail.x * 0.3f, fs * 25.0f, fs * 30.0f);
        const float h = std::max(minPlotH + controlsH, avail.y - resultsFooterHeight_);
        if (ImGui::BeginChild("##summary", ImVec2(summaryW, h))) draw_results_summary(a, false);
        ImGui::EndChild();
        ImGui::SameLine(0.0f, fs);
        ImGui::BeginGroup();
        if (std::exchange(scrollToPlot_, false)) ImGui::SetScrollHereY(0.0f);
        draw_plot_controls();
        draw_plot(ImVec2(ImGui::GetContentRegionAvail().x, h - controlsH));
        ImGui::EndGroup();
    } else {
        draw_results_summary(a, avail.x >= fs * 40.0f);
        ImGui::Spacing();
        if (std::exchange(scrollToPlot_, false)) ImGui::SetScrollHereY(0.0f);
        draw_plot_controls();
        draw_plot(ImVec2(ImGui::GetContentRegionAvail().x,
                         std::max(minPlotH, ImGui::GetContentRegionAvail().y - resultsFooterHeight_)));
    }
    const float top = ImGui::GetCursorPosY();
    ImGui::Spacing();
    draw_residuals_section(a);
    draw_history_section(a);
    resultsFooterHeight_ = ImGui::GetCursorPosY() - top;
}

void App::draw_results_summary(const kin::Analysis& a, bool twoColumns) {
    // The verdict first, then the numbers, then how the fit went: beside the
    // numbers when there is room, which leaves more height for the graph.
    for (const auto& v : fit_verdict(a)) draw_verdict(v);
    ImGui::Spacing();
    if (twoColumns) {
        ImGui::BeginGroup();
        draw_parameter_values(a);
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, ImGui::GetFontSize() * 1.5f);
        ImGui::BeginGroup();
        draw_result_notes(a);
        ImGui::EndGroup();
        return;
    }
    draw_parameter_values(a);
    ImGui::Spacing();
    draw_result_notes(a);
}

void App::draw_result_notes(const kin::Analysis& a) {
    for (const auto& line : plain_interpretation(a)) {
        if (line.warn) ImGui::PushStyleColor(ImGuiCol_Text, palette().warning);
        ImGui::Bullet();
        ImGui::TextWrapped("%s", line.text.c_str());
        if (line.warn) ImGui::PopStyleColor();
    }
    // The notes the verdict above does not already cover (e.g. how a fixed
    // v0 was set); the report lists them all.
    for (const auto& note : a.notes) {
        if (note == a.fit.message || note.rfind("Only ", 0) == 0 || note.rfind("Some standard deviations", 0) == 0)
            continue;
        ImGui::Bullet();
        ImGui::TextWrapped("%s", note.c_str());
    }
    {
        const std::string sd = kin::fmt_g(a.fit.stdDev, 3);
        const char* method = a.fit.method == kin::Method::GaussNewton ? "Gauss-Newton" : "Marquardt-Levenberg";
        const int it = a.fit.iterations;
        const std::string label = plain_model_label(a.model(), a.request.params);
        const std::string unit = kin::model_def(a.model()).family == kin::Family::Arrhenius || yUnit_.empty()
                                     ? std::string()
                                     : " " + yUnit_;
        char buf[384];
        if (a.linear)
            std::snprintf(buf, sizeof buf, "%s, %d points. Residual SD %s%s.", label.c_str(), a.fit.n, sd.c_str(),
                          unit.c_str());
        else
            std::snprintf(buf, sizeof buf, "%s. %s %d iteration%s (%s), %d points. Residual SD %s%s.", label.c_str(),
                          a.fit.status == kin::FitStatus::Converged ? "Converged after" : "Stopped after", it,
                          it == 1 ? "" : "s", method, a.fit.n, sd.c_str(), unit.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
        ImGui::TextWrapped("%s", buf);
        ImGui::PopStyleColor();
        const int df = a.linear ? a.linear->df : a.fit.dof;
        item_tooltip("The residual SD is the typical distance of a point from the fitted curve (" +
                     kin::fmt_g(a.fit.stdDev) + "). " + std::to_string(a.fit.nFree) + " free parameter" +
                     (a.fit.nFree == 1 ? "" : "s") + ", " + std::to_string(df) + " degrees of freedom.");
    }
    if (ImGui::SmallButton("Copy results")) copy_results();
    ImGui::SetItemTooltip("Copy the parameters, with all digits, as a table to paste into a spreadsheet. The Report "
                          "tab has the full report.");
}

void App::draw_parameter_values(const kin::Analysis& a) {
    // The constants are the answer, so they get the larger type; the SD sits
    // beside each value and the relative SD says how well it is determined.
    const auto& def = kin::model_def(a.model());
    const auto rows = a.parameter_rows();
    // As wide as its columns, so that notes can stand beside it.
    if (!ImGui::BeginTable("##results", 6,
                           ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoHostExtendX))
        return;
    ImGui::TableSetupColumn("Parameter");
    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("\xC2\xB1 SD");
    ImGui::TableSetupColumn("Unit");
    ImGui::TableSetupColumn("Relative SD");
    ImGui::TableSetupColumn("");
    // A quiet heading row: dim text, no fill.
    ImGui::TableNextRow();
    for (int c = 0; c < 6; ++c) {
        ImGui::TableSetColumnIndex(c);
        if (c == 0 || c == 3 || c == 5) continue;
        ImGui::TextDisabled("%s", ImGui::TableGetColumnName(c));
        if (c == 4)
            ImGui::SetItemTooltip("The SD as a percentage of the value. Under about 10%%, the parameter is well "
                                  "determined by the data.");
    }
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const bool large = !r.derived;
        const auto t = format_value_sd(r.value, r.fixed ? NAN : r.sd);
        const double pct = r.value != 0.0 ? std::fabs(r.sd * 100.0 / r.value) : NAN;
        const std::string full = r.name + " = " + kin::fmt_g(r.value) +
                                 (r.fixed ? std::string(" (fixed)") : " \xC2\xB1 " + kin::fmt_g(r.sd));
        const std::string unit = i < def.params.size() ? unit_of(def.params[i].role, a.model()) : std::string();
        ImGui::TableNextRow();
        // The small-type cells sit on the large type's baseline.
        if (large) push_large_font();
        const float lineH = ImGui::GetTextLineHeight();
        if (large) ImGui::PopFont();
        const float drop = std::max(0.0f, (lineH - ImGui::GetTextLineHeight()) * 0.7f);
        auto small_cell = [&]() { ImGui::SetCursorPosY(ImGui::GetCursorPosY() + drop); };

        ImGui::TableNextColumn();
        small_cell();
        ImGui::TextUnformatted(r.name.c_str());
        ImGui::TableNextColumn();
        if (large) push_large_font();
        {
            // Right-aligned, so that the decimal points of the rows roughly line up.
            const float w = ImGui::CalcTextSize(t.value.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
            ImGui::TextUnformatted(t.value.c_str());
            ImGui::SetItemTooltip("%s", full.c_str());
        }
        ImGui::TableNextColumn();
        if (!r.fixed) {
            const std::string sd = t.sd.empty() ? "\xC2\xB1 n/a" : "\xC2\xB1 " + t.sd;
            ImGui::TextUnformatted(sd.c_str());
            ImGui::SetItemTooltip("%s", full.c_str());
        }
        if (large) ImGui::PopFont();
        ImGui::TableNextColumn();
        small_cell();
        if (!unit.empty()) ImGui::TextDisabled("%s", unit.c_str());
        ImGui::TableNextColumn();
        small_cell();
        if (!r.fixed && std::isfinite(pct)) {
            const std::string p = format_percent(pct);
            if (pct <= 10.0) ImGui::TextUnformatted(p.c_str());
            else ImGui::TextColored(palette().warning, "%s", p.c_str());
        }
        ImGui::TableNextColumn();
        small_cell();
        if (r.fixed) ImGui::TextDisabled("fixed");
        else if (r.derived) {
            ImGui::TextDisabled("derived");
            ImGui::SetItemTooltip("kcal/mol x 4.184");
        }
    }
    ImGui::EndTable();
}

void App::draw_residuals_section(const kin::Analysis& a) {
    const auto& def = kin::model_def(a.model());
    const std::string xName = x_heading(a.model()), yName = y_heading(a.model());
    const auto& rt = a.residualTests;
    const bool pattern = (rt.runsTestable && !rt.runsRandom) || !rt.signsRandom;
    // The header gives the outcome, so the section rarely needs opening.
    const std::string label = std::string("Residuals and randomness tests: ") +
                              (pattern ? "the residuals show a pattern"
                               : rt.runsTestable ? "the signs look random"
                                                 : "the signs are balanced") +
                              "###residuals";
    if (openResiduals_) ImGui::SetNextItemOpen(true);
    if (pattern) ImGui::PushStyleColor(ImGuiCol_Text, palette().warning);
    const bool open = ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
    if (pattern) ImGui::PopStyleColor();
    // Bring the opened section into view once the graph above has shrunk to
    // make room (a frame later), so the scroll is not cut short.
    if (std::exchange(openResiduals_, false) || (ImGui::IsItemToggledOpen() && open)) residualScrollFrames_ = 2;
    else if (residualScrollFrames_ > 0 && --residualScrollFrames_ == 0) ImGui::SetScrollHereY(0.0f);
    help_link(HelpId::Residuals);
    if (!open) return;

    // The two randomness tests, in plain words (the report keeps v3.51's).
    ImGui::Text("Signs of the residuals, in order of %s:  %s", std::string(def.xName).c_str(), rt.sequence.c_str());
    item_tooltip("One sign per distinct " + std::string(def.xName) + " (replicates are averaged). A good fit scatters "
                 "the points on both sides of the curve with no pattern.");
    const std::string another = " Another equation may fit better.";
    if (!rt.runsTestable)
        ImGui::TextDisabled("Runs test: needs at least 8 non-zero residuals.");
    else if (rt.runsRandom)
        ImGui::Text("Runs test: the signs look random (%d runs, %s).", rt.runs, p_text(rt.runsP).c_str());
    else
        warning_text(("Runs test: the signs follow a pattern (" + std::to_string(rt.runs) + " runs, " +
                      p_text(rt.runsP) + ")." + another).c_str(), "");
    item_tooltip("Too few runs of the same sign mean the curve misses the data systematically.");
    if (rt.signsRandom)
        ImGui::Text("Sign test: + and \xE2\x88\x92 residuals are balanced (%d +, %d \xE2\x88\x92, %s).", rt.positives,
                    rt.negatives, p_text(rt.signP).c_str());
    else
        warning_text(("Sign test: more " + std::string(rt.positives > rt.negatives ? "+ than \xE2\x88\x92" : "\xE2\x88\x92 than +") +
                      " residuals than chance allows (" + std::to_string(rt.positives) + " +, " +
                      std::to_string(rt.negatives) + " \xE2\x88\x92, " + p_text(rt.signP) + ")." + another).c_str(),
                     "");
    ImGui::Spacing();

    const std::string fitted = "Fitted " + std::string(def.yName);
    const int decimals = residual_decimals(a.fit.stdDev);
    const float fs = ImGui::GetFontSize();
    const float rowH = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().CellPadding.y * 2.0f;
    const float height = std::min(fs * 16.0f, rowH * static_cast<float>(a.data.size() + 1) + rowH * 0.5f);
    if (ImGui::BeginTable("##resid", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0.0f, height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, fs * 2.5f);
        ImGui::TableSetupColumn(xName.c_str());
        ImGui::TableSetupColumn(yName.c_str());
        ImGui::TableSetupColumn(fitted.c_str());
        ImGui::TableSetupColumn("Residual");
        ImGui::TableSetupColumn("Outlier?", ImGuiTableColumnFlags_WidthFixed, fs * 4.5f);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int c = 0; c < 6; ++c) {
            ImGui::TableSetColumnIndex(c);
            ImGui::TableHeader(ImGui::TableGetColumnName(c));
            if (c == 4) ImGui::SetItemTooltip("%s minus the fitted value", std::string(def.yName).c_str());
            if (c == 5)
                item_tooltip("* marks a residual of at least 2 residual SDs (" + kin::fmt_g(2.0 * a.fit.stdDev, 3) + ")");
        }
        for (size_t i = 0; i < a.data.size(); ++i) {
            const bool big = a.largeResidual[i];
            if (big) ImGui::PushStyleColor(ImGuiCol_Text, palette().warning);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%zu", i + 1);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kin::fmt_g(a.data[i].x).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kin::fmt_g(a.data[i].y).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_fixed(a.yhat[i], decimals).c_str());
            ImGui::SetItemTooltip("%s", kin::fmt_g(a.yhat[i]).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(format_fixed(a.resid[i], decimals, true).c_str());
            ImGui::SetItemTooltip("%s", kin::fmt_g(a.resid[i]).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(big ? "*" : "");
            if (big) ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
}

void App::draw_history_section(const kin::Analysis& a) {
    if (a.linear || !ImGui::CollapsingHeader("Initial estimates and iteration history")) return;
    const auto& def = kin::model_def(a.model());
    const int np = static_cast<int>(def.params.size());
    ImGui::Text("Initial estimates (%s):", a.request.guess == kin::GuessMode::Automatic ? "automatic" : "manual");
    for (int j = 0; j < np; ++j) {
        ImGui::SameLine();
        ImGui::Text("%s = %s", std::string(def.params[static_cast<size_t>(j)].name).c_str(),
                    kin::fmt_g(a.fit.initial[static_cast<size_t>(j)]).c_str());
    }
    const bool gn = a.fit.method == kin::Method::GaussNewton;
    if (ImGui::BeginTable("##history", 4 + np, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
                          ImVec2(0, ImGui::GetFontSize() * 16.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Iter.");
        ImGui::TableSetupColumn("Sum of squares");
        ImGui::TableSetupColumn(gn ? "Step" : "Lambda");
        ImGui::TableSetupColumn("Step");
        for (const auto& p : def.params) ImGui::TableSetupColumn(std::string(p.name).c_str());
        ImGui::TableHeadersRow();
        for (const auto& h : a.fit.history) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%d", h.iteration);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kin::fmt_g(h.sse, 8).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(h.iteration == 0 ? "" : kin::fmt_g(h.lambdaOrStep, 3).c_str());
            ImGui::TableNextColumn();
            if (h.iteration == 0) ImGui::TextDisabled("start");
            else if (h.accepted) ImGui::TextUnformatted("accepted");
            else ImGui::TextDisabled("rejected");
            for (int j = 0; j < np; ++j) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(kin::fmt_g(h.p[static_cast<size_t>(j)]).c_str());
            }
        }
        ImGui::EndTable();
    }
}

void App::draw_standard_curve_tab() {
    if (!analysis_) {
        no_results_text();
        return;
    }
    const auto& a = *analysis_;
    const auto& def = kin::model_def(a.model());
    const std::string xName = x_heading(a.model()), yName = y_heading(a.model());
    const std::string xShort(def.xName), yShort(def.yName);
    const std::string xRange = "Data range: " + xShort + " from " + kin::fmt_g(a.xMin) + " to " + kin::fmt_g(a.xMax);
    const std::string yRange = "Data range: " + yShort + " from " + kin::fmt_g(a.yMin) + " to " + kin::fmt_g(a.yMax);
    const float w = ImGui::GetFontSize() * 9.0f;

    // Fixed-width labels so that the two input boxes line up.
    const float labelWidth = std::max(ImGui::CalcTextSize(xName.c_str()).x, ImGui::CalcTextSize(yName.c_str()).x) +
                             ImGui::GetStyle().ItemSpacing.x;
    auto label = [&](const std::string& text) {
        const float x = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text.c_str());
        ImGui::SameLine(x + labelWidth);
    };

    section_heading("Find " + xShort + " for a given " + yShort, true);
    help_link(HelpId::StandardCurve);
    if (focusStdX_) {
        ImGui::SetKeyboardFocusHere();
        focusStdX_ = false;
    }
    label(yName);
    ImGui::SetNextItemWidth(w);
    bool go = ImGui::InputDouble("##stdY", &stdInputY_, 0, 0, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue);
    item_tooltip(yRange + ". Shortcut: Alt+X.");
    ImGui::SameLine();
    go |= ImGui::Button("Calculate##x");
    if (go) {
        StdCurveEntry e;
        e.xFromY = true;
        e.input = stdInputY_;
        e.output = kin::inverse(a.model(), stdInputY_, a.fit.p);
        e.outOfRange = stdInputY_ < a.yMin || stdInputY_ > a.yMax;
        stdCurve_.insert(stdCurve_.begin(), e);
    }

    section_heading("Find " + yShort + " for a given " + xShort);
    if (focusStdY_) {
        ImGui::SetKeyboardFocusHere();
        focusStdY_ = false;
    }
    label(xName);
    ImGui::SetNextItemWidth(w);
    go = ImGui::InputDouble("##stdX", &stdInputX_, 0, 0, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue);
    item_tooltip(xRange + ". Shortcut: Alt+Y.");
    ImGui::SameLine();
    go |= ImGui::Button("Calculate##y");
    if (go) {
        StdCurveEntry e;
        e.xFromY = false;
        e.input = stdInputX_;
        const double y = kin::evaluate(a.model(), stdInputX_, a.fit.p);
        if (std::isfinite(y)) e.output = y;
        e.outOfRange = stdInputX_ < a.xMin || stdInputX_ > a.xMax;
        stdCurve_.insert(stdCurve_.begin(), e);
    }

    if (stdCurve_.empty()) return;
    section_heading("Results");
    if (ImGui::SmallButton("Clear")) stdCurve_.clear();
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        std::string t = "entered\tcalculated\n";
        for (const auto& e : stdCurve_)
            t += (e.xFromY ? yShort : xShort) + " = " + kin::fmt_g(e.input, 10) + "\t" + (e.xFromY ? xShort : yShort) +
                 " = " + (e.output ? kin::fmt_g(*e.output, 10) : std::string("n/a")) + "\n";
        ImGui::SetClipboardText(t.c_str());
        info("Copied the standard-curve values to the clipboard.");
    }
    if (ImGui::BeginTable("##std", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Entered");
        ImGui::TableSetupColumn("Calculated");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        const std::string xu = xUnit_.empty() || def.family == kin::Family::Arrhenius ? "" : " " + xUnit_;
        const std::string yu = yUnit_.empty() || def.family == kin::Family::Arrhenius ? "" : " " + yUnit_;
        for (const auto& e : stdCurve_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s = %s%s", (e.xFromY ? yShort : xShort).c_str(), kin::fmt_g(e.input).c_str(),
                        (e.xFromY ? yu : xu).c_str());
            ImGui::TableNextColumn();
            if (e.output)
                ImGui::Text("%s = %s%s", (e.xFromY ? xShort : yShort).c_str(), kin::fmt_g(*e.output).c_str(),
                            (e.xFromY ? xu : yu).c_str());
            else ImGui::TextColored(palette().bad, "cannot be calculated");
            ImGui::TableNextColumn();
            if (e.outOfRange)
                ImGui::TextColored(palette().bad, "%s is outside the range of the data",
                                   (e.xFromY ? yShort : xShort).c_str());
        }
        ImGui::EndTable();
    }
}

void App::draw_report_tab() {
    if (!analysis_) {
        no_results_text();
        return;
    }
    if (primary_button("Write report")) write_report();
    item_tooltip(report_file_tooltip());
    ImGui::SameLine();
    if (ImGui::Button("Copy to clipboard")) copy_report();
    ImGui::SetItemTooltip("Copy this report, with all digits, to the clipboard");
    if (!report_.status.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", report_.status.c_str());
    }

    // The options, folded under a one-line summary (File > Report options
    // opens them here).
    ImGui::Spacing();
    if (openReportSection_) {
        ImGui::SetNextItemOpen(true);
        openReportSection_ = false;
    }
    const bool open = ImGui::CollapsingHeader("Report options", ImGuiTreeNodeFlags_AllowOverlap);
    help_link(HelpId::Reports);
    if (open) {
        draw_report_options_form();
        ImGui::Spacing();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
        ImGui::TextWrapped("%s", report_options_summary().c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Spacing();
    std::string text = current_report_text(false);
    if (fonts().mono) ImGui::PushFont(fonts().mono, 0.0f);
    ImGui::InputTextMultiline("##preview", &text, ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);
    if (fonts().mono) ImGui::PopFont();
}

}  // namespace app
