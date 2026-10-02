#include "theme.hpp"

#include "platform.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <ostream>

namespace opgui {

Fonts g_fonts;

namespace {

bool g_dark = false;
bool g_haveBase = false;
ImGuiStyle g_base;  // sizes as set up by the platform main (DPI-scaled), before density

ImFont* loadFirst(const char* const* candidates) {
    ImGuiIO& io = ImGui::GetIO();
    for (auto p = candidates; *p; ++p) {
        std::error_code ec;
        if (std::filesystem::exists(*p, ec)) {
            if (ImFont* f = io.Fonts->AddFontFromFileTTF(*p)) return f;
        }
    }
    return nullptr;
}

ImVec4 rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a); }

ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

ImVec4 alpha(ImVec4 c, float a) {
    c.w = a;
    return c;
}

const Palette& current() { return g_dark ? themeSettings().dark : themeSettings().light; }

// "#RRGGBB" <-> color.
std::string hex(ImVec4 c) {
    auto byte = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    char text[8];
    std::snprintf(text, sizeof text, "#%02X%02X%02X", byte(c.x), byte(c.y), byte(c.z));
    return text;
}

bool parseHex(const std::string& s, ImVec4& out) {
    unsigned r = 0, g = 0, b = 0;
    if (s.size() != 7 || s[0] != '#' || std::sscanf(s.c_str() + 1, "%2x%2x%2x", &r, &g, &b) != 3) return false;
    out = rgb(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
    return true;
}

struct Slot {
    const char* key;
    ImVec4 Palette::*member;
};

const Slot kSlots[] = {
    {"accent", &Palette::accent},       {"positive", &Palette::positive}, {"negative", &Palette::negative},
    {"warning", &Palette::warning},     {"text", &Palette::text},         {"muted", &Palette::muted},
    {"background", &Palette::background}, {"panel", &Palette::panel},     {"sidebar", &Palette::sidebar},
    {"field", &Palette::field},         {"border", &Palette::border},
};

}  // namespace

void loadFonts() {
    ImGui::GetStyle().FontSizeBase = kBaseFontSize;
    g_fonts.regular = loadFirst(preferredFonts());
    if (!g_fonts.regular) g_fonts.regular = ImGui::GetIO().Fonts->AddFontDefault();
    g_fonts.bold = loadFirst(preferredBoldFonts());
    if (!g_fonts.bold) g_fonts.bold = g_fonts.regular;
}

Palette defaultPalette(bool dark) {
    Palette p;
    if (dark) {
        p.accent = rgb(64, 190, 180);
        p.positive = rgb(110, 200, 120);
        p.negative = rgb(240, 110, 100);
        p.warning = rgb(235, 180, 80);
        p.text = rgb(235, 237, 240);
        p.muted = rgb(150, 155, 162);
        p.background = rgb(30, 32, 36);
        p.panel = rgb(38, 41, 46);
        p.sidebar = rgb(24, 26, 29);
        p.field = rgb(45, 48, 54);
        p.border = rgb(64, 68, 76);
    } else {
        p.accent = rgb(0, 122, 122);
        p.positive = rgb(30, 130, 40);
        p.negative = rgb(200, 45, 40);
        p.warning = rgb(190, 120, 0);
        p.text = rgb(20, 22, 26);
        p.muted = rgb(110, 116, 125);
        p.background = rgb(247, 248, 250);
        p.panel = rgb(255, 255, 255);
        p.sidebar = rgb(236, 238, 241);
        p.field = rgb(255, 255, 255);
        p.border = rgb(210, 214, 220);
    }
    return p;
}

const std::vector<const char*>& presetNames() {
    static const std::vector<const char*> names = {"Teal (default)", "Blueprint", "Forest", "Terracotta", "Graphite",
                                                   "High contrast"};
    return names;
}

