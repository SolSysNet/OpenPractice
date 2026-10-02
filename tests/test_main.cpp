// Self-contained test runner (no external framework needed).
//
// Expected values are worked by hand from the definitions in calc.hpp.

#include "openpractice/calc.hpp"
#include "openpractice/cli.hpp"
#include "openpractice/diff.hpp"
#include "openpractice/format.hpp"
#include "openpractice/model.hpp"
#include "openpractice/report.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace op;
namespace fs = std::filesystem;

namespace {

int g_checks = 0;
int g_failures = 0;

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

#define TEST(name)                                 \
    void name();                                   \
    const Registrar registrar_##name(#name, name); \
    void name()

#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
        }                                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                                                  \
    do {                                                                                                \
        ++g_checks;                                                                                     \
        const auto va = (a);                                                                            \
        const auto vb = (b);                                                                            \
        if (!(va == vb)) {                                                                              \
            ++g_failures;                                                                               \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #a " == " #b "  (" << va \
                      << " vs " << vb << ")\n";                                                         \
        }                                                                                               \
    } while (0)

#define CHECK_THROWS(expr)                                                                   \
    do {                                                                                     \
        ++g_checks;                                                                          \
        bool threw = false;                                                                  \
        try {                                                                                \
            expr;                                                                            \
        } catch (const op::Error&) {                                                         \
            threw = true;                                                                    \
        }                                                                                    \
        if (!threw) {                                                                        \
            ++g_failures;                                                                    \
            std::cerr << __FILE__ << ":" << __LINE__ << ": expected op::Error from " #expr "\n"; \
        }                                                                                    \
    } while (0)

Money usdOf(long long dollars) { return Money::fromCents(dollars * 100); }
Date day(int y, unsigned m, unsigned d) { return Date::fromYMD(y, m, d); }

// A scratch directory removed at the end of the test.
struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("openpractice-test-" + std::to_string(std::rand()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string file(const char* name) const { return (path / name).u8string(); }
};

// One fixed-fee project, two people, two phases; numbers chosen to check by hand.
Practice smallPractice() {
    Practice p;
    p.firm.overheadRate = Decimal::fromInt(150);

    Staff a;
    a.id = 1;
    a.name = "Ana";
    a.initials = "AR";
    a.billingRate = usdOf(150);
    a.costRate = usdOf(50);
    Staff b;
    b.id = 2;
    b.name = "Ben";
    b.initials = "BW";
    b.billingRate = usdOf(100);
    b.costRate = usdOf(30);
    p.staff = {a, b};

    Project pr;
    pr.id = 1;
    pr.number = "26-01";
    pr.name = "Pavilion";
    pr.fee = usdOf(100000);
    pr.grossArea = Decimal::fromInt(5000);
    p.projects.push_back(pr);

    Phase sd;
    sd.id = 1;
    sd.projectId = 1;
    sd.code = "SD";
    sd.feeShare = Decimal::fromInt(40);
    sd.budgetHours = Decimal::fromInt(100);
    sd.complete = Decimal::fromInt(100);
    Phase dd;
    dd.id = 2;
    dd.projectId = 1;
    dd.code = "DD";
    dd.feeShare = Decimal::fromInt(60);
    dd.budgetHours = Decimal::fromInt(200);
    dd.complete = Decimal::fromInt(50);
    p.phases = {sd, dd};

    auto time = [&](int staff, int phase, int hours) {
        TimeEntry t;
        t.id = Practice::nextId(p.time);
        t.date = day(2026, 3, 2);
        t.staffId = staff;
        t.projectId = 1;
        t.phaseId = phase;
        t.hours = Decimal::fromInt(hours);
        p.time.push_back(t);
    };
    time(1, 1, 60);   // SD: 60 h Ana
    time(2, 1, 40);   // SD: 40 h Ben
    time(1, 2, 50);   // DD: 50 h Ana
    time(2, 2, 70);   // DD: 70 h Ben
    return p;
}

}  // namespace

// ------------------------------------------------------------------ numbers

