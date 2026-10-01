// A sample practice for trying OpenPractice: a 9-person A/E firm with a library in
// construction documents, a mixed-use building in design development, a school
// modernization under construction and a proposal. Dates are placed relative to `today`
// so the dashboard always has something current to show.

#include "openpractice/model.hpp"

#include <vector>

namespace op {
namespace {

Decimal dec(int whole, int tenths = 0) { return Decimal::fromRaw(whole * Decimal::kScale + tenths * 1000); }
Money usd(long long dollars) { return Money::fromCents(dollars * 100); }

bool isWeekday(Date d) {
    const int weekday = ((d.serial() + 3) % 7 + 7) % 7;  // 0 = Monday
    return weekday < 5;
}

struct Builder {
    Practice p;
    Date today;

    int staff(const char* name, const char* initials, Role role, const char* license, int bill, int cost) {
        Staff s;
        s.id = Practice::nextId(p.staff);
        s.name = name;
        s.initials = initials;
        s.role = role;
        s.license = license;
        s.billingRate = usd(bill);
        s.costRate = usd(cost);
        std::string email;
        for (char c : std::string(name)) {
            if (c == ' ') break;
            email += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        }
        s.email = email + "@northlight.example";
        p.staff.push_back(s);
        return s.id;
    }

    int client(const char* name, const char* contact, const char* phone, const char* address) {
        Client c;
        c.id = Practice::nextId(p.clients);
        c.name = name;
        c.contact = contact;
        c.phone = phone;
        c.address = address;
        p.clients.push_back(c);
        return c.id;
    }

    int phase(int projectId, const char* code, const char* name, int share, int budget, int complete, int from, int to) {
        Phase ph;
        ph.id = Practice::nextId(p.phases);
        ph.projectId = projectId;
        ph.code = code;
        ph.name = name;
        ph.feeShare = Decimal::fromInt(share);
        ph.budgetHours = Decimal::fromInt(budget);
        ph.complete = Decimal::fromInt(complete);
        ph.start = today.addDays(from);
        ph.end = today.addDays(to);
        p.phases.push_back(ph);
        return ph.id;
    }

    // Spreads `hours` over the weekdays of a phase (up to yesterday for a phase in progress),
    // alternating between team members in half-day and full-day blocks.
    void time(int projectId, int phaseId, const std::vector<int>& team, int hours, int seed) {
        const Phase* ph = p.findPhase(phaseId);
        if (!ph || hours <= 0) return;
        const Date last = *ph->end < today ? *ph->end : today.addDays(-1);
        std::vector<Date> days;
        for (Date d = *ph->start; d <= last; d = d.addDays(1)) {
            if (isWeekday(d)) days.push_back(d);
        }
        if (days.empty()) return;
        // Work in half hours so every entry is a round number.
        const long long halves = static_cast<long long>(hours) * 2;
        long long given = 0;
        for (std::size_t i = 0; i < days.size(); ++i) {
            const long long target = halves * static_cast<long long>(i + 1) / static_cast<long long>(days.size());
            long long today_ = target - given;
            int member = static_cast<int>((i + static_cast<std::size_t>(seed)) % team.size());
            while (today_ > 0) {
                const long long chunk = today_ > 16 ? 16 : today_;  // at most 8 hours per entry
                TimeEntry t;
                t.id = Practice::nextId(p.time);
                t.date = days[i];
                t.staffId = team[static_cast<std::size_t>(member)];
                t.projectId = projectId;
                t.phaseId = phaseId;
                t.hours = Decimal::fromRaw(chunk * Decimal::kScale / 2);
                t.billable = true;
                p.time.push_back(t);
                given += chunk;
                today_ -= chunk;
                member = (member + 1) % static_cast<int>(team.size());
            }
        }
    }

    int task(int projectId, int phaseId, const char* name, int assignee, TaskStatus status, int start, int due, int estimate,
             int after = 0, bool milestone = false) {
        Task t;
        t.id = Practice::nextId(p.tasks);
        t.projectId = projectId;
        t.phaseId = phaseId;
        t.name = name;
        t.assigneeId = assignee;
        t.status = status;
        t.start = today.addDays(start);
        t.due = today.addDays(due);
        t.estimateHours = Decimal::fromInt(estimate);
        t.predecessorId = after;
        t.milestone = milestone;
        p.tasks.push_back(t);
        return t.id;
    }

