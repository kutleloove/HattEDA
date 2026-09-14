#include "hatt/ui/BoardCopper.hpp"
#include "hatt/ui/GerberExport.hpp"
#include "hatt/ui/ProjectFile.hpp"
#include "hatt/ui/SketchCircuit.hpp"
#include "hatt/ui/ZoneFill.hpp"

#include <QLineF>
#include <QtTest>

#include <algorithm>

using namespace hatt::ui;

namespace {

SketchItem zoneItem(const QString& net, QRectF area, BoardLayer layer = BoardLayer::TopCopper) {
    SketchItem zone;
    zone.kind = SketchItem::Kind::Polyline;
    zone.variant = CopperZoneVariant;
    zone.closed = true;
    zone.layer = layer;
    zone.net = net;
    zone.points = {area.topLeft(), area.topRight(), area.bottomRight(), area.bottomLeft()};
    return zone;
}

SketchItem smdPad(QPointF at, double size) {
    SketchItem pad;
    pad.kind = SketchItem::Kind::Pad;
    pad.points = {at};
    pad.layer = BoardLayer::TopCopper;
    pad.pad = {1, PadShape::Rect, size, size, 0.0, layerBit(BoardLayer::TopCopper)};
    return pad;
}

} // namespace

class ZoneFillTests final : public QObject {
    Q_OBJECT

private slots:
    void otherNetsAreKeptClear();
    void ownNetStaysConnected();
    void zonesWithoutNetAreNotPoured();
    void boardEdgeClearanceShrinksThePour();
    void contoursCarryHoleDepth();
    void gerberWritesPourBeforeCopperWithClearPolarity();
    void schematicNetsDecideWhatThePourJoins();
    void zoneNetRoundTripsThroughProjectFile();
};

void ZoneFillTests::otherNetsAreKeptClear() {
    const SketchDocument board{zoneItem(QStringLiteral("GND"), {0, 0, 20, 20}), smdPad({10, 10}, 2.0)};
    const auto fills = fillZones(board, boardCopperObstacles(board), 0.2, 0.3);
    QCOMPARE(fills.size(), 1);
    const QPainterPath& fill = fills.first().fill;
    QVERIFY(!fill.contains(QPointF(10, 10)));
    QVERIFY(!fill.contains(QPointF(11.15, 10))); // inside pad edge + clearance
    QVERIFY(fill.contains(QPointF(11.4, 10)));
    QVERIFY(fill.contains(QPointF(2, 2)));
}

void ZoneFillTests::ownNetStaysConnected() {
    const SketchDocument board{zoneItem(QStringLiteral("GND"), {0, 0, 20, 20}), smdPad({10, 10}, 2.0)};
    auto obstacles = boardCopperObstacles(board);
    for (auto& obstacle : obstacles) obstacle.net = QStringLiteral("GND");
    const auto fills = fillZones(board, obstacles, 0.2, 0.3);
    QVERIFY(fills.first().fill.contains(QPointF(10, 10)));
    QVERIFY(fills.first().fill.contains(QPointF(11.15, 10)));
}

void ZoneFillTests::zonesWithoutNetAreNotPoured() {
    const SketchDocument board{zoneItem(QString(), {0, 0, 20, 20})};
    QVERIFY(fillZones(board, {}, 0.2, 0.3).isEmpty());
    // A zone on the bottom copper ignores top-only copper.
    const SketchDocument bottom{zoneItem(QStringLiteral("GND"), {0, 0, 20, 20}, BoardLayer::BottomCopper),
                                smdPad({10, 10}, 2.0)};
    QVERIFY(fillZones(bottom, boardCopperObstacles(bottom), 0.2, 0.3).first().fill.contains(QPointF(10, 10)));
}

void ZoneFillTests::boardEdgeClearanceShrinksThePour() {
    SketchItem outline;
    outline.kind = SketchItem::Kind::Polyline;
    outline.variant = BoardOutlineVariant;
    outline.closed = true;
    outline.layer = BoardLayer::BoardEdge;
    outline.points = {{0, 0}, {15, 0}, {15, 15}, {0, 15}};
    const SketchDocument board{outline, zoneItem(QStringLiteral("GND"), {-5, -5, 30, 30})};
    const QPainterPath fill = fillZones(board, {}, 0.2, 0.3).first().fill;
    QVERIFY(fill.contains(QPointF(14.5, 5)));
    QVERIFY(!fill.contains(QPointF(14.85, 5)));
    QVERIFY(!fill.contains(QPointF(20, 5)));
}

