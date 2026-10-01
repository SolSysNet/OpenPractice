#pragma once

// The practice data model: everything a firm enters, and nothing that is computed.
//
// A practice file holds one firm: its staff, clients and projects, and for each project its
// phases, tasks, time, drawing sheets, RFIs, submittals, change orders and invoices.
//
// Each record type is a plain struct plus a schema: a list of fields with a file key, a
// label, help text and a pointer to the struct member. The schema drives the file format,
// the command line, CSV export and the desktop app's tables and editors, so a field is
// declared exactly once. Fields that point at another record (a task's assignee, a time
// entry's phase) hold that record's id and name the kind of record they refer to.

#include "openpractice/date.hpp"
#include "openpractice/money.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace op {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ------------------------------------------------------------------ enums

enum class ProjectStatus { Proposal, Active, OnHold, Complete, Cancelled };
enum class FeeType { FixedFee, HourlyNotToExceed, Hourly, PercentOfConstruction };
enum class Role { Principal, ProjectManager, ProjectArchitect, Architect, Engineer, Designer, Drafter, Intern, Admin };
enum class TaskStatus { NotStarted, InProgress, Blocked, Done };
enum class Discipline {
    General,
    Civil,
    Landscape,
    Architectural,
    Interiors,
    Structural,
    Mechanical,
    Electrical,
    Plumbing,
    FireProtection,
};
enum class SheetStatus { InProgress, ForReview, Issued, Superseded };
enum class RfiStatus { Open, Answered, Closed };
enum class SubmittalStatus { Pending, UnderReview, Approved, ApprovedAsNoted, ReviseResubmit, Rejected };
enum class ChangeStatus { Proposed, Approved, Rejected };
enum class InvoiceStatus { Draft, Sent, Paid };

struct Choice {
    const char* key;    // stored in files and typed on the command line
    const char* label;  // shown to people
};

const std::vector<Choice>& choices(ProjectStatus);
const std::vector<Choice>& choices(FeeType);
const std::vector<Choice>& choices(Role);
const std::vector<Choice>& choices(TaskStatus);
const std::vector<Choice>& choices(Discipline);
const std::vector<Choice>& choices(SheetStatus);
const std::vector<Choice>& choices(RfiStatus);
const std::vector<Choice>& choices(SubmittalStatus);
const std::vector<Choice>& choices(ChangeStatus);
const std::vector<Choice>& choices(InvoiceStatus);

template <class E>
const char* choiceLabel(E value) {
    return choices(value)[static_cast<std::size_t>(value)].label;
}

// Sheet-number prefix for a discipline per the US National CAD Standard ("A", "S", ...).
const char* disciplinePrefix(Discipline d);

// ---------------------------------------------------------------- records
// Money and Decimal fields default to zero, which also means "blank". Reference fields
// hold the id of another record, or 0 for none.

struct Firm {
    std::string name;
    std::string address;
    std::string phone;
    std::string email;
    Decimal overheadRate = Decimal::fromInt(150);  // indirect cost as a % of direct labor
    Decimal targetMultiplier = Decimal::fromRaw(30000);  // fee earned / direct labor goal
};

struct Staff {
    int id = 0;
    std::string name;
    std::string initials;
    Role role = Role::Architect;
    std::string license;           // e.g. "RA", "PE", "SE"
    std::string email;
    Money billingRate;             // per hour, charged to clients
    Money costRate;                // per hour, direct salary cost
    Decimal weeklyCapacity = Decimal::fromInt(40);
    bool active = true;
};

struct Client {
    int id = 0;
    std::string name;
    std::string contact;
    std::string email;
    std::string phone;
    std::string address;
    std::string notes;
};

struct Project {
    int id = 0;
    std::string number;            // e.g. "2026-014"
    std::string name;
    int clientId = 0;
    int managerId = 0;
    ProjectStatus status = ProjectStatus::Active;
    std::string location;
    std::string buildingType;      // e.g. "Office", "K-12 school"
    Decimal grossArea;             // square feet
    FeeType feeType = FeeType::FixedFee;
    Money fee;                     // fixed fee, not-to-exceed cap, or hourly estimate
    Money constructionCost;        // estimated cost of the work
    Decimal feePercent;            // for a percentage-of-construction fee
    std::optional<Date> start;
    std::optional<Date> end;
    std::string description;
};

