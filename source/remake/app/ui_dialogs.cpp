// Modal dialogs and the help window: the "too few points" question, report
// options (v3.51 "printer Options"), help topics and About. Errors go to the
// message bar above the tabs instead (ui_results.cpp).

#include <SDL3/SDL_version.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <implot.h>

#include <algorithm>
#include <cfloat>
#include <string>
#include <string_view>
#include <utility>

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
    if (ImGui::BeginPopupModal("Too few data points##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const auto n = kin::clean_data(table_points()).size();
        const int k = kin::free_param_count(model_, params_);
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::Text("You have %zu data point%s and %d parameters to fit.", n, n == 1 ? "" : "s", k);
        ImGui::TextWrapped("A reliable fit needs more points than parameters + 1: at least %d points here. With "
                           "fewer, the data cannot pin the curve down, so the fitted values mean little.",
                           k + 2);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Fit anyway")) {
            ImGui::CloseCurrentPopup();
            if (n >= static_cast<size_t>(k)) fit_now();
            else error(std::to_string(n) + " data points cannot determine " + std::to_string(k) + " parameters.");
        }
        ImGui::EndPopup();
    }

    draw_report_options_dialog();
    draw_about_dialog();
    draw_replace_dialog();
}

// Replacing every run (a new analysis, an import, an example) cannot be
// undone, so it waits for a yes.
void App::draw_replace_dialog() {
    const char* id = "Replace the data?##replace";
    if (openReplace_) {
        ImGui::OpenPopup(id);
        openReplace_ = false;
    }
    center_next_window();
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const int n = static_cast<int>(runs_.size());
    int points = 0;
    for (int k = 0; k < n; ++k) points += run_point_count(k);
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
    ImGui::TextWrapped("%s replaces the data there is now: %d run%s with %d point%s in all, and %s results. This "
                       "cannot be undone.",
                       replaceWhat_.c_str(), n, n == 1 ? "" : "s", points, points == 1 ? "" : "s",
                       n == 1 ? "its" : "their");
    ImGui::TextDisabled("To keep them, cancel and save the analysis first (File > Save analysis).");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("Replace")) {
        auto action = std::move(pendingReplace_);
        pendingReplace_ = nullptr;
        ImGui::CloseCurrentPopup();
        if (action) action();
    }
    ImGui::SameLine();
    if (close_button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        pendingReplace_ = nullptr;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::draw_report_options_dialog() {
    center_next_window();
    if (!ImGui::BeginPopupModal("Report options##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    draw_report_options_form();
    ImGui::Separator();
    if (close_button("Done") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        save_settings();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

std::string App::report_options_summary() const {
    const auto& o = report_.options;
    std::string s = o.format == kin::ReportFormat::Short    ? "Short report"
                    : o.format == kin::ReportFormat::Medium ? "Medium report"
                                                            : "Long report";
    if (o.format != kin::ReportFormat::Long)
        s += o.layout == kin::ReportLayout::Csv ? ", comma-separated" : ", text columns";
    if (o.numbering == kin::Numbering::Sequential) s += ", numbered from " + std::to_string(report_.nextNumber);
    else if (o.numbering == kin::Numbering::RunNumber) s += ", numbered by file run";
    if (kIsWeb) s += ", downloaded as " + (report_.file.empty() ? std::string("results.txt") : report_.file.filename().string());
    else if (report_.file.empty()) s += ", no file chosen yet";
    else s += (report_.newFile ? ", starting " : ", appended to ") + report_.file.filename().string();
    return s + ".";
}

void App::draw_report_options_form() {
    auto& o = report_.options;
    const float w = ImGui::GetFontSize() * 22.0f;
    const auto before = o;
    if (!ImGui::BeginTable("##reportform", 2, ImGuiTableFlags_SizingFixedFit)) return;
    ImGui::TableSetupColumn("label");
    ImGui::TableSetupColumn("field", ImGuiTableColumnFlags_WidthFixed, w);

    form_row("Format");
    if (ImGui::BeginCombo("##format", kin::report_format_name(o.format).c_str())) {
        for (auto f : {kin::ReportFormat::Short, kin::ReportFormat::Medium, kin::ReportFormat::Long})
            if (ImGui::Selectable(kin::report_format_name(f).c_str(), f == o.format)) o.format = f;
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Short: one line per run. Medium: the data and parameters. Long: everything, with the "
                          "residuals and tests.");
    ImGui::BeginDisabled(o.format == kin::ReportFormat::Long);
    form_row("Layout");
    int layout = static_cast<int>(o.layout);
    ImGui::RadioButton("Comma-separated (spreadsheet)", &layout, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Text columns", &layout, 1);
    o.layout = static_cast<kin::ReportLayout>(layout);
    ImGui::EndDisabled();
    if (o.format == kin::ReportFormat::Long) ImGui::SetItemTooltip("For the short and medium formats");

    form_row("Experiment type");
    ImGui::InputText("##header", &o.header);
    bool save = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("The report's heading. If blank, the report has a line to fill in by hand.");
    form_row("Date of experiment");
    ImGui::InputText("##date", &o.experimentDate);
    save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetItemTooltip("If blank, the report has a line to fill in by hand.");
    form_row("Numbering");
    int num = static_cast<int>(o.numbering);
    ImGui::RadioButton("Off", &num, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Sequential", &num, 1);
    ImGui::SameLine();
    ImGui::RadioButton("File run number", &num, 2);
    o.numbering = static_cast<kin::Numbering>(num);
    ImGui::BeginDisabled(o.numbering != kin::Numbering::Sequential);
    form_row("Start numbering at");
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
    if (ImGui::InputInt("##start", &o.startNumber)) report_.nextNumber = o.startNumber;
    save |= o.startNumber != before.startNumber && !ImGui::IsItemActive();  // typed, or a step button
    save |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextDisabled("(next: %d)", report_.nextNumber);
    ImGui::EndDisabled();

    form_row("Report file");
    if (kIsWeb) {
        ImGui::TextDisabled("Downloaded after each write");
    } else {
        if (report_.file.empty()) ImGui::TextDisabled("None chosen yet");
        else {
            ImGui::TextUnformatted(report_.file.filename().string().c_str());
            ImGui::SetItemTooltip("%s", report_.file.string().c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Choose...")) {
            pendingReport_ = PendingReport::None;
            choose_report_file();
        }
        ImGui::SetItemTooltip("Reports are text files; print them from any editor.");
        if (!report_.file.empty()) {
            form_row("");
            int mode = report_.newFile ? 1 : 0;
            ImGui::RadioButton("Append to the file", &mode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Start a new file (overwrite)", &mode, 1);
            if ((mode == 1) != report_.newFile) {
                report_.newFile = mode == 1;
                if (report_.newFile) report_.nextNumber = o.startNumber;
            }
        }
    }
    ImGui::EndTable();

    if (ImGui::SmallButton("Reset the options to their defaults")) {
        o = kin::ReportOptions{};
        report_.nextNumber = o.startNumber;
        save = true;
    }
    // Keep the choices for the next session: at once for the choices, and
    // when a text field is left.
    if (save || o.format != before.format || o.layout != before.layout || o.numbering != before.numbering)
        save_settings();
}

void App::draw_about_dialog() {
    center_next_window();
    if (!ImGui::BeginPopupModal("About Kinetics##modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::TextUnformatted("Kinetics");
    ImGui::Text("Version %s, a port of SB's Kinetics Program 3.51 to modern C++", KINETICS_VERSION);
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

namespace {

// A help topic's body: paragraphs separated by blank lines, where "## " starts
// a heading, lines indented by four spaces are a fixed-width block and lines
// starting with "- " are a list.
void draw_help_body(std::string_view body) {
    const float fs = ImGui::GetFontSize();
    size_t pos = 0;
    bool first = true;
    while (pos < body.size()) {
        size_t end = body.find("\n\n", pos);
        if (end == std::string_view::npos) end = body.size();
        const std::string_view para = body.substr(pos, end - pos);
        pos = end + 2;
        if (para.empty()) continue;
        if (para.rfind("## ", 0) == 0) {
            section_heading(std::string(para.substr(3)), first);
        } else if (para.rfind("    ", 0) == 0) {
            if (!first) ImGui::Spacing();
            if (fonts().mono) ImGui::PushFont(fonts().mono, 0.0f);
            ImGui::Indent(fs);
            ImGui::TextUnformatted(para.data(), para.data() + para.size());
            ImGui::Unindent(fs);
            if (fonts().mono) ImGui::PopFont();
        } else if (para.rfind("- ", 0) == 0) {
            if (!first) ImGui::Spacing();
            size_t lp = 0;
            while (lp < para.size()) {
                size_t le = para.find('\n', lp);
                if (le == std::string_view::npos) le = para.size();
                std::string line(para.substr(lp, le - lp));
                lp = le + 1;
                if (line.rfind("- ", 0) == 0) line.erase(0, 2);
                ImGui::Bullet();
                ImGui::TextWrapped("%s", line.c_str());
            }
        } else {
            if (!first) ImGui::Spacing();
            const std::string text(para);
            ImGui::TextWrapped("%s", text.c_str());
        }
        first = false;
    }
}

}  // namespace

void App::draw_help_window() {
    if (!showHelp_) return;
    const auto topics = help_topics();
    helpTopic_ = std::clamp(helpTopic_, 0, static_cast<int>(topics.size()) - 1);
    const float fs = ImGui::GetFontSize();
    // The topic list fits the longest title.
    float listW = 0.0f;
    for (const auto& t : topics) listW = std::max(listW, ImGui::CalcTextSize(t.title).x);
    listW += ImGui::GetStyle().WindowPadding.x * 2.0f + ImGui::GetStyle().ScrollbarSize;
    ImGui::SetNextWindowSize(ImVec2(listW + fs * 40.0f, fs * 36.0f), ImGuiCond_FirstUseEver);
    center_next_window();
    if (ImGui::Begin("Help", &showHelp_)) {
        if (ImGui::BeginChild("##topics", ImVec2(listW, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX)) {
            for (int i = 0; i < static_cast<int>(topics.size()); ++i)
                if (ImGui::Selectable(topics[static_cast<size_t>(i)].title, i == helpTopic_)) helpTopic_ = i;
        }
        ImGui::EndChild();
        ImGui::SameLine();
        if (ImGui::BeginChild("##body", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding)) {
            static int shownTopic = -1;
            if (std::exchange(shownTopic, helpTopic_) != helpTopic_) ImGui::SetScrollY(0.0f);
            const auto& t = topics[static_cast<size_t>(helpTopic_)];
            push_heading_font(1.3f);
            ImGui::TextUnformatted(t.title);
            ImGui::PopFont();
            ImGui::Spacing();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(ImGui::GetContentRegionAvail().x, fs * 40.0f));
            draw_help_body(t.body);
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape))
            showHelp_ = false;
    }
    ImGui::End();
}

}  // namespace app