    void sheet(int projectId, int phaseId, const char* number, const char* title, Discipline d, int rev, SheetStatus status,
               int issued) {
        Sheet s;
        s.id = Practice::nextId(p.sheets);
        s.projectId = projectId;
        s.phaseId = phaseId;
        s.number = number;
        s.title = title;
        s.discipline = d;
        s.revision = rev;
        s.status = status;
        if (status == SheetStatus::Issued || rev > 0) s.issued = today.addDays(issued);
        p.sheets.push_back(s);
    }

    void rfi(int projectId, const char* subject, const char* from, int received, int due, RfiStatus status, int assignee,
             const char* question, const char* response = "") {
        Rfi r;
        r.id = Practice::nextId(p.rfis);
        r.projectId = projectId;
        r.number = p.nextRfiNumber(projectId);
        r.subject = subject;
        r.from = from;
        r.received = today.addDays(received);
        r.due = today.addDays(due);
        r.status = status;
        r.assigneeId = assignee;
        r.question = question;
        r.response = response;
        if (status != RfiStatus::Open) r.answered = today.addDays(due - 2 > received ? due - 2 : received + 1);
        p.rfis.push_back(r);
    }

    void submittal(int projectId, const char* number, const char* spec, const char* description, const char* contractor,
                   int received, int due, SubmittalStatus status, int reviewer) {
        Submittal s;
        s.id = Practice::nextId(p.submittals);
        s.projectId = projectId;
        s.number = number;
        s.specSection = spec;
        s.description = description;
        s.contractor = contractor;
        s.received = today.addDays(received);
        s.due = today.addDays(due);
        s.status = status;
        s.reviewerId = reviewer;
        if (status != SubmittalStatus::Pending && status != SubmittalStatus::UnderReview)
            s.returned = today.addDays(due - 1);
        p.submittals.push_back(s);
    }

    void change(int projectId, const char* description, long long amount, int hours, ChangeStatus status, int date) {
        ChangeOrder c;
        c.id = Practice::nextId(p.changes);
        c.projectId = projectId;
        c.number = p.nextChangeNumber(projectId);
        c.description = description;
        c.amount = usd(amount);
        c.hours = Decimal::fromInt(hours);
        c.status = status;
        c.date = today.addDays(date);
        p.changes.push_back(c);
    }

