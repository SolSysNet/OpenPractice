#include "openpractice/model.hpp"

#include "openpractice/util.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace op {

namespace fs = std::filesystem;

// ------------------------------------------------------------------ choices

const std::vector<Choice>& choices(ProjectStatus) {
    static const std::vector<Choice> c = {
        {"proposal", "Proposal"}, {"active", "Active"}, {"on_hold", "On hold"},
        {"complete", "Complete"}, {"cancelled", "Cancelled"},
    };
    return c;
}

const std::vector<Choice>& choices(FeeType) {
    static const std::vector<Choice> c = {
        {"fixed", "Fixed fee"},
        {"hourly_nte", "Hourly, not to exceed"},
        {"hourly", "Hourly"},
        {"percent", "% of construction cost"},
    };
    return c;
}

const std::vector<Choice>& choices(Role) {
    static const std::vector<Choice> c = {
        {"principal", "Principal"}, {"pm", "Project manager"}, {"project_architect", "Project architect"},
        {"architect", "Architect"}, {"engineer", "Engineer"},   {"designer", "Designer"},
        {"drafter", "Drafter / BIM tech"}, {"intern", "Intern"}, {"admin", "Administration"},
    };
    return c;
}

const std::vector<Choice>& choices(TaskStatus) {
    static const std::vector<Choice> c = {
        {"not_started", "Not started"}, {"in_progress", "In progress"}, {"blocked", "Blocked"}, {"done", "Done"},
    };
    return c;
}

const std::vector<Choice>& choices(Discipline) {
    static const std::vector<Choice> c = {
        {"general", "General"},         {"civil", "Civil"},         {"landscape", "Landscape"},
        {"architectural", "Architectural"}, {"interiors", "Interiors"}, {"structural", "Structural"},
        {"mechanical", "Mechanical"},   {"electrical", "Electrical"}, {"plumbing", "Plumbing"},
        {"fire", "Fire protection"},
    };
    return c;
}

const char* disciplinePrefix(Discipline d) {
    static const char* const prefixes[] = {"G", "C", "L", "A", "I", "S", "M", "E", "P", "FP"};
    return prefixes[static_cast<std::size_t>(d)];
}

const std::vector<Choice>& choices(SheetStatus) {
    static const std::vector<Choice> c = {
        {"in_progress", "In progress"}, {"for_review", "For review"}, {"issued", "Issued"}, {"superseded", "Superseded"},
    };
    return c;
}

const std::vector<Choice>& choices(RfiStatus) {
    static const std::vector<Choice> c = {{"open", "Open"}, {"answered", "Answered"}, {"closed", "Closed"}};
    return c;
}

const std::vector<Choice>& choices(SubmittalStatus) {
    static const std::vector<Choice> c = {
        {"pending", "Pending"},   {"under_review", "Under review"},       {"approved", "Approved"},
        {"approved_as_noted", "Approved as noted"}, {"revise_resubmit", "Revise & resubmit"}, {"rejected", "Rejected"},
    };
    return c;
}

const std::vector<Choice>& choices(ChangeStatus) {
    static const std::vector<Choice> c = {{"proposed", "Proposed"}, {"approved", "Approved"}, {"rejected", "Rejected"}};
    return c;
}

const std::vector<Choice>& choices(InvoiceStatus) {
    static const std::vector<Choice> c = {{"draft", "Draft"}, {"sent", "Sent"}, {"paid", "Paid"}};
    return c;
}

// ------------------------------------------------------------------ schemas

template <>
const Schema<Firm>& schema<Firm>() {
    using T = Firm;
    static const Schema<T> s{"FIRM", "firm", "Firm", "Firm", {
        {"name", "Firm name", "", &T::name},
        {"address", "Address", "", &T::address},
        {"phone", "Phone", "", &T::phone},
        {"email", "Email", "", &T::email},
        {"overhead", "Overhead rate %",
         "Indirect costs (rent, benefits, admin, unbilled time) as a percentage of direct labor. A typical "
         "A/E firm runs 140-180%. Used to figure each project's true cost and profit.",
         &T::overheadRate},
        {"target_multiplier", "Target multiplier",
         "The net multiplier you aim for: fee earned divided by direct labor cost. 3.0 is a common goal.",
         &T::targetMultiplier},
    }};
    return s;
}

