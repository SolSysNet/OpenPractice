#include "openpractice/report.hpp"

#include "openpractice/calc.hpp"
#include "openpractice/format.hpp"
#include "openpractice/pdf.hpp"
#include "openpractice/util.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace op {

// ---------------------------------------------------------------------- CSV

std::vector<std::string> listNames() {
    std::vector<std::string> names;
    forEachList(Practice{}, [&](const auto& s, const auto&) { names.push_back(s.name); });
    return names;
}

std::string listTitle(std::string_view name) {
    std::string title;
    forEachList(Practice{}, [&](const auto& s, const auto&) {
        if (name == s.name) title = s.plural;
    });
    return title;
}

namespace {

std::string csvCell(const std::string& s) {
    // Cells a spreadsheet would run as a formula get a leading apostrophe.
    std::string v = s;
    if (!v.empty() && (v[0] == '=' || v[0] == '+' || v[0] == '-' || v[0] == '@')) {
        const bool number = v.size() > 1 && (std::isdigit(static_cast<unsigned char>(v[1])) || v[1] == '$');
        if (!(v[0] == '-' && number)) v.insert(0, "'");
    }
    if (v.find_first_of(",\"\n\r") == std::string::npos) return v;
    std::string out = "\"";
    for (char c : v) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

}  // namespace

std::string exportCsv(const Practice& practice, std::string_view listName, int projectId) {
    std::string out;
    bool found = false;
    forEachList(practice, [&](const auto& s, const auto& items) {
        if (listName != s.name) return;
        found = true;
        std::vector<std::string> header;
        for (const auto& f : s.fields) header.push_back(csvCell(f.label));
        out += join(header, ",") + "\r\n";
        for (const auto& item : items) {
            using R = std::decay_t<decltype(item)>;
            if (projectId != 0 && projectOf(item) != projectId) continue;
            std::vector<std::string> row;
            for (const auto& f : s.fields) {
                // Plain numbers for amounts so spreadsheets can sum them.
                std::string v = std::holds_alternative<Money R::*>(f.member)
                                    ? getField(item, f)
                                    : displayValue(practice, item, f);
                row.push_back(csvCell(v));
            }
            out += join(row, ",") + "\r\n";
        }
    });
    if (!found) throw Error("unknown list '" + std::string(listName) + "' (use one of: " + join(listNames(), ", ") + ")");
    return out;
}

// ---------------------------------------------------------------------- PDF

namespace {

using pdf::Align;
using pdf::Color;
using pdf::Font;

constexpr Color kInk{0.12, 0.13, 0.15};
constexpr Color kMuted{0.42, 0.45, 0.49};
constexpr Color kAccent{0.0, 0.42, 0.42};
constexpr Color kPositive{0.12, 0.51, 0.16};
constexpr Color kNegative{0.78, 0.18, 0.16};
constexpr Color kWarning{0.72, 0.45, 0.0};
constexpr Color kRule{0.84, 0.86, 0.89};
constexpr Color kBand{0.95, 0.96, 0.97};
constexpr Color kHeaderBg{0.90, 0.95, 0.95};

constexpr double kMargin = 48;
constexpr double kBody = 9;
constexpr double kSmall = 7.5;
constexpr double kRow = 14;

Color healthColor(Health h) {
    switch (h) {
        case Health::OnTrack: return kPositive;
        case Health::Watch: return kWarning;
        case Health::OverBudget: return kNegative;
        case Health::NotStarted: break;
    }
    return kMuted;
}

struct Column {
    const char* title;
    double width;  // fraction of the usable width
    Align align = Align::Left;
};

struct Cell {
    std::string text;
    Color color = kInk;
    Font font = Font::Regular;
};

class Writer {
public:
    Writer(pdf::Document& doc, std::string runningTitle, std::string firm)
        : doc_(doc), title_(std::move(runningTitle)), firm_(std::move(firm)) {
        width_ = doc.size().width - 2 * kMargin;
        newPage();
    }

    void newPage() {
        page_ = &doc_.addPage();
        y_ = doc_.size().height - kMargin;
        const double footerY = kMargin - 22;
        page_->line(kMargin, footerY + 10, kMargin + width_, footerY + 10, 0.4, kRule);
        page_->text(kMargin, footerY, firm_.empty() ? "Prepared with OpenPractice" : firm_ + "  -  prepared with OpenPractice",
                    Font::Regular, kSmall, kMuted);
        page_->text(kMargin + width_, footerY, title_ + "  -  page " + std::to_string(doc_.pageCount()), Font::Regular,
                    kSmall, kMuted, Align::Right);
    }

