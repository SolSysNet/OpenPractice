// Firm-wide screens: the weekly timesheet, team, clients, reports and firm settings.

#include "app.hpp"
#include "imgui.h"
#include "openpractice/format.hpp"
#include "openpractice/report.hpp"
#include "openpractice/util.hpp"
#include "record_table.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>

namespace opgui {

using namespace op;

namespace {

const char* kDayNames[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
const char* kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string longDate(Date d) {
    char text[32];
    std::snprintf(text, sizeof text, "%s %u, %d", kMonthNames[d.month() - 1], d.day(), d.year());
    return text;
}

}  // namespace

// --------------------------------------------------------------- timesheet

void App::drawTimesheet() {
    Practice& p = *practice_;
    const float fs = ImGui::GetFontSize();
    screenHeader("Timesheet", "Hours by project and phase for one person, one week at a time. Changes save automatically.");

    if (p.staff.empty()) {
        ui::Callout("Add your team first (Team), then record time here.", colorAccent());
        return;
    }
    if (!p.findStaff(sheetStaff_)) sheetStaff_ = p.staff.front().id;

    // ---- toolbar: person and week
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Person");
    ImGui::SameLine();
    {
        std::vector<ui::Option> people = refOptions(Ref::Staff, 0);
        people.erase(people.begin());  // no "(none)"
        ui::SearchCombo("##staff", p.refName(Ref::Staff, sheetStaff_), people, sheetStaff_, fs * 12.0f);
    }
    ImGui::SameLine(0, fs * 1.5f);
    if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) sheetWeek_ = sheetWeek_.addDays(-7);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    const std::string week = "Week of " + longDate(sheetWeek_);
    ImGui::PushFont(g_fonts.bold, 0.0f);
    ImGui::TextUnformatted(week.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) sheetWeek_ = sheetWeek_.addDays(7);
    ImGui::SameLine();
    ImGui::BeginDisabled(sheetWeek_ == weekStart(today_));
    if (ImGui::Button("This week")) sheetWeek_ = weekStart(today_);
    ImGui::EndDisabled();

    const int staffId = sheetStaff_;
    const Date monday = sheetWeek_;
    const Date sunday = monday.addDays(6);

    // ---- rows: anything with hours this week, plus rows added this session
    struct Row {
        int project;
        int phase;
    };
    std::vector<Row> rows;
    auto addRow = [&](int project, int phase) {
        for (const auto& r : rows) {
            if (r.project == project && r.phase == phase) return;
        }
        rows.push_back({project, phase});
    };
    for (const auto& t : p.time) {
        if (t.staffId == staffId && t.date && *t.date >= monday && *t.date <= sunday) addRow(t.projectId, t.phaseId);
    }
    for (const auto& r : sheetRows_) {
        if (r.staff == staffId && p.findProject(r.project)) addRow(r.project, r.phase);
    }

    ImGui::SameLine(0, fs * 1.5f);
    if (ImGui::Button("Copy rows from last week")) {
        int added = 0;
        for (const auto& t : p.time) {
            if (t.staffId != staffId || !t.date || *t.date < monday.addDays(-7) || *t.date >= monday) continue;
            const bool present = std::any_of(rows.begin(), rows.end(), [&](const Row& r) { return r.project == t.projectId && r.phase == t.phaseId; });
            if (!present) {
                sheetRows_.push_back({staffId, t.projectId, t.phaseId});
                addRow(t.projectId, t.phaseId);
                ++added;
            }
        }
        notify(added ? "Added " + std::to_string(added) + " rows from last week" : std::string("No other rows last week"));
    }

    std::stable_sort(rows.begin(), rows.end(), [&](const Row& a, const Row& b) {
        const std::string na = p.refName(Ref::Project, a.project);
        const std::string nb = p.refName(Ref::Project, b.project);
        if (na != nb) return na < nb;
        return p.refName(Ref::Phase, a.phase) < p.refName(Ref::Phase, b.phase);
    });