Palette presetPalette(std::size_t index, bool dark) {
    Palette p = defaultPalette(dark);
    switch (index) {
        case 1: p.accent = dark ? rgb(88, 156, 240) : rgb(30, 100, 200); break;
        case 2: p.accent = dark ? rgb(110, 190, 110) : rgb(40, 125, 60); break;
        case 3: p.accent = dark ? rgb(235, 140, 100) : rgb(185, 80, 40); break;
        case 4:
            p.accent = dark ? rgb(170, 178, 190) : rgb(60, 70, 85);
            p.background = dark ? rgb(28, 28, 30) : rgb(244, 244, 245);
            p.sidebar = dark ? rgb(22, 22, 24) : rgb(232, 232, 234);
            break;
        case 5:
            if (dark) {
                p.accent = rgb(90, 200, 255);
                p.text = rgb(255, 255, 255);
                p.muted = rgb(200, 200, 200);
                p.background = rgb(0, 0, 0);
                p.panel = rgb(16, 16, 16);
                p.sidebar = rgb(0, 0, 0);
                p.field = rgb(24, 24, 24);
                p.border = rgb(200, 200, 200);
            } else {
                p.accent = rgb(0, 70, 170);
                p.text = rgb(0, 0, 0);
                p.muted = rgb(60, 60, 60);
                p.background = rgb(255, 255, 255);
                p.panel = rgb(255, 255, 255);
                p.sidebar = rgb(240, 240, 240);
                p.field = rgb(255, 255, 255);
                p.border = rgb(40, 40, 40);
            }
            break;
        default: break;
    }
    return p;
}

ThemeSettings& themeSettings() {
    static ThemeSettings settings{defaultPalette(false), defaultPalette(true)};
    return settings;
}

bool themeIsDark() { return g_dark; }

void applyTheme(bool dark) {
    g_dark = dark;
    ImGuiStyle& style = ImGui::GetStyle();
    if (!g_haveBase) {
        g_base = style;
        g_haveBase = true;
    }
    const ThemeSettings& t = themeSettings();
    const Palette& p = current();

    // Shape and size.
    const float dpi = style.FontScaleDpi > 0.0f ? style.FontScaleDpi : 1.0f;
    const float r = std::clamp(t.rounding, 0.0f, 12.0f) * dpi;
    const float d = std::clamp(t.density, 0.6f, 1.6f);
    style.FrameRounding = r;
    style.GrabRounding = r;
    style.TabRounding = r;
    style.WindowRounding = r + 2.0f * dpi;
    style.ChildRounding = r + 2.0f * dpi;
    style.PopupRounding = r + 2.0f * dpi;
    style.FramePadding = ImVec2(g_base.FramePadding.x * d, g_base.FramePadding.y * d);
    style.ItemSpacing = ImVec2(g_base.ItemSpacing.x * d, g_base.ItemSpacing.y * d);
    style.ItemInnerSpacing = ImVec2(g_base.ItemInnerSpacing.x * d, g_base.ItemInnerSpacing.y * d);
    style.CellPadding = ImVec2(g_base.CellPadding.x * d, g_base.CellPadding.y * d);
    style.FontScaleMain = std::clamp(t.textScale, 0.8f, 1.5f);
    style.FrameBorderSize = dark ? 0.0f : 1.0f;

    // Colors.
    if (dark) ImGui::StyleColorsDark(&style);
    else ImGui::StyleColorsLight(&style);
    ImVec4* c = style.Colors;
    const float headerAlpha = dark ? 0.45f : 0.20f;
    c[ImGuiCol_Text] = p.text;
    c[ImGuiCol_TextDisabled] = p.muted;
    c[ImGuiCol_WindowBg] = p.background;
    c[ImGuiCol_ChildBg] = p.background;
    c[ImGuiCol_PopupBg] = p.panel;
    c[ImGuiCol_MenuBarBg] = p.sidebar;
    c[ImGuiCol_Border] = p.border;
    c[ImGuiCol_Separator] = p.border;
    c[ImGuiCol_FrameBg] = p.field;
    c[ImGuiCol_FrameBgHovered] = mix(p.field, p.accent, 0.08f);
    c[ImGuiCol_FrameBgActive] = mix(p.field, p.accent, 0.16f);
    c[ImGuiCol_TitleBg] = p.sidebar;
    c[ImGuiCol_TitleBgActive] = mix(p.sidebar, p.text, 0.06f);
    c[ImGuiCol_TitleBgCollapsed] = p.sidebar;
    c[ImGuiCol_TableHeaderBg] = p.sidebar;
    c[ImGuiCol_TableBorderLight] = alpha(p.border, 0.7f);
    c[ImGuiCol_TableBorderStrong] = p.border;
    c[ImGuiCol_TableRowBgAlt] = alpha(p.text, dark ? 0.03f : 0.025f);
    c[ImGuiCol_Header] = alpha(p.accent, headerAlpha);
    c[ImGuiCol_HeaderHovered] = alpha(p.accent, headerAlpha + 0.10f);
    c[ImGuiCol_HeaderActive] = alpha(p.accent, headerAlpha + 0.20f);
    c[ImGuiCol_Button] = mix(p.background, p.text, dark ? 0.12f : 0.08f);
    c[ImGuiCol_ButtonHovered] = mix(p.background, p.text, dark ? 0.18f : 0.14f);
    c[ImGuiCol_ButtonActive] = mix(p.background, p.text, dark ? 0.24f : 0.20f);
    c[ImGuiCol_Tab] = c[ImGuiCol_Button];
    c[ImGuiCol_TabHovered] = alpha(p.accent, dark ? 0.75f : 0.35f);
    c[ImGuiCol_TabSelected] = dark ? mix(p.background, p.accent, 0.5f) : p.panel;
    c[ImGuiCol_TabSelectedOverline] = p.accent;
    c[ImGuiCol_TabDimmed] = c[ImGuiCol_Tab];
    c[ImGuiCol_TabDimmedSelected] = c[ImGuiCol_TabSelected];
    c[ImGuiCol_CheckMark] = p.accent;
    c[ImGuiCol_SliderGrab] = p.accent;
    c[ImGuiCol_SliderGrabActive] = mix(p.accent, p.text, 0.2f);
    c[ImGuiCol_TextSelectedBg] = alpha(p.accent, 0.35f);
    c[ImGuiCol_TextLink] = p.accent;
    c[ImGuiCol_NavCursor] = p.accent;
    c[ImGuiCol_ResizeGrip] = alpha(p.accent, 0.2f);
    c[ImGuiCol_ResizeGripHovered] = alpha(p.accent, 0.6f);
    c[ImGuiCol_ResizeGripActive] = alpha(p.accent, 0.9f);
}

