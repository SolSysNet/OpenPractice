#pragma once

// App::drawRecordTable: any list of records as a sortable, searchable table, with an editor
// panel for the selected record. Columns, editing and actions come from the record's schema
// plus a TableSpec. Include after app.hpp in the screens that use it.

#include "app.hpp"
#include "imgui.h"
#include "openpractice/format.hpp"
#include "openpractice/util.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <string>
#include <string_view>
#include <vector>

namespace opgui {

namespace detail {

template <class T>
const op::Field<T>* fieldByKey(const char* key) {
    for (const auto& f : op::schema<T>().fields) {
        if (std::string_view(f.key) == key) return &f;
    }
    return nullptr;
}

// "Time entries" -> "time entries", but "RFIs" stays "RFIs".
inline std::string lowerNoun(std::string_view s) {
    std::string out(s);
    if (out.size() > 1 && std::isupper(static_cast<unsigned char>(out[1]))) return out;
    if (!out.empty()) out[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[0])));
    return out;
}

template <class T>
bool isNumeric(const op::Field<T>& f) {
    return std::holds_alternative<op::Money T::*>(f.member) || std::holds_alternative<op::Decimal T::*>(f.member) ||
           (std::holds_alternative<int T::*>(f.member) && f.ref == op::Ref::None);
}

// Status-like choices get their semantic color in tables.
template <class T>
ImVec4 choiceColor(const T& record, const op::Field<T>& f) {
    ImVec4 color(0, 0, 0, 0);
    std::visit(
        [&](auto member) {
            using V = std::decay_t<decltype(record.*member)>;
            if constexpr (std::is_same_v<V, op::ProjectStatus> || std::is_same_v<V, op::TaskStatus> ||
                          std::is_same_v<V, op::SheetStatus> || std::is_same_v<V, op::RfiStatus> ||
                          std::is_same_v<V, op::SubmittalStatus> || std::is_same_v<V, op::ChangeStatus> ||
                          std::is_same_v<V, op::InvoiceStatus>)
                color = ui::statusColor(record.*member);
        },
        f.member);
    return color;
}

}  // namespace detail

