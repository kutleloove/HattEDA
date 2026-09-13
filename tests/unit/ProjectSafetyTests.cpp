#include "hatt/ui/DesignCanvas.hpp"
#include "hatt/ui/MainWindow.hpp"
#include "hatt/ui/ProjectSafety.hpp"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <QtTest>

#include <functional>
#include <memory>

using hatt::ui::CanvasTool;
using hatt::ui::DesignCanvas;
using hatt::ui::MainWindow;
using hatt::ui::ProjectData;
using hatt::ui::SketchItem;

namespace {

bool showActive(MainWindow& window) {
    window.resize(1440, 900);
    window.show();
    window.activateWindow();
    return QTest::qWaitForWindowActive(&window);
}

// Creates a project through the New project dialog (location comes from `projects/location`).
void createProject(MainWindow& window, const QString& name) {
    QTimer::singleShot(0, [name] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            dialog->findChild<QLineEdit*>(QStringLiteral("NewProjectName"))->setText(name);
            dialog->accept();
        }
    });
    window.createNewProject();
}

void placeResistor(MainWindow& window, QPointF world) {
    DesignCanvas* canvas = window.activeCanvas();
    canvas->setTool(CanvasTool::Symbol, QStringLiteral("schematic.resistor"));
    const QPointF position = canvas->worldToScreen(world);
    QMouseEvent press(QEvent::MouseButtonPress, position, canvas->mapToGlobal(position),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, position, canvas->mapToGlobal(position),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &release);
    canvas->setTool(CanvasTool::Select);
}

// Runs `handler` on the next visible modal dialog. Polls, so it also catches a dialog opened
// after another one closed; gives up after five seconds.
QTimer* onNextModal(std::function<void(QWidget*)> handler) {
    auto* timer = new QTimer;
    auto ticks = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, [timer, ticks, handler] {
        QWidget* modal = QApplication::activeModalWidget();
        if (modal != nullptr && modal->isVisible()) {
            timer->stop();
            timer->deleteLater();
            handler(modal);
        } else if (++*ticks > 500) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start(10);
    return timer;
}

// Records (and rejects) any dialog shown during its lifetime.
class NoDialogExpected {
public:
    NoDialogExpected()
        : timer_(onNextModal([this](QWidget* modal) {
              shown_ = true;
              qobject_cast<QDialog*>(modal)->reject();
          })) {}
    ~NoDialogExpected() { delete timer_.data(); }
    NoDialogExpected(const NoDialogExpected&) = delete;
    NoDialogExpected& operator=(const NoDialogExpected&) = delete;
    [[nodiscard]] bool shown() const { return shown_; }

private:
    bool shown_ = false;
    QPointer<QTimer> timer_;
};

void clickButton(QWidget* modal, const QString& objectName) {
    auto* button = modal->findChild<QAbstractButton*>(objectName);
    if (button == nullptr) {
        qobject_cast<QDialog*>(modal)->reject();
        QFAIL(qPrintable(QStringLiteral("No button %1").arg(objectName)));
    }
    button->click();
}

void clickStandard(QWidget* modal, QMessageBox::StandardButton which) {
    auto* box = qobject_cast<QMessageBox*>(modal);
    if (box == nullptr || box->button(which) == nullptr) {
        qobject_cast<QDialog*>(modal)->reject();
        QFAIL("Expected a message box with the requested button");
    }
    box->button(which)->click();
}

void setModified(const QString& path, const QDateTime& time) {
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(time, QFileDevice::FileModificationTime));
}

bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

ProjectData textProject(const QString& label) {
    ProjectData project;
    project.name = QStringLiteral("Recovered");
    SketchItem text;
    text.kind = SketchItem::Kind::Text;
    text.points = {{1.0, 2.0}};
    text.label = label;
    project.schematic.append(text);
    return project;
}

} // namespace

class ProjectSafetyTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void autosaveWritesRecoveryAndSaveRemovesIt();
    void autosaveIntervalComesFromSettings();
    void discardOnCloseRemovesRecoveryAndLock();
    void recoveryIsOfferedAndRestored();
    void openSavedVersionAndDamagedRecovery();
    void backupKeepsPreviousFileOnSecondSave();
    void lockDetectsSecondWindow();
    void staleLockDoesNotBlock();
    void missingRecentProjectCanBeRemoved();
    void failedSaveInPromptCancelsQuit();

private:
    QString projectsDir() const { return settingsDir_.filePath(QStringLiteral("projects")); }
    QTemporaryDir settingsDir_;
};

void ProjectSafetyTests::initTestCase() {
    QVERIFY(settingsDir_.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("HattEDA-Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("hatt-project-safety-tests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    // Projects are real files; keep them in the temporary directory.
    QSettings().setValue(QStringLiteral("projects/location"), projectsDir());
}

void ProjectSafetyTests::autosaveWritesRecoveryAndSaveRemovesIt() {
    MainWindow window;
    QVERIFY(showActive(window));
    createProject(window, QStringLiteral("Autosave"));
    const QString path = window.projectPath();
    QVERIFY(path.startsWith(settingsDir_.path()));
    const QString recovery = hatt::ui::recoveryFilePath(path);
    QCOMPARE(recovery, path + QStringLiteral(".autosave"));
    auto* guard = window.projectGuard();
    QVERIFY(guard->isAutosaveActive());

    // Nothing to recover while the project is clean.
    QVERIFY(!guard->autosaveNow());
    QVERIFY(!QFileInfo::exists(recovery));

    placeResistor(window, {20.32, 20.32});
    QVERIFY(window.isWindowModified());
    QVERIFY(guard->autosaveNow());
    QVERIFY(QFileInfo::exists(recovery));
    const auto recovered = hatt::ui::loadProjectFile(recovery);
    QVERIFY2(recovered.ok(), qPrintable(recovered.error));
    QCOMPARE(recovered.project.schematic.size(), 1);
    // The project file itself is untouched, and unchanged content is not written again.
    QCOMPARE(hatt::ui::loadProjectFile(path).project.schematic.size(), 0);
    QVERIFY(!guard->autosaveNow());

    window.findChild<QAction*>(QStringLiteral("hatteda.action.save"))->trigger();
    QVERIFY(!window.isWindowModified());
    QVERIFY2(!QFileInfo::exists(recovery), "Save removes the recovery file");

    // Undoing back to the saved state makes an earlier recovery copy obsolete.
    window.activeCanvas()->undoStack()->undo();
    QVERIFY(guard->autosaveNow());
    QVERIFY(QFileInfo::exists(recovery));
    window.activeCanvas()->undoStack()->redo();
    QVERIFY(!window.isWindowModified());
    QVERIFY(!guard->autosaveNow());
    QVERIFY(!QFileInfo::exists(recovery));
}

void ProjectSafetyTests::autosaveIntervalComesFromSettings() {
    QSettings().setValue(QStringLiteral("projects/autosaveMinutes"), 0);
    MainWindow window;
    createProject(window, QStringLiteral("NoAutosave"));
    QVERIFY(!window.projectPath().isEmpty());
    QVERIFY2(!window.projectGuard()->isAutosaveActive(), "0 minutes disables autosave");
    QSettings().remove(QStringLiteral("projects/autosaveMinutes"));
}

void ProjectSafetyTests::discardOnCloseRemovesRecoveryAndLock() {
    MainWindow window;
    QVERIFY(showActive(window));
    createProject(window, QStringLiteral("Discard"));
    const QString path = window.projectPath();
    QVERIFY(window.projectGuard()->holdsLock());
    QVERIFY(QFileInfo::exists(hatt::ui::lockFilePath(path)));
    placeResistor(window, {20.32, 20.32});
    QVERIFY(window.projectGuard()->autosaveNow());

    onNextModal([](QWidget* modal) { clickStandard(modal, QMessageBox::Discard); });
    QVERIFY(window.close());
    QVERIFY(!QFileInfo::exists(hatt::ui::recoveryFilePath(path)));
    QVERIFY(!QFileInfo::exists(hatt::ui::lockFilePath(path)));
    QVERIFY(!window.projectGuard()->holdsLock());
}

void ProjectSafetyTests::recoveryIsOfferedAndRestored() {
    QString path;
    {
        // A window that goes away without closing the project behaves like a crash: the
        // recovery file stays, the lock is released with the process.
        MainWindow crashed;
        QVERIFY(showActive(crashed));
        createProject(crashed, QStringLiteral("Crash"));
        path = crashed.projectPath();
        placeResistor(crashed, {20.32, 20.32});
        QVERIFY(crashed.projectGuard()->autosaveNow());
    }
    const QString recovery = hatt::ui::recoveryFilePath(path);
    QVERIFY(QFileInfo::exists(recovery));
    QVERIFY(!QFileInfo::exists(hatt::ui::lockFilePath(path)));
    setModified(path, QDateTime::currentDateTime().addSecs(-120));

    MainWindow window;
    QVERIFY(showActive(window));
    bool offered = false;
    onNextModal([&offered](QWidget* modal) {
        offered = modal->objectName() == QLatin1String("ProjectRecoveryDialog");
        clickStandard(modal, QMessageBox::Cancel);
    });
    QVERIFY(!window.openProjectFile(path));
    QVERIFY(offered);
    QVERIFY(window.projectPath().isEmpty());
    QVERIFY2(QFileInfo::exists(recovery), "Cancel keeps the recovery file");

    onNextModal([](QWidget* modal) {
        clickButton(modal, QStringLiteral("hatteda.recovery.restore"));
    });
    QVERIFY(window.openProjectFile(path));
    QCOMPARE(window.projectPath(), path);
    QVERIFY2(window.isWindowModified(), "Restored content is not saved yet");
    window.showMergenWorkspace();
    QCOMPARE(window.activeCanvas()->document().size(), 1);
    QVERIFY(QFileInfo::exists(recovery));

    // Save writes the original project path and removes the recovery file.
    QVERIFY(window.saveProject());
    QVERIFY(!window.isWindowModified());
    QVERIFY(!QFileInfo::exists(recovery));
    QCOMPARE(hatt::ui::loadProjectFile(path).project.schematic.size(), 1);
}

void ProjectSafetyTests::openSavedVersionAndDamagedRecovery() {
    const QString path = QDir(projectsDir()).filePath(QStringLiteral("Saved.hatt"));
    QVERIFY(QDir().mkpath(projectsDir()));
    ProjectData saved;
    saved.name = QStringLiteral("Saved");
    QVERIFY(hatt::ui::saveProjectFile(path, saved).isEmpty());
    setModified(path, QDateTime::currentDateTime().addSecs(-120));
    const QString recovery = hatt::ui::recoveryFilePath(path);

    // A damaged recovery file never blocks opening the saved project.
    QVERIFY(writeFile(recovery, "{ damaged"));
    {
        MainWindow window;
        const NoDialogExpected noDialog;
        QVERIFY(window.openProjectFile(path));
        QVERIFY(!noDialog.shown());
        QVERIFY(!window.isWindowModified());
    }

    // A recovery identical to the project is not offered either.
    QVERIFY(writeFile(recovery, hatt::ui::serializeProject(saved)));
    {
        MainWindow window;
        const NoDialogExpected noDialog;
        QVERIFY(window.openProjectFile(path));
        QVERIFY(!noDialog.shown());
    }

    // Open saved version discards the recovered changes.
    QVERIFY(hatt::ui::saveProjectFile(recovery, textProject(QStringLiteral("lost"))).isEmpty());
    MainWindow window;
    onNextModal([](QWidget* modal) {
        clickButton(modal, QStringLiteral("hatteda.recovery.open-saved"));
    });
    QVERIFY(window.openProjectFile(path));
    QVERIFY(!window.isWindowModified());
    window.showMergenWorkspace();
    QVERIFY(window.activeCanvas()->document().isEmpty());
    QVERIFY(!QFileInfo::exists(recovery));
}

void ProjectSafetyTests::backupKeepsPreviousFileOnSecondSave() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("Backup.hatt"));
    const QString backup = hatt::ui::backupFilePath(path);
    QCOMPARE(backup, path + QStringLiteral(".bak"));
    hatt::ui::ProjectGuard guard(nullptr, [] { return ProjectData(); }, [] { return false; });

    const ProjectData first = textProject(QStringLiteral("first"));
    QVERIFY(guard.save(path, first).isEmpty());
    QVERIFY2(!QFileInfo::exists(backup), "Nothing to back up on the first save");

    const ProjectData second = textProject(QStringLiteral("second"));
    QVERIFY(guard.save(path, second).isEmpty());
    QVERIFY(QFileInfo::exists(backup));
    QCOMPARE(hatt::ui::loadProjectFile(backup).project.schematic.first().label,
             QStringLiteral("first"));
    QCOMPARE(hatt::ui::loadProjectFile(path).project.schematic.first().label,
             QStringLiteral("second"));

    // Saving unchanged content keeps the previous generation.
    QVERIFY(guard.save(path, second).isEmpty());
    QCOMPARE(hatt::ui::loadProjectFile(backup).project.schematic.first().label,
             QStringLiteral("first"));

    // A backup that cannot be written leaves the project intact, and the save still happens.
    QVERIFY(QFile::remove(backup));
    QVERIFY(QDir().mkpath(QDir(backup).filePath(QStringLiteral("blocked"))));
    QVERIFY(!hatt::ui::backupProjectFile(path));
    QCOMPARE(hatt::ui::loadProjectFile(path).project.schematic.first().label,
             QStringLiteral("second"));
    QVERIFY(guard.save(path, textProject(QStringLiteral("third"))).isEmpty());
    QCOMPARE(hatt::ui::loadProjectFile(path).project.schematic.first().label,
             QStringLiteral("third"));
    const QStringList leftovers =
        QDir(dir.path()).entryList({QStringLiteral("*.partial")}, QDir::Files);
    QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QLatin1Char(' '))));
}

