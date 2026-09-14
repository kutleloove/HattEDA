#include "hatt/ui/DesignCanvas.hpp"
#include "hatt/ui/MainWindow.hpp"
#include "hatt/ui/ProjectFile.hpp"
#include "hatt/ui/Theme.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QImage>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QUndoStack>
#include <QtTest>

using hatt::ui::CanvasTool;
using hatt::ui::DesignCanvas;
using hatt::ui::Workspace;

namespace {

void activateEditor(hatt::ui::MainWindow& window);
bool showActive(hatt::ui::MainWindow& window);

QAction* action(const hatt::ui::MainWindow& window, const char* name) {
    return window.findChild<QAction*>(QString::fromLatin1(name));
}

void clickCanvas(DesignCanvas* canvas, QPointF world) {
    const QPointF position = canvas->worldToScreen(world);
    QMouseEvent press(QEvent::MouseButtonPress, position, canvas->mapToGlobal(position),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, position, canvas->mapToGlobal(position),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &release);
}

int rowForTool(const QListWidget* list, CanvasTool tool) {
    for (int row = 0; row < list->count(); ++row) {
        if (list->item(row)->data(Qt::UserRole).toInt() == static_cast<int>(tool)) {
            return row;
        }
    }
    return -1;
}

} // namespace

class MainWindowTests final : public QObject {
    Q_OBJECT

private slots:
    void startsOnWelcomePageWithoutProjectChrome();
    void toolModesAreMutuallyExclusive();
    void exposesPersistentPrimaryWorkspaces();
    void toolWorkspaceIsOpenedOnce();
    void toolActionsDriveCanvasAndObjectSelector();
    void probeModeIsUnavailableInKayra();
    void undoFollowsActiveWorkspaceAndKeepsTool();

    // Automated regression coverage supporting issue #2; not manual verification.
    void initTestCase();
    void keyboardShortcutsDriveToolsAndUndo();
    void escapeReturnsWindowToSelectionMode();
    void snapSettingsArePersisted();
    void gridStepShortcutsChangeSnapGrid();
    void boardUnitsFollowPreference();
    void arrayDialogCreatesGrid();
    void projectSaveOpenAndUnsavedChanges();
    void contextPropertiesAcceptAndCancel();
    void componentModeUsesProjectDevicesAndSchematicParts();
    void newDeviceCreatesFootprintAndPinMap();
    void selectionStatesFollowTheme_data();
    void selectionStatesFollowTheme();

private:
    QTemporaryDir settingsDir_;
};

void MainWindowTests::componentModeUsesProjectDevicesAndSchematicParts() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    auto* schematic = window.activeCanvas();
    auto* selector = window.findChild<QListWidget*>(QStringLiteral("ObjectSelector"));
    auto* deviceBar = window.findChild<QWidget*>(QStringLiteral("DeviceBar"));
    QVERIFY(selector && deviceBar);

    // A new project starts with an empty device list, as in Proteus ISIS.
    action(window, "hatteda.tool.component")->trigger();
    QVERIFY(deviceBar->isVisible());
    QCOMPARE(selector->count(), 0);
    QCOMPARE(schematic->tool(), CanvasTool::Select);

    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->objectName(), QStringLiteral("PickDevicesDialog"));
        auto* results = dialog->findChild<QListWidget*>(QStringLiteral("DeviceResults"));
        dialog->findChild<QLineEdit*>(QStringLiteral("DeviceSearch"))->setText(QStringLiteral("resis"));
        int visible = 0;
        for (int row = 0; row < results->count(); ++row) {
            if (!results->item(row)->isHidden()) {
                ++visible;
                results->item(row)->setSelected(true);
            }
        }
        QCOMPARE(visible, 1);
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    window.findChild<QPushButton*>(QStringLiteral("hatteda.devices.pick"))->click();
    window.addProjectDevices({QStringLiteral("schematic.capacitor"), QStringLiteral("board.r0603")});
    QCOMPARE(window.projectDevices(),
             QStringList({QStringLiteral("schematic.resistor"), QStringLiteral("schematic.capacitor")}));
    QCOMPARE(selector->count(), 2);
    QVERIFY(window.isWindowModified());

    selector->setCurrentRow(0);
    QCOMPARE(schematic->toolVariant(), QStringLiteral("schematic.resistor"));
    clickCanvas(schematic, {20.32, 20.32});
    clickCanvas(schematic, {40.64, 20.32});
    QCOMPARE(schematic->document().size(), 2);
    QCOMPARE(schematic->document().first().footprint, QStringLiteral("board.r0603"));
    QCOMPARE(schematic->document().first().pinPadMap, QVector<int>({1, 2}));
    QVERIFY(!window.removeProjectDevice(QStringLiteral("schematic.resistor")));
    QVERIFY(window.removeProjectDevice(QStringLiteral("schematic.capacitor")));
    QCOMPARE(selector->count(), 1);

    QVERIFY(window.saveProject());
    const auto saved = hatt::ui::loadProjectFile(window.projectPath());
    QVERIFY2(saved.ok(), qPrintable(saved.error));
    QCOMPARE(saved.project.library.devices, QStringList({QStringLiteral("schematic.resistor")}));

    // The PCB lists only schematic parts that are not placed yet.
    window.showKayraWorkspace();
    auto* board = window.activeCanvas();
    QVERIFY(!deviceBar->isVisible());
    QCOMPARE(selector->count(), 2);
    QVERIFY(selector->item(0)->text().startsWith(QStringLiteral("R1")));
    QCOMPARE(board->tool(), CanvasTool::Symbol);
    QCOMPARE(board->toolVariant(), QStringLiteral("board.r0603"));
    clickCanvas(board, {10.0, 10.0});
    QCOMPARE(board->document().size(), 1);
    QCOMPARE(board->document().first().sourceId, schematic->document().first().id);
    QCOMPARE(board->document().first().label, QStringLiteral("R1"));
    QTRY_COMPARE(selector->count(), 1);
    QVERIFY(selector->item(0)->text().startsWith(QStringLiteral("R2")));

    // Undoing the placement offers the part again.
    action(window, "hatteda.action.undo")->trigger();
    QTRY_COMPARE(selector->count(), 2);
}

