#include "hatt/ui/ComponentLibrary.hpp"
#include "hatt/ui/ComponentCatalog.hpp"
#include "hatt/ui/LibraryModel.hpp"

#include <QtTest>

#include <cmath>

using namespace hatt::ui;

namespace {

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool near(QPointF a, QPointF b) { return near(a.x(), b.x()) && near(a.y(), b.y()); }

FootprintParams soic8() {
    FootprintParams p;
    p.style = PackageStyle::DualRow;
    p.padCount = 8;
    p.pitch = 1.27;
    p.rowSpacing = 5.4;
    p.shape = PadShape::Rect;
    p.padWidth = 1.5;
    p.padLength = 0.6;
    p.drill = 0.0;
    return p;
}

FootprintDefinition drawnFootprint() {
    FootprintDefinition footprint;
    footprint.id = CustomFootprintPrefix + QStringLiteral("drawn");
    footprint.name = QStringLiteral("Drawn");
    PadDefinition second;
    second.number = 2;
    PadDefinition first;
    first.number = 1;
    first.drillDiameter = 0.4;
    // Stored out of pad number order on purpose.
    footprint.pads = {second, first};
    footprint.pins = {{2.0, 0.0}, {-2.0, 0.0}};
    SymbolShape outline;
    outline.points = {{-3, -1}, {3, -1}, {3, 1}, {-3, 1}};
    outline.closed = true;
    footprint.shapes = {outline};
    return footprint;
}

} // namespace

class ComponentLibraryTests final : public QObject {
    Q_OBJECT

private slots:
    void builtInCatalogIsConsistentAndSearchable() {
        registerBuiltInCatalog();
        QSet<QString> componentIds;
        QVERIFY(componentCatalog().size() >= 50);
        for (const auto& entry : componentCatalog()) {
            QVERIFY2(!componentIds.contains(entry.device.id), qPrintable(entry.device.id));
            componentIds.insert(entry.device.id);
            QVERIFY(entry.device.pinCount > 0);
            QCOMPARE(entry.device.pinNames.size(), entry.device.pinCount);
            QCOMPARE(entry.pinTypes.size(), entry.device.pinCount);
            QVERIFY(findSimulationModel(entry.device.simulationModel) != nullptr);
            const auto* symbol = findSymbol(entry.device.id);
            QVERIFY(symbol != nullptr);
            QCOMPARE(symbol->pins.size(), entry.device.pinCount);
            const auto* package = findSymbol(entry.device.footprint);
            QVERIFY2(package != nullptr, qPrintable(entry.device.footprint));
            QCOMPARE(package->pins.size(), entry.device.pinCount);
            for (const QString& option : entry.footprintOptions) {
                const auto* candidate = findSymbol(option);
                QVERIFY2(candidate != nullptr, qPrintable(option));
                QCOMPARE(candidate->pins.size(), entry.device.pinCount);
            }
            QVERIFY(validatePinPadMap(entry.device.pinPadMap, entry.device.pinCount).isEmpty());
        }
        QSet<QString> footprintIds;
        QVERIFY(footprintCatalog().size() >= 70);
        for (const auto& footprint : footprintCatalog()) {
            QVERIFY2(!footprintIds.contains(footprint.id), qPrintable(footprint.id));
            footprintIds.insert(footprint.id);
            QVERIFY2(validateFootprintParams(footprint.params).isEmpty(),
                     qPrintable(footprint.id + QStringLiteral(": ") + validateFootprintParams(footprint.params)));
            const SymbolDefinition symbol = footprintSymbol(footprint);
            QCOMPARE(symbol.pads.size(), footprint.params.padCount);
            QCOMPARE(symbol.pins.size(), footprint.params.padCount);
            for (int i = 0; i < symbol.pads.size(); ++i) QCOMPARE(symbol.pads[i].number, i + 1);
            QVERIFY(symbol.shapes.size() >= 2); // body plus the structural pin-1 mark
        }
        for (const QString& term : {QStringLiteral("resistor"), QStringLiteral("direnç"),
                                    QStringLiteral("1n4148"), QStringLiteral("npn"),
                                    QStringLiteral("opamp")}) {
            QVERIFY2(!searchComponentCatalog(term).isEmpty(), qPrintable(term));
        }
        QCOMPARE(findCatalogComponent(QStringLiteral("catalog.device.bc547"))->device.pinNames,
                 QStringList({QStringLiteral("C"), QStringLiteral("B"), QStringLiteral("E")}));
        QCOMPARE(findCatalogComponent(QStringLiteral("catalog.device.lm358"))->device.pinNames.size(), 8);

        QSet<QString> pickableIds;
        for (const auto* symbol : pickableDevices({})) {
            QVERIFY2(!pickableIds.contains(symbol->id), qPrintable(symbol->id));
            pickableIds.insert(symbol->id);
        }
        QVERIFY(pickableIds.contains(QStringLiteral("catalog.device.1n4148")));

        const auto twoPadFootprints = footprintsWithPads({}, 2);
        QVERIFY(twoPadFootprints.contains(findSymbol(QStringLiteral("catalog.footprint.passive.0603"))));
        QSet<QString> twoPadIds;
        for (const auto* symbol : twoPadFootprints) {
            QVERIFY2(!twoPadIds.contains(symbol->id), qPrintable(symbol->id));
            twoPadIds.insert(symbol->id);
        }
    }

