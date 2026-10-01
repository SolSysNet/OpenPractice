#pragma once

// Output: CSV export of any list, PDF reports, and a plain-text summary.

#include "openpractice/model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace op {

// Names accepted by exportCsv: "staff", "clients", "projects", "phases", "tasks", "time",
// "sheets", "rfis", "submittals", "changes", "invoices".
std::vector<std::string> listNames();
// Display title of a list name ("rfis" -> "RFIs"); empty if unknown.
std::string listTitle(std::string_view name);

// CSV (RFC 4180, UTF-8) of one list, with references shown by name. projectId 0 = all
// projects. Throws op::Error for an unknown list.
std::string exportCsv(const Practice& practice, std::string_view listName, int projectId = 0);

// A project status report: fee and budget, phases, open tasks, RFIs, submittals, drawing log,
// change orders and invoices. Letter size.
std::string projectReportPdf(const Practice& practice, int projectId, Date today);
// Every project on one page or more, with firm totals and staff utilization.
std::string portfolioReportPdf(const Practice& practice, Date today);

// Plain-text overview for the command line.
std::string practiceSummaryText(const Practice& practice, Date today);

}  // namespace op