void MainWindowTests::newDeviceCreatesFootprintAndPinMap() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    auto* schematic = window.activeCanvas();
    auto* selector = window.findChild<QListWidget*>(QStringLiteral("ObjectSelector"));
    action(window, "hatteda.tool.component")->trigger();

    bool footprintDialogChecked = false;
    bool deviceDialogChecked = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->objectName(), QStringLiteral("DeviceEditorDialog"));
        auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        QVERIFY(!ok->isEnabled()); // no name yet
        dialog->findChild<QLineEdit*>(QStringLiteral("DeviceName"))->setText(QStringLiteral("Regulator"));
        dialog->findChild<QSpinBox*>(QStringLiteral("DevicePinCount"))->setValue(3);
        dialog->findChild<QLineEdit*>(QStringLiteral("DevicePinNames"))->setText(QStringLiteral("IN, GND, OUT"));
        dialog->findChild<QLineEdit*>(QStringLiteral("DeviceManufacturer"))->setText(QStringLiteral("ST"));
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("DevicePinCurrent"))->setValue(1.5);
        QVERIFY(ok->isEnabled());

        // Creating the footprint from the device: pad count follows the pins, info box on the right.
        QTimer::singleShot(0, [&] {
            auto* editor = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(editor);
            QCOMPARE(editor->objectName(), QStringLiteral("FootprintEditorDialog"));
            auto* pads = editor->findChild<QSpinBox*>(QStringLiteral("FootprintPadCount"));
            QVERIFY(!pads->isEnabled());
            QCOMPARE(pads->value(), 3);
            auto* info = editor->findChild<QWidget*>(QStringLiteral("DeviceInfoBox"));
            QVERIFY(info);
            const QString details = info->findChild<QLabel*>(QStringLiteral("DeviceInfoDetails"))->text();
            QVERIFY(details.contains(QStringLiteral("ST")));
            QVERIFY(details.contains(QStringLiteral("1.5 A")));
            footprintDialogChecked = true;
            editor->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        dialog->findChild<QPushButton*>(QStringLiteral("DeviceCreateFootprint"))->click();
        QVERIFY(footprintDialogChecked);
        auto* footprint = dialog->findChild<QComboBox*>(QStringLiteral("DeviceFootprint"));
        QVERIFY(footprint->currentData().toString().startsWith(QStringLiteral("project.footprint.")));

        // Pin to pad table: a repeated pad blocks the dialog until every pad is used once.
        auto* map = dialog->findChild<QTableWidget*>(QStringLiteral("DevicePinMap"));
        QCOMPARE(map->rowCount(), 3);
        QCOMPARE(map->item(2, 1)->text(), QStringLiteral("OUT"));
        dialog->findChild<QSpinBox*>(QStringLiteral("DevicePinPad1"))->setValue(3);
        QVERIFY(!ok->isEnabled());
        dialog->findChild<QSpinBox*>(QStringLiteral("DevicePinPad3"))->setValue(1);
        QVERIFY(ok->isEnabled());
        deviceDialogChecked = true;
        ok->click();
    });
    window.findChild<QPushButton*>(QStringLiteral("hatteda.devices.new"))->click();
    QVERIFY(deviceDialogChecked);

    const QStringList devices = window.projectDevices();
    QCOMPARE(devices.size(), 1);
    QVERIFY(devices.first().startsWith(QStringLiteral("project.device.")));
    QCOMPARE(selector->count(), 1);
    selector->setCurrentRow(0);
    QCOMPARE(schematic->toolVariant(), devices.first());
    clickCanvas(schematic, {20.32, 20.32});
    QCOMPARE(schematic->document().size(), 1);
    const auto part = schematic->document().first();
    QCOMPARE(part.label, QStringLiteral("U1"));
    QVERIFY(part.footprint.startsWith(QStringLiteral("project.footprint.")));
    QCOMPARE(part.pinPadMap, QVector<int>({3, 2, 1}));

    QVERIFY(window.saveProject());
    const auto saved = hatt::ui::loadProjectFile(window.projectPath());
    QVERIFY2(saved.ok(), qPrintable(saved.error));
    QCOMPARE(saved.project.library.customDevices.size(), 1);
    QCOMPARE(saved.project.library.customFootprints.size(), 1);
    QCOMPARE(saved.project.library.customDevices.first().pinPadMap, QVector<int>({3, 2, 1}));
    QCOMPARE(saved.project.library.customFootprints.first().params.padCount, 3);

    // The PCB offers the part with its project footprint.
    window.showKayraWorkspace();
    auto* board = window.activeCanvas();
    QCOMPARE(selector->count(), 1);
    QCOMPARE(board->toolVariant(), part.footprint);
    clickCanvas(board, {10.0, 10.0});
    QCOMPARE(board->document().size(), 1);
    QCOMPARE(board->document().first().variant, part.footprint);
}

