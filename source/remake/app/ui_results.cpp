// Right panel: results, residuals, standard curve and report tabs
// (v3.51 View menu and Calculate > new X / new Y).

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
    const bool stale = results_stale();
    if (stale) {
        warning_text("The data or settings have changed since this fit. Press Fit (F5) to update the results.", "");
        ImGui::Spacing();
    }
    if (!ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_DrawSelectedOverline)) return;
    auto tab = [&](const char* label, Tab t, void (App::*fn)(), bool* open = nullptr) {
        const ImGuiTabItemFlags f = (requestedTab_ && *requestedTab_ == t) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(label, open, f)) {
            const bool dim = stale && t != Tab::FileRuns;
            if (dim) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, kStaleAlpha);
            if (ImGui::BeginChild("##tabcontent", ImVec2(0, 0), ImGuiChildFlags_None,
                                  t == Tab::Graph ? ImGuiWindowFlags_NoScrollbar : 0))
                (this->*fn)();
            ImGui::EndChild();
            if (dim) ImGui::PopStyleVar();
            ImGui::EndTabItem();
        }
    };
    // Each tab appears once it has something to show.
    const bool haveFit = analysis_.has_value();
    bool keepFile = true;
    tab("Results", Tab::Results, &App::draw_results_tab);
    if (haveFit) {
        tab("Graph", Tab::Graph, &App::draw_graph_tab);
        tab("Residuals", Tab::Residuals, &App::draw_residuals_tab);
        tab("Standard curve", Tab::StandardCurve, &App::draw_standard_curve_tab);
    }
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
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
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
         "Currently: " + std::string(def.menuName) + ". The Michaelis-Menten equation suits most single-substrate "
         "data; Help > Choosing an equation describes the others.",
         false);
    step(3, "Press Fit (F5)",
         "The results give each parameter with its standard deviation, then a graph and the residuals.", false);

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
    ImGui::Unindent(fs);
}