template <>
const Schema<Staff>& schema<Staff>() {
    using T = Staff;
    static const Schema<T> s{"STAFF", "staff", "Team member", "Team members", {
        {"id", "Id", "", &T::id},
        {"name", "Name", "", &T::name},
        {"initials", "Initials", "Shown on the schedule and timesheets.", &T::initials},
        {"role", "Role", "", &T::role},
        {"license", "License", "Professional registration, e.g. RA, PE, SE, NCARB.", &T::license},
        {"email", "Email", "", &T::email},
        {"billing_rate", "Billing rate / hr", "What the firm charges clients for an hour of this person's time.",
         &T::billingRate},
        {"cost_rate", "Cost rate / hr", "Direct salary cost of an hour of this person's time (salary / 2,080).",
         &T::costRate},
        {"capacity", "Weekly capacity (hrs)", "Hours this person is available each week, for utilization.",
         &T::weeklyCapacity},
        {"active", "Active", "Inactive people stay on past timesheets but can't be assigned new work.", &T::active},
    }};
    return s;
}

template <>
const Schema<Client>& schema<Client>() {
    using T = Client;
    static const Schema<T> s{"CLIENT", "clients", "Client", "Clients", {
        {"id", "Id", "", &T::id},
        {"name", "Name", "", &T::name},
        {"contact", "Contact person", "", &T::contact},
        {"email", "Email", "", &T::email},
        {"phone", "Phone", "", &T::phone},
        {"address", "Address", "", &T::address},
        {"notes", "Notes", "", &T::notes, Ref::None, true},
    }};
    return s;
}

template <>
const Schema<Project>& schema<Project>() {
    using T = Project;
    static const Schema<T> s{"PROJECT", "projects", "Project", "Projects", {
        {"id", "Id", "", &T::id},
        {"number", "Project number", "Your firm's job number, e.g. 2026-014.", &T::number},
        {"name", "Name", "", &T::name},
        {"client", "Client", "", &T::clientId, Ref::Client},
        {"manager", "Project manager", "", &T::managerId, Ref::Staff},
        {"status", "Status", "", &T::status},
        {"location", "Location", "Site address or city.", &T::location},
        {"building_type", "Building type", "e.g. Office, K-12 school, Multifamily.", &T::buildingType},
        {"area", "Gross area (sf)", "", &T::grossArea},
        {"fee_type", "Fee type", "", &T::feeType},
        {"fee", "Fee",
         "Fixed fee: the lump sum. Hourly not to exceed: the cap. Hourly: your estimate. Not used for a "
         "percentage-of-construction fee.",
         &T::fee},
        {"construction_cost", "Construction cost", "Estimated cost of the work, for a percentage fee and fee/sf.",
         &T::constructionCost},
        {"fee_percent", "Fee % of construction", "Only for a percentage-of-construction fee.", &T::feePercent},
        {"start", "Start", "", &T::start},
        {"end", "Target completion", "", &T::end},
        {"description", "Description", "", &T::description, Ref::None, true},
    }};
    return s;
}

template <>
const Schema<Phase>& schema<Phase>() {
    using T = Phase;
    static const Schema<T> s{"PHASE", "phases", "Phase", "Phases", {
        {"id", "Id", "", &T::id},
        {"project", "Project", "", &T::projectId, Ref::Project},
        {"code", "Code", "Short code, e.g. SD, DD, CD.", &T::code},
        {"name", "Name", "", &T::name},
        {"fee_share", "Share of fee %", "The part of the contract fee allotted to this phase. Phases should add to 100%.",
         &T::feeShare},
        {"budget_hours", "Budget hours", "", &T::budgetHours},
        {"complete", "% complete", "Your estimate of how much of this phase's work is done. Drives the earned fee.",
         &T::complete},
        {"start", "Start", "", &T::start},
        {"end", "End", "", &T::end},
    }};
    return s;
}