void MainWindowTests::contextPropertiesAcceptAndCancel() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    auto* canvas = window.activeCanvas();
    canvas->setTool(CanvasTool::Symbol, QStringLiteral("schematic.resistor"));
    clickCanvas(canvas, {20.32, 20.32});
    canvas->setTool(CanvasTool::Select);
    auto openProperties = [&](bool accept) {
        canvas->contextMenuRequested(canvas->mapToGlobal(QPoint(100, 100)), 0);
        auto* menu = window.findChild<QMenu*>(QStringLiteral("CanvasContextMenu"));
        QVERIFY(menu);
        auto* properties = menu->findChild<QAction*>(QStringLiteral("hatteda.context.properties"));
        QVERIFY(properties);
        menu->hide();
        QTimer::singleShot(0, [accept] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->findChild<QLineEdit*>(QStringLiteral("ItemLabel"))->setText(QStringLiteral("R99"));
            // Schematic coordinates are edited in mil: 1000 mil is 25.4 mm.
            auto* x = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("ItemPositionX"));
            QCOMPARE(x->suffix(), QStringLiteral(" mil"));
            x->setValue(1000);
            dialog->findChild<QLineEdit*>(QStringLiteral("ItemValue"))->setText(QStringLiteral("4.7k"));
            auto* footprint = dialog->findChild<QComboBox*>(QStringLiteral("ItemFootprint"));
            footprint->setCurrentIndex(footprint->findData(QStringLiteral("board.r0603")));
            auto* mapping = dialog->findChild<QLineEdit*>(QStringLiteral("ItemPinPadMap"));
            mapping->setText(QStringLiteral("1,1"));
            if (accept) {
                auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
                ok->click();
                QVERIFY(dialog->isVisible());
                mapping->setText(QStringLiteral("2,1"));
                ok->click();
            }
            else dialog->reject();
        });
        properties->trigger();
        delete menu;
    };
    const auto original = canvas->document().first();
    openProperties(false);
    QCOMPARE(canvas->document().first().label, original.label);
    QCOMPARE(canvas->undoStack()->count(), 1);
    openProperties(true);
    QCOMPARE(canvas->document().first().label, QStringLiteral("R99"));
    QCOMPARE(canvas->document().first().points.first().x(), 25.4);
    QCOMPARE(canvas->document().first().value, QStringLiteral("4.7k"));
    QCOMPARE(canvas->document().first().footprint, QStringLiteral("board.r0603"));
    QCOMPARE(canvas->document().first().pinPadMap, QVector<int>({2, 1}));
    QCOMPARE(canvas->undoStack()->count(), 2);
    canvas->undoStack()->undo();
    QCOMPARE(canvas->document().first().label, original.label);
    QCOMPARE(canvas->document().first().value, original.value);
    QCOMPARE(canvas->document().first().footprint, original.footprint);
}

