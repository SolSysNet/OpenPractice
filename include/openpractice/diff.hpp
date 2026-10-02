#pragma once

// What changed between two versions of a practice, record by record: used by the desktop
// app to show uncommitted changes before they are written to the file.

#include "openpractice/model.hpp"

#include <string>
#include <vector>

namespace op {

struct RecordChange {
    enum class Kind { Added, Removed, Modified };
    Kind kind = Kind::Modified;
    std::string type;                 // the record type, e.g. "Task" or "Firm"
    std::string name;                 // the record's name, number or title, e.g. "MEP coordination model"
    int projectId = 0;                // the project the record belongs to (0 for firm-wide records)
    std::vector<std::string> fields;  // labels of changed fields (Modified only)
};

// Changes that turn `before` into `after`, in file order. Records are matched by id.
std::vector<RecordChange> diffPractice(const Practice& before, const Practice& after);

// One line describing a change, e.g. "Modified task \"Door schedule\": Status, Due".
std::string describe(const RecordChange& change);

}  // namespace op