void writeThemeConfig(std::ostream& out) {
    const ThemeSettings& t = themeSettings();
    for (bool dark : {false, true}) {
        const Palette& p = dark ? t.dark : t.light;
        const Palette def = defaultPalette(dark);
        for (const auto& s : kSlots) {
            if (hex(p.*s.member) != hex(def.*s.member))
                out << "theme." << (dark ? "dark." : "light.") << s.key << '=' << hex(p.*s.member) << '\n';
        }
    }
    out << "theme.rounding=" << t.rounding << '\n';
    out << "theme.density=" << t.density << '\n';
    out << "theme.text_scale=" << t.textScale << '\n';
}

bool readThemeConfig(const std::string& key, const std::string& value) {
    if (key.rfind("theme.", 0) != 0) return false;
    ThemeSettings& t = themeSettings();
    auto number = [&](float& out, float lo, float hi) {
        try {
            out = std::clamp(std::stof(value), lo, hi);
        } catch (const std::exception&) {
        }
    };
    if (key == "theme.rounding") number(t.rounding, 0.0f, 12.0f);
    else if (key == "theme.density") number(t.density, 0.6f, 1.6f);
    else if (key == "theme.text_scale") number(t.textScale, 0.8f, 1.5f);
    for (bool dark : {false, true}) {
        const std::string prefix = dark ? "theme.dark." : "theme.light.";
        if (key.rfind(prefix, 0) != 0) continue;
        for (const auto& s : kSlots) {
            if (key.substr(prefix.size()) == s.key) parseHex(value, (dark ? t.dark : t.light).*s.member);
        }
    }
    return true;
}

ImVec4 colorAccent() { return current().accent; }
ImVec4 colorPositive() { return current().positive; }
ImVec4 colorNegative() { return current().negative; }
ImVec4 colorWarning() { return current().warning; }
ImVec4 colorMuted() { return current().muted; }
ImVec4 colorCardBg() { return current().panel; }

}  // namespace opgui
