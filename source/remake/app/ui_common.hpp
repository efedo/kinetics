#pragma once
// Small helpers shared by the UI files.

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "app/theme.hpp"

namespace app {

// Opacity of results that are out of date (the data or settings changed).
inline constexpr float kStaleAlpha = 0.45f;

enum class CellState { Blank, Number, Invalid };

inline CellState parse_cell(const std::string& s, double& out) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return CellState::Blank;
    const size_t e = s.find_last_not_of(" \t");
    const std::string t = s.substr(b, e - b + 1);
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end != '\0' || !std::isfinite(v)) return CellState::Invalid;
    out = v;
    return CellState::Number;
}

inline std::string number_text(double v) {
    if (!std::isfinite(v)) return {};
    char buf[48];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}

inline std::filesystem::path path_from_utf8(const std::string& s) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

// A tooltip for the last item that wraps long text.
inline void item_tooltip(const char* text) {
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

inline void item_tooltip(const std::string& text) { item_tooltip(text.c_str()); }

// A short warning in amber, wrapped to the available width, with any
// explanation in a tooltip.
inline void warning_text(const char* text, const std::string& tooltip) {
    ImGui::PushStyleColor(ImGuiCol_Text, palette().warning);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    if (!tooltip.empty()) item_tooltip(tooltip);
}

inline void no_results_text() { ImGui::TextDisabled("No results yet: press Fit (F5)."); }

// A label before the next widget, which then takes a "##id" label (ImGui
// draws labels after widgets, which reverses the reading order).
inline void field_label(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine();
}

inline void field_label(const std::string& label) { field_label(label.c_str()); }

// Text in a larger size of the heading (or the current) font.
inline void push_heading_font(float scale = 1.12f) {
    ImGui::PushFont(fonts().heading, ImGui::GetStyle().FontSizeBase * scale);
}

inline void push_large_font(float scale = 1.3f) { ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * scale); }

// A section title in the heading font, with some space above it unless it
// starts its panel. Space and type set sections apart, not lines.
inline void section_heading(const char* title, bool first = false) {
    if (!first) ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.5f));
    push_heading_font();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
    ImGui::PopFont();
}

inline void section_heading(const std::string& title, bool first = false) { section_heading(title.c_str(), first); }

// A section heading that folds: the same title after a small arrow. A click
// on either folds or unfolds the section (`open`). Returns true when clicked.
inline bool foldable_heading(const char* title, bool& open, bool first = false) {
    if (!first) ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.5f));
    push_heading_font();
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    const float h = ImGui::GetFrameHeight();
    const float arrowW = std::round(fs * 1.0f);
    const ImVec2 ts = ImGui::CalcTextSize(title);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 size(arrowW + ts.x + st.FramePadding.x, h);
    ImGui::AlignTextToFramePadding();  // so that buttons and links after it line up with the title
    ImGui::PushID(title);
    const bool clicked = ImGui::InvisibleButton("##fold", size);
    ImGui::PopID();
    if (clicked) open = !open;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hovered)
        dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                          st.FrameRounding);
    // The arrow: pointing down when open, right when folded.
    const ImU32 arrow = ImGui::GetColorU32(hovered ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : palette().textDim);
    const float r = fs * 0.22f;
    const ImVec2 c(p.x + arrowW * 0.45f, p.y + h * 0.5f);
    if (open)
        dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.55f), ImVec2(c.x + r, c.y - r * 0.55f),
                              ImVec2(c.x, c.y + r * 0.65f), arrow);
    else
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.55f, c.y - r), ImVec2(c.x + r * 0.65f, c.y),
                              ImVec2(c.x - r * 0.55f, c.y + r), arrow);
    dl->AddText(ImVec2(p.x + arrowW, p.y + std::round((h - ts.y) * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text), title);
    ImGui::PopFont();
    ImGui::SetItemTooltip(open ? "Click to fold this section" : "Click to show this section");
    return clicked;
}

// Text drawn at the right edge of the line, after SameLine.
inline void same_line_right(float width) {
    const float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
    ImGui::SameLine(std::max(x, ImGui::GetCursorPosX()));
}

