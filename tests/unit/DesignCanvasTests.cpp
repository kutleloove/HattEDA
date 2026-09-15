#include "hatt/ui/DesignCanvas.hpp"
#include "hatt/ui/SketchCircuit.hpp"
#include "hatt/ui/Units.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QUndoStack>
#include <QtTest>

using hatt::ui::AlignOperation;
using hatt::ui::Airwire;
using hatt::ui::CanvasTool;
using hatt::ui::DesignCanvas;
using hatt::ui::BoardLayer;
using hatt::ui::SketchItem;
using hatt::ui::SnapSettings;
using hatt::ui::Workspace;

namespace {

const QString Resistor = QStringLiteral("schematic.resistor");

bool samePoint(QPointF a, QPointF b) { return QLineF(a, b).length() < 1e-6; }

void sendMouse(DesignCanvas& canvas, QEvent::Type type, QPointF world, Qt::MouseButton button,
               Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const QPointF position = canvas.worldToScreen(world);
    QMouseEvent event(type, position, canvas.mapToGlobal(position), button, buttons, modifiers);
    QApplication::sendEvent(&canvas, &event);
}

void click(DesignCanvas& canvas, QPointF world, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    sendMouse(canvas, QEvent::MouseMove, world, Qt::NoButton, Qt::NoButton, modifiers);
    sendMouse(canvas, QEvent::MouseButtonPress, world, Qt::LeftButton, Qt::LeftButton, modifiers);
    sendMouse(canvas, QEvent::MouseButtonRelease, world, Qt::LeftButton, Qt::NoButton, modifiers);
}

void drag(DesignCanvas& canvas, QPointF from, QPointF to) {
    sendMouse(canvas, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    for (int step = 1; step <= 4; ++step) {
        sendMouse(canvas, QEvent::MouseMove, from + (to - from) * step / 4.0, Qt::NoButton,
                  Qt::LeftButton);
    }
    sendMouse(canvas, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
}

void placeResistor(DesignCanvas& canvas, QPointF world) {
    canvas.setTool(CanvasTool::Symbol, Resistor);
    click(canvas, world);
}

SketchItem resistorAt(QPointF world) {
    SketchItem item;
    item.kind = SketchItem::Kind::Symbol;
    item.variant = Resistor;
    item.points = {world};
    return item;
}

SketchItem wireThrough(std::initializer_list<QPointF> points) {
    SketchItem item;
    item.kind = SketchItem::Kind::Wire;
    item.points = points;
    return item;
}

SketchItem smdPadAt(QPointF point, BoardLayer layer, double width = 1.0,
                    double height = 1.8) {
    SketchItem item;
    item.kind = SketchItem::Kind::Pad;
    item.points = {point};
    item.layer = layer;
    item.pad.width = width;
    item.pad.height = height;
    item.pad.drillDiameter = 0.0;
    return item;
}

bool samePoints(const QVector<QPointF>& actual, std::initializer_list<QPointF> expected) {
    if (actual.size() != static_cast<qsizetype>(expected.size())) return false;
    qsizetype i = 0;
    for (const QPointF& point : expected) {
        if (!samePoint(actual[i++], point)) return false;
    }
    return true;
}

} // namespace

class DesignCanvasTests final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void placingSymbolIsUndoable();
    void designatorsIncrement();
    void undoKeepsActiveTool();
    void wireSnapsToGridAndEndsOnPin();
    void polylineSupportsBackspaceAndEnter();
    void rectangleByDragAndLineByTwoClicks();
    void orthogonalSnapConstrainsLines();
    void movingSelectionIsOneUndoStep();
    void clickWithoutMovingDoesNotCreateUndoEntry();
    void deleteUndoRestoresSelection();
    void rubberBandSelectsItems();
    void alignAndDistribute();
    void rotateTurnsPins();
    void measureDoesNotChangeDocument();
    void escapeCancelsThenRequestsSelection();

    // Automated regression coverage supporting issue #2; not manual verification.
    void escapeIsLayered();
    void escapeDuringDragCancelsMoveWithoutUndoEntry();
    void wireBackspaceRemovesLastCornerAndEnterFinishes();
    void rightClickCancelsThenRequestsMenu();
    void doubleRightClickDeletesOnlyTarget();
    void propertyEditIsUndoable();
    void wireFinishesOnDoubleClick();
    void rightClickWithSinglePointCancelsWire();
    void lineByDragAndRectangleCircleMeasureByTwoClicks();
    void arcNeedsThreeClicks();
    void shiftAndCtrlClickToggleSelection();
    void arrowKeysNudgeSelection();
    void rotateBeforePlacingSymbol();
    void wheelZoomKeepsPointUnderCursor();
    void middleButtonPans();

    void movingSymbolDragsConnectedWires();
    void draggingWireSegmentKeepsPinConnections();
    void draggingWireCornerMovesJoinedWires();
    void wiresBetweenPinsAreRoutedAtRightAngles();
    void placementGuidesAlignAndPinsJoin();
    void movingUsesGuidesAndShiftLocksAxis();
    void gridLevelChangesSnapStep();
    void wireCornerOnAnotherWireJoinsIt();
    void teeJoinsFollowMovedWires();
    void lengthUnitsFormat();
    void spacingShownWhileMovingAndPlacing();
    void equalSpacingSnapsWhileMovingAndPlacing();
    void createArrayCopiesInRowOrder();
    void textToolPlacesBoardTextWithHeight();
    void pcbRouteStartsOnPadLayerAndFitsPad();
    void pcbRoutePreviewCompletesToAirwireTarget();
    void pcbAssistedRouteAvoidsCopperObstacles();
    void pcbRouteNetLabelTracksClosestAirwire();
    void pcbDoubleClickPlacesViaAndChangesLayer();

private:
    DesignCanvas* canvas_ = nullptr;
};

void DesignCanvasTests::init() {
    canvas_ = new DesignCanvas(Workspace::Schematic);
    canvas_->resize(1000, 700);
    canvas_->setSnapSettings(SnapSettings{});
    canvas_->show();
    QVERIFY(QTest::qWaitForWindowExposed(canvas_));
}

void DesignCanvasTests::cleanup() {
    delete canvas_;
    canvas_ = nullptr;
}

void DesignCanvasTests::placingSymbolIsUndoable() {
    placeResistor(*canvas_, {10.3, 10.0});
    QCOMPARE(canvas_->document().size(), 1);
    QVERIFY(samePoint(canvas_->document().first().points.first(), {10.16, 10.16}));
    QCOMPARE(canvas_->document().first().label, QStringLiteral("R1"));

    canvas_->undoStack()->undo();
    QCOMPARE(canvas_->document().size(), 0);
    canvas_->undoStack()->redo();
    QCOMPARE(canvas_->document().size(), 1);
}

void DesignCanvasTests::designatorsIncrement() {
    placeResistor(*canvas_, {10.16, 10.16});
    click(*canvas_, {30.48, 10.16});
    QCOMPARE(canvas_->document().at(1).label, QStringLiteral("R2"));
    canvas_->undoStack()->undo();
    click(*canvas_, {30.48, 30.48});
    QCOMPARE(canvas_->document().at(1).label, QStringLiteral("R2"));
}

void DesignCanvasTests::undoKeepsActiveTool() {
    placeResistor(*canvas_, {10.16, 10.16});
    canvas_->undoStack()->undo();
    QCOMPARE(canvas_->tool(), CanvasTool::Symbol);
    QCOMPARE(canvas_->toolVariant(), Resistor);
}

void DesignCanvasTests::wireSnapsToGridAndEndsOnPin() {
    placeResistor(*canvas_, {20.32, 20.32});
    canvas_->setTool(CanvasTool::Wire);
    click(*canvas_, {35.7, 30.3});
    QVERIFY(canvas_->hasPendingOperation());
    click(*canvas_, {25.8, 20.0});

    QVERIFY(!canvas_->hasPendingOperation());
    QCOMPARE(canvas_->document().size(), 2);
    const SketchItem& wire = canvas_->document().at(1);
    QCOMPARE(wire.kind, SketchItem::Kind::Wire);
    // The wire is routed at right angles and enters the pin along its axis.
    QVERIFY(samePoints(wire.points, {{35.56, 30.48}, {35.56, 20.32}, {25.4, 20.32}}));
}

void DesignCanvasTests::polylineSupportsBackspaceAndEnter() {
    canvas_->setTool(CanvasTool::Polyline);
    click(*canvas_, {5.08, 5.08});
    click(*canvas_, {15.24, 5.08});
    click(*canvas_, {15.24, 15.24});
    click(*canvas_, {40.64, 50.8});
    QTest::keyClick(canvas_, Qt::Key_Backspace);
    QTest::keyClick(canvas_, Qt::Key_Return);

    QCOMPARE(canvas_->document().size(), 1);
    QCOMPARE(canvas_->document().first().kind, SketchItem::Kind::Polyline);
    QCOMPARE(canvas_->document().first().points.size(), 3);
    QCOMPARE(canvas_->undoStack()->count(), 1);
}

void DesignCanvasTests::rectangleByDragAndLineByTwoClicks() {
    canvas_->setTool(CanvasTool::Rectangle);
    drag(*canvas_, {5.08, 5.08}, {25.4, 15.24});
    QCOMPARE(canvas_->document().size(), 1);
    QCOMPARE(canvas_->document().first().kind, SketchItem::Kind::Rectangle);
    QVERIFY(samePoint(canvas_->document().first().points.at(1), {25.4, 15.24}));

    canvas_->setTool(CanvasTool::Line);
    click(*canvas_, {5.08, 30.48});
    QCOMPARE(canvas_->document().size(), 1);
    click(*canvas_, {25.4, 30.48});
    QCOMPARE(canvas_->document().size(), 2);
    QCOMPARE(canvas_->document().at(1).kind, SketchItem::Kind::Line);
}

void DesignCanvasTests::orthogonalSnapConstrainsLines() {
    SnapSettings settings;
    settings.diagonal = false;
    settings.orthogonal = true;
    canvas_->setSnapSettings(settings);
    canvas_->setTool(CanvasTool::Line);
    click(*canvas_, {5.08, 5.08});
    click(*canvas_, {25.4, 7.62});
    QVERIFY(samePoint(canvas_->document().first().points.at(1), {25.4, 5.08}));
}

void DesignCanvasTests::movingSelectionIsOneUndoStep() {
    placeResistor(*canvas_, {20.32, 20.32});
    canvas_->setTool(CanvasTool::Select);
    drag(*canvas_, {20.32, 20.32}, {30.48, 20.32});

    QVERIFY(samePoint(canvas_->document().first().points.first(), {30.48, 20.32}));
    QCOMPARE(canvas_->undoStack()->count(), 2);
    QCOMPARE(canvas_->selection(), QList<int>{0});
    canvas_->undoStack()->undo();
    QVERIFY(samePoint(canvas_->document().first().points.first(), {20.32, 20.32}));
}

void DesignCanvasTests::clickWithoutMovingDoesNotCreateUndoEntry() {
    placeResistor(*canvas_, {20.32, 20.32});
    canvas_->setTool(CanvasTool::Select);
    click(*canvas_, {20.32, 20.32});
    QCOMPARE(canvas_->selection(), QList<int>{0});
    QCOMPARE(canvas_->undoStack()->count(), 1);
}

void DesignCanvasTests::deleteUndoRestoresSelection() {
    placeResistor(*canvas_, {20.32, 20.32});
    canvas_->setTool(CanvasTool::Select);
    click(*canvas_, {20.32, 20.32});
    canvas_->deleteSelection();
    QCOMPARE(canvas_->document().size(), 0);
    QVERIFY(canvas_->selection().isEmpty());

    canvas_->undoStack()->undo();
    QCOMPARE(canvas_->document().size(), 1);
    QCOMPARE(canvas_->selection(), QList<int>{0});
}

void DesignCanvasTests::rubberBandSelectsItems() {
    placeResistor(*canvas_, {20.32, 20.32});
    click(*canvas_, {20.32, 40.64});
    click(*canvas_, {80.0, 80.0});
    canvas_->setTool(CanvasTool::Select);
    drag(*canvas_, {2.54, 2.54}, {35.56, 45.72});
    QCOMPARE(canvas_->selection(), (QList<int>{0, 1}));
}

void DesignCanvasTests::alignAndDistribute() {
    placeResistor(*canvas_, {10.16, 10.16});
    click(*canvas_, {15.24, 25.4});
    click(*canvas_, {40.64, 40.64});
    canvas_->setTool(CanvasTool::Select);
    canvas_->selectAll();

    canvas_->align(AlignOperation::DistributeHorizontally);
    QVERIFY(samePoint(canvas_->document().at(1).points.first(), {25.4, 25.4}));

    canvas_->align(AlignOperation::Left);
    for (const auto& item : canvas_->document()) {
        QVERIFY(qFuzzyCompare(hatt::ui::itemBounds(item).left() + 1.0,
                              hatt::ui::itemBounds(canvas_->document().first()).left() + 1.0));
    }
    QCOMPARE(canvas_->undoStack()->count(), 5);
    canvas_->undoStack()->undo();
    QVERIFY(samePoint(canvas_->document().at(2).points.first(), {40.64, 40.64}));
}

void DesignCanvasTests::rotateTurnsPins() {
    placeResistor(*canvas_, {20.32, 20.32});
    canvas_->setTool(CanvasTool::Select);
    canvas_->selectAll();
    canvas_->rotateSelection();
    const auto pins = hatt::ui::itemAnchors(canvas_->document().first());
    QCOMPARE(canvas_->document().first().quarterTurns, 1);
    QVERIFY(samePoint(pins.at(0), {20.32, 15.24}));
    QVERIFY(samePoint(pins.at(1), {20.32, 25.4}));
}

void DesignCanvasTests::measureDoesNotChangeDocument() {
    canvas_->setTool(CanvasTool::Measure);
    drag(*canvas_, {5.08, 5.08}, {30.48, 5.08});
    QCOMPARE(canvas_->document().size(), 0);
    QCOMPARE(canvas_->undoStack()->count(), 0);
}

void DesignCanvasTests::escapeCancelsThenRequestsSelection() {
    QSignalSpy requested(canvas_, &DesignCanvas::selectToolRequested);
    canvas_->setTool(CanvasTool::Wire);
    click(*canvas_, {5.08, 5.08});
    QVERIFY(canvas_->hasPendingOperation());
    QTest::keyClick(canvas_, Qt::Key_Escape);
    QVERIFY(!canvas_->hasPendingOperation());
    QCOMPARE(requested.count(), 0);
    QTest::keyClick(canvas_, Qt::Key_Escape);
    QCOMPARE(requested.count(), 1);
}

void DesignCanvasTests::escapeIsLayered() {
    QSignalSpy requested(canvas_, &DesignCanvas::selectToolRequested);

    // Layer 1: a pending operation is cancelled first, the tool stays active.
    canvas_->setTool(CanvasTool::Line);
    click(*canvas_, {5.08, 5.08});
    QVERIFY(canvas_->hasPendingOperation());
    QTest::keyClick(canvas_, Qt::Key_Escape);
    QVERIFY(!canvas_->hasPendingOperation());
    QCOMPARE(canvas_->tool(), CanvasTool::Line);
    QCOMPARE(canvas_->document().size(), 0);
    QCOMPARE(requested.count(), 0);

    // Layer 2: without a pending operation the selection is cleared before the tool changes.
    placeResistor(*canvas_, {20.32, 20.32});
    canvas_->setTool(CanvasTool::Select);
    click(*canvas_, {20.32, 20.32});
    QCOMPARE(canvas_->selection(), QList<int>{0});
    QTest::keyClick(canvas_, Qt::Key_Escape);
    QVERIFY(canvas_->selection().isEmpty());
    QCOMPARE(requested.count(), 0);

    // Selection tool with nothing to cancel: Esc is a no-op.
    QTest::keyClick(canvas_, Qt::Key_Escape);
    QCOMPARE(requested.count(), 0);

    // Layer 3: a finished measurement is dismissed before returning to selection.
    canvas_->setTool(CanvasTool::Measure);
    click(*canvas_, {5.08, 5.08});
    click(*canvas_, {30.48, 5.08});
    QVERIFY(!canvas_->hasPendingOperation());
    QTest::keyClick(canvas_, Qt::Key_Escape);
    QCOMPARE(requested.count(), 0);
    QTest::keyClick(canvas_, Qt::Key_Escape);
    QCOMPARE(requested.count(), 1);
    QCOMPARE(canvas_->document().size(), 1);
}

void DesignCanvasTests::escapeDuringDragCancelsMoveWithoutUndoEntry() {
    placeResistor(*canvas_, {20.32, 20.32});
    canvas_->setTool(CanvasTool::Select);
    sendMouse(*canvas_, QEvent::MouseButtonPress, {20.32, 20.32}, Qt::LeftButton, Qt::LeftButton);
    sendMouse(*canvas_, QEvent::MouseMove, {25.4, 20.32}, Qt::NoButton, Qt::LeftButton);
    sendMouse(*canvas_, QEvent::MouseMove, {30.48, 20.32}, Qt::NoButton, Qt::LeftButton);
    QVERIFY(canvas_->hasPendingOperation());
    QTest::keyClick(canvas_, Qt::Key_Escape);
    QVERIFY(!canvas_->hasPendingOperation());
    sendMouse(*canvas_, QEvent::MouseButtonRelease, {30.48, 20.32}, Qt::LeftButton, Qt::NoButton);

    QVERIFY(samePoint(canvas_->document().first().points.first(), {20.32, 20.32}));
    QCOMPARE(canvas_->undoStack()->count(), 1);
    // The selection survives the cancelled drag; the next Esc clears it.
    QCOMPARE(canvas_->selection(), QList<int>{0});
}

void DesignCanvasTests::wireBackspaceRemovesLastCornerAndEnterFinishes() {
    canvas_->setTool(CanvasTool::Wire);
    click(*canvas_, {5.08, 5.08});
    click(*canvas_, {15.24, 5.08});
    click(*canvas_, {15.24, 15.24});
    QTest::keyClick(canvas_, Qt::Key_Backspace);
    QVERIFY(canvas_->hasPendingOperation());
    QTest::keyClick(canvas_, Qt::Key_Return);

    QVERIFY(!canvas_->hasPendingOperation());
    QCOMPARE(canvas_->document().size(), 1);
    const SketchItem& wire = canvas_->document().first();
    QCOMPARE(wire.kind, SketchItem::Kind::Wire);
    QCOMPARE(wire.points.size(), 2);
    QVERIFY(samePoint(wire.points.last(), {15.24, 5.08}));
    QCOMPARE(canvas_->undoStack()->count(), 1);
    QCOMPARE(canvas_->tool(), CanvasTool::Wire);

    // Keypad Enter also finishes.
    click(*canvas_, {5.08, 25.4});
    click(*canvas_, {25.4, 25.4});
    QTest::keyClick(canvas_, Qt::Key_Enter);
    QCOMPARE(canvas_->document().size(), 2);
}

void DesignCanvasTests::rightClickCancelsThenRequestsMenu() {
    QSignalSpy menus(canvas_, &DesignCanvas::contextMenuRequested);
    QSignalSpy selectionTool(canvas_, &DesignCanvas::selectToolRequested);
    placeResistor(*canvas_, {50.8, 50.8});
    canvas_->setTool(CanvasTool::Wire);
    click(*canvas_, {5.08, 5.08});
    click(*canvas_, {15.24, 5.08});
    click(*canvas_, {15.24, 15.24});
    sendMouse(*canvas_, QEvent::MouseButtonPress, {30.48, 30.48}, Qt::RightButton, Qt::RightButton);
    sendMouse(*canvas_, QEvent::MouseButtonRelease, {30.48, 30.48}, Qt::RightButton, Qt::NoButton);

    QVERIFY(!canvas_->hasPendingOperation());
    QCOMPARE(canvas_->document().size(), 1);
    QCOMPARE(canvas_->document().first().kind, SketchItem::Kind::Symbol);
    QCOMPARE(canvas_->undoStack()->count(), 1);
    QCOMPARE(canvas_->tool(), CanvasTool::Select);
    QCOMPARE(selectionTool.count(), 1);
    // The OS may classify the next click as a double click with the cancellation click.
    sendMouse(*canvas_, QEvent::MouseButtonDblClick, {50.8, 50.8}, Qt::RightButton, Qt::RightButton);
    sendMouse(*canvas_, QEvent::MouseButtonRelease, {50.8, 50.8}, Qt::RightButton, Qt::NoButton);
    QCOMPARE(canvas_->document().size(), 1);
    QCOMPARE(menus.count(), 0);
    QTRY_COMPARE(menus.count(), 1);
    QCOMPARE(menus.first().at(1).toInt(), 0);
}

void DesignCanvasTests::doubleRightClickDeletesOnlyTarget() {
    QSignalSpy menus(canvas_, &DesignCanvas::contextMenuRequested);
    placeResistor(*canvas_, {20.32, 20.32});
    click(*canvas_, {50.8, 50.8});
    canvas_->setTool(CanvasTool::Select);
    canvas_->selectAll();
    auto twice = [&](QPointF at) {
        sendMouse(*canvas_, QEvent::MouseButtonPress, at, Qt::RightButton, Qt::RightButton);
        sendMouse(*canvas_, QEvent::MouseButtonRelease, at, Qt::RightButton, Qt::NoButton);
        sendMouse(*canvas_, QEvent::MouseButtonDblClick, at, Qt::RightButton, Qt::RightButton);
        sendMouse(*canvas_, QEvent::MouseButtonRelease, at, Qt::RightButton, Qt::NoButton);
    };
    twice({20.32, 20.32});
    QCOMPARE(canvas_->document().size(), 1);
    QCOMPARE(canvas_->undoStack()->count(), 3);
    canvas_->undoStack()->undo();
    QCOMPARE(canvas_->document().size(), 2);
    canvas_->undoStack()->redo();
    QCOMPARE(canvas_->document().size(), 1);
    twice({90, 90});
    QCOMPARE(canvas_->document().size(), 1);
    QTest::qWait(QApplication::doubleClickInterval() + 30);
    QCOMPARE(menus.count(), 0);
    QContextMenuEvent mouseContext(QContextMenuEvent::Mouse, {20, 20}, canvas_->mapToGlobal(QPoint(20, 20)));
    QApplication::sendEvent(canvas_, &mouseContext);
    QCOMPARE(menus.count(), 0);
    QContextMenuEvent keyboardContext(QContextMenuEvent::Keyboard, {20, 20}, canvas_->mapToGlobal(QPoint(20, 20)));
    QApplication::sendEvent(canvas_, &keyboardContext);
    QCOMPARE(menus.count(), 1);
}

void DesignCanvasTests::propertyEditIsUndoable() {
    placeResistor(*canvas_, {20.32, 20.32});
    const auto original = canvas_->document().first();
    canvas_->editItemProperties(0, QStringLiteral("R42"), {30, 40}, 1);
    QCOMPARE(canvas_->document().first().label, QStringLiteral("R42"));
    QCOMPARE(canvas_->document().first().points.first(), QPointF(30, 40));
    QCOMPARE(canvas_->document().first().quarterTurns, 1);
    QCOMPARE(canvas_->undoStack()->count(), 2);
    canvas_->undoStack()->undo();
    QCOMPARE(canvas_->document().first().label, original.label);
    QCOMPARE(canvas_->document().first().points, original.points);
    canvas_->undoStack()->redo();
    QCOMPARE(canvas_->document().first().label, QStringLiteral("R42"));
}

void DesignCanvasTests::wireFinishesOnDoubleClick() {
    canvas_->setTool(CanvasTool::Wire);
    click(*canvas_, {5.08, 5.08});
    // A real double-click delivers press, release, double-click, release.
    const QPointF end(15.24, 5.08);
    click(*canvas_, end);
    sendMouse(*canvas_, QEvent::MouseButtonDblClick, end, Qt::LeftButton, Qt::LeftButton);
    sendMouse(*canvas_, QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);

    QVERIFY(!canvas_->hasPendingOperation());
    QCOMPARE(canvas_->document().size(), 1);
    QCOMPARE(canvas_->document().first().points.size(), 2);
    QCOMPARE(canvas_->undoStack()->count(), 1);
}

void DesignCanvasTests::rightClickWithSinglePointCancelsWire() {
    canvas_->setTool(CanvasTool::Wire);
    click(*canvas_, {5.08, 5.08});
    sendMouse(*canvas_, QEvent::MouseButtonPress, {5.08, 5.08}, Qt::RightButton, Qt::RightButton);
    QVERIFY(!canvas_->hasPendingOperation());
    QCOMPARE(canvas_->document().size(), 0);
    QCOMPARE(canvas_->undoStack()->count(), 0);
}

void DesignCanvasTests::lineByDragAndRectangleCircleMeasureByTwoClicks() {
    canvas_->setTool(CanvasTool::Line);
    drag(*canvas_, {5.08, 5.08}, {25.4, 5.08});
    QCOMPARE(canvas_->document().size(), 1);
    QCOMPARE(canvas_->document().at(0).kind, SketchItem::Kind::Line);

    canvas_->setTool(CanvasTool::Rectangle);
    click(*canvas_, {5.08, 20.32});
    QVERIFY(canvas_->hasPendingOperation());
    click(*canvas_, {25.4, 30.48});
    QCOMPARE(canvas_->document().size(), 2);
    QCOMPARE(canvas_->document().at(1).kind, SketchItem::Kind::Rectangle);
    QVERIFY(samePoint(canvas_->document().at(1).points.at(1), {25.4, 30.48}));

    canvas_->setTool(CanvasTool::Circle);
    click(*canvas_, {50.8, 50.8});
    click(*canvas_, {55.88, 50.8});
    QCOMPARE(canvas_->document().size(), 3);
    QCOMPARE(canvas_->document().at(2).kind, SketchItem::Kind::Circle);

    canvas_->setTool(CanvasTool::Circle);
    drag(*canvas_, {76.2, 50.8}, {81.28, 50.8});
    QCOMPARE(canvas_->document().size(), 4);

    canvas_->setTool(CanvasTool::Measure);
    click(*canvas_, {5.08, 60.96});
    click(*canvas_, {30.48, 60.96});
    QCOMPARE(canvas_->document().size(), 4);
    QCOMPARE(canvas_->undoStack()->count(), 4);
}

void DesignCanvasTests::arcNeedsThreeClicks() {
    canvas_->setTool(CanvasTool::Arc);
    click(*canvas_, {5.08, 5.08});
    click(*canvas_, {25.4, 5.08});
    QCOMPARE(canvas_->document().size(), 0);
    QVERIFY(canvas_->hasPendingOperation());
    click(*canvas_, {15.24, 12.7});
    QVERIFY(!canvas_->hasPendingOperation());
    QCOMPARE(canvas_->document().size(), 1);
    const SketchItem& arc = canvas_->document().first();
    QCOMPARE(arc.kind, SketchItem::Kind::Arc);
    QVERIFY(samePoint(arc.points.at(0), {5.08, 5.08}));
    QVERIFY(samePoint(arc.points.at(2), {25.4, 5.08}));
    QCOMPARE(canvas_->undoStack()->count(), 1);
}

void DesignCanvasTests::shiftAndCtrlClickToggleSelection() {
    placeResistor(*canvas_, {20.32, 20.32});
    click(*canvas_, {50.8, 20.32});
    canvas_->setTool(CanvasTool::Select);

    click(*canvas_, {20.32, 20.32});
    QCOMPARE(canvas_->selection(), QList<int>{0});
    click(*canvas_, {50.8, 20.32}, Qt::ShiftModifier);
    QCOMPARE(canvas_->selection(), (QList<int>{0, 1}));
    click(*canvas_, {20.32, 20.32}, Qt::ControlModifier);
    QCOMPARE(canvas_->selection(), QList<int>{1});
    // Plain click on empty space clears; Shift-click on empty space keeps.
    click(*canvas_, {20.32, 20.32}, Qt::ShiftModifier);
    click(*canvas_, {80.0, 80.0}, Qt::ShiftModifier);
    QCOMPARE(canvas_->selection(), (QList<int>{0, 1}));
    click(*canvas_, {80.0, 80.0});
    QVERIFY(canvas_->selection().isEmpty());
    QCOMPARE(canvas_->undoStack()->count(), 2);
}

void DesignCanvasTests::arrowKeysNudgeSelection() {
    placeResistor(*canvas_, {20.32, 20.32});
    canvas_->setTool(CanvasTool::Select);
    click(*canvas_, {20.32, 20.32});

    QTest::keyClick(canvas_, Qt::Key_Right);
    QVERIFY(samePoint(canvas_->document().first().points.first(), {22.86, 20.32}));
    QTest::keyClick(canvas_, Qt::Key_Down, Qt::ShiftModifier);
    QVERIFY(samePoint(canvas_->document().first().points.first(), {22.86, 33.02}));
    QCOMPARE(canvas_->undoStack()->count(), 3);
    canvas_->undoStack()->undo();
    QVERIFY(samePoint(canvas_->document().first().points.first(), {22.86, 20.32}));
}

void DesignCanvasTests::rotateBeforePlacingSymbol() {
    canvas_->setTool(CanvasTool::Symbol, Resistor);
    canvas_->rotateSelection();
    click(*canvas_, {20.32, 20.32});
    QCOMPARE(canvas_->document().size(), 1);
    QCOMPARE(canvas_->document().first().quarterTurns, 1);
    QCOMPARE(canvas_->undoStack()->count(), 1);
}

void DesignCanvasTests::wheelZoomKeepsPointUnderCursor() {
    QSignalSpy zoom(canvas_, &DesignCanvas::zoomChanged);
    const QPointF world(40.64, 30.48);
    const QPointF screen = canvas_->worldToScreen(world);
    const int before = canvas_->zoomPercent();

    QWheelEvent in(screen, canvas_->mapToGlobal(screen), QPoint(), QPoint(0, 120), Qt::NoButton,
                   Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas_, &in);
    QVERIFY(canvas_->zoomPercent() > before);
    QVERIFY(QLineF(canvas_->worldToScreen(world), screen).length() < 1e-6);

    QWheelEvent out(screen, canvas_->mapToGlobal(screen), QPoint(), QPoint(0, -240), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(canvas_, &out);
    QVERIFY(canvas_->zoomPercent() < before);
    QVERIFY(QLineF(canvas_->worldToScreen(world), screen).length() < 1e-6);
    QCOMPARE(zoom.count(), 2);

    placeResistor(*canvas_, {200.0, 200.0});
    canvas_->zoomToFit();
    const QPointF fitted = canvas_->worldToScreen({200.0, 200.0});
    QVERIFY(QRectF(canvas_->rect()).contains(fitted));
}

void DesignCanvasTests::middleButtonPans() {
    canvas_->setTool(CanvasTool::Wire);
    const QPointF world(20.32, 20.32);
    const QPointF before = canvas_->worldToScreen(world);
    const QPointF start(300, 300);
    const QPointF end(350, 260);
    auto send = [&](QEvent::Type type, QPointF position, Qt::MouseButton button,
                    Qt::MouseButtons buttons) {
        QMouseEvent event(type, position, canvas_->mapToGlobal(position), button, buttons,
                          Qt::NoModifier);
        QApplication::sendEvent(canvas_, &event);
    };
    send(QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
    send(QEvent::MouseMove, end, Qt::NoButton, Qt::MiddleButton);
    send(QEvent::MouseButtonRelease, end, Qt::MiddleButton, Qt::NoButton);

    QVERIFY(QLineF(canvas_->worldToScreen(world), before + (end - start)).length() < 1e-6);
    QCOMPARE(canvas_->document().size(), 0);
    QVERIFY(!canvas_->hasPendingOperation());
}

void DesignCanvasTests::wireCornerOnAnotherWireJoinsIt() {
    canvas_->applyDocumentEdit(QStringLiteral("Setup"),
                               {wireThrough({{10.16, 20.32}, {50.8, 20.32}})});
    canvas_->setTool(CanvasTool::Wire);

    // Clicking on the existing wire and continuing joins it: the wire is split there.
    click(*canvas_, {30.48, 10.16});
    click(*canvas_, {30.48, 20.32});
    click(*canvas_, {30.48, 30.48});
    QTest::keyClick(canvas_, Qt::Key_Return);
    QCOMPARE(canvas_->document().size(), 3);
    QVERIFY(samePoints(canvas_->document().at(1).points, {{30.48, 10.16}, {30.48, 20.32}}));
    QVERIFY(samePoints(canvas_->document().at(2).points, {{30.48, 20.32}, {30.48, 30.48}}));

    // Passing straight over it without a corner stays a crossing.
    click(*canvas_, {40.64, 10.16});
    click(*canvas_, {40.64, 30.48});
    QTest::keyClick(canvas_, Qt::Key_Return);
    QCOMPARE(canvas_->document().size(), 4);

    const auto junctions = hatt::ui::schematicJunctions(canvas_->document());
    QCOMPARE(junctions.size(), 1);
    QVERIFY(samePoint(junctions.first(), {30.48, 20.32}));

    canvas_->undoStack()->undo();
    canvas_->undoStack()->undo();
    QCOMPARE(canvas_->document().size(), 1);
}

void DesignCanvasTests::teeJoinsFollowMovedWires() {
    // A (index 1) ends on the middle of B (index 0): a T join with a junction dot.
    const hatt::ui::SketchDocument tee{wireThrough({{10.16, 20.32}, {50.8, 20.32}}),
                                       wireThrough({{30.48, 20.32}, {30.48, 30.48}})};
    const auto joinedOnce = [](const hatt::ui::SketchDocument& document) {
        const auto dots = hatt::ui::schematicJunctions(document);
        return dots.size() == 1 && samePoint(dots.first(), document.at(1).points.first());
    };
    QVERIFY(joinedOnce(tee));

    // Dragging B's segment carries A's end along, A only gets shorter.
    auto document = hatt::ui::dragWireSegment(tee, 0, 0, {0.0, -5.08}, 2.54);
    QVERIFY(samePoints(document.at(0).points, {{10.16, 15.24}, {50.8, 15.24}}));
    QVERIFY(samePoints(document.at(1).points, {{30.48, 15.24}, {30.48, 30.48}}));
    QVERIFY(joinedOnce(document));

    // Moving B as a whole moves A's end with it; A's far end stays.
    document = hatt::ui::moveItemsKeepingConnections(tee, {0}, {2.54, -5.08}, 2.54);
    QVERIFY(samePoint(document.at(1).points.first(), {33.02, 15.24}));
    QVERIFY(samePoint(document.at(1).points.last(), {30.48, 30.48}));
    QVERIFY(joinedOnce(document));

    // Reshaping B at a corner keeps A's end on the new segment.
    hatt::ui::SketchDocument bent = tee;
    bent[0].points.append({50.8, 40.64});
    document = hatt::ui::dragWireVertex(bent, 0, 1, {0.0, -5.08}, 2.54);
    QVERIFY(samePoint(document.at(0).points.at(1), {50.8, 15.24}));
    QVERIFY(hatt::ui::distanceToSegment(document.at(1).points.first(),
                                        QLineF(document.at(0).points.at(0),
                                               document.at(0).points.at(1))) < 1e-6);
    QVERIFY(samePoint(document.at(1).points.last(), {30.48, 30.48}));
    QVERIFY(joinedOnce(document));

    // Through the canvas: a lone segment drag is one undo step and keeps the join.
    canvas_->applyDocumentEdit(QStringLiteral("Setup"), tee);
    canvas_->setTool(CanvasTool::Select);
    drag(*canvas_, {15.24, 20.32}, {15.24, 30.48});
    QVERIFY(joinedOnce(canvas_->document()));
    QVERIFY(samePoint(canvas_->document().at(1).points.first(),
                      {30.48, canvas_->document().at(0).points.first().y()}));
}

void DesignCanvasTests::lengthUnitsFormat() {
    using hatt::ui::LengthUnit;
    QCOMPARE(hatt::ui::formatLength(2.54, LengthUnit::Mil), QStringLiteral("100 mil"));
    QCOMPARE(hatt::ui::formatLength(2.54, LengthUnit::Inch), QStringLiteral("0.1 in"));
    QCOMPARE(hatt::ui::formatLength(0.127, LengthUnit::Inch), QStringLiteral("0.005 in"));
    QCOMPARE(hatt::ui::formatLength(0.635, LengthUnit::Millimetre), QStringLiteral("0.635 mm"));
    QCOMPARE(hatt::ui::formatLength(25.4, LengthUnit::Millimetre), QStringLiteral("25.4 mm"));
    QCOMPARE(hatt::ui::formatCoordinate(-0.0001, LengthUnit::Millimetre), QStringLiteral("0.000"));
    QCOMPARE(hatt::ui::formatCoordinate(12.7, LengthUnit::Mil), QStringLiteral("500.0"));
    QCOMPARE(hatt::ui::fromDisplayUnit(hatt::ui::toDisplayUnit(20.32, LengthUnit::Inch),
                                       LengthUnit::Inch), 20.32);
    QCOMPARE(hatt::ui::displayUnit(Workspace::Schematic, LengthUnit::Inch), LengthUnit::Mil);
    QCOMPARE(hatt::ui::displayUnit(Workspace::Board, LengthUnit::Inch), LengthUnit::Inch);
    QCOMPARE(canvas_->lengthUnit(), LengthUnit::Mil);
}

void DesignCanvasTests::spacingShownWhileMovingAndPlacing() {
    canvas_->applyDocumentEdit(QStringLiteral("Setup"),
                               {resistorAt({20.32, 20.32}), resistorAt({60.96, 20.32})});
    const QRectF left = hatt::ui::itemBounds(canvas_->document().at(0));
    canvas_->setTool(CanvasTool::Select);
    QVERIFY(canvas_->activeSpacings().isEmpty());

    // While R2 is dragged towards R1, the horizontal gap between their boxes is measured.
    sendMouse(*canvas_, QEvent::MouseButtonPress, {60.96, 20.32}, Qt::LeftButton, Qt::LeftButton);
    for (int step = 1; step <= 4; ++step) {
        sendMouse(*canvas_, QEvent::MouseMove, {60.96 - 2.54 * step, 20.32}, Qt::NoButton,
                  Qt::LeftButton);
    }
    const auto spacings = canvas_->activeSpacings();
    QCOMPARE(spacings.size(), 1);
    const auto& gap = spacings.first();
    QVERIFY(std::abs(gap.line.dy()) < 1e-6);
    QVERIFY(std::abs(gap.line.x1() - left.right()) < 1e-6);
    QVERIFY(std::abs(gap.distance - gap.line.length()) < 1e-6);
    QVERIFY(gap.distance > 0.0 && gap.distance < 60.96 - 20.32);
    sendMouse(*canvas_, QEvent::MouseButtonRelease, {50.8, 20.32}, Qt::LeftButton, Qt::NoButton);
    QVERIFY(canvas_->activeSpacings().isEmpty());

    // Placing a symbol below R1 shows the vertical gap to it.
    canvas_->setTool(CanvasTool::Symbol, Resistor);
    sendMouse(*canvas_, QEvent::MouseMove, {20.32, 40.64}, Qt::NoButton, Qt::NoButton);
    const auto placing = canvas_->activeSpacings();
    const bool below = std::any_of(placing.begin(), placing.end(), [&](const auto& s) {
        return std::abs(s.line.dx()) < 1e-6 && std::abs(s.line.y1() - left.bottom()) < 1e-6;
    });
    QVERIFY(below);
}

void DesignCanvasTests::equalSpacingSnapsWhileMovingAndPlacing() {
    // Free movement so only the equal-spacing snap can land the symbol exactly.
    SnapSettings settings;
    settings.grid = false;
    settings.objects = false;
    canvas_->setSnapSettings(settings);
    // R1–R2 centres are 20.32 apart, so R3 is equally spaced when its centre is at 60.96.
    canvas_->applyDocumentEdit(QStringLiteral("Setup"),
                               {resistorAt({20.32, 20.32}), resistorAt({40.64, 20.32}),
                                resistorAt({71.12, 20.32})});
    const double pixel = 1.0 / (canvas_->worldToScreen({1, 0}).x() - canvas_->worldToScreen({0, 0}).x());
    const QPointF nearEqual(60.96 + 4 * pixel, 20.32);

    canvas_->setTool(CanvasTool::Select);
    sendMouse(*canvas_, QEvent::MouseButtonPress, {71.12, 20.32}, Qt::LeftButton, Qt::LeftButton);
    for (int step = 1; step <= 4; ++step) {
        sendMouse(*canvas_, QEvent::MouseMove, QPointF(71.12, 20.32) + (nearEqual - QPointF(71.12, 20.32)) * step / 4.0,
                  Qt::NoButton, Qt::LeftButton);
    }
    const auto spacings = canvas_->activeSpacings();
    const auto equal = std::count_if(spacings.begin(), spacings.end(), [](const auto& s) { return s.equal; });
    QCOMPARE(equal, 2); // R2–R3 while moving, and the R1–R2 gap it matches
    sendMouse(*canvas_, QEvent::MouseButtonRelease, nearEqual, Qt::LeftButton, Qt::NoButton);
    QVERIFY(samePoint(canvas_->document().at(2).points.first(), {60.96, 20.32}));

    // Placing a new symbol after R3 snaps to the same pitch.
    canvas_->setTool(CanvasTool::Symbol, Resistor);
    click(*canvas_, {81.28 - 3 * pixel, 20.32});
    QCOMPARE(canvas_->document().size(), 4);
    QVERIFY(samePoint(canvas_->document().at(3).points.first(), {81.28, 20.32}));

    // Far from any matching gap nothing is snapped.
    click(*canvas_, {110.0, 20.32});
    QVERIFY(samePoint(canvas_->document().at(4).points.first(), {110.0, 20.32}));
}

void DesignCanvasTests::createArrayCopiesInRowOrder() {
    placeResistor(*canvas_, {20.32, 20.32});
    canvas_->setTool(CanvasTool::Select);
    canvas_->selectItem(0);
    canvas_->createArray(1, 1, {12.7, 7.62});
    QCOMPARE(canvas_->undoStack()->count(), 1);

    canvas_->createArray(2, 3, {12.7, 7.62});
    const auto& document = canvas_->document();
    QCOMPARE(document.size(), 6);
    const QPointF expected[] = {{20.32, 20.32}, {33.02, 20.32}, {45.72, 20.32},
                                {20.32, 27.94}, {33.02, 27.94}, {45.72, 27.94}};
    QSet<QString> ids;
    for (int i = 0; i < 6; ++i) {
        QVERIFY(samePoint(document.at(i).points.first(), expected[i]));
        QCOMPARE(document.at(i).label, QStringLiteral("R%1").arg(i + 1));
        ids.insert(document.at(i).id);
    }
    QCOMPARE(ids.size(), 6);
    QCOMPARE(canvas_->selection().size(), 6);
    QCOMPARE(canvas_->undoStack()->count(), 2);
    canvas_->undoStack()->undo();
    QCOMPARE(canvas_->document().size(), 1);
}

void DesignCanvasTests::movingSymbolDragsConnectedWires() {
    // R1 pins are at (15.24, 20.32) and (25.4, 20.32).
    canvas_->applyDocumentEdit(QStringLiteral("Setup"),
                               {resistorAt({20.32, 20.32}),
                                wireThrough({{25.4, 20.32}, {40.64, 20.32}}),
                                wireThrough({{15.24, 20.32}, {10.16, 20.32}, {10.16, 30.48}}),
                                wireThrough({{60.96, 60.96}, {71.12, 60.96}})});
    canvas_->setTool(CanvasTool::Select);
    drag(*canvas_, {20.32, 20.32}, {20.32, 25.4});

    const auto& document = canvas_->document();
    QVERIFY(samePoint(document.at(0).points.first(), {20.32, 25.4}));
    // A straight wire gets an orthogonal dogleg on the grid; its far end stays put.
    QVERIFY(samePoints(document.at(1).points,
                       {{25.4, 25.4}, {33.02, 25.4}, {33.02, 20.32}, {40.64, 20.32}}));
    // An L-shaped wire keeps its shape by sliding its free corner.
    QVERIFY(samePoints(document.at(2).points, {{15.24, 25.4}, {10.16, 25.4}, {10.16, 30.48}}));
    QVERIFY(samePoints(document.at(3).points, {{60.96, 60.96}, {71.12, 60.96}}));
    QCOMPARE(canvas_->undoStack()->count(), 2);

    canvas_->undoStack()->undo();
    QVERIFY(samePoints(canvas_->document().at(1).points, {{25.4, 20.32}, {40.64, 20.32}}));

    // Keyboard nudges keep connections too; moving along a straight wire just stretches it.
    click(*canvas_, {20.32, 20.32});
    QTest::keyClick(canvas_, Qt::Key_Right);
    QVERIFY(samePoints(canvas_->document().at(1).points, {{27.94, 20.32}, {40.64, 20.32}}));
    QVERIFY(samePoints(canvas_->document().at(2).points, {{17.78, 20.32}, {10.16, 20.32}, {10.16, 30.48}}));
}

void DesignCanvasTests::draggingWireSegmentKeepsPinConnections() {
    canvas_->applyDocumentEdit(QStringLiteral("Setup"),
                               {resistorAt({20.32, 20.32}), resistorAt({50.8, 20.32}),
                                wireThrough({{25.4, 20.32}, {45.72, 20.32}})});
    canvas_->setTool(CanvasTool::Select);
    // The horizontal segment only moves vertically; its pin ends get stubs.
    drag(*canvas_, {35.56, 20.32}, {38.1, 30.48});

    QCOMPARE(canvas_->selection(), QList<int>{2});
    QVERIFY(samePoints(canvas_->document().at(2).points,
                       {{25.4, 20.32}, {25.4, 30.48}, {45.72, 30.48}, {45.72, 20.32}}));
    QVERIFY(samePoint(canvas_->document().at(0).points.first(), {20.32, 20.32}));
    QVERIFY(samePoint(canvas_->document().at(1).points.first(), {50.8, 20.32}));
    QCOMPARE(canvas_->undoStack()->count(), 2);

    // Dragging it back collapses the stubs into the original straight wire.
    drag(*canvas_, {35.56, 30.48}, {35.56, 20.32});
    QVERIFY(samePoints(canvas_->document().at(2).points, {{25.4, 20.32}, {45.72, 20.32}}));
    QCOMPARE(canvas_->undoStack()->count(), 3);
    canvas_->undoStack()->undo();
    QCOMPARE(canvas_->document().at(2).points.size(), 4);
}

void DesignCanvasTests::draggingWireCornerMovesJoinedWires() {
    canvas_->applyDocumentEdit(QStringLiteral("Setup"),
                               {wireThrough({{5.08, 5.08}, {15.24, 5.08}, {15.24, 15.24}}),
                                wireThrough({{15.24, 5.08}, {25.4, 5.08}})});
    canvas_->setTool(CanvasTool::Select);
    drag(*canvas_, {15.24, 5.08}, {17.78, 10.16});

    QVERIFY(samePoints(canvas_->document().at(1).points, {{17.78, 10.16}, {25.4, 5.08}}));
    QVERIFY(samePoints(canvas_->document().at(0).points,
                       {{5.08, 5.08}, {17.78, 10.16}, {15.24, 15.24}}));
    QCOMPARE(canvas_->undoStack()->count(), 2);
}

void DesignCanvasTests::wiresBetweenPinsAreRoutedAtRightAngles() {
    // R1 right pin (25.4, 20.32) points right; R2 left pin (45.72, 30.48) points left.
    canvas_->applyDocumentEdit(QStringLiteral("Setup"),
                               {resistorAt({20.32, 20.32}), resistorAt({50.8, 30.48})});
    canvas_->setTool(CanvasTool::Wire);
    click(*canvas_, {25.4, 20.32});
    click(*canvas_, {45.72, 30.48});
    QVERIFY(!canvas_->hasPendingOperation());
    QCOMPARE(canvas_->document().size(), 3);
    QVERIFY(samePoints(canvas_->document().at(2).points,
                       {{25.4, 20.32}, {35.56, 20.32}, {35.56, 30.48}, {45.72, 30.48}}));

    // Ctrl draws the segment at a free angle, as in Proteus.
    click(*canvas_, {25.4, 20.32}, Qt::ControlModifier);
    click(*canvas_, {45.72, 30.48}, Qt::ControlModifier);
    QCOMPARE(canvas_->document().size(), 4);
    QVERIFY(samePoints(canvas_->document().at(3).points, {{25.4, 20.32}, {45.72, 30.48}}));
}

void DesignCanvasTests::pcbRouteStartsOnPadLayerAndFitsPad() {
    DesignCanvas board(Workspace::Board);
    board.resize(800, 600);
    board.setSnapSettings(SnapSettings{});
    board.show();
    QVERIFY(QTest::qWaitForWindowExposed(&board));
    board.applyDocumentEdit(QStringLiteral("Setup"),
                            {smdPadAt({10.16, 10.16}, BoardLayer::BottomCopper, 0.5, 1.5)});
    board.setActiveLayer(BoardLayer::TopCopper);
    board.setTrackWidth(1.0);
    board.setTool(CanvasTool::Wire);

    click(board, {10.16, 10.16});
    QCOMPARE(board.activeLayer(), BoardLayer::BottomCopper);
    click(board, {20.32, 10.16});
    QTest::keyClick(&board, Qt::Key_Return);

    QCOMPARE(board.document().size(), 2);
    const SketchItem& track = board.document().last();
    QCOMPARE(track.kind, SketchItem::Kind::Wire);
    QCOMPARE(track.layer, BoardLayer::BottomCopper);
    QVERIFY(std::abs(track.width - 0.3) < 1e-9);
}

void DesignCanvasTests::pcbRoutePreviewCompletesToAirwireTarget() {
    DesignCanvas board(Workspace::Board);
    board.resize(800, 600);
    board.setSnapSettings(SnapSettings{});
    board.show();
    QVERIFY(QTest::qWaitForWindowExposed(&board));
    const QPointF from(10.16, 10.16);
    const QPointF to(40.64, 30.48);
    board.applyDocumentEdit(QStringLiteral("Setup"),
                            {smdPadAt(from, BoardLayer::BottomCopper),
                             smdPadAt(to, BoardLayer::BottomCopper)});
    board.setAirwires({Airwire{QLineF(from, to), QStringLiteral("NET1")}});
    board.setTool(CanvasTool::Wire);

    click(board, from);
    sendMouse(board, QEvent::MouseMove, {25.4, 15.24}, Qt::NoButton, Qt::NoButton);
    const QVector<QPointF> preview = board.currentRoutePreview();
    QVERIFY(preview.size() >= 3);
    QVERIFY(samePoint(preview.first(), from));
    QVERIFY(samePoint(preview.last(), to));
    // The route being drawn continues to the airwire's endpoint, so it picks up that net's name.
    QCOMPARE(board.currentRouteNet(), QStringLiteral("NET1"));
}

void DesignCanvasTests::pcbAssistedRouteAvoidsCopperObstacles() {
    DesignCanvas board(Workspace::Board);
    board.resize(900, 600);
    board.setSnapSettings(SnapSettings{});
    board.show();
    QVERIFY(QTest::qWaitForWindowExposed(&board));
    const QPointF from(5.08, 10.16);
    const QPointF to(45.72, 10.16);
    SketchItem barrier = wireThrough({{25.4, 0.0}, {25.4, 20.32}});
    barrier.layer = BoardLayer::BottomCopper;
    barrier.width = 1.0;
    board.applyDocumentEdit(QStringLiteral("Setup"),
                            {smdPadAt(from, BoardLayer::BottomCopper),
                             smdPadAt(to, BoardLayer::BottomCopper),
                             smdPadAt({20.32, 10.16}, BoardLayer::BottomCopper, 3.0, 3.0),
                             barrier});
    board.setAirwires({Airwire{QLineF(from, to), QStringLiteral("NET2")}});
    board.setActiveLayer(BoardLayer::BottomCopper);
    board.setTool(CanvasTool::Wire);

    click(board, from);
    sendMouse(board, QEvent::MouseMove, {12.7, 10.16}, Qt::NoButton, Qt::NoButton);
    const QVector<QPointF> preview = board.currentRoutePreview();
    QVERIFY(preview.size() >= 4);
    QVERIFY(samePoint(preview.last(), to));
    const QLineF copperBarrier({25.4, 0.0}, {25.4, 20.32});
    bool detoured = false;
    for (const QPointF& point : preview) {
        if (point.y() < -0.85 || point.y() > 21.17) detoured = true;
        QVERIFY(hatt::ui::distanceToSegment(point, copperBarrier) >= 0.85 - 1e-6 ||
                samePoint(point, from) || samePoint(point, to));
    }
    QVERIFY(detoured);
}

void DesignCanvasTests::pcbRouteNetLabelTracksClosestAirwire() {
    // Two ratsnest lines leave the same pad towards different nets; the ghost route (and its net
    // name label) should follow whichever one the cursor is aimed at, Proteus style, while the
    // other stays a dim, unrelated ghost (see DesignCanvas::currentRouteNet/assistedRoute).
    DesignCanvas board(Workspace::Board);
    board.resize(900, 600);
    board.setSnapSettings(SnapSettings{});
    board.show();
    QVERIFY(QTest::qWaitForWindowExposed(&board));
    const QPointF from(10.16, 10.16);
    const QPointF targetA(40.64, 10.16);
    const QPointF targetB(10.16, 40.64);
    board.applyDocumentEdit(QStringLiteral("Setup"),
                            {smdPadAt(from, BoardLayer::TopCopper),
                             smdPadAt(targetA, BoardLayer::TopCopper),
                             smdPadAt(targetB, BoardLayer::TopCopper)});
    board.setAirwires({Airwire{QLineF(from, targetA), QStringLiteral("NETA")},
                       Airwire{QLineF(from, targetB), QStringLiteral("NETB")}});
    board.setTool(CanvasTool::Wire);

    QVERIFY(board.currentRouteNet().isEmpty());
    click(board, from);
    sendMouse(board, QEvent::MouseMove, {25.4, 10.16}, Qt::NoButton, Qt::NoButton);
    QCOMPARE(board.currentRouteNet(), QStringLiteral("NETA"));
    sendMouse(board, QEvent::MouseMove, {10.16, 25.4}, Qt::NoButton, Qt::NoButton);
    QCOMPARE(board.currentRouteNet(), QStringLiteral("NETB"));
}

void DesignCanvasTests::pcbDoubleClickPlacesViaAndChangesLayer() {
    DesignCanvas board(Workspace::Board);
    board.resize(800, 600);
    board.setSnapSettings(SnapSettings{});
    board.show();
    QVERIFY(QTest::qWaitForWindowExposed(&board));
    board.setActiveLayer(BoardLayer::BottomCopper);
    board.setTool(CanvasTool::Wire);

    click(board, {5.08, 5.08});
    const QPointF transition(15.24, 5.08);
    click(board, transition);
    sendMouse(board, QEvent::MouseButtonDblClick, transition, Qt::LeftButton, Qt::LeftButton);
    sendMouse(board, QEvent::MouseButtonRelease, transition, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(board.activeLayer(), BoardLayer::TopCopper);
    QVERIFY(board.hasPendingOperation());
    click(board, {25.4, 15.24});
    QTest::keyClick(&board, Qt::Key_Return);

    QCOMPARE(board.document().size(), 3);
    QCOMPARE(board.document()[0].kind, SketchItem::Kind::Wire);
    QCOMPARE(board.document()[0].layer, BoardLayer::BottomCopper);
    QCOMPARE(board.document()[1].kind, SketchItem::Kind::Via);
    QVERIFY(samePoint(board.document()[1].points.first(), transition));
    QCOMPARE(board.document()[2].kind, SketchItem::Kind::Wire);
    QCOMPARE(board.document()[2].layer, BoardLayer::TopCopper);
}

void DesignCanvasTests::placementGuidesAlignAndPinsJoin() {
    SnapSettings settings;
    settings.grid = false;
    canvas_->setSnapSettings(settings);
    canvas_->applyDocumentEdit(QStringLiteral("Setup"), {resistorAt({20.32, 20.32})});
    canvas_->setTool(CanvasTool::Symbol, Resistor);

    // Close to the first resistor's row: the preview is pulled onto it and a guide is shown.
    sendMouse(*canvas_, QEvent::MouseMove, {40.0, 20.6}, Qt::NoButton, Qt::NoButton);
    QVERIFY(!canvas_->activeGuides().isEmpty());
    click(*canvas_, {40.0, 20.6});
    QVERIFY(samePoint(canvas_->document().at(1).points.first(), {40.0, 20.32}));

    // A pin near another pin joins it exactly.
    click(*canvas_, {30.8, 20.9});
    QVERIFY(samePoint(canvas_->document().at(2).points.first(), {30.48, 20.32}));
}

void DesignCanvasTests::movingUsesGuidesAndShiftLocksAxis() {
    SnapSettings settings;
    settings.grid = false;
    canvas_->setSnapSettings(settings);
    canvas_->applyDocumentEdit(QStringLiteral("Setup"),
                               {resistorAt({20.32, 20.32}), resistorAt({40.64, 30.48})});
    canvas_->setTool(CanvasTool::Select);
    drag(*canvas_, {40.64, 30.48}, {40.64, 20.9});
    QVERIFY(samePoint(canvas_->document().at(1).points.first(), {40.64, 20.32}));

    canvas_->setSnapSettings(SnapSettings{});
    const QPointF from(40.64, 20.32);
    const QPointF to(50.8, 24.0);
    sendMouse(*canvas_, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
    sendMouse(*canvas_, QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    sendMouse(*canvas_, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    QVERIFY(samePoint(canvas_->document().at(1).points.first(), {50.8, 20.32}));
}

void DesignCanvasTests::gridLevelChangesSnapStep() {
    SnapSettings settings;
    settings.gridLevel = 1;
    canvas_->setSnapSettings(settings);
    QCOMPARE(canvas_->gridSize(), 1.27);
    placeResistor(*canvas_, {10.9, 10.0});
    QVERIFY(samePoint(canvas_->document().first().points.first(), {11.43, 10.16}));
    QCOMPARE(DesignCanvas::gridStep(Workspace::Board, 2), 0.635);
}

void DesignCanvasTests::textToolPlacesBoardTextWithHeight() {
    DesignCanvas board(Workspace::Board);
    board.resize(800, 600);
    board.show();
    QVERIFY(QTest::qWaitForWindowExposed(&board));
    board.setActiveLayer(hatt::ui::BoardLayer::BottomSilk);
    board.setTool(CanvasTool::Text);

    bool answered = false;
    QTimer::singleShot(50, [&answered] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->objectName(), QStringLiteral("PlaceTextDialog"));
        auto* ok = dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
        QVERIFY(!ok->isEnabled()); // empty text
        dialog->findChild<QLineEdit*>(QStringLiteral("TextContent"))->setText(QStringLiteral("REV B"));
        dialog->findChild<QDoubleSpinBox*>(QStringLiteral("TextHeight"))->setValue(3.5);
        answered = true;
        ok->click();
    });
    click(board, {10.0, 10.0});
    QTRY_VERIFY(answered);
    QTRY_COMPARE(board.document().size(), 1);
    const SketchItem text = board.document().first();
    QCOMPARE(text.kind, SketchItem::Kind::Text);
    QCOMPARE(text.label, QStringLiteral("REV B"));
    QCOMPARE(text.layer, hatt::ui::BoardLayer::BottomSilk);
    QCOMPARE(text.width, 3.5);
    QCOMPARE(hatt::ui::itemBounds(text).height(), 3.5);
    QVERIFY(board.tool() == CanvasTool::Text); // ready for the next label
    board.undoStack()->undo();
    QVERIFY(board.document().isEmpty());
}

QTEST_MAIN(DesignCanvasTests)
#include "DesignCanvasTests.moc"