template <class T>
void App::drawRecordTable(const TableSpec<T>& spec, std::vector<T>& items) {
    const op::Practice& practice = *practice_;
    const op::Schema<T>& schema = op::schema<T>();
    const float fs = ImGui::GetFontSize();
    ImGui::PushID(spec.id);

    // ---- which records
    std::string& search = search_[spec.id];
    const std::string needle = op::toLower(op::trim(search));
    std::vector<int> rows;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const T& r = items[static_cast<std::size_t>(i)];
        if (spec.filter && !spec.filter(r)) continue;
        if (!needle.empty()) {
            bool hit = false;
            for (const char* key : spec.columns) {
                if (const auto* f = detail::fieldByKey<T>(key)) hit |= op::toLower(op::displayValue(practice, r, *f)).find(needle) != std::string::npos;
            }
            for (const auto& x : spec.extras) hit |= op::toLower(x.text(r)).find(needle) != std::string::npos;
            if (!hit) continue;
        }
        rows.push_back(i);
    }

    int& selectedId = selection_[spec.id];
    T* selected = nullptr;
    if (!spec.open) {
        for (auto& r : items) {
            if (r.id == selectedId) selected = &r;
        }
    }

    // ---- toolbar: search, extra controls, count, Add
    ImGui::SetNextItemWidth(fs * 14.0f);
    ui::InputStringHint("##search", "Search...", search);
    if (spec.toolbar) {
        ImGui::SameLine();
        spec.toolbar();
    }
    ImGui::SameLine(0, fs);
    ImGui::AlignTextToFramePadding();
    ui::Muted((std::to_string(rows.size()) + " " + detail::lowerNoun(rows.size() == 1 ? schema.title : schema.plural)).c_str());
    if (spec.make) {
        const std::string label = std::string("+ ") + spec.addLabel;
        const float w = ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine();
        ui::AlignRight(w);
        if (ui::PrimaryButton(label.c_str())) {
            const auto make = spec.make;
            const std::string id = spec.id;
            defer([this, &items, make, id] {
                T record = make();
                record.id = op::Practice::nextId(items);
                items.push_back(record);
                selection_[id] = record.id;
                changed();
            });
        }
    }

    // ---- table (and editor panel beside it)
    const float panelWidth = selected ? std::min(fs * 26.0f, ImGui::GetContentRegionAvail().x * 0.45f) : 0.0f;
    const float tableWidth = selected ? ImGui::GetContentRegionAvail().x - panelWidth - fs * 0.8f : 0.0f;
    ImGui::BeginChild("##tablearea", ImVec2(tableWidth, 0), ImGuiChildFlags_None);
    const int columnCount = static_cast<int>(spec.columns.size() + spec.extras.size());
    const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuterH |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (columnCount > 0 && ImGui::BeginTable("##records", columnCount, flags)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        int c = 0;
        for (const char* key : spec.columns) {
            const auto* f = detail::fieldByKey<T>(key);
            ImGuiTableColumnFlags cf = c == spec.sortColumn ? ImGuiTableColumnFlags_DefaultSort : 0;
            if (c == spec.sortColumn && spec.sortDescending) cf |= ImGuiTableColumnFlags_PreferSortDescending;
            const bool narrow = f && (std::holds_alternative<std::optional<op::Date> T::*>(f->member) || detail::isNumeric(*f) ||
                                      std::holds_alternative<bool T::*>(f->member));
            ImGui::TableSetupColumn(f ? f->label : key, cf, narrow ? 0.6f : 1.0f);
            ++c;
        }
        for (const auto& x : spec.extras) ImGui::TableSetupColumn(x.header, 0, 0.6f);
        ImGui::TableHeadersRow();

        if (ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs(); sort && sort->SpecsCount > 0) {
            const ImGuiTableColumnSortSpecs& s = sort->Specs[0];
            const int col = s.ColumnIndex;
            const bool desc = s.SortDirection == ImGuiSortDirection_Descending;
            std::stable_sort(rows.begin(), rows.end(), [&](int ia, int ib) {
                const T& a = items[static_cast<std::size_t>(ia)];
                const T& b = items[static_cast<std::size_t>(ib)];
                int cmp = 0;
                if (col < static_cast<int>(spec.columns.size())) {
                    if (const auto* f = detail::fieldByKey<T>(spec.columns[static_cast<std::size_t>(col)]))
                        cmp = op::compareField(practice, a, b, *f);
                } else {
                    const auto& x = spec.extras[static_cast<std::size_t>(col) - spec.columns.size()];
                    cmp = x.text(a).compare(x.text(b));
                }
                return desc ? cmp > 0 : cmp < 0;
            });
        }

        for (int index : rows) {
            T& r = items[static_cast<std::size_t>(index)];
            ImGui::PushID(r.id);
            ImGui::TableNextRow();
            int col = 0;
            for (const char* key : spec.columns) {
                ImGui::TableSetColumnIndex(col);
                ImGui::PushID(key);
                const auto* f = detail::fieldByKey<T>(key);
                const bool inlineEdit = std::find_if(spec.inlineEdit.begin(), spec.inlineEdit.end(), [&](const char* k) {
                                            return std::string_view(k) == key;
                                        }) != spec.inlineEdit.end();
                if (col == 0) {
                    // The first cell carries the row selection.
                    const std::string text = f ? op::displayValue(practice, r, *f) : std::string();
                    ImGui::AlignTextToFramePadding();
                    const bool isSelected = selected && selected->id == r.id;
                    if (ImGui::Selectable((text.empty() ? "(untitled)" : text).c_str(), isSelected,
                                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                                          ImVec2(0, ImGui::GetFrameHeight() - ImGui::GetStyle().FramePadding.y * 2))) {
                        if (spec.open) spec.open(r.id);
                        else selectedId = isSelected ? 0 : r.id;
                    }
                } else if (f && inlineEdit) {
                    if (ui::FieldInput(practice, r, *f, refPicker(), -FLT_MIN)) changed();
                } else if (f) {
                    const std::string text = op::displayValue(practice, r, *f);
                    const ImVec4 color = detail::choiceColor(r, *f);
                    ImGui::AlignTextToFramePadding();
                    if (detail::isNumeric(*f)) {
                        if (std::holds_alternative<op::Money T::*>(f->member) && r.*std::get<op::Money T::*>(f->member) < op::Money())
                            ui::TextRightColored(colorNegative(), text.c_str());
                        else ui::TextRight(text.c_str());
                    } else if (color.w > 0.0f) {
                        ImGui::TextColored(color, "%s", text.c_str());
                    } else {
                        ImGui::TextUnformatted(text.c_str());
                    }
                }
                ImGui::PopID();
                ++col;
            }
            for (const auto& x : spec.extras) {
                ImGui::TableSetColumnIndex(col++);
                const std::string text = x.text(r);
                ImGui::AlignTextToFramePadding();
                const ImVec4 color = x.color ? x.color(r) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
                if (x.right) ui::TextRightColored(color, text.c_str());
                else ImGui::TextColored(color, "%s", text.c_str());
            }
            ImGui::PopID();
        }
        if (rows.empty()) {  // inside the table, which fills the area
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            const std::string empty = needle.empty() ? "No " + detail::lowerNoun(schema.plural) + " yet." : "Nothing matches.";
            ui::Muted(empty.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // ---- editor panel
    if (selected) {
        ImGui::SameLine(0, fs * 0.8f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, colorCardBg());
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs, fs * 0.8f));
        ImGui::BeginChild("##editor", ImVec2(panelWidth, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        const std::string name = spec.columns.empty() ? std::string()
                                                      : op::displayValue(practice, *selected, *detail::fieldByKey<T>(spec.columns[0]));
        ui::SubHeading(schema.title);
        if (!name.empty()) {
            ImGui::SameLine();
            ui::Muted(name.c_str());
        }
        ImGui::SameLine();
        ui::AlignRight(ImGui::GetFrameHeight());
        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) selectedId = 0;
        ImGui::SetItemTooltip("Close");
        ImGui::Separator();
        ImGui::Spacing();
        if (ui::RecordEditor("##fields", practice, *selected, schema, refPicker(), spec.hide, fs * 8.5f)) changed();
        if (spec.editorExtras) spec.editorExtras(*selected);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (ui::DangerButton("Delete")) {
            const std::string why = spec.deleteBlocker ? spec.deleteBlocker(*selected) : std::string();
            if (!why.empty()) {
                notify(why, true);
            } else {
                const int id = selected->id;
                const auto onDelete = spec.onDelete;
                std::string label = name.empty() ? "this " + detail::lowerNoun(schema.title) : "\"" + name + "\"";
                confirm("Delete " + detail::lowerNoun(schema.title) + "?", "Delete " + label + "? This can't be undone.",
                        "Delete", [this, &items, id, onDelete] {
                            defer([this, &items, id, onDelete] {
                                items.erase(std::remove_if(items.begin(), items.end(), [&](const T& r) { return r.id == id; }),
                                            items.end());
                                if (onDelete) onDelete(id);
                                changed();
                            });
                        });
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
    ImGui::PopID();
}

}  // namespace opgui
