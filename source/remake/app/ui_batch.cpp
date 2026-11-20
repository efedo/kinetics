// File runs tab: imported spreadsheet / MPA files, single-run fits and the
// automatic analysis of every run (v3.51 Calculate > file data, preview).

#include <imgui.h>

#include <algorithm>
#include <cfloat>

#include "app/app.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"

namespace app {

void App::draw_file_runs_tab() {
    // The tab is shown only once a file is imported (the import buttons for a
    // first file are on the getting-started page and in the File menu).
    if (import_.type == ImportType::None) return;

    const bool sheet = import_.type == ImportType::Spreadsheet;
    ImGui::Text("%s file: %s", sheet ? "Spreadsheet" : "MPA", import_.path.filename().string().c_str());
    ImGui::SetItemTooltip("%s", import_.path.string().c_str());
    if (ImGui::SmallButton("Import another spreadsheet file...")) show_open_dialog(FilePurpose::ImportSpreadsheet, default_dir());
    ImGui::SameLine();
    if (ImGui::SmallButton("Import another MPA file...")) show_open_dialog(FilePurpose::ImportMpa, default_dir());

    ImGui::SeparatorText("Single run");
    if (sheet) {
        const auto& f = *import_.sheet;
        std::string xs;
        for (size_t i = 0; i < f.x.size() && i < 12; ++i) xs += (i ? ", " : "") + kin::fmt_g(f.x[i]);
        if (f.x.size() > 12) xs += ", ...";
        ImGui::Text("%zu runs, %zu concentrations", f.runs.size(), f.x.size());
        item_tooltip(xs);
        if (ImGui::Checkbox("Treat any negative value as missing", &import_.negativeIsMissing)) mark_stale();
        item_tooltip("Version 3.51 skipped every negative value. Turn this off to analyse negative log10(v) or Y "
                     "values; only exactly -1 is then treated as missing.");

        field_label("Run");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        const std::string preview = "Run #" + std::to_string(import_.selectedRun);
        if (ImGui::BeginCombo("##run", preview.c_str())) {
            for (const auto& r : f.runs) {
                const auto n = f.points(r, import_.negativeIsMissing).size();
                const std::string label = "Run #" + std::to_string(r.number) + "  (" + std::to_string(n) + " points)";
                if (ImGui::Selectable(label.c_str(), r.number == import_.selectedRun)) import_.selectedRun = r.number;
            }
            ImGui::EndCombo();
        }
    } else {
        field_label("Velocities per dataset");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        if (ImGui::InputInt("##mpacount", &import_.mpaCount)) import_.mpaCount = std::clamp(import_.mpaCount, 1, 10000);
        const int count = kin::mpa_dataset_count(import_.mpaText, import_.mpaCount);
        field_label("Dataset");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        if (ImGui::InputInt("##dataset", &import_.selectedRun)) import_.selectedRun = std::max(1, import_.selectedRun);
        item_tooltip(std::to_string(count) + " complete dataset" + (count == 1 ? "" : "s") +
                     " in the file. MPA files hold velocities only: the X column of the data table supplies the "
                     "concentrations for every dataset.");
    }
    if (ImGui::Button("Load into table")) load_selected_run_into_table();
    ImGui::SetItemTooltip("Copy this run's points into the data table, to edit or fit them (Alt+M)");
    ImGui::SameLine();
    if (ImGui::Button("Fit this run")) fit_selected_run();
    ImGui::SetItemTooltip("Load this run into the data table and fit it (Alt+S)");

    ImGui::SeparatorText("All runs");
    field_label(sheet ? "Starting run" : "Starting dataset");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    ImGui::InputInt("##firstrun", &batch_.firstRun);
    ImGui::Checkbox("Write a report for each run", &batch_.writeReports);
    if (report_.file.empty()) item_tooltip("You will be asked for a report file.");
    else item_tooltip(report_.file.filename().string() + ", " + kin::report_format_name(report_.options.format) + " format");
    if (ImGui::Button("Analyse all runs")) analyse_all_runs();
    ImGui::SetItemTooltip("Fit every run from the starting one to the end of the file (Alt+A)");
    if (!batch_.status.empty()) {
        ImGui::SameLine();
        ImGui::TextWrapped("%s", batch_.status.c_str());
    }
    if (batch_.items.empty()) return;

    // Stepping through the runs replaces v3.51's "automatic preview".
    const int n = static_cast<int>(batch_.items.size());
    ImGui::BeginDisabled(batch_.selected <= 0);
    if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) show_batch_item(batch_.selected - 1);
    ImGui::SetItemTooltip("Previous run");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(batch_.selected >= n - 1);
    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) show_batch_item(batch_.selected + 1);
    ImGui::SetItemTooltip("Next run");
    ImGui::EndDisabled();
    if (batch_.selected >= 0) {
        ImGui::SameLine();
        ImGui::Text("Run #%d (%d of %d)", batch_.items[static_cast<size_t>(batch_.selected)].runNumber,
                    batch_.selected + 1, n);
        ImGui::SetItemTooltip("Shown in the Results, Graph and Residuals tabs");
        ImGui::SameLine();
        if (ImGui::SmallButton("Graph")) requestedTab_ = Tab::Graph;
    }

    const auto& def = kin::model_def(batch_.items.front().analysis ? batch_.items.front().analysis->model() : model_);
    std::vector<std::string> names;
    for (const auto& p : def.params) names.emplace_back(p.name);
    const int cols = 3 + static_cast<int>(names.size());
    if (ImGui::BeginTable("##batch", cols, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                                               ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX,
                          ImGui::GetContentRegionAvail())) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Run");
        ImGui::TableSetupColumn("Status");
        for (const auto& nm : names) ImGui::TableSetupColumn(nm.c_str());
        ImGui::TableSetupColumn("Residual SD");
        ImGui::TableHeadersRow();
        for (int i = 0; i < n; ++i) {
            const auto& it = batch_.items[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string label = "#" + std::to_string(it.runNumber) + "##run" + std::to_string(i);
            if (ImGui::Selectable(label.c_str(), batch_.selected == i, ImGuiSelectableFlags_SpanAllColumns))
                show_batch_item(i);
            ImGui::TableNextColumn();
            if (!it.analysis) {
                ImGui::TextColored(palette().bad, "failed");
                ImGui::SetItemTooltip("%s", it.error.c_str());
                continue;
            }
            const auto& a = *it.analysis;
            const bool ok = a.fit.status == kin::FitStatus::Converged || a.linear;
            if (ok) ImGui::TextUnformatted("converged");
            else ImGui::TextColored(palette().warning, "see notes");
            if (!ok) ImGui::SetItemTooltip("%s", a.fit.message.c_str());
            for (size_t j = 0; j < names.size(); ++j) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(format_value_pm(a.fit.p[j], a.fit.sd[j]).c_str());
                ImGui::SetItemTooltip("%s \xC2\xB1 %s", kin::fmt_g(a.fit.p[j]).c_str(), kin::fmt_g(a.fit.sd[j]).c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kin::fmt_g(a.fit.stdDev, 3).c_str());
        }
        ImGui::EndTable();
    }
}

}  // namespace app
