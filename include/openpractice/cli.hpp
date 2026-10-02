#pragma once

#include "openpractice/date.hpp"

#include <iosfwd>
#include <string>
#include <vector>

namespace op {

constexpr const char* kVersion = "0.2.0";

// Runs one OpenPractice command line (argv without the program name). Returns the exit
// code. `today` is the date used for overdue checks and new records.
int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err, Date today = Date::today());

}  // namespace op
