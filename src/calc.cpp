#include "openpractice/calc.hpp"

#include "openpractice/util.hpp"

#include <algorithm>
#include <map>

namespace op {

namespace {

std::int64_t divRound(std::int64_t num, std::int64_t den) {
    if (den == 0) return 0;
    std::int64_t q = num / den;
    const std::int64_t r = num % den;
    if (2 * (r < 0 ? -r : r) >= (den < 0 ? -den : den)) q += ((num < 0) != (den < 0)) ? -1 : 1;
    return q;
}

Decimal clampPercent(Decimal d) {
    if (d < Decimal()) return Decimal();
    if (d > Decimal::fromInt(100)) return Decimal::fromInt(100);
    return d;
}

bool isHourly(FeeType t) { return t == FeeType::Hourly || t == FeeType::HourlyNotToExceed; }

std::string pct(Decimal d) { return d.fixed(0) + "%"; }

// Health of a fixed-fee scope: compare how much of the budget is spent with how much of
// the work is done.
Health fixedFeeHealth(Decimal spent, Decimal complete, bool started) {
    if (!started) return Health::NotStarted;
    const Decimal gap = spent - complete;
    if (gap > Decimal::fromInt(15) || (spent >= Decimal::fromInt(100) && complete < Decimal::fromInt(95)))
        return Health::OverBudget;
    if (gap > Decimal::fromInt(5)) return Health::Watch;
    return Health::OnTrack;
}

Money paidOn(const Invoice& i) {
    if (i.status == InvoiceStatus::Paid && i.paid.isZero()) return i.amount;
    return i.paid;
}

}  // namespace

const char* healthLabel(Health h) {
    switch (h) {
        case Health::NotStarted: return "Not started";
        case Health::OnTrack: return "On track";
        case Health::Watch: return "Watch";
        case Health::OverBudget: return "Over budget";
    }
    return "";
}

Money contractFee(const Project& p) {
    if (p.feeType == FeeType::PercentOfConstruction) return percentOf(p.constructionCost, p.feePercent);
    return p.fee;
}

Money laborCost(const Practice& practice, const TimeEntry& t) {
    const Staff* s = practice.findStaff(t.staffId);
    return s ? multiply(s->costRate, t.hours) : Money();
}

Money laborValue(const Practice& practice, const TimeEntry& t) {
    const Staff* s = practice.findStaff(t.staffId);
    return s ? multiply(s->billingRate, t.hours) : Money();
}

ProjectStats projectStats(const Practice& practice, int projectId, Date today) {
    ProjectStats st;
    const Project* project = practice.findProject(projectId);
    if (!project) return st;
    const bool hourly = isHourly(project->feeType);

    st.baseFee = contractFee(*project);
    Decimal changeHours;
    for (const auto& c : practice.changes) {
        if (c.projectId != projectId) continue;
        if (c.status == ChangeStatus::Approved) {
            st.approvedChanges += c.amount;
            changeHours += c.hours;
        } else if (c.status == ChangeStatus::Proposed) {
            st.pendingChanges += c.amount;
        }
    }
    st.totalFee = st.baseFee + st.approvedChanges;

    // Phases, then time charged to each.
    std::map<int, std::size_t> phaseIndex;
    for (const auto& ph : practice.phases) {
        if (ph.projectId != projectId) continue;
        PhaseStats ps;
        ps.phase = &ph;
        ps.fee = percentOf(st.totalFee, ph.feeShare);
        ps.budgetHours = ph.budgetHours;
        phaseIndex[ph.id] = st.phases.size();
        st.phases.push_back(ps);
        st.budgetHours += ph.budgetHours;
    }
    st.budgetHours += changeHours;

    std::map<int, Money> billableValueByPhase;
    Money billableValue;
    for (const auto& t : practice.time) {
        if (t.projectId != projectId) continue;
        const Money cost = laborCost(practice, t);
        const Money value = laborValue(practice, t);
        st.hours += t.hours;
        st.laborCost += cost;
        st.laborValue += value;
        if (t.billable) {
            st.billableHours += t.hours;
            billableValue += value;
        }
        auto it = phaseIndex.find(t.phaseId);
        if (it != phaseIndex.end()) {
            PhaseStats& ps = st.phases[it->second];
            ps.hours += t.hours;
            ps.laborCost += cost;
            ps.laborValue += value;
            if (t.billable) billableValueByPhase[t.phaseId] += value;
        }
    }

    // Earned fee: progress on fixed fees, billable time on hourly work (capped by an NTE).
    for (auto& ps : st.phases) {
        if (hourly) ps.earned = billableValueByPhase[ps.phase->id];
        else ps.earned = percentOf(ps.fee, clampPercent(ps.phase->complete));
        ps.hoursUsed = percentage(ps.hours.raw(), ps.budgetHours.raw());
        const Decimal spent = ps.budgetHours.isZero() ? percentage(ps.laborValue.cents(), ps.fee.cents()) : ps.hoursUsed;
        ps.health = fixedFeeHealth(spent, clampPercent(ps.phase->complete), !ps.hours.isZero() || !ps.phase->complete.isZero());
        if (hourly) ps.health = ps.hours.isZero() ? Health::NotStarted : Health::OnTrack;
    }
    if (hourly) {
        st.earned = billableValue;
        if (project->feeType == FeeType::HourlyNotToExceed && st.totalFee > Money() && st.earned > st.totalFee)
            st.earned = st.totalFee;
    } else {
        for (const auto& ps : st.phases) st.earned += ps.earned;
    }
    st.complete = percentage(st.earned.cents(), st.totalFee.cents());
    st.hoursUsed = percentage(st.hours.raw(), st.budgetHours.raw());
    st.overhead = percentOf(st.laborCost, practice.firm.overheadRate);
    st.totalCost = st.laborCost + st.overhead;
    st.profit = st.earned - st.totalCost;
    if (st.laborCost > Money()) st.multiplier = Decimal::fromRaw(divRound(st.earned.cents() * Decimal::kScale, st.laborCost.cents()));
    if (project->grossArea > Decimal())
        st.feePerSf = Money::fromCents(divRound(st.totalFee.cents() * Decimal::kScale, project->grossArea.raw()));

    for (const auto& ps : st.phases) {
        if (ps.phase->complete < Decimal::fromInt(100)) {
            st.currentPhase = ps.phase;
            break;
        }
    }

    // Overall health.
    const bool started = !st.hours.isZero() || !st.earned.isZero();
    if (!started) {
        st.health = Health::NotStarted;
        st.healthNote = "No time or progress recorded yet.";
    } else if (project->feeType == FeeType::HourlyNotToExceed && st.totalFee > Money()) {
        const Decimal used = percentage(billableValue.cents(), st.totalFee.cents());
        st.health = used >= Decimal::fromInt(100) ? Health::OverBudget
                    : used >= Decimal::fromInt(85) ? Health::Watch
                                                   : Health::OnTrack;
        st.healthNote = pct(used) + " of the not-to-exceed amount used.";
    } else if (hourly) {
        if (st.budgetHours.isZero()) {
            st.health = Health::OnTrack;
            st.healthNote = "Hourly, no hours budget set.";
        } else {
            st.health = st.hoursUsed >= Decimal::fromInt(100) ? Health::OverBudget
                        : st.hoursUsed >= Decimal::fromInt(85) ? Health::Watch
                                                               : Health::OnTrack;
            st.healthNote = pct(st.hoursUsed) + " of budget hours used.";
        }
    } else if (!st.budgetHours.isZero()) {
        st.health = fixedFeeHealth(st.hoursUsed, st.complete, true);
        st.healthNote = pct(st.hoursUsed) + " of budget hours used, " + pct(st.complete) + " complete.";
    } else {
        const Decimal spent = percentage(st.totalCost.cents(), st.totalFee.cents());
        st.health = fixedFeeHealth(spent, st.complete, true);
        st.healthNote = pct(spent) + " of the fee spent (at cost with overhead), " + pct(st.complete) + " complete.";
    }

    // Billing.
    for (const auto& i : practice.invoices) {
        if (i.projectId != projectId || i.status == InvoiceStatus::Draft) continue;
        st.billed += i.amount;
        st.collected += paidOn(i);
    }
    st.receivable = st.billed - st.collected;
    st.unbilled = st.earned - st.billed;

    // Work items.
    for (const auto& t : practice.tasks) {
        if (t.projectId != projectId) continue;
        ++st.tasksTotal;
        if (t.status != TaskStatus::Done) ++st.tasksOpen;
        if (isOverdue(t, today)) ++st.tasksOverdue;
    }
    for (const auto& r : practice.rfis) {
        if (r.projectId != projectId || r.status != RfiStatus::Open) continue;
        ++st.rfisOpen;
        if (r.due && *r.due < today) ++st.rfisOverdue;
    }
    for (const auto& s : practice.submittals) {
        if (s.projectId != projectId) continue;
        if (s.status != SubmittalStatus::Pending && s.status != SubmittalStatus::UnderReview) continue;
        ++st.submittalsOpen;
        if (s.due && *s.due < today) ++st.submittalsOverdue;
    }
    for (const auto& s : practice.sheets) {
        if (s.projectId != projectId) continue;
        ++st.sheetsTotal;
        if (s.status == SheetStatus::Issued) ++st.sheetsIssued;
    }
    return st;
}

FirmStats firmStats(const Practice& practice, Date today) {
    FirmStats fs;
    for (const auto& p : practice.projects) {
        if (p.status == ProjectStatus::Proposal) ++fs.proposals;
        if (p.status != ProjectStatus::Active) continue;
        ++fs.activeProjects;
        const ProjectStats st = projectStats(practice, p.id, today);
        fs.activeFees += st.totalFee;
        fs.earned += st.earned;
        if (st.totalFee > st.earned) fs.backlog += st.totalFee - st.earned;
        if (st.unbilled > Money()) fs.unbilled += st.unbilled;
    }
    for (const auto& i : practice.invoices) {
        if (i.status != InvoiceStatus::Sent) continue;
        const Money open = i.amount - paidOn(i);
        fs.receivable += open;
        if (i.due && *i.due < today) fs.overdueReceivable += open;
    }
    const Date monday = weekStart(today);
    for (const auto& t : practice.time) {
        if (!t.date || *t.date < monday || *t.date > monday.addDays(6)) continue;
        fs.hoursThisWeek += t.hours;
        if (t.billable) fs.billableThisWeek += t.hours;
    }
    return fs;
}

// ------------------------------------------------------------------ time

Date weekStart(Date d) {
    const int weekday = ((d.serial() + 3) % 7 + 7) % 7;  // 0 = Monday; 1970-01-01 was a Thursday
    return d.addDays(-weekday);
}

Decimal hoursBetween(const Practice& practice, int staffId, Date from, Date to) {
    Decimal h;
    for (const auto& t : practice.time) {
        if (staffId != 0 && t.staffId != staffId) continue;
        if (!t.date || *t.date < from || *t.date > to) continue;
        h += t.hours;
    }
    return h;
}

std::vector<StaffLoad> staffLoad(const Practice& practice, Date from, Date to) {
    std::vector<StaffLoad> out;
    const int days = std::max(0, to - from + 1);
    for (const auto& s : practice.staff) {
        StaffLoad l;
        l.staff = &s;
        l.capacity = Decimal::fromRaw(divRound(s.weeklyCapacity.raw() * days, 7));
        out.push_back(l);
    }
    auto find = [&](int id) -> StaffLoad* {
        for (auto& l : out) {
            if (l.staff->id == id) return &l;
        }
        return nullptr;
    };
    for (const auto& t : practice.time) {
        if (!t.date || *t.date < from || *t.date > to) continue;
        if (StaffLoad* l = find(t.staffId)) {
            l->hours += t.hours;
            if (t.billable) l->billable += t.hours;
        }
    }
    for (const auto& t : practice.tasks) {
        if (t.status == TaskStatus::Done) continue;
        if (StaffLoad* l = find(t.assigneeId)) {
            l->openTaskHours += t.estimateHours;
            ++l->openTasks;
        }
    }
    for (auto& l : out) l.utilization = percentage(l.billable.raw(), l.capacity.raw());
    std::stable_partition(out.begin(), out.end(), [](const StaffLoad& l) { return l.staff->active; });
    return out;
}

// --------------------------------------------------------------- schedule

bool isOverdue(const Task& t, Date today) { return t.status != TaskStatus::Done && t.due && *t.due < today; }

bool startsBeforePredecessor(const Practice& practice, const Task& t) {
    const Task* pred = practice.findTask(t.predecessorId);
    return pred && pred->id != t.id && t.start && pred->due && *t.start < *pred->due;
}

std::vector<int> tasksInDependencyCycles(const Practice& practice, int projectId) {
    std::vector<int> out;
    const std::size_t limit = practice.tasks.size() + 1;
    for (const auto& t : practice.tasks) {
        if (projectId != 0 && t.projectId != projectId) continue;
        const Task* p = practice.findTask(t.predecessorId);
        for (std::size_t steps = 0; p && steps < limit; ++steps) {
            if (p->id == t.id) {
                out.push_back(t.id);
                break;
            }
            p = practice.findTask(p->predecessorId);
        }
    }
    return out;
}

// ------------------------------------------------------------------ review

std::vector<Issue> reviewPractice(const Practice& practice, Date today) {
    std::vector<Issue> issues;
    auto add = [&](Severity sev, int projectId, Area area, std::string message) {
        issues.push_back(Issue{sev, projectId, area, std::move(message)});
    };
    auto plural = [](int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); };

