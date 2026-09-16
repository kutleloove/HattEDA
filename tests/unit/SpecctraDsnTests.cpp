#include "hatt/ui/ComponentCatalog.hpp"
#include "hatt/ui/DesignChecks.hpp"
#include "hatt/ui/SketchCircuit.hpp"
#include "hatt/ui/SpecctraDsn.hpp"

#include <QtTest>

using namespace hatt::ui;

class SpecctraDsnTests final : public QObject {
    Q_OBJECT

private slots:
    void exportsOutlineLayersPadsAndNets();
    void honoursSingleLayerNetClasses();
    void acceptsClosedRectangleOnBoardEdgeLayer();
    void rejectsMissingOutlineAndBrokenConnectivity();
};

void SpecctraDsnTests::exportsOutlineLayersPadsAndNets() {
    const SketchDocument schematic = dcDividerExample();
    SketchItem outline;
    outline.kind = SketchItem::Kind::Polyline;
    outline.variant = BoardOutlineVariant;
    outline.closed = true;
    outline.points = {{0, 0}, {60, 0}, {60, 40}, {0, 40}};
    const BoardTransfer transfer = transferToBoard(schematic, {outline});
    QVERIFY2(transfer.errors.isEmpty(), qPrintable(transfer.errors.join('\n')));

    const SpecctraDsnResult dsn =
        exportSpecctraDsn(schematic, transfer.document, DesignRules{}, QStringLiteral("divider"));
    QVERIFY2(dsn.errors.isEmpty(), qPrintable(dsn.errors.join('\n')));
    QVERIFY(dsn.data.startsWith("(pcb \"divider\""));
    QVERIFY(dsn.data.contains("(parser (string_quote \")"));
    QVERIFY(dsn.data.contains("(layer F.Cu"));
    QVERIFY(dsn.data.contains("(layer B.Cu"));
    QVERIFY(dsn.data.contains("(boundary (path pcb"));
    QVERIFY(dsn.data.contains("(placement"));
    QVERIFY(dsn.data.contains("(padstack"));
    QVERIFY(dsn.data.contains("(network"));
    QVERIFY(dsn.data.contains("R1-1"));
    QVERIFY(dsn.data.contains("(class \"HATT_NET_"));
    QVERIFY(dsn.data.contains("(width 635)"));
    QVERIFY(dsn.data.endsWith("(wiring)\n)\n"));
}

void SpecctraDsnTests::honoursSingleLayerNetClasses() {
    const SketchDocument schematic = dcDividerExample();
    SketchItem outline;
    outline.kind = SketchItem::Kind::Rectangle;
    outline.layer = BoardLayer::BoardEdge;
    outline.points = {{0, 0}, {60, 40}};
    BoardTransfer transfer = transferToBoard(schematic, {outline});
    QVERIFY2(transfer.errors.isEmpty(), qPrintable(transfer.errors.join(QLatin1Char('\n'))));
    for (SketchItem& item : transfer.document) {
        if (item.kind == SketchItem::Kind::Symbol) item.variant = QStringLiteral("board.dip8");
    }

    DesignRules rules;
    rules.netClasses = effectiveNetClasses(rules);
    for (NetClass& netClass : rules.netClasses)
        netClass.layers = layerBit(BoardLayer::BottomCopper);
    const SpecctraDsnResult dsn =
        exportSpecctraDsn(schematic, transfer.document, rules, QStringLiteral("single-layer"));
    QVERIFY2(dsn.errors.isEmpty(), qPrintable(dsn.errors.join(QLatin1Char('\n'))));
    QVERIFY(!dsn.data.contains("(layer F.Cu"));
    QVERIFY(dsn.data.contains("(layer B.Cu"));
    QVERIFY(dsn.data.contains("(use_layer B.Cu)"));
    QVERIFY(!dsn.data.contains("(use_via"));
    QVERIFY(!dsn.data.contains("(shape (circle F.Cu"));
}

void SpecctraDsnTests::acceptsClosedRectangleOnBoardEdgeLayer() {
    const SketchDocument schematic = dcDividerExample();
    SketchItem outline;
    outline.kind = SketchItem::Kind::Rectangle;
    outline.layer = BoardLayer::BoardEdge;
    outline.points = {{0, 0}, {60, 40}};
    const BoardTransfer transfer = transferToBoard(schematic, {outline});
    QVERIFY2(transfer.errors.isEmpty(), qPrintable(transfer.errors.join(QLatin1Char('\n'))));

    const SpecctraDsnResult dsn =
        exportSpecctraDsn(schematic, transfer.document, DesignRules{}, QStringLiteral("rectangle"));
    QVERIFY2(dsn.errors.isEmpty(), qPrintable(dsn.errors.join(QLatin1Char('\n'))));
    QVERIFY(dsn.data.contains("(boundary (path pcb 0 0 0 60000 0 60000 -40000 0 -40000 0 0))"));

    const CheckReport check = runDesignRuleCheck(schematic, transfer.document, DesignRules{});
    for (const auto& violation : check.violations) {
        QVERIFY(violation.rule != QStringLiteral("drc.no-outline"));
    }
}

void SpecctraDsnTests::rejectsMissingOutlineAndBrokenConnectivity() {
    SketchDocument schematic = dcDividerExample();
    SpecctraDsnResult dsn = exportSpecctraDsn(schematic, {}, DesignRules{}, QStringLiteral("bad"));
    QVERIFY(dsn.data.isEmpty());
    QVERIFY(!dsn.errors.isEmpty());

    schematic.first().id.clear();
    SketchItem outline;
    outline.kind = SketchItem::Kind::Polyline;
    outline.variant = BoardOutlineVariant;
    outline.closed = true;
    outline.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    dsn = exportSpecctraDsn(schematic, {outline}, DesignRules{}, QStringLiteral("bad"));
    QVERIFY(dsn.data.isEmpty());
    QVERIFY(!dsn.errors.isEmpty());

    schematic = dcDividerExample();
    SketchItem track;
    track.kind = SketchItem::Kind::Wire;
    track.layer = BoardLayer::TopCopper;
    track.points = {{1, 1}, {5, 1}};
    dsn = exportSpecctraDsn(schematic, {outline, track}, DesignRules{}, QStringLiteral("bad"));
    QVERIFY(dsn.data.isEmpty());
    QVERIFY(!dsn.errors.isEmpty());
}

QTEST_MAIN(SpecctraDsnTests)
#include "SpecctraDsnTests.moc"