    void ensure(double height) {
        if (y_ - height < kMargin + 6) newPage();
    }

    void title(const std::string& text, const std::string& subtitle) {
        page_->text(kMargin, y_ - 18, text, Font::Bold, 18, kAccent);
        y_ -= 32;
        if (!subtitle.empty()) {
            page_->text(kMargin, y_, subtitle, Font::Regular, kBody + 1, kMuted);
            y_ -= 18;
        }
    }

    void heading(const std::string& text) {
        ensure(80);  // keep a heading with its table header and first rows
        y_ -= 10;
        page_->text(kMargin, y_, text, Font::Bold, 11.5, kInk);
        y_ -= 5;
        page_->line(kMargin, y_, kMargin + width_, y_, 0.8, kAccent);
        y_ -= 13;
    }

    void paragraph(const std::string& text, Color color = kMuted) {
        for (const auto& line : pdf::wrapText(text, Font::Regular, kBody, width_)) {
            ensure(kRow);
            page_->text(kMargin, y_, line, Font::Regular, kBody, color);
            y_ -= 12;
        }
    }

    // Label / value pairs in `columns` columns.
    void facts(const std::vector<std::pair<std::string, Cell>>& items, int columns = 3) {
        const double colWidth = width_ / columns;
        for (std::size_t i = 0; i < items.size(); i += static_cast<std::size_t>(columns)) {
            ensure(28);
            for (int c = 0; c < columns && i + static_cast<std::size_t>(c) < items.size(); ++c) {
                const auto& [label, cell] = items[i + static_cast<std::size_t>(c)];
                const double x = kMargin + c * colWidth;
                page_->text(x, y_, label, Font::Regular, kSmall, kMuted);
                page_->text(x, y_ - 12, cell.text, Font::Bold, kBody + 1.5, cell.color);
            }
            y_ -= 30;
        }
    }

    void table(const std::vector<Column>& columns, const std::vector<std::vector<Cell>>& rows, const char* empty = nullptr) {
        if (rows.empty()) {
            if (empty) paragraph(empty);
            y_ -= 4;
            return;
        }
        auto header = [&] {
            ensure(kRow * 2);
            page_->fillRect(kMargin, y_ - 4, width_, kRow, kHeaderBg);
            double x = kMargin;
            for (const auto& c : columns) {
                const double w = c.width * width_;
                drawCell(x, w, c.title, Font::Bold, kSmall, kInk, c.align);
                x += w;
            }
            y_ -= kRow;
        };
        header();
        int n = 0;
        for (const auto& row : rows) {
            if (y_ - kRow < kMargin + 6) {
                newPage();
                header();
            }
            if (n++ % 2 == 1) page_->fillRect(kMargin, y_ - 4, width_, kRow, kBand);
            double x = kMargin;
            for (std::size_t i = 0; i < columns.size() && i < row.size(); ++i) {
                const double w = columns[i].width * width_;
                drawCell(x, w, row[i].text, row[i].font, kBody, row[i].color, columns[i].align);
                x += w;
            }
            y_ -= kRow;
        }
        page_->line(kMargin, y_ + kRow - 4, kMargin + width_, y_ + kRow - 4, 0.4, kRule);
        y_ -= 6;
    }

private:
    // Text clipped with "..." to fit its column.
    void drawCell(double x, double w, const std::string& text, Font font, double size, Color color, Align align) {
        const double pad = 4;
        const double avail = w - 2 * pad;
        std::string t = text;
        if (pdf::textWidth(t, font, size) > avail) {
            while (!t.empty() && pdf::textWidth(t + "...", font, size) > avail) {
                t.pop_back();
                while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();  // UTF-8 tail
                if (!t.empty() && static_cast<unsigned char>(t.back()) >= 0xC0) t.pop_back();
            }
            t += "...";
        }
        const double tx = align == Align::Right ? x + w - pad : align == Align::Center ? x + w / 2 : x + pad;
        page_->text(tx, y_, t, font, size, color, align);
    }