namespace {

void activateEditor(hatt::ui::MainWindow& window) {
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            dialog->accept();
        }
    });
    window.createNewProject();
}

bool showActive(hatt::ui::MainWindow& window) {
    window.resize(1440, 900);
    window.show();
    window.activateWindow();
    return QTest::qWaitForWindowActive(&window);
}

} // namespace

void MainWindowTests::initTestCase() {
    QVERIFY(settingsDir_.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("HattEDA-Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("hatt-ui-shell-tests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
    // New projects are real files; keep them out of the user's Documents folder.
    QSettings().setValue(QStringLiteral("projects/location"), settingsDir_.filePath(QStringLiteral("projects")));
}

void MainWindowTests::projectSaveOpenAndUnsavedChanges() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    QVERIFY(!action(window, "hatteda.action.save")->isEnabled());
    activateEditor(window);
    const QString path = window.projectPath();
    QVERIFY(path.startsWith(settingsDir_.path()));
    QVERIFY2(QFileInfo::exists(path), "New project creates the file");
    QVERIFY(action(window, "hatteda.action.save")->isEnabled());
    QVERIFY(!window.isWindowModified());

    // Edit both workspaces: the window is marked modified until saved.
    auto* schematic = window.activeCanvas();
    schematic->setTool(CanvasTool::Symbol, QStringLiteral("schematic.resistor"));
    clickCanvas(schematic, {20.32, 20.32});
    QVERIFY(window.isWindowModified());
    window.showKayraWorkspace();
    auto* board = window.activeCanvas();
    board->setTool(CanvasTool::Symbol, QStringLiteral("board.r0603"));
    clickCanvas(board, {10.16, 10.16});
    action(window, "hatteda.action.save")->trigger();
    QVERIFY(!window.isWindowModified());

    hatt::ui::MainWindow reopened;
    // `window` still holds the project lock, so the second window warns first (ADR-0005).
    QTimer::singleShot(0, [] {
        auto* box = QApplication::activeModalWidget();
        QVERIFY(box);
        box->findChild<QAbstractButton*>(QStringLiteral("hatteda.lock.open-anyway"))->click();
    });
    QVERIFY(reopened.openProjectFile(path));
    QCOMPARE(reopened.projectPath(), path);
    QVERIFY(!reopened.isWindowModified());
    reopened.showMergenWorkspace();
    QCOMPARE(reopened.activeCanvas()->document().size(), 1);
    const auto& saved = schematic->document().first();
    const auto& loaded = reopened.activeCanvas()->document().first();
    QCOMPARE(loaded.id, saved.id);
    QCOMPARE(loaded.label, saved.label);
    QCOMPARE(loaded.points, saved.points);
    reopened.showKayraWorkspace();
    QCOMPARE(reopened.activeCanvas()->document().first().variant, QStringLiteral("board.r0603"));
    QVERIFY(QSettings().value(QStringLiteral("recentProjects")).toStringList().contains(path));

    // Unsaved changes: Cancel keeps the window open, Discard lets it close.
    schematic->undoStack()->undo();
    window.showMergenWorkspace();
    QVERIFY(window.isWindowModified());
    auto answer = [](QMessageBox::StandardButton button) {
        QTimer::singleShot(0, [button] {
            auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            QVERIFY(box);
            box->button(button)->click();
        });
    };
    answer(QMessageBox::Cancel);
    QVERIFY(!window.close());
    QVERIFY(window.isVisible());
    answer(QMessageBox::Discard);
    QVERIFY(window.close());
    // Discarding did not touch the file.
    QVERIFY(reopened.openProjectFile(path));
    reopened.showMergenWorkspace();
    QCOMPARE(reopened.activeCanvas()->document().size(), 1);

    // A corrupted file is reported and leaves the open project untouched.
    const QString broken = settingsDir_.filePath(QStringLiteral("broken.hatt"));
    {
        QFile file(broken);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ not json");
    }
    answer(QMessageBox::Ok);
    QVERIFY(!reopened.openProjectFile(broken));
    QCOMPARE(reopened.projectPath(), path);
    QCOMPARE(reopened.activeCanvas()->document().size(), 1);
}

void MainWindowTests::keyboardShortcutsDriveToolsAndUndo() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    QVERIFY(showActive(window));
    auto* pages = window.findChild<QStackedWidget*>(QStringLiteral("ApplicationPages"));
    QCOMPARE(pages->currentIndex(), 1);
    auto* canvas = window.activeCanvas();
    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());

    const QList<QPair<const char*, const char*>> modes = {
        {"A", "hatteda.tool.component"}, {"W", "hatteda.tool.connect"},
        {"R", "hatteda.tool.terminal"},  {"P", "hatteda.tool.probe"},
        {"D", "hatteda.tool.draw"},      {"M", "hatteda.tool.measure"},
        {"V", "hatteda.tool.select"},
    };
    for (const auto& [key, id] : modes) {
        QTest::keySequence(&window, QKeySequence(QString::fromLatin1(key)));
        QVERIFY2(action(window, id)->isChecked(), id);
    }
    QCOMPARE(canvas->tool(), CanvasTool::Select);
    QTest::keySequence(&window, QKeySequence(QStringLiteral("W")));
    QCOMPARE(canvas->tool(), CanvasTool::Wire);
    QTest::keySequence(&window, QKeySequence(QStringLiteral("M")));
    QCOMPARE(canvas->tool(), CanvasTool::Measure);

    window.addProjectDevices({QStringLiteral("schematic.resistor")});
    QTest::keySequence(&window, QKeySequence(QStringLiteral("A")));
    QCOMPARE(canvas->tool(), CanvasTool::Symbol);
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+R")));
    clickCanvas(canvas, {20.32, 20.32});
    QCOMPARE(canvas->document().size(), 1);
    QCOMPARE(canvas->document().first().quarterTurns, 1);

    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+Z")));
    QCOMPARE(canvas->document().size(), 0);
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+Y")));
    QCOMPARE(canvas->document().size(), 1);
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+Z")));
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    QCOMPARE(canvas->document().size(), 1);

    QTest::keySequence(&window, QKeySequence(QStringLiteral("V")));
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+A")));
    QCOMPARE(canvas->selection(), QList<int>{0});
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+D")));
    QCOMPARE(canvas->document().size(), 2);
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+R")));
    QCOMPARE(canvas->document().at(1).quarterTurns, 2);
    QTest::keySequence(&window, QKeySequence(QKeySequence::Delete));
    QCOMPARE(canvas->document().size(), 1);

    const int zoom = canvas->zoomPercent();
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl++")));
    QVERIFY(canvas->zoomPercent() > zoom);
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+-")));
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+-")));
    QVERIFY(canvas->zoomPercent() < zoom);
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Home")));
    QVERIFY(QRectF(canvas->rect()).contains(canvas->worldToScreen({20.32, 20.32})));
    const int fittedZoom = canvas->zoomPercent();
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+-")));
    QVERIFY(canvas->zoomPercent() < fittedZoom);
    QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+0")));
    QCOMPARE(canvas->zoomPercent(), fittedZoom);
}

