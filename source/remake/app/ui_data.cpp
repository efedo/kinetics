// Left panel: equation, data table, the Fit button and the fitting options
// (v3.51 Edit menu: "edit equation Type" and "enter/edit Data").
//
// Top to bottom in the order of the work: choose the equation, enter the
// data, fit. The parameter and minimisation settings, which most fits never
// need, sit folded below the Fit button.

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>

#include "app/app.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "core/guess.hpp"

namespace app {

void App::draw_left_panel() {
    ImGui::SeparatorText("Equation");
    draw_model_section();
    ImGui::SeparatorText("Data");
    draw_data_table();
    // The table fills the height left over by what follows it, measured here
    // and used on the next frame.
    const float footerTop = ImGui::GetCursorPosY();
    draw_fit_button();
    draw_fit_options_section();
    leftFooterHeight_ = ImGui::GetCursorPosY() - footerTop;
}

void App::draw_model_section() {
    const auto& def = kin::model_def(model_);
    const std::string preview = std::string(1, def.letter) + ". " + std::string(def.menuName);
    if (focusModel_) {
        ImGui::SetKeyboardFocusHere();
        focusModel_ = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##model", preview.c_str(), ImGuiComboFlags_HeightLargest)) {
        for (const auto& m : kin::all_models()) {
            const std::string label = std::string(1, m.letter) + ". " + std::string(m.menuName);
            const bool selected = m.id == model_;
            if (ImGui::Selectable(label.c_str(), selected) && !selected) set_model(m.id);
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s", std::string(def.equation).c_str());
}

void App::draw_parameter_table() {
    const auto& def = kin::model_def(model_);
    const bool linear = def.family == kin::Family::Linear;
    const bool manual = guess_ == kin::GuessMode::Manual && !linear;
    const bool bounds = showBounds_ && !linear;
    const int cols = bounds ? 5 : 3;
    const float numWidth = ImGui::GetFontSize() * 6.5f;

    const auto iv0 = kin::param_index(model_, kin::Role::V0);
    const bool v0Measured = v0_measured();

    if (ImGui::BeginTable("##params", cols, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                                                ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Parameter", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Fixed");
        ImGui::TableSetupColumn(manual ? "Fixed value / estimate" : "Fixed value", ImGuiTableColumnFlags_WidthFixed, numWidth);
        if (bounds) {
            ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, numWidth);
            ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, numWidth);
        }
        ImGui::TableHeadersRow();
        for (size_t j = 0; j < def.params.size(); ++j) {
            auto& s = params_[j];
            const auto& pd = def.params[j];
            ImGui::PushID(static_cast<int>(j));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(std::string(pd.name).c_str());

            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!pd.fixable);
            if (ImGui::Checkbox("##fixed", &s.fixed)) mark_stale();
            ImGui::EndDisabled();
            if (!pd.fixable) ImGui::SetItemTooltip("This parameter is always fitted.");

            ImGui::TableNextColumn();
            const bool editable = s.fixed || manual;
            if (editable) {
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputDouble("##value", &s.value, 0.0, 0.0, "%.6g")) mark_stale();
                if (v0Measured && iv0 && *iv0 == static_cast<int>(j))
                    item_tooltip("v0 will be the velocity measured at " + std::string(def.xName) + " = 0.");
                else
                    ImGui::SetItemTooltip(s.fixed ? "Value the parameter is fixed at" : "Initial estimate for the fit");
            } else {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("automatic");
            }
            if (bounds) {
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputDouble("##min", &s.min, 0.0, 0.0, "%.4g")) mark_stale();
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::InputDouble("##max", &s.max, 0.0, 0.0, "%.4g")) mark_stale();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (!linear) {
        ImGui::Checkbox("Show bounds", &showBounds_);
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("Reset parameters")) {
        params_ = kin::default_param_settings(model_);
        mark_stale();
    }
    if (manual) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Use automatic estimates")) {
            const auto pts = kin::clean_data(table_points());
            if (pts.empty()) {
                error("Enter data first: the estimates are calculated from the data.");
            } else {
                const auto g = kin::automatic_guess(model_, pts, params_);
                for (size_t j = 0; j < params_.size(); ++j)
                    if (!params_[j].fixed) params_[j].value = g[j];
                mark_stale();
            }
        }
    }
}

