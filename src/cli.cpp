#include "openpractice/cli.hpp"

#include "openpractice/calc.hpp"
#include "openpractice/format.hpp"
#include "openpractice/model.hpp"
#include "openpractice/report.hpp"
#include "openpractice/util.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <ostream>

namespace op {

namespace fs = std::filesystem;

namespace {

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;  // --key value
};

Args parseArgs(const std::vector<std::string>& args, std::size_t from) {
    Args a;
    for (std::size_t i = from; i < args.size(); ++i) {
        const std::string& s = args[i];
        if (startsWith(s, "--") && s.size() > 2) {
            const std::string key = s.substr(2);
            if (i + 1 >= args.size()) throw Error("--" + key + " needs a value");
            a.options[key] = args[++i];
        } else {
            a.positional.push_back(s);
        }
    }
    return a;
}

void writeFile(const std::string& path, const std::string& bytes) {
    std::ofstream out(fs::u8path(path), std::ios::binary | std::ios::trunc);
    out << bytes;
    if (!out) throw Error("cannot write '" + path + "'");
}

const Project& findProject(const Practice& p, const std::string& key) {
    for (const auto& pr : p.projects) {
        if (iequals(pr.number, key) || std::to_string(pr.id) == key) return pr;
    }
    for (const auto& pr : p.projects) {
        if (toLower(pr.name).find(toLower(key)) != std::string::npos) return pr;
    }
    throw Error("no project matching '" + key + "'");
}

const Staff& findStaff(const Practice& p, const std::string& key) {
    for (const auto& s : p.staff) {
        if (iequals(s.initials, key) || iequals(s.name, key) || std::to_string(s.id) == key) return s;
    }
    throw Error("no staff member matching '" + key + "' (use initials or full name)");
}

void requireCount(const Args& a, std::size_t n, const char* usage) {
    if (a.positional.size() != n) throw Error(std::string("usage: openpractice ") + usage);
}

void printHelp(std::ostream& out) {
    out << "OpenPractice " << kVersion << " - open source project management for architects and engineers\n\n"
           "Usage: openpractice <command> [arguments]\n\n"
           "  new <file> [--firm NAME]           Create an empty practice file\n"
           "  sample <file>                      Create a practice filled with sample projects\n"
           "  summary <file>                     Firm totals, every project, and what needs attention\n"
           "  check <file>                       List problems (exit code 1 if any are errors)\n"
           "  export <file> <list> [out.csv] [--project NUMBER]\n"
           "                                     Export a list as CSV (to stdout without out.csv).\n"
           "                                     Lists: staff clients projects phases tasks time sheets\n"
           "                                            rfis submittals changes invoices\n"
           "  report <file> <project> <out.pdf>  Project status report (project number, id or name)\n"
           "  portfolio <file> <out.pdf>         Portfolio report of every project\n"
           "  log <file> --staff WHO --project NUMBER --hours H [--phase CODE] [--date YYYY-MM-DD]\n"
           "      [--notes TEXT] [--nonbillable yes]\n"
           "                                     Record time\n"
           "  version                            Show the version\n\n"
           "The desktop app is openpractice-gui.\n";
}

}  // namespace

