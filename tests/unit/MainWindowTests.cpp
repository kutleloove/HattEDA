#include "hatt/ui/ChecksReport.hpp"
#include "hatt/ui/DesignRuleManager.hpp"
#include "hatt/ui/DesignCanvas.hpp"
#include "hatt/ui/MainWindow.hpp"
#include "hatt/ui/ProjectFile.hpp"
#include "hatt/ui/LayerColors.hpp"
#include "hatt/ui/PadStyles.hpp"
#include "hatt/ui/RoutingStyles.hpp"
#include "hatt/ui/SketchCircuit.hpp"
#include "hatt/ui/Theme.hpp"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
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
#include <QTreeWidget>
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
    void kayraPadViaPackageModesAndLayerSelector();
    void userTrackStylesAreListedEditedAndDeleted();
    void makePackageStoresFootprintAndDecomposeUndoes();
    void userPadStylesAndLayerColors();
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
    void designChecksReportAndRules();
    void fabricationExportAsksAboutRuleErrors();
    void assemblyExportsWriteCsv();
    void designRuleManagerEditsRulesClassesPairsAndDefaults();
    void zoneNetPropertyPoursOnTheCanvas();
    void zoneModeDrawsZonesAndListsThem();
    void trackModeRoutesWithNetClassWidths();
    void selectionStatesFollowTheme_data();
    void selectionStatesFollowTheme();

    // #36: right-click layer change and the text tool's font/size style bar.
    void moveToLayerContextMenuChangesTextItemLayer();
    void textStyleBarShowsFontAndAppliesToNewText();

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
                if (results->item(row)->data(Qt::UserRole + 1).toString() ==
                    QLatin1String("schematic.resistor")) {
                    results->item(row)->setSelected(true);
                }
            }
        }
        QVERIFY(visible >= 1);
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
        // Workspace-specific modes (Kayra package/via/pad) are disabled in Mergen.
        if (button->property("rail").toBool() && button->isEnabled()) modes.append(button);
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

void MainWindowTests::kayraPadViaPackageModesAndLayerSelector() {
    QSettings().remove(QStringLiteral("editor/board"));
    hatt::ui::MainWindow window;
    window.resize(1440, 900);
    auto* selector = window.findChild<QListWidget*>(QStringLiteral("ObjectSelector"));
    auto* layers = window.findChild<QComboBox*>(QStringLiteral("ActiveLayer"));
    QVERIFY(selector && layers);
    for (const char* id : {"hatteda.tool.package", "hatteda.tool.via", "hatteda.tool.pad"}) {
        QVERIFY2(!action(window, id)->isEnabled(), id);
    }

    window.showKayraWorkspace();
    auto* board = window.activeCanvas();
    QCOMPARE(board->workspace(), Workspace::Board);

    action(window, "hatteda.tool.pad")->trigger();
    QVERIFY(action(window, "hatteda.tool.pad")->isEnabled());
    QCOMPARE(board->tool(), CanvasTool::Pad);
    QCOMPARE(selector->count(), static_cast<int>(hatt::ui::padStyles().size()));
    clickCanvas(board, {5.08, 5.08});
    QCOMPARE(board->document().size(), 1);
    QCOMPARE(board->document().first().kind, hatt::ui::SketchItem::Kind::Pad);

    action(window, "hatteda.tool.via")->trigger();
    QCOMPARE(board->tool(), CanvasTool::Via);
    selector->setCurrentRow(selector->count() - 1);
    clickCanvas(board, {10.16, 5.08});
    QCOMPARE(board->document().size(), 2);
    QCOMPARE(board->document().last().width, hatt::ui::viaStyles().last().diameter);

    action(window, "hatteda.tool.connect")->trigger();
    QCOMPARE(board->tool(), CanvasTool::Wire);
    QCOMPARE(selector->count(), static_cast<int>(hatt::ui::trackStyles().size()));
    QCOMPARE(selector->currentItem()->text().left(3), QStringLiteral("T12"));
    selector->setCurrentRow(0);
    QCOMPARE(board->trackWidthSetting(), hatt::ui::trackStyles().first().width);

    action(window, "hatteda.tool.package")->trigger();
    QCOMPARE(board->tool(), CanvasTool::Symbol);
    QVERIFY(board->toolVariant().startsWith(QStringLiteral("board.")));

    // The bottom-left selector and the canvas share the active layer.
    layers->setCurrentIndex(static_cast<int>(hatt::ui::BoardLayer::BoardEdge));
    QCOMPARE(board->activeLayer(), hatt::ui::BoardLayer::BoardEdge);
    board->setActiveLayer(hatt::ui::BoardLayer::BottomCopper);
    QCOMPARE(layers->currentIndex(), static_cast<int>(hatt::ui::BoardLayer::BottomCopper));

    window.showMergenWorkspace();
    QVERIFY(!action(window, "hatteda.tool.package")->isEnabled());
    QVERIFY(action(window, "hatteda.tool.select")->isChecked());
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("BoardLayerPanel"))->isHidden());
    QSettings().remove(QStringLiteral("editor/board"));
}