    // `count` monthly invoices ending `lastDaysAgo` days ago; all paid but the newest
    // `unpaid` ones.
    void invoices(int projectId, int count, long long amount, int lastDaysAgo, int unpaid, int overdueDays = 0) {
        for (int k = count - 1; k >= 0; --k) {
            Invoice i;
            i.id = Practice::nextId(p.invoices);
            i.projectId = projectId;
            i.number = p.nextInvoiceNumber(projectId);
            i.date = today.addDays(-lastDaysAgo - 30 * k);
            i.due = i.date->addDays(30);
            i.amount = usd(amount);
            if (k < unpaid) {
                i.status = InvoiceStatus::Sent;
                if (k == unpaid - 1 && overdueDays > 0) {
                    i.date = today.addDays(-30 - overdueDays);
                    i.due = today.addDays(-overdueDays);
                }
            } else {
                i.status = InvoiceStatus::Paid;
                i.paid = i.amount;
            }
            p.invoices.push_back(i);
        }
    }
};

}  // namespace

Practice samplePractice(Date today) {
    Builder b;
    b.today = today;
    Practice& p = b.p;

    p.firm.name = "Northlight Architects + Engineers";
    p.firm.address = "410 Mill Street, Suite 300, Riverside";
    p.firm.phone = "(555) 014-2200";
    p.firm.email = "studio@northlight.example";
    p.firm.overheadRate = Decimal::fromInt(155);
    p.firm.targetMultiplier = Decimal::fromInt(3);

    const int maya = b.staff("Maya Chen", "MC", Role::Principal, "RA, AIA", 225, 85);
    const int david = b.staff("David Okafor", "DO", Role::ProjectManager, "RA", 175, 62);
    const int lena = b.staff("Lena Fischer", "LF", Role::ProjectArchitect, "RA", 150, 50);
    const int raj = b.staff("Raj Patel", "RP", Role::Engineer, "PE, SE", 165, 58);
    const int sofia = b.staff("Sofia Martinez", "SM", Role::Designer, "", 115, 36);
    const int tom = b.staff("Tom Becker", "TB", Role::Drafter, "", 95, 30);
    const int priya = b.staff("Priya Nair", "PN", Role::Engineer, "PE", 155, 54);
    const int jonah = b.staff("Jonah Wells", "JW", Role::Intern, "", 75, 24);
    const int grace = b.staff("Grace Kim", "GK", Role::Admin, "", 0, 32);
    (void)grace;

    const int city = b.client("City of Riverside, Public Works", "Alan Brooks", "(555) 014-3100", "1 Civic Center Plaza");
    const int harbor = b.client("Harbor Light Development LLC", "Nina Torres", "(555) 014-7788", "88 Wharf Road");
    const int school = b.client("Oak Valley Unified School District", "Dr. Paula Reyes", "(555) 014-5520", "2200 Valley Blvd");
    const int medical = b.client("Greenfield Medical Partners", "Sam Whitaker", "(555) 014-9001", "17 Greenfield Ave");

    // ---- Riverside Branch Library: fixed fee, in construction documents.
    Project lib;
    lib.id = Practice::nextId(p.projects);
    lib.number = "2025-031";
    lib.name = "Riverside Branch Library";
    lib.clientId = city;
    lib.managerId = david;
    lib.location = "1250 Elm Avenue, Riverside";
    lib.buildingType = "Public library";
    lib.grossArea = Decimal::fromInt(24000);
    lib.feeType = FeeType::FixedFee;
    lib.fee = usd(684000);
    lib.constructionCost = usd(9500000);
    lib.start = today.addDays(-300);
    lib.end = today.addDays(400);
    lib.description = "New 24,000 sf single-story branch library with community room, maker space and a 60-car lot. "
                      "Mass timber roof, targeting LEED Gold.";
    p.projects.push_back(lib);
    const int libSD = b.phase(lib.id, "SD", "Schematic Design", 15, 520, 100, -300, -240);
    const int libDD = b.phase(lib.id, "DD", "Design Development", 20, 760, 100, -235, -150);
    const int libCD = b.phase(lib.id, "CD", "Construction Documents", 40, 1650, 80, -145, 20);
    const int libBN = b.phase(lib.id, "BN", "Bidding / Negotiation", 5, 140, 0, 25, 60);
    b.phase(lib.id, "CA", "Construction Administration", 20, 900, 0, 65, 400);
    b.time(lib.id, libSD, {david, sofia, maya}, 530, 0);
    b.time(lib.id, libDD, {david, sofia, tom, raj}, 745, 1);
    b.time(lib.id, libCD, {david, tom, sofia, raj, priya, tom}, 1485, 2);

    b.task(lib.id, libCD, "Wall sections and envelope details", tom, TaskStatus::Done, -120, -60, 160);
    b.task(lib.id, libCD, "Mass timber roof framing plans", raj, TaskStatus::InProgress, -90, 6, 180);
    const int mep = b.task(lib.id, libCD, "MEP coordination model", priya, TaskStatus::InProgress, -60, -3, 140);
    b.task(lib.id, libCD, "Door, hardware and finish schedules", sofia, TaskStatus::InProgress, -30, 5, 90);
    b.task(lib.id, libCD, "Specifications: Divisions 01-14", lena, TaskStatus::NotStarted, -5, 14, 120);
    const int review = b.task(lib.id, libCD, "90% CD set: internal QA/QC review", maya, TaskStatus::NotStarted, 8, 12, 24, mep, true);
    const int permit = b.task(lib.id, libCD, "Submit for building permit", david, TaskStatus::NotStarted, 14, 18, 8, review, true);
    const int bidset = b.task(lib.id, libBN, "Issue bid set", david, TaskStatus::NotStarted, 22, 25, 16, permit, true);
    b.task(lib.id, libBN, "Pre-bid meeting and addenda", david, TaskStatus::NotStarted, 30, 50, 60, bidset);

    const struct {
        const char* n;
        const char* t;
        Discipline d;
        int rev;
        SheetStatus s;
    } libSheets[] = {
        {"G-001", "Cover sheet and drawing index", Discipline::General, 2, SheetStatus::ForReview},
        {"G-002", "Code analysis and life safety plan", Discipline::General, 2, SheetStatus::ForReview},
        {"C-101", "Site plan and grading", Discipline::Civil, 1, SheetStatus::InProgress},
        {"A-101", "Floor plan", Discipline::Architectural, 2, SheetStatus::ForReview},
        {"A-102", "Roof plan", Discipline::Architectural, 1, SheetStatus::InProgress},
        {"A-201", "Exterior elevations", Discipline::Architectural, 2, SheetStatus::ForReview},
        {"A-301", "Building sections", Discipline::Architectural, 1, SheetStatus::InProgress},
        {"A-501", "Wall sections", Discipline::Architectural, 1, SheetStatus::ForReview},
        {"A-601", "Door and finish schedules", Discipline::Architectural, 1, SheetStatus::InProgress},
        {"S-101", "Foundation plan", Discipline::Structural, 1, SheetStatus::ForReview},
        {"S-201", "Roof framing plan", Discipline::Structural, 1, SheetStatus::InProgress},
        {"M-101", "HVAC floor plan", Discipline::Mechanical, 1, SheetStatus::InProgress},
        {"E-101", "Lighting plan", Discipline::Electrical, 1, SheetStatus::InProgress},
        {"P-101", "Plumbing floor plan", Discipline::Plumbing, 1, SheetStatus::InProgress},
    };
    for (const auto& s : libSheets) b.sheet(lib.id, libCD, s.n, s.t, s.d, s.rev, s.s, -150);
    b.change(lib.id, "Community room AV and acoustic design", 18500, 80, ChangeStatus::Approved, -95);
    b.change(lib.id, "Expanded geotechnical coordination", 6200, 30, ChangeStatus::Proposed, -10);
    b.invoices(lib.id, 9, 52000, 4, 1);

    // ---- Harbor Lofts: percentage of construction, in design development.
    Project lofts;
    lofts.id = Practice::nextId(p.projects);
    lofts.number = "2026-004";
    lofts.name = "Harbor Lofts Mixed-Use";
    lofts.clientId = harbor;
    lofts.managerId = lena;
    lofts.location = "88 Wharf Road";
    lofts.buildingType = "Mixed-use, 6 stories";
    lofts.grossArea = Decimal::fromInt(96000);
    lofts.feeType = FeeType::PercentOfConstruction;
    lofts.constructionCost = usd(28000000);
    lofts.feePercent = dec(6, 5);
    lofts.start = today.addDays(-130);
    lofts.end = today.addDays(700);
    lofts.description = "84 apartments over 9,000 sf of ground-floor retail; podium with five stories of wood frame.";
    p.projects.push_back(lofts);
    const int hSD = b.phase(lofts.id, "SD", "Schematic Design", 15, 1100, 100, -130, -70);
    const int hDD = b.phase(lofts.id, "DD", "Design Development", 20, 1500, 45, -65, 30);
    const int hCD = b.phase(lofts.id, "CD", "Construction Documents", 40, 3200, 0, 35, 200);
    b.phase(lofts.id, "BN", "Bidding / Negotiation", 5, 220, 0, 205, 235);
    b.phase(lofts.id, "CA", "Construction Administration", 20, 2100, 0, 240, 700);
    b.time(lofts.id, hSD, {lena, sofia, maya, jonah}, 1155, 0);
    b.time(lofts.id, hDD, {lena, sofia, jonah, raj, lena}, 930, 3);

    b.task(lofts.id, hDD, "Unit plan development (typ. floors)", sofia, TaskStatus::InProgress, -50, 4, 220);
    b.task(lofts.id, hDD, "Podium structural system selection", raj, TaskStatus::Done, -55, -20, 60);
    b.task(lofts.id, hDD, "Building code and accessibility analysis", lena, TaskStatus::InProgress, -40, 10, 80);
    b.task(lofts.id, hDD, "Retail storefront studies", jonah, TaskStatus::Blocked, -20, -2, 50);
    const int est = b.task(lofts.id, hDD, "DD cost estimate reconciliation", lena, TaskStatus::NotStarted, 12, 22, 40);
    const int ddRev = b.task(lofts.id, hDD, "Owner DD review meeting", maya, TaskStatus::NotStarted, 26, 28, 8, est, true);
    b.task(lofts.id, hCD, "CD kickoff and sheet setup", tom, TaskStatus::NotStarted, 20, 40, 30, ddRev);
    b.change(lofts.id, "Retail tenant fit-out test fits", 42000, 260, ChangeStatus::Approved, -45);
    b.invoices(lofts.id, 3, 110000, 10, 2, 15);
    b.sheet(lofts.id, hDD, "A-101", "Ground floor plan", Discipline::Architectural, 1, SheetStatus::InProgress, -70);
    b.sheet(lofts.id, hDD, "A-102", "Typical residential floor plan", Discipline::Architectural, 1, SheetStatus::InProgress, -70);
    b.sheet(lofts.id, hDD, "A-201", "Building elevations", Discipline::Architectural, 0, SheetStatus::InProgress, 0);
    b.sheet(lofts.id, hDD, "S-101", "Podium framing plan", Discipline::Structural, 0, SheetStatus::InProgress, 0);

    // ---- Oak Valley Elementary: fixed fee, in construction administration.
    Project oak;
    oak.id = Practice::nextId(p.projects);
    oak.number = "2024-018";
    oak.name = "Oak Valley Elementary Modernization";
    oak.clientId = school;
    oak.managerId = david;
    oak.location = "500 Oak Valley Road";
    oak.buildingType = "K-5 school";
    oak.grossArea = Decimal::fromInt(52000);
    oak.feeType = FeeType::FixedFee;
    oak.fee = usd(415000);
    oak.constructionCost = usd(6900000);
    oak.start = today.addDays(-620);
    oak.end = today.addDays(60);
    oak.description = "Modernization of a 1962 campus: new HVAC, seismic upgrades to two classroom wings, ADA "
                      "improvements and a new entry canopy. Occupied campus; summer-phased work.";
    p.projects.push_back(oak);
    const int oSD = b.phase(oak.id, "SD", "Schematic Design", 12, 300, 100, -620, -560);
    const int oDD = b.phase(oak.id, "DD", "Design Development", 18, 450, 100, -555, -470);
    const int oCD = b.phase(oak.id, "CD", "Construction Documents", 45, 980, 100, -465, -330);
    const int oBN = b.phase(oak.id, "BN", "Bidding / Negotiation", 5, 90, 100, -325, -290);
    const int oCA = b.phase(oak.id, "CA", "Construction Administration", 20, 700, 55, -285, 60);
    b.time(oak.id, oSD, {david, maya}, 300, 0);
    b.time(oak.id, oDD, {david, priya, tom}, 468, 1);
    b.time(oak.id, oCD, {david, tom, priya, raj, tom}, 1040, 2);
    b.time(oak.id, oBN, {david}, 86, 0);
    b.time(oak.id, oCA, {david, priya, maya}, 350, 1);

    b.task(oak.id, oCA, "Biweekly site observation reports", david, TaskStatus::InProgress, -280, 55, 160);
    b.task(oak.id, oCA, "Review pay applications", david, TaskStatus::InProgress, -280, 55, 40);
    b.task(oak.id, oCA, "Wing B seismic retrofit observation", raj, TaskStatus::Done, -120, -60, 24);
    const int punch = b.task(oak.id, oCA, "Substantial completion punch list", david, TaskStatus::NotStarted, 30, 40, 32, 0, true);
    b.task(oak.id, oCA, "Closeout: record drawings and O&M review", tom, TaskStatus::NotStarted, 40, 60, 40, punch);

    b.rfi(oak.id, "Existing footing depth at grid C/4", "Summit Builders", -200, -193, RfiStatus::Closed, raj,
          "Exposed footing at C/4 is 18\" shallower than shown. Confirm the retrofit dowel embedment.",
          "Use 12\" embedment per revised detail 5/S-501 (ASI-03).");
    b.rfi(oak.id, "Door 114 hardware set conflict", "Summit Builders", -150, -143, RfiStatus::Closed, sofia,
          "Hardware set 7 calls for a closer on a door with a hold-open magnet. Which governs?",
          "Provide closer with magnetic hold-open release tied to fire alarm.");
    b.rfi(oak.id, "RTU-3 curb dimensions", "Valley Mechanical", -60, -53, RfiStatus::Answered, priya,
          "Submitted RTU-3 needs a 92\" x 60\" curb; roof framing opening is 88\" x 60\".",
          "Add header per sketch SK-M-07; structural to confirm.");
    b.rfi(oak.id, "Canopy steel connection at existing wall", "Summit Builders", -12, -2, RfiStatus::Open, raj,
          "Existing CMU wall at the entry is unreinforced at the canopy beam bearing. Provide connection.");
    b.rfi(oak.id, "Classroom ceiling height at soffit", "Summit Builders", -6, 4, RfiStatus::Open, lena,
          "New duct mains at Wing A conflict with the 9'-0\" ceiling at the corridor soffit.");
    b.rfi(oak.id, "Flooring transition at Room 120", "Bayside Flooring", -3, 7, RfiStatus::Open, sofia,
          "Thresholds between existing VCT and new LVT differ by 1/8\". Confirm transition strip.");

    b.submittal(oak.id, "03 30 00-01", "03 30 00 Cast-in-place concrete", "Concrete mix designs", "Summit Builders",
                -240, -226, SubmittalStatus::Approved, raj);
    b.submittal(oak.id, "05 12 00-01", "05 12 00 Structural steel framing", "Canopy steel shop drawings", "Ironline Steel",
                -90, -76, SubmittalStatus::ApprovedAsNoted, raj);
    b.submittal(oak.id, "23 74 13-01", "23 74 13 Packaged rooftop units", "RTU product data", "Valley Mechanical", -70, -56,
                SubmittalStatus::ReviseResubmit, priya);
    b.submittal(oak.id, "08 41 13-01", "08 41 13 Aluminum storefronts", "Entry storefront shop drawings", "ClearView Glazing",
                -8, 3, SubmittalStatus::UnderReview, lena);
    b.submittal(oak.id, "09 51 13-01", "09 51 13 Acoustical ceilings", "Ceiling tile and grid samples", "Summit Builders",
                -16, -2, SubmittalStatus::Pending, sofia);
    b.submittal(oak.id, "26 51 00-01", "26 51 00 Interior lighting", "LED fixture product data", "Brightway Electric", -5, 9,
                SubmittalStatus::UnderReview, priya);
    for (const auto& s : libSheets) {
        if (s.d == Discipline::Architectural || s.d == Discipline::Structural)
            b.sheet(oak.id, oCD, s.n, s.t, s.d, 3, SheetStatus::Issued, -330);
    }
    b.change(oak.id, "Additional site visits for summer phasing", 9600, 48, ChangeStatus::Approved, -200);
    b.invoices(oak.id, 10, 38000, 20, 1);

    // ---- Greenfield Medical: a proposal, hourly not to exceed.
    Project med;
    med.id = Practice::nextId(p.projects);
    med.number = "2026-011";
    med.name = "Greenfield Medical Office Building";
    med.clientId = medical;
    med.managerId = maya;
    med.status = ProjectStatus::Proposal;
    med.location = "17 Greenfield Avenue";
    med.buildingType = "Medical office";
    med.grossArea = Decimal::fromInt(32000);
    med.feeType = FeeType::HourlyNotToExceed;
    med.fee = usd(120000);
    med.description = "Feasibility study and schematic design for a two-story medical office building.";
    p.projects.push_back(med);

    return p;
}

}  // namespace op
