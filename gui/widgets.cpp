#include "widgets.hpp"

#include "imgui_stdlib.h"
#include "openpractice/format.hpp"
#include "openpractice/util.hpp"
#include "theme.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace opgui::ui {

// ------------------------------------------------------------------- input

bool InputString(const char* label, std::string& value, ImGuiInputTextFlags flags) {
    return ImGui::InputText(label, &value, flags);
}

bool InputStringHint(const char* label, const char* hint, std::string& value, ImGuiInputTextFlags flags) {
    return ImGui::InputTextWithHint(label, hint, &value, flags);
}

bool InputMultiline(const char* label, std::string& value, ImVec2 size) {
    return ImGui::InputTextMultiline(label, &value, size);
}

namespace {

// Text being typed into bound fields, keyed by widget id. Only fields being edited have an
// entry; everything else shows the model value.
std::unordered_map<ImGuiID, std::string>& editBuffers() {
    static std::unordered_map<ImGuiID, std::string> buffers;
    return buffers;
}

int weekday(op::Date d) {  // 0 = Sunday; 1970-01-01 was a Thursday
    return ((d.serial() + 4) % 7 + 7) % 7;
}

int daysInMonth(int year, int month) {
    const op::Date first = op::Date::fromYMD(year, static_cast<unsigned>(month), 1);
    const op::Date next = month == 12 ? op::Date::fromYMD(year + 1, 1, 1)
                                      : op::Date::fromYMD(year, static_cast<unsigned>(month + 1), 1);
    return next - first;
}

const char* kMonthNames[] = {"January", "February", "March",     "April",   "May",      "June",
                             "July",    "August",   "September", "October", "November", "December"};

// Month grid; returns true when a day was picked.
bool calendar(std::optional<op::Date>& date, int& viewYear, int& viewMonth) {
    bool picked = false;
    if (ImGui::ArrowButton("##prevyear", ImGuiDir_Left)) --viewYear;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%d", viewYear);
    ImGui::SameLine();
    if (ImGui::ArrowButton("##nextyear", ImGuiDir_Right)) ++viewYear;
    ImGui::SameLine(0, ImGui::GetFontSize());
    if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) {
        if (--viewMonth < 1) {
            viewMonth = 12;
            --viewYear;
        }
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) {
        if (++viewMonth > 12) {
            viewMonth = 1;
            ++viewYear;
        }
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(kMonthNames[viewMonth - 1]);

    const float cell = ImGui::GetFrameHeight() * 1.25f;
    const op::Date first = op::Date::fromYMD(viewYear, static_cast<unsigned>(viewMonth), 1);
    const int offset = weekday(first);
    const int days = daysInMonth(viewYear, viewMonth);
    if (!ImGui::BeginTable("##grid", 7, ImGuiTableFlags_SizingFixedSame | ImGuiTableFlags_NoPadOuterX)) return false;
    static const char* kDays[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    for (const char* name : kDays) ImGui::TableSetupColumn(name, ImGuiTableColumnFlags_WidthFixed, cell);
    ImGui::TableNextRow();
    for (const char* name : kDays) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", name);
    }
    for (int slot = 0; slot < offset + days; ++slot) {
        ImGui::TableNextColumn();
        if (slot < offset) continue;
        const int day = slot - offset + 1;
        const op::Date d = first.addDays(day - 1);
        const bool selected = date && d == *date;
        int colors = 0;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, colorAccent());
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            colors = 2;
        }
        char label[8];
        std::snprintf(label, sizeof label, "%d", day);
        ImGui::PushID(day);
        if (ImGui::Button(label, ImVec2(cell, cell))) {
            date = d;
            picked = true;
        }
        ImGui::PopID();
        ImGui::PopStyleColor(colors);
    }
    ImGui::EndTable();
    return picked;
}

}  // namespace

bool MoneyInput(const char* label, op::Money& value, float width) {
    const ImGuiID id = ImGui::GetID(label);
    auto& buffers = editBuffers();
    auto it = buffers.find(id);
    std::string text = it != buffers.end() ? it->second : (value.isZero() ? std::string() : value.formatted());
    const bool valid = op::trim(text).empty() || op::Money::parse(text).has_value();
    if (width != 0.0f) ImGui::SetNextItemWidth(width);
    if (!valid) ImGui::PushStyleColor(ImGuiCol_Text, colorNegative());
    const bool edited = ImGui::InputTextWithHint(label, "0.00", &text, ImGuiInputTextFlags_CharsScientific);
    if (!valid) {
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Enter an amount like 1234.56");
    }
    bool changed = false;
    if (ImGui::IsItemActive()) {
        buffers[id] = text;
        if (edited) {
            op::Money parsed;
            bool ok = true;
            if (!op::trim(text).empty()) {
                auto m = op::Money::parse(text);
                ok = m.has_value();
                if (m) parsed = *m;
            }
            if (ok && parsed != value) {
                value = parsed;
                changed = true;
            }
        }
    } else if (it != buffers.end()) {
        buffers.erase(it);
    }
    return changed;
}