void MainWindowTests::userTrackStylesAreListedEditedAndDeleted() {
    using hatt::ui::RoutingStyle;
    using hatt::ui::RoutingStyleKind;
    QSettings().remove(QStringLiteral("editor/board"));
    QCOMPARE(hatt::ui::routingStyleProblem(RoutingStyleKind::Track, {QStringLiteral("t12"), 0.4}, {}).isEmpty(),
             false); // name taken (case-insensitive)
    QVERIFY(!hatt::ui::routingStyleProblem(RoutingStyleKind::Track, {QStringLiteral("POWER"), 0.0}, {}).isEmpty());
    QVERIFY(!hatt::ui::routingStyleProblem(RoutingStyleKind::Via, {QStringLiteral("VX"), 0.6, 0.6}, {}).isEmpty());
    QVERIFY(hatt::ui::routingStyleProblem(RoutingStyleKind::Track, {QStringLiteral("POWER"), 1.5}, {}).isEmpty());
    hatt::ui::setCustomRoutingStyles(RoutingStyleKind::Track, {{QStringLiteral("POWER"), 1.5}});

    hatt::ui::MainWindow window;
    window.showKayraWorkspace();
    action(window, "hatteda.tool.connect")->trigger();
    auto* selector = window.findChild<QListWidget*>(QStringLiteral("ObjectSelector"));
    auto* bar = window.findChild<QWidget*>(QStringLiteral("RoutingStyleBar"));
    auto* edit = window.findChild<QPushButton*>(QStringLiteral("hatteda.styles.edit"));
    auto* remove = window.findChild<QPushButton*>(QStringLiteral("hatteda.styles.delete"));
    QVERIFY(selector && bar && edit && remove);
    QVERIFY(!bar->isHidden());
    QCOMPARE(selector->count(), static_cast<int>(hatt::ui::trackStyles().size()) + 1);
    QVERIFY(!edit->isEnabled()); // T12, built in

    selector->setCurrentRow(selector->count() - 1);
    QVERIFY(selector->currentItem()->text().startsWith(QStringLiteral("POWER")));
    QVERIFY(edit->isEnabled() && remove->isEnabled());
    QCOMPARE(window.activeCanvas()->trackWidthSetting(), 1.5);

    remove->click();
    QCOMPARE(selector->count(), static_cast<int>(hatt::ui::trackStyles().size()));
    QVERIFY(hatt::ui::customRoutingStyles(RoutingStyleKind::Track).isEmpty());

    action(window, "hatteda.tool.select")->trigger();
    QVERIFY(bar->isHidden());
    QSettings().remove(QStringLiteral("editor/board"));
}

void MainWindowTests::userPadStylesAndLayerColors() {
    using hatt::ui::BoardLayer;
    using hatt::ui::PadStyleEntry;
    QSettings().remove(QStringLiteral("editor/board"));
    PadStyleEntry style;
    style.id = hatt::ui::UserPadStylePrefix + QStringLiteral("test");
    style.name = QStringLiteral("C-70-30");
    style.pad.shape = hatt::ui::PadShape::Round;
    style.pad.width = style.pad.height = 1.778;
    style.pad.drillDiameter = 0.762;
    QVERIFY(hatt::ui::padStyleProblem(style).isEmpty());
    PadStyleEntry badDrill = style;
    badDrill.pad.drillDiameter = 2.0;
    QVERIFY(!hatt::ui::padStyleProblem(badDrill).isEmpty());
    hatt::ui::setCustomPadStyles({style});

    hatt::ui::MainWindow window;
    window.resize(1440, 900);
    window.showKayraWorkspace();
    auto* board = window.activeCanvas();
    action(window, "hatteda.tool.pad")->trigger();
    auto* selector = window.findChild<QListWidget*>(QStringLiteral("ObjectSelector"));
    QCOMPARE(selector->count(), static_cast<int>(hatt::ui::padStyles().size()) + 1);
    QVERIFY(!window.findChild<QWidget*>(QStringLiteral("RoutingStyleBar"))->isHidden());
    selector->setCurrentRow(selector->count() - 1);
    QCOMPARE(board->toolVariant(), style.id);
    QVERIFY(window.findChild<QPushButton*>(QStringLiteral("hatteda.styles.delete"))->isEnabled());
    clickCanvas(board, {5.08, 5.08});
    QCOMPARE(board->document().size(), 1);
    QCOMPARE(board->document().first().pad.width, 1.778);
    QCOMPARE(board->document().first().pad.layers, hatt::ui::CopperLayerMask);

    window.findChild<QPushButton*>(QStringLiteral("hatteda.styles.delete"))->click();
    QCOMPARE(selector->count(), static_cast<int>(hatt::ui::padStyles().size()));
    QCOMPARE(board->document().first().pad.width, 1.778); // placed pads keep their definition

    QPalette dark;
    dark.setColor(QPalette::Window, QColor(QStringLiteral("#101418")));
    QCOMPARE(DesignCanvas::layerColor(BoardLayer::TopCopper, dark), QColor(QStringLiteral("#ff4d4d")));
    hatt::ui::setLayerColorOverride(BoardLayer::TopCopper, true, QColor(QStringLiteral("#ff8800")));
    QCOMPARE(DesignCanvas::layerColor(BoardLayer::TopCopper, dark), QColor(QStringLiteral("#ff8800")));
    hatt::ui::clearLayerColorOverrides(true);
    QCOMPARE(DesignCanvas::layerColor(BoardLayer::TopCopper, dark), QColor(QStringLiteral("#ff4d4d")));
    QSettings().remove(QStringLiteral("editor/board"));
}

