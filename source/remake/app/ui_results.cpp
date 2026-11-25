// Right panel: the Results tab (verdict, parameters, the Calculate box,
// graph and residuals on one screen; v3.51 View menu and Calculate > new X /
// new Y) and the Report tab.

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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
                       f.message + " Try a higher iteration limit or other initial estimates (Fitting options)."});

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
                       "This equation may not suit these data; try another, or change the limit (Fitting options "
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
    // Results fitted with an equation or settings that have changed since
    // are greyed out, with a note saying why. (A change to the data clears
    // the results instead.)
    const bool stale = results_stale();
    if (stale) {
        warning_text("The equation or fit settings have changed since this fit. Press Fit (F5) to update the "
                     "results.",
                     "");
        ImGui::Spacing();
    }
    if (!ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_DrawSelectedOverline)) return;
    auto tab = [&](const char* label, Tab t, void (App::*fn)(), bool* open = nullptr) {
        const ImGuiTabItemFlags f = (requestedTab_ && *requestedTab_ == t) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(label, open, f)) {
            const bool dim = stale && t == Tab::Results;
            if (dim) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, kStaleAlpha);
            if (ImGui::BeginChild("##tabcontent", ImVec2(0, 0), ImGuiChildFlags_None)) (this->*fn)();
            ImGui::EndChild();
            if (dim) ImGui::PopStyleVar();
            ImGui::EndTabItem();
        }
    };
    // The runs first, where the work with several of them starts; the
    // results of the run shown (or how to get started); and the report once
    // there are results to report.
    tab("Runs", Tab::Runs, &App::draw_runs_tab);
    tab("Results", Tab::Results, &App::draw_results_tab);
    if (any_results()) tab("Report", Tab::Report, &App::draw_report_tab);
    requestedTab_.reset();
    ImGui::EndTabBar();
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
        // Wrapped to the panel when it is narrower than the usual width.
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(wrapWidth, ImGui::GetContentRegionAvail().x - fs));
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

    // Buttons and links go onto the next line when the panel is narrow.
    if (n == 0) {
        if (primary_button("Paste from clipboard")) paste_table_from_clipboard();
        ImGui::SetItemTooltip("Two columns of numbers copied from a spreadsheet");
        same_line_if_fits(button_width("Open a data table..."));
        if (ImGui::Button("Open a data table...")) show_open_dialog(FilePurpose::OpenTable, default_dir());
        same_line_if_fits(button_width("Try an example"));
        if (ImGui::Button("Try an example")) open_example(Example::MichaelisMenten);
        ImGui::SetItemTooltip("Loads ten Michaelis-Menten points. More examples: File > Open example.");
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, palette().accent);
        ImGui::TextWrapped("Next: press Fit (F5), under Fitting options on the left.");
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0.0f, fs * 1.5f));
    ImGui::TextDisabled("Have a file with many runs?");
    if (ImGui::SmallButton("Import spreadsheet of runs...")) show_open_dialog(FilePurpose::ImportRuns, default_dir());
    item_tooltip("A table saved as CSV or text, with the concentrations in the first column and a run in each "
                 "further column. Help > Runs and files describes the formats. Or add runs by hand on the Runs tab.");
    same_line_if_fits(button_width("Try an example file"));
    if (ImGui::SmallButton("Try an example file")) open_example(Example::HillRuns);
    ImGui::SetItemTooltip("Four runs of an allosteric enzyme, one per column");
    ImGui::Dummy(ImVec2(0.0f, fs * 0.5f));
    ImGui::TextDisabled("New to the program?");
    const char* link = "Read Getting started in the help (F1)";
    same_line_if_fits(ImGui::CalcTextSize(link).x);
    if (ImGui::TextLink(link)) open_help(HelpId::GettingStarted);
    ImGui::Unindent(fs);
}