void App::draw_fit_button() {
    const auto& def = kin::model_def(model_);
    const bool haveData = !kin::clean_data(table_points()).empty();
    // While a run from the file analysis is shown, the table's state does not
    // apply to the results.
    const bool showingRun = batch_.selected >= 0 && analysisRun_.has_value();

    ImGui::Spacing();
    ImGui::BeginDisabled(!haveData);
    if (primary_button(results_stale() ? "Fit again" : "Fit", ImVec2(-FLT_MIN, ImGui::GetFrameHeight() * 1.5f)))
        request_fit();
    ImGui::EndDisabled();
    if (!haveData)
        item_tooltip("Enter data first: type or paste " + std::string(def.xName) + " and " + std::string(def.yName) +
                     " values in the table above.");
    else
        ImGui::SetItemTooltip(results_stale() ? "The data or settings have changed: fit again to update the results (F5)"
                                              : "Fit the equation to the data in the table (F5)");

    // Out-of-date results are flagged on the right; here, only a fit that
    // did not converge needs a word.
    if (!analysis_ || showingRun || stale_) return;
    if (!analysis_->linear && analysis_->fit.status != kin::FitStatus::Converged)
        warning_text("The last fit did not converge: see the Results tab.", analysis_->fit.message);
}

void App::draw_fit_options_section() {
    ImGui::Spacing();
    if (openFitOptions_) {
        ImGui::SetNextItemOpen(true);
        openFitOptions_ = false;
    }
    const bool open = ImGui::CollapsingHeader("Fitting options");
    if (ImGui::IsItemToggledOpen() && open) ImGui::SetScrollHereY(0.0f);  // bring the options into view
    if (!open) {
        ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
        ImGui::TextWrapped("%s", fit_settings_summary().c_str());
        ImGui::PopStyleColor();
        return;
    }
    draw_parameter_table();
    ImGui::Spacing();
    draw_fit_settings();
}