void MainWindowTests::makePackageStoresFootprintAndDecomposeUndoes() {
    using hatt::ui::SketchItem;
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    window.showKayraWorkspace();
    auto* board = window.activeCanvas();

    hatt::ui::SketchDocument drawn;
    for (int i = 0; i < 2; ++i) {
        SketchItem pad;
        pad.kind = SketchItem::Kind::Pad;
        pad.points = {QPointF(10.0 + 2.54 * i, 10.0)};
        pad.pad.number = i + 1;
        pad.pad.shape = hatt::ui::PadShape::Round;
        pad.pad.width = pad.pad.height = 1.6;
        pad.pad.drillDiameter = 0.8;
        pad.pad.layers = hatt::ui::CopperLayerMask;
        drawn.append(pad);
    }
    SketchItem outline;
    outline.kind = SketchItem::Kind::Rectangle;
    outline.points = {QPointF(8.5, 8.5), QPointF(14.5, 11.5)};
    outline.layer = hatt::ui::BoardLayer::TopSilk;
    drawn.append(outline);
    board->applyDocumentEdit(QStringLiteral("Draw"), drawn);
    board->selectAll();
    QVERIFY(action(window, "hatteda.action.make-package")->isEnabled());

    bool dialogChecked = false;
    QTimer::singleShot(0, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->objectName(), QStringLiteral("MakePackageDialog"));
        auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        QVERIFY(!ok->isEnabled()); // no name yet
        QVERIFY(dialog->findChild<QLabel*>(QStringLiteral("PackageSummary"))->text().contains(QStringLiteral("2")));
        dialog->findChild<QLineEdit*>(QStringLiteral("PackageName"))->setText(QStringLiteral("CONN-2"));
        QVERIFY(ok->isEnabled());
        dialogChecked = true;
        ok->click();
    });
    action(window, "hatteda.action.make-package")->trigger();
    QVERIFY(dialogChecked);
    QCOMPARE(board->document().size(), 1);
    const SketchItem package = board->document().first();
    QCOMPARE(package.kind, SketchItem::Kind::Symbol);
    const auto* symbol = hatt::ui::findSymbol(package.variant);
    QVERIFY(symbol != nullptr);
    QCOMPARE(hatt::ui::symbolDisplayName(*symbol), QStringLiteral("CONN-2"));
    QCOMPARE(hatt::ui::itemPads(package).size(), 2);
    QVERIFY(window.isWindowModified());

    // Package mode offers the new footprint.
    action(window, "hatteda.tool.package")->trigger();
    auto* selector = window.findChild<QListWidget*>(QStringLiteral("ObjectSelector"));
    bool listed = false;
    for (int row = 0; row < selector->count(); ++row) {
        listed = listed || selector->item(row)->data(Qt::UserRole + 1).toString() == package.variant;
    }
    QVERIFY(listed);

    action(window, "hatteda.tool.select")->trigger();
    board->selectItem(0);
    action(window, "hatteda.action.decompose")->trigger();
    QCOMPARE(board->document().size(), 3);
    QCOMPARE(board->document().first().kind, SketchItem::Kind::Pad);
    action(window, "hatteda.action.undo")->trigger();
    QCOMPARE(board->document().size(), 1);
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

