#include "hatt/ui/GerberExport.hpp"

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
