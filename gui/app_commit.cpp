// The commit stage: edits stay uncommitted until the user commits them to the file. They
// can be reviewed, undone step by step or discarded, and a recovery file keeps them safe
// from a crash until then.

#include "app.hpp"
#include "imgui.h"
#include "openpractice/util.hpp"
#include "theme.hpp"
#include "widgets.hpp"

#include <filesystem>
#include <fstream>
#include <map>

namespace opgui {

namespace fs = std::filesystem;
using namespace op;

namespace {

constexpr std::size_t kUndoLimit = 100;

// Lists changes grouped by project, firm-wide changes first.
void changeList(const Practice& practice, const std::vector<RecordChange>& changes) {
    std::map<int, std::vector<const RecordChange*>> byProject;
    for (const auto& c : changes) byProject[c.projectId].push_back(&c);
    for (const auto& [projectId, list] : byProject) {
        std::string heading = projectId == 0 ? std::string("Firm, team and clients") : practice.refName(Ref::Project, projectId);
        if (projectId != 0 && heading.rfind("(missing", 0) == 0) heading = "Deleted project";
        ui::SubHeading(heading.c_str());
        for (const RecordChange* c : list) {
            const ImVec4 color = c->kind == RecordChange::Kind::Added     ? colorPositive()
                                 : c->kind == RecordChange::Kind::Removed ? colorNegative()
                                                                          : colorWarning();
            ImGui::TextColored(color, "%s", c->kind == RecordChange::Kind::Added ? "+" : c->kind == RecordChange::Kind::Removed ? "-" : "~");
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(describe(*c).c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::Spacing();
    }
}

std::string changeCount(std::size_t n) { return std::to_string(n) + (n == 1 ? " uncommitted change" : " uncommitted changes"); }

}  // namespace

// ----------------------------------------------------------------- state

void App::refreshPending() {
    if (!practice_) {
        pendingChanges_.clear();
        return;
    }
    if (diffVersion_ == version_) return;
    pendingChanges_ = diffPractice(committed_, *practice_);
    diffVersion_ = version_;
}

bool App::hasPending() {
    refreshPending();
    return !pendingChanges_.empty();
}

void App::resetHistory() {
    committed_ = *practice_;
    shadow_ = *practice_;
    undo_.clear();
    redo_.clear();
    gestureOpen_ = false;
    pendingChanges_.clear();
    diffVersion_ = version_;
    committedVersion_ = version_;
    recoveryVersion_ = 0;
    closeConfirmed_ = false;
}

bool App::commitChanges() {
    if (!practice_ || path_.empty()) return false;
    try {
        practice_->save(path_);
    } catch (const std::exception& e) {
        saveError_ = e.what();
        notify(std::string("Could not commit: ") + e.what(), true);
        return false;
    }
    saveError_.clear();
    committed_ = *practice_;
    committedVersion_ = version_;
    pendingChanges_.clear();
    diffVersion_ = version_;
    removeRecovery();
    return true;
}

void App::replaceWorking(Practice p) {
    *practice_ = std::move(p);
    shadow_ = *practice_;
    gestureOpen_ = false;
    ++version_;
}

void App::discardChanges() {
    defer([this] {
        if (!hasPending()) return;
        const std::size_t n = pendingChanges_.size();
        undo_.push_back(*practice_);
        redo_.clear();
        replaceWorking(committed_);
        removeRecovery();
        notify("Discarded " + std::to_string(n) + (n == 1 ? " change" : " changes") + ". Undo (Ctrl+Z) brings them back.");
    });
}

void App::undo() {
    defer([this] {
        if (undo_.empty()) return;
        redo_.push_back(*practice_);
        Practice previous = std::move(undo_.back());
        undo_.pop_back();
        replaceWorking(std::move(previous));
    });
}

void App::redo() {
    defer([this] {
        if (redo_.empty()) return;
        undo_.push_back(*practice_);
        Practice next = std::move(redo_.back());
        redo_.pop_back();
        replaceWorking(std::move(next));
    });
}

// Called once per frame after deferred edits: an edit ends when no widget is active any
// more, and the next change starts a new undo step.
void App::endEditGesture() {
    if (!practice_) return;
    if (gestureOpen_ && !ImGui::IsAnyItemActive()) {
        shadow_ = *practice_;
        gestureOpen_ = false;
    }
}

void App::changed() {
    lastChange_ = std::chrono::steady_clock::now();
    ++version_;
    if (!gestureOpen_) {
        undo_.push_back(shadow_);
        if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
        redo_.clear();
        gestureOpen_ = true;
    }
}

// ------------------------------------------------------------ recovery

std::string App::recoveryPath() const { return path_.empty() ? std::string() : path_ + ".uncommitted"; }

void App::writeRecovery() {
    if (!practice_ || path_.empty() || recoveryVersion_ == version_) return;
    const fs::path target = fs::u8path(recoveryPath());
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << practice_->serialize();
        if (!out) return;
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
    }
    recoveryVersion_ = version_;
}

void App::removeRecovery() {
    if (path_.empty()) return;
    std::error_code ec;
    fs::remove(fs::u8path(recoveryPath()), ec);
    recoveryVersion_ = 0;
}

void App::checkRecovery() {
    recovered_.reset();
    std::error_code ec;
    if (path_.empty() || !fs::exists(fs::u8path(recoveryPath()), ec)) return;
    try {
        Practice p = Practice::load(recoveryPath());
        if (diffPractice(committed_, p).empty()) {
            removeRecovery();
            return;
        }
        recovered_ = std::move(p);
        requestPopup("Recover changes");
    } catch (const std::exception& e) {
        notify(std::string("Found unreadable uncommitted changes (") + e.what() + "); they were left in " +
                   fs::u8path(recoveryPath()).filename().u8string(),
               true);
    }
}

// ---------------------------------------------------------------- guards

void App::guardPending(std::string action, std::function<void()> then) {
    if (!hasPending()) {
        then();
        return;
    }
    guardAction_ = std::move(action);
    guardThen_ = std::move(then);
    requestPopup("Uncommitted changes##guard");
}

bool App::canClose() {
    if (closeConfirmed_ || !hasPending()) return true;
    guardPending("closing", [this] {
        closeConfirmed_ = true;
        quit_ = true;
    });
    return false;
}

// -------------------------------------------------------------------- UI

void App::drawCommitBar() {
    const float fs = ImGui::GetFontSize();
    ImVec4 tint = colorWarning();
    tint.w = themeIsDark() ? 0.16f : 0.12f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tint);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 1.5f, fs * 0.35f));
    ImGui::BeginChild("##commitbar", ImVec2(0, ImGui::GetFrameHeight() + fs * 0.7f), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(colorWarning(), "%s", changeCount(pendingChanges_.size()).c_str());
    ImGui::SameLine();
    if (ui::LinkButton("Review")) requestPopup("Review changes");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ui::Muted("Changes are shown everywhere but not saved to the file until you commit them.");

    const float buttons = fs * 21.5f;
    ImGui::SameLine();
    ui::AlignRight(buttons);
    ImGui::BeginDisabled(undo_.empty());
    if (ImGui::Button("Undo", ImVec2(fs * 4.0f, 0))) undo();
    ImGui::SetItemTooltip("Undo the last change (Ctrl+Z)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(redo_.empty());
    if (ImGui::Button("Redo", ImVec2(fs * 4.0f, 0))) redo();
    ImGui::SetItemTooltip("Redo (Ctrl+Y)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Discard", ImVec2(fs * 5.0f, 0))) discardChanges();
    ImGui::SetItemTooltip("Go back to the file as last committed. You can undo this.");
    ImGui::SameLine();
    if (ui::PrimaryButton("Commit", ImVec2(fs * 6.5f, 0)) && commitChanges()) notify("Changes committed");
    ImGui::SetItemTooltip("Save these changes to the file (Ctrl+S)");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void App::drawCommitModals() {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    const ImVec2 viewport = ImGui::GetMainViewport()->Size;
    const float fs = ImGui::GetFontSize();
    const float listHeight = std::min(fs * 22.0f, viewport.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(fs * 38.0f, viewport.x * 0.8f), 0));
    if (ImGui::BeginPopupModal("Review changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        refreshPending();
        ui::SubHeading(changeCount(pendingChanges_.size()).c_str());
        ui::Muted("Compared with the file as last committed.");
        ImGui::Spacing();
        ImGui::BeginChild("##list", ImVec2(0, listHeight), ImGuiChildFlags_Borders);
        if (pendingChanges_.empty()) ui::Muted("Nothing to commit.");
        changeList(*practice_, pendingChanges_);
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::BeginDisabled(pendingChanges_.empty());
        if (ui::PrimaryButton("Commit", ImVec2(fs * 6, 0))) {
            if (commitChanges()) notify("Changes committed");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Discard all", ImVec2(fs * 6, 0))) {
            discardChanges();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(fs * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(fs * 34.0f, viewport.x * 0.8f), 0));
    if (ImGui::BeginPopupModal("Uncommitted changes##guard", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ui::SubHeading(("Commit your changes before " + guardAction_ + "?").c_str());
        ImGui::TextUnformatted(("You have " + changeCount(pendingChanges_.size()) + ":").c_str());
        ImGui::BeginChild("##list", ImVec2(0, std::min(listHeight, fs * 12.0f)), ImGuiChildFlags_Borders);
        changeList(*practice_, pendingChanges_);
        ImGui::EndChild();
        ImGui::Spacing();
        if (ui::PrimaryButton("Commit", ImVec2(fs * 6, 0))) {
            if (commitChanges()) {
                auto then = std::move(guardThen_);
                defer([then] { then(); });
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::DangerButton("Discard", ImVec2(fs * 6, 0))) {
            auto then = std::move(guardThen_);
            defer([this, then] {
                replaceWorking(committed_);
                removeRecovery();
                refreshPending();
                then();
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(fs * 6, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            guardThen_ = nullptr;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(fs * 34.0f, viewport.x * 0.8f), 0));
    if (ImGui::BeginPopupModal("Recover changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (!recovered_) {
            ImGui::CloseCurrentPopup();
        } else {
            const auto changes = diffPractice(committed_, *recovered_);
            ui::SubHeading("Recover uncommitted changes?");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(("OpenPractice closed last time with " + changeCount(changes.size()) +
                                    " to this file. Restore them to keep working on them, or discard them.")
                                       .c_str());
            ImGui::PopTextWrapPos();
            ImGui::BeginChild("##list", ImVec2(0, std::min(listHeight, fs * 12.0f)), ImGuiChildFlags_Borders);
            changeList(*recovered_, changes);
            ImGui::EndChild();
            ImGui::Spacing();
            if (ui::PrimaryButton("Restore", ImVec2(fs * 6, 0))) {
                defer([this] {
                    if (!recovered_) return;
                    undo_.push_back(*practice_);
                    replaceWorking(std::move(*recovered_));
                    recovered_.reset();
                    notify("Restored. Commit to save the changes to the file.");
                });
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ui::DangerButton("Discard", ImVec2(fs * 6, 0))) {
                recovered_.reset();
                removeRecovery();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
}

}  // namespace opgui
