// The dashboard: firm totals, every active project's health, what needs attention, what's
// due soon, and how loaded the team is this week.

#include "app.hpp"
#include "imgui.h"
#include "openpractice/format.hpp"
#include "openpractice/util.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cmath>

namespace opgui {

using namespace op;

namespace {

const char* kWeekdays[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};

struct Upcoming {
    Date date;
    std::string kind;
    std::string what;
    int projectId = 0;
    ProjectTab tab = ProjectTab::Overview;
};

float ratio(Decimal part, Decimal whole) {
    if (whole.isZero()) return 0.0f;
    return static_cast<float>(static_cast<double>(part.raw()) / static_cast<double>(whole.raw()));
}

}  // namespace

void App::drawDashboard() {
    const Practice& p = *practice_;
    const float fs = ImGui::GetFontSize();
    const int weekday = today_ - weekStart(today_);
    screenHeader("Dashboard", (p.firm.name.empty() ? std::string() : p.firm.name + "  -  ") + kWeekdays[weekday] + ", " +
                                  today_.str());

    if (p.projects.empty()) {
        ui::Callout("No projects yet. Add your team under Team, then create your first project under Projects.",
                    colorAccent());
        if (ui::PrimaryButton("New project")) createProject();
        return;
    }

    // ---- figures
    const FirmStats& f = firmStats_;
    const float gap = fs * 0.8f;
    const float cw = ui::CardWidth(6, gap);
    ui::StatCard("##active", "Active projects", std::to_string(f.activeProjects), colorAccent(),
                 std::to_string(f.proposals) + (f.proposals == 1 ? " proposal" : " proposals"), cw);
    ImGui::SameLine(0, gap);
    ui::StatCard("##backlog", "Backlog", usdShort(f.backlog), ImGui::GetStyleColorVec4(ImGuiCol_Text),
                 "fee not yet earned", cw);
    ImGui::SameLine(0, gap);
    ui::StatCard("##earned", "Earned to date", usdShort(f.earned), ImGui::GetStyleColorVec4(ImGuiCol_Text),
                 "of " + usdShort(f.activeFees) + " active fees", cw);
    ImGui::SameLine(0, gap);
    ui::StatCard("##unbilled", "Unbilled", usdShort(f.unbilled), f.unbilled > Money() ? colorWarning() : colorPositive(),
                 "earned, not invoiced", cw);
    ImGui::SameLine(0, gap);
    ui::StatCard("##ar", "Receivable", usdShort(f.receivable),
                 f.overdueReceivable > Money() ? colorNegative() : ImGui::GetStyleColorVec4(ImGuiCol_Text),
                 f.overdueReceivable > Money() ? usdShort(f.overdueReceivable) + " past due" : "none past due", cw);
    ImGui::SameLine(0, gap);
    Decimal capacity;
    for (const auto& s : p.staff) {
        if (s.active) capacity += s.weeklyCapacity;
    }
    const Decimal billablePct = percentage(f.billableThisWeek.raw(), f.hoursThisWeek.raw());
    ui::StatCard("##hours", "Hours this week", f.hoursThisWeek.fixed(1), ImGui::GetStyleColorVec4(ImGuiCol_Text),
                 billablePct.fixed(0) + "% billable, of " + capacity.fixed(0), cw);
    ImGui::Dummy(ImVec2(0, gap * 0.5f));

    // ---- projects | needs attention
    const float avail = ImGui::GetContentRegionAvail().x;
    const float left = std::floor(avail * 0.62f);
    const float right = avail - left - gap;
    const float topHeight = std::max(fs * 16.0f, ImGui::GetContentRegionAvail().y * 0.52f);

    ui::BeginCard("##projects", left, topHeight);
    ui::SubHeading("Active projects");
    ImGui::Spacing();
    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##ptable", 7, tf)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Project", 0, 2.4f);
        ImGui::TableSetupColumn("Phase", 0, 0.5f);
        ImGui::TableSetupColumn("Complete", 0, 1.2f);
        ImGui::TableSetupColumn("Hours used", 0, 1.2f);
        ImGui::TableSetupColumn("Profit", 0, 0.9f);
        ImGui::TableSetupColumn("Mult.", 0, 0.45f);
        ImGui::TableSetupColumn("Health", 0, 0.8f);
        ImGui::TableHeadersRow();
        for (const auto& pr : p.projects) {
            if (pr.status != ProjectStatus::Active) continue;
            const ProjectStats& st = stats(pr.id);
            ImGui::PushID(pr.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const std::string label = p.refName(Ref::Project, pr.id);
            if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) openProject(pr.id);
            ImGui::SetItemTooltip("%s\nClient: %s", st.healthNote.c_str(), p.refName(Ref::Client, pr.clientId).c_str());
            if (ImGui::BeginPopupContextItem("##projectmenu")) {
                projectContextMenu(pr.id);
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(st.currentPhase ? st.currentPhase->code.c_str() : "-");
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const std::string complete = st.complete.fixed(0) + "%";
            ui::ProgressBar(ratio(st.complete, Decimal::fromInt(100)), colorAccent(),
                            std::max(fs * 2.0f, ImGui::GetContentRegionAvail().x - fs * 2.6f), complete.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const std::string used = st.budgetHours.isZero() ? std::string("-") : st.hoursUsed.fixed(0) + "%";
            ui::ProgressBar(ratio(st.hoursUsed, Decimal::fromInt(100)), ui::healthColor(st.health),
                            std::max(fs * 2.0f, ImGui::GetContentRegionAvail().x - fs * 2.6f), used.c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ui::MoneyText(Money::fromCents(st.profit.cents() / 100 * 100));
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ui::TextRightColored(st.multiplier < p.firm.targetMultiplier ? colorWarning() : ImGui::GetStyleColorVec4(ImGuiCol_Text),
                                 st.multiplier.fixed(2).c_str());
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ui::Badge(healthLabel(st.health), ui::healthColor(st.health));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ui::EndCard(false);

    ImGui::SameLine(0, gap);
    drawAttention(right, topHeight);

    ImGui::Dummy(ImVec2(0, gap * 0.3f));
    const float bottomHeight = std::max(fs * 12.0f, ImGui::GetContentRegionAvail().y);
    drawUpcoming(left, bottomHeight);
    ImGui::SameLine(0, gap);

    // ---- team this week
    ui::BeginCard("##team", right, bottomHeight);
    ui::SubHeading("Team this week");
    ImGui::Spacing();
    const Date monday = weekStart(today_);
    const auto load = staffLoad(p, monday, monday.addDays(6));
    if (ImGui::BeginTable("##load", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Name", 0, 1.3f);
        ImGui::TableSetupColumn("Bar", 0, 1.6f);
        ImGui::TableSetupColumn("Open", 0, 0.7f);
        for (const auto& l : load) {
            if (!l.staff->active) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(l.staff->name.c_str());
            ImGui::TableNextColumn();
            const float r = ratio(l.hours, l.staff->weeklyCapacity);
            const ImVec4 c = r > 1.05f ? colorNegative() : colorAccent();
            const std::string text = l.hours.fixed(1) + " / " + l.staff->weeklyCapacity.fixed(0) + " h";
            ui::ProgressBar(r, c, std::max(fs * 3.0f, ImGui::GetContentRegionAvail().x - fs * 6.5f), text.c_str());
            ImGui::TableNextColumn();
            ui::Muted((std::to_string(l.openTasks) + (l.openTasks == 1 ? " task" : " tasks")).c_str());
            ImGui::SetItemTooltip("%s estimated hours on open tasks", l.openTaskHours.fixed(0).c_str());
        }
        ImGui::EndTable();
    }
    ui::EndCard(false);
}

void App::drawAttention(float width, float height) {
    const float fs = ImGui::GetFontSize();
    ui::BeginCard("##attention", width, height);
    ui::SubHeading("Needs attention");
    ImGui::Spacing();
    if (issues_.empty()) {
        ImGui::TextColored(colorPositive(), "Nothing needs attention. Nice work.");
    }
    ImGui::BeginChild("##issues", ImVec2(0, 0));
    int n = 0;
    for (const auto& issue : issues_) {
        ImGui::PushID(n++);
        const ImVec4 c = ui::severityColor(issue.severity);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + fs * 0.3f, pos.y + ImGui::GetTextLineHeight() * 0.5f),
                                                     fs * 0.22f, ImGui::GetColorU32(c));
        ImGui::SetCursorScreenPos(ImVec2(pos.x + fs * 0.9f, pos.y));
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - fs * 3.5f);
        ImGui::TextUnformatted(issue.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::SameLine();
        ui::AlignRight(fs * 2.6f);
        if (ui::LinkButton("Open")) goToIssue(issue);
        ImGui::Dummy(ImVec2(0, fs * 0.15f));
        ImGui::PopID();
    }
    ImGui::EndChild();
    ui::EndCard(false);
}

void App::drawUpcoming(float width, float height) {
    const Practice& p = *practice_;
    const Date horizon = today_.addDays(21);
    std::vector<Upcoming> items;
    auto active = [&](int projectId) {
        const Project* pr = p.findProject(projectId);
        return pr && pr->status == ProjectStatus::Active;
    };
    for (const auto& t : p.tasks) {
        if (t.status == TaskStatus::Done || !t.due || *t.due > horizon || !active(t.projectId)) continue;
        items.push_back({*t.due, t.milestone ? "Milestone" : "Task",
                         t.name + (t.assigneeId ? "  (" + p.refName(Ref::Staff, t.assigneeId) + ")" : ""), t.projectId,
                         ProjectTab::Schedule});
    }
    for (const auto& r : p.rfis) {
        if (r.status != RfiStatus::Open || !r.due || *r.due > horizon || !active(r.projectId)) continue;
        items.push_back({*r.due, "RFI", "RFI " + std::to_string(r.number) + ": " + r.subject, r.projectId, ProjectTab::Rfis});
    }
    for (const auto& s : p.submittals) {
        if ((s.status != SubmittalStatus::Pending && s.status != SubmittalStatus::UnderReview) || !s.due || *s.due > horizon ||
            !active(s.projectId))
            continue;
        items.push_back({*s.due, "Submittal", s.number + " " + s.description, s.projectId, ProjectTab::Submittals});
    }
    for (const auto& i : p.invoices) {
        if (i.status != InvoiceStatus::Sent || !i.due || *i.due > horizon) continue;
        items.push_back({*i.due, "Payment", "Invoice " + i.number + " due (" + usd(i.amount - i.paid) + ")", i.projectId,
                         ProjectTab::Invoices});
    }
    for (const auto& ph : p.phases) {
        if (!ph.end || *ph.end < today_ || *ph.end > horizon || ph.complete >= Decimal::fromInt(100) || !active(ph.projectId))
            continue;
        items.push_back({*ph.end, "Phase end", ph.code + " " + ph.name + " (" + ph.complete.fixed(0) + "% complete)",
                         ph.projectId, ProjectTab::Phases});
    }
    std::stable_sort(items.begin(), items.end(), [](const Upcoming& a, const Upcoming& b) { return a.date < b.date; });

    ui::BeginCard("##upcoming", width, height);
    ui::SubHeading("Coming up");
    ImGui::SameLine();
    ui::Muted("overdue and the next three weeks");
    ImGui::Spacing();
    if (items.empty()) ui::Muted("Nothing due in the next three weeks.");
    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (!items.empty() && ImGui::BeginTable("##up", 4, tf)) {
        ImGui::TableSetupColumn("When", 0, 0.75f);
        ImGui::TableSetupColumn("Kind", 0, 0.6f);
        ImGui::TableSetupColumn("What", 0, 2.6f);
        ImGui::TableSetupColumn("Project", 0, 1.1f);
        int n = 0;
        for (const auto& u : items) {
            ImGui::PushID(n++);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const int days = u.date - today_;
            const std::string when = days < 0 ? std::to_string(-days) + "d overdue"
                                     : days == 0 ? std::string("Today")
                                     : days == 1 ? std::string("Tomorrow")
                                                 : u.date.str().substr(5);
            const ImVec4 c = days < 0 ? colorNegative() : days <= 2 ? colorWarning() : ImGui::GetStyleColorVec4(ImGuiCol_Text);
            if (ImGui::Selectable("##row", false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                openProject(u.projectId, u.tab);
            ImGui::SameLine(0, 0);
            ImGui::TextColored(c, "%s", when.c_str());
            ImGui::TableNextColumn();
            ui::Muted(u.kind.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(u.what.c_str());
            ImGui::TableNextColumn();
            const Project* pr = p.findProject(u.projectId);
            ui::Muted(pr ? (pr->name.empty() ? pr->number.c_str() : pr->name.c_str()) : "");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ui::EndCard(false);
}

}  // namespace opgui
