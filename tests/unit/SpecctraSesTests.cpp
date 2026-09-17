#include "hatt/ui/SpecctraSes.hpp"

#include <QtTest>

using namespace hatt::ui;

class SpecctraSesTests final : public QObject {
    Q_OBJECT

private slots:
    void importsTracksAndViasFromRealSessionShape();
    void convertsMicrometreCoordinatesAndNamedLayers();
    void snapsRoundedEndpointsToPadCentres();
    void rejectsMalformedOrEmptyRouting();
};

void SpecctraSesTests::importsTracksAndViasFromRealSessionShape() {
    const QByteArray ses = R"SES(
(session "board"
  (base_design "board")
  (routes
    (resolution mil 1000)
    (library_out
      (padstack via0
        (shape (circle "1" 3024 0 0))
        (shape (circle "2" 3024 0 0))))
    (network_out
      (net VCC
        (via via0 45000 -140000 (type protect))
        (wire
          (path "1" 1772
            45000 -140000
            50000 -135000
            60000 -135000)
          (type protect))
        (wire
          (path "2" 1772
            45000 -140000
            40000 -145000)
          (type protect))))))
)SES";

    const SpecctraSesResult result = importSpecctraSes(ses);
    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(result.routing.size(), 3);

    const SketchItem& via = result.routing[0];
    QCOMPARE(via.kind, SketchItem::Kind::Via);
    QCOMPARE(via.net, QStringLiteral("VCC"));
    QVERIFY(std::abs(via.points.first().x() - 1.143) < 1e-9);
    QVERIFY(std::abs(via.points.first().y() - 3.556) < 1e-9);
    QVERIFY(std::abs(via.width - 0.0768096) < 1e-9);

    QCOMPARE(result.routing[1].kind, SketchItem::Kind::Wire);
    QCOMPARE(result.routing[1].layer, BoardLayer::TopCopper);
    QCOMPARE(result.routing[1].points.size(), 3);
    QVERIFY(std::abs(result.routing[1].width - 0.0450088) < 1e-9);
    QCOMPARE(result.routing[2].layer, BoardLayer::BottomCopper);
}

void SpecctraSesTests::convertsMicrometreCoordinatesAndNamedLayers() {
    const QByteArray ses = R"SES(
(session board
  (routes
    (resolution um 1)
    (network_out
      (net "N 1"
        (wire (path F.Cu 300 1000 -2000 2000 -3000))
        (wire (path B.Cu 400 2000 -3000 4000 -5000))))))
)SES";

    const SpecctraSesResult result = importSpecctraSes(ses);
    QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join(QLatin1Char('\n'))));
    QCOMPARE(result.routing.size(), 2);
    QCOMPARE(result.routing[0].net, QStringLiteral("N 1"));
    QCOMPARE(result.routing[0].points.first(), QPointF(1.0, 2.0));
    QCOMPARE(result.routing[0].points.last(), QPointF(2.0, 3.0));
    QCOMPARE(result.routing[0].width, 0.3);
    QCOMPARE(result.routing[1].layer, BoardLayer::BottomCopper);
}

void SpecctraSesTests::snapsRoundedEndpointsToPadCentres() {
    SketchItem pad;
    pad.kind = SketchItem::Kind::Pad;
    pad.points = {{10.0, 20.0}};
    pad.pad.width = 1.0;
    pad.pad.height = 1.0;
    pad.pad.layers = layerBit(BoardLayer::TopCopper);
    SketchItem route;
    route.kind = SketchItem::Kind::Wire;
    route.points = {{10.0004, 19.9996}, {15.0, 20.0}};

    const SketchDocument snapped = snapSpecctraRoutingToPads({route}, {pad});
    QCOMPARE(snapped.first().points.first(), QPointF(10.0, 20.0));
    QCOMPARE(snapped.first().points.last(), QPointF(15.0, 20.0));
}

void SpecctraSesTests::rejectsMalformedOrEmptyRouting() {
    QVERIFY(!importSpecctraSes("(session board (routes (resolution um 1)").errors.isEmpty());
    QVERIFY(!importSpecctraSes("(session board (routes (resolution um 1) (network_out)))").errors.isEmpty());
    QVERIFY(!importSpecctraSes("(session board (routes (resolution px 1) (network_out)))").errors.isEmpty());
}

QTEST_MAIN(SpecctraSesTests)
#include "SpecctraSesTests.moc"
