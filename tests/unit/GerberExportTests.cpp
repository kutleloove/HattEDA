#include "hatt/ui/GerberExport.hpp"
#include "hatt/ui/StrokeFont.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace hatt::ui;

class GerberExportTests final : public QObject {
    Q_OBJECT

private slots:
    void buildsCopperMaskPasteOutlineAndDrills();
    void serializesGerberX2AndExcellon();
    void writesFilesAtomically();
    void zonesAreLeftOutUnlessRequested();
    void designatorsAndTextGoToSilkscreen();
    void strokeFontCoversDesignators();
};

void GerberExportTests::buildsCopperMaskPasteOutlineAndDrills() {
    SketchItem smd;
    smd.kind = SketchItem::Kind::Pad;
    smd.points = {{10.0, 20.0}};
    smd.layer = BoardLayer::TopCopper;
    smd.pad = {1, PadShape::Rect, 1.2, 0.8, 0.0, layerBit(BoardLayer::TopCopper)};

    SketchItem via;
    via.kind = SketchItem::Kind::Via;
    via.points = {{15.0, 25.0}};
    via.width = 0.8;
    via.drillDiameter = 0.4;

    SketchItem bottomTrack;
    bottomTrack.kind = SketchItem::Kind::Wire;
    bottomTrack.points = {{1.0, 2.0}, {3.0, 4.0}};
    bottomTrack.layer = BoardLayer::BottomCopper;
    bottomTrack.width = 0.5;

    SketchItem outline;
    outline.kind = SketchItem::Kind::Rectangle;
    outline.points = {{0.0, 0.0}, {30.0, 40.0}};
    outline.variant = BoardOutlineVariant;
    outline.layer = BoardLayer::BoardEdge;

    SketchItem text;
    text.kind = SketchItem::Kind::Text;
    text.points = {{5.0, 5.0}};
    text.label = QStringLiteral("R1");
    text.layer = BoardLayer::BoardEdge; // no fabrication text on the profile

    const CamOutput output = buildCamOutput({smd, via, bottomTrack, outline, text});
    QCOMPARE(output.layers.size(), 9);
    QCOMPARE(output.layers[static_cast<int>(CamLayerKind::TopCopper)].primitives.size(), 2);
    QCOMPARE(output.layers[static_cast<int>(CamLayerKind::BottomCopper)].primitives.size(), 2);
    QCOMPARE(output.layers[static_cast<int>(CamLayerKind::TopMask)].primitives.size(), 1);
    QCOMPARE(output.layers[static_cast<int>(CamLayerKind::BottomMask)].primitives.size(), 0);
    QCOMPARE(output.layers[static_cast<int>(CamLayerKind::TopPaste)].primitives.size(), 1);
    QCOMPARE(output.layers[static_cast<int>(CamLayerKind::Outline)].primitives.size(), 1);
    QCOMPARE(output.drills.size(), 1);
    QCOMPARE(output.drills.first().at, QPointF(15.0, -25.0));
    QCOMPARE(output.drills.first().diameter, 0.4);
    QCOMPARE(output.skippedTexts, 1);
}

void GerberExportTests::zonesAreLeftOutUnlessRequested() {
    SketchItem zone;
    zone.kind = SketchItem::Kind::Polyline;
    zone.variant = CopperZoneVariant;
    zone.closed = true;
    zone.layer = BoardLayer::BottomCopper;
    zone.points = {{0.0, 0.0}, {10.0, 0.0}, {10.0, 10.0}, {0.0, 10.0}};

    const CamOutput safe = buildCamOutput({zone});
    QCOMPARE(safe.skippedZones, 1);
    QVERIFY(safe.layers[static_cast<int>(CamLayerKind::BottomCopper)].primitives.isEmpty());

    CamOptions options;
    options.includeZones = true;
    const CamOutput withZones = buildCamOutput({zone}, options);
    QCOMPARE(withZones.skippedZones, 0);
    const auto& copper = withZones.layers[static_cast<int>(CamLayerKind::BottomCopper)].primitives;
    QCOMPARE(copper.size(), 1);
    QCOMPARE(copper.first().kind, CamPrimitive::Kind::Region);
}

