# OpenPractice

**Free, open source project management for architects and engineers, in modern C++.**

OpenPractice runs the business side of an A/E firm's projects: fees by phase, budget hours, earned value,
profit and multiplier, schedules with dependencies, weekly timesheets, the drawing log, RFIs, submittals,
change orders and invoices. Everything lives in one plain-text file on your computer. There's no account,
no cloud, no subscription and no dependency beyond a C++17 compiler.

> Status: 0.1. Single user, single file. See [limits](#limits) and the [roadmap](#roadmap).

## Desktop app

`openpractice-gui` is a native desktop app built with [Dear ImGui](https://github.com/ocornut/imgui), running on
Win32 + Direct3D 11 on Windows and GLFW + OpenGL 3 on Linux and macOS. It shares its GUI framework (platform
layer, theme, widgets, autosave and error recovery) with OpenTax and OpenBooks.

- **Dashboard:** active projects, backlog, earned fee, unbilled work, receivables and this week's hours; each
  project's progress, hours used, profit, multiplier and health; a *Needs attention* list with a link to fix
  each item; everything due in the next three weeks; and how loaded each person is this week.
- **Projects:** each project has tabs for:
  - **Overview:** contract terms (fixed fee, hourly, hourly not-to-exceed or percentage of construction),
    fee, earned value, profit, net multiplier and hours, with per-phase progress.
  - **Phases & fee:** the standard AIA B101 phases (SD 15%, DD 20%, CD 40%, BN 5%, CA 20%) or your own, each
    with its share of the fee, budget hours and % complete (edited right in the table), and the fee,
    earned value, actual hours, labor cost and health that follow.
  - **Schedule:** a Gantt chart of phases and tasks with milestones, dependency arrows and a today line.
    Drag a bar to move a task; drag its end to change the due date. Tasks that start before their
    predecessor finishes are flagged.
  - **Tasks, Drawings, RFIs, Submittals, Change orders, Invoices, Time:** sortable, searchable tables
    with an editor beside them. Issue a drawing set in one step (status, date and revision), see RFI
    and submittal ages, invoice earned-but-unbilled fee with one click, and record payments.
- **Timesheet:** one person's week as a grid of project/phase rows by day, with daily and weekly
  totals against their capacity. Copy last week's rows to start a new week.
- **Team:** billing and cost rates, weekly capacity and four-week utilization for each person.
- **Clients**, **Reports** (PDF and CSV, below) and **Firm settings** (overhead rate and target
  multiplier).

Everything saves automatically. Open a file with `openpractice-gui path\to\firm.opp`, or use File > Open.
To look around first, choose **Explore a sample firm** on the start screen: a nine-person firm with a
library in construction documents, a mixed-use building in design development and a school
modernization under construction.

## How the numbers work

These are the standard definitions used in A/E practice management. All money is exact integer cents and
all hours and rates are fixed-point decimals; nothing uses floating point.

| Figure | Definition |
| --- | --- |
| Contract fee | The fee; or construction cost × fee % for a percentage-of-construction fee |
| Total fee | Contract fee + approved change orders |
| Phase fee | Total fee × the phase's share |
| Earned fee | Fixed and percentage fees: Σ phase fee × phase % complete. Hourly: billable hours × billing rates, capped at a not-to-exceed amount |
| Direct labor | Σ hours × each person's cost rate |
| Total cost | Direct labor × (1 + firm overhead rate) |
| Profit | Earned fee − total cost |
| Net multiplier | Earned fee ÷ direct labor |
| Hours used | Actual hours ÷ budget hours (phase budgets + approved change order hours) |
| Unbilled | Earned fee − invoiced (negative when billed ahead of progress) |
| Receivable | Invoiced (sent and paid) − collected |
| Utilization | Billable hours ÷ (weekly capacity × weeks) |

**Health** compares spending with progress. On a fixed fee it's *On track* while the share of budget hours
used is within 5 points of % complete, *Watch* up to 15 points over, and *Over budget* beyond that (or when
the budget is spent before the work is 95% done). Without budget hours, cost with overhead as a share of the
fee is used instead. Hourly not-to-exceed work turns *Watch* at 85% of the cap and *Over budget* at 100%.

The *Needs attention* review checks for phase shares that don't add to 100%, projects over budget, overdue
tasks, RFIs and submittals, past-due invoices, dependency loops, tasks starting before their predecessor,
time charged to another project's phase, people with time but no rates, and more.

## Reports

- **Project status report (PDF):** fee and budget summary, phase table, open tasks, open RFIs, submittals in
  review, drawing log, change orders and invoices.
- **Portfolio report (PDF):** every project's fee, progress, profit, multiplier and health, firm totals,
  four-week staff utilization and everything that needs attention.
- **CSV export** of any list, for one project or all, with names instead of ids and plain numbers for
  amounts. Cells that a spreadsheet would run as a formula are neutralized.

PDFs are written by a small built-in writer: standard fonts only, no scripts, links or attachments, and
every piece of text is escaped. Output is deterministic.

## Building

You need CMake 3.16+ and a C++17 compiler (GCC 9+, Clang 10+ or MSVC 2019+). Dear ImGui is vendored in
`third_party/`; nothing is downloaded.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

- **Windows:** MSVC or MinGW-w64. MinGW builds are linked statically, so the `.exe` files run anywhere.
- **Linux:** install GLFW first (`sudo apt install libglfw3-dev` or `sudo dnf install glfw-devel`).
- **macOS:** `brew install glfw`.
- `-DOPENPRACTICE_BUILD_GUI=OFF` builds only the engine, command line and tests.

## Command line

`openpractice` works on the same file as the desktop app, for scripts and quick entries:

```text
openpractice new firm.opp --firm "Studio North"     Create an empty practice file
openpractice sample sample.opp                      Create the sample practice
openpractice summary firm.opp                       Firm totals, every project, what needs attention
openpractice check firm.opp                         List problems (exit code 1 if any are errors)
openpractice log firm.opp --staff MC --project 2025-031 --phase CD --hours 3.5 --notes "Wall types"
openpractice export firm.opp rfis rfis.csv --project 2024-018
openpractice report firm.opp 2025-031 status.pdf    Project status report
openpractice portfolio firm.opp portfolio.pdf       Portfolio report
```

Projects can be named by number, id or part of the name; people by initials or full name.

## File format

A practice file (`.opp`) is UTF-8 text: an `OPENPRACTICE 1` header, then one record per line, a tag followed
by tab-separated `key=value` fields. Only fields that differ from their defaults are written; tabs, newlines
and backslashes in values are escaped.

```text
OPENPRACTICE 1
FIRM	name=Studio North	overhead=150
STAFF	id=1	name=Ana Ruiz	initials=AR	role=architect	billing_rate=150.00	cost_rate=50.00
PROJECT	id=1	number=2026-001	name=Pavilion	client=1	fee=100000.00	status=active
PHASE	id=1	project=1	code=SD	name=Schematic Design	fee_share=15	budget_hours=100	complete=40
TIME	id=1	date=2026-03-02	staff=1	project=1	phase=1	hours=7.5
```

Records refer to each other by id. Unknown record types or fields, duplicate ids and invalid values are
errors, so nothing is silently dropped. Saves are atomic (write a temporary file, keep the previous version
as `.bak`, then rename). The format is easy to diff, back up and keep in version control.

## Project layout

```text
include/openpractice/   the engine's public headers
  model.hpp             records, schemas, the Practice and its file format
  calc.hpp              earned value, cost, profit, health, utilization and the review
  report.hpp            CSV, PDF reports and the text summary
  format.hpp            display formatting shared by the GUI and reports
  money.hpp date.hpp    exact money, decimals and dates
src/                    the engine, the sample practice and the command line
gui/                    the desktop app
  app.cpp               lifecycle, autosave, menus, sidebar, welcome screen
  app_dashboard.cpp     the dashboard
  app_projects.cpp      projects and the project tabs
  app_schedule.cpp      the Gantt chart
  app_firm.cpp          timesheet, team, clients, reports, firm settings
  record_table.hpp      the schema-driven table + editor used by most screens
  widgets.*, theme.*    controls and styling
  platform_*, main_*    per-platform window, file dialogs and fonts
tests/                  self-contained test suite (no framework)
third_party/imgui/      Dear ImGui 1.92.9b (MIT), unmodified; the demo file is left out
```

Every record type is declared once, as a struct plus a schema of its fields. The schema drives the file
format, CSV export, the tables and the editors, so adding a field is a one-line change.

## Limits

- One user at a time on a file. Keep it on a shared drive or in Git if several people need it, but don't
  open it in two places at once.
- No expense or consultant tracking, no multi-currency, no resource leveling or critical-path calculation.
- Invoices track amounts and payments; OpenPractice doesn't produce invoice documents or post to an
  accounting system (export CSV for that).

## Roadmap

- Reimbursable expenses and consultant fees.
- Resource planning: hours planned by person and week, against capacity.
- Invoice PDFs and an export to OpenBooks.
- Optional password encryption of practice files, as in OpenTax.

## License

MIT. See [LICENSE](LICENSE). Dear ImGui is MIT licensed by Omar Cornut and contributors.
