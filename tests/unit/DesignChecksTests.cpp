#include "hatt/ui/BoardCopper.hpp"
#include "hatt/ui/DesignChecks.hpp"
#include "hatt/ui/SketchCircuit.hpp"

#include <QJsonArray>
#include <QJsonObject>
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

    void clearanceRulesByRegionAndObject() {
        DesignRules rules;
        rules.clearance = 0.25;
        rules.boardEdgeClearance = 0.4;
        const int top = layerBit(BoardLayer::TopCopper);
        const int bottom = layerBit(BoardLayer::BottomCopper);
        // Without explicit rules the global values form the DEFAULT rule.
        QCOMPARE(effectiveClearanceRules(rules).size(), 1);
        QCOMPARE(clearanceBetween(rules, top, ClearanceObject::Pad, ClearanceObject::Trace), 0.25);
        QCOMPARE(edgeClearance(rules, bottom), 0.4);

        ClearanceRule board;
        board.padPad = 0.3;
        board.padTrace = 0.25;
        board.traceTrace = 0.2;
        board.graphic = 0.5;
        board.edge = 0.3;
        ClearanceRule topRule = board;
        topRule.name = QStringLiteral("TOP");
        topRule.region = RuleRegion::TopCopper;
        topRule.traceTrace = 0.15;
        topRule.edge = 0.6;
        rules.clearanceRules = {board, topRule};
        QVERIFY(validateDesignRules(rules).isEmpty());
        QCOMPARE(clearanceBetween(rules, bottom, ClearanceObject::Pad, ClearanceObject::Pad), 0.3);
        QCOMPARE(clearanceBetween(rules, bottom, ClearanceObject::Trace, ClearanceObject::Pad), 0.25);
        QCOMPARE(clearanceBetween(rules, bottom, ClearanceObject::Trace, ClearanceObject::Trace), 0.2);
        QCOMPARE(clearanceBetween(rules, top, ClearanceObject::Trace, ClearanceObject::Trace), 0.15);
        QCOMPARE(clearanceBetween(rules, top | bottom, ClearanceObject::Trace, ClearanceObject::Trace), 0.2);
        QCOMPARE(clearanceBetween(rules, bottom, ClearanceObject::Graphic, ClearanceObject::Trace), 0.5);
        QCOMPARE(edgeClearance(rules, top), 0.6);
        QCOMPARE(edgeClearance(rules, bottom), 0.3);
        QCOMPARE(largestClearance(rules), 0.5);

        rules.clearanceRules[1].name = board.name;
        QVERIFY(!validateDesignRules(rules).isEmpty());
        rules.clearanceRules[1].name = QStringLiteral("TOP");
        rules.clearanceRules[1].padPad = 0;
        QVERIFY(!validateDesignRules(rules).isEmpty());
    }

    void netClassesAssignPowerSignalAndExplicitNets() {
        const SketchDocument schematic = dcDividerExample();
        DesignRules rules;
        const QHash<QString, QString> assignments = netClassAssignments(rules, schematic);
        QCOMPARE(assignments.value(QStringLiteral("0")), PowerNetClass);
        const auto signal = std::count_if(assignments.cbegin(), assignments.cend(),
                                          [](const QString& c) { return c == SignalNetClass; });
        QCOMPARE(signal, 2);
        QCOMPARE(netClassForNet(rules, schematic, QStringLiteral("0")).traceWidth, 0.635);

        NetClass power = effectiveNetClasses(rules).first();
        NetClass signalClass = effectiveNetClasses(rules).last();
        NetClass fast;
        fast.name = QStringLiteral("FAST");
        fast.traceWidth = 0.2;
        fast.neckWidth = 0.15;
        fast.layers = layerBit(BoardLayer::TopCopper);
        fast.ratsnestColor = QStringLiteral("#ff8800");
        const QString someSignal = std::find_if(assignments.cbegin(), assignments.cend(),
                                                [](const QString& c) { return c == SignalNetClass; }).key();
        fast.nets = {someSignal};
        rules.netClasses = {power, signalClass, fast};
        QVERIFY2(validateDesignRules(rules).isEmpty(), qPrintable(validateDesignRules(rules)));
        QCOMPARE(netClassForNet(rules, schematic, someSignal).name, QStringLiteral("FAST"));
        QCOMPARE(netClassForNet(rules, schematic, QStringLiteral("unknown")).name, SignalNetClass);

        rules.netClasses[1].nets = {someSignal};
        QVERIFY(!validateDesignRules(rules).isEmpty()); // one net in two classes
        rules.netClasses[1].nets.clear();
        rules.netClasses[2].viaDrill = 1.0;
        QVERIFY(!validateDesignRules(rules).isEmpty()); // drill wider than the via
        rules.netClasses[2].viaDrill = 0.4;
        rules.netClasses[2].ratsnestColor = QStringLiteral("not a colour");
        QVERIFY(!validateDesignRules(rules).isEmpty());
    }

    void designRulesJsonRoundTrip() {
        DesignRules rules;
        QJsonObject plain = designRulesToJson(rules);
        QCOMPARE(plain.keys().size(), 5); // optional parts are not written
        ClearanceRule rule;
        rule.name = QStringLiteral("BOTTOM");
        rule.region = RuleRegion::BottomCopper;
        rule.edge = 0.0;
        NetClass netClass;
        netClass.name = QStringLiteral("POWER");
        netClass.traceWidth = 1.0;
        netClass.viaDiameter = 1.2;
        netClass.viaDrill = 0.6;
        netClass.neckWidth = 0.5;
        netClass.layers = layerBit(BoardLayer::BottomCopper);
        netClass.ratsnestHidden = true;
        netClass.nets = {QStringLiteral("VCC"), QStringLiteral("0")};
        DifferentialPair pair{QStringLiteral("USB"), QStringLiteral("D+"), QStringLiteral("D-"), 0.25, 0.15};
        rules.clearanceRules = {ClearanceRule{}, rule};
        rules.netClasses = {netClass};
        rules.differentialPairs = {pair};
        rules.defaults.thermalRelief = false;
        rules.defaults.solderResistGuard = 0.1;
        DesignRules read;
        QVERIFY(designRulesFromJson(designRulesToJson(rules), read).isEmpty());
        QVERIFY(read == rules);

        QJsonObject broken = designRulesToJson(rules);
        QJsonArray list = broken.value(QStringLiteral("clearanceRules")).toArray();
        QJsonObject first = list.first().toObject();
        first[QStringLiteral("region")] = QStringLiteral("inner-1");
        list[0] = first;
        broken[QStringLiteral("clearanceRules")] = list;
        DesignRules ignored;
        QVERIFY(!designRulesFromJson(broken, ignored).isEmpty());
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

    void copperZonesJoinAndShortNets() {
        const SketchDocument schematic = dcDividerExample();
        const SketchDocument placed = transferToBoard(schematic, {outline(80, 60)}).document;
        const auto find = [&placed](const char* label) {
            return *std::find_if(placed.begin(), placed.end(), [label](const SketchItem& i) { return i.label == QLatin1String(label); });
        };
        auto zoneAround = [](const QVector<QPointF>& centres, BoardLayer layer) {
            // Zero-sized QRectFs are null and ignored by united(), so grow a 1.2 mm square per centre.
            QRectF area;
            for (const QPointF& centre : centres) area = area.united(QRectF(centre - QPointF(0.6, 0.6), QSizeF(1.2, 1.2)));
            SketchItem zone;
            zone.kind = SketchItem::Kind::Polyline;
            zone.variant = CopperZoneVariant;
            zone.closed = true;
            zone.layer = layer;
            zone.points = {area.topLeft(), area.topRight(), area.bottomRight(), area.bottomLeft()};
            return zone;
        };
        const auto r1 = itemPads(find("R1"));

        // Over both pads of R1 (different nets): a zone short, not a plain short or clearance error.
        SketchDocument board = placed;
        board.append(zoneAround({r1[0].center, r1[1].center}, BoardLayer::TopCopper));
        CheckReport report = runDesignRuleCheck(schematic, board, DesignRules{});
        QVERIFY2(withRule(report, "drc.zone-short").size() == 1, qPrintable(rules(report).join(QLatin1Char(' '))));
        QCOMPARE(withRule(report, "drc.short").size(), 0);
        QCOMPARE(withRule(report, "drc.zone-unfilled").size(), 1);
        QCOMPARE(withRule(report, "drc.clearance").size(), 0);

        // The same zone on the bottom layer does not touch the top side SMD pads.
        board = placed;
        board.append(zoneAround({r1[0].center, r1[1].center}, BoardLayer::BottomCopper));
        report = runDesignRuleCheck(schematic, board, DesignRules{});
        QCOMPARE(withRule(report, "drc.zone-short").size(), 0);
        QCOMPARE(withRule(report, "drc.zone-unfilled").size(), 1);

        // A zone joining the two pads of the divider midpoint net routes that net.
        const int before = withRule(runDesignRuleCheck(schematic, placed, DesignRules{}), "drc.unrouted").size();
        const QPointF from = r1[1].center;
        const QPointF to = itemPads(find("R2"))[0].center;
        board = placed;
        SketchItem strip = zoneAround({from}, BoardLayer::TopCopper);
        strip.points = {from + QPointF(-0.1, -0.1), to + QPointF(0.1, -0.1), to + QPointF(0.1, 0.1), from + QPointF(-0.1, 0.1)};
        board.append(strip);
        report = runDesignRuleCheck(schematic, board, DesignRules{});
        QCOMPARE(withRule(report, "drc.zone-short").size(), 0);
        QCOMPARE(withRule(report, "drc.unrouted").size(), before - 1);
    }

    void pouredZonesAreCheckedAsTheirFill() {
        const SketchDocument schematic = dcDividerExample();
        const SketchDocument placed = transferToBoard(schematic, {outline(80, 60)}).document;
        const auto r1 = *std::find_if(placed.begin(), placed.end(), [](const SketchItem& i) { return i.label == QLatin1String("R1"); });
        const BoardCopperModel model = buildBoardCopperModel(schematic, placed);
        const auto pad2 = std::find_if(model.conductors.begin(), model.conductors.end(), [&r1](const BoardConductor& c) {
            return c.itemId == r1.id && c.padIndex == 1;
        });
        QVERIFY(pad2 != model.conductors.end() && pad2->net >= 0);
        const QString midpoint = model.netNames.value(pad2->net);

        SketchItem zone;
        zone.kind = SketchItem::Kind::Polyline;
        zone.variant = CopperZoneVariant;
        zone.closed = true;
        zone.layer = BoardLayer::TopCopper;
        zone.points = {{1, 1}, {79, 1}, {79, 59}, {1, 59}};
        const int before = withRule(runDesignRuleCheck(schematic, placed, DesignRules{}), "drc.unrouted").size();

        // Without a net the zone stays a solid, unpoured polygon that shorts everything under it.
        SketchDocument board = placed;
        board.append(zone);
        CheckReport report = runDesignRuleCheck(schematic, board, DesignRules{});
        QCOMPARE(withRule(report, "drc.zone-unfilled").size(), 1);
        QCOMPARE(withRule(report, "drc.zone-short").size(), 1);

        // With the midpoint net the pour keeps clear of the other nets and joins R1.2 to R2.1.
        board.last().net = midpoint;
        report = runDesignRuleCheck(schematic, board, DesignRules{});
        QStringList messages;
        for (const auto& v : report.violations) messages << v.rule + QLatin1Char(':') + v.message;
        QVERIFY2(report.count(CheckSeverity::Error) == 0, qPrintable(messages.join(QLatin1Char('\n'))));
        QCOMPARE(withRule(report, "drc.zone-unfilled").size(), 0);
        QCOMPARE(withRule(report, "drc.unrouted").size(), before - 1);

        // A net with no copper under the zone pours nothing and says so.
        board.last().net = QStringLiteral("NO-SUCH-NET");
        report = runDesignRuleCheck(schematic, board, DesignRules{});
        QCOMPARE(withRule(report, "drc.zone-empty").size(), 1);
        QCOMPARE(report.count(CheckSeverity::Error), 0);
        board.last().net = midpoint;

        // A tight clearance rule still produces a clean pour (the fill follows the rules).
        DesignRules wide;
        wide.clearance = 0.5;
        report = runDesignRuleCheck(schematic, board, wide);
        QCOMPARE(withRule(report, "drc.clearance").size(), 0);
        QCOMPARE(withRule(report, "drc.zone-short").size(), 0);
    }

    void boardCopperModelGroupsAndShapes() {
        // Shapes: gaps and grown outlines.
        const CopperShape capsule{{0, 0}, {10, 0}, 0.5, {}};
        const CopperShape square{{}, {}, 0.0, QPolygonF(QVector<QPointF>{{0, 2}, {1, 2}, {1, 3}, {0, 3}})};
        QVERIFY(qAbs(copperShapeGap(capsule, square) - 1.5) < 1e-9);
        QVERIFY(copperShapePath(capsule).contains(QPointF(10.4, 0)));
        QVERIFY(!copperShapePath(capsule).contains(QPointF(10.6, 0)));
        QVERIFY(copperShapePath(capsule, 0.2).contains(QPointF(10.6, 0)));
        QVERIFY(copperShapePath(square, 0.3).contains(QPointF(0.5, 1.8)));
        QVERIFY(!copperShapePath(square).contains(QPointF(0.5, 1.8)));

        // Model: a track from R1 pad 2 takes the pad's net through its group; near pairs by distance.
        const SketchDocument schematic = dcDividerExample();
        SketchDocument board = transferToBoard(schematic, {outline(80, 60)}).document;
        const auto r1 = *std::find_if(board.begin(), board.end(), [](const SketchItem& i) { return i.label == QLatin1String("R1"); });
        const QPointF pad2 = itemPads(r1)[1].center;
        SketchItem stub = track({pad2, pad2 + QPointF(0, 5)}, 0.25);
        board.append(stub);
        const BoardCopperModel model = buildBoardCopperModel(schematic, board, 0.2);
        QVERIFY(model.netsKnown);
        const auto trackIndex = std::find_if(model.conductors.begin(), model.conductors.end(),
                                             [&stub](const BoardConductor& c) { return c.itemId == stub.id; }) - model.conductors.begin();
        const auto padIndex = std::find_if(model.conductors.begin(), model.conductors.end(), [&r1](const BoardConductor& c) {
                                  return c.itemId == r1.id && c.padIndex == 1;
                              }) - model.conductors.begin();
        QVERIFY(trackIndex < model.conductors.size() && padIndex < model.conductors.size());
        QCOMPARE(model.conductors[trackIndex].net, -1);
        QVERIFY(model.conductors[padIndex].net >= 0);
        QCOMPARE(model.groups[trackIndex], model.groups[padIndex]);
        QCOMPARE(model.groupNets(model.groups[trackIndex]), QVector<int>{model.conductors[padIndex].net});
        QVERIFY(model.placedSources.contains(schematic.first().id));
        QCOMPARE(model.conductors[padIndex].name, QStringLiteral("R1.2"));
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
