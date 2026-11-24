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
    // Added for the newer styles.
    ImVec4 card;         // the panels' background, on windowBg (a style with cards)
    ImVec4 frameBorder;  // around input fields (a style with frame borders)
    ImVec4 accentText;   // text on accentFill
    ImVec4 checkFill;    // a ticked check box or chosen radio button
    ImVec4 checkMark;
};

constexpr ImVec4 kWhite(1.0f, 1.0f, 1.0f, 1.0f);
constexpr ImVec4 kBlack(0.0f, 0.0f, 0.0f, 1.0f);

// ---- Flat: neutral greys with one muted accent (the original look) -----

constexpr Scheme kStandardDark{
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
    rgb(0x1e1f22), rgb(0x36383d), rgb(0xdcdde0), rgb(0x2a2c30), rgb(0xdcdde0),
};

constexpr Scheme kStandardLight{
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
    rgb(0xf3f3f5), rgb(0xcfd1d6), rgb(0x1f2124), rgb(0xe5e6e9), rgb(0x1f2124),
};

// ---- Soft: white cards on a cool grey canvas, an indigo accent ----------

constexpr Scheme kSoftLight{
    false,
    {rgb(0x1f2937), rgb(0x6b7280), rgb(0x4f46e5), rgb(0xb45309), rgb(0xdc2626), rgb(0xfee2e2), rgb(0x059669)},
    rgb(0x4b5563),
    rgb(0xeef1f5), rgb(0xffffff), rgb(0xffffff, 0.99f), rgb(0xffffff), rgb(0xf3f4f6), rgb(0xeef1f5),
    rgb(0xf8fafc), rgb(0xf1f5f9), rgb(0xffffff),
    rgb(0xeef1f5), rgb(0xe3e8ef), rgb(0xd8dee7),
    rgb(0xe2e6ec),
    rgb(0x4f46e5), rgb(0x4338ca), rgb(0x3730a3),
    with_alpha(kBlack, 0.022f), with_alpha(kBlack, 0.06f), with_alpha(kBlack, 0.2f),
    with_alpha(rgb(0x0f172a), 0.35f),
    rgb(0xffffff), rgb(0xd6dbe3), kWhite, rgb(0x4f46e5), kWhite,
};

constexpr Scheme kSoftDark{
    true,
    {rgb(0xe6edf3), rgb(0x8b949e), rgb(0x818cf8), rgb(0xe3b341), rgb(0xf47067), rgb(0x4b1c1c), rgb(0x56d364)},
    rgb(0xadbac7),
    rgb(0x0e1117), rgb(0x161b22), rgb(0x1c2128, 0.99f), rgb(0x0d1117), rgb(0x1c2128), rgb(0x161b22),
    rgb(0x0d1117), rgb(0x131820), rgb(0x0d1117),
    rgb(0x21262d), rgb(0x2a313a), rgb(0x30363d),
    rgb(0x2a3038),
    rgb(0x4f46e5), rgb(0x5b54ea), rgb(0x6366f1),
    with_alpha(kWhite, 0.025f), with_alpha(kWhite, 0.06f), with_alpha(kWhite, 0.18f),
    with_alpha(kBlack, 0.6f),
    rgb(0x161b22), rgb(0x30363d), kWhite, rgb(0x6366f1), kWhite,
};

// ---- Slate: crisp neutrals, high contrast, a teal accent (as the icon) --