    for (const auto& p : practice.projects) {
        if (p.status != ProjectStatus::Active) continue;
        const std::string name = practice.refName(Ref::Project, p.id);
        const ProjectStats st = projectStats(practice, p.id, today);

        if (st.phases.empty()) {
            add(Severity::Warning, p.id, Area::Phases, name + " has no phases, so no fee can be earned. Add the standard phases.");
        } else if (!isHourly(p.feeType)) {
            Decimal shares;
            for (const auto& ps : st.phases) shares += ps.phase->feeShare;
            if (shares != Decimal::fromInt(100))
                add(Severity::Error, p.id, Area::Phases,
                    name + ": phase fee shares add to " + shares.str() + "%, not 100%.");
        }
        if (st.baseFee.isZero() && p.feeType != FeeType::Hourly)
            add(Severity::Warning, p.id, Area::Project, name + " has no fee set.");
        if (st.health == Health::OverBudget)
            add(Severity::Error, p.id, Area::Phases, name + " is over budget: " + st.healthNote);
        else if (st.health == Health::Watch)
            add(Severity::Warning, p.id, Area::Phases, name + " needs watching: " + st.healthNote);
        for (const auto& ps : st.phases) {
            if (ps.health == Health::OverBudget && !isHourly(p.feeType))
                add(Severity::Warning, p.id, Area::Phases,
                    name + " " + ps.phase->code + ": " + ps.hours.fixed(1) + " of " + ps.budgetHours.fixed(0) +
                        " budget hours used at " + ps.phase->complete.fixed(0) + "% complete.");
        }
        if (st.tasksOverdue > 0)
            add(Severity::Warning, p.id, Area::Tasks, name + ": " + plural(st.tasksOverdue, "task is", "tasks are") + " overdue.");
        if (st.rfisOverdue > 0)
            add(Severity::Error, p.id, Area::Rfis,
                name + ": " + plural(st.rfisOverdue, "RFI response is", "RFI responses are") + " overdue.");
        if (st.submittalsOverdue > 0)
            add(Severity::Error, p.id, Area::Submittals,
                name + ": " + plural(st.submittalsOverdue, "submittal review is", "submittal reviews are") + " overdue.");
        if (st.totalFee > Money() && st.billed > st.totalFee)
            add(Severity::Warning, p.id, Area::Invoices, name + ": invoiced more than the total fee.");
        if (st.unbilled > Money() && st.unbilled.cents() >= 100000)
            add(Severity::Note, p.id, Area::Invoices, name + ": $" + st.unbilled.formatted() + " earned but not yet invoiced.");
        if (p.end && *p.end < today)
            add(Severity::Warning, p.id, Area::Project, name + " is past its target completion date.");

        const auto cycles = tasksInDependencyCycles(practice, p.id);
        if (!cycles.empty())
            add(Severity::Error, p.id, Area::Tasks,
                name + ": " + plural(static_cast<int>(cycles.size()), "task depends", "tasks depend") +
                    " on itself through its predecessors.");
        int conflicts = 0;
        for (const auto& t : practice.tasks) {
            if (t.projectId == p.id && t.status != TaskStatus::Done && startsBeforePredecessor(practice, t)) ++conflicts;
        }
        if (conflicts > 0)
            add(Severity::Warning, p.id, Area::Tasks,
                name + ": " + plural(conflicts, "task starts", "tasks start") + " before its predecessor is due.");
    }

