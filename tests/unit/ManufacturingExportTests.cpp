#include "hatt/ui/ComponentLibrary.hpp"
#include "hatt/ui/ManufacturingExport.hpp"
#include "hatt/ui/SketchCircuit.hpp"

#include <QtTest>

using namespace hatt::ui;

namespace {

SketchItem part(const QString& variant, const QString& label, const QString& value, const QString& footprint) {
    SketchItem item;
    item.kind = SketchItem::Kind::Symbol;
    item.variant = variant;
    item.label = label;
    item.value = value;
    item.footprint = footprint;
    item.points = {{0, 0}};
    return item;
}

QList<QByteArray> rows(const QByteArray& csv) {
    QList<QByteArray> result = csv.split('\n');
    if (!result.isEmpty() && result.last().isEmpty()) result.removeLast();
    for (auto& row : result) row = row.trimmed();
    return result;
}

} // namespace

class ManufacturingExportTests final : public QObject {
    Q_OBJECT

private slots:
    void referencesSortNaturally() {
        QStringList references{QStringLiteral("R10"), QStringLiteral("U1"), QStringLiteral("R2"), QStringLiteral("C3")};
        std::sort(references.begin(), references.end(), referenceLess);
        QCOMPARE(references, QStringList({QStringLiteral("C3"), QStringLiteral("R2"), QStringLiteral("R10"), QStringLiteral("U1")}));
    }

    void bomGroupsByDeviceValueAndFootprint() {
        ProjectLibrary library;
        DeviceDefinition regulator;
        regulator.id = newCustomDeviceId();
        regulator.name = QStringLiteral("LDO, 3.3 V");
        regulator.pinCount = 3;
        regulator.spec.manufacturer = QStringLiteral("Texas \"TI\" Instruments");
        regulator.spec.partNumber = QStringLiteral("TLV1117-33");
        library.customDevices = {regulator};
        registerProjectLibrary(library);

        SketchItem excluded = part(QStringLiteral("schematic.vdc"), QStringLiteral("V1"), QStringLiteral("5"), QStringLiteral("board.header-1x2"));
        excluded.excludeFromBoard = true;
        SketchItem ground;
        ground.kind = SketchItem::Kind::Symbol;
        ground.variant = QStringLiteral("schematic.ground");
        ground.points = {{0, 0}};
        const SketchDocument schematic{
            part(QStringLiteral("schematic.resistor"), QStringLiteral("R10"), QStringLiteral("1k"), QStringLiteral("board.r0603")),
            part(QStringLiteral("schematic.resistor"), QStringLiteral("R2"), QStringLiteral("1k"), QStringLiteral("board.r0603")),
            part(QStringLiteral("schematic.resistor"), QStringLiteral("R3"), QStringLiteral("4.7k"), QStringLiteral("board.r0603")),
            part(QStringLiteral("schematic.capacitor"), QStringLiteral("C1"), QStringLiteral("1k"), QStringLiteral("board.c0805")),
            part(regulator.id, QStringLiteral("U1"), QStringLiteral("3.3V"), QString()),
            excluded,
            ground,
        };
        const QVector<BomLine> bom = buildBom(schematic, library);
        QCOMPARE(bom.size(), 4);
        QCOMPARE(bom[0].references, QStringList{QStringLiteral("C1")});
        QCOMPARE(bom[1].references, QStringList({QStringLiteral("R2"), QStringLiteral("R10")}));
        QCOMPARE(bom[1].quantity(), 2);
        QCOMPARE(bom[1].value, QStringLiteral("1k"));
        QCOMPARE(bom[2].references, QStringList{QStringLiteral("R3")});
        QCOMPARE(bom[3].manufacturer, regulator.spec.manufacturer);
        QCOMPARE(bom[3].deviceName, regulator.name);

        const auto lines = rows(bomCsv(bom));
        QCOMPARE(lines.size(), 5);
        QCOMPARE(lines[0], QByteArray("Item,Quantity,References,Value,Footprint,Device,Manufacturer,Part number"));
        QVERIFY(lines[2].startsWith("2,2,R2 R10,1k,Resistor 0603,Resistor,,"));
        // Commas and quotes are quoted (RFC 4180).
        QVERIFY(lines[4].contains("\"LDO, 3.3 V\""));
        QVERIFY(lines[4].contains("\"Texas \"\"TI\"\" Instruments\""));
        QVERIFY(bomCsv(bom).contains("\r\n"));
    }

    void placementUsesPadCentresRotationAndSide() {
        SketchItem resistor = part(QStringLiteral("board.r0603"), QStringLiteral("R1"), QStringLiteral("1k"), QString());
        resistor.points = {{10, 20}};
        SketchItem header = part(QStringLiteral("board.header-1x2"), QStringLiteral("J1"), QString(), QString());
        header.points = {{30, 5}};
        header.quarterTurns = 1;
        header.onBottom = true;
        SketchItem hole = part(QStringLiteral("board.mounting-hole"), QStringLiteral("H1"), QString(), QString());
        SketchItem track;
        track.kind = SketchItem::Kind::Wire;
        track.points = {{0, 0}, {1, 0}};
        const QVector<PlacementLine> placement = buildPlacement({header, resistor, track, hole});
        const auto find = [&placement](const char* reference) {
            return *std::find_if(placement.begin(), placement.end(),
                                 [reference](const PlacementLine& l) { return l.reference == QLatin1String(reference); });
        };
        const PlacementLine r1 = find("R1");
        QCOMPARE(r1.centre, QPointF(10, -20));
        QCOMPARE(r1.rotation, 0.0);
        QVERIFY(!r1.bottom);
        const PlacementLine j1 = find("J1");
        // Pads (0,0) and (2.54,0) mirrored for the bottom (-2.54,0) and turned once: centre (30, 5-1.27).
        QVERIFY(qAbs(j1.centre.x() - 30.0) < 1e-9);
        QVERIFY(qAbs(j1.centre.y() - (-(5.0 - 1.27))) < 1e-9);
        QCOMPARE(j1.rotation, 270.0);
        QVERIFY(j1.bottom);
        QVERIFY(std::is_sorted(placement.begin(), placement.end(), [](const PlacementLine& a, const PlacementLine& b) {
            return referenceLess(a.reference, b.reference);
        }));

        const auto lines = rows(placementCsv(placement));
        QCOMPARE(lines[0], QByteArray("Designator,Value,Package,Mid X (mm),Mid Y (mm),Rotation,Layer"));
        QVERIFY(lines.contains("R1,1k,Resistor 0603,10.0000,-20.0000,0,Top"));
    }

    void dividerExampleAfterTransfer() {
        const SketchDocument schematic = dcDividerExample();
        const SketchDocument board = transferToBoard(schematic, {}).document;
        QCOMPARE(buildBom(schematic, {}).size(), 2); // two 1k resistors grouped, one source
        QCOMPARE(buildPlacement(board).size(), 3);
    }
};

QTEST_GUILESS_MAIN(ManufacturingExportTests)
#include "ManufacturingExportTests.moc"
