#pragma once

// The OpenPractice desktop application. Platform-independent; the platform main loop calls
// frame() once per frame.
//
// Screens edit the open practice directly through bound widgets. Every change bumps a
// version so project figures are recomputed (well under a millisecond for a typical firm),
// and the file is saved as soon as no field is being edited, so work is never lost.
//
// Edits that add or remove records are deferred to the end of the frame (defer()), so a
// screen never draws from a list that was reallocated under it.

#include "openpractice/calc.hpp"
#include "openpractice/model.hpp"
#include "platform.hpp"
#include "widgets.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace opgui {

enum class Screen { Dashboard, Projects, Project, Timesheet, Team, Clients, Reports, Firm };

enum class ProjectTab { Overview, Phases, Schedule, Tasks, Drawings, Rfis, Submittals, Changes, Invoices, Time };

struct ConfirmRequest {
    std::string title;
    std::string message;
    std::string button;
    std::function<void()> action;
};

struct NewPracticeForm {
    std::string firm;
    std::string path;
    std::string error;
};

// A computed column in a record table.
template <class T>
struct ExtraColumn {
    const char* header;
    std::function<std::string(const T&)> text;
    std::function<ImVec4(const T&)> color = {};  // default text color when empty
    bool right = false;
};

// How a list of records is shown and edited: a sortable, searchable table, with an editor
// panel for the selected record. See record_table.hpp.
template <class T>
struct TableSpec {
    const char* id;                          // keys selection and search state
    std::vector<const char*> columns;        // field keys, in order
    std::vector<ExtraColumn<T>> extras;      // shown after the field columns
    std::vector<const char*> inlineEdit;     // field columns edited right in the table
    std::function<bool(const T&)> filter;    // which records to list
    std::function<T()> make;                 // the Add button's new record; empty = no Add
    const char* addLabel = "Add";
    std::function<bool(const char* key)> hide;  // fields left out of the editor
    std::function<void(int id)> open;        // a row click opens this instead of the editor
    std::function<std::string(const T&)> deleteBlocker;  // non-empty: why it can't be deleted
    std::function<void(int id)> onDelete;    // cleanup after a delete (runs deferred)
    std::function<void()> toolbar;           // extra controls beside search
    std::function<void(T&)> editorExtras;    // drawn under the editor fields
    int sortColumn = 0;
    bool sortDescending = false;
};

class App {
public:
    explicit App(std::string initialPath);
    ~App();

    void frame();
    bool quitRequested() const { return quit_; }
    std::string windowTitle() const;
    bool wantsFrequentRedraw() const;

private:
    // ---- persistence (app.cpp)
    bool openPractice(std::string path);
    void closePractice();
    bool createPractice(NewPracticeForm& form, bool sample);
    void openSample();
    void changed();  // the open practice was edited
    void saveNow();
    void recalculate();
    void loadConfig();
    void saveConfig() const;
    void rememberRecent(std::string path);
    void chooseOpenFile();
    void drawFrame();
    void defer(std::function<void()> action);
    // Saves bytes produced by `make` through a save dialog (or a typed path), or with
    // preview, to a temporary file opened in the default app.
    void saveOutput(const char* title, FileFilter filter, const char* extension, const std::string& suggestedName,
                    std::function<std::string()> make, bool preview = false);

    // ---- chrome (app.cpp)
    void drawMenuBar();
    void drawSidebar();
    void drawStatusBar();
    void drawWelcome();
    void drawModals();
    void go(Screen screen);
    void openProject(int projectId, std::optional<ProjectTab> tab = std::nullopt);
    void goToIssue(const op::Issue& issue);
    void notify(std::string message, bool error = false);
    void requestPopup(const char* name);
    void confirm(std::string title, std::string message, std::string button, std::function<void()> action);
    void screenHeader(const char* title, const std::string& subtitle = {});

    // ---- shared helpers (app.cpp, record_table.hpp)
    std::vector<ui::Option> refOptions(op::Ref kind, int projectId) const;
    ui::RefOptions refPicker() const;
    const op::ProjectStats& stats(int projectId);
    template <class T>
    void drawRecordTable(const TableSpec<T>& spec, std::vector<T>& items);

    // ---- dashboard (app_dashboard.cpp)
    void drawDashboard();
    void drawAttention(float width, float height);
    void drawUpcoming(float width, float height);

    // ---- projects (app_projects.cpp)
    void drawProjects();
    void drawProject();
    void drawProjectOverview(op::Project& p);
    void drawPhases(op::Project& p);
    void drawTasks(op::Project& p);
    void drawDrawings(op::Project& p);
    void drawRfis(op::Project& p);
    void drawSubmittals(op::Project& p);
    void drawChanges(op::Project& p);
    void drawInvoices(op::Project& p);
    void drawProjectTime(op::Project& p);
    void createProject();  // deferred; opens the new project
    std::string safeReportName(const op::Project& p) const;

    // ---- schedule (app_schedule.cpp)
    void drawSchedule(op::Project& p);

    // ---- firm (app_firm.cpp)
    void drawTimesheet();
    void drawTeam();
    void drawClients();
    void drawReports();
    void drawFirm();

    // ---- state
    std::optional<op::Practice> practice_;
    std::string path_;
    std::uint64_t version_ = 1;
    std::uint64_t computedVersion_ = 0;
    std::map<int, op::ProjectStats> stats_;
    std::vector<op::Issue> issues_;
    op::FirmStats firmStats_;
    op::Date today_;
    bool dirty_ = false;
    std::chrono::steady_clock::time_point lastChange_;
    std::string saveError_;
    std::vector<std::function<void()>> deferred_;

    Screen screen_ = Screen::Dashboard;
    ProjectTab tab_ = ProjectTab::Overview;
    bool tabRequested_ = false;  // select tab_ programmatically on the next frame
    int projectId_ = 0;
    std::map<std::string, int> selection_;      // record table id -> selected record id
    std::map<std::string, std::string> search_;  // record table id -> search text
    int projectFilter_ = 0;                      // 0 active, 1 proposals, 2 all
    bool hideDoneTasks_ = true;

    // timesheet
    int sheetStaff_ = 0;
    op::Date sheetWeek_;
    struct SheetRow {
        int staff = 0;
        int project = 0;
        int phase = 0;
    };
    std::vector<SheetRow> sheetRows_;  // rows added this session before any hours are entered
    int addRowProject_ = 0;
    int addRowPhase_ = 0;

    // schedule
    float dayWidth_ = 12.0f;
    int dragTask_ = 0;
    int dragMode_ = 0;  // 1 move, 2 resize end
    float dragAccum_ = 0.0f;
    int scheduleScrolledFor_ = 0;  // project whose schedule was last scrolled to today

    // reports
    int reportProject_ = 0;
    std::string exportList_ = "time";
    int exportProject_ = 0;

    bool quit_ = false;
    bool darkTheme_ = false;
    bool rememberRecentFiles_ = true;
    std::vector<std::string> recent_;
    std::string configPath_;
    std::string iniPath_;

    std::string notice_;
    bool noticeIsError_ = false;
    std::chrono::steady_clock::time_point noticeTime_;
    std::string pendingPopup_;
    ConfirmRequest confirm_;
    std::string typedPath_;
    std::string typedPathPrompt_;
    std::function<void(const std::string&)> typedPathAction_;
    NewPracticeForm newPractice_;
};

}  // namespace opgui