constexpr Scheme kSlateLight{
    false,
    {rgb(0x18181b), rgb(0x71717a), rgb(0x0d9488), rgb(0xb45309), rgb(0xdc2626), rgb(0xfee2e2), rgb(0x059669)},
    rgb(0x3f3f46),
    rgb(0xf4f4f5), rgb(0xffffff), rgb(0xffffff, 0.99f), rgb(0xffffff), rgb(0xfafafa), rgb(0xf4f4f5),
    rgb(0xffffff), rgb(0xfafafa), rgb(0xffffff),
    rgb(0xf4f4f5), rgb(0xe4e4e7), rgb(0xd4d4d8),
    rgb(0xe4e4e7),
    rgb(0x0f766e), rgb(0x0d9488), rgb(0x115e59),
    with_alpha(kBlack, 0.02f), with_alpha(kBlack, 0.06f), with_alpha(kBlack, 0.22f),
    with_alpha(rgb(0x09090b), 0.35f),
    rgb(0xffffff), rgb(0xd4d4d8), kWhite, rgb(0x0f766e), kWhite,
};

constexpr Scheme kSlateDark{
    true,
    {rgb(0xfafafa), rgb(0xa1a1aa), rgb(0x2dd4bf), rgb(0xfbbf24), rgb(0xf87171), rgb(0x4c1d1d), rgb(0x4ade80)},
    rgb(0xd4d4d8),
    rgb(0x09090b), rgb(0x18181b), rgb(0x18181b, 0.99f), rgb(0x09090b), rgb(0x1f1f23), rgb(0x18181b),
    rgb(0x09090b), rgb(0x111113), rgb(0x09090b),
    rgb(0x27272a), rgb(0x3f3f46), rgb(0x52525b),
    rgb(0x27272a),
    rgb(0x0f766e), rgb(0x0d9488), rgb(0x14b8a6),
    with_alpha(kWhite, 0.025f), with_alpha(kWhite, 0.06f), with_alpha(kWhite, 0.2f),
    with_alpha(kBlack, 0.6f),
    rgb(0x18181b), rgb(0x3f3f46), kWhite, rgb(0x0d9488), kWhite,
};

// ---- Paper: a warm lab notebook, ivory cards and a forest-green accent --

constexpr Scheme kPaperLight{
    false,
    {rgb(0x2b2621), rgb(0x7a7066), rgb(0x1f6f54), rgb(0xa16207), rgb(0xb42318), rgb(0xfbe3df), rgb(0x2f7d4f)},
    rgb(0x4a433c),
    rgb(0xf1ece3), rgb(0xfffdf8), rgb(0xfffdf8, 0.99f), rgb(0xfffefb), rgb(0xf6f1e8), rgb(0xf1ece3),
    rgb(0xffffff), rgb(0xfbf8f2), rgb(0xffffff),
    rgb(0xefe9df), rgb(0xe6ded1), rgb(0xddd3c3),
    rgb(0xe4ddd1),
    rgb(0x1f6f54), rgb(0x26805f), rgb(0x195c45),
    with_alpha(rgb(0x5a4a32), 0.03f), with_alpha(rgb(0x5a4a32), 0.08f), with_alpha(rgb(0x5a4a32), 0.25f),
    with_alpha(rgb(0x2b2621), 0.35f),
    rgb(0xfffdf8), rgb(0xd8cfc0), kWhite, rgb(0x1f6f54), kWhite,
};

constexpr Scheme kPaperDark{
    true,
    {rgb(0xece7df), rgb(0xa39b8f), rgb(0x5fbf95), rgb(0xe0b25a), rgb(0xe9806f), rgb(0x4a2520), rgb(0x7fcf8f)},
    rgb(0xc9c1b5),
    rgb(0x1b1a17), rgb(0x23211d), rgb(0x2a2823, 0.99f), rgb(0x1b1a17), rgb(0x2a2823), rgb(0x23211d),
    rgb(0x1b1a17), rgb(0x201e1b), rgb(0x1b1a17),
    rgb(0x2e2b26), rgb(0x38342e), rgb(0x423d36),
    rgb(0x36322c),
    rgb(0x2e8f6a), rgb(0x349d75), rgb(0x3aab80),
    with_alpha(kWhite, 0.025f), with_alpha(kWhite, 0.06f), with_alpha(kWhite, 0.18f),
    with_alpha(kBlack, 0.6f),
    rgb(0x23211d), rgb(0x423d36), kWhite, rgb(0x2e8f6a), kWhite,
};