template <>
const Schema<Task>& schema<Task>() {
    using T = Task;
    static const Schema<T> s{"TASK", "tasks", "Task", "Tasks", {
        {"id", "Id", "", &T::id},
        {"project", "Project", "", &T::projectId, Ref::Project},
        {"phase", "Phase", "", &T::phaseId, Ref::Phase},
        {"name", "Task", "", &T::name},
        {"assignee", "Assigned to", "", &T::assigneeId, Ref::Staff},
        {"status", "Status", "", &T::status},
        {"start", "Start", "", &T::start},
        {"due", "Due", "", &T::due},
        {"estimate", "Estimate (hrs)", "", &T::estimateHours},
        {"after", "Starts after", "A task that has to finish before this one can start.", &T::predecessorId, Ref::Task},
        {"milestone", "Milestone", "A deadline or deliverable date rather than a span of work.", &T::milestone},
        {"notes", "Notes", "", &T::notes, Ref::None, true},
    }};
    return s;
}

template <>
const Schema<TimeEntry>& schema<TimeEntry>() {
    using T = TimeEntry;
    static const Schema<T> s{"TIME", "time", "Time entry", "Time entries", {
        {"id", "Id", "", &T::id},
        {"date", "Date", "", &T::date},
        {"staff", "Person", "", &T::staffId, Ref::Staff},
        {"project", "Project", "", &T::projectId, Ref::Project},
        {"phase", "Phase", "", &T::phaseId, Ref::Phase},
        {"hours", "Hours", "", &T::hours},
        {"billable", "Billable", "", &T::billable},
        {"notes", "Notes", "", &T::notes},
    }};
    return s;
}

template <>
const Schema<Sheet>& schema<Sheet>() {
    using T = Sheet;
    static const Schema<T> s{"SHEET", "sheets", "Drawing sheet", "Drawings", {
        {"id", "Id", "", &T::id},
        {"project", "Project", "", &T::projectId, Ref::Project},
        {"number", "Sheet", "Sheet number, e.g. A-101 (US National CAD Standard).", &T::number},
        {"title", "Title", "", &T::title},
        {"discipline", "Discipline", "", &T::discipline},
        {"phase", "Phase", "", &T::phaseId, Ref::Phase},
        {"revision", "Revision", "", &T::revision},
        {"status", "Status", "", &T::status},
        {"issued", "Last issued", "", &T::issued},
    }};
    return s;
}

template <>
const Schema<Rfi>& schema<Rfi>() {
    using T = Rfi;
    static const Schema<T> s{"RFI", "rfis", "RFI", "RFIs", {
        {"id", "Id", "", &T::id},
        {"project", "Project", "", &T::projectId, Ref::Project},
        {"number", "RFI #", "", &T::number},
        {"subject", "Subject", "", &T::subject},
        {"from", "From", "Who submitted it, e.g. the general contractor.", &T::from},
        {"received", "Received", "", &T::received},
        {"due", "Response due", "", &T::due},
        {"status", "Status", "", &T::status},
        {"assignee", "Assigned to", "", &T::assigneeId, Ref::Staff},
        {"question", "Question", "", &T::question, Ref::None, true},
        {"response", "Response", "", &T::response, Ref::None, true},
        {"answered", "Answered", "", &T::answered},
    }};
    return s;
}

