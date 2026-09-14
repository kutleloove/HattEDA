#include "hatt/ui/DesignChecks.hpp"
#include "hatt/ui/SketchCircuit.hpp"

#include <QtTest>

#include <algorithm>

using namespace hatt::ui;

namespace {

SketchItem symbol(const QString& variant, const QString& label, QPointF at, const QString& value = {}) {
    SketchItem item;
    item.kind = SketchItem::Kind::Symbol;
    item.variant = variant;
    item.label = label;
    item.value = value;
    item.points = {at};
    if (variant == QLatin1String("schematic.resistor")) {
        item.footprint = QStringLiteral("board.r0603");
        item.pinPadMap = {1, 2};
    }
    return item;
}

SketchItem wire(QVector<QPointF> points) {
    SketchItem item;
    item.kind = SketchItem::Kind::Wire;
    item.points = std::move(points);
    return item;
}

SketchItem track(QVector<QPointF> points, double width = 0.3, BoardLayer layer = BoardLayer::TopCopper) {
    SketchItem item = wire(std::move(points));
    item.width = width;
    item.layer = layer;
    return item;
}

SketchItem rectPad(QPointF at, double size = 1.0) {
    SketchItem item;
    item.kind = SketchItem::Kind::Pad;
    item.points = {at};
    item.pad.shape = PadShape::Rect;
    item.pad.width = item.pad.height = size;
    return item;
}

SketchItem outline(double w, double h) {
    SketchItem item;
    item.kind = SketchItem::Kind::Polyline;
    item.variant = BoardOutlineVariant;
    item.closed = true;
    item.points = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    return item;
}

QVector<CheckViolation> withRule(const CheckReport& report, const char* rule) {
    QVector<CheckViolation> result;
    for (const auto& v : report.violations) {
        if (v.rule == QLatin1String(rule)) result.append(v);
    }
    return result;
}

QStringList rules(const CheckReport& report) {
    QStringList result;
    for (const auto& v : report.violations) result << v.rule;
    return result;
}

} // namespace

