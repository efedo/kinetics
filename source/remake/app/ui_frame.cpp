// The window frame: the menus and shortcuts, the strip with the four steps
// of the work, the banner shown with a run from an imported file, the two
// panels and the splitter between them, and the status bar.

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "app/app.hpp"
#include "app/help_text.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"

namespace app {
namespace {

enum class StepState { Pending, Ready, Done, Warn, Bad };

ImVec4 step_colour(StepState s) {
    switch (s) {
    case StepState::Pending: return palette().textDim;
    case StepState::Ready:
    case StepState::Done: return palette().accent;
    case StepState::Warn: return palette().warning;
    case StepState::Bad: return palette().bad;
    }
    return palette().text;
}

// One step of the strip: a numbered circle (a tick once done, "!" when it
// needs a look) and its label. Returns true when clicked.
bool step_item(int number, const std::string& label, StepState state, const std::string& tooltip) {
    ImGui::PushID(number);
    const float fs = ImGui::GetFontSize();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float r = fs * 0.5f;
    const ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    const ImVec2 size(st.FramePadding.x * 2.0f + r * 2.0f + fs * 0.4f + ts.x, ImGui::GetFrameHeight());
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##step", size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered())
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                          st.FrameRounding);
    const ImU32 col = ImGui::GetColorU32(step_colour(state));
    const ImU32 bg = ImGui::GetColorU32(ImGuiCol_WindowBg);
    const ImVec2 c(p.x + st.FramePadding.x + r, p.y + size.y * 0.5f);
    const bool filled = state == StepState::Done || state == StepState::Warn || state == StepState::Bad;
    if (filled) dl->AddCircleFilled(c, r, col);
    else dl->AddCircle(c, r, col, 0, 1.5f);
    if (state == StepState::Done) {
        // A tick.
        const float w = std::max(1.5f, fs * 0.11f);
        dl->AddLine(ImVec2(c.x - r * 0.45f, c.y + r * 0.02f), ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), bg, w);
        dl->AddLine(ImVec2(c.x - r * 0.1f, c.y + r * 0.38f), ImVec2(c.x + r * 0.48f, c.y - r * 0.35f), bg, w);
    } else {
        char num[4];
        std::snprintf(num, sizeof num, "%s", filled ? "!" : std::to_string(number).c_str());
        const float small = ImGui::GetStyle().FontSizeBase * 0.8f;
        ImGui::PushFont(nullptr, small);
        const ImVec2 ns = ImGui::CalcTextSize(num);
        dl->AddText(ImVec2(c.x - ns.x * 0.5f, c.y - ns.y * 0.5f), filled ? bg : col, num);
        ImGui::PopFont();
    }
    dl->AddText(ImVec2(c.x + r + fs * 0.4f, p.y + (size.y - ts.y) * 0.5f),
                ImGui::GetColorU32(state == StepState::Pending ? palette().textDim : palette().text), label.c_str());
    item_tooltip(tooltip);
    ImGui::PopID();
    return clicked;
}

// "›" between two steps.
void step_chevron() {
    ImGui::SameLine(0.0f, 0.0f);
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = ImGui::GetFrameHeight();
    ImGui::Dummy(ImVec2(fs * 1.2f, h));
    const ImVec2 c(p.x + fs * 0.6f, p.y + h * 0.5f);
    const ImU32 col = ImGui::GetColorU32(palette().textDim);
    const float d = fs * 0.2f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(c.x - d * 0.5f, c.y - d), ImVec2(c.x + d * 0.5f, c.y), col, 1.5f);
    dl->AddLine(ImVec2(c.x + d * 0.5f, c.y), ImVec2(c.x - d * 0.5f, c.y + d), col, 1.5f);
    ImGui::SameLine(0.0f, 0.0f);
}