    void dualRowPadsAreNumberedCounterClockwise() {
        FootprintDefinition footprint;
        footprint.id = CustomFootprintPrefix + QStringLiteral("soic8");
        footprint.name = QStringLiteral("SOIC-8");
        footprint.params = soic8();
        QVERIFY(validateFootprintParams(footprint.params).isEmpty());
        const SymbolDefinition symbol = footprintSymbol(footprint);
        QCOMPARE(symbol.workspace, Workspace::Board);
        QCOMPARE(symbol.pins.size(), 8);
        QCOMPARE(symbol.pads.size(), 8);
        QVERIFY(near(symbol.pins[0], {-2.7, -1.905}));
        QVERIFY(near(symbol.pins[3], {-2.7, 1.905}));
        QVERIFY(near(symbol.pins[4], {2.7, 1.905}));
        QVERIFY(near(symbol.pins[7], {2.7, -1.905}));
        for (int i = 0; i < 8; ++i) {
            QCOMPARE(symbol.pads[i].number, i + 1);
            QCOMPARE(symbol.pads[i].drillDiameter, 0.0);
            QCOMPARE(symbol.pads[i].layers, 1 << static_cast<int>(BoardLayer::TopCopper));
        }
        // Silkscreen only: body outline and pin 1 mark, no copper shapes.
        QVERIFY(!symbol.shapes.isEmpty());
        for (const auto& shape : symbol.shapes) QVERIFY(!shape.copper && !shape.hole);
    }

    void quadRowSwapsPadSizesOnTopAndBottom() {
        FootprintParams p = soic8();
        p.style = PackageStyle::QuadRow;
        p.padCount = 16;
        p.pitch = 0.5;
        p.rowSpacing = 5.0;
        p.padWidth = 1.0;
        p.padLength = 0.3;
        QVERIFY2(validateFootprintParams(p).isEmpty(), qPrintable(validateFootprintParams(p)));
        FootprintDefinition footprint;
        footprint.params = p;
        const SymbolDefinition symbol = footprintSymbol(footprint);
        QVERIFY(near(symbol.pins[0], {-2.5, -0.75}));
        QVERIFY(near(symbol.pins[4], {-0.75, 2.5}));
        QVERIFY(near(symbol.pads[0].width, 1.0) && near(symbol.pads[0].height, 0.3));
        QVERIFY(near(symbol.pads[4].width, 0.3) && near(symbol.pads[4].height, 1.0));
    }

    void throughHolePinOneIsSquare() {
        FootprintParams p;
        p.style = PackageStyle::SingleRow;
        p.padCount = 3;
        FootprintDefinition footprint;
        footprint.params = p;
        const SymbolDefinition symbol = footprintSymbol(footprint);
        QCOMPARE(symbol.pads[0].shape, PadShape::Rect);
        QCOMPARE(symbol.pads[1].shape, PadShape::Round);
        const int both = (1 << static_cast<int>(BoardLayer::TopCopper)) |
                         (1 << static_cast<int>(BoardLayer::BottomCopper));
        QCOMPARE(symbol.pads[1].layers, both);
    }

    void invalidParametersAreRejected() {
        FootprintParams odd = soic8();
        odd.padCount = 7;
        QVERIFY(!validateFootprintParams(odd).isEmpty());
        FootprintParams overlapping = soic8();
        overlapping.padLength = 1.5;
        QVERIFY(!validateFootprintParams(overlapping).isEmpty());
        FootprintParams twoTerminal = soic8();
        twoTerminal.style = PackageStyle::TwoTerminal;
        QVERIFY(!validateFootprintParams(twoTerminal).isEmpty());
        FootprintParams drill;
        drill.drill = 2.0;
        QVERIFY(!validateFootprintParams(drill).isEmpty());
        FootprintParams none;
        none.style = PackageStyle::None;
        QVERIFY(!validateFootprintParams(none).isEmpty());
    }

