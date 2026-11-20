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
