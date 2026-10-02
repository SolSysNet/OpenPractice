#include "openpractice/diff.hpp"

#include "openpractice/util.hpp"

#include <cctype>
#include <map>
#include <type_traits>

namespace op {

namespace {

// The list of T in a practice.
template <class T>
const std::vector<T>& listOf(const Practice& p) {
    if constexpr (std::is_same_v<T, Staff>) return p.staff;
    else if constexpr (std::is_same_v<T, Client>) return p.clients;
    else if constexpr (std::is_same_v<T, Project>) return p.projects;
    else if constexpr (std::is_same_v<T, Phase>) return p.phases;
    else if constexpr (std::is_same_v<T, Task>) return p.tasks;
    else if constexpr (std::is_same_v<T, TimeEntry>) return p.time;
    else if constexpr (std::is_same_v<T, Sheet>) return p.sheets;
    else if constexpr (std::is_same_v<T, Rfi>) return p.rfis;
    else if constexpr (std::is_same_v<T, Submittal>) return p.submittals;
    else if constexpr (std::is_same_v<T, ChangeOrder>) return p.changes;
    else return p.invoices;
}

// A short name for a record: its first non-empty text field, or something descriptive for
// records without one.
template <class T>
std::string nameOf(const Practice& p, const T& record) {
    if constexpr (std::is_same_v<T, TimeEntry>) {
        std::string who = p.refName(Ref::Staff, record.staffId);
        return trim((record.date ? record.date->str() : std::string()) + " " + who + " " + record.hours.str() + " h");
    } else {
        for (const auto& f : schema<T>().fields) {
            if (const auto* member = std::get_if<std::string T::*>(&f.member)) {
                if (!trim(record.**member).empty()) {
                    std::string s = trim(record.**member);
                    if (s.size() > 60) s = s.substr(0, 57) + "...";
                    return s;
                }
            }
        }
        return "#" + std::to_string(record.id);
    }
}

template <class T>
std::vector<std::string> changedFields(const T& a, const T& b) {
    std::vector<std::string> out;
    for (const auto& f : schema<T>().fields) {
        if (getField(a, f) != getField(b, f)) out.push_back(f.label);
    }
    return out;
}

}  // namespace

std::vector<RecordChange> diffPractice(const Practice& before, const Practice& after) {
    std::vector<RecordChange> changes;

    if (auto fields = changedFields(before.firm, after.firm); !fields.empty())
        changes.push_back({RecordChange::Kind::Modified, schema<Firm>().title, after.firm.name, 0, std::move(fields)});

    forEachList(after, [&](const auto& s, const auto& now) {
        using T = typename std::decay_t<decltype(now)>::value_type;
        const auto& was = listOf<T>(before);
        std::map<int, const T*> old;
        for (const auto& r : was) old[r.id] = &r;
        std::map<int, bool> seen;
        for (const auto& r : now) {
            seen[r.id] = true;
            auto it = old.find(r.id);
            if (it == old.end()) {
                changes.push_back({RecordChange::Kind::Added, s.title, nameOf(after, r), projectOf(r), {}});
            } else if (auto fields = changedFields(*it->second, r); !fields.empty()) {
                changes.push_back({RecordChange::Kind::Modified, s.title, nameOf(after, r), projectOf(r), std::move(fields)});
            }
        }
        for (const auto& r : was) {
            if (!seen.count(r.id)) changes.push_back({RecordChange::Kind::Removed, s.title, nameOf(before, r), projectOf(r), {}});
        }
    });
    return changes;
}

std::string describe(const RecordChange& c) {
    const char* verb = c.kind == RecordChange::Kind::Added ? "Added" : c.kind == RecordChange::Kind::Removed ? "Deleted" : "Changed";
    std::string type = c.type;
    if (type.size() > 1 && !std::isupper(static_cast<unsigned char>(type[1])))
        type[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(type[0])));
    std::string out = std::string(verb) + " " + type;
    if (!c.name.empty()) out += " \"" + c.name + "\"";
    if (!c.fields.empty()) out += ": " + join(c.fields, ", ");
    return out;
}

}  // namespace op