struct Phase {
    int id = 0;
    int projectId = 0;
    std::string code;              // "SD", "DD", "CD", ...
    std::string name;
    Decimal feeShare;              // % of the contract fee allotted to this phase
    Decimal budgetHours;
    Decimal complete;              // % complete
    std::optional<Date> start;
    std::optional<Date> end;
};

struct Task {
    int id = 0;
    int projectId = 0;
    int phaseId = 0;
    std::string name;
    int assigneeId = 0;
    TaskStatus status = TaskStatus::NotStarted;
    std::optional<Date> start;
    std::optional<Date> due;
    Decimal estimateHours;
    int predecessorId = 0;         // a task that must finish first
    bool milestone = false;
    std::string notes;
};

struct TimeEntry {
    int id = 0;
    std::optional<Date> date;
    int staffId = 0;
    int projectId = 0;
    int phaseId = 0;
    Decimal hours;
    bool billable = true;
    std::string notes;
};

struct Sheet {
    int id = 0;
    int projectId = 0;
    std::string number;            // e.g. "A-101"
    std::string title;
    Discipline discipline = Discipline::Architectural;
    int phaseId = 0;
    int revision = 0;
    SheetStatus status = SheetStatus::InProgress;
    std::optional<Date> issued;
};

struct Rfi {
    int id = 0;
    int projectId = 0;
    int number = 0;
    std::string subject;
    std::string from;              // who asked: contractor, consultant
    std::optional<Date> received;
    std::optional<Date> due;
    RfiStatus status = RfiStatus::Open;
    int assigneeId = 0;
    std::string question;
    std::string response;
    std::optional<Date> answered;
};

struct Submittal {
    int id = 0;
    int projectId = 0;
    std::string number;            // e.g. "08 41 13-01"
    std::string specSection;       // e.g. "08 41 13 Aluminum storefronts"
    std::string description;
    std::string contractor;
    std::optional<Date> received;
    std::optional<Date> due;
    SubmittalStatus status = SubmittalStatus::Pending;
    int reviewerId = 0;
    std::optional<Date> returned;
};

struct ChangeOrder {
    int id = 0;
    int projectId = 0;
    int number = 0;
    std::string description;
    Money amount;                  // change to the fee (negative for a credit)
    Decimal hours;                 // added hours budget
    ChangeStatus status = ChangeStatus::Proposed;
    std::optional<Date> date;
};

struct Invoice {
    int id = 0;
    int projectId = 0;
    std::string number;
    std::optional<Date> date;
    std::optional<Date> due;
    Money amount;
    Money paid;
    InvoiceStatus status = InvoiceStatus::Draft;
    std::string notes;
};

// ---------------------------------------------------------------- schema

// What a reference field points at.
enum class Ref { None, Staff, Client, Project, Phase, Task };

template <class T>
using FieldMember = std::variant<std::string T::*, Money T::*, Decimal T::*, std::optional<Date> T::*, bool T::*, int T::*,
                                 ProjectStatus T::*, FeeType T::*, Role T::*, TaskStatus T::*, Discipline T::*,
                                 SheetStatus T::*, RfiStatus T::*, SubmittalStatus T::*, ChangeStatus T::*,
                                 InvoiceStatus T::*>;

template <class T>
struct Field {
    const char* key;
    const char* label;
    const char* help;
    FieldMember<T> member;
    Ref ref = Ref::None;     // for int members holding another record's id
    bool multiline = false;  // long free text (notes, questions)
};

template <class T>
struct Schema {
    const char* tag;     // file record type, e.g. "TASK"
    const char* name;    // command-line / CSV name, e.g. "tasks"
    const char* title;   // singular, e.g. "Task"
    const char* plural;  // e.g. "Tasks"
    std::vector<Field<T>> fields;
};

template <class T>
const Schema<T>& schema();
template <> const Schema<Firm>& schema<Firm>();
template <> const Schema<Staff>& schema<Staff>();
template <> const Schema<Client>& schema<Client>();
template <> const Schema<Project>& schema<Project>();
template <> const Schema<Phase>& schema<Phase>();
template <> const Schema<Task>& schema<Task>();
template <> const Schema<TimeEntry>& schema<TimeEntry>();
template <> const Schema<Sheet>& schema<Sheet>();
template <> const Schema<Rfi>& schema<Rfi>();
template <> const Schema<Submittal>& schema<Submittal>();
template <> const Schema<ChangeOrder>& schema<ChangeOrder>();
template <> const Schema<Invoice>& schema<Invoice>();

