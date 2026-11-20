#pragma once
// Small helpers shared by the UI files.

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "app/theme.hpp"

namespace app {

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

}  // namespace app
