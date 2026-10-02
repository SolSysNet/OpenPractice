#include "app.hpp"

#include "imgui.h"
#include "imgui_internal.h"  // ErrorRecoveryStoreState / TryToRecoverState
#include "openpractice/cli.hpp"
#include "openpractice/format.hpp"
#include "openpractice/report.hpp"
#include "openpractice/util.hpp"
#include "platform.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace opgui {

namespace fs = std::filesystem;
using namespace op;

namespace {

constexpr const char* kFilterDescription = "OpenPractice files (*.opp)";
constexpr const char* kFilterPattern = "*.opp";
constexpr float kNoticeSeconds = 6.0f;

// A path must be printable UTF-8 that converts to a filesystem path; anything else (for
// example a corrupted config) is ignored rather than trusted.
bool isUsablePath(const std::string& s) {
    if (s.empty() || s.size() > 4096) return false;
    for (char ch : s) {
        if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7F) return false;
    }
    try {
        (void)fs::u8path(s);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool samePath(const std::string& a, const std::string& b) {
    if (!isUsablePath(a) || !isUsablePath(b)) return a == b;
    std::error_code ec;
    if (fs::equivalent(fs::u8path(a), fs::u8path(b), ec)) return true;
    const std::string na = fs::u8path(a).lexically_normal().u8string();
    const std::string nb = fs::u8path(b).lexically_normal().u8string();
#ifdef _WIN32
    return iequals(na, nb);
#else
    return na == nb;
#endif
}

std::string documentsDirectory() {
    const char* home = std::getenv("USERPROFILE");
    if (!home || !*home) home = std::getenv("HOME");
    if (!home || !*home) return fs::current_path().u8string();
    fs::path docs = fs::u8path(home) / "Documents";
    std::error_code ec;
    return fs::is_directory(docs, ec) ? docs.u8string() : fs::u8path(home).u8string();
}

std::string safeFileName(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '+') out += c;
    }
    return trim(out);
}

std::string defaultPracticePath(const std::string& firm) {
    std::string name = safeFileName(firm);
    if (name.empty()) name = "My Practice";
    return (fs::u8path(documentsDirectory()) / fs::u8path(name + ".opp")).u8string();
}

struct NavItem {
    Screen screen;
    const char* label;
};

const NavItem kNav[] = {
    {Screen::Dashboard, "Dashboard"}, {Screen::Projects, "Projects"}, {Screen::Timesheet, "Timesheet"},
    {Screen::Team, "Team"},           {Screen::Clients, "Clients"},   {Screen::Reports, "Reports"},
    {Screen::Firm, "Firm settings"},
};

}  // namespace

// ------------------------------------------------------------ lifecycle

App::App(std::string initialPath) {
    const std::string dir = configDirectory();
    configPath_ = (fs::u8path(dir) / "openpractice-gui.cfg").u8string();
    iniPath_ = (fs::u8path(dir) / "imgui.ini").u8string();
    ImGui::GetIO().IniFilename = iniPath_.c_str();
    loadConfig();
    applyTheme(darkTheme_);
    today_ = Date::today();
    sheetWeek_ = weekStart(today_);
    newPractice_.path = defaultPracticePath("");

    try {
        if (!initialPath.empty()) {
            openPractice(initialPath);
        } else if (!recent_.empty()) {
            std::error_code ec;
            if (fs::exists(fs::u8path(recent_.front()), ec)) openPractice(recent_.front());
        }
    } catch (const std::exception& e) {
        notify(std::string("Could not open your last practice file: ") + e.what(), true);
    }
}

App::~App() {
    // Uncommitted changes survive in the recovery file and are offered again next time.
    if (practice_ && hasPending()) writeRecovery();
    saveConfig();
}