void MainWindowTests::escapeReturnsWindowToSelectionMode() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    action(window, "hatteda.tool.connect")->trigger();
    auto* canvas = window.activeCanvas();
    canvas->setFocus();
    clickCanvas(canvas, {5.08, 5.08});
    QVERIFY(canvas->hasPendingOperation());

    QTest::keyClick(canvas, Qt::Key_Escape);
    QVERIFY(!canvas->hasPendingOperation());
    QVERIFY(action(window, "hatteda.tool.connect")->isChecked());
    QTest::keyClick(canvas, Qt::Key_Escape);
    QVERIFY(action(window, "hatteda.tool.select")->isChecked());
    QCOMPARE(canvas->tool(), CanvasTool::Select);
}

void MainWindowTests::snapSettingsArePersisted() {
    QSettings().remove(QStringLiteral("editor/snap"));
    {
        hatt::ui::MainWindow window;
        auto* edges = window.findChild<QPushButton*>(QStringLiteral("hatteda.snap.edges"));
        auto* grid = window.findChild<QPushButton*>(QStringLiteral("hatteda.snap.grid"));
        auto* diagonal = window.findChild<QPushButton*>(QStringLiteral("hatteda.snap.diagonal"));
        auto* orthogonal = window.findChild<QPushButton*>(QStringLiteral("hatteda.snap.orthogonal"));
        QVERIFY(edges && grid && diagonal && orthogonal);
        QVERIFY(!edges->isChecked());
        QVERIFY(grid->isChecked());
        QVERIFY(diagonal->isChecked());

        edges->click();
        grid->click();
        orthogonal->click();
        QVERIFY(!diagonal->isChecked());
    }
    QSettings settings;
    QCOMPARE(settings.value(QStringLiteral("editor/snap/edges")).toBool(), true);
    QCOMPARE(settings.value(QStringLiteral("editor/snap/grid")).toBool(), false);
    QCOMPARE(settings.value(QStringLiteral("editor/snap/orthogonal")).toBool(), true);
    QCOMPARE(settings.value(QStringLiteral("editor/snap/diagonal")).toBool(), false);

    hatt::ui::MainWindow reopened;
    QVERIFY(reopened.findChild<QPushButton*>(QStringLiteral("hatteda.snap.edges"))->isChecked());
    QVERIFY(!reopened.findChild<QPushButton*>(QStringLiteral("hatteda.snap.grid"))->isChecked());
    QVERIFY(reopened.findChild<QPushButton*>(QStringLiteral("hatteda.snap.orthogonal"))->isChecked());
    QVERIFY(!reopened.findChild<QPushButton*>(QStringLiteral("hatteda.snap.diagonal"))->isChecked());

    // The restored settings reach the canvas: with grid snap off a line keeps its raw end point.
    action(reopened, "hatteda.tool.draw")->trigger();
    auto* selector = reopened.findChild<QListWidget*>(QStringLiteral("ObjectSelector"));
    selector->setCurrentRow(rowForTool(selector, CanvasTool::Line));
    auto* canvas = reopened.activeCanvas();
    clickCanvas(canvas, {5.0, 5.0});
    clickCanvas(canvas, {20.3, 5.0});
    QCOMPARE(canvas->document().size(), 1);
    QVERIFY(QLineF(canvas->document().first().points.at(1), QPointF(20.3, 5.0)).length() < 1e-3);
    QSettings().remove(QStringLiteral("editor/snap"));
}