void MainWindowTests::designChecksReportAndRules() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    auto* checks = action(window, "hatteda.action.run-checks");
    QVERIFY(checks && !checks->isEnabled());
    activateEditor(window);
    QVERIFY(checks->isEnabled());
    auto* schematic = window.activeCanvas();
    schematic->setTool(CanvasTool::Symbol, QStringLiteral("schematic.resistor"));
    clickCanvas(schematic, {20.32, 20.32});
    schematic->setTool(CanvasTool::Select);
    schematic->clearSelection();

    checks->trigger();
    auto* host = window.findChild<QTabWidget*>(QStringLiteral("ToolWorkspaceHost"));
    QVERIFY(host);
    auto* report = qobject_cast<hatt::ui::ChecksReport*>(host->currentWidget());
    QVERIFY(report);
    QCOMPARE(report->objectName(), QStringLiteral("hatteda.tool.design-checks"));
    auto* table = report->findChild<QTreeWidget*>(QStringLiteral("ChecksTable"));
    QCOMPARE(table->topLevelItemCount(), report->violations().size());
    const auto& violations = report->violations();
    const auto pin = std::find_if(violations.begin(), violations.end(), [](const hatt::ui::CheckViolation& v) {
        return v.rule == QLatin1String("erc.unconnected-pin");
    });
    QVERIFY(pin != violations.end());
    // No board outline and an unplaced part are reported by the DRC.
    QVERIFY(std::any_of(violations.begin(), violations.end(), [](const auto& v) { return v.rule == QLatin1String("drc.not-placed"); }));

    // Clicking a problem shows it on its canvas.
    report->activateRow(static_cast<int>(pin - violations.begin()));
    QCOMPARE(window.activeCanvas(), schematic);
    QCOMPARE(schematic->selection(), QList<int>({0}));

    // Running again reuses the workspace.
    const int tabs = window.toolWorkspaceCount();
    checks->trigger();
    QCOMPARE(window.toolWorkspaceCount(), tabs);

    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->objectName(), QStringLiteral("DesignRuleManagerDialog"));
        auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        auto* padPad = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("RulePadPad"));
        padPad->setValue(0.0);
        QVERIFY(!ok->isEnabled());
        for (const char* name : {"RulePadPad", "RulePadTrace", "RuleTraceTrace", "RuleGraphic"}) {
            dialog->findChild<QDoubleSpinBox*>(QString::fromLatin1(name))->setValue(0.25);
        }
        QVERIFY(ok->isEnabled());
        ok->click();
    });
    QVERIFY(window.projectGuard() != nullptr);
    action(window, "hatteda.action.design-rules")->trigger();
    QCOMPARE(window.designRules().clearance, 0.25);
    QVERIFY(window.designRules().clearanceRules.isEmpty()); // one DEFAULT rule stays compact
    QVERIFY(window.isWindowModified());
    QVERIFY(window.saveProject());
    const auto saved = hatt::ui::loadProjectFile(window.projectPath());
    QVERIFY2(saved.ok(), qPrintable(saved.error));
    QCOMPARE(saved.project.rules.clearance, 0.25);
}