    // Sets one cell: the first entry takes the new total and any others are removed.
    auto setCell = [this, staffId](int project, int phase, Date day, Decimal hours) {
        defer([this, staffId, project, phase, day, hours] {
            auto& time = practice_->time;
            bool kept = false;
            for (auto it = time.begin(); it != time.end();) {
                if (it->staffId == staffId && it->projectId == project && it->phaseId == phase && it->date && *it->date == day) {
                    if (!kept && hours > Decimal()) {
                        it->hours = hours;
                        kept = true;
                        ++it;
                    } else {
                        it = time.erase(it);
                    }
                } else {
                    ++it;
                }
            }
            if (!kept && hours > Decimal()) {
                TimeEntry t;
                t.id = Practice::nextId(time);
                t.date = day;
                t.staffId = staffId;
                t.projectId = project;
                t.phaseId = phase;
                t.hours = hours;
                time.push_back(t);
            }
            changed();
        });
    };

    ImGui::Spacing();
    const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuterH |
                               ImGuiTableFlags_SizingFixedFit;
    Decimal dayTotals[7];
    Decimal weekTotal;
    if (ImGui::BeginTable("##sheet", 10, tf)) {
        ImGui::TableSetupColumn("Project", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthFixed, fs * 11.0f);
        for (int d = 0; d < 7; ++d) ImGui::TableSetupColumn(kDayNames[d], ImGuiTableColumnFlags_WidthFixed, fs * 4.2f);
        ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, fs * 4.2f);

        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Project");
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Phase");
        for (int d = 0; d < 7; ++d) {
            ImGui::TableNextColumn();
            const Date day = monday.addDays(d);
            char label[16];
            std::snprintf(label, sizeof label, "%s %u", kDayNames[d], day.day());
            if (day == today_) ImGui::TextColored(colorAccent(), "%s", label);
            else if (d >= 5) ui::Muted(label);
            else ImGui::TextUnformatted(label);
        }
        ImGui::TableNextColumn();
        ui::TextRight("Total");