StepState verdict_state(const kin::Analysis& a, std::string& label) {
    switch (worst_level(fit_verdict(a))) {
    case VerdictLevel::Good: label = "Fit: done"; return StepState::Done;
    case VerdictLevel::Caution: label = "Fit: check the results"; return StepState::Warn;
    case VerdictLevel::Bad: label = "Fit: poor fit"; return StepState::Bad;
    }
    return StepState::Done;
}

}  // namespace

// ===========================================================================
// Frame
// ===========================================================================

bool App::frame() {
    handle_file_results();
    handle_shortcuts();
    // The classic interface may have ended the run view.
    if (tableResult_ && !showing_run()) leave_run_view();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float fs = ImGui::GetFontSize();
    if (std::exchange(firstFrame_, false) && vp->WorkSize.y < fs * 41.0f)
        openFitOptions_ = false;  // a short window keeps the fitting options folded

    const float statusH = ImGui::GetFrameHeight() + ImGui::GetStyle().FramePadding.y * 2.0f;
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, std::max(vp->WorkSize.y - statusH, fs * 10.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Kinetics##main", nullptr, flags);
    ImGui::PopStyleVar(2);

    draw_menu_bar();
    draw_step_strip();
    if (showing_run()) draw_run_banner();
    draw_panels(ImGui::GetContentRegionAvail());
    ImGui::End();

    draw_status_bar(statusH);
    draw_dialogs();
    draw_help_window();
    return !quit_;
}

void App::draw_panels(const ImVec2& size) {
    const float fs = ImGui::GetFontSize();
    // The left panel keeps a width in font heights, so it suits the text size;
    // the right panel, with the graph, takes the rest.
    // In a narrow window it gives way, keeping at least half for the results.
    const float minW = std::min(fs * 22.0f, size.x * 0.4f);
    const float maxW = std::max(minW, std::min(fs * 34.0f, size.x * 0.5f));
    const float leftW = std::clamp(leftWidthEm_ * fs, minW, maxW);
    const float splitW = std::round(fs * 0.9f);

    if (ImGui::BeginChild("##left", ImVec2(leftW, size.y), ImGuiChildFlags_None)) draw_left_panel();
    ImGui::EndChild();

    ImGui::SameLine(0.0f, 0.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##split", ImVec2(splitW, size.y));
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.x != 0.0f)
        leftWidthEm_ = std::clamp((leftW + ImGui::GetIO().MouseDelta.x) / fs, 22.0f, 34.0f);
    if (ImGui::IsItemDeactivated()) save_settings();
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        leftWidthEm_ = 26.0f;
        save_settings();
    }
    ImGui::SetItemTooltip("Drag to widen or narrow the panels; double-click for the usual width");
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(p.x + splitW * 0.5f, p.y), ImVec2(p.x + splitW * 0.5f, p.y + size.y),
        ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive : hot ? ImGuiCol_SeparatorHovered
                                                                                  : ImGuiCol_Separator),
        hot ? 2.0f : 1.0f);

    ImGui::SameLine(0.0f, 0.0f);
    if (ImGui::BeginChild("##right", ImVec2(0, size.y), ImGuiChildFlags_None)) draw_right_panel();
    ImGui::EndChild();
}

// ===========================================================================
// Step strip, run banner, status bar
// ===========================================================================