// Sizes, in pixels at 96 dpi.
struct Metrics {
    float windowRounding, childRounding, frameRounding, popupRounding, tabRounding, grabRounding;
    ImVec2 framePadding, itemSpacing, cellPadding, windowPadding;
    float frameBorder, scrollbarSize, tabOverline;
    bool cards;
};

struct Preset {
    StyleChoice id;
    const char* name;   // in kinetics.ini and on the command line
    const char* label;  // in the menu
    Metrics m;
    const Scheme* light;
    const Scheme* dark;
};

constexpr Metrics kSlateMetrics{8, 8, 6, 8, 6, 6, {10, 6}, {10, 7}, {8, 4}, {14, 12}, 1, 10, 2, true};

constexpr Preset kPresets[] = {
    // Slate's crisp neutrals when light, Soft's deep blue-greys when dark,
    // with Slate's sizes either way.
    {StyleChoice::Default, "default", "Default", kSlateMetrics, &kSlateLight, &kSoftDark},
    {StyleChoice::Soft, "soft", "Soft",
     {10, 12, 8, 10, 8, 8, {11, 7}, {10, 8}, {9, 5}, {16, 14}, 1, 10, 0, true}, &kSoftLight, &kSoftDark},
    {StyleChoice::Slate, "slate", "Slate", kSlateMetrics, &kSlateLight, &kSlateDark},
    {StyleChoice::Paper, "paper", "Paper",
     {6, 6, 4, 6, 4, 4, {10, 6}, {10, 8}, {9, 5}, {16, 14}, 1, 11, 2, true}, &kPaperLight, &kPaperDark},
    {StyleChoice::Flat, "flat", "Flat (the original look)",
     {6, 0, 4, 4, 4, 4, {6, 4}, {8, 6}, {6, 3}, {8, 8}, 0, 14, 2, false}, &kStandardLight, &kStandardDark},
};

const Preset& preset(StyleChoice s) {
    for (const auto& p : kPresets)
        if (p.id == s) return p;
    return kPresets[0];
}

// Follow the system's light or dark setting unless the user picks one.
ThemeChoice g_choice = ThemeChoice::System;
StyleChoice g_style = StyleChoice::Default;
float g_scale = 1.0f;
const Scheme* g_scheme = &kSoftDark;

const Scheme& resolve(ThemeChoice choice) {
    const Preset& p = preset(g_style);
    switch (choice) {
    case ThemeChoice::Dark: return *p.dark;
    case ThemeChoice::Light: return *p.light;
    case ThemeChoice::System: break;
    }
    // Dark unless the system asks for light: the app was designed dark first.
    return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_LIGHT ? *p.light : *p.dark;
}

