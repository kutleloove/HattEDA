#include "hatt/ui/SketchCircuit.hpp"
#include "hatt/ui/CircuitWorkflow.hpp"
#include "hatt/ui/MainWindow.hpp"
#include "hatt/ui/Theme.hpp"
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QFontDatabase>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QMenu>
#include <QTextEdit>
#include <QUndoStack>
#include <QtTest>

using namespace hatt::ui;
class SketchCircuitTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        if (!qEnvironmentVariable("HATT_SCREENSHOT_DIR").isEmpty()) {
            const QString font = qEnvironmentVariable("SystemRoot") + "/Fonts/segoeui.ttf";
            QVERIFY(QFontDatabase::addApplicationFont(font) >= 0);
        }
        static QTemporaryDir settings;
        QVERIFY(settings.isValid());
        QCoreApplication::setOrganizationName("HattEDA-CircuitTests");
        QCoreApplication::setApplicationName("hatt-sketch-circuit-tests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        // New projects are real files; keep them out of the user's Documents folder.
        QSettings().setValue("projects/location", settings.filePath("projects"));
    }
    void dividerSolvesFromActualSymbolPins() {
        const auto document = dcDividerExample();
        const auto snapshot = analyzeSchematic(document);
        QVERIFY2(snapshot.errors.isEmpty(), qPrintable(snapshot.errors.join("; ")));
        QVERIFY(snapshot.simulationErrors.isEmpty());
        QCOMPARE(snapshot.dc.netCount, 3);
        const auto result = hatt::electrical::solveDc(snapshot.dc);
        QVERIFY2(result.success, result.error.c_str());
        QCOMPARE(result.currents.size(), std::size_t(3));
        QVERIFY(std::abs(result.currents[1] - 0.0025) < 1e-10);
        QVERIFY(std::abs(result.currents[2] - 0.0025) < 1e-10);
        QVERIFY(std::abs(result.currents[0] + 0.0025) < 1e-10);
    }
    void transferPreservesPlacementAndRouting() {
        auto schematic = dcDividerExample();
        auto transfer = transferToBoard(schematic, {});
        QVERIFY2(transfer.errors.isEmpty(), qPrintable(transfer.errors.join("; ")));
        QCOMPARE(transfer.added, 3);
        auto board = transfer.document;
        // Footprints are added in designator order; board[1] is linked to schematic[1] (R1).
        const auto r1 = std::find_if(board.begin(), board.end(),
                                     [&](const SketchItem& item) { return item.sourceId == schematic[1].id; });
        QVERIFY(r1 != board.end());
        std::swap(*r1, board[1]);
        translateItem(board[1], {20, 10});
        board[1].quarterTurns = 1;
        const auto guide = boardGuidance(schematic, board);
        QVERIFY2(guide.errors.isEmpty(), qPrintable(guide.errors.join("; ")));
        QCOMPARE(guide.airwires.size(), 3);
        SketchItem track;
        track.kind = SketchItem::Kind::Wire;
        track.points = {guide.airwires[0].p1(), guide.airwires[0].p2()};
        board.append(track);
        const auto routed = boardGuidance(schematic, board);
        QVERIFY(routed.airwires.size() < guide.airwires.size());
        transfer = transferToBoard(schematic, board);
        QVERIFY(transfer.errors.isEmpty());
        QCOMPARE(transfer.added, 0);
        QCOMPARE(transfer.updated, 0);
        QCOMPARE(transfer.document[1].points, board[1].points);
        QCOMPARE(transfer.document[1].quarterTurns, 1);
        QCOMPARE(transfer.document.last().points, track.points);
        schematic[1].label = "R42";
        transfer = transferToBoard(schematic, board);
        QCOMPARE(transfer.updated, 1);
        QCOMPARE(transfer.document[1].label, QString("R42"));
        schematic[1].pinPadMap = {2, 1};
        transfer = transferToBoard(schematic, board);
        QVERIFY(!transfer.errors.isEmpty());
        QCOMPARE(transfer.document[1].label, board[1].label);
    }
    void rejectsMissingFootprintAndDuplicateLink() {
        auto schematic = dcDividerExample();
        schematic[1].footprint.clear();
        QVERIFY(!transferToBoard(schematic, {}).errors.isEmpty());
        schematic = dcDividerExample();
        auto board = transferToBoard(schematic, {}).document;
        auto duplicate = board.first();
        duplicate.id = "different-id";
        board.append(duplicate);
        QVERIFY(!transferToBoard(schematic, board).errors.isEmpty());
        auto& resistor = schematic[1];
        resistor.value = "NaN";
        QVERIFY(!analyzeSchematic(schematic).simulationErrors.isEmpty());
    }
    void detectsCopperShortAndUnsupportedSimulation() {
        auto schematic = dcDividerExample();
        auto board = transferToBoard(schematic, {}).document;
        const auto& source = board.first();
        const auto* footprint = findSymbol(source.variant);
        SketchItem track;
        track.kind = SketchItem::Kind::Wire;
        track.points = {symbolToWorld(source, footprint->pins[0]), symbolToWorld(source, footprint->pins[1])};
        board.append(track);
        QVERIFY(boardGuidance(schematic, board).errors.join(" ").contains("short"));
        schematic[1].variant = QStringLiteral("schematic.capacitor");
        QVERIFY(analyzeSchematic(schematic).simulationErrors.join(" ").contains("does not support"));
    }
    void actualWorkflowReportsAndUndoUpdatesGuidance() {
        QWidget host;
        QMenu menu;
        DesignCanvas schematic(Workspace::Schematic), board(Workspace::Board);
        QHash<QString, QPointer<QTextEdit>> reports;
        CircuitWorkflow flow(&host, &menu, &schematic, &board,
            [&](const QString& id, const QString&, QWidget* widget) {
                widget->setParent(&host);
                reports[id] = qobject_cast<QTextEdit*>(widget);
            }, [] { return true; }, [] {});
        flow.loadExample();
        QCOMPARE(schematic.document().size(), 7);
        flow.showNetlist();
        auto* netlist = reports["hatteda.tool.netlist"].data();
        QVERIFY(netlist);
        QVERIFY(netlist->toPlainText().contains("R1"));
        flow.showNetlist();
        QCOMPARE(reports["hatteda.tool.netlist"].data(), netlist);
        flow.updateBoard();
        QCOMPARE(board.document().size(), 3);
        QCOMPARE(board.airwires().size(), 3);
        flow.updateBoard();
        QCOMPARE(board.undoStack()->count(), 1);
        board.undoStack()->undo();
        QVERIFY(board.document().isEmpty());
        QVERIFY(board.airwires().isEmpty());
        board.undoStack()->redo();
        QCOMPARE(board.airwires().size(), 3);
        flow.runDc();
        auto* sim = reports["hatteda.tool.dc-results"].data();
        QVERIFY(sim);
        QTRY_VERIFY_WITH_TIMEOUT(sim->toPlainText().contains("2.5"), 5000);
        flow.runDc();
        QTRY_VERIFY_WITH_TIMEOUT(sim->toPlainText().contains("2.5"), 5000);
        schematic.undoStack()->undo();
        QVERIFY(sim->toPlainText().contains("out of date"));
    }
    void boardPartsFollowSchematicPlacement() {
        auto schematic = dcDividerExample();
        auto waiting = unplacedBoardParts(schematic, {});
        QVERIFY(waiting.problems.isEmpty());
        QCOMPARE(waiting.parts.size(), 3);
        QCOMPARE(waiting.parts[0].label, QString("R1"));
        QCOMPARE(waiting.parts[1].label, QString("R2"));
        QCOMPARE(waiting.parts[2].label, QString("V1"));
        QCOMPARE(waiting.parts[0].sourceId, schematic[1].id);
        QCOMPARE(waiting.parts[0].variant, QString("board.r0603"));

        // A placed footprint leaves the list; an excluded component never enters it.
        SketchDocument board = {waiting.parts[0]};
        schematic[2].excludeFromBoard = true;
        waiting = unplacedBoardParts(schematic, board);
        QCOMPARE(waiting.parts.size(), 1);
        QCOMPARE(waiting.parts[0].label, QString("V1"));
        const auto transfer = transferToBoard(schematic, board);
        QVERIFY2(transfer.errors.isEmpty(), qPrintable(transfer.errors.join("; ")));
        QCOMPARE(transfer.added, 1);
        const auto guide = boardGuidance(schematic, transfer.document);
        QVERIFY(!guide.errors.join(" ").contains("R2"));

        // Without a footprint the component is reported instead of listed.
        schematic[1].footprint.clear();
        waiting = unplacedBoardParts(schematic, {});
        QCOMPARE(waiting.parts.size(), 1);
        QCOMPARE(waiting.problems.size(), 1);
    }
    void autoPlacerKeepsPartsInsideOutlineWithoutOverlap() {
        SketchItem outline;
        outline.kind = SketchItem::Kind::Polyline;
        outline.variant = BoardOutlineVariant;
        outline.closed = true;
        outline.points = {{0, 0}, {40, 0}, {40, 30}, {0, 30}};
        SketchItem existing;
        existing.kind = SketchItem::Kind::Symbol;
        existing.variant = "board.dip8";
        existing.points = {{8, 8}};
        const SketchDocument board = {outline, existing};
        SketchDocument parts;
        for (int i = 0; i < 6; ++i) {
            SketchItem part;
            part.kind = SketchItem::Kind::Symbol;
            part.variant = i % 2 ? "board.soic8" : "board.r0603";
            part.label = QString("U%1").arg(i + 1);
            parts.append(part);
        }
        const auto placed = autoPlaceParts(board, parts, 1.27);
        QCOMPARE(placed.size(), board.size() + parts.size());
        QVector<QRectF> bounds;
        for (int i = 1; i < placed.size(); ++i) bounds.append(itemBounds(placed[i]));
        for (int i = 0; i < bounds.size(); ++i) {
            QVERIFY2(QRectF(0, 0, 40, 30).contains(bounds[i]), qPrintable(placed[i + 1].label));
            for (int j = i + 1; j < bounds.size(); ++j) QVERIFY(!bounds[i].intersects(bounds[j]));
        }
        for (int i = 2; i < placed.size(); ++i) {
            const QPointF origin = placed[i].points.first();
            QVERIFY(std::abs(origin.x() / 1.27 - std::round(origin.x() / 1.27)) < 1e-6);
        }
    }
    void netlistTextListsPartsAndNets() {
        const auto text = netlistText(dcDividerExample());
        QVERIFY(text.contains("*PARTS"));
        QVERIFY(text.contains("R1 schematic.resistor 1k board.r0603"));
        QVERIFY(text.contains("*NETS"));
        QVERIFY(text.contains(QRegularExpression("\\n0: .*V1\\.2")));
        QStringList errors;
        auto broken = dcDividerExample();
        broken[1].label = "V1";
        QVERIFY(netlistText(broken, &errors).isEmpty());
        QVERIFY(!errors.isEmpty());
    }
    void autoPlacerDialogPlacesPartsInOneStep() {
        MainWindow window;
        window.resize(1440, 900);
        window.show();
        QTimer::singleShot(0, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->accept();
        });
        window.createNewProject();
        auto* flow = window.findChild<CircuitWorkflow*>();
        flow->loadExample();
        window.showKayraWorkspace();
        auto* board = window.activeCanvas();
        SketchItem outline;
        outline.kind = SketchItem::Kind::Polyline;
        outline.variant = BoardOutlineVariant;
        outline.closed = true;
        outline.points = {{0, 0}, {50, 0}, {50, 40}, {0, 40}};
        board->applyDocumentEdit("outline", {outline});

        QTimer::singleShot(0, [] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            QCOMPARE(dialog->objectName(), QString("AutoPlacerDialog"));
            dialog->findChild<QDoubleSpinBox*>("AutoPlacerGrid")->setValue(2.54);
            dialog->findChild<QDoubleSpinBox*>("AutoPlacerSpacing")->setValue(5.0);
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        window.findChild<QAction*>("hatteda.action.auto-place")->trigger();
        QCOMPARE(board->document().size(), 4);
        QCOMPARE(board->undoStack()->count(), 2);
        for (int i = 1; i < 4; ++i) {
            QVERIFY(QRectF(0, 0, 50, 40).contains(itemBounds(board->document()[i])));
            QVERIFY(!board->document()[i].sourceId.isEmpty());
        }
        QCOMPARE(QSettings().value("pcb/autoPlacer/spacing").toDouble(), 5.0);
        QCOMPARE(board->airwires().size(), 3);
        QCOMPARE(flow->autoPlace(1.27, 2.54), 0);
        QCOMPARE(board->undoStack()->count(), 2);
    }
    void duplicateCreatesNewIdentity() {
        DesignCanvas canvas(Workspace::Schematic);
        const auto document = dcDividerExample();
        canvas.restore({document[1]}, {0});
        canvas.duplicateSelection();
        QCOMPARE(canvas.document().size(), 2);
        QVERIFY(canvas.document()[0].id != canvas.document()[1].id);
        QVERIFY(analyzeSchematic(canvas.document()).errors.isEmpty());
        canvas.undoStack()->undo();
        QCOMPARE(canvas.document().first().id, document[1].id);
    }
    void hostReusesReportsAndShowsBothThemes() {
        MainWindow window;
        window.resize(1440, 900);
        window.show();
        QTimer::singleShot(0, [] {
            if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) dialog->accept();
        });
        window.createNewProject();
        auto* flow = window.findChild<CircuitWorkflow*>();
        QVERIFY(flow);
        flow->loadExample();
        flow->showNetlist();
        QPointer<QTextEdit> netlist = window.findChild<QTextEdit*>("hatteda.tool.netlist");
        QVERIFY(netlist);
        flow->showNetlist();
        QVERIFY(netlist);
        QCOMPARE(window.toolWorkspaceCount(), 1);
        flow->runDc();
        auto* simulation = window.findChild<QTextEdit*>("hatteda.tool.dc-results");
        QVERIFY(simulation);
        QTRY_VERIFY_WITH_TIMEOUT(simulation->toPlainText().contains("2.5"), 5000);
        flow->runDc();
        QTRY_VERIFY_WITH_TIMEOUT(simulation->toPlainText().contains("2.5"), 5000);
        QCOMPARE(window.toolWorkspaceCount(), 2);
        const QString directory = qEnvironmentVariable("HATT_SCREENSHOT_DIR");
        for (auto mode : {ThemeMode::Dark, ThemeMode::Light}) {
            Theme::apply(*qApp, mode);
            const QString suffix = mode == ThemeMode::Dark ? "dark" : "light";
            auto capture = [&](QString name) {
                QCoreApplication::processEvents();
                if (!directory.isEmpty()) {
                    QVERIFY(QDir().mkpath(directory));
                    QVERIFY(window.grab().save(directory + '/' + name + '-' + suffix + ".png"));
                }
            };
            flow->showNetlist(); capture("netlist");
            window.showMergenWorkspace(); capture("schematic");
            flow->updateBoard(); capture("pcb");
            flow->runDc();
            QTRY_VERIFY_WITH_TIMEOUT(simulation->toPlainText().contains("2.5"), 5000);
            capture("dc");
            const auto rendered = simulation->viewport()->grab().toImage();
            const QColor background = rendered.pixelColor(rendered.width() - 10, rendered.height() - 10);
            QCOMPARE(background.lightness() > 128, mode == ThemeMode::Light);
        }
    }
};
QTEST_MAIN(SketchCircuitTests)
#include "SketchCircuitTests.moc"