void App::loadConfig() {
    std::ifstream in(fs::u8path(configPath_));
    std::string line;
    while (std::getline(in, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (readThemeConfig(key, value)) continue;
        if (key == "theme") darkTheme_ = value == "dark";
        if (key == "auto_commit") autoCommit_ = value == "on";
        if (key == "recent_files") rememberRecentFiles_ = value != "off";
        if (key == "recent" && rememberRecentFiles_ && isUsablePath(value) && recent_.size() < 8 &&
            std::none_of(recent_.begin(), recent_.end(), [&](const std::string& r) { return samePath(r, value); }))
            recent_.push_back(value);
    }
}

void App::saveConfig() const {
    // Write a temporary file and rename it over the old one, so a crash mid-write can't leave a
    // truncated config behind.
    const fs::path target = fs::u8path(configPath_);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << "theme=" << (darkTheme_ ? "dark" : "light") << '\n';
        out << "recent_files=" << (rememberRecentFiles_ ? "on" : "off") << '\n';
        out << "auto_commit=" << (autoCommit_ ? "on" : "off") << '\n';
        writeThemeConfig(out);
        if (rememberRecentFiles_) {
            for (const auto& r : recent_) {
                if (isUsablePath(r)) out << "recent=" << r << '\n';
            }
        }
        if (!out) return;
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
    }
}

void App::rememberRecent(std::string path) {
    if (!rememberRecentFiles_) return;
    recent_.erase(std::remove_if(recent_.begin(), recent_.end(), [&](const std::string& r) { return samePath(r, path); }),
                  recent_.end());
    recent_.insert(recent_.begin(), path);
    if (recent_.size() > 8) recent_.resize(8);
    saveConfig();
}

bool App::openPractice(std::string path) {
    if (!isUsablePath(path)) {
        notify("That file name can't be opened.", true);
        return false;
    }
    try {
        Practice p = Practice::load(path);
        practice_ = std::move(p);
    } catch (const std::exception& e) {
        notify(std::string("Could not open the file: ") + e.what(), true);
        return false;
    }
    path_ = path;
    saveError_.clear();
    deferred_.clear();  // edits queued against the previous file
    ++version_;
    resetHistory();
    recalculate();  // cached figures point into the previous file's records
    screen_ = Screen::Dashboard;
    projectId_ = 0;
    selection_.clear();
    search_.clear();
    sheetRows_.clear();
    sheetStaff_ = 0;
    for (const auto& s : practice_->staff) {
        if (s.active) {
            sheetStaff_ = s.id;
            break;
        }
    }
    rememberRecent(path);
    notify("Opened " + fs::u8path(path).filename().u8string());
    checkRecovery();
    return true;
}

void App::closePractice() {
    practice_.reset();
    path_.clear();
    deferred_.clear();
    stats_.clear();
    issues_.clear();
    newPractice_ = {};
    newPractice_.path = defaultPracticePath("");
    ++version_;
}

bool App::createPractice(NewPracticeForm& form, bool sample) {
    if (trim(form.path).empty()) {
        form.error = "Choose where to save the practice file.";
        return false;
    }
    std::error_code ec;
    if (fs::exists(fs::u8path(form.path), ec)) {
        form.error = "That file already exists. Open it instead, or choose another name.";
        return false;
    }
    try {
        Practice p = sample ? samplePractice(Date::today()) : Practice{};
        if (!sample) p.firm.name = trim(form.firm);
        p.save(form.path);
    } catch (const std::exception& e) {
        form.error = e.what();
        return false;
    }
    const std::string path = form.path;
    form.error.clear();
    if (!openPractice(path)) return false;
    if (!sample) {
        screen_ = Screen::Firm;
        notify("Created " + fs::u8path(path).filename().u8string() + ". Start with your firm details and team.");
    }
    return true;
}

void App::openSample() {
    const std::string path = (fs::u8path(documentsDirectory()) / "OpenPractice Sample.opp").u8string();
    std::error_code ec;
    if (fs::exists(fs::u8path(path), ec)) {
        openPractice(path);
        return;
    }
    NewPracticeForm form;
    form.path = path;
    if (!createPractice(form, true)) notify(form.error, true);
}

void App::defer(std::function<void()> action) { deferred_.push_back(std::move(action)); }

void App::recalculate() {
    const Date today = Date::today();
    if (today != today_) {  // the app was left open past midnight
        today_ = today;
        ++version_;
    }
    if (!practice_ || computedVersion_ == version_) return;
    stats_.clear();
    for (const auto& p : practice_->projects) stats_[p.id] = projectStats(*practice_, p.id, today_);
    issues_ = reviewPractice(*practice_, today_);
    firmStats_ = firmStats(*practice_, today_);
    refreshPending();
    computedVersion_ = version_;
}

