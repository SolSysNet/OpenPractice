// The schedule: a Gantt chart of a project's phases and tasks, with dependencies, today's
// date, and bars that can be dragged to move a task or stretched to change its due date.

#include "app.hpp"
#include "imgui.h"
#include "openpractice/format.hpp"
#include "openpractice/util.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace opgui {

using namespace op;

namespace {

const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

struct BarPos {
    float x0 = 0;
    float x1 = 0;
    float y = 0;  // vertical center
};

ImU32 withAlpha(ImVec4 c, float a) {
    c.w = a;
    return ImGui::GetColorU32(c);
}

void arrow(ImDrawList* draw, ImVec2 from, ImVec2 to, ImU32 color, float fs) {
    // Out to the right, down/up, then into the successor from the left.
    const float out = fs * 0.5f;
    const ImVec2 a(from.x + out, from.y);
    draw->AddLine(from, a, color, 1.3f);
    if (to.x - out >= a.x) {
        draw->AddLine(a, ImVec2(a.x, to.y), color, 1.3f);
        draw->AddLine(ImVec2(a.x, to.y), to, color, 1.3f);
    } else {  // successor starts before the predecessor ends: loop back
        const float mid = (from.y + to.y) * 0.5f;
        draw->AddLine(a, ImVec2(a.x, mid), color, 1.3f);
        draw->AddLine(ImVec2(a.x, mid), ImVec2(to.x - out, mid), color, 1.3f);
        draw->AddLine(ImVec2(to.x - out, mid), ImVec2(to.x - out, to.y), color, 1.3f);
        draw->AddLine(ImVec2(to.x - out, to.y), to, color, 1.3f);
    }
    const float h = fs * 0.25f;
    draw->AddTriangleFilled(to, ImVec2(to.x - h * 1.4f, to.y - h), ImVec2(to.x - h * 1.4f, to.y + h), color);
}

}  // namespace