template <>
const Schema<Submittal>& schema<Submittal>() {
    using T = Submittal;
    static const Schema<T> s{"SUBMITTAL", "submittals", "Submittal", "Submittals", {
        {"id", "Id", "", &T::id},
        {"project", "Project", "", &T::projectId, Ref::Project},
        {"number", "Submittal #", "Usually the spec section plus a sequence, e.g. 08 41 13-01.", &T::number},
        {"spec", "Spec section", "", &T::specSection},
        {"description", "Description", "", &T::description},
        {"contractor", "Submitted by", "", &T::contractor},
        {"received", "Received", "", &T::received},
        {"due", "Review due", "", &T::due},
        {"status", "Status", "", &T::status},
        {"reviewer", "Reviewer", "", &T::reviewerId, Ref::Staff},
        {"returned", "Returned", "", &T::returned},
    }};
    return s;
}

template <>
const Schema<ChangeOrder>& schema<ChangeOrder>() {
    using T = ChangeOrder;
    static const Schema<T> s{"CHANGE", "changes", "Change order", "Change orders", {
        {"id", "Id", "", &T::id},
        {"project", "Project", "", &T::projectId, Ref::Project},
        {"number", "CO #", "", &T::number},
        {"description", "Description", "Additional services or a change in scope.", &T::description},
        {"amount", "Fee change", "Added fee (negative for a credit). Approved changes add to the contract fee.",
         &T::amount},
        {"hours", "Added hours", "Hours added to the budget.", &T::hours},
        {"status", "Status", "", &T::status},
        {"date", "Date", "", &T::date},
    }};
    return s;
}

template <>
const Schema<Invoice>& schema<Invoice>() {
    using T = Invoice;
    static const Schema<T> s{"INVOICE", "invoices", "Invoice", "Invoices", {
        {"id", "Id", "", &T::id},
        {"project", "Project", "", &T::projectId, Ref::Project},
        {"number", "Invoice #", "", &T::number},
        {"date", "Date", "", &T::date},
        {"due", "Due", "", &T::due},
        {"amount", "Amount", "", &T::amount},
        {"paid", "Paid", "", &T::paid},
        {"status", "Status", "", &T::status},
        {"notes", "Notes", "", &T::notes},
    }};
    return s;
}

// ------------------------------------------------------------------ fields

namespace {

template <class E>
std::string choiceKey(E value) {
    return choices(value)[static_cast<std::size_t>(value)].key;
}

template <class E>
E parseChoice(std::string_view text) {
    const auto& list = choices(E{});
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (iequals(text, list[i].key) || iequals(text, list[i].label)) return static_cast<E>(i);
    }
    std::string keys;
    for (const auto& c : list) keys += (keys.empty() ? "" : ", ") + std::string(c.key);
    throw Error("'" + std::string(text) + "' is not one of: " + keys);
}

bool parseBool(std::string_view text) {
    const std::string t = toLower(trim(text));
    if (t == "yes" || t == "y" || t == "true" || t == "1" || t == "x") return true;
    if (t == "no" || t == "n" || t == "false" || t == "0" || t.empty()) return false;
    throw Error("'" + std::string(text) + "' is not yes or no");
}

}  // namespace

template <class T>
std::string getField(const T& record, const Field<T>& field) {
    return std::visit(
        [&](auto member) -> std::string {
            const auto& v = record.*member;
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, std::string>) return v;
            else if constexpr (std::is_same_v<V, Money>) return v.isZero() ? std::string() : v.str();
            else if constexpr (std::is_same_v<V, Decimal>) return v.isZero() ? std::string() : v.str();
            else if constexpr (std::is_same_v<V, std::optional<Date>>) return v ? v->str() : std::string();
            else if constexpr (std::is_same_v<V, bool>) return v ? "yes" : "no";
            else if constexpr (std::is_same_v<V, int>) return std::to_string(v);
            else return choiceKey(v);
        },
        field.member);
}

