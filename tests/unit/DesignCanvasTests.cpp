#include "hatt/ui/DesignCanvas.hpp"

#include <QApplication>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QUndoStack>
#include <QtTest>

using hatt::ui::AlignOperation;
using hatt::ui::CanvasTool;
using hatt::ui::DesignCanvas;
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
    QCOMPARE(wire.points.size(), 2);
    QVERIFY(samePoint(wire.points.at(0), {35.56, 30.48}));
    QVERIFY(samePoint(wire.points.at(1), {25.4, 20.32}));
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

QTEST_MAIN(DesignCanvasTests)
#include "DesignCanvasTests.moc"