TEST(decimal_arithmetic_and_format) {
    CHECK_EQ(Decimal::parse("7.5")->str(), std::string("7.5"));
    CHECK_EQ((Decimal::fromInt(3) + *Decimal::parse("1.25")).str(), std::string("4.25"));
    CHECK_EQ(Decimal::parse("12.345")->fixed(1), std::string("12.3"));
    CHECK_EQ(Decimal::parse("12.35")->fixed(1), std::string("12.4"));
    CHECK_EQ(Decimal::parse("1234.5")->fixed(0), std::string("1,235"));
    CHECK_EQ(Decimal::parse("-2.5")->fixed(0), std::string("-3"));
    CHECK_EQ(Decimal().fixed(2), std::string("0.00"));
    CHECK_EQ(percentage(1, 3).fixed(2), std::string("33.33"));
    CHECK_EQ(percentage(5, 0), Decimal());
    CHECK_EQ(product(*Decimal::parse("1.5"), *Decimal::parse("2.5")).str(), std::string("3.75"));
    CHECK_EQ(multiply(usdOf(150), *Decimal::parse("2.5")), usdOf(375));
}

TEST(money_display) {
    CHECK_EQ(usd(Money::fromCents(-123456)), std::string("-$1,234.56"));
    CHECK_EQ(usdShort(usdOf(1250000)), std::string("$1.3M"));
    CHECK_EQ(usdShort(usdOf(350400)), std::string("$350K"));
    CHECK_EQ(usdShort(usdOf(9999)), std::string("$9,999"));
    CHECK_EQ(usdShort(Money::fromCents(-4250)), std::string("-$43"));
}

TEST(week_start_is_monday) {
    CHECK_EQ(weekStart(day(2026, 9, 30)), day(2026, 9, 28));  // Wednesday
    CHECK_EQ(weekStart(day(2026, 9, 28)), day(2026, 9, 28));  // Monday
    CHECK_EQ(weekStart(day(2026, 10, 4)), day(2026, 9, 28));  // Sunday
}

// ------------------------------------------------------------------ model

TEST(sample_round_trips_through_text) {
    const Practice p = samplePractice(day(2026, 9, 30));
    const std::string text = p.serialize();
    const Practice q = Practice::parse(text);
    CHECK_EQ(q.serialize(), text);
    CHECK_EQ(q.projects.size(), p.projects.size());
    CHECK_EQ(q.time.size(), p.time.size());
    CHECK(q.time.size() > 500);
    CHECK_EQ(q.firm.name, p.firm.name);
}

TEST(multiline_text_survives_escaping) {
    Practice p;
    Client c;
    c.id = 1;
    c.name = "Tab\tand \\backslash";
    c.notes = "line one\nline two\r\n\tindented";
    p.clients.push_back(c);
    const Practice q = Practice::parse(p.serialize());
    CHECK_EQ(q.clients.at(0).name, c.name);
    CHECK_EQ(q.clients.at(0).notes, c.notes);
}

TEST(parse_rejects_bad_files) {
    CHECK_THROWS(Practice::parse(""));
    CHECK_THROWS(Practice::parse("OPENTAX 1\n"));
    CHECK_THROWS(Practice::parse("OPENPRACTICE 1\nWIDGET\tid=1\n"));
    CHECK_THROWS(Practice::parse("OPENPRACTICE 1\nSTAFF\tid=1\tshoe_size=9\n"));
    CHECK_THROWS(Practice::parse("OPENPRACTICE 1\nSTAFF\tid=1\trole=astronaut\n"));
    CHECK_THROWS(Practice::parse("OPENPRACTICE 1\nSTAFF\tname=No id\n"));
    CHECK_THROWS(Practice::parse("OPENPRACTICE 1\nSTAFF\tid=1\nSTAFF\tid=1\n"));
    CHECK_THROWS(Practice::parse("OPENPRACTICE 1\nTIME\tid=1\thours=lots\n"));
    CHECK_THROWS(Practice::parse("OPENPRACTICE 1\nTASK\tid=1\tdue=31/31/2026\n"));
    // Comments, blank lines, CRLF and a BOM are fine.
    const Practice ok = Practice::parse("\xEF\xBB\xBFOPENPRACTICE 1\r\n# note\r\n\r\nSTAFF\tid=4\tname=Kim\r\n");
    CHECK_EQ(ok.staff.size(), std::size_t(1));
    CHECK_EQ(ok.staff[0].name, std::string("Kim"));
    CHECK(ok.staff[0].active);  // defaults are kept for fields not written
}