const ProjectStats& App::stats(int projectId) {
    auto it = stats_.find(projectId);
    if (it == stats_.end()) it = stats_.emplace(projectId, projectStats(*practice_, projectId, today_)).first;
    return it->second;
}

std::string App::windowTitle() const {
    if (!practice_) return "OpenPractice";
    const std::string name = practice_->firm.name.empty() ? "Practice" : practice_->firm.name;
    return name + " - OpenPractice (" + fs::u8path(path_).filename().u8string() + ")";
}

bool App::wantsFrequentRedraw() const {
    // ImGui hands a burst of keystrokes to the app one per frame; keep drawing until the
    // queue is empty.
    if (!ImGui::GetCurrentContext()->InputEventsQueue.empty()) return true;
    // Keep ticking until an edit ends and the recovery file (or an automatic commit) lands.
    if (practice_ && (gestureOpen_ || (autoCommit_ ? version_ != committedVersion_ : version_ != recoveryVersion_)))
        return true;
    if (notice_.empty()) return false;
    const float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - noticeTime_).count();
    return age < kNoticeSeconds + 1.0f;
}

void App::notify(std::string message, bool error) {
    notice_ = std::move(message);
    noticeIsError_ = error;
    noticeTime_ = std::chrono::steady_clock::now();
}

void App::requestPopup(const char* name) { pendingPopup_ = name; }

void App::confirm(std::string title, std::string message, std::string button, std::function<void()> action) {
    confirm_ = ConfirmRequest{std::move(title), std::move(message), std::move(button), std::move(action)};
    requestPopup("Confirm##dialog");
}

void App::go(Screen screen) {
    screen_ = screen;
    ImGui::SetScrollY(0.0f);
}

void App::openProject(int projectId, std::optional<ProjectTab> tab) {
    if (projectId != projectId_) {
        tab_ = ProjectTab::Overview;
        tabRequested_ = true;
    }
    projectId_ = projectId;
    screen_ = Screen::Project;
    if (tab) {
        tab_ = *tab;
        tabRequested_ = true;
    }
}

void App::goToIssue(const Issue& issue) {
    if (issue.projectId == 0 || !practice_->findProject(issue.projectId)) {
        go(issue.area == Area::Team ? Screen::Team : Screen::Firm);
        return;
    }
    ProjectTab tab = ProjectTab::Overview;
    switch (issue.area) {
        case Area::Phases: tab = ProjectTab::Phases; break;
        case Area::Tasks: tab = ProjectTab::Schedule; break;
        case Area::Drawings: tab = ProjectTab::Drawings; break;
        case Area::Rfis: tab = ProjectTab::Rfis; break;
        case Area::Submittals: tab = ProjectTab::Submittals; break;
        case Area::Changes: tab = ProjectTab::Changes; break;
        case Area::Invoices: tab = ProjectTab::Invoices; break;
        case Area::Time: tab = ProjectTab::Time; break;
        default: break;
    }
    openProject(issue.projectId, tab);
}

void App::chooseOpenFile() {
    guardPending("opening another file", [this] {
        if (nativeFileDialogsAvailable()) {
            if (auto path = openFileDialog("Open a practice file", {kFilterDescription, kFilterPattern})) openPractice(*path);
        } else {
            typedPath_.clear();
            typedPathPrompt_ = "Open the practice file at:";
            typedPathAction_ = [this](const std::string& p) { openPractice(p); };
            requestPopup("Enter a path##typed");
        }
    });
}

void App::saveOutput(const char* title, FileFilter filter, const char* extension, const std::string& suggestedName,
                     std::function<std::string()> make, bool preview) {
    auto write = [this, make](const std::string& path, bool open) {
        try {
            const std::string bytes = make();
            std::ofstream out(fs::u8path(path), std::ios::binary | std::ios::trunc);
            out << bytes;
            out.close();
            if (!out) throw Error("cannot write '" + path + "'");
            if (open) {
                if (!openWithDefaultApp(path)) notify("Saved " + path + ", but no app could open it.", true);
            } else {
                notify("Saved " + fs::u8path(path).filename().u8string());
            }
        } catch (const std::exception& e) {
            notify(e.what(), true);
        }
    };
    if (preview) {
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "OpenPractice";
        fs::create_directories(dir, ec);
        write((dir / fs::u8path(suggestedName)).u8string(), true);
        return;
    }
    if (nativeFileDialogsAvailable()) {
        if (auto path = saveFileDialog(title, filter, extension, suggestedName)) write(*path, false);
    } else {
        typedPath_ = (fs::u8path(documentsDirectory()) / fs::u8path(suggestedName)).u8string();
        typedPathPrompt_ = std::string(title) + ":";
        typedPathAction_ = [write](const std::string& p) { write(p, false); };
        requestPopup("Enter a path##typed");
    }
}

