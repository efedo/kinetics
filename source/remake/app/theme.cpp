#include "app/theme.hpp"

#include <SDL3/SDL_video.h>
#include <implot.h>

namespace app {
namespace {

// 0xRRGGBB as an ImGui colour.
constexpr ImVec4 rgb(unsigned v, float a = 1.0f) {
    return ImVec4(static_cast<float>((v >> 16) & 0xFF) / 255.0f, static_cast<float>((v >> 8) & 0xFF) / 255.0f,
                  static_cast<float>(v & 0xFF) / 255.0f, a);
}

constexpr ImVec4 with_alpha(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }

struct Scheme {
    bool dark;
    Palette p;
    ImVec4 textMid;  // axis labels
    ImVec4 windowBg, menuBg, popupBg, plotBg, tableHeaderBg, titleBgActive;
    ImVec4 frame, frameHover, frameActive;            // input fields, checkboxes
    ImVec4 control, controlHover, controlActive;      // buttons, headers, selected tab
    ImVec4 border;
    ImVec4 accentFill, accentFillHover, accentFillActive;  // the Fit button, behind plain text
    ImVec4 rowAlt, grid, tick;                         // faint tints over the background
    ImVec4 modalDim;
};

constexpr ImVec4 kWhite(1.0f, 1.0f, 1.0f, 1.0f);
constexpr ImVec4 kBlack(0.0f, 0.0f, 0.0f, 1.0f);

constexpr Scheme kDark{
    true,
    {rgb(0xdcdde0), rgb(0x8c8f95), rgb(0x6a9ed3), rgb(0xd9b35f), rgb(0xe0706a), rgb(0x5a2828), rgb(0x7cc28a)},
    rgb(0xb3b5ba),
    rgb(0x1e1f22), rgb(0x18191b), rgb(0x26272b, 0.98f), rgb(0x18191b), rgb(0x26272b), rgb(0x26272b),
    rgb(0x2a2c30), rgb(0x33353a), rgb(0x3a3d42),
    rgb(0x303237), rgb(0x3b3e44), rgb(0x464950),
    rgb(0x36383d),
    rgb(0x335677), rgb(0x3c6489), rgb(0x46729a),
    with_alpha(kWhite, 0.025f), with_alpha(kWhite, 0.06f), with_alpha(kWhite, 0.15f),
    with_alpha(kBlack, 0.55f),
};

constexpr Scheme kLight{
    false,
    {rgb(0x1f2124), rgb(0x6b6f76), rgb(0x2b6cb0), rgb(0x956400), rgb(0xc0362c), rgb(0xf7d4d1), rgb(0x2e7d3c)},
    rgb(0x45484e),
    rgb(0xf3f3f5), rgb(0xe8e9ec), rgb(0xffffff, 0.98f), rgb(0xffffff), rgb(0xe8e9ec), rgb(0xdfe0e4),
    rgb(0xe5e6e9), rgb(0xdcdee2), rgb(0xd3d5da),
    rgb(0xd9dbdf), rgb(0xcdd0d5), rgb(0xc0c4ca),
    rgb(0xcfd1d6),
    rgb(0xc6d8ee), rgb(0xb5cdea), rgb(0xa3c0e4),
    with_alpha(kBlack, 0.03f), with_alpha(kBlack, 0.08f), with_alpha(kBlack, 0.25f),
    with_alpha(kBlack, 0.25f),
};

// Follow the system's light or dark setting unless the user picks one.
ThemeChoice g_choice = ThemeChoice::System;
const Scheme* g_scheme = &kDark;

const Scheme& resolve(ThemeChoice choice) {
    switch (choice) {
    case ThemeChoice::Dark: return kDark;
    case ThemeChoice::Light: return kLight;
    case ThemeChoice::System: break;
    }
    // Dark unless the system asks for light: the app was designed dark first.
    return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_LIGHT ? kLight : kDark;
}

void set_imgui_colours(const Scheme& s) {
    ImVec4* c = ImGui::GetStyle().Colors;
    const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);
    const Palette& p = s.p;

    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textDim;
    c[ImGuiCol_WindowBg] = s.windowBg;
    c[ImGuiCol_ChildBg] = none;
    c[ImGuiCol_PopupBg] = s.popupBg;
    c[ImGuiCol_Border] = s.border;
    c[ImGuiCol_BorderShadow] = none;