void App::draw_results_tab() {
    const float fs = ImGui::GetFontSize();
    if (!analysis_) {
        // Why the run's last fit failed, then how to get started.
        const auto& failure = runs_[static_cast<size_t>(current_)].error;
        if (!failure.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, palette().bad);
            ImGui::TextWrapped("%s could not be fitted: %s", run_label(current_).c_str(), failure.c_str());
            ImGui::PopStyleColor();
        }
        draw_getting_started();
        return;
    }
    const auto& a = *analysis_;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float controlsH = ImGui::GetFrameHeightWithSpacing();
    // On a wide panel the numbers sit beside the graph, otherwise above it.
    // Either way the graph takes the height left over by the folded sections
    // below it (measured on the last frame), but never less than its width
    // calls for (the tab scrolls instead).
    if (avail.x >= fs * 64.0f) {
        const float summaryW = std::clamp(avail.x * 0.3f, fs * 25.0f, fs * 30.0f);
        const float plotW = avail.x - summaryW - fs;
        const float plotH = std::max(avail.y - resultsFooterHeight_ - controlsH, plot_min_height(plotW));
        if (ImGui::BeginChild("##summary", ImVec2(summaryW, plotH + controlsH))) draw_results_summary(a, false);
        ImGui::EndChild();
        ImGui::SameLine(0.0f, fs);
        ImGui::BeginGroup();
        if (std::exchange(scrollToPlot_, false)) ImGui::SetScrollHereY(0.0f);
        draw_plot_controls();
        draw_plot(ImVec2(ImGui::GetContentRegionAvail().x, plotH));
        ImGui::EndGroup();
    } else {
        draw_results_summary(a, avail.x >= fs * 40.0f);
        ImGui::Spacing();
        if (std::exchange(scrollToPlot_, false)) ImGui::SetScrollHereY(0.0f);
        draw_plot_controls();
        const float w = ImGui::GetContentRegionAvail().x;
        draw_plot(ImVec2(w, std::max(ImGui::GetContentRegionAvail().y - resultsFooterHeight_, plot_min_height(w))));
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
    const float fs = ImGui::GetFontSize();
    const float gap = fs * 1.5f;
    const float boxW = prediction_box_width(a);  // the Calculate box
    if (twoColumns) {
        ImGui::BeginGroup();
        draw_parameter_values(a);
        ImGui::EndGroup();
        ImGui::SameLine(0.0f, gap);
        // The notes, and the Calculate box beside them when there is room
        // for both; otherwise the box goes below.
        const float notesW = ImGui::GetContentRegionAvail().x;
        const bool boxBeside = notesW >= fs * 14.0f + gap + boxW;
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + (boxBeside ? notesW - gap - boxW : notesW));
        draw_result_notes(a);
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        if (boxBeside) {
            same_line_right(boxW);
            draw_prediction_box(a, boxW);
        } else {
            ImGui::Spacing();
            draw_prediction_box(a, std::min(boxW, ImGui::GetContentRegionAvail().x));
        }
        return;
    }
    draw_parameter_values(a);
    ImGui::Spacing();
    draw_result_notes(a);
    ImGui::Spacing();
    draw_prediction_box(a, std::min(boxW, ImGui::GetContentRegionAvail().x));
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
}

