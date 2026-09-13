#pragma once

#include "hatt/ui/ProjectFile.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstddef>
#include <functional>
#include <memory>

class QLockFile;
class QWidget;

namespace hatt::ui {

// Project safety sidecar files (ADR-0005). All live next to the `.hatt` file:
//   <name>.hatt.autosave  recovery copy written by autosave, same format as the project
//   <name>.hatt.bak       previous project file, one generation, refreshed on save
//   <name>.hatt.lock      QLockFile held while a window has the project open
[[nodiscard]] QString recoveryFilePath(const QString& projectPath);
[[nodiscard]] QString backupFilePath(const QString& projectPath);
[[nodiscard]] QString lockFilePath(const QString& projectPath);

// Copies an existing project file to its `.bak` through a temporary copy, so a failed copy never
// touches the project or the previous backup. Returns true when there is nothing to back up.
[[nodiscard]] bool backupProjectFile(const QString& projectPath);

// Autosave, crash recovery, backup-on-save and the single-instance lock for the project open in
// one window. The host supplies the project snapshot (the same `ProjectData` it saves) and its
// dirty state; the guard never defines a file format of its own. Dialog parent: `window`.
class ProjectGuard final : public QObject {
    Q_OBJECT

public:
    using Snapshot = std::function<ProjectData()>;
    using DirtyCheck = std::function<bool()>;

    enum class Recovery { None, Restored, OpenSaved, Cancelled };
    static constexpr int DefaultAutosaveMinutes = 2;

    ProjectGuard(QWidget* window, Snapshot snapshot, DirtyCheck dirty);
    ~ProjectGuard() override;

    // Before opening: when another live process holds the project lock, asks Open anyway /
    // Cancel. Returns false when the user cancels. Does not keep the lock.
    [[nodiscard]] bool confirmLock(const QString& projectPath);

    // After the saved project loaded: when a valid recovery file is newer than the project and
    // differs from it, asks Restore / Open saved version / Cancel. Restore replaces `project`
    // with the recovered content; Open saved version deletes the recovery file. A damaged
    // recovery file is ignored with a status note.
    [[nodiscard]] Recovery resolveRecovery(const QString& projectPath, ProjectData& project);

    // The window now shows `projectPath` (open, new or Save as). Leaves the previous project
    // (removes its recovery file, releases its lock), takes the new lock and restarts autosave.
    void projectActivated(const QString& projectPath);
    // Saves with a `.bak` of the previous file (skipped when the content is unchanged). Returns
    // the translated save error, or an empty string on success.
    [[nodiscard]] QString save(const QString& path, const ProjectData& project);
    // A save to `path` succeeded: the recovery files of `path` and the open project are obsolete.
    void projectSaved(const QString& path);
    // The user discarded the unsaved changes.
    void discardRecovery();
    // The window closes the project: removes the recovery file and releases the lock.
    void projectClosed();

    // Writes the recovery file when the project is dirty and changed since the last autosave.
    // Returns true when a file was written. The timer calls this every `autosaveMinutes`.
    bool autosaveNow();
    [[nodiscard]] bool isAutosaveActive() const { return timer_.isActive(); }
    [[nodiscard]] bool holdsLock() const;

signals:
    void statusMessage(const QString& message, int timeout);

private:
    [[nodiscard]] bool mayTouchRecovery() const;
    void removeRecoveryFile();

    QWidget* window_;
    Snapshot snapshot_;
    DirtyCheck dirty_;
    QTimer timer_;
    QString projectPath_;
    std::unique_ptr<QLockFile> lock_;
    bool lockedElsewhere_ = false;
    bool autosaved_ = false;
    std::size_t lastAutosaveHash_ = 0;
};

} // namespace hatt::ui