        int n = 0;
        for (const auto& row : rows) {
            ImGui::PushID(n++);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(p.refName(Ref::Project, row.project).c_str());
            if (ImGui::BeginPopupContextItem("##rowmenu")) {
                ImGui::TextDisabled("%s", p.refName(Ref::Project, row.project).c_str());
                ImGui::Separator();
                if (ImGui::MenuItem("Open project")) openProject(row.project, ProjectTab::Time);
                if (ImGui::MenuItem("Clear this week's hours")) {
                    const Row cleared = row;
                    defer([this, staffId, monday, sunday, cleared] {
                        auto& time = practice_->time;
                        time.erase(std::remove_if(time.begin(), time.end(),
                                                  [&](const TimeEntry& t) {
                                                      return t.staffId == staffId && t.projectId == cleared.project &&
                                                             t.phaseId == cleared.phase && t.date && *t.date >= monday &&
                                                             *t.date <= sunday;
                                                  }),
                                   time.end());
                        sheetRows_.erase(std::remove_if(sheetRows_.begin(), sheetRows_.end(),
                                                        [&](const SheetRow& r) {
                                                            return r.staff == staffId && r.project == cleared.project &&
                                                                   r.phase == cleared.phase;
                                                        }),
                                         sheetRows_.end());
                        changed();
                    });
                }
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            const std::string phase = p.refName(Ref::Phase, row.phase);
            ImGui::TextUnformatted(phase.empty() ? "-" : phase.c_str());
            Decimal rowTotal;
            for (int d = 0; d < 7; ++d) {
                ImGui::TableNextColumn();
                const Date day = monday.addDays(d);
                Decimal hours;
                for (const auto& t : p.time) {
                    if (t.staffId == staffId && t.projectId == row.project && t.phaseId == row.phase && t.date && *t.date == day)
                        hours += t.hours;
                }
                ImGui::PushID(d);
                Decimal edited = hours;
                if (ui::DecimalInput("##h", edited, -FLT_MIN, "")) {
                    if (edited < Decimal() || edited > Decimal::fromInt(24)) notify("Hours for a day must be between 0 and 24.", true);
                    else setCell(row.project, row.phase, day, edited);
                }
                ImGui::PopID();
                rowTotal += hours;
                dayTotals[d] += hours;
            }
            weekTotal += rowTotal;
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(g_fonts.bold, 0.0f);
            ui::TextRight(rowTotal.fixed(1).c_str());
            ImGui::PopFont();
            ImGui::PopID();
        }

        // ---- add a row
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        {
            std::vector<ui::Option> projects;
            for (const auto& pr : p.projects) {
                if (pr.status == ProjectStatus::Active || pr.status == ProjectStatus::Proposal)
                    projects.push_back({pr.id, p.refName(Ref::Project, pr.id), false});
            }
            std::string preview = p.refName(Ref::Project, addRowProject_);
            if (ui::SearchCombo("##addproject", preview.empty() ? "Add a project..." : preview, projects, addRowProject_, -FLT_MIN))
                addRowPhase_ = 0;
        }
        ImGui::TableNextColumn();
        {
            const auto phases = refOptions(Ref::Phase, addRowProject_);
            std::string preview = p.refName(Ref::Phase, addRowPhase_);
            ui::SearchCombo("##addphase", preview.empty() ? "(no phase)" : preview, phases, addRowPhase_, -FLT_MIN);
        }
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(addRowProject_ == 0);
        if (ImGui::Button("Add row")) {
            sheetRows_.push_back({staffId, addRowProject_, addRowPhase_});
            addRowProject_ = addRowPhase_ = 0;
        }
        ImGui::EndDisabled();

        // ---- totals
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableNextColumn();
        ImGui::PushFont(g_fonts.bold, 0.0f);
        ImGui::TextUnformatted("Total");
        ImGui::PopFont();
        ImGui::TableNextColumn();
        for (int d = 0; d < 7; ++d) {
            ImGui::TableNextColumn();
            ui::TextRightColored(dayTotals[d] > Decimal::fromInt(10) ? colorWarning() : ImGui::GetStyleColorVec4(ImGuiCol_Text),
                                 dayTotals[d].fixed(1).c_str());
        }
        ImGui::TableNextColumn();
        const Staff* person = p.findStaff(staffId);
        const bool over = person && weekTotal > person->weeklyCapacity;
        ImGui::PushFont(g_fonts.bold, 0.0f);
        ui::TextRightColored(over ? colorWarning() : colorAccent(), weekTotal.fixed(1).c_str());
        ImGui::PopFont();
        ImGui::EndTable();
    }
    if (const Staff* person = p.findStaff(staffId)) {
        ImGui::Spacing();
        const std::string cap = weekTotal.fixed(1) + " of " + person->weeklyCapacity.fixed(0) + " hours this week";
        ui::ProgressBar(person->weeklyCapacity.isZero() ? 0.0f
                                                        : static_cast<float>(weekTotal.raw()) / static_cast<float>(person->weeklyCapacity.raw()),
                        colorAccent(), fs * 14.0f, cap.c_str());
    }
    ImGui::Spacing();
    ui::MutedWrapped("Each cell is the total for that day. Notes and non-billable hours are edited on a project's Time tab.");
}

// -------------------------------------------------------------------- team

void App::drawTeam() {
    const Date to = today_;
    const Date from = today_.addDays(-27);
    const auto load = staffLoad(*practice_, from, to);
    screenHeader("Team", "Rates drive project cost and profit. Utilization is billable hours as a share of capacity over the last four weeks.");

    auto loadOf = [load](const Staff& s) -> StaffLoad {
        for (const auto& l : load) {
            if (l.staff->id == s.id) return l;
        }
        return {};
    };
    TableSpec<Staff> spec;
    spec.id = "staff";
    spec.columns = {"name", "initials", "role", "license", "billing_rate", "cost_rate", "capacity", "active"};
    spec.extras = {
        {"Hours (4 wk)", [=](const Staff& s) { return loadOf(s).hours.fixed(1); }, {}, true},
        {"Utilization", [=](const Staff& s) { return loadOf(s).utilization.fixed(0) + "%"; },
         [=](const Staff& s) {
             const Decimal u = loadOf(s).utilization;
             return u > Decimal::fromInt(100) ? colorNegative() : u < Decimal::fromInt(60) ? colorWarning() : colorPositive();
         },
         true},
        {"Open tasks", [=](const Staff& s) { return std::to_string(loadOf(s).openTasks); }, {}, true},
    };
    spec.make = [] {
        Staff s;
        s.name = "New team member";
        return s;
    };
    spec.addLabel = "Add person";
    spec.deleteBlocker = [this](const Staff& s) {
        const auto refs = practice_->referencesTo(Ref::Staff, s.id);
        if (refs.empty()) return std::string();
        return s.name + " is on " + join(refs, ", ") + ". Clear Active instead, to keep their history.";
    };
    drawRecordTable(spec, practice_->staff);
}

// ----------------------------------------------------------------- clients

void App::drawClients() {
    screenHeader("Clients");
    TableSpec<Client> spec;
    spec.id = "clients";
    spec.columns = {"name", "contact", "email", "phone"};
    spec.extras = {
        {"Projects",
         [this](const Client& c) {
             int n = 0;
             for (const auto& p : practice_->projects) n += p.clientId == c.id;
             return std::to_string(n);
         },
         {}, true},
        {"Total fees",
         [this](const Client& c) {
             Money total;
             for (const auto& p : practice_->projects) {
                 if (p.clientId == c.id && p.status != ProjectStatus::Cancelled && p.status != ProjectStatus::Proposal)
                     total += stats(p.id).totalFee;
             }
             return usd(total);
         },
         {}, true},
    };
    spec.make = [] {
        Client c;
        c.name = "New client";
        return c;
    };
    spec.addLabel = "Add client";
    spec.editorExtras = [this](Client& c) {
        bool any = false;
        for (const auto& p : practice_->projects) {
            if (p.clientId != c.id) continue;
            if (!any) {
                ImGui::Spacing();
                ui::SubHeading("Projects");
                any = true;
            }
            ImGui::PushID(p.id);
            if (ui::LinkButton(practice_->refName(Ref::Project, p.id).c_str())) openProject(p.id);
            ImGui::PopID();
        }
    };
    spec.deleteBlocker = [this](const Client& c) {
        const auto refs = practice_->referencesTo(Ref::Client, c.id);
        return refs.empty() ? std::string() : c.name + " is the client on " + join(refs, ", ") + ".";
    };
    drawRecordTable(spec, practice_->clients);
}

// ----------------------------------------------------------------- reports

void App::drawReports() {
    Practice& p = *practice_;
    const float fs = ImGui::GetFontSize();
    screenHeader("Reports", "PDF reports to share with clients and partners, and CSV for spreadsheets.");
    const float gap = fs * 0.8f;
    const float cw = std::min(ui::CardWidth(2, gap), fs * 30.0f);
    const FileFilter pdfFilter{"PDF documents (*.pdf)", "*.pdf"};

    if (!p.findProject(reportProject_)) reportProject_ = p.projects.empty() ? 0 : p.projects.front().id;
    ui::BeginCard("##status", cw);
    ui::SubHeading("Project status report");
    ui::MutedWrapped("Fee and budget, phases, open tasks, RFIs, submittals, drawing log, change orders and invoices.");
    ImGui::Spacing();
    {
        auto options = refOptions(Ref::Project, 0);
        options.erase(options.begin());
        ui::SearchCombo("##project", p.refName(Ref::Project, reportProject_), options, reportProject_, cw - fs * 2.2f);
    }
    ImGui::BeginDisabled(reportProject_ == 0);
    const int id = reportProject_;
    const std::string file = p.findProject(id) ? safeReportName(*p.findProject(id)) + " status report.pdf" : "status report.pdf";
    if (ui::PrimaryButton("Save PDF...")) saveOutput("Save the status report", pdfFilter, "pdf", file, [this, id] { return projectReportPdf(*practice_, id, today_); });
    ImGui::SameLine();
    if (ImGui::Button("View")) saveOutput("", pdfFilter, "pdf", file, [this, id] { return projectReportPdf(*practice_, id, today_); }, true);
    ImGui::EndDisabled();
    ui::EndCard(false);

    ImGui::SameLine(0, gap);
    ui::BeginCard("##portfolio", cw);
    ui::SubHeading("Portfolio report");
    ui::MutedWrapped("Every project's fee, progress, profit and health, firm totals, staff utilization and what needs attention.");
    ImGui::Spacing();
    if (ui::PrimaryButton("Save PDF...##portfolio"))
        saveOutput("Save the portfolio report", pdfFilter, "pdf", "Portfolio report " + today_.str() + ".pdf",
                   [this] { return portfolioReportPdf(*practice_, today_); });
    ImGui::SameLine();
    if (ImGui::Button("View##portfolio"))
        saveOutput("", pdfFilter, "pdf", "Portfolio report " + today_.str() + ".pdf",
                   [this] { return portfolioReportPdf(*practice_, today_); }, true);
    ui::EndCard(false);

    ImGui::Spacing();
    ui::BeginCard("##csv", cw);
    ui::SubHeading("Export to CSV");
    ui::MutedWrapped("Any list, with names instead of ids. Amounts are plain numbers so spreadsheets can total them.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(fs * 10.0f);
    if (ImGui::BeginCombo("##list", listTitle(exportList_).c_str())) {
        for (const auto& name : listNames()) {
            if (ImGui::Selectable(listTitle(name).c_str(), name == exportList_)) exportList_ = name;
        }
        ImGui::EndCombo();
    }
    const bool perProject = exportList_ != "staff" && exportList_ != "clients" && exportList_ != "projects";
    if (perProject) {
        ImGui::SameLine();
        auto options = refOptions(Ref::Project, 0);
        options[0].label = "All projects";
        std::string preview = p.refName(Ref::Project, exportProject_);
        ui::SearchCombo("##exportproject", preview.empty() ? "All projects" : preview, options, exportProject_,
                        cw - fs * 13.2f);
    }
    const int projectId = perProject ? exportProject_ : 0;
    const std::string list = exportList_;
    if (ui::PrimaryButton("Save CSV...")) {
        std::string name = listTitle(list);
        if (projectId) name += " - " + safeReportName(*p.findProject(projectId));
        saveOutput("Export to CSV", {"CSV files (*.csv)", "*.csv"}, "csv", name + ".csv",
                   [this, list, projectId] { return exportCsv(*practice_, list, projectId); });
    }
    ui::EndCard(false);

    ImGui::SameLine(0, gap);
    ui::BeginCard("##cli", cw);
    ui::SubHeading("Command line");
    ui::MutedWrapped("The openpractice command works on the same file, for scripts and quick entries:");
    ImGui::Spacing();
    ImGui::TextUnformatted("openpractice summary firm.opp");
    ImGui::TextUnformatted("openpractice log firm.opp --staff MC --project 2025-031 \\");
    ImGui::TextUnformatted("    --phase CD --hours 3.5");
    ImGui::TextUnformatted("openpractice export firm.opp rfis rfis.csv");
    ui::EndCard(false);
}

// -------------------------------------------------------------------- firm

void App::drawFirm() {
    Practice& p = *practice_;
    const float fs = ImGui::GetFontSize();
    screenHeader("Firm settings");
    const float width = std::min(ImGui::GetContentRegionAvail().x, fs * 40.0f);
    ui::BeginCard("##firm", width);
    if (ui::RecordEditor("##firmfields", p, p.firm, schema<Firm>(), refPicker(), {}, fs * 11.0f)) changed();
    ui::EndCard();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ui::Muted("How profit is figured");
    ui::MutedWrapped("Direct labor is each hour times the person's cost rate. Total cost adds overhead at the rate above. "
                     "Profit is the fee earned so far less total cost, and the net multiplier is fee earned divided by "
                     "direct labor. A project whose multiplier is below the target is shown in amber.");
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (p.staff.empty()) {
        ui::Callout("Next: add the people on your team, with their billing and cost rates.", colorAccent());
        if (ui::PrimaryButton("Go to Team")) go(Screen::Team);
    } else if (p.projects.empty()) {
        ui::Callout("Next: create your first project. It starts with the standard AIA phases.", colorAccent());
        if (ui::PrimaryButton("New project")) createProject();
    }
}

}  // namespace opgui
