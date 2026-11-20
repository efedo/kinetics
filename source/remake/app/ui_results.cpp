// Right panel: results, residuals, standard curve and report tabs
// (v3.51 View menu and Calculate > new X / new Y).

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cfloat>

#include "app/app.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"

namespace app {

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
    if (!ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_DrawSelectedOverline)) return;
    auto tab = [&](const char* label, Tab t, void (App::*fn)()) {
        const ImGuiTabItemFlags f = (requestedTab_ && *requestedTab_ == t) ? ImGuiTabItemFlags_SetSelected : 0;
        if (ImGui::BeginTabItem(label, nullptr, f)) {
            if (ImGui::BeginChild("##tabcontent", ImVec2(0, 0), ImGuiChildFlags_None,
                                  t == Tab::Graph ? ImGuiWindowFlags_NoScrollbar : 0))
                (this->*fn)();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
    };
    // Each tab appears once it has something to show.
    const bool haveFit = analysis_.has_value();
    tab("Results", Tab::Results, &App::draw_results_tab);
    if (haveFit) {
        tab("Graph", Tab::Graph, &App::draw_graph_tab);
        tab("Residuals", Tab::Residuals, &App::draw_residuals_tab);
        tab("Standard curve", Tab::StandardCurve, &App::draw_standard_curve_tab);
    }
    if (haveImport) tab("File runs", Tab::FileRuns, &App::draw_file_runs_tab);
    if (haveFit) tab("Report", Tab::Report, &App::draw_report_tab);
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

    ImGui::Text("%s", a.description.c_str());
    ImGui::Spacing();
    ImGui::Text("RESULTS of %s", a.method_title().c_str());
    if (!a.linear) {
        if (a.fit.status == kin::FitStatus::Converged) ImGui::TextUnformatted(a.fit.message.c_str());
        else ImGui::TextColored(palette().warning, "%s", a.fit.message.c_str());
        ImGui::Text("# of iterations = %d        1 Std. deviation = %s", a.fit.iterations, kin::fmt_g(a.fit.stdDev).c_str());
    } else {
        ImGui::Text("Residual std. deviation = %s", kin::fmt_g(a.fit.stdDev).c_str());
    }
    {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%d points, %d free parameter%s, %d degrees of freedom", a.fit.n, a.fit.nFree,
                      a.fit.nFree == 1 ? "" : "s", a.linear ? a.linear->df : a.fit.dof);
        item_tooltip(buf);
    }
    ImGui::Spacing();

    if (ImGui::BeginTable("##results", 5, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
        ImGui::TableSetupColumn("+/- 1 SD", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
        ImGui::TableSetupColumn("SD %", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 4.5f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const auto& r : a.parameter_rows()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.name.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kin::fmt_g(r.value).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.fixed ? "0" : kin::fmt_g(r.sd).c_str());
            ImGui::TableNextColumn();
            if (!r.fixed && r.value != 0.0 && std::isfinite(r.sd)) {
                const double pct = std::fabs(r.sd * 100.0 / r.value);
                if (pct <= 10.0) ImGui::Text("%.2f%%", pct);
                else ImGui::TextColored(palette().warning, "%.2f%%", pct);
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
    for (const auto& line : a.interpretation()) {
        ImGui::Bullet();
        ImGui::TextWrapped("%s", line.c_str());
    }
    for (const auto& note : a.notes) {
        ImGui::PushStyleColor(ImGuiCol_Text, palette().warning);
        ImGui::Bullet();
        ImGui::TextWrapped("%s", note.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    if (ImGui::Button("Copy report")) copy_report();
    ImGui::SameLine();
    if (ImGui::Button("Write report (Alt+P)")) write_report();

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
    for (const auto& line : a.residual_test_lines()) ImGui::TextWrapped("%s", line.c_str());
    ImGui::Spacing();
    const std::string yhat = a.model() == kin::ModelId::Arrhenius ? "log10(vhat)" : std::string(def.yName) + "hat";
    if (ImGui::BeginTable("##resid", 6, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                            ImGuiTableFlags_SizingStretchProp,
                          ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Pair #", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.5f);
        ImGui::TableSetupColumn(std::string(def.xName).c_str());
        ImGui::TableSetupColumn(std::string(def.yName).c_str());
        ImGui::TableSetupColumn(yhat.c_str());
        ImGui::TableSetupColumn((std::string(def.yName) + " - " + yhat).c_str());
        ImGui::TableSetupColumn("> 2 SD", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.5f);
        ImGui::TableHeadersRow();  // the last header is "> 2 SD"
        item_tooltip("* marks a residual of at least 2 standard deviations (" + kin::fmt_g(2.0 * a.fit.stdDev, 4) + ")");
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
            ImGui::TextUnformatted(kin::fmt_g(a.yhat[i]).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%+.5e", a.resid[i]);
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

    ImGui::SeparatorText(("New " + xName + " for an entered " + yName + "  (Alt+X)").c_str());
    if (focusStdX_) {
        ImGui::SetKeyboardFocusHere();
        focusStdX_ = false;
    }
    ImGui::SetNextItemWidth(w);
    bool go = ImGui::InputDouble((yName + "##stdY").c_str(), &stdInputY_, 0, 0, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue);
    item_tooltip(yRange);
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

    ImGui::SeparatorText(("New " + yName + " for an entered " + xName + "  (Alt+Y)").c_str());
    if (focusStdY_) {
        ImGui::SetKeyboardFocusHere();
        focusStdY_ = false;
    }
    ImGui::SetNextItemWidth(w);
    go = ImGui::InputDouble((xName + "##stdX").c_str(), &stdInputX_, 0, 0, "%.6g", ImGuiInputTextFlags_EnterReturnsTrue);
    item_tooltip(xRange);
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
                ImGui::TextColored(palette().bad, "%s outside curve range!", (e.xFromY ? yName : xName).c_str());
        }
        ImGui::EndTable();
    }
}

void App::draw_report_tab() {
    auto& o = report_.options;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    if (ImGui::BeginCombo("Format", kin::report_format_name(o.format).c_str())) {
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
    if (ImGui::Button("Write report (Alt+P)")) write_report();
    if (kIsWeb) item_tooltip("The whole report file is downloaded each time you write a report.");
    else if (report_.file.empty()) item_tooltip("You will be asked for a report file.");
    ImGui::SameLine();
    if (ImGui::Button("Copy to clipboard")) copy_report();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Report options... (Alt+O)")) openReportOptions_ = true;
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