void App::drawSchedule(Project& project) {
    Practice& practice = *practice_;
    const float fs = ImGui::GetFontSize();
    const int projectId = project.id;

    // ---- rows: each phase followed by its tasks, then tasks without a phase
    struct Row {
        Phase* phase = nullptr;
        Task* task = nullptr;
        const char* heading = nullptr;
    };
    std::vector<Row> rows;
    auto tasksOf = [&](int phaseId) {
        std::vector<Task*> list;
        for (auto& t : practice.tasks) {
            if (t.projectId != projectId || t.phaseId != phaseId) continue;
            if (hideDoneTasks_ && t.status == TaskStatus::Done) continue;
            list.push_back(&t);
        }
        std::stable_sort(list.begin(), list.end(), [](const Task* a, const Task* b) {
            const auto ka = a->start ? a->start : a->due;
            const auto kb = b->start ? b->start : b->due;
            if (ka.has_value() != kb.has_value()) return ka.has_value();
            return ka && *ka < *kb;
        });
        return list;
    };
    for (auto& ph : practice.phases) {
        if (ph.projectId != projectId) continue;
        rows.push_back({&ph, nullptr, nullptr});
        for (Task* t : tasksOf(ph.id)) rows.push_back({nullptr, t, nullptr});
    }
    {
        const auto loose = tasksOf(0);
        if (!loose.empty()) rows.push_back({nullptr, nullptr, "Other tasks"});
        for (Task* t : loose) rows.push_back({nullptr, t, nullptr});
    }

    // ---- date range
    Date first = today_;
    Date last = today_;
    auto widen = [&](const std::optional<Date>& d) {
        if (!d) return;
        first = std::min(first, *d);
        last = std::max(last, *d);
    };
    widen(project.start);
    widen(project.end);
    for (const auto& r : rows) {
        if (r.phase) {
            widen(r.phase->start);
            widen(r.phase->end);
        }
        if (r.task) {
            widen(r.task->start);
            widen(r.task->due);
        }
    }
    first = weekStart(first).addDays(-7);
    last = last.addDays(21);
    const int days = last - first + 1;

    // ---- toolbar
    ImGui::SetNextItemWidth(fs * 9.0f);
    ImGui::SliderFloat("##zoom", &dayWidth_, 2.0f, 40.0f, "Zoom %.0f px/day", ImGuiSliderFlags_Logarithmic);
    ImGui::SameLine();
    ImGui::Checkbox("Hide done", &hideDoneTasks_);
    ImGui::SameLine();
    const bool jumpToday = ImGui::Button("Today");
    ImGui::SameLine();
    if (ui::PrimaryButton("+ Add task")) {
        defer([this, projectId] {
            Task t;
            t.id = Practice::nextId(practice_->tasks);
            t.projectId = projectId;
            for (const auto& ph : practice_->phases) {
                if (ph.projectId == projectId && ph.complete < Decimal::fromInt(100)) {
                    t.phaseId = ph.id;
                    break;
                }
            }
            t.name = "New task";
            t.start = today_;
            t.due = today_.addDays(7);
            practice_->tasks.push_back(t);
            selection_["schedule"] = t.id;
            changed();
        });
    }
    ImGui::SameLine(0, fs * 1.5f);
    ui::Muted("Drag a bar to move it, drag its right end to change the due date, click to edit.");

    // ---- chart and editor panel
    int& selectedId = selection_["schedule"];
    Task* selected = nullptr;
    for (auto& t : practice.tasks) {
        if (t.id == selectedId && t.projectId == projectId) selected = &t;
    }
    const float panelWidth = selected ? std::min(fs * 24.0f, ImGui::GetContentRegionAvail().x * 0.4f) : 0.0f;
    const float chartWidth = selected ? ImGui::GetContentRegionAvail().x - panelWidth - fs * 0.8f : 0.0f;

    const float dw = dayWidth_;
    const float rowH = fs * 1.75f;
    const float nameW = fs * 15.0f;
    const float timelineW = static_cast<float>(days) * dw;
    std::map<int, BarPos> bars;
    int clickedTask = 0;

    ImGui::BeginChild("##chartarea", ImVec2(chartWidth, 0));
    const ImGuiTableFlags tf = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_NoPadInnerX;
    if (rows.empty()) {
        ImGui::Spacing();
        ui::Muted("No phases or tasks yet. Add phases on the Phases & fee tab, then add tasks here.");
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));  // rows touch, so shading is continuous
        if (ImGui::BeginTable("##gantt", 2, tf)) {
            ImGui::TableSetupScrollFreeze(1, 1);
            ImGui::TableSetupColumn("Task", ImGuiTableColumnFlags_WidthFixed, nameW);
            ImGui::TableSetupColumn("Timeline", ImGuiTableColumnFlags_WidthFixed, timelineW);

            const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            const ImU32 gridColor = withAlpha(colorMuted(), 0.18f);
            const ImU32 weekendColor = withAlpha(colorMuted(), 0.07f);
            const ImU32 todayColor = ImGui::GetColorU32(colorNegative());
            const float todayOffset = (static_cast<float>(today_ - first) + 0.5f) * dw;

            // Background for a timeline cell: weekends, week lines and today.
            auto background = [&](ImDrawList* draw, float x, float y0, float y1) {
                for (int d = 0; d < days; ++d) {
                    const Date day = first.addDays(d);
                    const int wd = day - weekStart(day);
                    const float dx = x + static_cast<float>(d) * dw;
                    if (wd >= 5 && dw >= 4.0f) draw->AddRectFilled(ImVec2(dx, y0), ImVec2(dx + dw, y1), weekendColor);
                    if (wd == 0) draw->AddLine(ImVec2(dx, y0), ImVec2(dx, y1), gridColor);
                }
                draw->AddLine(ImVec2(x + todayOffset, y0), ImVec2(x + todayOffset, y1), todayColor, 1.5f);
            };

            // ---- header row: months and week dates
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers, rowH * 1.5f);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(" Phase / task");
            ImGui::TableSetColumnIndex(1);
            {
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImVec2 o = ImGui::GetCursorScreenPos();
                const float h = rowH * 1.5f;
                for (int d = 0; d < days; ++d) {
                    const Date day = first.addDays(d);
                    const float dx = o.x + static_cast<float>(d) * dw;
                    if (day.day() == 1 || d == 0) {
                        draw->AddLine(ImVec2(dx, o.y), ImVec2(dx, o.y + h), withAlpha(colorMuted(), 0.5f));
                        char label[16];
                        std::snprintf(label, sizeof label, "%s %d", kMonths[day.month() - 1], day.year());
                        draw->AddText(ImVec2(dx + 4, o.y + 2), ImGui::GetColorU32(textColor), label);
                    }
                    if (day - weekStart(day) == 0 && dw * 7 >= fs * 1.8f) {
                        char num[8];
                        std::snprintf(num, sizeof num, "%u", day.day());
                        draw->AddText(ImVec2(dx + 2, o.y + h * 0.52f), ImGui::GetColorU32(colorMuted()), num);
                    }
                }
                draw->AddLine(ImVec2(o.x + todayOffset, o.y + h * 0.5f), ImVec2(o.x + todayOffset, o.y + h), todayColor, 1.5f);
                ImGui::Dummy(ImVec2(timelineW, h));
                // Open around today, and come back to it on request.
                if (jumpToday || scheduleScrolledFor_ != projectId) {
                    ImGui::SetScrollX(std::max(0.0f, todayOffset - fs * 14.0f));
                    scheduleScrolledFor_ = projectId;
                }
            }

            // ---- body
            int rowIndex = 0;
            for (const auto& row : rows) {
                ImGui::PushID(rowIndex++);
                ImGui::TableNextRow(0, rowH);
                ImGui::TableSetColumnIndex(0);
                const float cellY = ImGui::GetCursorScreenPos().y;
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (rowH - ImGui::GetTextLineHeight()) * 0.5f - 2.0f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fs * 0.3f);
                if (row.phase) {
                    ImGui::PushFont(g_fonts.bold, 0.0f);
                    ImGui::TextUnformatted(trim(row.phase->code + " " + row.phase->name).c_str());
                    ImGui::PopFont();
                } else if (row.heading) {
                    ImGui::PushFont(g_fonts.bold, 0.0f);
                    ImGui::TextUnformatted(row.heading);
                    ImGui::PopFont();
                } else {
                    Task& t = *row.task;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fs * 0.8f);
                    const bool late = isOverdue(t, today_);
                    const std::string label = t.name.empty() ? "(untitled)" : t.name;
                    if (ImGui::Selectable(label.c_str(), selectedId == t.id, 0, ImVec2(nameW - fs * 1.3f, 0)))
                        clickedTask = t.id;
                    if (late) ImGui::SetItemTooltip("%d days overdue", today_ - *t.due);
                    else if (t.assigneeId) ImGui::SetItemTooltip("%s", practice.refName(Ref::Staff, t.assigneeId).c_str());
                }

                ImGui::TableSetColumnIndex(1);
                ImDrawList* draw = ImGui::GetWindowDrawList();
                const ImVec2 o(ImGui::GetCursorScreenPos().x, cellY);
                background(draw, o.x, o.y, o.y + rowH);
                const float mid = o.y + rowH * 0.5f;
                auto xOf = [&](Date d) { return o.x + static_cast<float>(d - first) * dw; };

                if (row.phase && row.phase->start && row.phase->end) {
                    const Phase& ph = *row.phase;
                    const float x0 = xOf(*ph.start);
                    const float x1 = xOf(*ph.end) + dw;
                    const float h = rowH * 0.32f;
                    draw->AddRectFilled(ImVec2(x0, mid - h), ImVec2(x1, mid + h), withAlpha(colorAccent(), 0.18f), 3.0f);
                    const float frac = std::clamp(static_cast<float>(ph.complete.raw()) / (100.0f * Decimal::kScale), 0.0f, 1.0f);
                    if (frac > 0)
                        draw->AddRectFilled(ImVec2(x0, mid - h), ImVec2(x0 + (x1 - x0) * frac, mid + h), withAlpha(colorAccent(), 0.55f), 3.0f);
                    const std::string label = ph.complete.fixed(0) + "%";
                    draw->AddText(ImVec2(x1 + 4, mid - ImGui::GetTextLineHeight() * 0.5f), ImGui::GetColorU32(colorMuted()), label.c_str());
                } else if (row.task) {
                    Task& t = *row.task;
                    std::optional<Date> s = t.start ? t.start : t.due;
                    std::optional<Date> e = t.due ? t.due : t.start;
                    if (s && e && *e < *s) std::swap(s, e);
                    if (!s) {
                        draw->AddText(ImVec2(xOf(today_) + fs * 0.5f, mid - ImGui::GetTextLineHeight() * 0.5f),
                                      ImGui::GetColorU32(colorMuted()), "not scheduled");
                    } else {
                        // Preview a drag in progress.
                        int shiftStart = 0;
                        int shiftEnd = 0;
                        if (dragTask_ == t.id) {
                            const int delta = static_cast<int>(std::lround(dragAccum_ / dw));
                            if (dragMode_ == 1) shiftStart = shiftEnd = delta;
                            else shiftEnd = std::max(delta, *s - *e);
                        }
                        const float x0 = xOf(s->addDays(shiftStart));
                        const float x1 = xOf(e->addDays(shiftEnd)) + dw;
                        const ImVec4 color = isOverdue(t, today_) ? colorNegative() : ui::statusColor(t.status);
                        const float h = rowH * 0.3f;
                        if (t.milestone) {
                            const float cx = x1 - dw * 0.5f;
                            const float r = rowH * 0.32f;
                            draw->AddQuadFilled(ImVec2(cx, mid - r), ImVec2(cx + r, mid), ImVec2(cx, mid + r), ImVec2(cx - r, mid),
                                                ImGui::GetColorU32(color));
                            bars[t.id] = {cx - r, cx + r, mid};
                        } else {
                            const bool done = t.status == TaskStatus::Done;
                            draw->AddRectFilled(ImVec2(x0, mid - h), ImVec2(x1, mid + h), withAlpha(color, done ? 0.35f : 0.85f), 4.0f);
                            if (selectedId == t.id)
                                draw->AddRect(ImVec2(x0 - 1, mid - h - 1), ImVec2(x1 + 1, mid + h + 1), ImGui::GetColorU32(textColor), 4.0f,
                                              1.5f);
                            bars[t.id] = {x0, x1, mid};
                        }
                        // Assignee initials after the bar.
                        if (const Staff* st = practice.findStaff(t.assigneeId)) {
                            const std::string who = st->initials.empty() ? st->name : st->initials;
                            draw->AddText(ImVec2(bars[t.id].x1 + 4, mid - ImGui::GetTextLineHeight() * 0.5f),
                                          ImGui::GetColorU32(colorMuted()), who.c_str());
                        }

                        // Interaction: body moves, right end resizes.
                        const BarPos& b = bars[t.id];
                        const float grip = std::min(fs * 0.5f, (b.x1 - b.x0) * 0.4f);
                        ImGui::SetCursorScreenPos(ImVec2(b.x0, mid - h));
                        ImGui::InvisibleButton("##move", ImVec2(std::max(2.0f, b.x1 - b.x0 - (t.milestone ? 0.0f : grip)), h * 2));
                        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        if (ImGui::IsItemActivated()) {
                            dragTask_ = t.id;
                            dragMode_ = 1;
                            dragAccum_ = 0.0f;
                        }
                        if (ImGui::IsItemActive() && dragTask_ == t.id) dragAccum_ += ImGui::GetIO().MouseDelta.x;
                        const bool moveEnded = ImGui::IsItemDeactivated() && dragTask_ == t.id && dragMode_ == 1;
                        if (ImGui::IsItemHovered() && !ImGui::IsItemActive()) {
                            ImGui::SetTooltip("%s\n%s to %s%s", t.name.c_str(), s->str().c_str(), e->str().c_str(),
                                              t.assigneeId ? ("\n" + practice.refName(Ref::Staff, t.assigneeId)).c_str() : "");
                        }
                        bool resizeEnded = false;
                        if (!t.milestone) {
                            ImGui::SetCursorScreenPos(ImVec2(b.x1 - grip, mid - h));
                            ImGui::InvisibleButton("##resize", ImVec2(grip + 3.0f, h * 2));
                            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                            if (ImGui::IsItemActivated()) {
                                dragTask_ = t.id;
                                dragMode_ = 2;
                                dragAccum_ = 0.0f;
                            }
                            if (ImGui::IsItemActive() && dragTask_ == t.id) dragAccum_ += ImGui::GetIO().MouseDelta.x;
                            resizeEnded = ImGui::IsItemDeactivated() && dragTask_ == t.id && dragMode_ == 2;
                        }
                        if (moveEnded || resizeEnded) {
                            const int delta = static_cast<int>(std::lround(dragAccum_ / dw));
                            if (delta == 0 && moveEnded) {
                                clickedTask = t.id;
                            } else if (delta != 0) {
                                if (moveEnded) {
                                    if (t.start) t.start = t.start->addDays(delta);
                                    if (t.due) t.due = t.due->addDays(delta);
                                } else {
                                    const Date newDue = e->addDays(std::max(delta, *s - *e));
                                    t.due = newDue;
                                    if (!t.start) t.start = *s;
                                }
                                changed();
                            }
                            dragTask_ = 0;
                            dragMode_ = 0;
                        }
                    }
                }
                // Keep the row height even when only drawing.
                ImGui::SetCursorScreenPos(ImVec2(o.x, o.y));
                ImGui::Dummy(ImVec2(timelineW, rowH));
                ImGui::PopID();
            }

            // ---- dependency arrows (drawn last, over the bars)
            ImGui::TableSetColumnIndex(1);
            ImDrawList* draw = ImGui::GetWindowDrawList();
            for (const auto& row : rows) {
                if (!row.task || !row.task->predecessorId) continue;
                auto to = bars.find(row.task->id);
                auto from = bars.find(row.task->predecessorId);
                if (to == bars.end() || from == bars.end()) continue;
                const bool conflict = startsBeforePredecessor(practice, *row.task);
                arrow(draw, ImVec2(from->second.x1, from->second.y), ImVec2(to->second.x0, to->second.y),
                      conflict ? ImGui::GetColorU32(colorNegative()) : withAlpha(colorMuted(), 0.9f), fs);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    if (clickedTask) selectedId = selectedId == clickedTask ? 0 : clickedTask;

    // ---- editor panel
    if (selected) {
        ImGui::SameLine(0, fs * 0.8f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, colorCardBg());
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs, fs * 0.8f));
        ImGui::BeginChild("##taskeditor", ImVec2(panelWidth, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        ui::SubHeading("Task");
        ImGui::SameLine();
        ui::AlignRight(ImGui::GetFrameHeight());
        if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) selectedId = 0;
        ImGui::Separator();
        ImGui::Spacing();
        if (ui::RecordEditor("##task", practice, *selected, schema<Task>(), refPicker(),
                             [](const char* k) { return std::string_view(k) == "project"; }, fs * 7.5f))
            changed();
        if (startsBeforePredecessor(practice, *selected))
            ui::Callout("Starts before the task it comes after is due.", colorWarning());
        ImGui::Spacing();
        if (ui::DangerButton("Delete task")) {
            const int id = selected->id;
            confirm("Delete task?", "Delete \"" + selected->name + "\"? This can't be undone.", "Delete", [this, id] {
                defer([this, id] {
                    auto& tasks = practice_->tasks;
                    tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [&](const Task& t) { return t.id == id; }), tasks.end());
                    practice_->clearReferences(Ref::Task, id);
                    changed();
                });
            });
        }
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
}

}  // namespace opgui