TEST(choices_accept_keys_and_labels) {
    Task t;
    setField(t, *findField<Task>("status"), "in_progress");
    CHECK(t.status == TaskStatus::InProgress);
    setField(t, *findField<Task>("status"), "Blocked");
    CHECK(t.status == TaskStatus::Blocked);
    CHECK_EQ(getField(t, *findField<Task>("status")), std::string("blocked"));
}

TEST(standard_phases_add_to_100) {
    Practice p;
    p.addStandardPhases(7);
    CHECK_EQ(p.phases.size(), std::size_t(5));
    Decimal total;
    for (const auto& ph : p.phases) total += ph.feeShare;
    CHECK_EQ(total, Decimal::fromInt(100));
    CHECK_EQ(p.phases[2].code, std::string("CD"));
    CHECK_EQ(p.phases[4].id, 5);
}

TEST(references_are_found_cleared_and_cascaded) {
    Practice p = smallPractice();
    Task t;
    t.id = 1;
    t.projectId = 1;
    t.phaseId = 2;
    t.assigneeId = 1;
    p.tasks.push_back(t);

    auto refs = p.referencesTo(Ref::Staff, 1);
    CHECK_EQ(refs.size(), std::size_t(2));  // in file order: tasks, then time
    CHECK_EQ(refs[0], std::string("1 task"));
    CHECK_EQ(refs[1], std::string("2 time entries"));
    CHECK(p.referencesTo(Ref::Project, 1).empty());  // owned records don't block a project

    p.clearReferences(Ref::Phase, 2);
    CHECK_EQ(p.tasks[0].phaseId, 0);
    CHECK_EQ(p.time[2].phaseId, 0);
    CHECK_EQ(p.time[0].phaseId, 1);

    CHECK_EQ(p.refName(Ref::Project, 1), std::string("26-01 Pavilion"));
    CHECK_EQ(p.refName(Ref::Staff, 9), std::string("(missing #9)"));
    CHECK_EQ(p.refName(Ref::Staff, 0), std::string());

    p.removeProject(1);
    CHECK(p.projects.empty());
    CHECK(p.phases.empty());
    CHECK(p.time.empty());
    CHECK(p.tasks.empty());
    CHECK_EQ(p.staff.size(), std::size_t(2));
}

TEST(numbering) {
    Practice p = smallPractice();
    CHECK_EQ(p.nextRfiNumber(1), 1);
    Rfi r;
    r.id = 1;
    r.projectId = 1;
    r.number = 4;
    p.rfis.push_back(r);
    CHECK_EQ(p.nextRfiNumber(1), 5);
    CHECK_EQ(p.nextRfiNumber(2), 1);
    CHECK_EQ(p.nextInvoiceNumber(1), std::string("26-01-01"));
    CHECK_EQ(Practice::nextId(p.time), 5);
}

// ------------------------------------------------------------------ calc

TEST(fixed_fee_project_stats) {
    const Practice p = smallPractice();
    const ProjectStats st = projectStats(p, 1, day(2026, 3, 10));
    // Earned: SD 40,000 x 100% + DD 60,000 x 50% = 70,000.
    CHECK_EQ(st.totalFee, usdOf(100000));
    CHECK_EQ(st.earned, usdOf(70000));
    CHECK_EQ(st.complete, Decimal::fromInt(70));
    // Labor: Ana 110 h x 50 + Ben 110 h x 30 = 5,500 + 3,300 = 8,800.
    CHECK_EQ(st.hours, Decimal::fromInt(220));
    CHECK_EQ(st.laborCost, usdOf(8800));
    CHECK_EQ(st.laborValue, usdOf(110 * 150 + 110 * 100));
    // Overhead 150% -> total cost 22,000; profit 48,000; multiplier 70,000 / 8,800.
    CHECK_EQ(st.overhead, usdOf(13200));
    CHECK_EQ(st.totalCost, usdOf(22000));
    CHECK_EQ(st.profit, usdOf(48000));
    CHECK_EQ(st.multiplier.fixed(4), std::string("7.9545"));
    CHECK_EQ(st.feePerSf, usdOf(20));
    // Hours: 220 of 300 budget = 73.33%, vs 70% complete -> on track (gap under 5 points).
    CHECK_EQ(st.hoursUsed.fixed(2), std::string("73.33"));
    CHECK(st.health == Health::OnTrack);
    CHECK(st.currentPhase && st.currentPhase->code == "DD");
    // DD: 120 of 200 hours (60%) at 50% complete -> watch.
    CHECK_EQ(st.phases.at(1).hours, Decimal::fromInt(120));
    CHECK(st.phases.at(1).health == Health::Watch);
    CHECK_EQ(st.phases.at(0).earned, usdOf(40000));
}