void App::draw_parameter_values(const kin::Analysis& a) {
    // The constants are the answer, so the table is in the larger type, all of
    // it: each value, its SD, and the relative SD, which says how well the
    // data determine it. Lines between the rows hold it together as a table.
    const auto& def = kin::model_def(a.model());
    const auto rows = a.parameter_rows();
    std::vector<std::string> units(rows.size());
    bool anyUnit = false, anyTag = false;
    for (size_t i = 0; i < rows.size(); ++i) {
        if (i < def.params.size()) units[i] = unit_of(def.params[i].role, a.model());
        anyUnit = anyUnit || !units[i].empty();
        anyTag = anyTag || rows[i].fixed || rows[i].derived;
    }
    // Columns only for what there is: no gap for units nobody entered.
    const int cols = 4 + (anyUnit ? 1 : 0) + (anyTag ? 1 : 0);
    const float fs = ImGui::GetFontSize();
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(fs * 0.45f, fs * 0.2f));
    // As wide as its columns, so that notes can stand beside it.
    const bool open = ImGui::BeginTable("##results", cols,
                                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX |
                                            ImGuiTableFlags_NoHostExtendX | ImGuiTableFlags_BordersInnerH);
    if (!open) {
        ImGui::PopStyleVar();
        return;
    }
    ImGui::TableSetupColumn("Parameter");
    ImGui::TableSetupColumn("Value");
    ImGui::TableSetupColumn("\xC2\xB1 SD");
    if (anyUnit) ImGui::TableSetupColumn("Unit");
    ImGui::TableSetupColumn("Relative SD");
    if (anyTag) ImGui::TableSetupColumn("");

    // Right-aligned in its cell, so that the decimal points of the rows
    // roughly line up.
    auto right_aligned = [](const char* s) {
        const float w = ImGui::CalcTextSize(s).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - w));
    };
    // A quiet heading row: dim text in the normal type, no fill.
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    right_aligned("Value");
    ImGui::TextDisabled("Value");
    ImGui::TableNextColumn();
    ImGui::TextDisabled("\xC2\xB1 SD");
    if (anyUnit) ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    right_aligned("Relative SD");
    ImGui::TextDisabled("Relative SD");
    ImGui::SetItemTooltip("The SD as a percentage of the value. Under about 10%%, the parameter is well "
                          "determined by the data.");

    push_large_font();
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        const auto t = format_value_sd(r.value, r.fixed ? NAN : r.sd);
        const double pct = r.value != 0.0 ? std::fabs(r.sd * 100.0 / r.value) : NAN;
        const std::string full = r.name + " = " + kin::fmt_g(r.value) +
                                 (r.fixed ? std::string(" (fixed)") : " \xC2\xB1 " + kin::fmt_g(r.sd));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(r.name.c_str());
        ImGui::TableNextColumn();
        right_aligned(t.value.c_str());
        ImGui::TextUnformatted(t.value.c_str());
        ImGui::SetItemTooltip("%s", full.c_str());
        ImGui::TableNextColumn();
        if (!r.fixed) {
            const std::string sd = t.sd.empty() ? "\xC2\xB1 n/a" : "\xC2\xB1 " + t.sd;
            ImGui::TextUnformatted(sd.c_str());
            ImGui::SetItemTooltip("%s", full.c_str());
        }
        if (anyUnit) {
            ImGui::TableNextColumn();
            if (!units[i].empty()) ImGui::TextDisabled("%s", units[i].c_str());
        }
        ImGui::TableNextColumn();
        if (!r.fixed && std::isfinite(pct)) {
            const std::string p = format_percent(pct);
            right_aligned(p.c_str());
            if (pct <= 10.0) ImGui::TextUnformatted(p.c_str());
            else ImGui::TextColored(palette().warning, "%s", p.c_str());
        }
        if (anyTag) {
            ImGui::TableNextColumn();
            if (r.fixed) ImGui::TextDisabled("fixed");
            else if (r.derived) {
                ImGui::TextDisabled("derived");
                ImGui::SetItemTooltip("kcal/mol x 4.184");
            }
        }
    }
    ImGui::PopFont();
    ImGui::EndTable();
    ImGui::PopStyleVar();
}

