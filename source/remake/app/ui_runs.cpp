// Runs tab: the analysis's runs, all fitted with the same equation and
// options (v3.51's file of runs, Calculate > file data, without its run at a
// time through the table). Click a run to show it in both panels; right-click
// it to fit it alone, rename it or delete it. A run is added by hand, or a
// whole spreadsheet of runs imported from the File menu.

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>

#include "app/app.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"

namespace app {

void App::draw_runs_tab() {
    const int n = static_cast<int>(runs_.size());
    const float fs = ImGui::GetFontSize();

    // Where the runs came from.
    ImGui::AlignTextToFramePadding();
    const std::string count = std::to_string(n) + " run" + (n == 1 ? "" : "s");
    if (!runsFile_.empty()) {
        ImGui::Text("%s: %s", runsFile_.filename().string().c_str(), count.c_str());
        item_tooltip(runsFile_.string());
    } else {
        ImGui::TextUnformatted(count.c_str());
    }
    help_link(HelpId::FileData);

    // Until the runs are edited, the file can be read again another way.
    if (runsAsImported_) {
        if (runsTable_) {
            field_label("Runs are in the file's");
            int rows = runsInRows_ ? 1 : 0;
            if (ImGui::RadioButton("columns", &rows, 0) && runsInRows_) {
                runsInRows_ = false;
                reread_runs_file();
            }
            ImGui::SetItemTooltip("The first column holds the concentrations, and each further column a run");
            ImGui::SameLine();
            if (ImGui::RadioButton("rows", &rows, 1) && !runsInRows_) {
                runsInRows_ = true;
                reread_runs_file();
            }
            ImGui::SetItemTooltip("The first row holds the concentrations, and each further row a run");
            ImGui::SameLine();
        }
        if (ImGui::Checkbox("Treat any negative value as missing", &import_.negativeIsMissing)) {
            save_settings();
            reread_runs_file();
        }
        item_tooltip("Version 3.51 skipped every negative value in a file. Turn this off to analyse negative "
                     "log10(v) or Y values; only exactly -1 is then treated as missing. The file is read again.");
    }

    ImGui::Spacing();
    if (primary_button("Fit all runs")) fit_all_runs();
    ImGui::SetItemTooltip("Fit every run with the equation and options on the left (Alt+A). To fit one run, "
                          "right-click it, or show it and press Fit (F5).");
    ImGui::Spacing();

    // The runs and their results.
    const auto& def = kin::model_def(model_);
    std::vector<std::string> names;
    for (const auto& p : def.params) names.emplace_back(p.name);
    const int cols = 4 + static_cast<int>(names.size());
    int selectAt = -1, fitAt = -1, deleteAt = -1;
    if (ImGui::BeginTable("##runs", cols, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Run");
        ImGui::TableSetupColumn("Points");
        ImGui::TableSetupColumn("Status");
        for (const auto& nm : names) ImGui::TableSetupColumn(nm.c_str());
        ImGui::TableSetupColumn("Residual SD");
        ImGui::TableHeadersRow();
        for (int k = 0; k < n; ++k) {
            ImGui::PushID(k);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (ImGui::Selectable(run_label(k).c_str(), k == current_, ImGuiSelectableFlags_SpanAllColumns))
                selectAt = k;
            ImGui::SetItemTooltip("Click to show this run; right-click to fit, rename or delete it");
            if (ImGui::BeginPopupContextItem("##runmenu")) {
                if (ImGui::MenuItem("Fit this run")) fitAt = k;
                ImGui::Separator();
                field_label("Name");
                ImGui::SetNextItemWidth(fs * 12.0f);
                ImGui::InputText("##name", &runs_[static_cast<size_t>(k)].name);
                ImGui::Separator();
                if (ImGui::MenuItem("Delete this run", nullptr, false, n > 1)) deleteAt = k;
                if (n < 2) ImGui::SetItemTooltip("An analysis keeps at least one run");
                ImGui::EndPopup();
            }

            ImGui::TableNextColumn();
            ImGui::Text("%d", run_point_count(k));

            ImGui::TableNextColumn();
            const auto* a = run_analysis(k);
            const auto& failure = runs_[static_cast<size_t>(k)].error;
            if (!a) {
                if (!failure.empty()) {
                    ImGui::TextColored(palette().bad, "failed");
                    item_tooltip(failure);
                } else {
                    ImGui::TextDisabled("not fitted");
                }
            } else if (run_stale(k)) {
                ImGui::TextColored(palette().warning, "out of date");
                ImGui::SetItemTooltip("The equation or options have changed since this run was fitted");
            } else {
                // The Results tab's verdict, in a word, with its lines in the tooltip.
                const auto verdict = fit_verdict(*a);
                switch (worst_level(verdict)) {
                case VerdictLevel::Good: ImGui::TextUnformatted("good fit"); break;
                case VerdictLevel::Caution: ImGui::TextColored(palette().warning, "check"); break;
                case VerdictLevel::Bad: ImGui::TextColored(palette().bad, "poor fit"); break;
                }
                std::string why;
                for (const auto& v : verdict) why += (why.empty() ? "" : "\n") + v.headline;
                item_tooltip(why);
            }
            const bool dim = a && run_stale(k);
            if (dim) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, kStaleAlpha);
            for (size_t j = 0; j < names.size(); ++j) {
                ImGui::TableNextColumn();
                if (!a || j >= a->fit.p.size() || a->model() != model_) continue;
                ImGui::TextUnformatted(format_value_pm(a->fit.p[j], a->fit.sd[j]).c_str());
                ImGui::SetItemTooltip("%s \xC2\xB1 %s", kin::fmt_g(a->fit.p[j]).c_str(), kin::fmt_g(a->fit.sd[j]).c_str());
            }
            ImGui::TableNextColumn();
            if (a) ImGui::TextUnformatted(kin::fmt_g(a->fit.stdDev, 3).c_str());
            if (dim) ImGui::PopStyleVar();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("+ Add run")) add_run();
    ImGui::SetItemTooltip("A new run with an empty table, to type or paste its data into");

    // Changes to the list, after it is drawn.
    if (fitAt >= 0) fit_run(fitAt);
    else if (deleteAt >= 0) delete_run(deleteAt);
    else if (selectAt >= 0) select_run(selectAt);
}

}  // namespace app