std::vector<ui::Option> App::refOptions(Ref kind, int projectId) const {
    std::vector<ui::Option> out;
    out.push_back({0, "(none)", false});
    const Practice& p = *practice_;
    switch (kind) {
        case Ref::Staff:
            for (const auto& s : p.staff) {
                if (s.active) out.push_back({s.id, s.name, false});
            }
            if (std::any_of(p.staff.begin(), p.staff.end(), [](const Staff& s) { return !s.active; })) {
                out.push_back({0, "Inactive", true});
                for (const auto& s : p.staff) {
                    if (!s.active) out.push_back({s.id, s.name, false});
                }
            }
            break;
        case Ref::Client:
            for (const auto& c : p.clients) out.push_back({c.id, c.name, false});
            break;
        case Ref::Project:
            for (const auto& pr : p.projects) out.push_back({pr.id, p.refName(Ref::Project, pr.id), false});
            break;
        case Ref::Phase:
            for (const auto& ph : p.phases) {
                if (ph.projectId == projectId) out.push_back({ph.id, p.refName(Ref::Phase, ph.id), false});
            }
            break;
        case Ref::Task:
            for (const auto& t : p.tasks) {
                if (t.projectId == projectId) out.push_back({t.id, t.name, false});
            }
            break;
        case Ref::None: break;
    }
    return out;
}

ui::RefOptions App::refPicker() const {
    return [this](Ref kind, int projectId) { return refOptions(kind, projectId); };
}

// ----------------------------------------------------------------- frame

// Draws one frame. An exception while drawing (a bug, or data the UI didn't expect) is caught
// here: ImGui's window stack is unwound, the user sees the message, and the app keeps running.
void App::frame() {
    ImGuiErrorRecoveryState recovery;
    ImGui::ErrorRecoveryStoreState(&recovery);
    try {
        drawFrame();
    } catch (const std::exception& e) {
        ImGuiIO& io = ImGui::GetIO();
        const bool asserts = io.ConfigErrorRecoveryEnableAssert;
        io.ConfigErrorRecoveryEnableAssert = false;  // recovering on purpose
        ImGui::ErrorRecoveryTryToRecoverState(&recovery);
        io.ConfigErrorRecoveryEnableAssert = asserts;
        notify(std::string("Something went wrong: ") + e.what(), true);
        deferred_.clear();
        screen_ = Screen::Dashboard;  // don't keep re-entering the failing screen
    }
}