template <class T>
void setField(T& record, const Field<T>& field, std::string_view text) {
    std::visit(
        [&](auto member) {
            auto& v = record.*member;
            using V = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<V, std::string>) {
                v = field.multiline ? std::string(text) : trim(text);
            } else if constexpr (std::is_same_v<V, Money>) {
                if (trim(text).empty()) {
                    v = Money();
                    return;
                }
                auto m = Money::parse(text);
                if (!m) throw Error(std::string(field.key) + ": '" + std::string(text) + "' is not an amount");
                v = *m;
            } else if constexpr (std::is_same_v<V, Decimal>) {
                if (trim(text).empty()) {
                    v = Decimal();
                    return;
                }
                auto d = Decimal::parse(text);
                if (!d) throw Error(std::string(field.key) + ": '" + std::string(text) + "' is not a number");
                v = *d;
            } else if constexpr (std::is_same_v<V, std::optional<Date>>) {
                if (trim(text).empty()) {
                    v.reset();
                    return;
                }
                auto d = Date::parse(text);
                if (!d) throw Error(std::string(field.key) + ": '" + std::string(text) + "' is not a date (YYYY-MM-DD)");
                v = *d;
            } else if constexpr (std::is_same_v<V, bool>) {
                v = parseBool(text);
            } else if constexpr (std::is_same_v<V, int>) {
                auto n = parseInt(trim(text));
                if (!n || *n < 0 || *n > 100000000)
                    throw Error(std::string(field.key) + ": '" + std::string(text) + "' is not a whole number");
                v = static_cast<int>(*n);
            } else {
                v = parseChoice<V>(trim(text));
            }
        },
        field.member);
}

#define OPENPRACTICE_INSTANTIATE(T)                                   \
    template std::string getField<T>(const T&, const Field<T>&);      \
    template void setField<T>(T&, const Field<T>&, std::string_view);

OPENPRACTICE_INSTANTIATE(Firm)
OPENPRACTICE_INSTANTIATE(Staff)
OPENPRACTICE_INSTANTIATE(Client)
OPENPRACTICE_INSTANTIATE(Project)
OPENPRACTICE_INSTANTIATE(Phase)
OPENPRACTICE_INSTANTIATE(Task)
OPENPRACTICE_INSTANTIATE(TimeEntry)
OPENPRACTICE_INSTANTIATE(Sheet)
OPENPRACTICE_INSTANTIATE(Rfi)
OPENPRACTICE_INSTANTIATE(Submittal)
OPENPRACTICE_INSTANTIATE(ChangeOrder)
OPENPRACTICE_INSTANTIATE(Invoice)

#undef OPENPRACTICE_INSTANTIATE

// ------------------------------------------------------------------ practice

namespace {

template <class T>
const T* findById(const std::vector<T>& items, int id) {
    if (id == 0) return nullptr;
    for (const auto& i : items) {
        if (i.id == id) return &i;
    }
    return nullptr;
}

}  // namespace

const Staff* Practice::findStaff(int id) const { return findById(staff, id); }
const Client* Practice::findClient(int id) const { return findById(clients, id); }
const Project* Practice::findProject(int id) const { return findById(projects, id); }
const Phase* Practice::findPhase(int id) const { return findById(phases, id); }
const Task* Practice::findTask(int id) const { return findById(tasks, id); }
Project* Practice::findProject(int id) { return const_cast<Project*>(findById(projects, id)); }
Phase* Practice::findPhase(int id) { return const_cast<Phase*>(findById(phases, id)); }

std::string Practice::refName(Ref kind, int id) const {
    if (id == 0 || kind == Ref::None) return {};
    const std::string missing = "(missing #" + std::to_string(id) + ")";
    switch (kind) {
        case Ref::Staff:
            if (auto s = findStaff(id)) return s->name;
            return missing;
        case Ref::Client:
            if (auto c = findClient(id)) return c->name;
            return missing;
        case Ref::Project:
            if (auto p = findProject(id)) return trim(p->number + " " + p->name);
            return missing;
        case Ref::Phase:
            if (auto p = findPhase(id)) return trim(p->code + " " + p->name);
            return missing;
        case Ref::Task:
            if (auto t = findTask(id)) return t->name;
            return missing;
        case Ref::None: break;
    }
    return {};
}