    for (const auto& i : practice.invoices) {
        if (i.status == InvoiceStatus::Sent && i.due && *i.due < today) {
            const int days = today - *i.due;
            add(Severity::Warning, i.projectId, Area::Invoices,
                "Invoice " + i.number + " ($" + (i.amount - paidOn(i)).formatted() + ") is " + std::to_string(days) +
                    " days past due.");
        }
    }

    for (const auto& t : practice.time) {
        if (t.phaseId == 0) continue;
        const Phase* ph = practice.findPhase(t.phaseId);
        if (ph && ph->projectId != t.projectId) {
            add(Severity::Error, t.projectId, Area::Time,
                "A time entry on " + (t.date ? t.date->str() : std::string("an undated day")) +
                    " is charged to a phase of a different project.");
        }
    }

    std::map<int, bool> warnedRates;
    for (const auto& t : practice.time) {
        const Staff* s = practice.findStaff(t.staffId);
        if (!s || warnedRates[s->id]) continue;
        if (s->costRate.isZero() || s->billingRate.isZero()) {
            warnedRates[s->id] = true;
            add(Severity::Warning, 0, Area::Team, s->name + " has time recorded but no " +
                (s->costRate.isZero() ? "cost" : "billing") + " rate, so project costs are understated.");
        }
    }

    std::stable_sort(issues.begin(), issues.end(),
                     [](const Issue& a, const Issue& b) { return static_cast<int>(a.severity) < static_cast<int>(b.severity); });
    return issues;
}

}  // namespace op