void MainWindowTests::gridStepShortcutsChangeSnapGrid() {
    QSettings().remove(QStringLiteral("editor/snap"));
    {
        hatt::ui::MainWindow window;
        QVERIFY(showActive(window));
        activateEditor(window);
        QVERIFY(showActive(window));
        auto* canvas = window.activeCanvas();
        canvas->setFocus();
        QTRY_VERIFY(canvas->hasFocus());
        QCOMPARE(canvas->gridSize(), 2.54);
        QVERIFY(window.findChild<QPushButton*>(QStringLiteral("hatteda.snap.guides"))->isChecked());

        QCOMPARE(action(window, "hatteda.grid.step-1")->shortcut(), QKeySequence(QStringLiteral("Ctrl+F1")));
        QTest::keySequence(&window, QKeySequence(QStringLiteral("F2")));
        QCOMPARE(canvas->gridSize(), 1.27);
        QTest::keySequence(&window, QKeySequence(QStringLiteral("F4")));
        QCOMPARE(canvas->gridSize(), 12.7);
        QTest::keySequence(&window, QKeySequence(QStringLiteral("Ctrl+F1")));
        QCOMPARE(canvas->gridSize(), 0.254);
        QVERIFY(action(window, "hatteda.grid.step-1")->isChecked());

        window.showKayraWorkspace();
        QCOMPARE(window.activeCanvas()->gridSize(), 0.127);
        auto* button = window.findChild<QPushButton*>(QStringLiteral("GridStepButton"));
        QVERIFY(button != nullptr);
        QVERIFY(button->text().contains(QStringLiteral("0.127")));
    }
    QCOMPARE(QSettings().value(QStringLiteral("editor/snap/gridLevel")).toInt(), 0);
    hatt::ui::MainWindow reopened;
    QCOMPARE(reopened.activeCanvas()->gridSize(), 0.254);
    QSettings().remove(QStringLiteral("editor/snap"));
}

void MainWindowTests::arrayDialogCreatesGrid() {
    QSettings().remove(QStringLiteral("editor/snap"));
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    auto* canvas = window.activeCanvas();
    canvas->setTool(CanvasTool::Symbol, QStringLiteral("schematic.resistor"));
    clickCanvas(canvas, {20.32, 20.32});
    canvas->setTool(CanvasTool::Select);
    auto* array = action(window, "hatteda.action.array");
    QVERIFY(!array->isEnabled());
    canvas->selectItem(0);
    QVERIFY(array->isEnabled());

    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->objectName(), QStringLiteral("ArrayDialog"));
        dialog->findChild<QSpinBox*>(QStringLiteral("ArrayRows"))->setValue(3);
        dialog->findChild<QSpinBox*>(QStringLiteral("ArrayColumns"))->setValue(3);
        auto* pitchY = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("ArrayPitchY"));
        QCOMPARE(pitchY->suffix(), QStringLiteral(" mil"));
        pitchY->setValue(300); // 7.62 mm
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    array->trigger();

    const auto& document = canvas->document();
    QCOMPARE(document.size(), 9);
    const double width = hatt::ui::itemBounds(document.first()).width();
    const double pitchX = (std::ceil(width / 2.54 - 1e-9) + 1.0) * 2.54;
    QVERIFY(QLineF(document.at(8).points.first(), QPointF(20.32 + 2 * pitchX, 20.32 + 2 * 7.62)).length() < 1e-6);
    QCOMPARE(document.at(8).label, QStringLiteral("R9"));
    QCOMPARE(canvas->undoStack()->count(), 2);
}