std::vector<std::string> Practice::referencesTo(Ref kind, int id) const {
    std::vector<std::string> out;
    if (id == 0) return out;
    forEachList(*this, [&](const auto& s, const auto& items) {
        int count = 0;
        for (const auto& item : items) {
            // A project's own records are deleted with it, not references that block it.
            if (kind == Ref::Project && projectOf(item) == id) continue;
            using R = std::decay_t<decltype(item)>;
            for (const auto& f : s.fields) {
                if (f.ref != kind) continue;
                const auto* member = std::get_if<int R::*>(&f.member);
                if (member && item.**member == id) {
                    ++count;
                    break;
                }
            }
        }
        if (count > 0) {
            std::string what = count == 1 ? s.title : s.plural;
            out.push_back(std::to_string(count) + " " + toLower(what));
        }
    });
    return out;
}

void Practice::clearReferences(Ref kind, int id) {
    if (id == 0) return;
    forEachList(*this, [&](const auto& s, auto& items) {
        for (auto& item : items) {
            using R = std::decay_t<decltype(item)>;
            for (const auto& f : s.fields) {
                if (f.ref != kind) continue;
                if (const auto* member = std::get_if<int R::*>(&f.member)) {
                    if (item.**member == id) item.**member = 0;
                }
            }
        }
    });
}

void Practice::removeProject(int id) {
    forEachList(*this, [&](const auto&, auto& items) {
        items.erase(std::remove_if(items.begin(), items.end(), [&](const auto& item) { return projectOf(item) == id; }),
                    items.end());
    });
}

int Practice::nextRfiNumber(int projectId) const {
    int n = 0;
    for (const auto& r : rfis) {
        if (r.projectId == projectId) n = std::max(n, r.number);
    }
    return n + 1;
}

int Practice::nextChangeNumber(int projectId) const {
    int n = 0;
    for (const auto& c : changes) {
        if (c.projectId == projectId) n = std::max(n, c.number);
    }
    return n + 1;
}

std::string Practice::nextInvoiceNumber(int projectId) const {
    const Project* p = findProject(projectId);
    int n = 0;
    for (const auto& i : invoices) {
        if (i.projectId == projectId) ++n;
    }
    const std::string base = p && !trim(p->number).empty() ? trim(p->number) : "INV";
    char seq[16];
    std::snprintf(seq, sizeof seq, "%02d", n + 1);
    return base + "-" + seq;
}

void Practice::addStandardPhases(int projectId) {
    struct Std {
        const char* code;
        const char* name;
        int share;
    };
    static const Std kPhases[] = {
        {"SD", "Schematic Design", 15},       {"DD", "Design Development", 20}, {"CD", "Construction Documents", 40},
        {"BN", "Bidding / Negotiation", 5},   {"CA", "Construction Administration", 20},
    };
    for (const auto& k : kPhases) {
        Phase ph;
        ph.id = nextId(phases);
        ph.projectId = projectId;
        ph.code = k.code;
        ph.name = k.name;
        ph.feeShare = Decimal::fromInt(k.share);
        phases.push_back(ph);
    }
}

// ------------------------------------------------------------- serialization

namespace {

constexpr const char* kHeader = "OPENPRACTICE 1";

std::string escape(std::string_view s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c;
        }
    }
    return out;
}

std::string unescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 == s.size()) {
            out += s[i];
            continue;
        }
        const char n = s[++i];
        out += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
    }
    return out;
}

// Writes only fields that differ from a default-constructed record.
template <class T>
std::string recordLine(const Schema<T>& s, const T& record) {
    static const T blank{};
    std::string line = s.tag;
    for (const auto& f : s.fields) {
        const std::string value = getField(record, f);
        if (value == getField(blank, f)) continue;
        line += '\t';
        line += f.key;
        line += '=';
        line += escape(value);
    }
    return line;
}

