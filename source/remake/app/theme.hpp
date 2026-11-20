#pragma once
// The app's colour schemes: neutral greys with one muted accent, in a dark
// and a light version.
//
// Colour is reserved for things that need attention. The accent marks the main
// action (Fit), the selected tab, the fitted curve and keyboard focus; amber
// and red mark warnings and errors. Ordinary text, values and controls stay
// grey.

#include <imgui.h>

#include <optional>
#include <string_view>

namespace app {

enum class ThemeChoice { System, Dark, Light };

// Colours the UI code uses directly. Plain text needs none: keep colour for
// what needs attention.
struct Palette {
    ImVec4 text;
    ImVec4 textDim;
    ImVec4 accent;   // main action, selected tab, fitted curve
    ImVec4 warning;  // out of date, poor precision, large residuals
    ImVec4 bad;      // errors
    ImVec4 badFill;  // background of a cell that is not a number
};

// Sets the Dear ImGui and ImPlot sizes. `scale` is the display scale (1.0 at
// 96 dpi). Call once, before set_theme.
void init_style(float scale);

// Sets the colours. Dark is the default; System follows the operating
// system's (or browser's) light or dark setting.
void set_theme(ThemeChoice choice);
ThemeChoice theme_choice();

// Call when the operating system's light or dark setting changes.
void system_theme_changed();

// Colours of the theme in use.
const Palette& palette();

// "system", "dark", "light", as stored in the settings file.
const char* theme_name(ThemeChoice choice);
std::optional<ThemeChoice> theme_from_name(std::string_view name);

// A button filled with the accent colour, for the main action on a panel.
bool primary_button(const char* label, const ImVec2& size = ImVec2(0.0f, 0.0f));

}  // namespace app