void App::draw_results_tab() {
    if (!analysis_) {
        // Only reachable with an imported file: the other ways in need a fit.
        ImGui::TextDisabled("No results yet. Fit a run on the File runs tab, or press Fit (F5) to fit the data table.");
        return;
    }
    const auto& a = *analysis_;
    // A run from the file analysis is not the table's data, so say which.
    if (batch_.selected >= 0 && analysisRun_)
        ImGui::Text("Run #%d of %s", *analysisRun_, import_.path.filename().string().c_str());

    // What was fitted, and how the fit went.
    ImGui::TextUnformatted(plain_model_label(a.model(), a.request.params).c_str());
    if (!a.linear && a.fit.status != kin::FitStatus::Converged) warning_text(a.fit.message.c_str(), "");
    {
        const std::string sd = kin::fmt_g(a.fit.stdDev, 3);
        const char* method = a.fit.method == kin::Method::GaussNewton ? "Gauss-Newton" : "Marquardt-Levenberg";
        const int it = a.fit.iterations;
        char buf[256];
        if (a.linear)
            std::snprintf(buf, sizeof buf, "Linear regression, %d points. Residual SD %s.", a.fit.n, sd.c_str());
        else
            std::snprintf(buf, sizeof buf, "%s %d iteration%s (%s), %d points. Residual SD %s.",
                          a.fit.status == kin::FitStatus::Converged ? "Converged after" : "Stopped after", it,
                          it == 1 ? "" : "s", method, a.fit.n, sd.c_str());
        ImGui::TextDisabled("%s", buf);
        const int df = a.linear ? a.linear->df : a.fit.dof;
        item_tooltip("The residual SD is the typical distance of a point from the fitted curve (" +
                     kin::fmt_g(a.fit.stdDev) + "). " + std::to_string(a.fit.nFree) + " free parameter" +
                     (a.fit.nFree == 1 ? "" : "s") + ", " + std::to_string(df) + " degrees of freedom.");
    }

    ImGui::SeparatorText("Best-fit parameters");
    if (ImGui::BeginTable("##results", 5, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_SizingFixedFit)) {
        const float fs = ImGui::GetFontSize();
        ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, fs * 9.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, fs * 7.0f);
        ImGui::TableSetupColumn("\xC2\xB1 SD", ImGuiTableColumnFlags_WidthFixed, fs * 6.0f);
        ImGui::TableSetupColumn("Relative SD", ImGuiTableColumnFlags_WidthFixed, fs * 6.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int c = 0; c < 5; ++c) {
            ImGui::TableSetColumnIndex(c);
            ImGui::TableHeader(ImGui::TableGetColumnName(c));
            if (c == 3)
                ImGui::SetItemTooltip("The SD as a percentage of the value. Under about 10%%, the parameter is well "
                                      "determined by the data.");
        }
        for (const auto& r : a.parameter_rows()) {
            const auto t = format_value_sd(r.value, r.fixed ? NAN : r.sd);
            const double pct = r.value != 0.0 ? std::fabs(r.sd * 100.0 / r.value) : NAN;
            const std::string full = r.name + " = " + kin::fmt_g(r.value) +
                                     (r.fixed ? std::string(" (fixed)") : " \xC2\xB1 " + kin::fmt_g(r.sd));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(t.value.c_str());
            ImGui::SetItemTooltip("%s", full.c_str());
            ImGui::TableNextColumn();
            if (!r.fixed) {
                ImGui::TextUnformatted(t.sd.empty() ? "n/a" : t.sd.c_str());
                ImGui::SetItemTooltip("%s", full.c_str());
            }
            ImGui::TableNextColumn();
            if (!r.fixed && std::isfinite(pct)) {
                const std::string p = format_percent(pct);
                if (pct <= 10.0) ImGui::TextUnformatted(p.c_str());
                else ImGui::TextColored(palette().warning, "%s", p.c_str());
            }
            ImGui::TableNextColumn();
            if (r.fixed) ImGui::TextDisabled("fixed");
            else if (r.derived) {
                ImGui::TextDisabled("derived");
                ImGui::SetItemTooltip("kcal/mol x 4.184");
            }
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    for (const auto& line : plain_interpretation(a)) {
        if (line.warn) ImGui::PushStyleColor(ImGuiCol_Text, palette().warning);
        ImGui::Bullet();
        ImGui::TextWrapped("%s", line.text.c_str());
        if (line.warn) ImGui::PopStyleColor();
    }
    for (const auto& note : a.notes) {
        ImGui::PushStyleColor(ImGuiCol_Text, palette().warning);
        ImGui::Bullet();
        ImGui::TextWrapped("%s", note.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    if (ImGui::Button("Copy report")) copy_report();
    ImGui::SetItemTooltip("Copy the report, with all digits, to the clipboard");
    ImGui::SameLine();
    if (ImGui::Button("Write report")) write_report();
    item_tooltip(report_file_tooltip());

    if (!a.linear && ImGui::CollapsingHeader("Initial estimates and iteration history")) {
        const auto& def = kin::model_def(a.model());
        const int np = static_cast<int>(def.params.size());
        ImGui::Text("Initial estimates (%s):", a.request.guess == kin::GuessMode::Automatic ? "automatic" : "manual");
        for (int j = 0; j < np; ++j) {
            ImGui::SameLine();
            ImGui::Text("%s = %s", std::string(def.params[static_cast<size_t>(j)].name).c_str(),
                        kin::fmt_g(a.fit.initial[static_cast<size_t>(j)]).c_str());
        }
        const bool gn = a.fit.method == kin::Method::GaussNewton;
        if (ImGui::BeginTable("##history", 4 + np, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                                                      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
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
}

void App::draw_residuals_tab() {
    if (!analysis_) {
        no_results_text();
        return;
    }
    const auto& a = *analysis_;
    const auto& def = kin::model_def(a.model());
    const std::string xName(def.xName), yName(def.yName);

    // The two randomness tests, in plain words (the report keeps v3.51's).
    const auto& rt = a.residualTests;
    ImGui::Text("Signs of the residuals, in order of %s:  %s", xName.c_str(), rt.sequence.c_str());
    item_tooltip("One sign per distinct " + xName + " (replicates are averaged). A good fit scatters the points on "
                 "both sides of the curve with no pattern.");
    const std::string pattern = " Another equation may fit better.";
    if (!rt.runsTestable)
        ImGui::TextDisabled("Runs test: needs at least 8 non-zero residuals.");
    else if (rt.runsRandom)
        ImGui::Text("Runs test: the signs look random (%d runs, %s).", rt.runs, p_text(rt.runsP).c_str());
    else
        warning_text(("Runs test: the signs follow a pattern (" + std::to_string(rt.runs) + " runs, " +
                      p_text(rt.runsP) + ")." + pattern).c_str(), "");
    item_tooltip("Too few runs of the same sign mean the curve misses the data systematically.");
    if (rt.signsRandom)
        ImGui::Text("Sign test: + and \xE2\x88\x92 residuals are balanced (%d +, %d \xE2\x88\x92, %s).", rt.positives,
                    rt.negatives, p_text(rt.signP).c_str());
    else
        warning_text(("Sign test: more " + std::string(rt.positives > rt.negatives ? "+ than \xE2\x88\x92" : "\xE2\x88\x92 than +") +
                      " residuals than chance allows (" + std::to_string(rt.positives) + " +, " +
                      std::to_string(rt.negatives) + " \xE2\x88\x92, " + p_text(rt.signP) + ")." + pattern).c_str(),
                     "");
    ImGui::Spacing();

    const std::string fitted = "Fitted " + yName;
    const int decimals = residual_decimals(a.fit.stdDev);
    if (ImGui::BeginTable("##resid", 6, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                            ImGuiTableFlags_SizingStretchProp,
                          ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.5f);
        ImGui::TableSetupColumn(xName.c_str());
        ImGui::TableSetupColumn(yName.c_str());
        ImGui::TableSetupColumn(fitted.c_str());
        ImGui::TableSetupColumn("Residual");
        ImGui::TableSetupColumn("Outlier?", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        for (int c = 0; c < 6; ++c) {
            ImGui::TableSetColumnIndex(c);
            ImGui::TableHeader(ImGui::TableGetColumnName(c));
            if (c == 4) ImGui::SetItemTooltip("%s minus the fitted value", yName.c_str());
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
}

void App::draw_standard_curve_tab() {
    if (!analysis_) {
        no_results_text();
        return;
    }
    const auto& a = *analysis_;
    const auto& def = kin::model_def(a.model());
    const std::string xName(def.xName), yName(def.yName);
    const std::string xRange = "Data range: " + xName + " from " + kin::fmt_g(a.xMin) + " to " + kin::fmt_g(a.xMax);
    const std::string yRange = "Data range: " + yName + " from " + kin::fmt_g(a.yMin) + " to " + kin::fmt_g(a.yMax);
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

    ImGui::SeparatorText(("Find " + xName + " for a given " + yName).c_str());
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

    ImGui::SeparatorText(("Find " + yName + " for a given " + xName).c_str());
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
    ImGui::SeparatorText("Results");
    if (ImGui::SmallButton("Clear")) stdCurve_.clear();
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy")) {
        std::string t = "entered\tcalculated\n";
        for (const auto& e : stdCurve_)
            t += (e.xFromY ? yName : xName) + " = " + kin::fmt_g(e.input, 10) + "\t" + (e.xFromY ? xName : yName) +
                 " = " + (e.output ? kin::fmt_g(*e.output, 10) : std::string("n/a")) + "\n";
        ImGui::SetClipboardText(t.c_str());
    }
    if (ImGui::BeginTable("##std", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Entered");
        ImGui::TableSetupColumn("Calculated");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const auto& e : stdCurve_) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s = %s", (e.xFromY ? yName : xName).c_str(), kin::fmt_g(e.input).c_str());
            ImGui::TableNextColumn();
            if (e.output) ImGui::Text("%s = %s",(e.xFromY ? xName : yName).c_str(), kin::fmt_g(*e.output).c_str());
            else ImGui::TextColored(palette().bad, "cannot be calculated");
            ImGui::TableNextColumn();
            if (e.outOfRange)
                ImGui::TextColored(palette().bad, "%s is outside the range of the data",
                                   (e.xFromY ? yName : xName).c_str());
        }
        ImGui::EndTable();
    }
}

void App::draw_report_tab() {
    auto& o = report_.options;
    field_label("Format");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    if (ImGui::BeginCombo("##format", kin::report_format_name(o.format).c_str())) {
        for (auto f : {kin::ReportFormat::Short, kin::ReportFormat::Medium, kin::ReportFormat::Long})
            if (ImGui::Selectable(kin::report_format_name(f).c_str(), f == o.format)) o.format = f;
        ImGui::EndCombo();
    }
    if (o.format != kin::ReportFormat::Long) {
        ImGui::SameLine();
        int layout = static_cast<int>(o.layout);
        ImGui::RadioButton("CSV", &layout, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Text columns", &layout, 1);
        o.layout = static_cast<kin::ReportLayout>(layout);
    }
    ImGui::BeginDisabled(!analysis_);
    if (ImGui::Button("Write report")) write_report();
    item_tooltip(report_file_tooltip());
    ImGui::SameLine();
    if (ImGui::Button("Copy to clipboard")) copy_report();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Report options...")) openReportOptions_ = true;
    ImGui::SetItemTooltip("Header, date, numbering and the report file (Alt+O)");
    if (kIsWeb || !report_.file.empty()) {
        const std::string file = kIsWeb ? (report_.file.empty() ? std::string("results.txt") : report_.file.filename().string())
                                        : report_.file.string();
        ImGui::TextDisabled("Report file: %s", file.c_str());
        ImGui::SetItemTooltip(report_.newFile ? "The next report starts the file afresh." : "Reports are appended.");
    }
    if (!report_.status.empty()) ImGui::TextUnformatted(report_.status.c_str());

    if (!analysis_) {
        no_results_text();
        return;
    }
    std::string text = current_report_text(false);
    if (fonts.mono) ImGui::PushFont(fonts.mono, 0.0f);
    ImGui::InputTextMultiline("##preview", &text, ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);
    if (fonts.mono) ImGui::PopFont();
}

}  // namespace app