void MainWindowTests::designRuleManagerEditsRulesClassesPairsAndDefaults() {
    hatt::ui::DesignRules start;
    const QStringList nets{QStringLiteral("0"), QStringLiteral("N1"), QStringLiteral("VCC")};
    const QHash<QString, QString> automatic{{QStringLiteral("0"), hatt::ui::PowerNetClass},
                                            {QStringLiteral("N1"), hatt::ui::SignalNetClass},
                                            {QStringLiteral("VCC"), hatt::ui::PowerNetClass}};
    hatt::ui::DesignRuleManagerDialog dialog(start, nets, automatic);
    auto* ok = dialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
    QVERIFY(ok->isEnabled());
    QVERIFY(dialog.rules() == start); // nothing edited: compact rules equal the input

    // Design Rules: clone DEFAULT into a top copper rule with a tighter trace gap.
    auto* ruleList = dialog.findChild<QListWidget*>(QStringLiteral("RuleList"));
    QCOMPARE(ruleList->count(), 1);
    QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("RuleDelete"))->isEnabled());
    dialog.findChild<QPushButton*>(QStringLiteral("RuleClone"))->click();
    QCOMPARE(ruleList->count(), 2);
    dialog.findChild<QLineEdit*>(QStringLiteral("RuleName"))->setText(QStringLiteral("TOP"));
    auto* region = dialog.findChild<QComboBox*>(QStringLiteral("RuleRegion"));
    region->setCurrentIndex(region->findData(static_cast<int>(hatt::ui::RuleRegion::TopCopper)));
    dialog.findChild<QDoubleSpinBox*>(QStringLiteral("RuleTraceTrace"))->setValue(0.15);
    dialog.findChild<QLineEdit*>(QStringLiteral("RuleName"))->setText(QStringLiteral("DEFAULT"));
    QVERIFY(!ok->isEnabled()); // duplicate rule name
    dialog.findChild<QLineEdit*>(QStringLiteral("RuleName"))->setText(QStringLiteral("TOP"));
    QVERIFY(ok->isEnabled());

    // Net Classes: a new class that takes N1 and routes on top only.
    dialog.findChild<QPushButton*>(QStringLiteral("NetClassNew"))->click();
    auto* classCombo = dialog.findChild<QComboBox*>(QStringLiteral("NetClassCombo"));
    QCOMPARE(classCombo->count(), 3);
    dialog.findChild<QDoubleSpinBox*>(QStringLiteral("NetClassTraceWidth"))->setValue(0.5);
    auto* classClearance = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("NetClassClearance"));
    QVERIFY(classClearance);
    QCOMPARE(classClearance->value(), 0.0); // new classes follow the design rules
    classClearance->setValue(0.4);
    dialog.findChild<QCheckBox*>(QStringLiteral("NetClassBottom"))->setChecked(false);
    auto* available = dialog.findChild<QListWidget*>(QStringLiteral("NetClassAvailableNets"));
    for (int row = 0; row < available->count(); ++row) {
        available->item(row)->setSelected(available->item(row)->data(Qt::UserRole).toString() == QLatin1String("N1"));
    }
    dialog.findChild<QPushButton*>(QStringLiteral("NetClassAssign"))->click();
    QCOMPARE(dialog.findChild<QListWidget*>(QStringLiteral("NetClassNets"))->count(), 1);

    // Differential pair and defaults.
    dialog.findChild<QPushButton*>(QStringLiteral("PairAdd"))->click();
    auto* pairs = dialog.findChild<QTableWidget*>(QStringLiteral("PairTable"));
    QCOMPARE(pairs->rowCount(), 1);
    pairs->item(0, 4)->setText(QStringLiteral("abc"));
    QVERIFY(!ok->isEnabled()); // gap is not a number
    pairs->item(0, 4)->setText(QStringLiteral("0.18"));
    QVERIFY(ok->isEnabled());
    dialog.findChild<QCheckBox*>(QStringLiteral("DefaultThermalRelief"))->setChecked(false);

    const hatt::ui::DesignRules rules = dialog.rules();
    QVERIFY2(hatt::ui::validateDesignRules(rules).isEmpty(), qPrintable(hatt::ui::validateDesignRules(rules)));
    QCOMPARE(rules.clearanceRules.size(), 2);
    QCOMPARE(rules.clearanceRules[1].region, hatt::ui::RuleRegion::TopCopper);
    QCOMPARE(rules.clearanceRules[1].traceTrace, 0.15);
    QCOMPARE(rules.netClasses.size(), 3);
    QCOMPARE(rules.netClasses[2].nets, QStringList{QStringLiteral("N1")});
    QCOMPARE(rules.netClasses[2].traceWidth, 0.5);
    QCOMPARE(rules.netClasses[2].clearance, 0.4);
    QCOMPARE(rules.netClasses[0].clearance, 0.0);
    QCOMPARE(rules.netClasses[2].layers, hatt::ui::layerBit(hatt::ui::BoardLayer::TopCopper));
    QCOMPARE(rules.differentialPairs.size(), 1);
    QCOMPARE(rules.differentialPairs.first().gap, 0.18);
    QVERIFY(!rules.defaults.thermalRelief);
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

void MainWindowTests::assemblyExportsWriteCsv() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    QVERIFY(!action(window, "hatteda.action.export-bom")->isEnabled());
    activateEditor(window);
    QVERIFY(action(window, "hatteda.action.export-bom")->isEnabled());
    QVERIFY(action(window, "hatteda.action.export-pick-place")->isEnabled());
    auto* schematic = window.activeCanvas();
    schematic->setTool(CanvasTool::Symbol, QStringLiteral("schematic.resistor"));
    clickCanvas(schematic, {20.32, 20.32});
    schematic->setTool(CanvasTool::Select);
    window.showKayraWorkspace();
    auto* board = window.activeCanvas();
    board->setTool(CanvasTool::Symbol, QStringLiteral("board.r0603"));
    clickCanvas(board, {10.0, 10.0});
    board->setTool(CanvasTool::Select);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString bomPath = directory.filePath(QStringLiteral("bom.csv"));
    QVERIFY(window.exportBom(bomPath));
    QFile bom(bomPath);
    QVERIFY(bom.open(QIODevice::ReadOnly));
    const QByteArray bomText = bom.readAll();
    QVERIFY(bomText.startsWith("Item,Quantity,References"));
    QVERIFY(bomText.contains(",1,R1,1k,"));

    const QString placementPath = directory.filePath(QStringLiteral("placement.csv"));
    QVERIFY(window.exportPlacement(placementPath));
    QFile placement(placementPath);
    QVERIFY(placement.open(QIODevice::ReadOnly));
    const QByteArray placementText = placement.readAll();
    QVERIFY(placementText.startsWith("Designator,Value,Package"));
    QCOMPARE(placementText.count('\n'), 2);
}