void App::draw_fit_settings() {
    const bool linear = kin::model_def(model_).family == kin::Family::Linear;
    ImGui::BeginGroup();
    ImGui::BeginDisabled(linear);
    if (ImGui::BeginTable("##fitform", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("label");
        ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthStretch);
        form_row("Method");
        if (ImGui::RadioButton("Marquardt-Levenberg", method_ == kin::Method::Marquardt)) {
            method_ = kin::Method::Marquardt;
            mark_stale();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Gauss-Newton", method_ == kin::Method::GaussNewton)) {
            method_ = kin::Method::GaussNewton;
            mark_stale();
        }
        form_row("Initial estimates");
        if (ImGui::RadioButton("Automatic", guess_ == kin::GuessMode::Automatic)) {
            guess_ = kin::GuessMode::Automatic;
            mark_stale();
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Manual", guess_ == kin::GuessMode::Manual)) {
            guess_ = kin::GuessMode::Manual;
            mark_stale();
        }
        form_row("Iteration limit");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        if (ImGui::InputInt("##maxit", &maxIterations_, 10, 50)) {
            maxIterations_ = std::clamp(maxIterations_, 0, 100000);
            mark_stale();
        }
        if (!linear) ImGui::SetItemTooltip(method_ == kin::Method::Marquardt ? "0 = default (200)" : "0 = default (30)");
        if (!linear && maxIterations_ == 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(default: %d)", method_ == kin::Method::Marquardt ? 200 : 30);
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();
    if (linear) ImGui::SetItemTooltip("Linear regression is solved directly.");
}

void App::draw_data_table() {
    const auto& def = kin::model_def(model_);
    if (ImGui::SmallButton("Paste")) paste_table_from_clipboard();
    ImGui::SetItemTooltip("Paste two columns copied from a spreadsheet");
    ImGui::SameLine();
    if (ImGui::SmallButton("Open...")) show_open_dialog(FilePurpose::OpenTable, default_dir());
    ImGui::SameLine();
    if (ImGui::SmallButton("Save...")) show_save_dialog(FilePurpose::SaveTable, tablePath_.empty() ? default_dir() + "data.csv" : tablePath_.string());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        set_table({});
        dataSource_ = "Entered by hand";
        dataRun_.reset();
    }

    if (rows_.empty() || !rows_.back().x.empty() || !rows_.back().y.empty()) rows_.push_back({});

    // After Fit (F5) with nothing to fit, say what to do here instead of in
    // an error box; the note goes once there are data.
    if (showEmptyHint_ && !kin::clean_data(table_points()).empty()) showEmptyHint_ = false;
    if (showEmptyHint_)
        warning_text(("Enter data first: type or paste " + std::string(def.xName) + " and " + std::string(def.yName) +
                      " values in the table, then press Fit.")
                         .c_str(),
                     "");

    int insertAt = -1, deleteAt = -1;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_BordersOuter;
    // Leave room for the Fit button and the options below, but always show a
    // few rows (the panel scrolls if the window is too short for both).
    const float minHeight = ImGui::GetFrameHeightWithSpacing() * 6.0f;
    const float height = std::max(minHeight, ImGui::GetContentRegionAvail().y - leftFooterHeight_);
    const bool empty = rows_.size() == 1;
    if (ImGui::BeginTable("##data", 3, flags, ImVec2(0, height))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.5f);
        ImGui::TableSetupColumn(std::string(def.xName).c_str(), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(std::string(def.yName).c_str(), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        if (focusTable_) ImGui::SetScrollY(0.0f);

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows_.size()));
        if (focusTable_) clipper.IncludeItemByIndex(0);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                auto& row = rows_[static_cast<size_t>(i)];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                const bool last = i + 1 == static_cast<int>(rows_.size());
                // Keep row numbers out of the Tab order so Tab goes X -> Y -> next X.
                ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
                ImGui::Selectable(last ? "*" : std::to_string(i + 1).c_str(), false,
                                  ImGuiSelectableFlags_AllowOverlap);
                ImGui::PopItemFlag();
                if (ImGui::BeginPopupContextItem("##rowmenu")) {
                    if (ImGui::MenuItem("Insert row above")) insertAt = i;
                    if (ImGui::MenuItem("Delete row", nullptr, false, !last)) deleteAt = i;
                    ImGui::EndPopup();
                }
                ImGui::SetItemTooltip(last ? "Type in this row to add a point" : "Right-click to insert or delete");

                for (int c = 0; c < 2; ++c) {
                    ImGui::TableNextColumn();
                    std::string& cell = c == 0 ? row.x : row.y;
                    double tmp = 0.0;
                    const bool bad = parse_cell(cell, tmp) == CellState::Invalid;
                    // Filled cells read as a plain grid (the box appears on hover
                    // and while editing); the new-point row keeps its boxes as a
                    // cue to type there.
                    ImGui::PushStyleColor(ImGuiCol_FrameBg,
                                          bad ? palette().badFill : last ? ImGui::GetStyleColorVec4(ImGuiCol_FrameBg)
                                                                : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (focusTable_ && i == 0 && c == 0) {
                        ImGui::SetKeyboardFocusHere();
                        focusTable_ = false;
                    }
                    // An empty table says what to type where.
                    const std::string hint = empty ? "type " + std::string(c == 0 ? def.xName : def.yName) : std::string();
                    if (ImGui::InputTextWithHint(c == 0 ? "##x" : "##y", hint.c_str(), &cell,
                                                 ImGuiInputTextFlags_CharsScientific)) {
                        mark_stale();
                        if (dataSource_ != "Entered by hand" && dataSource_.find("(edited)") == std::string::npos)
                            dataSource_ += " (edited)";
                    }
                    ImGui::PopStyleColor();
                    if (bad) ImGui::SetItemTooltip("Not a number: this row is left out of the fit");
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (insertAt >= 0) {
        rows_.insert(rows_.begin() + insertAt, DataRow{});
        mark_stale();
    }
    if (deleteAt >= 0) {
        rows_.erase(rows_.begin() + deleteAt);
        mark_stale();
    }
}

}  // namespace app