void MainWindowTests::boardUnitsFollowPreference() {
    using hatt::ui::LengthUnit;
    QSettings().remove(QStringLiteral("editor/units"));
    QSettings().remove(QStringLiteral("editor/snap"));
    {
        hatt::ui::MainWindow window;
        QVERIFY(showActive(window));
        activateEditor(window);
        QCOMPARE(window.activeCanvas()->lengthUnit(), LengthUnit::Mil);
        auto* button = window.findChild<QPushButton*>(QStringLiteral("GridStepButton"));
        QCOMPARE(button->text(), QStringLiteral("100 mil"));

        window.showKayraWorkspace();
        auto* board = window.activeCanvas();
        QCOMPARE(board->lengthUnit(), LengthUnit::Millimetre);
        QVERIFY(action(window, "hatteda.units.board-mm")->isChecked());

        action(window, "hatteda.units.board-in")->trigger();
        QCOMPARE(board->lengthUnit(), LengthUnit::Inch);
        QVERIFY(button->text().endsWith(QStringLiteral(" in")));
        QCOMPARE(QSettings().value(QStringLiteral("editor/units/board")).toString(), QStringLiteral("in"));

        // The preference only affects the PCB; the schematic stays in mil.
        window.showMergenWorkspace();
        QCOMPARE(window.activeCanvas()->lengthUnit(), LengthUnit::Mil);
    }
    hatt::ui::MainWindow reopened;
    reopened.showKayraWorkspace();
    QCOMPARE(reopened.activeCanvas()->lengthUnit(), LengthUnit::Inch);
    QVERIFY(action(reopened, "hatteda.units.board-in")->isChecked());
    QSettings().remove(QStringLiteral("editor/units"));
}

void MainWindowTests::startsOnWelcomePageWithoutProjectChrome() {
    hatt::ui::MainWindow window;
    auto* pages = window.findChild<QStackedWidget*>(QStringLiteral("ApplicationPages"));
    QVERIFY(pages != nullptr);
    QCOMPARE(pages->currentIndex(), 0);
    QCOMPARE(pages->currentWidget()->objectName(), QStringLiteral("WelcomePage"));
    for (auto* dock : window.findChildren<QDockWidget*>()) {
        QVERIFY(dock->isHidden());
    }
}

void MainWindowTests::toolModesAreMutuallyExclusive() {
    hatt::ui::MainWindow window;
    const auto buttons = window.findChildren<QToolButton*>();
    QList<QToolButton*> modes;
    for (auto* button : buttons) {
        if (button->property("rail").toBool()) modes.append(button);
    }
    QVERIFY(modes.size() >= 2);
    modes.at(1)->click();
    QVERIFY(modes.at(1)->isChecked());
    modes.at(2)->click();
    QVERIFY(!modes.at(1)->isChecked());
    QVERIFY(modes.at(2)->isChecked());
    int checked = 0;
    for (auto* button : modes) {
        checked += button->isChecked() ? 1 : 0;
    }
    QCOMPARE(checked, 1);
}

void MainWindowTests::exposesPersistentPrimaryWorkspaces() {
    hatt::ui::MainWindow window;
    QCOMPARE(window.primaryWorkspaceCount(), 2);
    QCOMPARE(window.toolWorkspaceCount(), 0);
}

void MainWindowTests::toolWorkspaceIsOpenedOnce() {
    hatt::ui::MainWindow window;
    window.openDiagnosticsWorkspace();
    window.openDiagnosticsWorkspace();

    QCOMPARE(window.toolWorkspaceCount(), 1);
    auto* host = window.findChild<QTabWidget*>(QStringLiteral("ToolWorkspaceHost"));
    QVERIFY(host != nullptr);
    QCOMPARE(host->widget(0)->objectName(),
             QStringLiteral("hatteda.tool.simulation-diagnostics"));
}

void MainWindowTests::toolActionsDriveCanvasAndObjectSelector() {
    hatt::ui::MainWindow window;
    auto* selector = window.findChild<QListWidget*>(QStringLiteral("ObjectSelector"));
    QVERIFY(selector != nullptr);

    action(window, "hatteda.tool.draw")->trigger();
    QVERIFY(!selector->isHidden());
    const int rectangleRow = rowForTool(selector, CanvasTool::Rectangle);
    QVERIFY(rectangleRow >= 0);
    selector->setCurrentRow(rectangleRow);
    QCOMPARE(window.activeCanvas()->tool(), CanvasTool::Rectangle);

    window.addProjectDevices({QStringLiteral("schematic.resistor")});
    action(window, "hatteda.tool.component")->trigger();
    QCOMPARE(window.activeCanvas()->tool(), CanvasTool::Symbol);
    QVERIFY(!window.activeCanvas()->toolVariant().isEmpty());
    QVERIFY(!action(window, "hatteda.tool.draw")->isChecked());
    QVERIFY(action(window, "hatteda.tool.component")->isChecked());

    action(window, "hatteda.tool.draw")->trigger();
    QCOMPARE(window.activeCanvas()->tool(), CanvasTool::Rectangle);

    action(window, "hatteda.tool.select")->trigger();
    QVERIFY(selector->isHidden());
    QCOMPARE(window.activeCanvas()->tool(), CanvasTool::Select);
}

