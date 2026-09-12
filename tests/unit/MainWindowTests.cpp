#include "hatt/ui/DesignCanvas.hpp"
#include "hatt/ui/MainWindow.hpp"
#include "hatt/ui/Theme.hpp"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QImage>
#include <QListWidget>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QUndoStack>
#include <QtTest>

using hatt::ui::CanvasTool;
using hatt::ui::DesignCanvas;
using hatt::ui::Workspace;

namespace {

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
    void selectionStatesFollowTheme_data();
    void selectionStatesFollowTheme();
};

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