void App::draw_step_strip() {
    int invalid = 0;
    const size_t n = kin::clean_data(table_points(&invalid)).size();
    const auto* item = shown_item();
    const std::string file = import_.path.filename().string();
    const bool sheet = import_.type == ImportType::Spreadsheet;

    // 1. Data
    std::string label;
    StepState state;
    std::string tip = "Enter or edit the data in the table (Alt+D)";
    if (item) {
        label = std::string("Data: ") + (sheet ? "run #" : "dataset #") + std::to_string(item->runNumber) + " of " + file;
        state = StepState::Done;
        tip = "A run from the imported file is shown. Click to go back to your data table (Alt+D).";
    } else if (n == 0) {
        label = "Data: none yet";
        state = StepState::Ready;
    } else {
        label = "Data: " + std::to_string(n) + " point" + (n == 1 ? "" : "s");
        state = StepState::Done;
        if (invalid > 0) {
            label += ", " + std::to_string(invalid) + " not a number";
            state = StepState::Warn;
            tip = std::to_string(invalid) + " cell" + (invalid == 1 ? " is" : "s are") +
                  " not a number; those rows are left out of the fit. " + tip;
        }
    }
    if (step_item(1, label, state, tip)) {
        leave_run_view();
        focusTable_ = true;
    }

    // 2. Equation
    step_chevron();
    const bool started = n > 0 || analysis_ || item;
    if (step_item(2, std::string("Equation: ") + equation_name(model_), started ? StepState::Done : StepState::Pending,
                  "Choose the equation (Alt+T)"))
        focusModel_ = true;

    // 3. Fit
    step_chevron();
    tip = "Fit the equation to the data in the table (F5)";
    if (item) {
        tip = "Fit every run of the file again (F5)";
        if (batchStale_) {
            label = "Fit: out of date";
            state = StepState::Warn;
        } else if (!item->analysis) {
            label = "Fit: failed";
            state = StepState::Bad;
        } else {
            state = verdict_state(*item->analysis, label);
        }
    } else if (analysis_ && stale_) {
        label = "Fit: out of date";
        state = StepState::Warn;
    } else if (analysis_) {
        state = verdict_state(*analysis_, label);
    } else if (n > 0) {
        label = "Fit: press F5";
        state = StepState::Ready;
    } else {
        label = "Fit";
        state = StepState::Pending;
    }
    if (step_item(3, label, state, tip)) request_fit();

    // 4. Report
    step_chevron();
    const bool haveFit = analysis_.has_value();
    if (step_item(4, reportWritten_ ? "Report: written" : "Report",
                  reportWritten_ ? StepState::Done : haveFit ? StepState::Ready : StepState::Pending,
                  haveFit ? "Write or copy a report of the results (Report tab; Alt+P writes it)"
                          : "Report options (Alt+O). Reports can be written once there are results.")) {
        if (haveFit) requestedTab_ = Tab::Report;
        else show_report_options();
    }
    ImGui::Separator();
}

void App::draw_run_banner() {
    const auto* item = shown_item();
    if (!item) return;
    const bool sheet = import_.type == ImportType::Spreadsheet;
    const int count = static_cast<int>(batch_.items.size());
    const int i = batch_.selected;
    const ImVec4 a = palette().accent;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(a.x, a.y, a.z, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(a.x, a.y, a.z, 0.45f));
    if (ImGui::BeginChild("##runbanner", ImVec2(0, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding)) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Showing %s #%d (%d of %d) from %s", sheet ? "run" : "dataset", item->runNumber, i + 1, count,
                    import_.path.filename().string().c_str());
        item_tooltip(import_.path.string());
        ImGui::SameLine();
        ImGui::BeginDisabled(i <= 0);
        if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) show_batch_item(i - 1);
        ImGui::SetItemTooltip("Previous run");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i >= count - 1);
        if (ImGui::ArrowButton("##next", ImGuiDir_Right)) show_batch_item(i + 1);
        ImGui::SetItemTooltip("Next run");
        ImGui::EndDisabled();
        const char* edit = "Edit this run in the table";
        same_line_if_fits(button_width(edit));
        if (ImGui::Button(edit)) edit_shown_run_in_table();
        item_tooltip("Copy this run's points into the data table, replacing what is there (Undo in the status bar), "
                     "to change or fit them on their own.");
        const char* back = "Back to my table";
        same_line_if_fits(button_width(back));
        if (ImGui::Button(back)) leave_run_view();
        ImGui::SetItemTooltip("Show your data table and its own results again");
        if (batchStale_) {
            ImGui::PushStyleColor(ImGuiCol_Text, palette().warning);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("The settings have changed since the runs were fitted.");
            ImGui::PopStyleColor();
            const char* again = "Fit all runs again";
            same_line_if_fits(button_width(again));
            if (primary_button(again)) refit_runs();
            ImGui::SetItemTooltip("Fit every run with the current equation and options (F5)");
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
}