    c[ImGuiCol_FrameBg] = s.frame;
    c[ImGuiCol_FrameBgHovered] = s.frameHover;
    c[ImGuiCol_FrameBgActive] = s.frameActive;
    c[ImGuiCol_CheckMark] = p.text;
    c[ImGuiCol_CheckboxSelectedBg] = s.frame;
    c[ImGuiCol_SliderGrab] = s.controlActive;
    c[ImGuiCol_SliderGrabActive] = p.accent;
    c[ImGuiCol_InputTextCursor] = p.text;
    c[ImGuiCol_TextSelectedBg] = with_alpha(p.accent, s.dark ? 0.35f : 0.25f);

    c[ImGuiCol_TitleBg] = s.menuBg;
    c[ImGuiCol_TitleBgActive] = s.titleBgActive;
    c[ImGuiCol_TitleBgCollapsed] = s.menuBg;
    c[ImGuiCol_MenuBarBg] = s.menuBg;

    c[ImGuiCol_ScrollbarBg] = none;
    c[ImGuiCol_ScrollbarGrab] = s.control;
    c[ImGuiCol_ScrollbarGrabHovered] = s.controlHover;
    c[ImGuiCol_ScrollbarGrabActive] = s.controlActive;

    c[ImGuiCol_Button] = s.control;
    c[ImGuiCol_ButtonHovered] = s.controlHover;
    c[ImGuiCol_ButtonActive] = s.controlActive;
    c[ImGuiCol_Header] = s.control;
    c[ImGuiCol_HeaderHovered] = s.controlHover;
    c[ImGuiCol_HeaderActive] = s.controlActive;

    c[ImGuiCol_Separator] = s.border;
    c[ImGuiCol_SeparatorHovered] = with_alpha(p.accent, 0.6f);
    c[ImGuiCol_SeparatorActive] = p.accent;
    c[ImGuiCol_ResizeGrip] = with_alpha(s.control, 0.5f);
    c[ImGuiCol_ResizeGripHovered] = with_alpha(p.accent, 0.6f);
    c[ImGuiCol_ResizeGripActive] = p.accent;

    // Unselected tabs read as plain labels; the selected one is a grey tab
    // with an accent line along its top.
    c[ImGuiCol_Tab] = s.windowBg;
    c[ImGuiCol_TabHovered] = s.controlHover;
    c[ImGuiCol_TabSelected] = s.control;
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = s.windowBg;
    c[ImGuiCol_TabDimmedSelected] = s.control;
    c[ImGuiCol_TabDimmedSelectedOverline] = with_alpha(p.accent, 0.5f);

    c[ImGuiCol_PlotLines] = p.textDim;
    c[ImGuiCol_PlotLinesHovered] = p.accent;
    c[ImGuiCol_PlotHistogram] = p.textDim;
    c[ImGuiCol_PlotHistogramHovered] = p.accent;

    c[ImGuiCol_TableHeaderBg] = s.tableHeaderBg;
    c[ImGuiCol_TableBorderStrong] = s.border;
    c[ImGuiCol_TableBorderLight] = with_alpha(s.border, 0.7f);
    c[ImGuiCol_TableRowBg] = none;
    c[ImGuiCol_TableRowBgAlt] = s.rowAlt;