int runCli(const std::vector<std::string>& args, std::ostream& out, std::ostream& err, Date today) {
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        printHelp(out);
        return args.empty() ? 1 : 0;
    }
    const std::string& cmd = args[0];
    try {
        const Args a = parseArgs(args, 1);
        if (cmd == "version" || cmd == "--version") {
            out << "openpractice " << kVersion << "\n";
            return 0;
        }
        if (cmd == "new" || cmd == "sample") {
            requireCount(a, 1, cmd == "new" ? "new <file> [--firm NAME]" : "sample <file>");
            const std::string& path = a.positional[0];
            std::error_code ec;
            if (fs::exists(fs::u8path(path), ec)) throw Error("'" + path + "' already exists");
            Practice p = cmd == "sample" ? samplePractice(today) : Practice{};
            if (auto it = a.options.find("firm"); it != a.options.end()) p.firm.name = it->second;
            p.save(path);
            out << "Created " << path << "\n";
            return 0;
        }
        if (cmd == "summary") {
            requireCount(a, 1, "summary <file>");
            out << practiceSummaryText(Practice::load(a.positional[0]), today);
            return 0;
        }
        if (cmd == "check") {
            requireCount(a, 1, "check <file>");
            const auto issues = reviewPractice(Practice::load(a.positional[0]), today);
            bool errors = false;
            for (const auto& i : issues) {
                const char* tag = i.severity == Severity::Error ? "error" : i.severity == Severity::Warning ? "warning" : "note";
                errors |= i.severity == Severity::Error;
                out << tag << ": " << i.message << "\n";
            }
            if (issues.empty()) out << "No problems found.\n";
            return errors ? 1 : 0;
        }
        if (cmd == "export") {
            if (a.positional.size() < 2 || a.positional.size() > 3)
                throw Error("usage: openpractice export <file> <list> [out.csv] [--project NUMBER]");
            const Practice p = Practice::load(a.positional[0]);
            int projectId = 0;
            if (auto it = a.options.find("project"); it != a.options.end()) projectId = findProject(p, it->second).id;
            const std::string csv = exportCsv(p, a.positional[1], projectId);
            if (a.positional.size() == 3) {
                writeFile(a.positional[2], csv);
                out << "Wrote " << a.positional[2] << "\n";
            } else {
                out << csv;
            }
            return 0;
        }
        if (cmd == "report") {
            requireCount(a, 3, "report <file> <project> <out.pdf>");
            const Practice p = Practice::load(a.positional[0]);
            writeFile(a.positional[2], projectReportPdf(p, findProject(p, a.positional[1]).id, today));
            out << "Wrote " << a.positional[2] << "\n";
            return 0;
        }
        if (cmd == "portfolio") {
            requireCount(a, 2, "portfolio <file> <out.pdf>");
            writeFile(a.positional[1], portfolioReportPdf(Practice::load(a.positional[0]), today));
            out << "Wrote " << a.positional[1] << "\n";
            return 0;
        }
        if (cmd == "log") {
            requireCount(a, 1, "log <file> --staff WHO --project NUMBER --hours H [--phase CODE] [--date D]");
            const std::string& path = a.positional[0];
            Practice p = Practice::load(path);
            auto need = [&](const char* key) -> const std::string& {
                auto it = a.options.find(key);
                if (it == a.options.end()) throw Error(std::string("log needs --") + key);
                return it->second;
            };
            TimeEntry t;
            t.id = Practice::nextId(p.time);
            t.staffId = findStaff(p, need("staff")).id;
            t.projectId = findProject(p, need("project")).id;
            auto hours = Decimal::parse(need("hours"));
            if (!hours || *hours <= Decimal() || *hours > Decimal::fromInt(24)) throw Error("--hours must be between 0 and 24");
            t.hours = *hours;
            t.date = today;
            if (auto it = a.options.find("date"); it != a.options.end()) {
                t.date = Date::parse(it->second);
                if (!t.date) throw Error("'" + it->second + "' is not a date");
            }
            if (auto it = a.options.find("phase"); it != a.options.end()) {
                for (const auto& ph : p.phases) {
                    if (ph.projectId == t.projectId && iequals(ph.code, it->second)) t.phaseId = ph.id;
                }
                if (t.phaseId == 0) throw Error("project has no phase '" + it->second + "'");
            }
            if (auto it = a.options.find("notes"); it != a.options.end()) t.notes = it->second;
            if (auto it = a.options.find("nonbillable"); it != a.options.end()) t.billable = !iequals(it->second, "yes");
            p.time.push_back(t);
            p.save(path);
            out << "Logged " << t.hours.str() << " h for " << p.refName(Ref::Staff, t.staffId) << " on "
                << p.refName(Ref::Project, t.projectId) << " (" << t.date->str() << ")\n";
            return 0;
        }
        err << "Unknown command '" << cmd << "'. Run 'openpractice help'.\n";
        return 1;
    } catch (const std::exception& e) {
        err << "openpractice: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace op