void App::drawFrame() {
    recalculate();
    drawMenuBar();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    const ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("##host", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    if (!practice_) {
        drawWelcome();
    } else {
        if (screen_ == Screen::Project && !practice_->findProject(projectId_)) screen_ = Screen::Projects;
        const float statusHeight = ImGui::GetFrameHeightWithSpacing() + 4.0f;
        const float sidebarWidth = ImGui::GetFontSize() * 14.0f;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_MenuBarBg));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 12));
        ImGui::BeginChild("##sidebar", ImVec2(sidebarWidth, -statusHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
        drawSidebar();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::SameLine(0, 0);
        ImGui::BeginGroup();
        if (!pendingChanges_.empty() && !autoCommit_) drawCommitBar();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 18));
        // Screens with their own scrolling tables fill the window; the rest scroll as a page.
        ImGui::BeginChild("##content", ImVec2(0, -statusHeight), ImGuiChildFlags_AlwaysUseWindowPadding);
        switch (screen_) {
            case Screen::Dashboard: drawDashboard(); break;
            case Screen::Projects: drawProjects(); break;
            case Screen::Project: drawProject(); break;
            case Screen::Timesheet: drawTimesheet(); break;
            case Screen::Team: drawTeam(); break;
            case Screen::Clients: drawClients(); break;
            case Screen::Reports: drawReports(); break;
            case Screen::Firm: drawFirm(); break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndGroup();
        drawStatusBar();
    }
    ImGui::End();

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) chooseOpenFile();
    if (practice_ && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
        if (!hasPending()) notify("Nothing to commit");
        else if (commitChanges()) notify("Changes committed");
    }
    // Undo / redo apply to the practice, except while typing in a field (which has its own).
    if (practice_ && !ImGui::GetIO().WantTextInput) {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) undo();
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal) ||
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal))
            redo();
    }

    if (!pendingPopup_.empty()) {
        ImGui::OpenPopup(pendingPopup_.c_str());
        pendingPopup_.clear();
    }
    drawModals();
    if (practice_) drawCommitModals();
    if (showThemeEditor_) drawThemeEditor();

    // Structural edits (add / delete) run now that nothing is drawing from the lists.
    if (practice_) {
        std::vector<std::function<void()>> actions;
        actions.swap(deferred_);
        for (auto& a : actions) a();
    } else {
        deferred_.clear();
    }

    endEditGesture();

    // When the user leaves the field (or after a second without edits): commit automatically
    // if that's turned on, otherwise keep the uncommitted changes in the recovery file.
    const float idle = std::chrono::duration<float>(std::chrono::steady_clock::now() - lastChange_).count();
    if (practice_ && (!ImGui::IsAnyItemActive() || idle > 1.0f)) {
        if (autoCommit_ && version_ != committedVersion_) {
            if (!hasPending()) committedVersion_ = version_;
            else commitChanges();
        } else if (!autoCommit_ && version_ != recoveryVersion_) {
            if (hasPending()) {
                writeRecovery();
            } else {
                removeRecovery();
                recoveryVersion_ = version_;
            }
        }
    }
}

