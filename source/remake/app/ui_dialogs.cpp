// Modal dialogs and the help window: errors, the "too few points" warning,
// report options (v3.51 "printer Options"), help topics and About.

#include <SDL3/SDL_version.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <implot.h>

#include <algorithm>
#include <cfloat>

#include "app/app.hpp"
#include "app/help_text.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"

namespace app {

namespace {

void center_next_window() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

bool close_button(const char* label = "OK") {
    const bool pressed = ImGui::Button(label, ImVec2(ImGui::GetFontSize() * 6.0f, 0));
    ImGui::SetItemDefaultFocus();
    return pressed;
}

}  // namespace

void App::draw_dialogs() {
    if (openError_) {
        ImGui::OpenPopup("Error##modal");
        openError_ = false;
    }
    if (openFewPoints_) {
        ImGui::OpenPopup("Too few data points##modal");
        openFewPoints_ = false;
    }
    if (openReportOptions_) {
        ImGui::OpenPopup("Report options##modal");
        openReportOptions_ = false;
    }
    if (openAbout_) {
        ImGui::OpenPopup("About Kinetics##modal");
        openAbout_ = false;
    }

    center_next_window();
    if (ImGui::BeginPopupModal("Error##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextColored(palette().bad, "%s", errorText_.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (close_button() || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            errorText_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    center_next_window();
    if (ImGui::BeginPopupModal("Too few data points##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto n = kin::clean_data(table_points()).size();
        const int k = kin::free_param_count(model_, params_);
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::Text("You have %zu data points and %d free parameters.", n, k);
        ImGui::TextWrapped("You can't fit a curve with as many parameters as points (or nearly as many): "
                           "you need #points > #parameters + 1. Otherwise you are, in the final analysis, "
                           "making up a curve.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Get out of this mess")) ImGui::CloseCurrentPopup();
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Continue with the fit (no guarantees)")) {
            ImGui::CloseCurrentPopup();
            if (n >= static_cast<size_t>(k)) fit_now();
            else error(std::to_string(n) + " data points cannot determine " + std::to_string(k) + " parameters.");
        }
        ImGui::EndPopup();
    }

    draw_report_options_dialog();
    draw_about_dialog();
}

void App::draw_report_options_dialog() {
    center_next_window();
    if (!ImGui::BeginPopupModal("Report options##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    auto& o = report_.options;
    const float w = ImGui::GetFontSize() * 22.0f;

    int fmt = static_cast<int>(o.format);
    ImGui::TextUnformatted("Format:");
    ImGui::RadioButton("Short: one line per run", &fmt, 0);
    ImGui::RadioButton("Medium: data and parameters", &fmt, 1);
    ImGui::RadioButton("Long: full report with residuals and tests", &fmt, 2);
    o.format = static_cast<kin::ReportFormat>(fmt);
    ImGui::BeginDisabled(o.format == kin::ReportFormat::Long);
    int layout = static_cast<int>(o.layout);
    ImGui::TextUnformatted("Short/medium layout:");
    ImGui::SameLine();
    ImGui::RadioButton("Comma-separated (spreadsheet)", &layout, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Text columns", &layout, 1);
    o.layout = static_cast<kin::ReportLayout>(layout);
    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::SetNextItemWidth(w);
    ImGui::InputText("Header (experiment type)", &o.header);
    ImGui::SetItemTooltip("If blank, the report has a line to fill in by hand.");
    ImGui::SetNextItemWidth(w);
    ImGui::InputText("Date of experiment", &o.experimentDate);
    ImGui::SetItemTooltip("If blank, the report has a line to fill in by hand.");

    ImGui::Spacing();
    int num = static_cast<int>(o.numbering);
    ImGui::TextUnformatted("Numbering:");
    ImGui::SameLine();
    ImGui::RadioButton("Off", &num, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Sequential", &num, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Use file run #", &num, 2);
    o.numbering = static_cast<kin::Numbering>(num);
    ImGui::BeginDisabled(o.numbering != kin::Numbering::Sequential);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    if (ImGui::InputInt("Starting # for numbering", &o.startNumber)) report_.nextNumber = o.startNumber;
    ImGui::SameLine();
    ImGui::TextDisabled("(next: %d)", report_.nextNumber);
    ImGui::EndDisabled();

    ImGui::SeparatorText("Destination");
    if (report_.file.empty()) ImGui::TextDisabled("No report file chosen.");
    else ImGui::TextWrapped("%s", report_.file.string().c_str());
    if (ImGui::Button("Choose file...")) {
        pendingReport_ = PendingReport::None;
        choose_report_file();
    }
    ImGui::SetItemTooltip(kIsWeb ? "In the browser, the whole report file is downloaded after each write."
                                 : "Reports are text files; print them from any editor.");
    if (!report_.file.empty()) {
        ImGui::SameLine();
        int mode = report_.newFile ? 1 : 0;
        ImGui::RadioButton("Append to the file", &mode, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Start a new file (overwrite)", &mode, 1);
        if ((mode == 1) != report_.newFile) {
            report_.newFile = mode == 1;
            if (report_.newFile) report_.nextNumber = o.startNumber;
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Reset all values to defaults")) {
        o = kin::ReportOptions{};
        report_.nextNumber = o.startNumber;
    }
    ImGui::SameLine();
    if (close_button("Done") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        save_settings();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::draw_about_dialog() {
    center_next_window();
    if (!ImGui::BeginPopupModal("About Kinetics##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("SB'S KINETICS PROGRAM");
    ImGui::Text("Version %s, a port of version 3.51 to modern C++", KINETICS_VERSION);
    ImGui::TextUnformatted("BioTechniques 13:906-911 (1992)");
    ImGui::Spacing();
    ImGui::TextDisabled("Built with SDL %d.%d.%d, Dear ImGui %s and ImPlot %s.", SDL_MAJOR_VERSION,
                        SDL_MINOR_VERSION, SDL_MICRO_VERSION, IMGUI_VERSION, IMPLOT_VERSION);
    if (kIsWeb) ImGui::TextDisabled("WebAssembly build. Preferences are kept in this browser's local storage.");
    else ImGui::TextDisabled("Preferences: %s", (prefDir_ / "kinetics.ini").string().c_str());
    ImGui::Spacing();
    if (close_button() || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::draw_help_window() {
    if (!showHelp_) return;
    const auto topics = help_topics();
    helpTopic_ = std::clamp(helpTopic_, 0, static_cast<int>(topics.size()) - 1);
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(fs * 52.0f, fs * 34.0f), ImGuiCond_FirstUseEver);
    center_next_window();
    if (ImGui::Begin("Help", &showHelp_)) {
        if (ImGui::BeginChild("##topics", ImVec2(fs * 15.0f, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
            for (int i = 0; i < static_cast<int>(topics.size()); ++i)
                if (ImGui::Selectable(topics[static_cast<size_t>(i)].title, i == helpTopic_)) helpTopic_ = i;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
            const auto& t = topics[static_cast<size_t>(helpTopic_)];
            ImGui::TextUnformatted(t.title);
            ImGui::Separator();
            ImGui::TextWrapped("%s", t.body);
        }
        ImGui::EndChild();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape))
            showHelp_ = false;
    }
    ImGui::End();
}

}  // namespace app