void App::draw_residuals_section(const kin::Analysis& a) {
    const auto& def = kin::model_def(a.model());
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

    // Points far from the curve (the residual values themselves are in the
    // graph's tooltips and in Copy results).
    std::string far;
    int nFar = 0;
    for (size_t i = 0; i < a.data.size() && i < a.largeResidual.size(); ++i)
        if (a.largeResidual[i]) {
            far += (far.empty() ? "" : ", ") + std::to_string(i + 1);
            ++nFar;
        }
    const std::string twoSd = kin::fmt_g(2.0 * a.fit.stdDev, 3);
    if (nFar == 0)
        ImGui::Text("No point is 2 residual SDs (%s) or more from the curve.", twoSd.c_str());
    else
        warning_text(((nFar == 1 ? "Point " : "Points ") + far + (nFar == 1 ? " is" : " are") + " 2 residual SDs (" +
                      twoSd + ") or more from the curve: check " + (nFar == 1 ? "it" : "them") + " for mistakes.")
                         .c_str(),
                     "Shown in amber in the strip of residuals under the graph.");
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

std::optional<std::pair<double, double>> App::calculated_point(const kin::Analysis& a) const {
    double v = 0.0;
    if (parse_cell(calcInput_, v) != CellState::Number) return std::nullopt;
    if (calcInputIsX_) {
        if (const double y = kin::evaluate(a.model(), v, a.fit.p); std::isfinite(y)) return std::pair{v, y};
        return std::nullopt;
    }
    const auto x = kin::inverse(a.model(), v, a.fit.p);
    if (!x || !std::isfinite(*x)) return std::nullopt;
    // A concentration below zero is the formula's other branch, not a point
    // on the curve: v at or above Vmax, say, is never reached. (Temperatures
    // and the straight line's X can be negative.)
    const auto family = kin::model_def(a.model()).family;
    if (*x < 0.0 && family != kin::Family::Arrhenius && family != kin::Family::Linear) return std::nullopt;
    return std::pair{*x, v};
}

// The Calculate box's one row, "[S] [  ]  ->  v [  ]": its labels and sizes,
// for both the box's width and its drawing.
struct CalcRow {
    std::string xLabel, yLabel;
    float labels = 0.0f;  // both labels, with the space after each
    float arrow = 0.0f;   // the arrow between the fields, with its margins
    float field = 0.0f;   // each field, at its full width
    float width() const { return labels + arrow + 2.0f * field; }
};

static CalcRow calc_row(const kin::Analysis& a, const std::string& xUnit, const std::string& yUnit) {
    const auto& def = kin::model_def(a.model());
    const bool arrhenius = def.family == kin::Family::Arrhenius;
    auto label = [&](std::string_view name, const std::string& unit) {
        return std::string(name) + (unit.empty() || arrhenius ? std::string() : " (" + unit + ")");
    };
    const float fs = ImGui::GetFontSize(), sp = ImGui::GetStyle().ItemSpacing.x;
    CalcRow r;
    r.xLabel = label(def.xName, xUnit);
    r.yLabel = label(def.yName, yUnit);
    r.labels = ImGui::CalcTextSize(r.xLabel.c_str()).x + ImGui::CalcTextSize(r.yLabel.c_str()).x + 2.0f * sp;
    r.arrow = fs * 2.4f;
    r.field = fs * 5.0f;
    return r;
}

float App::prediction_box_width(const kin::Analysis& a) const {
    const float fs = ImGui::GetFontSize();
    return std::max(calc_row(a, xUnit_, yUnit_).width(), fs * 12.0f) + 2.0f * ImGui::GetStyle().WindowPadding.x;
}

void App::draw_prediction_box(const kin::Analysis& a, float width) {
    const auto& def = kin::model_def(a.model());
    const std::string xName(def.xName), yName(def.yName);
    const float fs = ImGui::GetFontSize();
    const CalcRow row = calc_row(a, xUnit_, yUnit_);
    const auto pt = calculated_point(a);
    double typedValue = 0.0;
    const CellState state = parse_cell(calcInput_, typedValue);

    // A lightly tinted, rounded box, so that it reads as a tool of its own.
    const ImVec4 tint = palette().accent;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(tint.x, tint.y, tint.z, 0.07f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(tint.x, tint.y, tint.z, 0.45f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, fs * 0.35f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    const bool visible = ImGui::BeginChild("##predict", ImVec2(width, 0.0f),
                                           ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY |
                                               ImGuiChildFlags_AlwaysUseWindowPadding,
                                           ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    if (visible) {
        push_heading_font();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Calculate");
        ImGui::PopFont();
        help_link(HelpId::StandardCurve);

        // Two fields, X and Y, like a unit converter: type in either, and the
        // other shows the value read off the fitted curve, in the colour of
        // the cross that marks the point on the graph. The arrow points from
        // the value typed to the one calculated.
        const float field = std::clamp((ImGui::GetContentRegionAvail().x - row.labels - row.arrow) / 2.0f, fs * 3.0f,
                                       row.field);
        auto draw_field = [&](bool isX) {
            const std::string& label = isX ? row.xLabel : row.yLabel;
            const std::string& name = isX ? xName : yName;
            const std::string& other = isX ? yName : xName;
            const bool typed = !calcInput_.empty() && calcInputIsX_ == isX;
            const bool answer = !typed && pt.has_value();
            const double value = answer ? (isX ? pt->first : pt->second) : NAN;
            std::string text = typed ? calcInput_ : answer ? kin::fmt_g(value, 4) : std::string();
            ImGui::PushID(isX ? "x" : "y");
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label.c_str());
            ImGui::SameLine();
            if (std::exchange(isX ? focusCalcX_ : focusCalcY_, false)) ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(field);
            if (answer) ImGui::PushStyleColor(ImGuiCol_Text, prediction_colour());
            if (ImGui::InputText("##value", &text, ImGuiInputTextFlags_CharsScientific)) {
                calcInput_ = text;
                calcInputIsX_ = isX;
            }
            if (answer) ImGui::PopStyleColor();
            item_tooltip((answer ? name + " = " + kin::fmt_g(value, 10) + ", read off the fitted curve. " : std::string()) +
                         "Type " + name + " here to read " + other + " off the curve (" + (isX ? "Alt+Y" : "Alt+X") +
                         "). The data's " + name + " runs from " + kin::fmt_g(isX ? a.xMin : a.yMin) + " to " +
                         kin::fmt_g(isX ? a.xMax : a.yMax) + ".");
            ImGui::PopID();
        };

        // The arrow: from the field typed in to the other, or both ways
        // before anything is typed.
        auto draw_arrow = [&]() {
            const int dir = calcInput_.empty() ? 0 : calcInputIsX_ ? 1 : -1;
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float h = ImGui::GetFrameHeight();
            const float cy = std::round(p.y + h * 0.5f) + 0.5f;
            const float x0 = p.x + fs * 0.55f, x1 = p.x + row.arrow - fs * 0.55f, head = fs * 0.3f;
            const ImU32 ink = ImGui::GetColorU32(dir == 0 ? palette().textDim : palette().text);
            const float thick = std::max(1.0f, fs * 0.09f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), ink, thick);
            if (dir >= 0) {
                dl->AddLine(ImVec2(x1 - head, cy - head), ImVec2(x1, cy), ink, thick);
                dl->AddLine(ImVec2(x1 - head, cy + head), ImVec2(x1, cy), ink, thick);
            }
            if (dir <= 0) {
                dl->AddLine(ImVec2(x0 + head, cy - head), ImVec2(x0, cy), ink, thick);
                dl->AddLine(ImVec2(x0 + head, cy + head), ImVec2(x0, cy), ink, thick);
            }
            ImGui::Dummy(ImVec2(row.arrow, h));
        };

        draw_field(true);
        ImGui::SameLine(0.0f, 0.0f);
        draw_arrow();
        ImGui::SameLine(0.0f, 0.0f);
        draw_field(false);

        // One line under the fields, always there so that the box keeps its
        // height: what to do, what went wrong, or what the answer is.
        const std::string& typedName = calcInputIsX_ ? xName : yName;
        const double lo = calcInputIsX_ ? a.xMin : a.yMin, hi = calcInputIsX_ ? a.xMax : a.yMax;
        std::string note;
        ImVec4 col = palette().textDim;
        if (state == CellState::Blank) {
            note = "Type in either field to read the other off the fitted curve.";
        } else if (state == CellState::Invalid) {
            note = "Not a number.";
            col = palette().bad;
        } else if (!pt) {
            note = "The fitted curve does not reach " + typedName + " = " + calcInput_ + ".";
            col = palette().bad;
        } else if (typedValue < lo || typedValue > hi) {
            note = "Extrapolated: the data's " + typedName + " runs from " + kin::fmt_g(lo) + " to " + kin::fmt_g(hi) + ".";
            col = palette().warning;
        } else {
            note = "Read off the fitted curve, and marked on the graph by a cross.";
        }
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextWrapped("%s", note.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

void App::draw_report_tab() {
    if (!any_results()) {
        no_results_text();
        return;
    }
    if (primary_button("Write report")) write_report();
    item_tooltip(report_file_tooltip());
    ImGui::SameLine();
    if (ImGui::Button("Copy to clipboard")) copy_report();
    ImGui::SetItemTooltip("Copy the report, with all digits, to the clipboard");
    if (!report_.status.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", report_.status.c_str());
    }

    // The options, folded under a one-line summary (Alt+O opens them here).
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
