#include "hatt/ui/DesignCanvas.hpp"

#include <QApplication>
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

QTEST_MAIN(DesignCanvasTests)
#include "DesignCanvasTests.moc"