bool DecimalInput(const char* label, op::Decimal& value, float width, const char* hint) {
    const ImGuiID id = ImGui::GetID(label);
    auto& buffers = editBuffers();
    auto it = buffers.find(id);
    std::string text = it != buffers.end() ? it->second : (value.isZero() ? std::string() : value.str());
    const bool valid = op::trim(text).empty() || op::Decimal::parse(text).has_value();
    if (width != 0.0f) ImGui::SetNextItemWidth(width);
    if (!valid) ImGui::PushStyleColor(ImGuiCol_Text, colorNegative());
    const bool edited = ImGui::InputTextWithHint(label, hint, &text, ImGuiInputTextFlags_CharsScientific);
    if (!valid) {
        ImGui::PopStyleColor();
        ImGui::SetItemTooltip("Enter a number like 7.5");
    }
    bool changed = false;
    if (ImGui::IsItemActive()) {
        buffers[id] = text;
        if (edited) {
            op::Decimal parsed;
            bool ok = true;
            if (!op::trim(text).empty()) {
                auto d = op::Decimal::parse(text);
                ok = d.has_value();
                if (d) parsed = *d;
            }
            if (ok && parsed != value) {
                value = parsed;
                changed = true;
            }
        }
    } else if (it != buffers.end()) {
        buffers.erase(it);
    }
    return changed;
}