TEST(change_orders_and_billing) {
    Practice p = smallPractice();
    ChangeOrder approved;
    approved.id = 1;
    approved.projectId = 1;
    approved.amount = usdOf(10000);
    approved.hours = Decimal::fromInt(50);
    approved.status = ChangeStatus::Approved;
    ChangeOrder proposed = approved;
    proposed.id = 2;
    proposed.status = ChangeStatus::Proposed;
    proposed.amount = usdOf(2500);
    p.changes = {approved, proposed};

    Invoice paid;
    paid.id = 1;
    paid.projectId = 1;
    paid.amount = usdOf(30000);
    paid.status = InvoiceStatus::Paid;  // paid in full with no amount entered
    Invoice sent;
    sent.id = 2;
    sent.projectId = 1;
    sent.amount = usdOf(20000);
    sent.paid = usdOf(5000);
    sent.status = InvoiceStatus::Sent;
    sent.due = day(2026, 3, 1);
    Invoice draft = sent;
    draft.id = 3;
    draft.status = InvoiceStatus::Draft;
    p.invoices = {paid, sent, draft};

    const ProjectStats st = projectStats(p, 1, day(2026, 3, 10));
    CHECK_EQ(st.totalFee, usdOf(110000));
    CHECK_EQ(st.pendingChanges, usdOf(2500));
    CHECK_EQ(st.budgetHours, Decimal::fromInt(350));
    CHECK_EQ(st.earned, usdOf(77000));  // 44,000 + 66,000 x 50%
    CHECK_EQ(st.billed, usdOf(50000));  // drafts don't count
    CHECK_EQ(st.collected, usdOf(35000));
    CHECK_EQ(st.receivable, usdOf(15000));
    CHECK_EQ(st.unbilled, usdOf(27000));

    p.projects[0].status = ProjectStatus::Active;
    const FirmStats fs = firmStats(p, day(2026, 3, 10));
    CHECK_EQ(fs.activeProjects, 1);
    CHECK_EQ(fs.receivable, usdOf(15000));
    CHECK_EQ(fs.overdueReceivable, usdOf(15000));
    CHECK_EQ(fs.backlog, usdOf(33000));
}

TEST(percent_of_construction_fee) {
    Project pr;
    pr.feeType = FeeType::PercentOfConstruction;
    pr.constructionCost = usdOf(28000000);
    pr.feePercent = *Decimal::parse("6.5");
    pr.fee = usdOf(1);  // ignored
    CHECK_EQ(contractFee(pr), usdOf(1820000));
}

TEST(hourly_not_to_exceed_caps_earned_fee) {
    Practice p = smallPractice();
    p.projects[0].feeType = FeeType::HourlyNotToExceed;
    p.projects[0].fee = usdOf(20000);
    p.time[3].billable = false;  // Ben's 70 DD hours
    const ProjectStats st = projectStats(p, 1, day(2026, 3, 10));
    // Billable value: 110 h x 150 + 40 h x 100 = 20,500, capped at 20,000.
    CHECK_EQ(st.earned, usdOf(20000));
    CHECK_EQ(st.billableHours, Decimal::fromInt(150));
    CHECK(st.health == Health::OverBudget);  // 102.5% of the cap
}