    pdf::Document& doc_;
    pdf::Page* page_ = nullptr;
    std::string title_;
    std::string firm_;
    double width_ = 0;
    double y_ = 0;
};

std::string pct(Decimal d) { return d.fixed(0) + "%"; }
std::string hrs(Decimal d) { return d.fixed(1); }
std::string dateText(const std::optional<Date>& d) { return d ? d->str() : std::string(); }

Cell moneyCell(Money m) { return Cell{usd(m), m < Money() ? kNegative : kInk}; }

}  // namespace

std::string projectReportPdf(const Practice& practice, int projectId, Date today) {
    const Project* p = practice.findProject(projectId);
    if (!p) throw Error("no project with id " + std::to_string(projectId));
    const ProjectStats st = projectStats(practice, projectId, today);
    const std::string name = practice.refName(Ref::Project, projectId);

    pdf::Document doc;
    doc.setTitle("Project status: " + name);
    doc.setAuthor(practice.firm.name);
    Writer w(doc, trim(p->number + " status report"), practice.firm.name);
    w.title(name, "Project status report  -  " + today.str());

    w.facts({
        {"Client", {practice.refName(Ref::Client, p->clientId)}},
        {"Project manager", {practice.refName(Ref::Staff, p->managerId)}},
        {"Status", {choiceLabel(p->status)}},
        {"Location", {p->location}},
        {"Fee type", {choiceLabel(p->feeType)}},
        {"Current phase", {st.currentPhase ? trim(st.currentPhase->code + " " + st.currentPhase->name) : "-"}},
    });

    w.heading("Fee and budget");
    w.facts({
        {"Total fee", {usd(st.totalFee)}},
        {"Earned to date", {usd(st.earned) + "  (" + pct(st.complete) + ")"}},
        {"Budget health", {healthLabel(st.health), healthColor(st.health)}},
        {"Hours", {hrs(st.hours) + " of " + hrs(st.budgetHours)}},
        {"Direct labor", {usd(st.laborCost)}},
        {"Cost with overhead", {usd(st.totalCost)}},
        {"Profit", moneyCell(st.profit)},
        {"Net multiplier", {st.multiplier.fixed(2), st.multiplier < practice.firm.targetMultiplier ? kWarning : kPositive}},
        {"Pending changes", {usd(st.pendingChanges)}},
        {"Invoiced", {usd(st.billed)}},
        {"Collected", {usd(st.collected)}},
        {"Unbilled", moneyCell(st.unbilled)},
    });
    if (!st.healthNote.empty()) w.paragraph(st.healthNote);

    w.heading("Phases");
    {
        std::vector<std::vector<Cell>> rows;
        for (const auto& ps : st.phases) {
            rows.push_back({{trim(ps.phase->code + " " + ps.phase->name)},
                            {pct(ps.phase->feeShare)},
                            {usd(ps.fee)},
                            {pct(ps.phase->complete)},
                            {usd(ps.earned)},
                            {hrs(ps.budgetHours)},
                            {hrs(ps.hours)},
                            {usd(ps.laborCost)},
                            {healthLabel(ps.health), healthColor(ps.health)}});
        }
        w.table({{"Phase", 0.23},
                 {"Share", 0.07, Align::Right},
                 {"Fee", 0.12, Align::Right},
                 {"Done", 0.07, Align::Right},
                 {"Earned", 0.12, Align::Right},
                 {"Budget h", 0.08, Align::Right},
                 {"Actual h", 0.08, Align::Right},
                 {"Labor", 0.12, Align::Right},
                 {"Health", 0.11}},
                rows, "No phases.");
    }

    w.heading("Open tasks");
    {
        std::vector<const Task*> open;
        for (const auto& t : practice.tasks) {
            if (t.projectId == projectId && t.status != TaskStatus::Done) open.push_back(&t);
        }
        std::stable_sort(open.begin(), open.end(), [](const Task* a, const Task* b) {
            if (a->due.has_value() != b->due.has_value()) return a->due.has_value();
            return a->due && *a->due < *b->due;
        });
        std::vector<std::vector<Cell>> rows;
        for (const Task* t : open) {
            const bool late = isOverdue(*t, today);
            rows.push_back({{t->name},
                            {practice.findPhase(t->phaseId) ? practice.findPhase(t->phaseId)->code : ""},
                            {practice.refName(Ref::Staff, t->assigneeId)},
                            {dateText(t->due), late ? kNegative : kInk},
                            {choiceLabel(t->status)}});
        }
        w.table({{"Task", 0.42}, {"Phase", 0.08}, {"Assigned to", 0.2}, {"Due", 0.14}, {"Status", 0.16}}, rows,
                "No open tasks.");
    }

    w.heading("Open RFIs");
    {
        std::vector<std::vector<Cell>> rows;
        for (const auto& r : practice.rfis) {
            if (r.projectId != projectId || r.status != RfiStatus::Open) continue;
            const bool late = r.due && *r.due < today;
            rows.push_back({{std::to_string(r.number)},
                            {r.subject},
                            {r.from},
                            {dateText(r.received)},
                            {dateText(r.due), late ? kNegative : kInk},
                            {r.received ? std::to_string(today - *r.received) : ""}});
        }
        w.table({{"#", 0.06}, {"Subject", 0.4}, {"From", 0.18}, {"Received", 0.13}, {"Due", 0.13}, {"Days", 0.1, Align::Right}},
                rows, "No open RFIs.");
    }

    w.heading("Submittals in review");
    {
        std::vector<std::vector<Cell>> rows;
        for (const auto& s : practice.submittals) {
            if (s.projectId != projectId) continue;
            if (s.status != SubmittalStatus::Pending && s.status != SubmittalStatus::UnderReview) continue;
            const bool late = s.due && *s.due < today;
            rows.push_back({{s.number}, {s.description}, {s.contractor}, {dateText(s.due), late ? kNegative : kInk},
                            {choiceLabel(s.status)}});
        }
        w.table({{"Number", 0.16}, {"Description", 0.38}, {"Submitted by", 0.18}, {"Due", 0.13}, {"Status", 0.15}}, rows,
                "No submittals in review.");
    }

    w.heading("Drawing log");
    {
        std::vector<const Sheet*> sheets;
        for (const auto& s : practice.sheets) {
            if (s.projectId == projectId) sheets.push_back(&s);
        }
        std::stable_sort(sheets.begin(), sheets.end(), [](const Sheet* a, const Sheet* b) {
            if (a->discipline != b->discipline) return a->discipline < b->discipline;
            return a->number < b->number;
        });
        std::vector<std::vector<Cell>> rows;
        for (const Sheet* s : sheets) {
            rows.push_back({{s->number}, {s->title}, {choiceLabel(s->discipline)}, {std::to_string(s->revision)},
                            {choiceLabel(s->status)}, {dateText(s->issued)}});
        }
        w.table({{"Sheet", 0.1}, {"Title", 0.4}, {"Discipline", 0.15}, {"Rev", 0.06, Align::Right}, {"Status", 0.15}, {"Issued", 0.14}},
                rows, "No sheets.");
    }

    w.heading("Change orders");
    {
        std::vector<std::vector<Cell>> rows;
        for (const auto& c : practice.changes) {
            if (c.projectId != projectId) continue;
            rows.push_back({{std::to_string(c.number)}, {c.description}, moneyCell(c.amount), {hrs(c.hours)},
                            {choiceLabel(c.status)}, {dateText(c.date)}});
        }
        w.table({{"#", 0.06}, {"Description", 0.46}, {"Fee", 0.14, Align::Right}, {"Hours", 0.08, Align::Right}, {"Status", 0.12}, {"Date", 0.14}},
                rows, "No change orders.");
    }

    w.heading("Invoices");
    {
        std::vector<std::vector<Cell>> rows;
        for (const auto& i : practice.invoices) {
            if (i.projectId != projectId) continue;
            const bool late = i.status == InvoiceStatus::Sent && i.due && *i.due < today;
            rows.push_back({{i.number}, {dateText(i.date)}, {dateText(i.due), late ? kNegative : kInk}, {usd(i.amount)},
                            {usd(i.status == InvoiceStatus::Paid && i.paid.isZero() ? i.amount : i.paid)},
                            {choiceLabel(i.status)}});
        }
        w.table({{"Invoice", 0.18}, {"Date", 0.15}, {"Due", 0.15}, {"Amount", 0.17, Align::Right}, {"Paid", 0.17, Align::Right}, {"Status", 0.18}},
                rows, "No invoices.");
    }
    return doc.build();
}

std::string portfolioReportPdf(const Practice& practice, Date today) {
    pdf::Document doc;
    doc.setTitle("Project portfolio");
    doc.setAuthor(practice.firm.name);
    Writer w(doc, "Portfolio report", practice.firm.name);
    w.title(practice.firm.name.empty() ? "Project portfolio" : practice.firm.name, "Portfolio report  -  " + today.str());

    const FirmStats fs = firmStats(practice, today);
    w.facts({
        {"Active projects", {std::to_string(fs.activeProjects)}},
        {"Active fees", {usd(fs.activeFees)}},
        {"Backlog (unearned)", {usd(fs.backlog)}},
        {"Earned to date", {usd(fs.earned)}},
        {"Unbilled", {usd(fs.unbilled)}},
        {"Receivable", {usd(fs.receivable)}},
    });

    w.heading("Projects");
    std::vector<std::vector<Cell>> rows;
    for (const auto& p : practice.projects) {
        const ProjectStats st = projectStats(practice, p.id, today);
        rows.push_back({{p.number},
                        {p.name},
                        {practice.refName(Ref::Client, p.clientId)},
                        {choiceLabel(p.status)},
                        {usd(st.totalFee)},
                        {pct(st.complete)},
                        moneyCell(st.profit),
                        {st.multiplier.fixed(2)},
                        {healthLabel(st.health), healthColor(st.health)}});
    }
    w.table({{"Number", 0.09},
             {"Project", 0.22},
             {"Client", 0.17},
             {"Status", 0.08},
             {"Fee", 0.12, Align::Right},
             {"Done", 0.06, Align::Right},
             {"Profit", 0.11, Align::Right},
             {"Mult.", 0.05, Align::Right},
             {"Health", 0.10}},
            rows, "No projects.");

    w.heading("Staff utilization, last 4 weeks");
    const Date to = today;
    const Date from = today.addDays(-27);
    std::vector<std::vector<Cell>> staffRows;
    for (const auto& l : staffLoad(practice, from, to)) {
        if (!l.staff->active) continue;
        staffRows.push_back({{l.staff->name},
                             {choiceLabel(l.staff->role)},
                             {hrs(l.hours)},
                             {hrs(l.billable)},
                             {pct(l.utilization)},
                             {std::to_string(l.openTasks)},
                             {hrs(l.openTaskHours)}});
    }
    w.table({{"Name", 0.24}, {"Role", 0.2}, {"Hours", 0.1, Align::Right}, {"Billable", 0.1, Align::Right},
             {"Utilization", 0.12, Align::Right}, {"Open tasks", 0.12, Align::Right}, {"Est. hours", 0.12, Align::Right}},
            staffRows, "No staff.");

    const auto issues = reviewPractice(practice, today);
    if (!issues.empty()) {
        w.heading("Needs attention");
        for (const auto& i : issues) {
            const Color c = i.severity == Severity::Error ? kNegative : i.severity == Severity::Warning ? kWarning : kMuted;
            w.paragraph("- " + i.message, c);
        }
    }
    return doc.build();
}

// --------------------------------------------------------------------- text

std::string practiceSummaryText(const Practice& practice, Date today) {
    std::ostringstream out;
    const FirmStats fs = firmStats(practice, today);
    out << (practice.firm.name.empty() ? "OpenPractice" : practice.firm.name) << "  (" << today.str() << ")\n";
    out << "  Active projects: " << fs.activeProjects << "   Proposals: " << fs.proposals << "\n";
    out << "  Active fees: " << usd(fs.activeFees) << "   Earned: " << usd(fs.earned) << "   Backlog: " << usd(fs.backlog)
        << "\n";
    out << "  Unbilled: " << usd(fs.unbilled) << "   Receivable: " << usd(fs.receivable)
        << "   Hours this week: " << fs.hoursThisWeek.fixed(1) << "\n\n";

    for (const auto& p : practice.projects) {
        const ProjectStats st = projectStats(practice, p.id, today);
        out << "  " << practice.refName(Ref::Project, p.id) << "  [" << choiceLabel(p.status) << "]\n";
        out << "      fee " << usd(st.totalFee) << ", earned " << usd(st.earned) << " (" << st.complete.fixed(0)
            << "%), hours " << st.hours.fixed(1) << "/" << st.budgetHours.fixed(1) << ", profit " << usd(st.profit)
            << ", multiplier " << st.multiplier.fixed(2) << ", " << healthLabel(st.health) << "\n";
    }
    const auto issues = reviewPractice(practice, today);
    if (!issues.empty()) {
        out << "\nNeeds attention:\n";
        for (const auto& i : issues) {
            const char* tag = i.severity == Severity::Error ? "error" : i.severity == Severity::Warning ? "warning" : "note";
            out << "  [" << tag << "] " << i.message << "\n";
        }
    }
    return out.str();
}

}  // namespace op