bool SearchCombo(const char* label, const std::string& preview, const std::vector<Option>& options, int& value,
                 float width) {
    bool changed = false;
    if (width != 0.0f) ImGui::SetNextItemWidth(width);
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, ImGui::GetFontSize() * 22));
    if (ImGui::BeginCombo(label, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        static std::string filter;
        if (ImGui::IsWindowAppearing()) {
            filter.clear();
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool enter = ImGui::InputTextWithHint("##search", "Type to search...", &filter,
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        const std::string needle = op::toLower(op::trim(filter));
        bool firstPicked = false;
        for (const auto& o : options) {
            if (!needle.empty() && (o.header || op::toLower(o.label).find(needle) == std::string::npos)) continue;
            if (o.header) {
                ImGui::Spacing();
                ImGui::TextDisabled("%s", o.label.c_str());
                continue;
            }
            if (enter && !firstPicked) {
                value = o.id;
                changed = true;
                firstPicked = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PushID(o.id);
            const bool selected = o.id == value;
            if (ImGui::Selectable(o.label.c_str(), selected)) {
                value = o.id;
                changed = true;
            }
            if (selected && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool DateInput(const char* label, std::optional<op::Date>& value, float width) {
    struct State {
        int viewYear = 0;
        int viewMonth = 1;
    };
    static std::unordered_map<ImGuiID, State> states;
    ImGui::PushID(label);
    const ImGuiID id = ImGui::GetID("text");
    auto& buffers = editBuffers();
    auto it = buffers.find(id);
    std::string text = it != buffers.end() ? it->second : (value ? value->str() : std::string());
    const bool valid = op::trim(text).empty() || op::Date::parse(text).has_value();
    ImGui::SetNextItemWidth(width > 0.0f ? width : ImGui::GetFontSize() * 7.0f);
    if (!valid) ImGui::PushStyleColor(ImGuiCol_Text, colorNegative());
    const bool edited = ImGui::InputTextWithHint("##text", "YYYY-MM-DD", &text);
    if (!valid) ImGui::PopStyleColor();
    ImGui::SetItemTooltip("YYYY-MM-DD or MM/DD/YYYY");
    bool changed = false;
    if (ImGui::IsItemActive()) {
        buffers[id] = text;
        if (edited) {
            if (op::trim(text).empty()) {
                if (value) {
                    value.reset();
                    changed = true;
                }
            } else if (auto d = op::Date::parse(text); d && (!value || *d != *value)) {
                value = *d;
                changed = true;
            }
        }
    } else if (it != buffers.end()) {
        buffers.erase(it);
    }
    ImGui::SameLine(0, 2.0f);
    State& s = states[id];
    if (ImGui::ArrowButton("##open", ImGuiDir_Down)) {
        const op::Date base = value ? *value : op::Date::today();
        s.viewYear = base.year();
        s.viewMonth = static_cast<int>(base.month());
        ImGui::OpenPopup("calendar");
    }
    if (ImGui::BeginPopup("calendar")) {
        if (calendar(value, s.viewYear, s.viewMonth)) {
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool IntInput(const char* label, int& value, int min, int max, float width) {
    if (width != 0.0f) ImGui::SetNextItemWidth(width);
    int v = value;
    if (ImGui::InputInt(label, &v, 1, 10)) {
        v = std::clamp(v, min, max);
        if (v != value) {
            value = v;
            return true;
        }
    }
    return false;
}

void HelpMarker(const char* text) {
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// -------------------------------------------------------------------- text

void Heading(const char* text) {
    ImGui::PushFont(g_fonts.bold, kBaseFontSize * 1.55f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void SubHeading(const char* text) {
    ImGui::PushFont(g_fonts.bold, kBaseFontSize * 1.1f);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void Muted(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, colorMuted());
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

void MutedWrapped(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, colorMuted());
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::min(ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 48.0f));
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

void TextRight(const char* text) {
    const float w = ImGui::CalcTextSize(text).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
    ImGui::TextUnformatted(text);
}

void TextRightColored(ImVec4 color, const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    TextRight(text);
    ImGui::PopStyleColor();
}

void MoneyText(op::Money amount, bool rightAlign, bool redIfNegative) {
    const std::string s = op::usd(amount);
    const bool red = redIfNegative && amount < op::Money();
    if (red) ImGui::PushStyleColor(ImGuiCol_Text, colorNegative());
    if (rightAlign) TextRight(s.c_str());
    else ImGui::TextUnformatted(s.c_str());
    if (red) ImGui::PopStyleColor();
}

void Badge(const char* text, ImVec4 color) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    const ImVec2 pad(ImGui::GetFontSize() * 0.45f, 1.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec4 bg = color;
    bg.w = 0.16f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x + pad.x * 2, pos.y + size.y + pad.y * 2), ImGui::GetColorU32(bg),
                        size.y * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad.x, pos.y + pad.y));
    ImGui::TextColored(color, "%s", text);
}

void ErrorText(const std::string& error) {
    if (error.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_Text, colorNegative());
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(error.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// ----------------------------------------------------------------- buttons

namespace {
bool coloredButton(const char* label, ImVec2 size, ImVec4 base) {
    ImVec4 hover = base;
    hover.x *= 1.1f;
    hover.y *= 1.1f;
    hover.z *= 1.1f;
    ImVec4 active = base;
    active.x *= 0.9f;
    active.y *= 0.9f;
    active.z *= 0.9f;
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}
}  // namespace

bool PrimaryButton(const char* label, ImVec2 size) { return coloredButton(label, size, colorAccent()); }
bool DangerButton(const char* label, ImVec2 size) { return coloredButton(label, size, colorNegative()); }

bool LinkButton(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, colorAccent());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const bool pressed = ImGui::SmallButton(label);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    return pressed;
}

// ------------------------------------------------------------------ layout

void StatCard(const char* id, const char* title, const std::string& value, ImVec4 valueColor, const std::string& note,
              float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colorCardBg());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    const float height = ImGui::GetFontSize() * 5.4f;
    ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    Muted(title);
    ImGui::PushFont(g_fonts.bold, kBaseFontSize * 1.6f);
    ImGui::TextColored(valueColor, "%s", value.c_str());
    ImGui::PopFont();
    Muted(note.c_str());
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void BeginCard(const char* id, float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colorCardBg());
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ImGui::GetFontSize(), ImGui::GetFontSize() * 0.8f));
    const ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding |
                                  (height <= 0.0f ? ImGuiChildFlags_AutoResizeY : 0);
    ImGui::BeginChild(id, ImVec2(width, height > 0.0f ? height : 0.0f), flags, ImGuiWindowFlags_NoScrollbar);
}

void EndCard(bool spacing) {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    if (spacing) ImGui::Spacing();
}

void Callout(const char* text, ImVec4 color) {
    const float fs = ImGui::GetFontSize();
    const float width = std::min(ImGui::GetContentRegionAvail().x, fs * 46.0f);
    const float wrap = width - fs * 1.6f;
    const ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, false, wrap);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(width, textSize.y + fs * 0.9f);
    ImVec4 bg = color;
    bg.w = 0.12f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::GetColorU32(bg), 6.0f);
    draw->AddRectFilled(pos, ImVec2(pos.x + 4.0f, pos.y + size.y), ImGui::GetColorU32(color), 2.0f);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + fs * 0.8f, pos.y + fs * 0.45f));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + size.y + fs * 0.4f));
    ImGui::Dummy(ImVec2(0, 0));
}

void ProgressBar(float fraction, ImVec4 color, float width, const char* text) {
    const float fs = ImGui::GetFontSize();
    const float h = fs * 0.45f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float y = pos.y + (ImGui::GetTextLineHeight() - h) * 0.5f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec4 track = colorMuted();
    track.w = 0.22f;
    draw->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + width, y + h), ImGui::GetColorU32(track), h * 0.5f);
    const float f = std::clamp(fraction, 0.0f, 1.0f);
    if (f > 0.0f)
        draw->AddRectFilled(ImVec2(pos.x, y), ImVec2(pos.x + std::max(width * f, h), y + h), ImGui::GetColorU32(color),
                            h * 0.5f);
    if (fraction > 1.0f) {  // over: a notch at the end
        draw->AddRectFilled(ImVec2(pos.x + width - 3.0f, y - 2.0f), ImVec2(pos.x + width, y + h + 2.0f),
                            ImGui::GetColorU32(colorNegative()));
    }
    ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
    if (text) {
        ImGui::SameLine(0, fs * 0.5f);
        ImGui::TextUnformatted(text);
    }
}