TEST(over_budget_when_spending_outruns_progress) {
    Practice p = smallPractice();
    p.phases[1].complete = Decimal::fromInt(20);  // DD: 60% of hours at 20% complete
    const ProjectStats st = projectStats(p, 1, day(2026, 3, 10));
    CHECK(st.phases[1].health == Health::OverBudget);
    // Project: 73% of hours, 52% complete.
    CHECK(st.health == Health::OverBudget);
    const auto issues = reviewPractice([&] {
        Practice q = p;
        q.projects[0].status = ProjectStatus::Active;
        return q;
    }(), day(2026, 3, 10));
    CHECK(!issues.empty());
    CHECK(issues[0].severity == Severity::Error);
    CHECK(issues[0].message.find("over budget") != std::string::npos);
}

TEST(review_finds_schedule_and_setup_problems) {
    Practice p = smallPractice();
    p.phases[1].feeShare = Decimal::fromInt(50);  // shares add to 90%
    Task a;
    a.id = 1;
    a.projectId = 1;
    a.name = "A";
    a.predecessorId = 2;
    Task b = a;
    b.id = 2;
    b.name = "B";
    b.predecessorId = 1;
    Task late = a;
    late.id = 3;
    late.predecessorId = 0;
    late.due = day(2026, 3, 1);
    p.tasks = {a, b, late};
    Rfi r;
    r.id = 1;
    r.projectId = 1;
    r.due = day(2026, 3, 9);
    p.rfis.push_back(r);

    auto has = [](const std::vector<Issue>& issues, Severity sev, const char* text) {
        for (const auto& i : issues) {
            if (i.severity == sev && i.message.find(text) != std::string::npos) return true;
        }
        return false;
    };
    // Proposals aren't reviewed.
    p.projects[0].status = ProjectStatus::Proposal;
    CHECK(reviewPractice(p, day(2026, 3, 10)).empty());

    p.projects[0].status = ProjectStatus::Active;
    const auto issues = reviewPractice(p, day(2026, 3, 10));
    CHECK(has(issues, Severity::Error, "add to 90%"));
    CHECK(has(issues, Severity::Error, "depend on itself"));
    CHECK(has(issues, Severity::Warning, "1 task is overdue"));
    CHECK(has(issues, Severity::Error, "1 RFI response is overdue"));
    CHECK_EQ(tasksInDependencyCycles(p, 1).size(), std::size_t(2));
}

TEST(predecessor_conflicts) {
    Practice p;
    Task a;
    a.id = 1;
    a.due = day(2026, 5, 10);
    Task b;
    b.id = 2;
    b.predecessorId = 1;
    b.start = day(2026, 5, 8);
    p.tasks = {a, b};
    CHECK(startsBeforePredecessor(p, p.tasks[1]));
    p.tasks[1].start = day(2026, 5, 10);
    CHECK(!startsBeforePredecessor(p, p.tasks[1]));
}

TEST(staff_utilization) {
    Practice p = smallPractice();
    p.staff[1].active = false;
    Task t;
    t.id = 1;
    t.assigneeId = 1;
    t.estimateHours = Decimal::fromInt(12);
    p.tasks.push_back(t);
    // One week: Ana 110 billable hours of 40 capacity.
    const auto load = staffLoad(p, day(2026, 3, 2), day(2026, 3, 8));
    CHECK_EQ(load.size(), std::size_t(2));
    CHECK_EQ(load[0].staff->name, std::string("Ana"));
    CHECK_EQ(load[0].capacity, Decimal::fromInt(40));
    CHECK_EQ(load[0].utilization.fixed(1), std::string("275.0"));
    CHECK_EQ(load[0].openTasks, 1);
    CHECK_EQ(load[0].openTaskHours, Decimal::fromInt(12));
    CHECK_EQ(hoursBetween(p, 0, day(2026, 3, 2), day(2026, 3, 2)), Decimal::fromInt(220));
    CHECK_EQ(hoursBetween(p, 2, day(2026, 3, 3), day(2026, 3, 9)), Decimal());
}