std::string App::next_step_hint() const {
    if (const auto* item = shown_item()) {
        if (batchStale_) return "The settings have changed: press Fit all runs again (F5) to update the runs.";
        if (!item->analysis) return "This run could not be fitted: " + item->error;
        return "Step through the runs with the arrows above; the File runs tab lists them all.";
    }
    if (results_stale()) return "The data or settings have changed: press Fit (F5) to update the results.";
    if (analysis_) {
        if (worst_level(fit_verdict(*analysis_)) != VerdictLevel::Good)
            return "Read the verdict at the top of the results before using these values.";
        if (!reportWritten_) return "Next: write a report (Alt+P), or use Copy results for a spreadsheet.";
        return "Report written. Change the data or the equation and fit again, or open another table (Ctrl+O).";
    }
    if (import_.type != ImportType::None && batch_.items.empty())
        return "Next: press Analyse all runs on the File runs tab (Alt+A), or fit one run at a time.";
    if (kin::clean_data(table_points()).empty())
        return "Next: type or paste your data into the table, or open a data table (Ctrl+O).";
    return "Next: press Fit (F5).";
}

void App::draw_status_bar(float height) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - height));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, height));
    ImVec4 bg = bar_colour();
    if (notice_) {
        // A message tints the bar: red for an error, the accent otherwise.
        const ImVec4 c = notice_->isError ? palette().bad : palette().accent;
        const float t = notice_->isError ? 0.3f : 0.22f;
        bg = ImVec4(bg.x + (c.x - bg.x) * t, bg.y + (c.y - bg.y) * t, bg.z + (c.z - bg.z) * t, 1.0f);
    }
    ImGui::PushStyleColor(ImGuiCol_WindowBg, bg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(st.WindowPadding.x, std::max(0.0f, (height - ImGui::GetFrameHeight()) * 0.5f)));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("##statusbar", nullptr, flags)) {
        const float avail = ImGui::GetContentRegionAvail().x;
        if (notice_) {
            bool close = false, undo = false;
            const bool canUndo = notice_->undo.has_value();
            const float closeW = ImGui::GetFrameHeight();
            const float undoW = canUndo ? button_width("Undo") + st.ItemSpacing.x : 0.0f;
            const float textW = std::max(0.0f, avail - closeW - undoW - st.ItemSpacing.x);
            const std::string shown = fit_text(notice_->text, textW);
            ImGui::AlignTextToFramePadding();
            if (notice_->isError) ImGui::TextColored(palette().bad, "%s", shown.c_str());
            else ImGui::TextUnformatted(shown.c_str());
            if (shown != notice_->text || !notice_->detail.empty())
                item_tooltip(shown != notice_->text && !notice_->detail.empty()
                                 ? notice_->text + "\n\n" + notice_->detail
                                 : shown != notice_->text ? notice_->text : notice_->detail);
            same_line_right(closeW + undoW);
            if (canUndo) {
                undo = ImGui::Button("Undo");
                item_tooltip(notice_->undoTooltip);
                ImGui::SameLine();
            }
            // A drawn cross: the font's multiplication sign is too small to hit.
            close = ImGui::InvisibleButton("##closenotice", ImVec2(closeW, closeW));
            {
                const ImVec2 a0 = ImGui::GetItemRectMin(), a1 = ImGui::GetItemRectMax();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (ImGui::IsItemHovered())
                    dl->AddRectFilled(a0, a1, ImGui::GetColorU32(ImGuiCol_ButtonHovered), st.FrameRounding);
                const float r = closeW * 0.22f;
                const ImVec2 c((a0.x + a1.x) * 0.5f, (a0.y + a1.y) * 0.5f);
                const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
                dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ink, 1.5f);
                dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), ink, 1.5f);
            }
            ImGui::SetItemTooltip("Close this message");
            if (undo) {
                auto snapshot = std::move(*notice_->undo);
                restore_table(std::move(snapshot));  // closes the message
            } else if (close) {
                notice_.reset();
            }
        } else {
            // Where the data came from, then what to do next.
            std::string where;
            if (const auto* item = shown_item())
                where = std::string(import_.type == ImportType::Spreadsheet ? "Run #" : "Dataset #") +
                        std::to_string(item->runNumber) + " of " + import_.path.filename().string();
            else {
                const size_t n = kin::clean_data(table_points()).size();
                where = n == 0 ? std::string("No data yet")
                               : dataSource_ + ", " + std::to_string(n) + " point" + (n == 1 ? "" : "s");
            }
            const std::string sep = "   \xC2\xB7   ";
            const std::string whereShown = fit_text(where, avail * 0.45f) + sep;
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", whereShown.c_str());
            if (whereShown != where + sep) item_tooltip(where);
            ImGui::SameLine(0.0f, 0.0f);
            const std::string hint = next_step_hint();
            const std::string shown =
                fit_text(hint, std::max(0.0f, avail - ImGui::CalcTextSize(whereShown.c_str()).x));
            ImGui::TextUnformatted(shown.c_str());
            if (shown != hint) item_tooltip(hint);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
}