void ProjectSafetyTests::lockDetectsSecondWindow() {
    MainWindow first;
    createProject(first, QStringLiteral("Locked"));
    const QString path = first.projectPath();
    QVERIFY(first.projectGuard()->holdsLock());

    MainWindow second;
    bool warned = false;
    onNextModal([&warned](QWidget* modal) {
        warned = modal->objectName() == QLatin1String("ProjectLockedDialog");
        clickStandard(modal, QMessageBox::Cancel);
    });
    QVERIFY(!second.openProjectFile(path));
    QVERIFY(warned);
    QVERIFY(second.projectPath().isEmpty());

    onNextModal([](QWidget* modal) {
        clickButton(modal, QStringLiteral("hatteda.lock.open-anyway"));
    });
    QVERIFY(second.openProjectFile(path));
    QCOMPARE(second.projectPath(), path);
    QVERIFY(!second.projectGuard()->holdsLock());
    QVERIFY(first.projectGuard()->holdsLock());

    // Re-opening the project in the window that holds the lock does not warn.
    {
        const NoDialogExpected noDialog;
        QVERIFY(first.openProjectFile(path));
        QVERIFY(!noDialog.shown());
        QVERIFY(first.projectGuard()->holdsLock());
    }

    // Closing releases the lock, so the next window opens without a warning.
    QVERIFY(second.close());
    QVERIFY(first.close());
    QVERIFY(!QFileInfo::exists(hatt::ui::lockFilePath(path)));
    MainWindow third;
    const NoDialogExpected noDialog;
    QVERIFY(third.openProjectFile(path));
    QVERIFY(!noDialog.shown());
    QVERIFY(third.projectGuard()->holdsLock());
}

