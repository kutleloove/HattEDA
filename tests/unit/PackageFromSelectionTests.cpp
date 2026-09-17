#include "hatt/ui/ComponentLibrary.hpp"
#include "hatt/ui/PackageFromSelection.hpp"

#include <QLineF>
#include <QtTest>

using namespace hatt::ui;

namespace {

SketchItem padItem(QPointF at, int number, double drill, BoardLayer layer = BoardLayer::TopCopper) {
    SketchItem item;
    item.kind = SketchItem::Kind::Pad;
    item.points = {at};
    item.pad.number = number;
    item.pad.shape = PadShape::Rect;
    item.pad.width = 1.2;
    item.pad.height = 0.8;
    item.pad.drillDiameter = drill;
    item.pad.layers = drill > 0 ? CopperLayerMask : layerBit(layer);
    item.layer = layer;
    return item;
}

SketchItem silk(SketchItem::Kind kind, QVector<QPointF> points, BoardLayer layer = BoardLayer::TopSilk) {
    SketchItem item;
    item.kind = kind;
    item.points = std::move(points);
    item.layer = layer;
    return item;
}

bool near(QPointF a, QPointF b) { return QLineF(a, b).length() < 1e-6; }

// The placed package puts every pad where the drawn pad was, on the same copper layers.
void comparePads(const QVector<PlacedPad>& expected, const QVector<PlacedPad>& actual) {
    QCOMPARE(actual.size(), expected.size());
    for (const auto& pad : expected) {
        const auto match = std::find_if(actual.begin(), actual.end(), [&](const PlacedPad& other) {
            return near(other.center, pad.center) && other.layers == pad.layers &&
                   qFuzzyCompare(other.width, pad.width) && qFuzzyCompare(other.height, pad.height);
        });
        QVERIFY2(match != actual.end(), qPrintable(QStringLiteral("pad at %1,%2").arg(pad.center.x()).arg(pad.center.y())));
    }
}

} // namespace

class PackageFromSelectionTests final : public QObject {
    Q_OBJECT

private slots:
    void selectionBecomesExplicitFootprint();
    void bottomSidePackageIsStoredFromTheTop();
    void decomposeRestoresPadsAndSilk();
    void selectionWithoutPadsGivesNoFootprint();
};

void PackageFromSelectionTests::selectionBecomesExplicitFootprint() {
    SketchDocument document;
    document << padItem({10, 10}, 1, 0.6) << padItem({12.54, 10}, 1, 0.6) // duplicate numbers
             << silk(SketchItem::Kind::Rectangle, {{9, 9}, {13.5, 11}});
    SketchItem track;
    track.kind = SketchItem::Kind::Wire;
    track.points = {{0, 0}, {5, 0}};
    document << track;
    SketchItem edge = silk(SketchItem::Kind::Line, {{0, 0}, {1, 1}}, BoardLayer::BoardEdge);
    document << edge;

    const auto result = extractPackage(document, {0, 1, 2, 3, 4}, PackageOrigin::FirstPad);
    QCOMPARE(result.footprint.pads.size(), 2);
    QCOMPARE(result.footprint.shapes.size(), 1);
    QVERIFY(result.footprint.shapes.first().closed);
    QCOMPARE(result.footprint.shapes.first().points.size(), 4);
    QVERIFY(result.renumbered);
    QVERIFY(!result.mirrored);
    QCOMPARE(result.ignoredItems, 2);
    QCOMPARE(result.usedItems, QList<int>({0, 1, 2}));
    QVERIFY(near(result.origin, {10, 10}));
    QVERIFY(near(result.footprint.pins.first(), {0, 0}));
    QVERIFY2(validateExplicitFootprint(result.footprint).isEmpty(),
             qPrintable(validateExplicitFootprint(result.footprint)));

    FootprintDefinition footprint = result.footprint;
    footprint.id = newCustomFootprintId();
    footprint.name = QStringLiteral("TEST-2");
    registerSymbols({footprintSymbol(footprint)});
    const SketchDocument replaced = replaceWithPackage(document, result, footprint.id);
    QCOMPARE(replaced.size(), 3); // track, board edge line, package
    QCOMPARE(replaced.last().kind, SketchItem::Kind::Symbol);
    QVector<PlacedPad> drawn = itemPads(document[0]) + itemPads(document[1]);
    comparePads(drawn, itemPads(replaced.last()));

    const auto centred = extractPackage(document, {0, 1, 2}, PackageOrigin::PadCentre);
    QVERIFY(near(centred.origin, {11.27, 10}));
}

void PackageFromSelectionTests::bottomSidePackageIsStoredFromTheTop() {
    SketchDocument document;
    document << padItem({20, 5}, 1, 0.0, BoardLayer::BottomCopper) << padItem({22, 5}, 2, 0.0, BoardLayer::BottomCopper)
             << silk(SketchItem::Kind::Line, {{19, 4}, {23, 4}}, BoardLayer::BottomSilk);
    const auto result = extractPackage(document, {0, 1, 2}, PackageOrigin::FirstPad);
    QVERIFY(result.mirrored);
    QVERIFY(!result.renumbered);
    for (const auto& pad : result.footprint.pads) QCOMPARE(pad.layers, layerBit(BoardLayer::TopCopper));
    QVERIFY(near(result.footprint.pins[1], {-2, 0}));

    FootprintDefinition footprint = result.footprint;
    footprint.id = newCustomFootprintId();
    footprint.name = QStringLiteral("BOTTOM-2");
    registerSymbols({footprintSymbol(footprint)});
    const SketchDocument replaced = replaceWithPackage(document, result, footprint.id);
    QCOMPARE(replaced.size(), 1);
    QVERIFY(replaced.first().onBottom);
    comparePads(itemPads(document[0]) + itemPads(document[1]), itemPads(replaced.first()));
}

void PackageFromSelectionTests::decomposeRestoresPadsAndSilk() {
    SketchItem footprint;
    footprint.kind = SketchItem::Kind::Symbol;
    footprint.variant = QStringLiteral("board.soic8");
    footprint.points = {{30, 30}};
    footprint.quarterTurns = 1;
    footprint.onBottom = true;
    const auto* symbol = findSymbol(footprint.variant);
    QVERIFY(symbol != nullptr);

    SketchDocument document{footprint};
    QCOMPARE(decomposePackages(document, {0}), 1);
    int pads = 0;
    int outlines = 0;
    QVector<PlacedPad> placed;
    for (const auto& item : document) {
        if (item.kind == SketchItem::Kind::Pad) {
            ++pads;
            placed += itemPads(item);
        } else {
            ++outlines;
            QCOMPARE(item.layer, BoardLayer::BottomSilk);
        }
    }
    QCOMPARE(pads, symbol->pads.size());
    QVERIFY(outlines >= 1);
    comparePads(itemPads(footprint), placed);

    // Nothing to decompose: the document stays as it is.
    SketchDocument drawn = document;
    QCOMPARE(decomposePackages(drawn, {0, 1}), 0);
    QCOMPARE(drawn.size(), document.size());
}

void PackageFromSelectionTests::selectionWithoutPadsGivesNoFootprint() {
    SketchDocument document{silk(SketchItem::Kind::Circle, {{0, 0}, {1, 0}})};
    const auto result = extractPackage(document, {0}, PackageOrigin::FirstPad);
    QVERIFY(result.footprint.pads.isEmpty());
}

QTEST_GUILESS_MAIN(PackageFromSelectionTests)
#include "PackageFromSelectionTests.moc"
