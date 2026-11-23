// Left panel: the Data, Equation and Fitting options sections (v3.51 Edit menu: "enter/
// edit Data" and "edit equation Type").
//
// Top to bottom in the order of the steps in the strip above: enter the
// data, choose the equation, fit. Each section folds. The Fitting options section holds
// the parameter and minimisation settings, which most fits never need and
// so start folded, with the Fit button below them. While a run from an
// imported file is shown, its points take the table's place, read-only.

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "app/app.hpp"
#include "app/theme.hpp"
#include "app/ui_common.hpp"
#include "core/guess.hpp"

namespace app {
namespace {

// The equation list, grouped by what the data are, with a line on when to
// use each equation. The first of a group to try is marked.
struct EquationInfo {
    kin::ModelId id;
    const char* name;
    const char* useWhen;
    bool startHere;
};

constexpr EquationInfo kEquations[] = {
    {kin::ModelId::MichaelisMenten, "Michaelis-Menten", "v against [S], rising to a plateau. Suits most enzymes.", true},
    {kin::ModelId::HillGt0, "Hill (h > 0)", "An S-shaped curve: cooperative binding, h estimated.", false},
    {kin::ModelId::HillGe1, "Hill (h \xE2\x89\xA5 1)", "Only for an enzyme known to be positively cooperative.", false},
    {kin::ModelId::ActivatorGt0, "Activator (h > 0)", "v against [A], with some activity without activator.", true},
    {kin::ModelId::ActivatorGe1, "Activator (h \xE2\x89\xA5 1)", "As above, for an activator known to act cooperatively.",
     false},
    {kin::ModelId::ActivatorEq1, "Activator (h = 1)", "As above, when h does not differ from 1.", false},
    {kin::ModelId::InhibitionLinear, "Linear inhibition", "v falling towards zero as [I] rises. Gives I50.", true},
    {kin::ModelId::InhibitionI50, "Non-linear inhibition (I50)", "Curves the linear equation misses. Gives I50 and h.",
     false},
    {kin::ModelId::InhibitionK50, "Non-linear inhibition (K50)", "v falling to a floor (vmin). Gives the mid-point K50.",
     false},
    {kin::ModelId::Arrhenius, "Arrhenius", "log10(v) against temperature in \xC2\xB0" "C. Gives Ea.", false},
    {kin::ModelId::Linear, "Linear regression", "A straight line, Y against X.", false},
};

const EquationInfo& equation_info(kin::ModelId id) {
    for (const auto& e : kEquations)
        if (e.id == id) return e;
    return kEquations[0];
}

struct EquationGroup {
    const char* title;
    kin::Family a, b;  // the families in the group (b may repeat a)
};

constexpr EquationGroup kGroups[] = {
    {"Substrate saturation", kin::Family::MichaelisMenten, kin::Family::Hill},
    {"Activators", kin::Family::Activation, kin::Family::Activation},
    {"Inhibitors", kin::Family::Inhibition, kin::Family::Inhibition},
    {"Other", kin::Family::Arrhenius, kin::Family::Linear},
};

// The curve's shape, for a thumbnail beside its name: x from 0 to 4,
// y from 0 to 1.
double thumbnail_y(kin::ModelId id, double x) {
    using M = kin::ModelId;
    const double h2 = x * x / (1.0 + x * x);
    switch (id) {
    case M::MichaelisMenten: return x / (0.6 + x);
    case M::HillGt0:
    case M::HillGe1: return std::pow(x, 2.5) / (std::pow(1.2, 2.5) + std::pow(x, 2.5));
    case M::ActivatorGt0:
    case M::ActivatorGe1: return 0.3 + 0.65 * h2;
    case M::ActivatorEq1: return 0.3 + 0.65 * x / (0.6 + x);
    case M::InhibitionLinear: return 0.95 / (1.0 + 1.6 * x);
    case M::InhibitionI50: return 0.95 * (1.0 - h2);
    case M::InhibitionK50: return 0.95 - 0.7 * h2;
    case M::Arrhenius: return 0.1 + 0.8 * std::pow(x / 4.0, 1.4);
    case M::Linear: return 0.1 + 0.8 * x / 4.0;
    }
    return 0.0;
}

void draw_thumbnail(kin::ModelId id, ImVec2 p, ImVec2 size, ImU32 col, ImU32 axes) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x, p.y + size.y), axes);
    dl->AddLine(ImVec2(p.x, p.y + size.y), ImVec2(p.x + size.x, p.y + size.y), axes);
    ImVec2 pts[24];
    for (int i = 0; i < 24; ++i) {
        const double x = 4.0 * i / 23.0;
        const double y = std::clamp(thumbnail_y(id, x), 0.0, 1.0);
        pts[i] = ImVec2(p.x + static_cast<float>(x / 4.0) * size.x, p.y + size.y - static_cast<float>(y) * size.y);
    }
    dl->AddPolyline(pts, 24, col, ImDrawFlags_None, 1.5f);
}

}  // namespace

