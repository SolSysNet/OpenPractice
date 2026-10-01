#pragma once

// Everything OpenPractice computes from what was entered: earned fee, labor cost, profit and
// multiplier for each project and phase, budget health, billing and receivables, staff
// utilization, and a review of things that need attention.
//
// Definitions (the standard ones for A/E practice management):
//   earned fee       = sum over phases of (contract fee x phase share x phase % complete)
//   direct labor     = sum of hours x each person's cost rate
//   total cost       = direct labor x (1 + firm overhead rate)
//   profit           = earned fee - total cost
//   net multiplier   = earned fee / direct labor
//   hours used       = actual hours / budget hours
//   receivable       = invoiced - collected;  unbilled = earned - invoiced

#include "openpractice/model.hpp"

#include <string>
#include <vector>

namespace op {

enum class Health { NotStarted, OnTrack, Watch, OverBudget };
const char* healthLabel(Health h);

struct PhaseStats {
    const Phase* phase = nullptr;
    Money fee;            // contract fee x share
    Money earned;         // fee x % complete
    Decimal budgetHours;
    Decimal hours;        // actual
    Money laborCost;      // at cost rates
    Money laborValue;     // at billing rates
    Decimal hoursUsed;    // % of budget hours spent
    Health health = Health::NotStarted;
};

struct ProjectStats {
    Money baseFee;          // per the contract terms (see contractFee)
    Money approvedChanges;
    Money pendingChanges;
    Money totalFee;         // base + approved changes
    Money earned;
    Decimal complete;       // earned as a % of the total fee
    Decimal budgetHours;    // phase budgets + approved change hours
    Decimal hours;
    Decimal billableHours;
    Decimal hoursUsed;      // % of budget hours spent
    Money laborCost;
    Money laborValue;
    Money overhead;
    Money totalCost;
    Money profit;
    Decimal multiplier;     // earned / direct labor, e.g. 2.85
    Money billed;
    Money collected;
    Money receivable;
    Money unbilled;         // negative when billed ahead of progress
    Money feePerSf;         // total fee / gross area
    int tasksTotal = 0;
    int tasksOpen = 0;
    int tasksOverdue = 0;
    int rfisOpen = 0;
    int rfisOverdue = 0;
    int submittalsOpen = 0;
    int submittalsOverdue = 0;
    int sheetsTotal = 0;
    int sheetsIssued = 0;
    const Phase* currentPhase = nullptr;  // first phase not yet 100% complete
    Health health = Health::NotStarted;
    std::string healthNote;
    std::vector<PhaseStats> phases;  // in the order they were entered
};

// The base fee from the project's contract terms: the fee, or construction cost x fee % for
// a percentage-of-construction fee.
Money contractFee(const Project& p);
Money laborCost(const Practice& practice, const TimeEntry& t);
Money laborValue(const Practice& practice, const TimeEntry& t);

ProjectStats projectStats(const Practice& practice, int projectId, Date today);

// Firm-wide totals over active projects (and receivables from all projects).
struct FirmStats {
    int activeProjects = 0;
    int proposals = 0;
    Money activeFees;       // total fee of active projects
    Money earned;           // earned to date on active projects
    Money backlog;          // fee not yet earned on active projects
    Money unbilled;         // earned but not invoiced (active projects)
    Money receivable;       // invoiced, not yet collected (all projects)
    Money overdueReceivable;
    Decimal hoursThisWeek;
    Decimal billableThisWeek;
};
FirmStats firmStats(const Practice& practice, Date today);

// ---- time

Date weekStart(Date d);  // the Monday on or before d
Decimal hoursBetween(const Practice& practice, int staffId, Date from, Date to);  // inclusive, 0 = everyone

struct StaffLoad {
    const Staff* staff = nullptr;
    Decimal hours;
    Decimal billable;
    Decimal capacity;       // weekly capacity prorated over the range
    Decimal utilization;    // billable hours as a % of capacity
    Decimal openTaskHours;  // estimates of assigned tasks not yet done
    int openTasks = 0;
};
// Hours between from and to (inclusive) for every staff member, active ones first.
std::vector<StaffLoad> staffLoad(const Practice& practice, Date from, Date to);

// ---- review

enum class Severity { Error, Warning, Note };

// Where in the app an issue can be fixed.
enum class Area { Project, Phases, Tasks, Drawings, Rfis, Submittals, Changes, Invoices, Time, Team, Firm };

struct Issue {
    Severity severity = Severity::Note;
    int projectId = 0;  // 0 for firm-wide
    Area area = Area::Project;
    std::string message;
};

// Problems and reminders across the practice, most severe first.
std::vector<Issue> reviewPractice(const Practice& practice, Date today);

// ---- schedule

// True if the task isn't done and its due date has passed.
bool isOverdue(const Task& t, Date today);
// A task's start is before its predecessor's due date.
bool startsBeforePredecessor(const Practice& practice, const Task& t);
// Tasks whose predecessors form a loop (A after B after A), which can never be scheduled.
std::vector<int> tasksInDependencyCycles(const Practice& practice, int projectId);

}  // namespace op
