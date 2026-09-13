#include "hatt/ui/ProjectSafety.hpp"

#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QLockFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QWidget>

#include <algorithm>

namespace hatt::ui {
namespace {

constexpr int NoteTimeout = 8000;

QByteArray readAll(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// True when a live process (another window or instance) holds the lock of `projectPath`.
// A lock that cannot be created for other reasons (read-only folder) does not count.
bool lockedByOtherProcess(const QString& projectPath, qint64* pid, QString* host) {
    QLockFile probe(lockFilePath(projectPath));
    if (probe.tryLock(0)) {
        probe.unlock();
        return false;
    }
    if (probe.error() != QLockFile::LockFailedError) {
        return false;
    }
    QString application;
    if (!probe.getLockInfo(pid, host, &application)) {
        *pid = 0;
        host->clear();
    }
    return true;
}

} // namespace

QString recoveryFilePath(const QString& projectPath) {
    return projectPath + QStringLiteral(".autosave");
}

QString backupFilePath(const QString& projectPath) { return projectPath + QStringLiteral(".bak"); }

QString lockFilePath(const QString& projectPath) { return projectPath + QStringLiteral(".lock"); }

bool backupProjectFile(const QString& projectPath) {
    if (!QFileInfo::exists(projectPath)) {
        return true;
    }
    // Copy first, then swap: the project file is only ever read, and a failed copy leaves the
    // previous backup in place.
    const QString backup = backupFilePath(projectPath);
    const QString partial = backup + QStringLiteral(".partial");
    QFile::remove(partial);
    if (!QFile::copy(projectPath, partial)) {
        return false;
    }
    QFile copied(partial);
    copied.setPermissions(copied.permissions() | QFileDevice::WriteOwner);
    if ((QFileInfo::exists(backup) && !QFile::remove(backup)) || !QFile::rename(partial, backup)) {
        QFile::remove(partial);
        return false;
    }
    return true;
}

ProjectGuard::ProjectGuard(QWidget* window, Snapshot snapshot, DirtyCheck dirty)
    : QObject(window), window_(window), snapshot_(std::move(snapshot)), dirty_(std::move(dirty)) {
    connect(&timer_, &QTimer::timeout, this, &ProjectGuard::autosaveNow);
}

ProjectGuard::~ProjectGuard() = default;

bool ProjectGuard::holdsLock() const { return lock_ && lock_->isLocked(); }

bool ProjectGuard::confirmLock(const QString& projectPath) {
    if (projectPath == projectPath_ && holdsLock()) {
        return true;
    }
    qint64 pid = 0;
    QString host;
    if (!lockedByOtherProcess(projectPath, &pid, &host)) {
        return true;
    }
    const QString name = QDir::toNativeSeparators(projectPath);
    const QString owner = pid > 0 ? tr("It is open in another HattEDA window (process %1 on %2).")
                                        .arg(pid)
                                        .arg(host)
                                  : tr("It is open in another HattEDA window.");
    QMessageBox box(QMessageBox::Warning, tr("Project already open"),
                    tr("%1 is already open.").arg(name), QMessageBox::NoButton, window_);
    box.setObjectName(QStringLiteral("ProjectLockedDialog"));
    box.setInformativeText(owner + QLatin1Char(' ') +
                           tr("Editing it in two windows can overwrite changes."));
    auto* openAnyway = box.addButton(tr("Open anyway"), QMessageBox::AcceptRole);
    openAnyway->setObjectName(QStringLiteral("hatteda.lock.open-anyway"));
    box.setDefaultButton(box.addButton(QMessageBox::Cancel));
    box.exec();
    return box.clickedButton() == openAnyway;
}

ProjectGuard::Recovery ProjectGuard::resolveRecovery(const QString& projectPath,
                                                     ProjectData& project) {
    const QString recoveryPath = recoveryFilePath(projectPath);
    const QFileInfo recoveryInfo(recoveryPath);
    if (!recoveryInfo.exists() ||
        recoveryInfo.lastModified() <= QFileInfo(projectPath).lastModified()) {
        return Recovery::None;
    }
    // A recovery file of a project that is open in a live process is that session's autosave,
    // not the remains of a crash.
    qint64 pid = 0;
    QString host;
    if (!(projectPath == projectPath_ && holdsLock()) &&
        lockedByOtherProcess(projectPath, &pid, &host)) {
        return Recovery::None;
    }
    const QByteArray recovered = readAll(recoveryPath);
    if (recovered == readAll(projectPath)) {
        return Recovery::None;
    }
    ProjectLoad load = parseProject(recovered);
    if (!load.ok()) {
        emit statusMessage(tr("Ignored the damaged recovery file %1.")
                               .arg(QDir::toNativeSeparators(recoveryPath)),
                           NoteTimeout);
        return Recovery::None;
    }

    QMessageBox box(QMessageBox::Question, tr("Recover unsaved changes"),
                    tr("%1 has unsaved changes from %2 that were not saved, for example because "
                       "HattEDA stopped unexpectedly.")
                        .arg(QFileInfo(projectPath).completeBaseName(),
                             QLocale().toString(recoveryInfo.lastModified(), QLocale::ShortFormat)),
                    QMessageBox::NoButton, window_);
    box.setObjectName(QStringLiteral("ProjectRecoveryDialog"));
    box.setInformativeText(tr("Restore the recovered changes, or open the last saved version and "
                              "discard them."));
    auto* restore = box.addButton(tr("Restore"), QMessageBox::AcceptRole);
    restore->setObjectName(QStringLiteral("hatteda.recovery.restore"));
    auto* openSaved = box.addButton(tr("Open saved version"), QMessageBox::DestructiveRole);
    openSaved->setObjectName(QStringLiteral("hatteda.recovery.open-saved"));
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(restore);
    box.exec();
    if (box.clickedButton() == restore) {
        project = std::move(load.project);
        return Recovery::Restored;
    }
    if (box.clickedButton() == openSaved) {
        QFile::remove(recoveryPath);
        return Recovery::OpenSaved;
    }
    return Recovery::Cancelled;
}

void ProjectGuard::projectActivated(const QString& projectPath) {
    if (projectPath != projectPath_) {
        projectClosed();
        projectPath_ = projectPath;
        auto lock = std::make_unique<QLockFile>(lockFilePath(projectPath));
        if (lock->tryLock(0)) {
            lock_ = std::move(lock);
        } else {
            // Another process holds it (the user chose Open anyway), or the folder does not allow
            // a lock file; then the project is simply not locked.
            lockedElsewhere_ = lock->error() == QLockFile::LockFailedError;
        }
    }
    autosaved_ = false;
    lastAutosaveHash_ = 0;
    const int minutes = std::clamp(
        QSettings().value(QStringLiteral("projects/autosaveMinutes"), DefaultAutosaveMinutes).toInt(),
        0, 24 * 60);
    if (minutes > 0) {
        timer_.start(minutes * 60 * 1000);
    } else {
        timer_.stop();
    }
}

QString ProjectGuard::save(const QString& path, const ProjectData& project) {
    if (QFileInfo::exists(path) && readAll(path) != serializeProject(project) &&
        !backupProjectFile(path)) {
        emit statusMessage(tr("Could not keep a backup of %1.").arg(QDir::toNativeSeparators(path)),
                           NoteTimeout);
    }
    return saveProjectFile(path, project);
}

void ProjectGuard::projectSaved(const QString& path) {
    if (path != projectPath_) {
        QFile::remove(recoveryFilePath(path));
    }
    removeRecoveryFile();
}

void ProjectGuard::discardRecovery() { removeRecoveryFile(); }

void ProjectGuard::projectClosed() {
    removeRecoveryFile();
    timer_.stop();
    lock_.reset();
    lockedElsewhere_ = false;
    projectPath_.clear();
}

bool ProjectGuard::autosaveNow() {
    if (projectPath_.isEmpty() || !mayTouchRecovery()) {
        return false;
    }
    if (!dirty_()) {
        // Undone back to the saved state: an earlier recovery copy is obsolete.
        if (autosaved_) removeRecoveryFile();
        return false;
    }
    const ProjectData project = snapshot_();
    const std::size_t hash = qHash(serializeProject(project));
    if (autosaved_ && hash == lastAutosaveHash_) {
        return false;
    }
    const QString error = saveProjectFile(recoveryFilePath(projectPath_), project);
    if (!error.isEmpty()) {
        emit statusMessage(tr("Autosave failed: %1").arg(error), NoteTimeout);
        return false;
    }
    autosaved_ = true;
    lastAutosaveHash_ = hash;
    return true;
}

bool ProjectGuard::mayTouchRecovery() const {
    // While another process holds the lock, the recovery file belongs to that session.
    return !projectPath_.isEmpty() && !lockedElsewhere_;
}

void ProjectGuard::removeRecoveryFile() {
    if (mayTouchRecovery()) {
        QFile::remove(recoveryFilePath(projectPath_));
    }
    autosaved_ = false;
    lastAutosaveHash_ = 0;
}

} // namespace hatt::ui