void App::drawMenuBar() {
    if (!ImGui::BeginMainMenuBar()) return;
    const bool open = practice_.has_value();
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New Practice...")) guardPending("starting a new practice", [this] { closePractice(); });
        if (ImGui::MenuItem("Open...", "Ctrl+O")) chooseOpenFile();
        if (ImGui::BeginMenu("Open Recent", !recent_.empty())) {
            for (const auto& r : std::vector<std::string>(recent_)) {
                if (ImGui::MenuItem(r.c_str())) guardPending("opening another file", [this, r] { openPractice(r); });
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Clear Recent Files")) {
                recent_.clear();
                saveConfig();
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Open Sample Practice")) guardPending("opening another file", [this] { openSample(); });
        ImGui::Separator();
        if (ImGui::MenuItem("Close", nullptr, false, open)) guardPending("closing the file", [this] { closePractice(); });
        ImGui::Separator();
        if (ImGui::MenuItem("Exit") && canClose()) quit_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit", open)) {
        const bool pending = hasPending();
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undo_.empty())) undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redo_.empty())) redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Commit Changes", "Ctrl+S", false, pending) && commitChanges()) notify("Changes committed");
        if (ImGui::MenuItem("Review Changes...", nullptr, false, pending)) requestPopup("Review changes");
        if (ImGui::MenuItem("Discard Changes", nullptr, false, pending)) discardChanges();
        ImGui::Separator();
        if (ImGui::MenuItem("Commit Automatically", nullptr, autoCommit_)) {
            autoCommit_ = !autoCommit_;
            saveConfig();
        }
        ImGui::SetItemTooltip("Save every change to the file as you make it, without a commit step.");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Go", open)) {
        for (const auto& n : kNav) {
            if (ImGui::MenuItem(n.label, nullptr, screen_ == n.screen)) go(n.screen);
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Dark Theme", nullptr, darkTheme_)) {
            darkTheme_ = !darkTheme_;
            applyTheme(darkTheme_);
            saveConfig();
        }
        if (ImGui::MenuItem("Remember Recent Files", nullptr, rememberRecentFiles_)) {
            rememberRecentFiles_ = !rememberRecentFiles_;
            if (!rememberRecentFiles_) recent_.clear();
            else if (practice_) rememberRecent(path_);
            saveConfig();
        }
        ImGui::SetItemTooltip("Keep a list of recently opened files and reopen the last one at startup.");
        ImGui::Separator();
        if (ImGui::MenuItem("Theme Editor...", nullptr, showThemeEditor_)) showThemeEditor_ = !showThemeEditor_;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About OpenPractice")) requestPopup("About OpenPractice");
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void App::drawSidebar() {
    const float fs = ImGui::GetFontSize();
    ImGui::PushFont(g_fonts.bold, kBaseFontSize * 1.2f);
    ImGui::PushStyleColor(ImGuiCol_Text, colorAccent());
    ImGui::TextUnformatted("OpenPractice");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushTextWrapPos(0.0f);
    ui::Muted(practice_->firm.name.empty() ? "Your firm" : practice_->firm.name.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0, fs * 0.6f));

    int errors = 0;
    for (const auto& i : issues_) {
        if (i.severity == Severity::Error) ++errors;
    }

    auto navItem = [&](const char* label, bool selected, float indent, int badge, ImVec4 dot) -> bool {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::Selectable("##nav", selected, 0, ImVec2(0, fs * 1.6f));
        ImDrawList* draw = ImGui::GetWindowDrawList();
        float x = pos.x + fs * 0.5f + indent;
        if (dot.w > 0.0f) {
            draw->AddCircleFilled(ImVec2(x + fs * 0.25f, pos.y + fs * 0.8f), fs * 0.22f, ImGui::GetColorU32(dot));
            x += fs * 0.8f;
        }
        const ImU32 textColor = ImGui::GetColorU32(selected ? colorAccent() : ImGui::GetStyleColorVec4(ImGuiCol_Text));
        // Clip long project names to the sidebar.
        const float right = pos.x + ImGui::GetContentRegionAvail().x - (badge > 0 ? fs * 2.0f : fs * 0.3f);
        draw->PushClipRect(ImVec2(x, pos.y), ImVec2(right, pos.y + fs * 1.6f), true);
        draw->AddText(ImVec2(x, pos.y + fs * 0.3f), textColor, label);
        draw->PopClipRect();
        if (badge > 0) {
            char text[16];
            std::snprintf(text, sizeof text, "%d", badge);
            const ImVec2 bs = ImGui::CalcTextSize(text);
            const float r = pos.x + ImGui::GetContentRegionAvail().x;
            const ImVec2 bmin(r - bs.x - fs * 0.9f, pos.y + fs * 0.3f);
            draw->AddRectFilled(bmin, ImVec2(r - fs * 0.2f, bmin.y + bs.y + 2), ImGui::GetColorU32(colorNegative()), bs.y);
            draw->AddText(ImVec2(bmin.x + fs * 0.35f, bmin.y + 1), IM_COL32_WHITE, text);
        }
        return clicked;
    };

    for (const auto& n : kNav) {
        ImGui::PushID(static_cast<int>(n.screen));
        const bool selected = screen_ == n.screen || (n.screen == Screen::Projects && screen_ == Screen::Project);
        if (navItem(n.label, selected && !(n.screen == Screen::Projects && screen_ == Screen::Project), 0.0f,
                    n.screen == Screen::Dashboard ? errors : 0, ImVec4(0, 0, 0, 0)))
            go(n.screen);
        // Active projects listed under Projects for quick switching.
        if (n.screen == Screen::Projects) {
            for (const auto& p : practice_->projects) {
                if (p.status != ProjectStatus::Active && !(screen_ == Screen::Project && projectId_ == p.id)) continue;
                ImGui::PushID(p.id);
                const std::string label = p.name.empty() ? p.number : p.name;
                const auto it = stats_.find(p.id);
                const ImVec4 dot = it != stats_.end() ? ui::healthColor(it->second.health) : colorMuted();
                if (navItem(label.c_str(), screen_ == Screen::Project && projectId_ == p.id, fs * 0.8f, 0, dot))
                    openProject(p.id);
                if (ImGui::BeginPopupContextItem("##project")) {
                    projectContextMenu(p.id);
                    ImGui::EndPopup();
                }
                if (ImGui::IsItemHovered() && it != stats_.end())
                    ImGui::SetTooltip("%s\n%s", practice_->refName(Ref::Project, p.id).c_str(), it->second.healthNote.c_str());
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
}

void App::drawStatusBar() {
    ImGui::Separator();
    ImGui::SetCursorPosX(10.0f);
    ImGui::AlignTextToFramePadding();
    if (!saveError_.empty()) {
        ImGui::TextColored(colorNegative(), "Not saved: %s", saveError_.c_str());
    } else {
        ui::Muted(path_.c_str());
        ImGui::SameLine();
        if (!pendingChanges_.empty()) {
            ImGui::TextColored(colorWarning(), "%zu uncommitted %s", pendingChanges_.size(),
                               pendingChanges_.size() == 1 ? "change" : "changes");
        } else {
            ui::Muted(autoCommit_ ? "(saved)" : "(all changes committed)");
        }
    }
    if (notice_.empty()) return;
    const float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - noticeTime_).count();
    if (age > kNoticeSeconds) return;
    const float width = ImGui::CalcTextSize(notice_.c_str()).x;
    ImGui::SameLine(std::max(ImGui::GetWindowWidth() - width - 16.0f, ImGui::GetCursorPosX() + 20.0f));
    ImVec4 color = noticeIsError_ ? colorNegative() : colorPositive();
    color.w = std::min(1.0f, (kNoticeSeconds - age) / 1.0f);
    ImGui::TextColored(color, "%s", notice_.c_str());
}

void App::projectContextMenu(int projectId) {
    Project* p = practice_->findProject(projectId);
    if (!p) return;
    ImGui::TextDisabled("%s", practice_->refName(Ref::Project, projectId).c_str());
    ImGui::Separator();
    if (ImGui::MenuItem("Open")) openProject(projectId, ProjectTab::Overview);
    if (ImGui::MenuItem("Schedule")) openProject(projectId, ProjectTab::Schedule);
    if (ImGui::MenuItem("Phases & fee")) openProject(projectId, ProjectTab::Phases);
    if (ImGui::MenuItem("Status report (PDF)...")) {
        saveOutput("Save the status report", {"PDF documents (*.pdf)", "*.pdf"}, "pdf",
                   safeReportName(*p) + " status report.pdf",
                   [this, projectId] { return projectReportPdf(*practice_, projectId, today_); });
    }
    ImGui::Separator();
    if (statusMenu(p->status)) changed();
    ImGui::Separator();
    if (ImGui::MenuItem("Delete project...")) {
        confirm("Delete this project?",
                "Delete " + practice_->refName(Ref::Project, projectId) +
                    " and everything recorded on it? You can undo this until you commit.",
                "Delete", [this, projectId] {
                    defer([this, projectId] {
                        practice_->removeProject(projectId);
                        changed();
                        if (screen_ == Screen::Project && projectId_ == projectId) go(Screen::Projects);
                    });
                });
    }
}

void App::screenHeader(const char* title, const std::string& subtitle) {
    ui::Heading(title);
    if (!subtitle.empty()) ui::Muted(subtitle.c_str());
    ImGui::Dummy(ImVec2(0, ImGui::GetFontSize() * 0.4f));
}

void App::drawWelcome() {
    const float fs = ImGui::GetFontSize();
    const float width = std::min(ImGui::GetContentRegionAvail().x - fs * 4, fs * 54.0f);
    ImGui::SetCursorPos(ImVec2((ImGui::GetWindowWidth() - width) * 0.5f, fs * 3.0f));
    ImGui::BeginGroup();
    ImGui::PushFont(g_fonts.bold, kBaseFontSize * 2.6f);
    ImGui::TextColored(colorAccent(), "OpenPractice");
    ImGui::PopFont();
    ImGui::PushFont(nullptr, kBaseFontSize * 1.25f);
    ImGui::TextUnformatted("Open source project management for architects and engineers.");
    ImGui::PopFont();
    ui::Muted("Projects, phases and fees, schedules, timesheets, drawings, RFIs, submittals and billing, in one file you own.");
    ImGui::Dummy(ImVec2(0, fs));

    const float half = (width - fs) * 0.5f;
    ui::BeginCard("##start", half);
    ui::SubHeading("Start a practice");
    ImGui::Spacing();
    const float field = half - fs * 2.2f;
    ImGui::TextUnformatted("Firm name");
    ImGui::SetNextItemWidth(field);
    const std::string before = defaultPracticePath(newPractice_.firm);
    if (ui::InputString("##firm", newPractice_.firm) && newPractice_.path == before)
        newPractice_.path = defaultPracticePath(newPractice_.firm);
    ImGui::TextUnformatted("Save as");
    ImGui::SetNextItemWidth(field - (nativeFileDialogsAvailable() ? fs * 5.5f : 0.0f));
    ui::InputString("##path", newPractice_.path);
    if (nativeFileDialogsAvailable()) {
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            if (auto p = saveFileDialog("Save the practice file as", {kFilterDescription, kFilterPattern}, "opp",
                                        fs::u8path(newPractice_.path).filename().u8string()))
                newPractice_.path = *p;
        }
    }
    ImGui::Spacing();
    ui::ErrorText(newPractice_.error);
    if (ui::PrimaryButton("Create practice", ImVec2(field, fs * 2.0f))) createPractice(newPractice_, false);
    ImGui::Spacing();
    ui::Muted("New to OpenPractice?");
    if (ImGui::Button("Explore a sample firm", ImVec2(field, 0))) openSample();
    ImGui::SetItemTooltip("Opens a practice with three projects at different stages\n(saved as OpenPractice Sample.opp in Documents).");
    ui::EndCard(false);  // the second column sits beside this card

    ImGui::SameLine(0, fs);
    ImGui::BeginGroup();
    ui::BeginCard("##open", half);
    ui::SubHeading("Continue");
    ImGui::Spacing();
    if (ImGui::Button("Open a practice file...", ImVec2(half - fs * 2.2f, 0))) chooseOpenFile();
    if (!recent_.empty()) {
        ImGui::Spacing();
        ui::Muted("Recent");
        for (const auto& r : std::vector<std::string>(recent_)) {
            const std::string label = fs::u8path(r).filename().u8string();
            if (ImGui::Selectable(label.c_str())) openPractice(r);
            ImGui::SetItemTooltip("%s", r.c_str());
        }
    }
    ui::EndCard();
    ui::BeginCard("##why", half);
    ui::SubHeading("Built for A/E practice");
    ImGui::Spacing();
    const char* points[] = {
        "Fees by phase (SD, DD, CD, BN, CA) with earned value, budget hours and health.",
        "Real profitability: direct labor, overhead rate and net multiplier per project.",
        "Gantt schedule with dependencies, weekly timesheets and staff utilization.",
        "Drawing log, RFIs, submittals, change orders and invoices with aging.",
        "Private and free: one plain-text file on your computer. MIT licensed.",
    };
    for (const char* p : points) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + half - fs * 3.2f);
        ImGui::TextUnformatted(p);
        ImGui::PopTextWrapPos();
    }
    ui::EndCard();
    ImGui::EndGroup();
    ImGui::EndGroup();
}