void MainWindowTests::fabricationExportAsksAboutRuleErrors() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    window.showKayraWorkspace();
    auto* board = window.activeCanvas();
    hatt::ui::SketchItem thin;
    thin.kind = hatt::ui::SketchItem::Kind::Wire;
    thin.layer = hatt::ui::BoardLayer::TopCopper;
    thin.points = {QPointF(1.0, 1.0), QPointF(10.0, 1.0)};
    thin.width = 0.05; // below the default minimum track width
    board->applyDocumentEdit(QStringLiteral("Draw"), {thin});

    // Cancel: nothing happens and no tool workspace opens.
    bool asked = false;
    QTimer::singleShot(0, [&] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        QVERIFY(box);
        QCOMPARE(box->objectName(), QStringLiteral("FabricationChecksDialog"));
        asked = true;
        box->button(QMessageBox::Cancel)->click();
    });
    action(window, "hatteda.action.export-fabrication")->trigger();
    QVERIFY(asked);
    QCOMPARE(window.toolWorkspaceCount(), 0);

    // Open report shows the design checks instead of exporting.
    asked = false;
    QTimer::singleShot(0, [&] {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        QVERIFY(box);
        box->findChild<QAbstractButton*>(QStringLiteral("hatteda.fabrication.open-report"))->click();
        asked = true;
    });
    action(window, "hatteda.action.export-fabrication")->trigger();
    QVERIFY(asked);
    QCOMPARE(window.toolWorkspaceCount(), 1);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("hatteda.tool.design-checks")) != nullptr);
}

void MainWindowTests::zoneNetPropertyPoursOnTheCanvas() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    window.showKayraWorkspace();
    auto* board = window.activeCanvas();
    hatt::ui::SketchItem zone;
    zone.kind = hatt::ui::SketchItem::Kind::Polyline;
    zone.variant = hatt::ui::CopperZoneVariant;
    zone.closed = true;
    zone.layer = hatt::ui::BoardLayer::TopCopper;
    zone.points = {QPointF(0, 0), QPointF(20, 0), QPointF(20, 20), QPointF(0, 20)};
    board->applyDocumentEdit(QStringLiteral("Draw"), {zone});
    QVERIFY(board->zoneFills().isEmpty()); // no net: not poured

    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto* net = dialog->findChild<QComboBox*>(QStringLiteral("ItemZoneNet"));
        QVERIFY(net);
        QCOMPARE(net->currentData().toString(), QString());
        net->setEditText(QStringLiteral("GND"));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    board->contextMenuRequested(board->mapToGlobal(QPoint(100, 100)), 0);
    auto* menu = window.findChild<QMenu*>(QStringLiteral("CanvasContextMenu"));
    QVERIFY(menu);
    auto* properties = menu->findChild<QAction*>(QStringLiteral("hatteda.context.properties"));
    QVERIFY(properties);
    menu->hide();
    properties->trigger();
    QCOMPARE(board->document().first().net, QStringLiteral("GND"));
    QCOMPARE(board->zoneFills().size(), 1);
    // Poured, but nothing on the board belongs to GND yet, so the whole pour is an island.
    QVERIFY(board->zoneFills().contains(board->document().first().id));
    QVERIFY(board->zoneFills().value(board->document().first().id).isEmpty());

    action(window, "hatteda.action.undo")->trigger();
    QVERIFY(board->document().first().net.isEmpty());
    QVERIFY(board->zoneFills().isEmpty());
}

void MainWindowTests::moveToLayerContextMenuChangesTextItemLayer() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    window.showKayraWorkspace();
    auto* board = window.activeCanvas();
    hatt::ui::SketchItem text;
    text.kind = hatt::ui::SketchItem::Kind::Text;
    text.label = QStringLiteral("REV A");
    text.layer = hatt::ui::BoardLayer::TopSilk;
    text.points = {QPointF(5, 5)};
    board->applyDocumentEdit(QStringLiteral("Place text"), {text});

    board->contextMenuRequested(board->mapToGlobal(QPoint(100, 100)), 0);
    auto* menu = window.findChild<QMenu*>(QStringLiteral("CanvasContextMenu"));
    QVERIFY(menu);
    auto* layerMenu = menu->findChild<QMenu*>(QStringLiteral("hatteda.context.move-to-layer"));
    QVERIFY(layerMenu);
    QAction* bottomSilkAction = nullptr;
    for (QAction* candidate : layerMenu->actions()) {
        if (candidate->text() == hatt::ui::boardLayerName(hatt::ui::BoardLayer::BottomSilk)) {
            bottomSilkAction = candidate;
        }
    }
    QVERIFY(bottomSilkAction);
    menu->hide();
    bottomSilkAction->trigger();
    // Bottom-side text mirrors automatically at paint time (DesignCanvas::drawItem checks
    // isBottomLayer), so a plain layer change is enough to match Gerber bottom-silk convention.
    QCOMPARE(board->document().first().layer, hatt::ui::BoardLayer::BottomSilk);

    action(window, "hatteda.action.undo")->trigger();
    QCOMPARE(board->document().first().layer, hatt::ui::BoardLayer::TopSilk);
}