    c[ImGuiCol_TextLink] = p.accent;
    c[ImGuiCol_TreeLines] = s.border;
    c[ImGuiCol_DragDropTarget] = p.accent;
    c[ImGuiCol_DragDropTargetBg] = none;
    c[ImGuiCol_UnsavedMarker] = p.text;
    c[ImGuiCol_NavCursor] = p.accent;
    c[ImGuiCol_NavWindowingHighlight] = with_alpha(p.textDim, 0.7f);
    c[ImGuiCol_NavWindowingDimBg] = with_alpha(s.modalDim, 0.2f);
    c[ImGuiCol_ModalWindowDimBg] = s.modalDim;
}

void set_implot_colours(const Scheme& s) {
    ImVec4* c = ImPlot::GetStyle().Colors;
    const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImPlotCol_FrameBg] = none;
    c[ImPlotCol_PlotBg] = s.plotBg;
    c[ImPlotCol_PlotBorder] = s.border;
    c[ImPlotCol_LegendBg] = with_alpha(s.popupBg, 0.9f);
    c[ImPlotCol_LegendBorder] = s.border;
    c[ImPlotCol_LegendText] = s.p.text;
    c[ImPlotCol_TitleText] = s.p.text;
    c[ImPlotCol_InlayText] = s.p.text;
    c[ImPlotCol_AxisText] = s.textMid;
    c[ImPlotCol_AxisGrid] = s.grid;
    c[ImPlotCol_AxisTick] = s.tick;
    c[ImPlotCol_AxisBg] = none;
    c[ImPlotCol_AxisBgHovered] = with_alpha(s.controlHover, 0.6f);
    c[ImPlotCol_AxisBgActive] = with_alpha(s.controlActive, 0.6f);
    c[ImPlotCol_Selection] = s.p.accent;
    c[ImPlotCol_Crosshairs] = s.p.textDim;
}

void apply(const Scheme& s) {
    g_scheme = &s;
    // Start from ImGui's own scheme so any colour a later version adds gets a
    // sensible value.
    if (s.dark) {
        ImGui::StyleColorsDark();
        ImPlot::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
        ImPlot::StyleColorsLight();
    }
    set_imgui_colours(s);
    set_implot_colours(s);
}

}  // namespace

void init_style(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 6.0f;
    s.FrameRounding = 4.0f;
    s.GrabRounding = 4.0f;
    s.TabRounding = 4.0f;
    s.PopupRounding = 4.0f;
    s.FramePadding = ImVec2(6.0f, 4.0f);
    s.ItemSpacing = ImVec2(8.0f, 6.0f);
    s.CellPadding = ImVec2(6.0f, 3.0f);
    s.SeparatorTextBorderSize = 1.0f;
    s.TabBarOverlineSize = 2.0f;
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;
    apply(resolve(g_choice));
}

void set_theme(ThemeChoice choice) {
    g_choice = choice;
    apply(resolve(choice));
}

ThemeChoice theme_choice() { return g_choice; }

void system_theme_changed() {
    if (g_choice == ThemeChoice::System) apply(resolve(g_choice));
}

const Palette& palette() { return g_scheme->p; }

const char* theme_name(ThemeChoice choice) {
    switch (choice) {
    case ThemeChoice::Dark: return "dark";
    case ThemeChoice::Light: return "light";
    case ThemeChoice::System: break;
    }
    return "system";
}

std::optional<ThemeChoice> theme_from_name(std::string_view name) {
    if (name == "system") return ThemeChoice::System;
    if (name == "dark") return ThemeChoice::Dark;
    if (name == "light") return ThemeChoice::Light;
    return std::nullopt;
}

Fonts& fonts() {
    static Fonts f;
    return f;
}

ImVec4 bar_colour() { return g_scheme->menuBg; }

bool primary_button(const char* label, const ImVec2& size) {
    const Scheme& s = *g_scheme;
    ImGui::PushStyleColor(ImGuiCol_Button, s.accentFill);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, s.accentFillHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, s.accentFillActive);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

}  // namespace app