TEST(sample_practice_is_consistent) {
    const Date today = day(2026, 9, 30);
    const Practice p = samplePractice(today);
    // Every reference resolves.
    forEachList(p, [&](const auto& s, const auto& items) {
        for (const auto& item : items) {
            for (const auto& f : s.fields) {
                if (f.ref == Ref::None) continue;
                const std::string v = displayValue(p, item, f);
                CHECK(v.find("missing") == std::string::npos);
            }
        }
    });
    for (const auto& pr : p.projects) {
        const ProjectStats st = projectStats(p, pr.id, today);
        if (pr.status == ProjectStatus::Active) {
            CHECK(st.earned > Money());
            CHECK(st.profit > Money());
        }
    }
    for (const auto& i : reviewPractice(p, today)) CHECK(i.message.find("phase of a different project") == std::string::npos);
}

// ------------------------------------------------------------------ output

TEST(csv_export) {
    Practice p = smallPractice();
    p.staff[0].name = "Ruiz, Ana \"AR\"";
    p.staff[1].name = "=HYPERLINK(\"x\")";
    const std::string csv = exportCsv(p, "staff");
    CHECK(csv.find("\"Ruiz, Ana \"\"AR\"\"\"") != std::string::npos);
    CHECK(csv.find("'=HYPERLINK") != std::string::npos);
    CHECK(csv.find(",150.00,") != std::string::npos);  // plain amounts for spreadsheets

    const std::string time = exportCsv(p, "time", 1);
    CHECK(time.find("26-01 Pavilion") != std::string::npos);
    CHECK_EQ(std::count(time.begin(), time.end(), '\n'), 5);  // header + 4 entries
    const std::string none = exportCsv(p, "time", 99);
    CHECK_EQ(std::count(none.begin(), none.end(), '\n'), 1);  // header only
    CHECK_THROWS(exportCsv(p, "widgets"));
    CHECK_EQ(listTitle("rfis"), std::string("RFIs"));
    CHECK_EQ(listNames().size(), std::size_t(11));
}

TEST(pdf_reports) {
    const Date today = day(2026, 9, 30);
    const Practice p = samplePractice(today);
    for (const auto& pr : p.projects) {
        const std::string pdf = projectReportPdf(p, pr.id, today);
        CHECK(pdf.rfind("%PDF-1.4", 0) == 0);
        CHECK(pdf.find("%%EOF") != std::string::npos);
    }
    const std::string portfolio = portfolioReportPdf(p, today);
    CHECK(portfolio.rfind("%PDF-1.4", 0) == 0);
    CHECK(portfolio == portfolioReportPdf(p, today));  // deterministic
    CHECK_THROWS(projectReportPdf(p, 999, today));
}

TEST(save_and_load_files) {
    TempDir dir;
    const std::string path = dir.file("firm.opp");
    Practice p = smallPractice();
    p.save(path);
    p.firm.name = "Second save";
    p.save(path);
    CHECK(fs::exists(fs::u8path(path + ".bak")));
    CHECK(!fs::exists(fs::u8path(path + ".tmp")));
    CHECK_EQ(Practice::load(path).firm.name, std::string("Second save"));
    CHECK_EQ(Practice::load(path + ".bak").firm.name, std::string());
    CHECK_THROWS(Practice::load(dir.file("missing.opp")));
}