    void packageStyleTokensRoundTrip() {
        for (auto style : {PackageStyle::None, PackageStyle::TwoTerminal, PackageStyle::SingleRow,
                           PackageStyle::DualRow, PackageStyle::QuadRow}) {
            PackageStyle read = PackageStyle::None;
            QVERIFY(packageStyleFromToken(packageStyleToken(style), read));
            QCOMPARE(read, style);
        }
        PackageStyle unused;
        QVERIFY(!packageStyleFromToken(QStringLiteral("bga"), unused));
    }

    void suggestionWithoutDataOnlyUsesThePinCount() {
        DeviceDefinition device;
        device.pinCount = 8;
        const FootprintSuggestion dip = suggestFootprint(device);
        QVERIFY(!dip.fromDatasheet);
        QCOMPARE(dip.params.padCount, 8);
        QCOMPARE(dip.params.style, PackageStyle::DualRow);
        QVERIFY(near(dip.params.pitch, 2.54));
        QVERIFY(near(dip.params.rowSpacing, 7.62));
        QVERIFY(dip.params.drill > 0);
        QVERIFY(validateFootprintParams(dip.params).isEmpty());

        for (int pins : {1, 2, 3, 5}) {
            device.pinCount = pins;
            const FootprintSuggestion row = suggestFootprint(device);
            QCOMPARE(row.params.padCount, pins);
            QCOMPARE(row.params.style, PackageStyle::SingleRow);
            QVERIFY2(validateFootprintParams(row.params).isEmpty(), qPrintable(QString::number(pins)));
        }
    }

    void suggestionFollowsDatasheetGeometry() {
        DeviceDefinition device;
        device.pinCount = 8;
        device.spec.package = PackageStyle::DualRow;
        device.spec.pitch = 1.27;
        device.spec.bodyWidth = 3.9;
        device.spec.bodyLength = 4.9;
        device.spec.leadWidth = 0.41;
        device.spec.leadLength = 1.04;
        const FootprintSuggestion soic = suggestFootprint(device);
        QVERIFY(soic.fromDatasheet);
        QCOMPARE(soic.params.style, PackageStyle::DualRow);
        QCOMPARE(soic.params.drill, 0.0);
        QCOMPARE(soic.params.shape, PadShape::Rect);
        QVERIFY(near(soic.params.pitch, 1.27));
        QVERIFY(near(soic.params.rowSpacing, 4.94));
        QVERIFY(near(soic.params.padWidth, 1.64));
        QVERIFY(near(soic.params.padLength, 0.56));
        QVERIFY(soic.notes.isEmpty());

        // A datasheet package that cannot hold the pin count falls back to a single row.
        device.pinCount = 7;
        const FootprintSuggestion fallback = suggestFootprint(device);
        QCOMPARE(fallback.params.style, PackageStyle::SingleRow);
        QVERIFY(!fallback.notes.isEmpty());
    }

    void pinCurrentWidensPadsWithinThePitch() {
        QVERIFY(near(recommendedCopperWidth(1.0), 0.30, 0.01));
        QCOMPARE(recommendedCopperWidth(0.0), 0.0);
        QVERIFY(recommendedCopperWidth(2.0) > recommendedCopperWidth(1.0));

        DeviceDefinition device;
        device.pinCount = 8;
        device.spec.package = PackageStyle::DualRow;
        device.spec.pitch = 1.27;
        device.spec.bodyWidth = 3.9;
        device.spec.leadWidth = 0.41;
        device.spec.leadLength = 1.04;
        device.spec.pinCurrent = 2.0;
        const FootprintSuggestion two = suggestFootprint(device);
        QVERIFY(near(two.params.padLength, 0.78, 0.011));
        QCOMPARE(two.notes.size(), 1);

        device.spec.pinCurrent = 3.0;
        const FootprintSuggestion three = suggestFootprint(device);
        QVERIFY(near(three.params.padLength, 1.07));
        QCOMPARE(three.notes.size(), 2);
        QVERIFY(validateFootprintParams(three.params).isEmpty());
    }

