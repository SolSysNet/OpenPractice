// Projects: the project list and each project's workspace (overview, phases and fee,
// schedule, tasks, drawings, RFIs, submittals, change orders, invoices and time).

#include "app.hpp"
#include "imgui.h"
#include "openpractice/format.hpp"
#include "openpractice/report.hpp"
#include "openpractice/util.hpp"
#include "record_table.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>

namespace opgui {

using namespace op;

namespace {

bool hideProjectField(const char* key) { return std::string_view(key) == "project"; }

std::string daysText(int days) { return days == 1 ? "1 day" : std::to_string(days) + " days"; }

float ratio(Decimal part, Decimal whole) {
    if (whole.isZero()) return 0.0f;
    return static_cast<float>(static_cast<double>(part.raw()) / static_cast<double>(whole.raw()));
}

int currentPhaseId(const Practice& p, int projectId) {
    for (const auto& ph : p.phases) {
        if (ph.projectId == projectId && ph.complete < Decimal::fromInt(100)) return ph.id;
    }
    return 0;
}

// A line of label/value pairs above a table.
void summaryLine(std::initializer_list<std::pair<const char*, std::string>> items) {
    const float fs = ImGui::GetFontSize();
    bool first = true;
    for (const auto& [label, value] : items) {
        if (!first) ImGui::SameLine(0, fs * 1.6f);
        first = false;
        ui::Muted(label);
        ImGui::SameLine();
        ImGui::PushFont(g_fonts.bold, 0.0f);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopFont();
    }
    ImGui::Spacing();
}

}  // namespace

// ------------------------------------------------------------------ list

void App::createProject() {
    defer([this] {
        Practice& p = *practice_;
        Project pr;
        pr.id = Practice::nextId(p.projects);
        const std::string year = std::to_string(today_.year());
        int seq = 0;
        for (const auto& other : p.projects) {
            if (startsWith(other.number, year + "-")) {
                if (auto n = parseInt(other.number.substr(year.size() + 1))) seq = std::max(seq, static_cast<int>(*n));
            }
        }
        char number[32];
        std::snprintf(number, sizeof number, "%s-%03d", year.c_str(), seq + 1);
        pr.number = number;
        pr.name = "New project";
        pr.status = ProjectStatus::Proposal;
        pr.start = today_;
        p.projects.push_back(pr);
        p.addStandardPhases(pr.id);
        changed();
        openProject(pr.id, ProjectTab::Overview);
    });
}

void App::drawProjects() {
    screenHeader("Projects", "Click a project to open it.");
    TableSpec<Project> spec;
    spec.id = "projects";
    spec.columns = {"number", "name", "client", "manager", "status", "fee_type"};
    spec.extras = {
        {"Total fee", [this](const Project& p) { return usd(stats(p.id).totalFee); }, {}, true},
        {"Complete", [this](const Project& p) { return stats(p.id).complete.fixed(0) + "%"; }, {}, true},
        {"Health", [this](const Project& p) { return std::string(healthLabel(stats(p.id).health)); },
         [this](const Project& p) { return ui::healthColor(stats(p.id).health); }},
    };
    spec.filter = [this](const Project& p) {
        if (projectFilter_ == 0) return p.status == ProjectStatus::Active || p.status == ProjectStatus::OnHold;
        if (projectFilter_ == 1) return p.status == ProjectStatus::Proposal;
        return true;
    };
    spec.toolbar = [this] {
        ImGui::RadioButton("Active", &projectFilter_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Proposals", &projectFilter_, 1);
        ImGui::SameLine();
        ImGui::RadioButton("All", &projectFilter_, 2);
        ImGui::SameLine(0, ImGui::GetFontSize());
        if (ui::PrimaryButton("+ New project")) createProject();
    };
    spec.open = [this](int id) { openProject(id); };
    drawRecordTable(spec, practice_->projects);
}

// ------------------------------------------------------------- workspace

void App::drawProject() {
    Practice& practice = *practice_;
    Project& p = *practice.findProject(projectId_);
    const ProjectStats& st = stats(p.id);
    const float fs = ImGui::GetFontSize();

    if (ui::LinkButton("< Projects")) {
        go(Screen::Projects);
        return;
    }
    ui::Heading(practice.refName(Ref::Project, p.id).c_str());
    ImGui::SameLine(0, fs);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + fs * 0.45f);
    ui::Badge(choiceLabel(p.status), ui::statusColor(p.status));
    ImGui::SameLine(0, fs * 0.8f);
    ui::Badge(healthLabel(st.health), ui::healthColor(st.health));
    {
        const float w = fs * 13.0f;
        ImGui::SameLine();
        ui::AlignRight(w);
        const int id = p.id;
        const std::string file = safeReportName(p) + " status report.pdf";
        if (ImGui::Button("Status report (PDF)...", ImVec2(w - fs * 2.6f, 0)))
            saveOutput("Save the status report", {"PDF documents (*.pdf)", "*.pdf"}, "pdf", file,
                       [this, id] { return projectReportPdf(*practice_, id, today_); });
        ImGui::SameLine(0, fs * 0.3f);
        if (ImGui::Button("View", ImVec2(fs * 2.3f, 0)))
            saveOutput("", {"PDF", "*.pdf"}, "pdf", file, [this, id] { return projectReportPdf(*practice_, id, today_); }, true);
        ImGui::SetItemTooltip("Open the report in your PDF viewer");
    }
    {
        std::string sub = practice.refName(Ref::Client, p.clientId);
        if (p.managerId) sub += (sub.empty() ? "" : "  -  ") + std::string("PM ") + practice.refName(Ref::Staff, p.managerId);
        if (!p.location.empty()) sub += (sub.empty() ? "" : "  -  ") + p.location;
        ui::Muted(sub.empty() ? "Set the client and project manager on the Overview tab." : sub.c_str());
    }
    ImGui::Spacing();

    struct TabInfo {
        ProjectTab tab;
        const char* label;
        int count;
    };
    const TabInfo tabs[] = {
        {ProjectTab::Overview, "Overview", 0},
        {ProjectTab::Phases, "Phases & fee", 0},
        {ProjectTab::Schedule, "Schedule", 0},
        {ProjectTab::Tasks, "Tasks", st.tasksOverdue},
        {ProjectTab::Drawings, "Drawings", 0},
        {ProjectTab::Rfis, "RFIs", st.rfisOverdue},
        {ProjectTab::Submittals, "Submittals", st.submittalsOverdue},
        {ProjectTab::Changes, "Change orders", 0},
        {ProjectTab::Invoices, "Invoices", 0},
        {ProjectTab::Time, "Time", 0},
    };
    if (ImGui::BeginTabBar("##projecttabs")) {
        for (const auto& t : tabs) {
            std::string label = t.label;
            if (t.count > 0) label += " (" + std::to_string(t.count) + ")";
            label += "###" + std::string(t.label);
            const ImGuiTabItemFlags flags = tabRequested_ && tab_ == t.tab ? ImGuiTabItemFlags_SetSelected : 0;
            if (t.count > 0) ImGui::PushStyleColor(ImGuiCol_Text, colorNegative());
            const bool open = ImGui::BeginTabItem(label.c_str(), nullptr, flags);
            if (t.count > 0) ImGui::PopStyleColor();
            if (t.count > 0) ImGui::SetItemTooltip("%d overdue", t.count);
            if (!open) continue;
            if (!tabRequested_) tab_ = t.tab;
            ImGui::Spacing();
            ImGui::BeginChild("##tab", ImVec2(0, 0));
            switch (t.tab) {
                case ProjectTab::Overview: drawProjectOverview(p); break;
                case ProjectTab::Phases: drawPhases(p); break;
                case ProjectTab::Schedule: drawSchedule(p); break;
                case ProjectTab::Tasks: drawTasks(p); break;
                case ProjectTab::Drawings: drawDrawings(p); break;
                case ProjectTab::Rfis: drawRfis(p); break;
                case ProjectTab::Submittals: drawSubmittals(p); break;
                case ProjectTab::Changes: drawChanges(p); break;
                case ProjectTab::Invoices: drawInvoices(p); break;
                case ProjectTab::Time: drawProjectTime(p); break;
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    tabRequested_ = false;
}

std::string App::safeReportName(const Project& p) const {
    std::string out;
    for (char c : trim(p.number + " " + p.name)) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') out += c;
    }
    return out.empty() ? std::string("Project") : out;
}

void App::drawProjectOverview(Project& p) {
    Practice& practice = *practice_;
    const ProjectStats& st = stats(p.id);
    const float fs = ImGui::GetFontSize();
    const float gap = fs * 0.8f;
    const float cw = ui::CardWidth(6, gap);
    const ImVec4 text = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    std::string feeNote = "base " + usdShort(st.baseFee);
    if (!st.approvedChanges.isZero()) feeNote += " + " + usdShort(st.approvedChanges) + " CO";
    ui::StatCard("##fee", "Total fee", usdShort(st.totalFee), text, feeNote, cw);
    ImGui::SameLine(0, gap);
    ui::StatCard("##earned", "Earned", usdShort(st.earned), colorAccent(), st.complete.fixed(0) + "% complete", cw);
    ImGui::SameLine(0, gap);
    ui::StatCard("##profit", "Profit", usdShort(st.profit), st.profit < Money() ? colorNegative() : colorPositive(),
                 "after " + usdShort(st.totalCost) + " cost", cw);
    ImGui::SameLine(0, gap);
    const bool noLabor = st.laborCost.isZero();
    ui::StatCard("##mult", "Net multiplier", noLabor ? "-" : st.multiplier.fixed(2),
                 noLabor ? text : st.multiplier < practice.firm.targetMultiplier ? colorWarning() : colorPositive(),
                 "target " + practice.firm.targetMultiplier.fixed(2), cw);
    ImGui::SameLine(0, gap);
    ui::StatCard("##hours", "Hours", st.hours.fixed(0), ui::healthColor(st.health),
                 st.budgetHours.isZero() ? "no budget set" : "of " + st.budgetHours.fixed(0) + " budget", cw);
    ImGui::SameLine(0, gap);
    ui::StatCard("##billing", "Unbilled", usdShort(st.unbilled), st.unbilled > Money() ? colorWarning() : text,
                 usdShort(st.receivable) + " receivable", cw);
    ImGui::Spacing();
    if (!st.healthNote.empty()) ui::Callout((std::string(healthLabel(st.health)) + ": " + st.healthNote).c_str(), ui::healthColor(st.health));

    const float avail = ImGui::GetContentRegionAvail().x;
    const float left = std::floor(avail * 0.5f);
    const float right = avail - left - gap;

    ui::BeginCard("##details", left);
    ui::SubHeading("Project details");
    ImGui::Spacing();
    if (ui::RecordEditor("##project", practice, p, schema<Project>(), refPicker(), {}, fs * 10.0f)) changed();
    ImGui::Spacing();
    if (p.feeType == FeeType::PercentOfConstruction)
        ui::Muted(("Contract fee: " + usd(contractFee(p)) + " (" + p.feePercent.str() + "% of " + usd(p.constructionCost) + ")").c_str());
    if (!st.feePerSf.isZero()) ui::Muted(("Fee per sf: " + usd(st.feePerSf)).c_str());
    ui::EndCard(false);  // no trailing spacing: the next column sits beside this card

    ImGui::SameLine(0, gap);
    ImGui::BeginGroup();
    ui::BeginCard("##progress", right);
    ui::SubHeading("Phases");
    ImGui::Spacing();
    if (st.phases.empty()) {
        ui::Muted("No phases yet.");
        if (ImGui::Button("Add standard AIA phases")) {
            const int id = p.id;
            defer([this, id] {
                practice_->addStandardPhases(id);
                changed();
            });
        }
    }
    if (!st.phases.empty() && ImGui::BeginTable("##phasebars", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Phase", 0, 1.4f);
        ImGui::TableSetupColumn("Complete", 0, 1.5f);
        ImGui::TableSetupColumn("Hours", 0, 1.5f);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        ui::Muted("Complete");
        ImGui::TableNextColumn();
        ui::Muted("Budget hours used");
        for (const auto& ps : st.phases) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(trim(ps.phase->code + " " + ps.phase->name).c_str());
            ImGui::TableNextColumn();
            const std::string c = ps.phase->complete.fixed(0) + "%";
            ui::ProgressBar(ratio(ps.phase->complete, Decimal::fromInt(100)), colorAccent(),
                            std::max(fs * 2.0f, ImGui::GetContentRegionAvail().x - fs * 2.8f), c.c_str());
            ImGui::TableNextColumn();
            const std::string h = ps.budgetHours.isZero() ? ps.hours.fixed(0) + " h" : ps.hoursUsed.fixed(0) + "%";
            ui::ProgressBar(ratio(ps.hours, ps.budgetHours), ui::healthColor(ps.health),
                            std::max(fs * 2.0f, ImGui::GetContentRegionAvail().x - fs * 3.2f), h.c_str());
            ImGui::SetItemTooltip("%s of %s budget hours", ps.hours.fixed(1).c_str(), ps.budgetHours.fixed(0).c_str());
        }
        ImGui::EndTable();
    }
    ui::EndCard();

    ui::BeginCard("##work", right);
    ui::SubHeading("Work");
    ImGui::Spacing();
    auto row = [&](const char* label, const std::string& value, bool bad, ProjectTab tab) {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine(fs * 10.0f);
        if (bad) ImGui::TextColored(colorNegative(), "%s", value.c_str());
        else ImGui::TextUnformatted(value.c_str());
        ImGui::SameLine();
        ui::AlignRight(fs * 2.6f);
        if (ui::LinkButton("Open")) openProject(p.id, tab);
        ImGui::PopID();
    };
    row("Tasks", std::to_string(st.tasksOpen) + " open" + (st.tasksOverdue ? ", " + std::to_string(st.tasksOverdue) + " overdue" : ""),
        st.tasksOverdue > 0, ProjectTab::Tasks);
    row("RFIs", std::to_string(st.rfisOpen) + " open" + (st.rfisOverdue ? ", " + std::to_string(st.rfisOverdue) + " overdue" : ""),
        st.rfisOverdue > 0, ProjectTab::Rfis);
    row("Submittals",
        std::to_string(st.submittalsOpen) + " in review" +
            (st.submittalsOverdue ? ", " + std::to_string(st.submittalsOverdue) + " overdue" : ""),
        st.submittalsOverdue > 0, ProjectTab::Submittals);
    row("Drawings", std::to_string(st.sheetsIssued) + " of " + std::to_string(st.sheetsTotal) + " sheets issued", false,
        ProjectTab::Drawings);
    row("Billing", usd(st.billed) + " invoiced, " + usd(st.collected) + " collected", false, ProjectTab::Invoices);
    ui::EndCard();

    ui::BeginCard("##danger", right);
    ui::SubHeading("Delete project");
    ui::MutedWrapped("Deletes the project with its phases, tasks, time, drawings, RFIs, submittals, change orders and "
                     "invoices. Consider setting the status to Complete or Cancelled instead.");
    ImGui::Spacing();
    if (ui::DangerButton("Delete project...")) {
        const int id = p.id;
        confirm("Delete this project?",
                "Delete " + practice.refName(Ref::Project, id) + " and everything recorded on it? This can't be undone.",
                "Delete", [this, id] {
                    defer([this, id] {
                        practice_->removeProject(id);
                        changed();
                        go(Screen::Projects);
                    });
                });
    }
    ui::EndCard();
    ImGui::EndGroup();
}

// ------------------------------------------------------------------ phases

void App::drawPhases(Project& p) {
    const ProjectStats& st = stats(p.id);
    const int projectId = p.id;
    auto phaseStats = [this, projectId](const Phase& ph) -> const PhaseStats* {
        for (const auto& ps : stats(projectId).phases) {
            if (ps.phase->id == ph.id) return &ps;
        }
        return nullptr;
    };

    Decimal shares;
    for (const auto& ps : st.phases) shares += ps.phase->feeShare;
    const bool hourly = p.feeType == FeeType::Hourly || p.feeType == FeeType::HourlyNotToExceed;
    summaryLine({{"Total fee", usd(st.totalFee)},
                 {"Earned", usd(st.earned)},
                 {"Shares", shares.str() + "%"},
                 {"Hours", st.hours.fixed(1) + " of " + st.budgetHours.fixed(0)},
                 {"Direct labor", usd(st.laborCost)}});
    if (!st.phases.empty() && !hourly && shares != Decimal::fromInt(100))
        ui::Callout(("Phase shares add to " + shares.str() + "%. They should add to 100% so the whole fee can be earned.").c_str(),
                    colorNegative());
    if (hourly)
        ui::MutedWrapped("This is an hourly project: the fee is earned from billable time at each person's billing rate, so "
                         "% complete is for tracking only.");

    TableSpec<Phase> spec;
    spec.id = "phases";
    spec.columns = {"code", "name", "fee_share", "complete", "budget_hours", "start", "end"};
    spec.inlineEdit = {"complete"};
    spec.extras = {
        {"Fee", [=](const Phase& ph) { auto s = phaseStats(ph); return s ? usd(s->fee) : std::string(); }, {}, true},
        {"Earned", [=](const Phase& ph) { auto s = phaseStats(ph); return s ? usd(s->earned) : std::string(); }, {}, true},
        {"Actual h", [=](const Phase& ph) { auto s = phaseStats(ph); return s ? s->hours.fixed(1) : std::string(); }, {}, true},
        {"Labor", [=](const Phase& ph) { auto s = phaseStats(ph); return s ? usd(s->laborCost) : std::string(); }, {}, true},
        {"Health", [=](const Phase& ph) { auto s = phaseStats(ph); return s ? std::string(healthLabel(s->health)) : std::string(); },
         [=](const Phase& ph) { auto s = phaseStats(ph); return s ? ui::healthColor(s->health) : colorMuted(); }},
    };
    spec.sortColumn = 5;  // start date
    spec.filter = [projectId](const Phase& ph) { return ph.projectId == projectId; };
    spec.make = [projectId] {
        Phase ph;
        ph.projectId = projectId;
        ph.code = "AS";
        ph.name = "Additional services";
        return ph;
    };
    spec.addLabel = "Add phase";
    spec.hide = hideProjectField;
    spec.toolbar = [this, projectId, &st] {
        if (st.phases.empty() && ImGui::Button("Add standard AIA phases")) {
            defer([this, projectId] {
                practice_->addStandardPhases(projectId);
                changed();
            });
        }
    };
    spec.deleteBlocker = [this](const Phase& ph) {
        int n = 0;
        for (const auto& t : practice_->time) n += t.phaseId == ph.id;
        return n == 0 ? std::string()
                      : "This phase has " + std::to_string(n) + " time entries. Move them to another phase before deleting it.";
    };
    spec.onDelete = [this](int id) { practice_->clearReferences(Ref::Phase, id); };
    drawRecordTable(spec, practice_->phases);
}

// ------------------------------------------------------------------- tasks

void App::drawTasks(Project& p) {
    const int projectId = p.id;
    TableSpec<Task> spec;
    spec.id = "tasks";
    spec.columns = {"name", "phase", "assignee", "status", "start", "due", "estimate", "after", "milestone"};
    spec.inlineEdit = {"status"};
    spec.sortColumn = 5;
    spec.extras = {
        {"Late", [this](const Task& t) { return isOverdue(t, today_) ? daysText(today_ - *t.due) : std::string(); },
         [](const Task&) { return colorNegative(); }},
    };
    spec.filter = [this, projectId](const Task& t) {
        return t.projectId == projectId && !(hideDoneTasks_ && t.status == TaskStatus::Done);
    };
    spec.toolbar = [this] { ImGui::Checkbox("Hide done", &hideDoneTasks_); };
    spec.make = [this, projectId] {
        Task t;
        t.projectId = projectId;
        t.phaseId = currentPhaseId(*practice_, projectId);
        t.name = "New task";
        t.start = today_;
        t.due = today_.addDays(7);
        return t;
    };
    spec.addLabel = "Add task";
    spec.hide = hideProjectField;
    spec.editorExtras = [this](Task& t) {
        if (startsBeforePredecessor(*practice_, t))
            ui::Callout("Starts before the task it comes after is due.", colorWarning());
    };
    spec.onDelete = [this](int id) { practice_->clearReferences(Ref::Task, id); };
    drawRecordTable(spec, practice_->tasks);
}

// ---------------------------------------------------------------- drawings

void App::drawDrawings(Project& p) {
    const int projectId = p.id;
    const ProjectStats& st = stats(projectId);
    summaryLine({{"Sheets", std::to_string(st.sheetsTotal)}, {"Issued", std::to_string(st.sheetsIssued)}});

    TableSpec<Sheet> spec;
    spec.id = "sheets";
    spec.columns = {"number", "title", "discipline", "phase", "revision", "status", "issued"};
    spec.inlineEdit = {"status"};
    spec.filter = [projectId](const Sheet& s) { return s.projectId == projectId; };
    spec.make = [this, projectId] {
        Sheet s;
        s.projectId = projectId;
        s.phaseId = currentPhaseId(*practice_, projectId);
        int n = 0;
        for (const auto& other : practice_->sheets) n += other.projectId == projectId && other.discipline == Discipline::Architectural;
        s.number = "A-" + std::to_string(101 + n);
        s.title = "New sheet";
        return s;
    };
    spec.addLabel = "Add sheet";
    spec.hide = hideProjectField;
    spec.toolbar = [this, projectId] {
        if (ImGui::Button("Issue set...")) requestPopup("Issue drawing set");
        ImGui::SetItemTooltip("Mark sheets that are ready as issued, with a new revision and today's date.");
        const float fs = ImGui::GetFontSize();
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Issue drawing set", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static bool includeInProgress = false;
            static bool bumpRevision = true;
            int forReview = 0, inProgress = 0;
            for (const auto& s : practice_->sheets) {
                if (s.projectId != projectId) continue;
                forReview += s.status == SheetStatus::ForReview;
                inProgress += s.status == SheetStatus::InProgress;
            }
            ui::SubHeading("Issue a drawing set");
            ImGui::Text("%d sheets are ready for review.", forReview);
            ImGui::Checkbox(("Also issue " + std::to_string(inProgress) + " sheets still in progress").c_str(), &includeInProgress);
            ImGui::Checkbox("Increase each sheet's revision number", &bumpRevision);
            ImGui::Spacing();
            const int count = forReview + (includeInProgress ? inProgress : 0);
            ImGui::BeginDisabled(count == 0);
            if (ui::PrimaryButton(("Issue " + std::to_string(count) + " sheets").c_str(), ImVec2(fs * 10, 0))) {
                for (auto& s : practice_->sheets) {
                    if (s.projectId != projectId) continue;
                    if (s.status == SheetStatus::ForReview || (includeInProgress && s.status == SheetStatus::InProgress)) {
                        s.status = SheetStatus::Issued;
                        s.issued = today_;
                        if (bumpRevision) ++s.revision;
                    }
                }
                changed();
                notify("Issued " + std::to_string(count) + " sheets");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(fs * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    };
    drawRecordTable(spec, practice_->sheets);
}

// -------------------------------------------------------------------- RFIs

void App::drawRfis(Project& p) {
    const int projectId = p.id;
    const ProjectStats& st = stats(projectId);
    summaryLine({{"Open", std::to_string(st.rfisOpen)}, {"Overdue", std::to_string(st.rfisOverdue)}});

    TableSpec<Rfi> spec;
    spec.id = "rfis";
    spec.columns = {"number", "subject", "from", "received", "due", "status", "assignee"};
    spec.inlineEdit = {"status"};
    spec.sortColumn = 0;
    spec.sortDescending = true;
    spec.extras = {
        {"Age",
         [this](const Rfi& r) {
             if (!r.received) return std::string();
             const Date end = r.status == RfiStatus::Open || !r.answered ? today_ : *r.answered;
             return daysText(end - *r.received);
         },
         [this](const Rfi& r) {
             return r.status == RfiStatus::Open && r.due && *r.due < today_ ? colorNegative()
                                                                            : ImGui::GetStyleColorVec4(ImGuiCol_Text);
         },
         true},
    };
    spec.filter = [projectId](const Rfi& r) { return r.projectId == projectId; };
    spec.make = [this, projectId] {
        Rfi r;
        r.projectId = projectId;
        r.number = practice_->nextRfiNumber(projectId);
        r.subject = "New RFI";
        r.received = today_;
        r.due = today_.addDays(7);
        return r;
    };
    spec.addLabel = "Log RFI";
    spec.hide = hideProjectField;
    spec.editorExtras = [this](Rfi& r) {
        if (r.status == RfiStatus::Open && !r.response.empty() && ImGui::Button("Mark answered today")) {
            r.status = RfiStatus::Answered;
            r.answered = today_;
            changed();
        }
    };
    drawRecordTable(spec, practice_->rfis);
}

// -------------------------------------------------------------- submittals

void App::drawSubmittals(Project& p) {
    const int projectId = p.id;
    const ProjectStats& st = stats(projectId);
    summaryLine({{"In review", std::to_string(st.submittalsOpen)}, {"Overdue", std::to_string(st.submittalsOverdue)}});

    TableSpec<Submittal> spec;
    spec.id = "submittals";
    spec.columns = {"number", "description", "contractor", "received", "due", "status", "reviewer"};
    spec.inlineEdit = {"status"};
    spec.sortColumn = 4;
    spec.extras = {
        {"Days",
         [this](const Submittal& s) {
             if (!s.received) return std::string();
             const bool open = s.status == SubmittalStatus::Pending || s.status == SubmittalStatus::UnderReview;
             const Date end = open || !s.returned ? today_ : *s.returned;
             return std::to_string(end - *s.received);
         },
         [this](const Submittal& s) {
             const bool open = s.status == SubmittalStatus::Pending || s.status == SubmittalStatus::UnderReview;
             return open && s.due && *s.due < today_ ? colorNegative() : ImGui::GetStyleColorVec4(ImGuiCol_Text);
         },
         true},
    };
    spec.filter = [projectId](const Submittal& s) { return s.projectId == projectId; };
    spec.make = [this, projectId] {
        Submittal s;
        s.projectId = projectId;
        s.description = "New submittal";
        s.received = today_;
        s.due = today_.addDays(14);
        return s;
    };
    spec.addLabel = "Log submittal";
    spec.hide = hideProjectField;
    drawRecordTable(spec, practice_->submittals);
}

// ----------------------------------------------------------- change orders

void App::drawChanges(Project& p) {
    const int projectId = p.id;
    const ProjectStats& st = stats(projectId);
    summaryLine({{"Base fee", usd(st.baseFee)}, {"Approved", usd(st.approvedChanges)}, {"Pending", usd(st.pendingChanges)},
                 {"Total fee", usd(st.totalFee)}});

    TableSpec<ChangeOrder> spec;
    spec.id = "changes";
    spec.columns = {"number", "description", "amount", "hours", "status", "date"};
    spec.inlineEdit = {"status"};
    spec.filter = [projectId](const ChangeOrder& c) { return c.projectId == projectId; };
    spec.make = [this, projectId] {
        ChangeOrder c;
        c.projectId = projectId;
        c.number = practice_->nextChangeNumber(projectId);
        c.description = "Additional services";
        c.date = today_;
        return c;
    };
    spec.addLabel = "Add change order";
    spec.hide = hideProjectField;
    drawRecordTable(spec, practice_->changes);
}

// ---------------------------------------------------------------- invoices

void App::drawInvoices(Project& p) {
    const int projectId = p.id;
    const ProjectStats& st = stats(projectId);
    summaryLine({{"Earned", usd(st.earned)},
                 {"Invoiced", usd(st.billed)},
                 {"Collected", usd(st.collected)},
                 {"Receivable", usd(st.receivable)},
                 {"Unbilled", usd(st.unbilled)}});

    TableSpec<Invoice> spec;
    spec.id = "invoices";
    spec.columns = {"number", "date", "due", "amount", "paid", "status"};
    spec.inlineEdit = {"status"};
    spec.sortColumn = 1;
    spec.sortDescending = true;
    spec.extras = {
        {"Balance",
         [](const Invoice& i) {
             const Money paid = i.status == InvoiceStatus::Paid && i.paid.isZero() ? i.amount : i.paid;
             return i.status == InvoiceStatus::Draft ? std::string() : usd(i.amount - paid);
         },
         {}, true},
        {"Past due",
         [this](const Invoice& i) {
             return i.status == InvoiceStatus::Sent && i.due && *i.due < today_ ? daysText(today_ - *i.due) : std::string();
         },
         [](const Invoice&) { return colorNegative(); }},
    };
    spec.filter = [projectId](const Invoice& i) { return i.projectId == projectId; };
    spec.make = [this, projectId] {
        Invoice i;
        i.projectId = projectId;
        i.number = practice_->nextInvoiceNumber(projectId);
        i.date = today_;
        i.due = today_.addDays(30);
        return i;
    };
    spec.addLabel = "New invoice";
    spec.hide = hideProjectField;
    const Money unbilled = st.unbilled;
    spec.toolbar = [this, projectId, unbilled] {
        ImGui::BeginDisabled(unbilled <= Money());
        if (ImGui::Button(("Invoice earned fee (" + usd(unbilled > Money() ? unbilled : Money()) + ")").c_str())) {
            defer([this, projectId, unbilled] {
                Invoice i;
                i.id = Practice::nextId(practice_->invoices);
                i.projectId = projectId;
                i.number = practice_->nextInvoiceNumber(projectId);
                i.date = today_;
                i.due = today_.addDays(30);
                i.amount = unbilled;
                i.notes = "Fee earned through " + today_.str();
                practice_->invoices.push_back(i);
                selection_["invoices"] = i.id;
                changed();
                notify("Draft invoice " + i.number + " created");
            });
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip("Creates a draft invoice for fee earned but not yet invoiced.");
    };
    spec.editorExtras = [this](Invoice& i) {
        if (i.status != InvoiceStatus::Paid && ImGui::Button("Record full payment")) {
            i.status = InvoiceStatus::Paid;
            i.paid = i.amount;
            changed();
        }
    };
    drawRecordTable(spec, practice_->invoices);
}

// -------------------------------------------------------------------- time

void App::drawProjectTime(Project& p) {
    const int projectId = p.id;
    const ProjectStats& st = stats(projectId);
    summaryLine({{"Hours", st.hours.fixed(1)},
                 {"Billable", st.billableHours.fixed(1)},
                 {"Direct labor", usd(st.laborCost)},
                 {"At billing rates", usd(st.laborValue)}});

    TableSpec<TimeEntry> spec;
    spec.id = "projecttime";
    spec.columns = {"date", "staff", "phase", "hours", "billable", "notes"};
    spec.sortColumn = 0;
    spec.sortDescending = true;
    spec.extras = {
        {"Cost", [this](const TimeEntry& t) { return usd(laborCost(*practice_, t)); }, {}, true},
    };
    spec.filter = [projectId](const TimeEntry& t) { return t.projectId == projectId; };
    spec.make = [this, projectId] {
        TimeEntry t;
        t.projectId = projectId;
        t.phaseId = currentPhaseId(*practice_, projectId);
        t.staffId = sheetStaff_;
        t.date = today_;
        return t;
    };
    spec.addLabel = "Add time";
    spec.hide = hideProjectField;
    drawRecordTable(spec, practice_->time);
}

}  // namespace opgui