// Field values as text: money as plain cents ("1234.50"), decimals trimmed ("7.5"), dates
// as YYYY-MM-DD, booleans as "yes"/"no", choices by key, references by id. setField throws
// op::Error on invalid input.
template <class T>
std::string getField(const T& record, const Field<T>& field);
template <class T>
void setField(T& record, const Field<T>& field, std::string_view text);
template <class T>
const Field<T>* findField(std::string_view key) {
    for (const auto& f : schema<T>().fields) {
        if (key == f.key) return &f;
    }
    return nullptr;
}

template <class T, class = void>
struct HasProjectId : std::false_type {};
template <class T>
struct HasProjectId<T, std::void_t<decltype(std::declval<const T&>().projectId)>> : std::true_type {};

// The project a record belongs to (0 for firm-wide records).
template <class T>
int projectOf(const T& record) {
    if constexpr (std::is_same_v<T, Project>) return record.id;
    else if constexpr (HasProjectId<T>::value) return record.projectId;
    else return 0;
}

// ---------------------------------------------------------------- practice

struct Practice {
    Firm firm;
    std::vector<Staff> staff;
    std::vector<Client> clients;
    std::vector<Project> projects;
    std::vector<Phase> phases;
    std::vector<Task> tasks;
    std::vector<TimeEntry> time;
    std::vector<Sheet> sheets;
    std::vector<Rfi> rfis;
    std::vector<Submittal> submittals;
    std::vector<ChangeOrder> changes;
    std::vector<Invoice> invoices;

    // Lookups by id; null when missing (or id is 0).
    const Staff* findStaff(int id) const;
    const Client* findClient(int id) const;
    const Project* findProject(int id) const;
    const Phase* findPhase(int id) const;
    const Task* findTask(int id) const;
    Project* findProject(int id);
    Phase* findPhase(int id);

    // Display name of a referenced record: "Ana Ruiz", "2026-014 Riverside Library",
    // "SD Schematic Design". Empty for 0, "(missing #id)" for a dangling reference.
    std::string refName(Ref kind, int id) const;

    // Human-readable list of what refers to record `id` of `kind`, e.g.
    // {"12 time entries", "3 tasks"}. Records owned by a project don't count as references
    // to it; removeProject() deletes them.
    std::vector<std::string> referencesTo(Ref kind, int id) const;
    // Sets every reference to `id` of `kind` back to 0.
    void clearReferences(Ref kind, int id);
    // Deletes a project and everything it owns.
    void removeProject(int id);

    // The next free id in a list, and the next RFI / change order number in a project.
    template <class T>
    static int nextId(const std::vector<T>& items) {
        int id = 0;
        for (const auto& i : items) id = i.id > id ? i.id : id;
        return id + 1;
    }
    int nextRfiNumber(int projectId) const;
    int nextChangeNumber(int projectId) const;
    std::string nextInvoiceNumber(int projectId) const;

    // Adds the standard AIA B101 phases (SD 15%, DD 20%, CD 40%, BN 5%, CA 20%) to a project.
    void addStandardPhases(int projectId);

    // Serialization. The file is UTF-8 text: an "OPENPRACTICE 1" header, then one record per
    // line: TAG, then tab-separated key=value fields. Unknown tags or keys are errors, so
    // nothing is silently dropped.
    std::string serialize() const;
    static Practice parse(std::string_view text);

    // Atomic save (write temp, keep .bak, rename).
    void save(const std::string& path) const;
    static Practice load(const std::string& path);
};

// Calls f(schema, vector) for every list, in file order.
template <class F>
void forEachList(Practice& p, F&& f) {
    f(schema<Staff>(), p.staff);
    f(schema<Client>(), p.clients);
    f(schema<Project>(), p.projects);
    f(schema<Phase>(), p.phases);
    f(schema<Task>(), p.tasks);
    f(schema<TimeEntry>(), p.time);
    f(schema<Sheet>(), p.sheets);
    f(schema<Rfi>(), p.rfis);
    f(schema<Submittal>(), p.submittals);
    f(schema<ChangeOrder>(), p.changes);
    f(schema<Invoice>(), p.invoices);
}
template <class F>
void forEachList(const Practice& p, F&& f) {
    forEachList(const_cast<Practice&>(p), [&](const auto& s, auto& v) { f(s, std::as_const(v)); });
}

// A practice for trying the app: a small firm with three projects at different stages,
// with dates arranged around `today`.
Practice samplePractice(Date today);

}  // namespace op
