#pragma once

// Fonts, colors and shapes. Colors come from a palette per mode (light and dark) that the
// theme editor can change; everything else in the style is derived from the palette.

#include "imgui.h"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace opgui {

struct Fonts {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;  // falls back to regular
};

extern Fonts g_fonts;

// Loads UI fonts (system fonts where available, ImGui's built-in font otherwise).
void loadFonts();

// Base size (unscaled pixels) used for body text.
constexpr float kBaseFontSize = 16.0f;

struct Palette {
    ImVec4 accent;      // primary actions, selection, links
    ImVec4 positive;    // on track, paid, approved
    ImVec4 negative;    // overdue, over budget, errors
    ImVec4 warning;     // watch, needs review
    ImVec4 text;
    ImVec4 muted;       // secondary text
    ImVec4 background;  // window background
    ImVec4 panel;       // cards and popups
    ImVec4 sidebar;     // sidebar, menu bar and table headers
    ImVec4 field;       // input fields
    ImVec4 border;
};

struct ThemeSettings {
    Palette light;
    Palette dark;
    float rounding = 4.0f;   // corner radius of controls, in pixels at 100% scale
    float density = 1.0f;    // spacing and padding multiplier
    float textScale = 1.0f;  // text size multiplier
};

ThemeSettings& themeSettings();
Palette defaultPalette(bool dark);
const std::vector<const char*>& presetNames();
Palette presetPalette(std::size_t index, bool dark);

// Rebuilds the ImGui style from the settings; call after changing them.
void applyTheme(bool dark);
bool themeIsDark();

// Config lines ("theme.light.accent=#007A7A", ...) and their reader. readThemeConfig returns
// false for keys that aren't theme settings.
void writeThemeConfig(std::ostream& out);
bool readThemeConfig(const std::string& key, const std::string& value);

// Semantic colors that follow the current theme.
ImVec4 colorAccent();    // primary action
ImVec4 colorPositive();  // paid, balanced
ImVec4 colorNegative();  // overdue, negative amounts, errors
ImVec4 colorWarning();   // partial, attention
ImVec4 colorMuted();     // secondary text
ImVec4 colorCardBg();

}  // namespace opgui