void MainWindowTests::textStyleBarShowsFontAndAppliesToNewText() {
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    auto* selector = window.findChild<QListWidget*>(QStringLiteral("ObjectSelector"));
    auto* styleBar = window.findChild<QWidget*>(QStringLiteral("TextStyleBar"));
    auto* fontCombo = window.findChild<QFontComboBox*>(QStringLiteral("TextFontCombo"));
    auto* sizeSpin = window.findChild<QDoubleSpinBox*>(QStringLiteral("TextSizeSpin"));
    QVERIFY(selector && styleBar && fontCombo && sizeSpin);

    action(window, "hatteda.tool.draw")->trigger();
    QVERIFY(styleBar->isHidden());
    selector->setCurrentRow(rowForTool(selector, CanvasTool::Text));
    QVERIFY(!styleBar->isHidden());
    QVERIFY(!fontCombo->isHidden()); // schematic text: a QFont family applies

    // Pick a font the offscreen test platform actually has, rather than assuming a specific
    // family is installed; what matters here is that whatever the combo shows reaches the item.
    if (fontCombo->count() > 1) fontCombo->setCurrentIndex(1);
    const QString expectedFamily = fontCombo->currentFont().family();
    sizeSpin->setValue(5.0);

    bool answered = false;
    QTimer::singleShot(50, [&answered] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        dialog->findChild<QLineEdit*>(QStringLiteral("TextContent"))->setText(QStringLiteral("NOTE"));
        answered = true;
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    clickCanvas(window.activeCanvas(), {10.0, 10.0});
    QTRY_VERIFY(answered);
    QTRY_COMPARE(window.activeCanvas()->document().size(), 1);
    QCOMPARE(window.activeCanvas()->document().first().fontFamily, expectedFamily);
    QCOMPARE(window.activeCanvas()->document().first().width, 5.0);

    // Board fabrication text always uses the fixed StrokeFont, so the font choice is hidden there.
    window.showKayraWorkspace();
    action(window, "hatteda.tool.draw")->trigger();
    auto* boardSelector = window.findChild<QListWidget*>(QStringLiteral("ObjectSelector"));
    boardSelector->setCurrentRow(rowForTool(boardSelector, CanvasTool::Text));
    QVERIFY(!styleBar->isHidden());
    QVERIFY(fontCombo->isHidden());
}

void MainWindowTests::zoneModeDrawsZonesAndListsThem() {
    using hatt::ui::SketchItem;
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    auto* selector = window.findChild<QListWidget*>(QStringLiteral("ObjectSelector"));
    auto* zones = window.findChild<QListWidget*>(QStringLiteral("ZoneList"));
    QVERIFY(selector && zones);
    auto* zoneMode = action(window, "hatteda.tool.zone");
    QCOMPARE(zoneMode->shortcut(), QKeySequence(QStringLiteral("Z")));
    QVERIFY(!zoneMode->isEnabled()); // Kayra only

    window.showKayraWorkspace();
    auto* board = window.activeCanvas();
    QVERIFY(zoneMode->isEnabled());
    // Zones left the 2D graphics list for their own mode.
    action(window, "hatteda.tool.draw")->trigger();
    for (int row = 0; row < selector->count(); ++row) {
        QVERIFY(!hatt::ui::isZoneVariant(selector->item(row)->data(Qt::UserRole + 1).toString()));
    }
    QVERIFY(zones->isHidden());

    zoneMode->trigger();
    QVERIFY(zoneMode->isChecked());
    QVERIFY(!action(window, "hatteda.tool.draw")->isChecked());
    QCOMPARE(selector->count(), 3);
    QCOMPARE(board->tool(), CanvasTool::Zone);
    QCOMPARE(board->toolVariant(), hatt::ui::CopperZoneVariant);
    QVERIFY(!zones->isHidden());
    QCOMPARE(zones->count(), 0);

    auto drawSquare = [board](double x) {
        for (const QPointF& corner : {QPointF(x, 0), QPointF(x + 12.7, 0), QPointF(x + 12.7, 12.7), QPointF(x, 12.7)}) {
            clickCanvas(board, corner);
        }
        clickCanvas(board, QPointF(x, 0)); // the first corner closes the zone
    };
    drawSquare(0.0);
    QCOMPARE(board->document().size(), 1);
    const SketchItem copper = board->document().first();
    QCOMPARE(copper.variant, hatt::ui::CopperZoneVariant);
    QVERIFY(copper.closed);
    QCOMPARE(copper.points.size(), 4);
    QVERIFY(hatt::ui::isCopperLayer(copper.layer));
    QCOMPARE(zones->count(), 1);
    QVERIFY2(zones->item(0)->text().startsWith(QStringLiteral("No net, Solid")), qPrintable(zones->item(0)->text()));

    selector->setCurrentRow(1);
    QCOMPARE(board->toolVariant(), hatt::ui::KeepoutZoneVariant);
    drawSquare(25.4);
    QCOMPARE(board->document().size(), 2);
    QCOMPARE(board->document().last().variant, hatt::ui::KeepoutZoneVariant);
    QCOMPARE(zones->count(), 2);
    QVERIFY(zones->item(1)->text().startsWith(QStringLiteral("Keepout")));

    selector->setCurrentRow(2);
    drawSquare(50.8);
    QCOMPARE(board->document().last().variant, hatt::ui::AreaZoneVariant);
    QVERIFY(!hatt::ui::isCopperLayer(board->document().last().layer));
    QVERIFY(zones->item(2)->text().startsWith(QStringLiteral("Area, Solid")));
    QVERIFY(board->zoneFills().contains(board->document().last().id)); // area fill drawn on the canvas

    // The list and the board selection follow each other.
    emit zones->itemClicked(zones->item(0));
    QCOMPARE(board->selection(), QList<int>{0});
    board->revealItems({board->document().at(1).id}, std::nullopt);
    QCOMPARE(zones->currentRow(), 1);

    // Double-click edits the copper zone: net and fill show up in its summary.
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto* fill = dialog->findChild<QComboBox*>(QStringLiteral("ItemZoneFill"));
        QVERIFY(fill);
        fill->setCurrentIndex(fill->findData(static_cast<int>(hatt::ui::ZoneFillStyle::Hatched)));
        dialog->findChild<QComboBox*>(QStringLiteral("ItemZoneNet"))->setEditText(QStringLiteral("GND"));
        dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
    });
    emit zones->itemDoubleClicked(zones->item(0));
    QCOMPARE(board->document().first().zoneFill, hatt::ui::ZoneFillStyle::Hatched);
    QCOMPARE(board->document().first().net, QStringLiteral("GND"));
    QVERIFY2(zones->item(0)->text().startsWith(QStringLiteral("GND=")) && zones->item(0)->text().contains(QStringLiteral("Hatched")),
             qPrintable(zones->item(0)->text()));

    // Keepouts have no fill style to edit.
    QTimer::singleShot(0, [] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QVERIFY(dialog->findChild<QComboBox*>(QStringLiteral("ItemZoneFill")) == nullptr);
        QVERIFY(dialog->findChild<QComboBox*>(QStringLiteral("ItemZoneNet")) == nullptr);
        dialog->reject();
    });
    emit zones->itemDoubleClicked(zones->item(1));

    window.showMergenWorkspace();
    QVERIFY(!zoneMode->isEnabled());
    QVERIFY(action(window, "hatteda.tool.select")->isChecked());
    QVERIFY(zones->isHidden());
}