void ProjectSafetyTests::staleLockDoesNotBlock() {
    QVERIFY(QDir().mkpath(projectsDir()));
    const QString path = QDir(projectsDir()).filePath(QStringLiteral("Stale.hatt"));
    QVERIFY(hatt::ui::saveProjectFile(path, ProjectData()).isEmpty());
    // Left behind by a process that no longer runs.
    const QString lock = hatt::ui::lockFilePath(path);
    QVERIFY(writeFile(lock, "2147483000\nhatteda\nno-such-host-for-tests\n"));
    setModified(lock, QDateTime::currentDateTime().addSecs(-3600));

    MainWindow window;
    const NoDialogExpected noDialog;
    QVERIFY(window.openProjectFile(path));
    QVERIFY(!noDialog.shown());
    QVERIFY(window.projectGuard()->holdsLock());
}

void ProjectSafetyTests::missingRecentProjectCanBeRemoved() {
    const QString missing = QDir(projectsDir()).filePath(QStringLiteral("Gone.hatt"));
    const QString other = QDir(projectsDir()).filePath(QStringLiteral("Other.hatt"));
    QSettings().setValue(QStringLiteral("recentProjects"), QStringList{missing, other});

    MainWindow window;
    auto* list = window.findChild<QListWidget*>(QStringLiteral("RecentProjects"));
    QVERIFY(list);
    QCOMPARE(list->count(), 2);

    QString message;
    onNextModal([&message](QWidget* modal) {
        message = qobject_cast<QMessageBox*>(modal) ? qobject_cast<QMessageBox*>(modal)->text()
                                                    : QString();
        clickStandard(modal, QMessageBox::No);
    });
    emit list->itemDoubleClicked(list->item(0));
    QVERIFY2(message.contains(QDir::toNativeSeparators(missing)), qPrintable(message));
    QVERIFY(window.projectPath().isEmpty());
    QCOMPARE(list->count(), 2);

    onNextModal([](QWidget* modal) { clickStandard(modal, QMessageBox::Yes); });
    emit list->itemDoubleClicked(list->item(0));
    QCOMPARE(QSettings().value(QStringLiteral("recentProjects")).toStringList(), QStringList{other});
    QCOMPARE(list->count(), 1);
    QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), other);
    QSettings().remove(QStringLiteral("recentProjects"));
}