void set_imgui_colours(const Scheme& s) {
    ImVec4* c = ImGui::GetStyle().Colors;
    const ImVec4 none(0.0f, 0.0f, 0.0f, 0.0f);
    const Palette& p = s.p;
    const bool cards = preset(g_style).m.cards;

    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.textDim;
    c[ImGuiCol_WindowBg] = s.windowBg;
    c[ImGuiCol_ChildBg] = none;
    c[ImGuiCol_PopupBg] = s.popupBg;
    c[ImGuiCol_Border] = cards ? s.frameBorder : s.border;
    c[ImGuiCol_BorderShadow] = none;

    c[ImGuiCol_FrameBg] = s.frame;
    c[ImGuiCol_FrameBgHovered] = s.frameHover;
    c[ImGuiCol_FrameBgActive] = s.frameActive;
    c[ImGuiCol_CheckMark] = s.checkMark;
    c[ImGuiCol_CheckboxSelectedBg] = s.checkFill;
    if (cards) {
        // An accent tick on a light accent tint, and an accent dot in a radio
        // button, which draws its dot on the plain field.
        c[ImGuiCol_CheckMark] = p.accent;
        c[ImGuiCol_CheckboxSelectedBg] = with_alpha(p.accent, s.dark ? 0.28f : 0.16f);
    }
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

    if (cards) {
        // Tabs as a row of pills: the selected one filled, the others plain
        // labels that light up under the mouse.
        c[ImGuiCol_Tab] = none;
        c[ImGuiCol_TabHovered] = s.controlHover;
        c[ImGuiCol_TabSelected] = with_alpha(p.accent, s.dark ? 0.22f : 0.12f);
        c[ImGuiCol_TabSelectedOverline] = p.accent;
        c[ImGuiCol_TabDimmed] = none;
        c[ImGuiCol_TabDimmedSelected] = with_alpha(p.accent, s.dark ? 0.16f : 0.09f);
        c[ImGuiCol_TabDimmedSelectedOverline] = with_alpha(p.accent, 0.5f);
    } else {
        // Unselected tabs read as plain labels; the selected one is a grey tab
        // with an accent line along its top.
        c[ImGuiCol_Tab] = s.windowBg;
        c[ImGuiCol_TabHovered] = s.controlHover;
        c[ImGuiCol_TabSelected] = s.control;
        c[ImGuiCol_TabSelectedOverline] = p.accent;
        c[ImGuiCol_TabDimmed] = s.windowBg;
        c[ImGuiCol_TabDimmedSelected] = s.control;
        c[ImGuiCol_TabDimmedSelectedOverline] = with_alpha(p.accent, 0.5f);
    }

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

// The style's sizes, scaled for the display. The text size (FontScaleMain)
// and the font size are kept.
void apply_metrics() {
    ImGuiStyle& cur = ImGui::GetStyle();
    const float fontMain = cur.FontScaleMain, fontBase = cur.FontSizeBase;
    const Metrics& m = preset(g_style).m;
    ImGuiStyle s;
    s.WindowRounding = m.windowRounding;
    s.ChildRounding = m.childRounding;
    s.FrameRounding = m.frameRounding;
    s.PopupRounding = m.popupRounding;
    s.TabRounding = m.tabRounding;
    s.GrabRounding = m.grabRounding;
    s.ScrollbarRounding = m.scrollbarSize;
    s.FramePadding = m.framePadding;
    s.ItemSpacing = m.itemSpacing;
    s.CellPadding = m.cellPadding;
    s.WindowPadding = m.windowPadding;
    s.FrameBorderSize = m.frameBorder;
    s.PopupBorderSize = 1.0f;
    s.ScrollbarSize = m.scrollbarSize;
    s.SeparatorTextBorderSize = 1.0f;
    s.TabBarOverlineSize = m.tabOverline;
    s.TabBarBorderSize = m.cards ? 0.0f : s.TabBarBorderSize;
    s.ScaleAllSizes(g_scale);
    s.FontScaleDpi = g_scale;
    s.FontScaleMain = fontMain;
    s.FontSizeBase = fontBase;
    cur = s;
}

}  // namespace

void init_style(float scale) {
    g_scale = scale;
    apply_metrics();
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

void set_style(StyleChoice style) {
    g_style = style;
    apply_metrics();
    apply(resolve(g_choice));
}

StyleChoice style_choice() { return g_style; }

const char* style_name(StyleChoice style) { return preset(style).name; }
const char* style_label(StyleChoice style) { return preset(style).label; }

std::optional<StyleChoice> style_from_name(std::string_view name) {
    for (const auto& p : kPresets)
        if (name == p.name) return p.id;
    return std::nullopt;
}

bool style_cards() { return preset(g_style).m.cards; }
ImVec4 card_colour() { return g_scheme->card; }
ImVec4 primary_text_colour() { return g_scheme->accentText; }

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
    ImGui::PushStyleColor(ImGuiCol_Text, s.accentText);
    ImGui::PushStyleColor(ImGuiCol_Border, s.accentFill);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(5);
    return pressed;
}

}  // namespace app