void MainWindowTests::trackModeRoutesWithNetClassWidths() {
    using hatt::ui::SketchItem;
    hatt::ui::MainWindow window;
    QVERIFY(showActive(window));
    activateEditor(window);
    const hatt::ui::SketchDocument schematic = hatt::ui::dcDividerExample();
    window.activeCanvas()->applyDocumentEdit(QStringLiteral("Schematic"), schematic);
    SketchItem outline;
    outline.kind = SketchItem::Kind::Polyline;
    outline.variant = hatt::ui::BoardOutlineVariant;
    outline.closed = true;
    outline.layer = hatt::ui::BoardLayer::BoardEdge;
    outline.points = {{0, 0}, {80, 0}, {80, 60}, {0, 60}};
    const hatt::ui::SketchDocument placed = hatt::ui::transferToBoard(schematic, {outline}).document;
    window.showKayraWorkspace();
    auto* board = window.activeCanvas();
    board->applyDocumentEdit(QStringLiteral("Board"), placed);
    const auto r2 = *std::find_if(placed.begin(), placed.end(), [](const SketchItem& i) { return i.label == QLatin1String("R2"); });
    const QPointF ground = hatt::ui::itemPads(r2)[1].center; // net 0: POWER, 0.635 mm

    action(window, "hatteda.tool.connect")->trigger();
    QCOMPARE(board->tool(), CanvasTool::Wire);
    clickCanvas(board, ground);
    QVERIFY(board->activeRouteClass().has_value());
    QCOMPARE(board->activeRouteClass()->netClass, hatt::ui::PowerNetClass);
    QCOMPARE(board->activeRouteClass()->traceWidth, 0.635);
    board->cancelOperation();
    QVERIFY(!board->activeRouteClass().has_value());
}

QTEST_MAIN(MainWindowTests)
#include "MainWindowTests.moc"