const char* equation_name(kin::ModelId id) { return equation_info(id).name; }

void App::draw_left_panel() {
    // Alt+D and Alt+T put the cursor in a section, which must be open.
    if (focusTable_ && !showing_run()) dataOpen_ = true;
    if (focusModel_) equationOpen_ = true;
    if (showing_run()) draw_run_points();
    else draw_data_table();
    // The table fills the height left over by what follows it, measured here
    // and used on the next frame.
    const float footerTop = ImGui::GetCursorPosY();
    draw_model_section();
    draw_fit_section();
    leftFooterHeight_ = ImGui::GetCursorPosY() - footerTop;
}

void App::section_fold_heading(Section s, const char* title, bool& open, bool first) {
    const bool asked = gotoSection_ && *gotoSection_ == s;
    if (asked) {
        open = true;
        gotoSection_.reset();
    }
    const bool clicked = foldable_heading(title, open, first);
    // Bring the section into view; the panel scrolls when the window is too
    // short for everything.
    if (asked || (clicked && open)) ImGui::SetScrollHereY(0.0f);
}

void App::draw_model_section() {
    section_fold_heading(Section::Equation, "Equation", equationOpen_);
    help_link(HelpId::Equation);
    heading_gap();
    if (!equationOpen_) return;
    const auto& def = kin::model_def(model_);
    const float fs = ImGui::GetFontSize();
    // In a short window the formula goes into the list's tooltip.
    const bool shortWindow = ImGui::GetMainViewport()->WorkSize.y < fs * 41.0f;
    if (focusModel_) {
        ImGui::SetKeyboardFocusHere();
        focusModel_ = false;
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    // The list is wider than the panel, to fit the "use when" lines.
    ImGui::SetNextWindowSizeConstraints(ImVec2(fs * 30.0f, 0.0f),
                                        ImVec2(FLT_MAX, ImGui::GetMainViewport()->WorkSize.y * 0.85f));
    if (ImGui::BeginCombo("##model", equation_name(model_), ImGuiComboFlags_HeightLargest)) {
        const ImGuiStyle& st = ImGui::GetStyle();
        const float lineH = ImGui::GetTextLineHeight();
        const ImVec2 thumb(fs * 2.2f, lineH * 1.5f);
        for (const auto& g : kGroups) {
            ImGui::SeparatorText(g.title);
            for (const auto& m : kin::all_models()) {
                if (m.family != g.a && m.family != g.b) continue;
                const auto& info = equation_info(m.id);
                const bool selected = m.id == model_;
                ImGui::PushID(static_cast<int>(m.id));
                // Two lines: the name (and "start here"), then when to use it.
                const ImVec2 p = ImGui::GetCursorScreenPos();
                const float h = lineH * 2.0f + st.ItemSpacing.y * 0.5f;
                if (ImGui::Selectable("##eq", selected, 0, ImVec2(0.0f, h)) && !selected) set_model(m.id);
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::SetItemTooltip("%s", std::string(m.equation).c_str());
                const float x = p.x + thumb.x + fs * 0.6f;
                const float right = ImGui::GetItemRectMax().x - st.FramePadding.x;
                draw_thumbnail(m.id, ImVec2(p.x + fs * 0.1f, p.y + (h - thumb.y) * 0.5f), thumb,
                               ImGui::GetColorU32(palette().accent),
                               ImGui::GetColorU32(ImVec4(palette().textDim.x, palette().textDim.y, palette().textDim.z, 0.5f)));
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddText(ImVec2(x, p.y), ImGui::GetColorU32(ImGuiCol_Text), info.name);
                if (info.startHere) {
                    const char* tag = "start here";
                    const float tw = ImGui::CalcTextSize(tag).x;
                    dl->AddText(ImVec2(right - tw, p.y), ImGui::GetColorU32(palette().accent), tag);
                }
                const std::string use = fit_text(info.useWhen, right - x);
                dl->AddText(ImVec2(x, p.y + lineH + st.ItemSpacing.y * 0.25f), ImGui::GetColorU32(palette().textDim),
                            use.c_str());
                // v3.51's menu letters still choose an equation while the list is open.
                if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_A + (m.letter - 'a')), false) &&
                    !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt) {
                    if (!selected) set_model(m.id);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    }
    item_tooltip(shortWindow ? std::string(def.equation) + "\n\nAlt+T. Letters a to k (v3.51's menu) choose an "
                                                            "equation while the list is open."
                             : std::string("Alt+T. Letters a to k (v3.51's menu) choose an equation while the list "
                                           "is open."));
    if (!shortWindow) {
        ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
        ImGui::TextWrapped("%s", std::string(def.equation).c_str());
        ImGui::PopStyleColor();
    }
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

    if (ImGui::BeginTable("##params", cols, ImGuiTableFlags_SizingFixedFit)) {
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

namespace {

// The Fit button: a "play" triangle and the label in the headings' type,
// on a button sized to them. Returns true when pressed.
bool play_button(const char* label) {
    push_heading_font();
    const float fs = ImGui::GetFontSize();
    const ImVec2 ts = ImGui::CalcTextSize(label);
    const float th = std::round(fs * 0.7f);  // the triangle's height
    const float tw = std::round(th * 0.87f);  // and width
    const float gap = std::round(fs * 0.45f);
    const ImVec2 size(std::round(tw + gap + ts.x + fs * 2.4f), std::round(ImGui::GetFrameHeight() * 1.3f));
    // Centred in the column.
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, std::floor((ImGui::GetContentRegionAvail().x - size.x) * 0.5f)));
    const bool pressed = primary_button("##fit", size);
    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    const float x = std::floor((a.x + b.x - (tw + gap + ts.x)) * 0.5f);
    const float cy = std::floor((a.y + b.y) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);  // dimmed when disabled
    dl->AddTriangleFilled(ImVec2(x, cy - th * 0.5f), ImVec2(x + tw, cy), ImVec2(x, cy + th * 0.5f), ink);
    dl->AddText(ImVec2(x + tw + gap, std::floor(cy - ts.y * 0.5f)), ink, label);
    ImGui::PopFont();
    return pressed;
}

}  // namespace

void App::draw_fit_button() {
    const auto& def = kin::model_def(model_);
    const bool runs = showing_run();
    // While the file's runs are shown and up to date there is nothing to fit.
    const bool haveData = runs ? batchStale_ : !kin::clean_data(table_points()).empty();

    ImGui::Spacing();
    ImGui::BeginDisabled(!haveData);
    const char* label = runs ? "Fit all runs again (F5)" : results_stale() ? "Fit again (F5)" : "Fit (F5)";
    if (play_button(label)) request_fit();
    ImGui::EndDisabled();
    if (runs)
        item_tooltip(batchStale_ ? "Fit every run of " + import_.path.filename().string() +
                                       " with the current equation and options (F5)"
                                 : std::string("The runs are fitted with the current settings. Change the equation "
                                               "or the options to fit them again, or press Back to my table to fit "
                                               "your own data."));
    else if (!haveData)
        item_tooltip("Enter data first: type or paste " + std::string(def.xName) + " and " + std::string(def.yName) +
                     " values in the table above.");
    else
        ImGui::SetItemTooltip(results_stale() ? "The equation or settings have changed: fit again to update the results"
                                              : "Fit the equation to the data in the table");

    // Out-of-date results are flagged on the right; here, only a fit that
    // did not converge needs a word.
    if (!analysis_ || runs || stale_) return;
    if (!analysis_->linear && analysis_->fit.status != kin::FitStatus::Converged)
        warning_text("The last fit did not converge: see the Results tab.", analysis_->fit.message);
}

// The fitting options, which fold, and below them the Fit button, which
// stays in view when they are folded.
void App::draw_fit_section() {
    section_fold_heading(Section::Fit, "Fitting options", fitOpen_);
    help_link(HelpId::FittingOptions);
    heading_gap();
    if (fitOpen_) {
        draw_parameter_table();
        ImGui::Spacing();
        draw_fit_settings();
    }
    draw_fit_button();
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

namespace {

// The data table's height: what the panel leaves above `footer`, but always
// a few rows (the panel scrolls if the window is too short for both).
float table_height(float footer) {
    const float minHeight = ImGui::GetFrameHeightWithSpacing() * 6.0f;
    // Measured from the panel's height, not the space left below the cursor,
    // which shrinks while the panel is scrolled and would keep it scrolled.
    return std::max(minHeight, ImGui::GetWindowHeight() - ImGui::GetCursorPosY() - footer);
}

// The current table row in the accent colour: the point under the mouse on
// the graph.
void highlight_row() {
    const ImVec4 a = palette().accent;
    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, ImGui::GetColorU32(ImVec4(a.x, a.y, a.z, 0.35f)));
}

// Narrow columns: numbers need about 10 font heights, not the panel's width.
float table_width() {
    return std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 24.0f);
}

}  // namespace