void ProjectSafetyTests::failedSaveInPromptCancelsQuit() {
    MainWindow window;
    QVERIFY(showActive(window));
    createProject(window, QStringLiteral("SaveFails"));
    const QString path = window.projectPath();
    placeResistor(window, {20.32, 20.32});
    QVERIFY(window.isWindowModified());

    // File › Quit asks too; Cancel keeps the window.
    auto* quit = window.findChild<QAction*>(QStringLiteral("hatteda.action.quit"));
    QVERIFY(quit);
    onNextModal([](QWidget* modal) { clickStandard(modal, QMessageBox::Cancel); });
    quit->trigger();
    QVERIFY(window.isVisible());

    // Make the project path unwritable: a folder now stands where the file was.
    QVERIFY(QFile::remove(path));
    QVERIFY(QDir().mkpath(path));
    bool errorShown = false;
    onNextModal([&errorShown](QWidget* modal) {
        onNextModal([&errorShown](QWidget* error) {
            errorShown = true;
            clickStandard(error, QMessageBox::Ok);
        });
        clickStandard(modal, QMessageBox::Save);
    });
    QVERIFY2(!window.close(), "A failed save in the prompt cancels closing");
    QVERIFY(errorShown);
    QVERIFY(window.isVisible());
    QVERIFY(window.isWindowModified());

    QVERIFY(QDir(path).removeRecursively());
    onNextModal([](QWidget* modal) { clickStandard(modal, QMessageBox::Discard); });
    quit->trigger();
    QVERIFY(!window.isVisible());
}

QTEST_MAIN(ProjectSafetyTests)
#include "ProjectSafetyTests.moc"