void AlignRight(float width) {
    const float x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - width;
    if (x > ImGui::GetCursorPosX()) ImGui::SetCursorPosX(x);
}

float CardWidth(int count, float gap) {
    const float avail = ImGui::GetContentRegionAvail().x;
    return std::floor((avail - gap * static_cast<float>(count - 1)) / static_cast<float>(count));
}

// ------------------------------------------------------------------ colors

ImVec4 healthColor(op::Health h) {
    switch (h) {
        case op::Health::OnTrack: return colorPositive();
        case op::Health::Watch: return colorWarning();
        case op::Health::OverBudget: return colorNegative();
        case op::Health::NotStarted: break;
    }
    return colorMuted();
}

ImVec4 severityColor(op::Severity s) {
    switch (s) {
        case op::Severity::Error: return colorNegative();
        case op::Severity::Warning: return colorWarning();
        case op::Severity::Note: break;
    }
    return colorAccent();
}

ImVec4 statusColor(op::ProjectStatus s) {
    switch (s) {
        case op::ProjectStatus::Active: return colorPositive();
        case op::ProjectStatus::Proposal: return colorAccent();
        case op::ProjectStatus::OnHold: return colorWarning();
        default: return colorMuted();
    }
}

ImVec4 statusColor(op::TaskStatus s) {
    switch (s) {
        case op::TaskStatus::InProgress: return colorAccent();
        case op::TaskStatus::Blocked: return colorNegative();
        case op::TaskStatus::Done: return colorPositive();
        default: return colorMuted();
    }
}

ImVec4 statusColor(op::SheetStatus s) {
    switch (s) {
        case op::SheetStatus::ForReview: return colorWarning();
        case op::SheetStatus::Issued: return colorPositive();
        case op::SheetStatus::InProgress: return colorAccent();
        default: return colorMuted();
    }
}

ImVec4 statusColor(op::RfiStatus s) {
    switch (s) {
        case op::RfiStatus::Open: return colorWarning();
        case op::RfiStatus::Answered: return colorAccent();
        default: return colorPositive();
    }
}

ImVec4 statusColor(op::SubmittalStatus s) {
    switch (s) {
        case op::SubmittalStatus::Pending: return colorMuted();
        case op::SubmittalStatus::UnderReview: return colorWarning();
        case op::SubmittalStatus::Approved:
        case op::SubmittalStatus::ApprovedAsNoted: return colorPositive();
        default: return colorNegative();
    }
}

ImVec4 statusColor(op::ChangeStatus s) {
    switch (s) {
        case op::ChangeStatus::Approved: return colorPositive();
        case op::ChangeStatus::Rejected: return colorNegative();
        default: return colorWarning();
    }
}

ImVec4 statusColor(op::InvoiceStatus s) {
    switch (s) {
        case op::InvoiceStatus::Paid: return colorPositive();
        case op::InvoiceStatus::Sent: return colorAccent();
        default: return colorMuted();
    }
}

}  // namespace opgui::ui