void App::draw_data_table() {
    const auto& def = kin::model_def(model_);
    const bool arrhenius = def.family == kin::Family::Arrhenius;

    // The heading, with the table's buttons beside it.
    section_fold_heading(Section::Data, "Data", dataOpen_, true);
    struct Btn {
        const char* label;
        const char* tip;
    };
    const Btn buttons[] = {{"Paste", "Paste two columns copied from a spreadsheet"},
                           {"Open...", "Open a data table (Ctrl+O)"},
                           {"Save...", "Save the table as a comma-separated file (Ctrl+S)"},
                           {"Clear", "Empty the table. The status bar then offers Undo."}};
    // Beside the heading while they fit (leaving room for the help link),
    // then on the next line.
    const float helpW = ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
    for (int b = 0; b < 4; ++b) {
        same_line_if_fits(button_width(buttons[b].label) + helpW);
        if (ImGui::SmallButton(buttons[b].label)) {
            switch (b) {
            case 0: paste_table_from_clipboard(); break;
            case 1: show_open_dialog(FilePurpose::OpenTable, default_dir()); break;
            case 2:
                show_save_dialog(FilePurpose::SaveTable,
                                 tablePath_.empty() ? default_dir() + "data.csv" : tablePath_.string());
                break;
            default: clear_table(); break;
            }
        }
        ImGui::SetItemTooltip("%s", buttons[b].tip);
    }
    help_link(HelpId::Data);
    heading_gap();

    if (rows_.empty() || !rows_.back().x.empty() || !rows_.back().y.empty()) rows_.push_back({});
    if (!dataOpen_) return;

    // After Fit (F5) with nothing to fit, say what to do here instead of in
    // an error box; the note goes once there are data.
    if (showEmptyHint_ && !kin::clean_data(table_points()).empty()) showEmptyHint_ = false;
    if (showEmptyHint_)
        warning_text(("Enter data first: type or paste " + std::string(def.xName) + " and " + std::string(def.yName) +
                      " values in the table, then press Fit.")
                         .c_str(),
                     "");

    // The row of the point under the mouse on the graph. The results' data
    // are the table's numeric rows, so the point is found by its values
    // (the nth of equal points, for replicates).
    int hoverRow = -1;
    if (hoveredPoint_ >= 0 && analysis_ && static_cast<size_t>(hoveredPoint_) < analysis_->data.size()) {
        const auto& data = analysis_->data;
        const kin::Point target = data[static_cast<size_t>(hoveredPoint_)];
        int nth = 0;
        for (int k = 0; k < hoveredPoint_; ++k)
            nth += data[static_cast<size_t>(k)].x == target.x && data[static_cast<size_t>(k)].y == target.y;
        for (size_t r = 0; r < rows_.size() && hoverRow < 0; ++r) {
            double x = 0.0, y = 0.0;
            if (parse_cell(rows_[r].x, x) == CellState::Number && parse_cell(rows_[r].y, y) == CellState::Number &&
                x == target.x && y == target.y && nth-- == 0)
                hoverRow = static_cast<int>(r);
        }
    }

    int insertAt = -1, deleteAt = -1;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_BordersOuter;
    const bool empty = rows_.size() == 1;
    if (ImGui::BeginTable("##data", 3, flags, ImVec2(table_width(), table_height(leftFooterHeight_)))) {
        // The heading and the units stay in view while the rows scroll.
        ImGui::TableSetupScrollFreeze(0, arrhenius ? 1 : 2);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.5f);
        ImGui::TableSetupColumn(std::string(def.xName).c_str(), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(std::string(def.yName).c_str(), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        if (!arrhenius) {
            // Optional units, used in the results, the graph and reports.
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            for (int c = 0; c < 2; ++c) {
                ImGui::TableNextColumn();
                std::string& unit = c == 0 ? xUnit_ : yUnit_;
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint(c == 0 ? "##xunit" : "##yunit", "unit (optional)", &unit);
                if (ImGui::IsItemDeactivatedAfterEdit()) save_settings();
                ImGui::PopStyleColor(2);
                item_tooltip(c == 0 ? "The unit of " + std::string(def.xName) + ", e.g. \xC2\xB5M. It is shown with "
                                      "the half-saturation constant, on the graph and in reports."
                                    : "The unit of " + std::string(def.yName) + ", e.g. nmol/min/mg. It is shown "
                                      "with Vmax and v0, on the graph and in reports.");
            }
        }
        if (focusTable_) ImGui::SetScrollY(0.0f);

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(rows_.size()));
        if (focusTable_) clipper.IncludeItemByIndex(0);
        if (hoverRow >= 0) clipper.IncludeItemByIndex(hoverRow);
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                auto& row = rows_[static_cast<size_t>(i)];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                if (i == hoverRow) highlight_row();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                const bool last = i + 1 == static_cast<int>(rows_.size());
                // Keep row numbers out of the Tab order so Tab goes X -> Y -> next X.
                ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
                ImGui::Selectable(last ? "*" : std::to_string(i + 1).c_str(), false,
                                  ImGuiSelectableFlags_AllowOverlap);
                ImGui::PopItemFlag();
                if (i == hoverRow && !ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
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
                        mark_table_changed();
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
        mark_table_changed();
    }
    if (deleteAt >= 0) {
        rows_.erase(rows_.begin() + deleteAt);
        mark_table_changed();
    }
}

void App::draw_run_points() {
    const auto* item = shown_item();
    if (!item) return;
    const auto& def = kin::model_def(item->analysis ? item->analysis->model() : model_);
    const bool sheet = import_.type == ImportType::Spreadsheet;
    section_fold_heading(Section::Data, "Data", dataOpen_, true);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s #%d, read-only", sheet ? "run" : "dataset", item->runNumber);
    help_link(HelpId::FileData);
    heading_gap();
    if (!dataOpen_) return;

    // The points the run was fitted to, or for a run that failed, its points
    // as the file gives them.
    std::vector<kin::Point> pts;
    if (item->analysis) pts = item->analysis->data;
    else if (sheet && import_.sheet)
        if (const auto* run = import_.sheet->find_run(item->runNumber)) pts = import_.sheet->points(*run, import_.negativeIsMissing);

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInner | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_BordersOuter;
    const float noteH = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
    if (ImGui::BeginTable("##runpoints", 3, flags, ImVec2(table_width(), table_height(leftFooterHeight_ + noteH)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 2.5f);
        ImGui::TableSetupColumn(x_heading(def.id).c_str(), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(y_heading(def.id).c_str(), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < pts.size(); ++i) {
            ImGui::TableNextRow();
            // The point under the mouse on the graph (the run's own data).
            const bool hovered = item->analysis && static_cast<int>(i) == hoveredPoint_;
            if (hovered) highlight_row();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%zu", i + 1);
            if (hovered && !ImGui::IsItemVisible()) ImGui::SetScrollHereY(0.5f);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(number_text(pts[i].x).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(number_text(pts[i].y).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, palette().textDim);
    ImGui::TextWrapped("To change these points, press Edit this run in the table, above.");
    ImGui::PopStyleColor();
}

}  // namespace app