void GerberExportTests::designatorsAndTextGoToSilkscreen() {
    SketchItem top;
    top.kind = SketchItem::Kind::Symbol;
    top.variant = QStringLiteral("board.r0603");
    top.points = {{10.0, 10.0}};
    top.label = QStringLiteral("R1");
    SketchItem bottom = top;
    bottom.label = QStringLiteral("C2");
    bottom.onBottom = true;
    bottom.points = {{30.0, 10.0}};
    SketchItem text;
    text.kind = SketchItem::Kind::Text;
    text.points = {{5.0, 20.0}};
    text.label = QStringLiteral("REV A");
    text.layer = BoardLayer::TopSilk;

    auto silkStrokes = [](const CamOutput& output, CamLayerKind kind) {
        int count = 0;
        for (const auto& primitive : output.layers[static_cast<int>(kind)].primitives) {
            count += primitive.kind == CamPrimitive::Kind::Stroke ? 1 : 0;
        }
        return count;
    };
    CamOptions plain;
    plain.designators = false;
    const CamOutput without = buildCamOutput({top, bottom}, plain);
    const CamOutput with = buildCamOutput({top, bottom, text});
    QVERIFY(silkStrokes(with, CamLayerKind::TopSilk) > silkStrokes(without, CamLayerKind::TopSilk));
    QVERIFY(silkStrokes(with, CamLayerKind::BottomSilk) > silkStrokes(without, CamLayerKind::BottomSilk));
    QCOMPARE(with.skippedTexts, 0);

    // The designator sits above the footprint (Y up in CAM, so above means larger Y).
    const QRectF bounds = itemBounds(top);
    double lowest = 1e9;
    const auto& silk = with.layers[static_cast<int>(CamLayerKind::TopSilk)].primitives;
    const auto& plainSilk = without.layers[static_cast<int>(CamLayerKind::TopSilk)].primitives;
    for (qsizetype i = plainSilk.size(); i < silk.size(); ++i) {
        for (const QPointF& point : silk[i].points) {
            if (point.x() > 20.0) continue; // skip the REV A text further down
            if (point.y() < -15.0) continue;
            lowest = std::min(lowest, point.y());
        }
    }
    QVERIFY2(lowest >= -bounds.top() + CamDesignatorGap - 1e-6, qPrintable(QString::number(lowest)));
}

void GerberExportTests::strokeFontCoversDesignators() {
    const auto lines = strokeText(QStringLiteral("U10-R"), {0.0, 0.0}, 1.2);
    QVERIFY(lines.size() >= 8);
    for (const auto& line : lines) {
        for (const QPointF& point : line) {
            QVERIFY(point.y() >= -1e-9 && point.y() <= 1.2 + 1e-9);
            QVERIFY(point.x() >= -1e-9 && point.x() <= strokeTextWidth(QStringLiteral("U10-R"), 1.2) + 1e-9);
        }
    }
    QCOMPARE(strokeTextWidth(QString(), 1.0), 0.0);
}

void GerberExportTests::serializesGerberX2AndExcellon() {
    CamLayer copper{CamLayerKind::TopCopper,
                    {{CamPrimitive::Kind::Flash,
                      {CamApertureShape::Rectangle, 1.2, 0.8},
                      {{10.0, -20.0}}},
                     {CamPrimitive::Kind::Region, {}, {{0.0, 0.0}, {2.0, 0.0}, {2.0, -2.0}}}}};
    const QByteArray gerber = gerberLayer(copper, QStringLiteral("test"));
    QVERIFY(gerber.contains("%TF.FileFunction,Copper,L1,Top*%"));
    QVERIFY(gerber.contains("%FSLAX46Y46*%"));
    QVERIFY(gerber.contains("%MOMM*%"));
    QVERIFY(gerber.contains("%ADD10R,1.200000X0.800000*%"));
    QVERIFY(gerber.contains("X10000000Y-20000000D03*"));
    QVERIFY(gerber.contains("G36*"));
    QVERIFY(gerber.endsWith("M02*\n"));

    const QByteArray drill = excellonDrill({{{1.0, -2.0}, 0.8}, {{3.0, -4.0}, 0.4}});
    QVERIFY(drill.contains("METRIC,TZ"));
    QVERIFY(drill.indexOf("T1C0.400") < drill.indexOf("T2C0.800"));
    QVERIFY(drill.contains("X3.000Y-4.000"));
    QVERIFY(drill.endsWith("M30\n"));
}

void GerberExportTests::writesFilesAtomically() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QVector<CamFile> files = {{QStringLiteral("board-F_Cu.gtl"), QByteArray("gerber")},
                                    {QStringLiteral("board-PTH.drl"), QByteArray("drill")}};
    QCOMPARE(writeCamFiles(files, directory.path()), QString());
    for (const CamFile& expected : files) {
        QFile file(directory.filePath(expected.fileName));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), expected.content);
    }
    QVERIFY(!writeCamFiles({{QStringLiteral("../escape.gbr"), QByteArray("bad")}}, directory.path())
                 .isEmpty());
}

QTEST_MAIN(GerberExportTests)
#include "GerberExportTests.moc"