void MainWindowTests::probeModeIsUnavailableInKayra() {
    hatt::ui::MainWindow window;
    action(window, "hatteda.tool.probe")->trigger();
    window.showKayraWorkspace();
    QCOMPARE(window.activeCanvas()->workspace(), Workspace::Board);
    QVERIFY(!action(window, "hatteda.tool.probe")->isEnabled());
    QVERIFY(action(window, "hatteda.tool.select")->isChecked());
    QCOMPARE(window.activeCanvas()->tool(), CanvasTool::Select);
}

void MainWindowTests::undoFollowsActiveWorkspaceAndKeepsTool() {
    hatt::ui::MainWindow window;
    window.resize(1440, 900);
    window.addProjectDevices({QStringLiteral("schematic.resistor")});
    action(window, "hatteda.tool.component")->trigger();
    auto* mergen = window.activeCanvas();
    clickCanvas(mergen, {20.32, 20.32});
    QCOMPARE(mergen->document().size(), 1);
    QVERIFY(action(window, "hatteda.action.undo")->isEnabled());

    window.showKayraWorkspace();
    QVERIFY(!action(window, "hatteda.action.undo")->isEnabled());

    window.showMergenWorkspace();
    action(window, "hatteda.action.undo")->trigger();
    QCOMPARE(mergen->document().size(), 0);
    QVERIFY(action(window, "hatteda.tool.component")->isChecked());
    QCOMPARE(mergen->tool(), CanvasTool::Symbol);
    action(window, "hatteda.action.redo")->trigger();
    QCOMPARE(mergen->document().size(), 1);
}

void MainWindowTests::selectionStatesFollowTheme_data() {
    QTest::addColumn<bool>("light");
    QTest::newRow("dark") << false;
    QTest::newRow("light") << true;
}

void MainWindowTests::selectionStatesFollowTheme() {
    QFETCH(bool, light);
    auto* application = qobject_cast<QApplication*>(QCoreApplication::instance());
    QVERIFY(application != nullptr);
    const QPalette originalPalette = QApplication::palette();
    hatt::ui::Theme::apply(*application,
                           light ? hatt::ui::ThemeMode::Light : hatt::ui::ThemeMode::Dark);
    // Selected surfaces must follow the theme: light ground in light, dark ground in dark.
    const auto matchesTheme = [light](const QColor& color) {
        return light ? color.lightness() > 170 : color.lightness() < 90;
    };

    QToolButton rail;
    rail.setProperty("command", true);
    rail.setProperty("rail", true);
    rail.setCheckable(true);
    rail.setChecked(true);
    rail.setFixedSize(40, 40);
    const QImage railImage = rail.grab().toImage();
    const QColor railGround = railImage.pixelColor(railImage.width() / 2, railImage.height() / 2);
    QVERIFY2(matchesTheme(railGround), qPrintable(railGround.name()));
    // Structural cue: the checked rail button keeps a brand-colored left rail.
    const QColor railCue = railImage.pixelColor(1, railImage.height() / 2);
    QVERIFY2(railCue.green() > railCue.red() + 40, qPrintable(railCue.name()));

    QListWidget selector;
    selector.setObjectName(QStringLiteral("ObjectSelector"));
    selector.resize(220, 120);
    selector.addItem(QStringLiteral("Rectangle"));
    selector.addItem(QStringLiteral("Line"));
    selector.setCurrentRow(0);
    selector.grab();
    const QRect itemRect = selector.visualItemRect(selector.item(0));
    const QImage listImage = selector.viewport()->grab().toImage();
    const QColor selected = listImage.pixelColor(itemRect.right() - 8, itemRect.center().y());
    QVERIFY2(matchesTheme(selected), qPrintable(selected.name()));

    QVERIFY(application->styleSheet().contains(QStringLiteral("QToolButton[command=\"true\"]:disabled")));

    application->setStyleSheet(QString());
    QApplication::setPalette(originalPalette);
}

QTEST_MAIN(MainWindowTests)
#include "MainWindowTests.moc"
