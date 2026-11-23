// File runs tab: an imported table or spreadsheet file of runs (or an MPA
// file read by the classic interface), the automatic analysis of every run
// and single-run fits (v3.51 Calculate > file data, preview).
//
// A file of runs is usually analysed as a whole, so that comes first; the
// runs table below it selects the run that both panels then show.

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
    ImGui::AlignTextToFramePadding();
    if (sheet) {
        const auto& f = *import_.sheet;
        ImGui::Text("%s: %zu run%s, %zu concentration%s", import_.path.filename().string().c_str(), f.runs.size(),
                    f.runs.size() == 1 ? "" : "s", f.x.size(), f.x.size() == 1 ? "" : "s");
        std::string xs;
        for (size_t i = 0; i < f.x.size() && i < 12; ++i) xs += (i ? ", " : "") + kin::fmt_g(f.x[i]);
        if (f.x.size() > 12) xs += ", ...";
        item_tooltip(import_.path.string() + "\n\nConcentrations: " + xs +
                     "\n\nTo use another file, choose File > Import. The tab's close button closes this one.");
    } else {
        const int count = kin::mpa_dataset_count(import_.mpaText, import_.mpaCount);
        ImGui::Text("%s: %d complete dataset%s (MPA, velocities only)", import_.path.filename().string().c_str(), count,
                    count == 1 ? "" : "s");
        item_tooltip(import_.path.string() + "\n\nThe X column of the data table supplies the concentrations for "
                     "every dataset. To use another file, choose File > Import.");
    }
    help_link(HelpId::FileData);
    // A table of runs can hold them either way round.
    if (sheet && import_.table) {
        field_label("Runs are in the table's");
        int rows = import_.runsInRows ? 1 : 0;
        if (ImGui::RadioButton("columns", &rows, 0)) set_runs_in_rows(false);
        ImGui::SetItemTooltip("The first column holds the concentrations, and each further column a run");
        ImGui::SameLine();
        if (ImGui::RadioButton("rows", &rows, 1)) set_runs_in_rows(true);
        ImGui::SetItemTooltip("The first row holds the concentrations, and each further row a run");
    }

    // All runs.
    if (primary_button("Analyse all runs")) analyse_all_runs();
    ImGui::SetItemTooltip("Fit every run from the starting one to the end of the file with the current equation "
                          "and options (Alt+A)");
    ImGui::SameLine();
    field_label(sheet ? "from run" : "from dataset");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
    ImGui::InputInt("##firstrun", &batch_.firstRun);
    ImGui::SameLine();
    ImGui::Checkbox("Write a report for each run", &batch_.writeReports);
    if (report_.file.empty()) item_tooltip("You will be asked for a report file.");
    else item_tooltip(report_.file.filename().string() + ", " + kin::report_format_name(report_.options.format) + " format");
    if (!sheet) {
        field_label("Velocities per dataset");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
        if (ImGui::InputInt("##mpacount", &import_.mpaCount)) {
            import_.mpaCount = std::clamp(import_.mpaCount, 1, 10000);
            mark_stale();
        }
    } else if (ImGui::Checkbox("Treat any negative value as missing", &import_.negativeIsMissing)) {
        mark_stale();
    }
    if (sheet)
        item_tooltip("Version 3.51 skipped every negative value. Turn this off to analyse negative log10(v) or Y "
                     "values; only exactly -1 is then treated as missing.");

    // One run at a time, into the data table.
    if (ImGui::CollapsingHeader("One run at a time")) {
        if (sheet) {
            const auto& f = *import_.sheet;
            field_label("Run");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
            const std::string selName = run_name(import_.selectedRun);
            const std::string preview = "Run #" + std::to_string(import_.selectedRun) + (selName.empty() ? "" : " " + selName);
            if (ImGui::BeginCombo("##run", preview.c_str())) {
                for (const auto& r : f.runs) {
                    const auto n = f.points(r, import_.negativeIsMissing).size();
                    const std::string name = run_name(r.number);
                    const std::string label = "Run #" + std::to_string(r.number) + (name.empty() ? "" : " " + name) +
                                              "  (" + std::to_string(n) + " points)";
                    if (ImGui::Selectable(label.c_str(), r.number == import_.selectedRun)) import_.selectedRun = r.number;
                }
                ImGui::EndCombo();
            }
        } else {
            field_label("Dataset");
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
            if (ImGui::InputInt("##dataset", &import_.selectedRun)) import_.selectedRun = std::max(1, import_.selectedRun);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load into table")) load_selected_run_into_table();
        ImGui::SetItemTooltip("Copy this run's points into the data table, to edit or fit them (Alt+M). The status "
                              "bar offers Undo.");
        ImGui::SameLine();
        if (ImGui::Button("Fit this run")) fit_selected_run();
        ImGui::SetItemTooltip("Load this run into the data table and fit it (Alt+S)");
    }

    if (!batch_.status.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", batch_.status.c_str());
    }
    if (batch_.items.empty()) return;
    ImGui::TextDisabled("Click a run to show its points, results and graph.");

    const int n = static_cast<int>(batch_.items.size());
    const auto& def = kin::model_def(batch_.items.front().analysis ? batch_.items.front().analysis->model() : model_);
    std::vector<std::string> names;
    for (const auto& p : def.params) names.emplace_back(p.name);
    const int cols = 3 + static_cast<int>(names.size());
    if (ImGui::BeginTable("##batch", cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
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
            const std::string name = run_name(it.runNumber);
            const std::string label =
                "#" + std::to_string(it.runNumber) + (name.empty() ? "" : " " + name) + "##run" + std::to_string(i);
            const bool shown = showing_run() && batch_.selected == i;
            if (ImGui::Selectable(label.c_str(), shown, ImGuiSelectableFlags_SpanAllColumns)) show_batch_item(i);
            ImGui::TableNextColumn();
            if (!it.analysis) {
                ImGui::TextColored(palette().bad, "failed");
                ImGui::SetItemTooltip("%s", it.error.c_str());
                continue;
            }
            const auto& a = *it.analysis;
            // The verdict of the Results tab, in a word, with its lines in the tooltip.
            const auto verdict = fit_verdict(a);
            switch (worst_level(verdict)) {
            case VerdictLevel::Good: ImGui::TextUnformatted("good fit"); break;
            case VerdictLevel::Caution: ImGui::TextColored(palette().warning, "check"); break;
            case VerdictLevel::Bad: ImGui::TextColored(palette().bad, "poor fit"); break;
            }
            std::string why;
            for (const auto& v : verdict) why += (why.empty() ? "" : "\n") + v.headline;
            item_tooltip(why);
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