void ZoneFillTests::contoursCarryHoleDepth() {
    const SketchDocument board{zoneItem(QStringLiteral("GND"), {0, 0, 20, 20}), smdPad({10, 10}, 2.0)};
    const auto contours = zoneContours(fillZones(board, boardCopperObstacles(board), 0.2, 0.3).first().fill);
    QCOMPARE(contours.size(), 2);
    QCOMPARE(contours[0].depth, 0);
    QCOMPARE(contours[1].depth, 1);
    QVERIFY(contours[1].polygon.boundingRect().width() < 4.0);
}

void ZoneFillTests::gerberWritesPourBeforeCopperWithClearPolarity() {
    const SketchDocument board{zoneItem(QStringLiteral("GND"), {0, 0, 20, 20}), smdPad({10, 10}, 2.0)};
    CamOptions options;
    options.zoneFills = fillZones(board, boardCopperObstacles(board), 0.2, 0.3);
    options.designators = false;
    const CamOutput output = buildCamOutput(board, options);
    QCOMPARE(output.skippedZones, 0);
    const auto& copper = output.layers[static_cast<int>(CamLayerKind::TopCopper)].primitives;
    QCOMPARE(copper.size(), 3);
    QCOMPARE(copper[0].kind, CamPrimitive::Kind::Region);
    QVERIFY(!copper[0].clear);
    QVERIFY(copper[1].clear);
    QCOMPARE(copper[2].kind, CamPrimitive::Kind::Flash);

    const QByteArray gerber = gerberLayer(output.layers[static_cast<int>(CamLayerKind::TopCopper)], QStringLiteral("t"));
    const qsizetype clear = gerber.indexOf("%LPC*%");
    const qsizetype dark = gerber.indexOf("%LPD*%", clear);
    const qsizetype flash = gerber.indexOf("D03*");
    QVERIFY(clear > 0);
    QVERIFY(dark > clear);
    QVERIFY(flash > dark);

    // Without a pour the zone is still left out.
    QCOMPARE(buildCamOutput(board).skippedZones, 1);
}

void ZoneFillTests::schematicNetsDecideWhatThePourJoins() {
    const SketchDocument schematic = dcDividerExample();
    SketchItem outline;
    outline.kind = SketchItem::Kind::Polyline;
    outline.variant = BoardOutlineVariant;
    outline.closed = true;
    outline.layer = BoardLayer::BoardEdge;
    outline.points = {{0, 0}, {80, 0}, {80, 60}, {0, 60}};
    BoardTransfer transfer = transferToBoard(schematic, {outline});
    QVERIFY2(transfer.errors.isEmpty(), qPrintable(transfer.errors.join(QLatin1Char('\n'))));
    SketchDocument board = transfer.document;
    const auto header = std::find_if(board.begin(), board.end(), [](const SketchItem& item) {
        return item.kind == SketchItem::Kind::Symbol && item.label == QLatin1String("V1");
    });
    QVERIFY(header != board.end());
    const QVector<PlacedPad> headerPads = itemPads(*header);
    QCOMPARE(headerPads.size(), 2);

    const BoardCopperModel model = buildBoardCopperModel(schematic, board);
    QVERIFY(model.netsKnown);
    QString plusNet;
    for (const BoardConductor& conductor : model.conductors) {
        if (conductor.kind == ConductorKind::Pad && QLineF(conductor.pad.center, headerPads[0].center).length() < 1e-6) {
            plusNet = model.netNames.value(conductor.net);
        }
    }
    QVERIFY(!plusNet.isEmpty());

    board.append(zoneItem(plusNet, {1, 1, 78, 58}, BoardLayer::BottomCopper));
    const auto fills = pourZones(schematic, board, 0.2, 0.3);
    QCOMPARE(fills.size(), 1);
    QVERIFY(fills.first().fill.contains(headerPads[0].center));  // own net: joined
    QVERIFY(!fills.first().fill.contains(headerPads[1].center)); // other net: cleared
    QVERIFY(fills.first().fill.contains(QPointF(5, 5)));
}

void ZoneFillTests::zoneNetRoundTripsThroughProjectFile() {
    ProjectData project;
    project.name = QStringLiteral("zones");
    project.board = {zoneItem(QStringLiteral("GND"), {0, 0, 5, 5})};
    const ProjectLoad loaded = parseProject(serializeProject(project));
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    QCOMPARE(loaded.project.board.first().net, QStringLiteral("GND"));

    QByteArray broken = serializeProject(project);
    broken.replace("\"net\": \"GND\"", "\"net\": 5");
    QVERIFY(broken.contains("\"net\": 5"));
    QVERIFY(!parseProject(broken).ok());
}

QTEST_GUILESS_MAIN(ZoneFillTests)
#include "ZoneFillTests.moc"