template <class T>
void applyFields(T& record, const Schema<T>& s, const std::vector<std::string>& parts, int lineNo) {
    for (std::size_t i = 1; i < parts.size(); ++i) {
        const auto eq = parts[i].find('=');
        if (eq == std::string::npos) throw Error("line " + std::to_string(lineNo) + ": expected key=value");
        const std::string key = parts[i].substr(0, eq);
        const Field<T>* f = nullptr;
        for (const auto& candidate : s.fields) {
            if (key == candidate.key) f = &candidate;
        }
        if (!f) throw Error("line " + std::to_string(lineNo) + ": unknown field '" + key + "' in " + parts[0]);
        try {
            setField(record, *f, unescape(std::string_view(parts[i]).substr(eq + 1)));
        } catch (const Error& e) {
            throw Error("line " + std::to_string(lineNo) + ": " + e.what());
        }
    }
}

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("cannot open '" + path.u8string() + "'");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

std::string Practice::serialize() const {
    std::string out = std::string(kHeader) + "\n";
    out += recordLine(schema<Firm>(), firm) + "\n";
    forEachList(*this, [&](const auto& s, const auto& items) {
        for (const auto& item : items) out += recordLine(s, item) + "\n";
    });
    return out;
}

Practice Practice::parse(std::string_view text) {
    Practice p;
    std::istringstream in{std::string(text)};
    std::string line;
    int lineNo = 0;
    bool sawHeader = false;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (lineNo == 1 && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line.erase(0, 3);  // BOM
        if (!sawHeader) {
            if (line != kHeader) throw Error("not an OpenPractice file (missing '" + std::string(kHeader) + "' header)");
            sawHeader = true;
            continue;
        }
        if (trim(line).empty() || line[0] == '#') continue;
        const std::vector<std::string> parts = split(line, '\t');
        const std::string& tag = parts[0];
        bool handled = false;
        if (tag == schema<Firm>().tag) {
            applyFields(p.firm, schema<Firm>(), parts, lineNo);
            handled = true;
        }
        forEachList(p, [&](const auto& s, auto& items) {
            if (handled || tag != s.tag) return;
            items.emplace_back();
            applyFields(items.back(), s, parts, lineNo);
            if (items.back().id <= 0) throw Error("line " + std::to_string(lineNo) + ": " + tag + " without an id");
            for (std::size_t i = 0; i + 1 < items.size(); ++i) {
                if (items[i].id == items.back().id)
                    throw Error("line " + std::to_string(lineNo) + ": duplicate " + tag + " id " +
                                std::to_string(items.back().id));
            }
            handled = true;
        });
        if (!handled) throw Error("line " + std::to_string(lineNo) + ": unknown record type '" + tag + "'");
    }
    if (!sawHeader) throw Error("empty file");
    return p;
}

void Practice::save(const std::string& path) const {
    const fs::path target = fs::u8path(path);
    fs::path tmp = target;
    tmp += ".tmp";
    fs::path backup = target;
    backup += ".bak";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw Error("cannot write '" + tmp.u8string() + "'");
        out << serialize();
        out.flush();
        if (!out) throw Error("failed while writing '" + tmp.u8string() + "'");
    }
    std::error_code ec;
    if (fs::exists(target, ec)) fs::copy_file(target, backup, fs::copy_options::overwrite_existing, ec);
    fs::rename(tmp, target, ec);
    if (ec) {  // some platforms refuse to rename over an existing file
        fs::remove(target, ec);
        ec.clear();
        fs::rename(tmp, target, ec);
        if (ec) throw Error("cannot replace '" + target.u8string() + "': " + ec.message());
    }
}

Practice Practice::load(const std::string& path) { return parse(readFile(fs::u8path(path))); }

}  // namespace op