void App::help_link(HelpId id) {
    const char* label = "(?)";
    same_line_right(ImGui::CalcTextSize(label).x);
    ImGui::PushID(static_cast<int>(id));
    if (ImGui::TextLink(label)) open_help(id);
    item_tooltip(std::string("Help: ") + help_topics()[static_cast<size_t>(id)].title);
    ImGui::PopID();
}

// ===========================================================================
// Menus and shortcuts
// ===========================================================================

void App::draw_menu_bar() {
    if (!ImGui::BeginMenuBar()) return;
    const bool haveFit = analysis_.has_value();
    const bool haveImport = import_.type != ImportType::None;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open data table...", "Ctrl+O")) show_open_dialog(FilePurpose::OpenTable, default_dir());
        if (ImGui::MenuItem("Save data table...", "Ctrl+S"))
            show_save_dialog(FilePurpose::SaveTable, tablePath_.empty() ? default_dir() + "data.csv" : tablePath_.string());
        ImGui::Separator();
        if (ImGui::MenuItem("Import spreadsheet file of runs..."))
            show_open_dialog(FilePurpose::ImportSpreadsheet, default_dir());
        ImGui::SetItemTooltip("Concentrations, then one row of velocities per run");
        if (ImGui::MenuItem("Import plate-reader file (MPA, velocities only)..."))
            show_open_dialog(FilePurpose::ImportMpa, default_dir());
        if (ImGui::MenuItem("Close imported file", nullptr, false, haveImport)) close_import();
        if (ImGui::BeginMenu("Open example")) {
            if (ImGui::MenuItem("Michaelis-Menten data table")) open_example(Example::MichaelisMenten);
            if (ImGui::MenuItem("Arrhenius data table")) open_example(Example::Arrhenius);
            if (ImGui::MenuItem("Spreadsheet file with four runs (Hill)")) open_example(Example::SpreadsheetRuns);
            if (ImGui::MenuItem("MPA file with two datasets (inhibition)")) open_example(Example::MpaPlate);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Report options...", "Alt+O")) show_report_options();
        if (ImGui::MenuItem("Write report", "Alt+P", false, haveFit)) write_report();
        if (ImGui::MenuItem("Copy report to clipboard", nullptr, false, haveFit)) copy_report();
        if (!kIsWeb) {  // a browser tab is closed by the browser
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Alt+Q")) quit_ = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::BeginMenu("Equation", true)) {
            // v3.51's menu letters are given as the shortcuts.
            for (const auto& m : kin::all_models()) {
                const char shortcut[2] = {m.letter, '\0'};
                if (ImGui::MenuItem(equation_name(m.id), shortcut, m.id == model_) && m.id != model_) set_model(m.id);
            }
            ImGui::EndMenu();
        }
        ImGui::SetItemTooltip("Alt+T");
        if (ImGui::MenuItem("Enter/edit data", "Alt+D")) {
            leave_run_view();
            focusTable_ = true;
        }
        if (ImGui::MenuItem("Load imported run into table", "Alt+M", false, haveImport)) load_selected_run_into_table();
        ImGui::Separator();
        if (ImGui::MenuItem("Paste data from clipboard")) paste_table_from_clipboard();
        if (ImGui::MenuItem("Sort data")) sort_table();
        if (ImGui::MenuItem("Clear data")) clear_table();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Calculate")) {
        const auto m = analysis_ ? analysis_->model() : model_;
        const std::string xName = x_heading(m), yName = y_heading(m);
        if (ImGui::MenuItem(showing_run() ? "Fit all file runs again" : "Fit table data", "F5 / Alt+R")) request_fit();
        if (ImGui::MenuItem("Fit the selected file run", "Alt+S", false, haveImport)) fit_selected_run();
        if (ImGui::MenuItem("Fit all file runs", "Alt+A", false, haveImport)) {
            requestedTab_ = Tab::FileRuns;
            analyse_all_runs();
        }
        if (ImGui::MenuItem("Show file runs", "Alt+W", false, haveImport)) requestedTab_ = Tab::FileRuns;
        ImGui::Separator();
        if (ImGui::MenuItem(("Find " + xName + " for a given " + yName + "...").c_str(), "Alt+X", false, haveFit)) {
            requestedTab_ = Tab::StandardCurve;
            focusStdX_ = true;
        }
        if (ImGui::MenuItem(("Find " + yName + " for a given " + xName + "...").c_str(), "Alt+Y", false, haveFit)) {
            requestedTab_ = Tab::StandardCurve;
            focusStdY_ = true;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Initial estimates");
        if (ImGui::MenuItem("Automatic", nullptr, guess_ == kin::GuessMode::Automatic)) {
            guess_ = kin::GuessMode::Automatic;
            mark_stale();
        }
        if (ImGui::MenuItem("Manual (values in the parameter table)", nullptr, guess_ == kin::GuessMode::Manual)) {
            guess_ = kin::GuessMode::Manual;
            mark_stale();
            openFitOptions_ = true;  // the estimates are entered there
        }
        ImGui::TextDisabled("Fitting method");
        if (ImGui::MenuItem("Marquardt-Levenberg", nullptr, method_ == kin::Method::Marquardt)) {
            method_ = kin::Method::Marquardt;
            mark_stale();
        }
        if (ImGui::MenuItem("Gauss-Newton", nullptr, method_ == kin::Method::GaussNewton)) {
            method_ = kin::Method::GaussNewton;
            mark_stale();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        // The tabs other than Results appear once they have something to show.
        if (ImGui::MenuItem("Results", "Alt+L")) requestedTab_ = Tab::Results;
        if (ImGui::MenuItem("Graph", "Alt+G", false, haveFit)) show_window("graph");
        if (ImGui::MenuItem("Residuals", "Alt+I", false, haveFit)) show_window("residuals");
        if (ImGui::MenuItem("Standard curve", nullptr, false, haveFit)) requestedTab_ = Tab::StandardCurve;
        if (ImGui::MenuItem("File runs", nullptr, false, haveImport)) requestedTab_ = Tab::FileRuns;
        if (ImGui::MenuItem("Report", nullptr, false, haveFit)) requestedTab_ = Tab::Report;
        ImGui::Separator();
        if (ImGui::MenuItem("Classic interface (v3.51)", "Ctrl+F12")) classicRequested_ = true;
        if (ImGui::BeginMenu("Text size")) {
            if (ImGui::MenuItem("Larger", "Ctrl+=", false, textScale_ < kTextScales.back())) step_text_scale(+1);
            if (ImGui::MenuItem("Smaller", "Ctrl+-", false, textScale_ > kTextScales.front())) step_text_scale(-1);
            ImGui::Separator();
            for (int pct : kTextScales) {
                const std::string label = std::to_string(pct) + "%";
                if (ImGui::MenuItem(label.c_str(), pct == 100 ? "Ctrl+0" : nullptr, textScale_ == pct) && pct != textScale_) {
                    set_text_scale(pct);
                    save_settings();
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Theme")) {
            const ThemeChoice now = theme_choice();
            for (const auto& [choice, label] : {std::pair{ThemeChoice::Dark, "Dark"},
                                                std::pair{ThemeChoice::Light, "Light"},
                                                std::pair{ThemeChoice::System, "Same as the system"}})
                if (ImGui::MenuItem(label, nullptr, now == choice) && choice != now) {
                    set_theme(choice);
                    save_settings();
                }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        const auto topics = help_topics();
        for (int i = 0; i < static_cast<int>(topics.size()); ++i)
            if (ImGui::MenuItem(topics[static_cast<size_t>(i)].title, i == 0 ? "Alt+H / F1" : nullptr))
                open_help(static_cast<HelpId>(i));
        ImGui::Separator();
        if (ImGui::MenuItem("About Kinetics")) openAbout_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
}

void App::handle_shortcuts() {
    using namespace ImGui;
    constexpr ImGuiInputFlags g = ImGuiInputFlags_RouteGlobal;
    const bool haveImport = import_.type != ImportType::None;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_Q, g) && !kIsWeb) quit_ = true;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_O, g)) show_report_options();
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_P, g)) write_report();
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_T, g)) focusModel_ = true;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_D, g)) {
        leave_run_view();
        focusTable_ = true;
    }
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_M, g)) load_selected_run_into_table();
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_R, g) || Shortcut(ImGuiKey_F5, g)) request_fit();
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_S, g)) fit_selected_run();
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_A, g) && haveImport) {
        requestedTab_ = Tab::FileRuns;
        analyse_all_runs();
    }
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_W, g)) requestedTab_ = Tab::FileRuns;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_X, g)) {
        requestedTab_ = Tab::StandardCurve;
        focusStdX_ = true;
    }
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_Y, g)) {
        requestedTab_ = Tab::StandardCurve;
        focusStdY_ = true;
    }
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_L, g)) requestedTab_ = Tab::Results;
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_G, g)) show_window("graph");
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_I, g)) show_window("residuals");
    if (Shortcut(ImGuiMod_Alt | ImGuiKey_H, g) || Shortcut(ImGuiKey_F1, g)) open_help(HelpId::GettingStarted);
    if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, g)) show_open_dialog(FilePurpose::OpenTable, default_dir());
    if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, g))
        show_save_dialog(FilePurpose::SaveTable, tablePath_.empty() ? default_dir() + "data.csv" : tablePath_.string());
    // Text size: Ctrl+= (or Ctrl++), Ctrl+-, Ctrl+0, also on the keypad.
    if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_Equal, g) || Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Equal, g) ||
        Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadAdd, g))
        step_text_scale(+1);
    if (Shortcut(ImGuiMod_Ctrl | ImGuiKey_Minus, g) || Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadSubtract, g))
        step_text_scale(-1);
    if ((Shortcut(ImGuiMod_Ctrl | ImGuiKey_0, g) || Shortcut(ImGuiMod_Ctrl | ImGuiKey_Keypad0, g)) && textScale_ != 100) {
        set_text_scale(100);
        save_settings();
    }
}

}  // namespace app