TEST(command_line) {
    TempDir dir;
    const Date today = day(2026, 9, 30);
    auto run = [&](std::vector<std::string> args, std::string* output = nullptr) {
        std::ostringstream out;
        std::ostringstream err;
        const int code = runCli(args, out, err, today);
        if (output) *output = out.str() + err.str();
        return code;
    };
    const std::string path = dir.file("sample.opp");
    std::string out;
    CHECK_EQ(run({"sample", path}), 0);
    CHECK_EQ(run({"sample", path}, &out), 1);  // never overwrites
    CHECK(out.find("already exists") != std::string::npos);
    CHECK_EQ(run({"summary", path}, &out), 0);
    CHECK(out.find("Riverside Branch Library") != std::string::npos);
    CHECK_EQ(run({"check", path}, &out), 1);  // the sample has overdue items on purpose
    CHECK(out.find("error:") != std::string::npos);

    const auto before = Practice::load(path).time.size();
    CHECK_EQ(run({"log", path, "--staff", "tb", "--project", "2025-031", "--phase", "CD", "--hours", "2.5", "--notes",
                  "Wall types"},
                 &out),
             0);
    const Practice after = Practice::load(path);
    CHECK_EQ(after.time.size(), before + 1);
    CHECK_EQ(after.time.back().hours.str(), std::string("2.5"));
    CHECK_EQ(after.refName(Ref::Phase, after.time.back().phaseId), std::string("CD Construction Documents"));
    CHECK_EQ(*after.time.back().date, today);
    CHECK_EQ(run({"log", path, "--staff", "nobody", "--project", "2025-031", "--hours", "1"}, &out), 1);
    CHECK_EQ(run({"log", path, "--staff", "TB", "--project", "2025-031", "--hours", "30"}, &out), 1);

    CHECK_EQ(run({"export", path, "rfis", "--project", "2024-018"}, &out), 0);
    CHECK(out.find("Canopy steel connection") != std::string::npos);
    CHECK_EQ(run({"report", path, "Harbor", dir.file("h.pdf")}), 0);
    CHECK(fs::file_size(fs::u8path(dir.file("h.pdf"))) > 1000);
    CHECK_EQ(run({"portfolio", path, dir.file("p.pdf")}), 0);
    CHECK_EQ(run({"new", dir.file("empty.opp"), "--firm", "Studio Uno"}), 0);
    CHECK_EQ(Practice::load(dir.file("empty.opp")).firm.name, std::string("Studio Uno"));
    CHECK_EQ(run({"check", dir.file("empty.opp")}, &out), 0);
    CHECK_EQ(run({"frobnicate"}), 1);
    CHECK_EQ(run({"version"}, &out), 0);
    CHECK(out.find(kVersion) != std::string::npos);
}

// ------------------------------------------------------------------ diff

TEST(diff_finds_added_removed_and_changed_records) {
    const Practice before = smallPractice();
    CHECK(diffPractice(before, before).empty());

    Practice after = before;
    after.firm.name = "Studio";
    after.phases[1].complete = Decimal::fromInt(60);
    after.phases[1].budgetHours = Decimal::fromInt(250);
    after.time.erase(after.time.begin());
    Task t;
    t.id = 1;
    t.projectId = 1;
    t.name = "Door schedule";
    after.tasks.push_back(t);

    const auto changes = diffPractice(before, after);
    CHECK_EQ(changes.size(), std::size_t(4));
    CHECK(changes[0].kind == RecordChange::Kind::Modified);
    CHECK_EQ(changes[0].type, std::string("Firm"));
    CHECK(changes[1].kind == RecordChange::Kind::Modified);
    CHECK_EQ(changes[1].name, std::string("DD"));
    CHECK_EQ(changes[1].projectId, 1);
    CHECK_EQ(changes[1].fields.size(), std::size_t(2));
    CHECK_EQ(describe(changes[1]), std::string("Changed phase \"DD\": Budget hours, % complete"));
    CHECK(changes[2].kind == RecordChange::Kind::Added);
    CHECK_EQ(describe(changes[2]), std::string("Added task \"Door schedule\""));
    CHECK(changes[3].kind == RecordChange::Kind::Removed);
    CHECK_EQ(changes[3].name, std::string("2026-03-02 Ana 60 h"));
    CHECK_EQ(describe(changes[3]), std::string("Deleted time entry \"2026-03-02 Ana 60 h\""));

    Rfi r;
    r.id = 3;
    r.subject = "Footing depth";
    Practice withRfi = before;
    withRfi.rfis.push_back(r);
    CHECK_EQ(describe(diffPractice(before, withRfi).at(0)), std::string("Added RFI \"Footing depth\""));
}

int main() {
    for (const auto& t : registry()) {
        const int before = g_failures;
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::cerr << t.name << ": unexpected exception: " << e.what() << "\n";
        }
        if (g_failures != before) std::cerr << "  in test " << t.name << "\n";
    }
    std::cout << registry().size() << " tests, " << g_checks << " checks, " << g_failures << " failures\n";
    return g_failures == 0 ? 0 : 1;
}