class DesignChecksTests final : public QObject {
    Q_OBJECT

private slots:
    void rulesValidation() {
        QVERIFY(validateDesignRules(DesignRules{}).isEmpty());
        DesignRules rules;
        rules.clearance = 0;
        QVERIFY(!validateDesignRules(rules).isEmpty());
        rules = {};
        rules.minDrill = -1;
        QVERIFY(!validateDesignRules(rules).isEmpty());
        rules = {};
        rules.boardEdgeClearance = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!validateDesignRules(rules).isEmpty());
    }

    void unconnectedPinsAreWarnings() {
        const SketchDocument schematic{symbol(QStringLiteral("schematic.resistor"), QStringLiteral("R1"), {20, 20}, QStringLiteral("1k"))};
        const CheckReport report = runElectricalRuleCheck(schematic);
        QCOMPARE(report.count(CheckSeverity::Error), 0);
        const auto pins = withRule(report, "erc.unconnected-pin");
        QCOMPARE(pins.size(), 2);
        QVERIFY(pins[0].hasLocation);
        QCOMPARE(pins[0].location, QPointF(14.92, 20));
        QCOMPARE(pins[0].itemIds, QStringList{schematic.first().id});
        QCOMPARE(pins[0].workspace, Workspace::Schematic);
    }

    void shortedDanglingAndUnusedTerminals() {
        SketchDocument schematic{symbol(QStringLiteral("schematic.resistor"), QStringLiteral("R1"), {20, 20}, QStringLiteral("1k")),
                                 wire({{14.92, 20}, {14.92, 15}, {25.08, 15}, {25.08, 20}}),
                                 wire({{40, 40}, {50, 40}}),
                                 symbol(QStringLiteral("schematic.ground"), QString(), {70, 70})};
        const CheckReport report = runElectricalRuleCheck(schematic);
        QCOMPARE(withRule(report, "erc.shorted-component").size(), 1);
        QCOMPARE(withRule(report, "erc.unconnected-pin").size(), 0);
        const auto dangling = withRule(report, "erc.dangling-wire");
        QCOMPARE(dangling.size(), 2);
        QCOMPARE(dangling[0].location, QPointF(40, 40));
        QCOMPARE(withRule(report, "erc.unused-terminal").size(), 1);
    }

    void referencesValuesFootprintsAndNetNames() {
        SketchItem first = symbol(QStringLiteral("schematic.resistor"), QStringLiteral("R1"), {20, 20});
        SketchItem second = symbol(QStringLiteral("schematic.resistor"), QStringLiteral("R1"), {40, 20}, QStringLiteral("2k"));
        second.footprint.clear();
        second.pinPadMap.clear();
        SketchItem excluded = symbol(QStringLiteral("schematic.capacitor"), QStringLiteral("C1"), {60, 20}, QStringLiteral("1n"));
        excluded.excludeFromBoard = true;
        SketchItem vcc = symbol(QStringLiteral("schematic.power"), QStringLiteral("VCC"), {80, 20});
        SketchItem gnd = symbol(QStringLiteral("schematic.output"), QStringLiteral("GND"), {90, 20});
        SketchItem unnamed = symbol(QStringLiteral("schematic.input"), QString(), {100, 40});
        const SketchDocument schematic{first, second, excluded, vcc, gnd, wire({{80, 20}, {90, 20}}), unnamed};
        const CheckReport report = runElectricalRuleCheck(schematic);
        const auto analysis = withRule(report, "erc.analysis");
        // Two duplicate references, the unnamed port and the VCC/GND conflict.
        QCOMPARE(analysis.size(), 4);
        const auto conflict = std::find_if(analysis.begin(), analysis.end(), [](const CheckViolation& v) {
            return v.message.contains(QStringLiteral("GND")) && v.message.contains(QStringLiteral("VCC"));
        });
        QVERIFY(conflict != analysis.end());
        QVERIFY(conflict->hasLocation);
        QCOMPARE(conflict->itemIds.size(), 2);
        QCOMPARE(withRule(report, "erc.missing-value").size(), 1);
        const auto footprint = withRule(report, "erc.footprint");
        QCOMPARE(footprint.size(), 1);
        QCOMPARE(footprint.first().itemIds, QStringList{second.id});
    }

    void trackWidthDrillAndAnnularRing() {
        SketchItem via;
        via.kind = SketchItem::Kind::Via;
        via.points = {{10, 10}};
        via.width = 0.5;
        via.drillDiameter = 0.25;
        const SketchDocument board{outline(50, 30), track({{5, 5}, {20, 5}}, 0.1), via};
        const CheckReport report = runDesignRuleCheck({}, board, DesignRules{});
        QCOMPARE(withRule(report, "drc.track-width").size(), 1);
        QCOMPARE(withRule(report, "drc.drill").size(), 1);
        QCOMPARE(withRule(report, "drc.annular-ring").size(), 1);
        QCOMPARE(withRule(report, "drc.no-outline").size(), 0);
        QCOMPARE(withRule(report, "drc.clearance").size(), 0);
    }

    void clearanceDependsOnGapAndLayer() {
        DesignRules rules;
        rules.clearance = 0.2;
        // Rectangular pads 1 mm wide: centres 1.15 mm apart leave a 0.15 mm gap.
        CheckReport report = runDesignRuleCheck({}, {outline(50, 30), rectPad({10, 10}), rectPad({11.15, 10})}, rules);
        const auto close = withRule(report, "drc.clearance");
        QCOMPARE(close.size(), 1);
        QVERIFY(close.first().message.contains(QStringLiteral("0.150")));
        QVERIFY(withRule(runDesignRuleCheck({}, {outline(50, 30), rectPad({10, 10}), rectPad({11.25, 10})}, rules),
                         "drc.clearance").isEmpty());
        // Tracks 0.3 mm wide with centre lines 0.4 mm apart: 0.1 mm gap, but only on a shared layer.
        report = runDesignRuleCheck({}, {outline(50, 30), track({{5, 5}, {20, 5}}), track({{5, 5.4}, {20, 5.4}})}, rules);
        QCOMPARE(withRule(report, "drc.clearance").size(), 1);
        report = runDesignRuleCheck({}, {outline(50, 30), track({{5, 5}, {20, 5}}),
                                         track({{5, 5.4}, {20, 5.4}}, 0.3, BoardLayer::BottomCopper)}, rules);
        QCOMPARE(withRule(report, "drc.clearance").size(), 0);
        // Touching copper is connected, not a clearance problem.
        report = runDesignRuleCheck({}, {outline(50, 30), track({{5, 5}, {20, 5}}), track({{20, 5}, {20, 15}})}, rules);
        QCOMPARE(withRule(report, "drc.clearance").size(), 0);
    }

    void boardEdgeAndOutline() {
        CheckReport report = runDesignRuleCheck({}, {rectPad({10, 10})}, DesignRules{});
        QCOMPARE(withRule(report, "drc.no-outline").size(), 1);
        report = runDesignRuleCheck({}, {outline(50, 30), rectPad({0.6, 10}), rectPad({60, 10}), rectPad({25, 15})}, DesignRules{});
        const auto edge = withRule(report, "drc.board-edge");
        QCOMPARE(edge.size(), 2);
        QVERIFY(edge[0].location == QPointF(0.6, 10) || edge[1].location == QPointF(0.6, 10));
    }

    void shortsUnroutedOverlapAndPlacement() {
        const SketchDocument schematic = dcDividerExample();
        BoardTransfer transfer = transferToBoard(schematic, {outline(80, 60)});
        QVERIFY2(transfer.errors.isEmpty(), qPrintable(transfer.errors.join(QLatin1Char('\n'))));
        SketchDocument board = transfer.document;
        CheckReport report = runDesignRuleCheck(schematic, board, DesignRules{});
        QVERIFY2(withRule(report, "drc.netlist").isEmpty(), qPrintable(rules(report).join(QLatin1Char(' '))));
        const int unrouted = withRule(report, "drc.unrouted").size();
        QCOMPARE(unrouted, 3); // V1+/R1, R1/R2, R2/V1- (ground)
        QCOMPARE(withRule(report, "drc.short").size(), 0);
        QCOMPARE(withRule(report, "drc.not-placed").size(), 0);

        // A track across the two pads of R1 joins different nets.
        const auto r1 = std::find_if(board.begin(), board.end(), [](const SketchItem& i) { return i.label == QLatin1String("R1"); });
        QVERIFY(r1 != board.end());
        const auto pads = itemPads(*r1);
        board.append(track({pads[0].center, pads[1].center}, 0.2));
        report = runDesignRuleCheck(schematic, board, DesignRules{});
        QCOMPARE(withRule(report, "drc.short").size(), 1);

        // Two footprints on top of each other overlap; a missing footprint is reported.
        SketchDocument stacked = transfer.document;
        const auto r2 = std::find_if(stacked.begin(), stacked.end(), [](const SketchItem& i) { return i.label == QLatin1String("R2"); });
        const auto r1Stacked = std::find_if(stacked.begin(), stacked.end(), [](const SketchItem& i) { return i.label == QLatin1String("R1"); });
        r2->points = r1Stacked->points;
        report = runDesignRuleCheck(schematic, stacked, DesignRules{});
        QCOMPARE(withRule(report, "drc.overlap").size(), 1);
        stacked.erase(r2);
        report = runDesignRuleCheck(schematic, stacked, DesignRules{});
        const auto missing = withRule(report, "drc.not-placed");
        QCOMPARE(missing.size(), 1);
        QCOMPARE(missing.first().workspace, Workspace::Schematic);
    }

    void routingANetRemovesItsUnroutedWarning() {
        const SketchDocument schematic = dcDividerExample();
        SketchDocument board = transferToBoard(schematic, {outline(80, 60)}).document;
        const auto find = [&board](const char* label) {
            return *std::find_if(board.begin(), board.end(), [label](const SketchItem& i) { return i.label == QLatin1String(label); });
        };
        // R1 pad 2 and R2 pad 1 share the divider midpoint net.
        const QPointF from = itemPads(find("R1"))[1].center;
        const QPointF to = itemPads(find("R2"))[0].center;
        const int before = withRule(runDesignRuleCheck(schematic, board, DesignRules{}), "drc.unrouted").size();
        board.append(track({from, QPointF(from.x(), to.y() - 3), QPointF(to.x(), to.y() - 3), to}, 0.25));
        const CheckReport after = runDesignRuleCheck(schematic, board, DesignRules{});
        QCOMPARE(withRule(after, "drc.unrouted").size(), before - 1);
        QCOMPARE(withRule(after, "drc.short").size(), 0);
    }
};

QTEST_GUILESS_MAIN(DesignChecksTests)
#include "DesignChecksTests.moc"
