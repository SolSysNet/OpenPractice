// The theme editor: colors for the light and dark modes, corner rounding, spacing and text
// size, with presets and a live preview. Changes apply immediately and are saved with the
// app's settings.

#include "app.hpp"
#include "imgui.h"
#include "theme.hpp"
#include "widgets.hpp"

#include <cfloat>
#include <string>

namespace opgui {

namespace {

struct ColorSlot {
    const char* label;
    const char* help;
    ImVec4 Palette::*member;
};

const ColorSlot kColors[] = {
    {"Accent", "Primary buttons, selection, links and progress.", &Palette::accent},
    {"Positive", "On track, paid, approved.", &Palette::positive},
    {"Negative", "Overdue, over budget, errors, delete.", &Palette::negative},
    {"Warning", "Watch, needs review, uncommitted changes.", &Palette::warning},
    {"Text", "Body text.", &Palette::text},
    {"Muted text", "Labels, hints and secondary text.", &Palette::muted},
    {"Background", "The window behind everything.", &Palette::background},
    {"Panels", "Cards, editors and popups.", &Palette::panel},
    {"Sidebar", "Sidebar, menu bar and table headers.", &Palette::sidebar},
    {"Input fields", "Text boxes, dropdowns and date fields.", &Palette::field},
    {"Borders", "Outlines of cards, fields and tables.", &Palette::border},
};

}  // namespace

void App::drawThemeEditor() {
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(fs * 27.0f, fs * 40.0f), ImGuiCond_FirstUseEver);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - fs * 28.0f, vp->WorkPos.y + fs * 2.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Theme editor", &showThemeEditor_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }
    ThemeSettings& t = themeSettings();
    bool apply = false;
    bool save = false;

    // ---- mode and preset
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Mode");
    ImGui::SameLine(fs * 7.0f);
    if (ImGui::RadioButton("Light", !darkTheme_)) {
        darkTheme_ = false;
        apply = save = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Dark", darkTheme_)) {
        darkTheme_ = true;
        apply = save = true;
    }
    Palette& p = darkTheme_ ? t.dark : t.light;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Preset");
    ImGui::SameLine(fs * 7.0f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo("##preset", "Apply a preset...")) {
        const auto& names = presetNames();
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (ImGui::Selectable(names[i])) {
                p = presetPalette(i, darkTheme_);
                apply = save = true;
            }
        }
        ImGui::EndCombo();
    }
    ui::Muted(darkTheme_ ? "Editing the dark mode colors." : "Editing the light mode colors.");
    ImGui::Separator();

    // ---- colors
    ui::SubHeading("Colors");
    if (ImGui::BeginTable("##colors", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, fs * 7.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        for (const auto& c : kColors) {
            ImGui::PushID(c.label);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(c.label);
            ImGui::SetItemTooltip("%s", c.help);
            ImGui::TableNextColumn();
            ImVec4& color = p.*c.member;
            float rgb[3] = {color.x, color.y, color.z};
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::ColorEdit3("##c", rgb, ImGuiColorEditFlags_DisplayHex | ImGuiColorEditFlags_PickerHueWheel)) {
                color = ImVec4(rgb[0], rgb[1], rgb[2], 1.0f);
                apply = true;
            }
            save |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();

    // ---- shape and size
    ui::SubHeading("Shape and size");
    auto slider = [&](const char* label, float& value, float lo, float hi, const char* format, float shown = 1.0f) {
        ImGui::PushID(label);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::SameLine(fs * 7.0f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        float v = value * shown;
        if (ImGui::SliderFloat("##s", &v, lo * shown, hi * shown, format)) {
            value = v / shown;
            apply = true;
        }
        save |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PopID();
    };
    slider("Corners", t.rounding, 0.0f, 12.0f, "%.0f px rounding");
    slider("Spacing", t.density, 0.6f, 1.6f, "%.0f%%", 100.0f);
    slider("Text size", t.textScale, 0.8f, 1.5f, "%.0f%%", 100.0f);
    ImGui::Spacing();

    // ---- preview
    ui::SubHeading("Preview");
    ui::BeginCard("##preview", 0.0f);
    ui::PrimaryButton("Commit");
    ImGui::SameLine();
    ImGui::Button("Cancel");
    ImGui::SameLine();
    ui::DangerButton("Delete");
    ImGui::SameLine();
    ui::LinkButton("Open");
    ui::Badge("On track", colorPositive());
    ImGui::SameLine();
    ui::Badge("Watch", colorWarning());
    ImGui::SameLine();
    ui::Badge("Over budget", colorNegative());
    ImGui::SameLine();
    ui::Muted("Muted text");
    ui::ProgressBar(0.62f, colorAccent(), fs * 10.0f, "62% complete");
    static std::string sample = "Input field";
    ui::InputString("##sample", sample);
    ui::EndCard(false);
    ImGui::Spacing();

    // ---- reset
    if (ImGui::Button(darkTheme_ ? "Reset dark colors" : "Reset light colors")) {
        p = defaultPalette(darkTheme_);
        apply = save = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset everything")) {
        t = ThemeSettings{defaultPalette(false), defaultPalette(true)};
        apply = save = true;
    }

    ImGui::End();
    if (apply || save) applyTheme(darkTheme_);
    if (save) saveConfig();
}

}  // namespace opgui