void App::drawModals() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const float fs = ImGui::GetFontSize();

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm##dialog", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::SubHeading(confirm_.title.c_str());
        ImGui::PushTextWrapPos(fs * 26);
        ImGui::TextUnformatted(confirm_.message.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ui::DangerButton(confirm_.button.c_str(), ImVec2(fs * 7, 0))) {
            if (confirm_.action) confirm_.action();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fs * 7, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("About OpenPractice", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(g_fonts.bold, kBaseFontSize * 1.4f);
        ImGui::TextColored(colorAccent(), "OpenPractice %s", kVersion);
        ImGui::PopFont();
        ImGui::TextUnformatted("Open source project management for architects and engineers.");
        ui::Muted("MIT License. Uses Dear ImGui (MIT).");
        ImGui::Spacing();
        ImGui::PushTextWrapPos(fs * 28);
        ui::Muted("Your practice lives in one plain-text .opp file. Nothing is sent anywhere: no account, no cloud, "
                  "no telemetry.");
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(fs * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Enter a path##typed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(typedPathPrompt_.c_str());
        ImGui::SetNextItemWidth(fs * 30);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter = ui::InputString("##path", typedPath_, ImGuiInputTextFlags_EnterReturnsTrue);
        if (ui::PrimaryButton("OK", ImVec2(fs * 6, 0)) || enter) {
            if (typedPathAction_ && isUsablePath(typedPath_)) typedPathAction_(typedPath_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fs * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace opgui