// SameLine if an item `width` wide still fits after the last item;
// otherwise the next item starts a new line.
inline void same_line_if_fits(float width) {
    // Called after an item: the cursor is at the start of the next line.
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <= right) ImGui::SameLine();
}

// The width of Button(label) or SmallButton(label), which pads only less
// vertically.
inline float button_width(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

// Text that fits `width`, shortened with "..." at the end if need be.
inline std::string fit_text(const std::string& text, float width) {
    if (ImGui::CalcTextSize(text.c_str()).x <= width) return text;
    const float dots = ImGui::CalcTextSize("...").x;
    size_t n = text.size();
    while (n > 0) {
        --n;
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;  // UTF-8 boundary
        if (ImGui::CalcTextSize(text.c_str(), text.c_str() + n).x + dots <= width) break;
    }
    return text.substr(0, n) + "...";
}

// One row of a two-column form table: the label, then the widget in the
// second column at full width.
inline void form_row(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-FLT_MIN);
}

// A value and its standard deviation, both rounded to the precision the SD
// supports (two significant figures of the SD): {"2.71", "0.14"}. Very large
// or very small numbers use scientific notation. Without a usable SD (a
// fixed parameter) the value keeps six significant figures and sd is empty.
struct ValueText {
    std::string value;
    std::string sd;
};

inline ValueText format_value_sd(double v, double sd) {
    char a[64], b[64];
    if (!std::isfinite(v)) return {"n/a", ""};
    if (!std::isfinite(sd) || sd <= 0.0) {
        std::snprintf(a, sizeof a, "%.6g", v);
        return {a, ""};
    }
    const int sdExp = static_cast<int>(std::floor(std::log10(sd)));
    const int decimals = 1 - sdExp;
    if (std::max(std::fabs(v), sd) < 1e6 && decimals <= 6) {
        const int d = std::max(decimals, 0);
        const double q = std::pow(10.0, -decimals);
        double vr = decimals < 0 ? std::round(v / q) * q : v;
        const double sr = decimals < 0 ? std::round(sd / q) * q : sd;
        if (std::fabs(vr) < 0.5 * std::pow(10.0, -d)) vr = 0.0;  // no "-0.00"
        std::snprintf(a, sizeof a, "%.*f", d, vr);
        std::snprintf(b, sizeof b, "%.*f", d, sr);
    } else {
        const int vExp = v != 0.0 ? static_cast<int>(std::floor(std::log10(std::fabs(v)))) : sdExp;
        const int sig = std::clamp(vExp - sdExp + 2, 2, 8);
        std::snprintf(a, sizeof a, "%.*e", sig - 1, v);
        std::snprintf(b, sizeof b, "%.1e", sd);
    }
    return {a, b};
}

// "2.71 ± 0.14", or the value alone without a usable SD.
inline std::string format_value_pm(double v, double sd) {
    const auto t = format_value_sd(v, sd);
    return t.sd.empty() ? t.value : t.value + " \xC2\xB1 " + t.sd;
}

// The SD as a percentage of the value: "5.1%", "22%", ">1000%".
inline std::string format_percent(double pct) {
    if (!std::isfinite(pct)) return {};
    if (pct >= 1000.0) return ">1000%";
    char buf[32];
    std::snprintf(buf, sizeof buf, pct < 10.0 ? "%.1f%%" : "%.0f%%", pct);
    return buf;
}

// Decimals that show residuals of this standard deviation to about two
// significant figures.
inline int residual_decimals(double sd) {
    if (!std::isfinite(sd) || sd <= 0.0) return 4;
    return std::clamp(2 - static_cast<int>(std::floor(std::log10(sd))), 0, 8);
}

inline std::string format_fixed(double v, int decimals, bool sign = false) {
    char buf[64];
    if (!std::isfinite(v)) return "n/a";
    if (std::fabs(v) < 0.5 * std::pow(10.0, -decimals)) v = 0.0;
    std::snprintf(buf, sizeof buf, sign ? "%+.*f" : "%.*f", decimals, v);
    return buf;
}

}  // namespace app