    void explicitFootprintsKeepTheirGeometry() {
        FootprintDefinition footprint = drawnFootprint();
        QVERIFY(footprint.isExplicit());
        QCOMPARE(footprint.padCount(), 2);
        QVERIFY2(validateExplicitFootprint(footprint).isEmpty(),
                 qPrintable(validateExplicitFootprint(footprint)));
        const SymbolDefinition symbol = footprintSymbol(footprint);
        QCOMPARE(symbol.pads.size(), 2);
        QCOMPARE(symbol.pads[0].number, 1);
        QVERIFY(near(symbol.pins[0], {-2.0, 0.0}));
        QCOMPARE(symbol.pads[1].number, 2);
        QVERIFY(near(symbol.pins[1], {2.0, 0.0}));
        QCOMPARE(symbol.shapes.size(), 1);

        FootprintDefinition repeated = footprint;
        repeated.pads[0].number = 1;
        QVERIFY(!validateExplicitFootprint(repeated).isEmpty());
        FootprintDefinition unplaced = footprint;
        unplaced.pins.removeLast();
        QVERIFY(!validateExplicitFootprint(unplaced).isEmpty());
        FootprintDefinition drilled = footprint;
        drilled.pads[1].drillDiameter = 1.0;
        QVERIFY(!validateExplicitFootprint(drilled).isEmpty());
        FootprintDefinition noLayer = footprint;
        noLayer.pads[0].layers = 0;
        QVERIFY(!validateExplicitFootprint(noLayer).isEmpty());
    }

    void pinPadMapsMustBePermutations() {
        QVERIFY(validatePinPadMap({}, 3).isEmpty());
        QVERIFY(validatePinPadMap({3, 1, 2}, 3).isEmpty());
        QVERIFY(!validatePinPadMap({1, 2}, 3).isEmpty());
        QVERIFY(!validatePinPadMap({1, 1, 2}, 3).isEmpty());
        QVERIFY(!validatePinPadMap({0, 1, 2}, 3).isEmpty());
        QVERIFY(!validatePinPadMap({1, 2, 4}, 3).isEmpty());
    }

    void deviceSymbolsPlacePinsOnTheGrid() {
        DeviceDefinition twoPin;
        twoPin.pinCount = 2;
        const SymbolDefinition small = deviceSymbol(twoPin);
        QCOMPARE(small.pins.size(), 2);
        QVERIFY(near(small.pins[0], {-5.08, 0}));
        QVERIFY(near(small.pins[1], {5.08, 0}));

        DeviceDefinition ic;
        ic.pinCount = 5;
        ic.prefix = QStringLiteral("IC");
        ic.pinPadMap = {5, 4, 3, 2, 1};
        const SymbolDefinition box = deviceSymbol(ic);
        QCOMPARE(box.pins.size(), 5);
        QCOMPARE(box.prefix, QStringLiteral("IC"));
        QCOMPARE(box.defaultPinPadMap, ic.pinPadMap);
        for (const QPointF& pin : box.pins) {
            QVERIFY(near(std::abs(pin.x()), 7.62));
            QVERIFY(near(std::remainder(pin.y(), 2.54), 0.0, 1e-9));
        }
        // Left row downwards (pins 1-3), right row upwards (pins 4-5).
        QVERIFY(box.pins[0].x() < 0 && box.pins[1].y() > box.pins[0].y());
        QVERIFY(box.pins[3].x() > 0 && box.pins[4].y() < box.pins[3].y());

        ic.pinPadMap = {1, 1, 2, 3, 4};
        QVERIFY(deviceSymbol(ic).defaultPinPadMap.isEmpty());
    }

    void registeredLibrarySymbolsResolve() {
        ProjectLibrary library;
        FootprintDefinition footprint;
        footprint.id = newCustomFootprintId();
        footprint.name = QStringLiteral("Header 3");
        footprint.params.padCount = 3;
        DeviceDefinition device;
        device.id = newCustomDeviceId();
        device.name = QStringLiteral("Regulator");
        device.pinCount = 3;
        device.footprint = footprint.id;
        library.customFootprints = {footprint, drawnFootprint()};
        library.customDevices = {device};
        registerProjectLibrary(library);

        const auto* symbol = findSymbol(device.id);
        QVERIFY(symbol != nullptr);
        QCOMPARE(symbolDisplayName(*symbol), QStringLiteral("Regulator"));
        QVERIFY(isPickableDevice(device.id));
        QVERIFY(!isPickableDevice(footprint.id));
        QVERIFY(pickableDevices(library).contains(symbol));
        const auto footprints = footprintsWithPads(library, 3);
        QVERIFY(footprints.contains(findSymbol(footprint.id)));
        QVERIFY(!footprintsWithPads(library, 2).contains(findSymbol(footprint.id)));
        QVERIFY(footprintsWithPads(library, 2).contains(findSymbol(drawnFootprint().id)));

        // Re-registering an edited definition replaces it; the old pointer stays valid.
        device.name = QStringLiteral("LDO");
        library.customDevices = {device};
        registerProjectLibrary(library);
        QCOMPARE(symbolDisplayName(*findSymbol(device.id)), QStringLiteral("LDO"));
        QCOMPARE(symbolDisplayName(*symbol), QStringLiteral("Regulator"));
    }
};

QTEST_GUILESS_MAIN(ComponentLibraryTests)
#include "ComponentLibraryTests.moc"
