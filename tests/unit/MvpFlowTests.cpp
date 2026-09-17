#include "hatt/ui/DesignChecks.hpp"
#include "hatt/ui/GerberExport.hpp"
#include "hatt/ui/ManufacturingExport.hpp"
#include "hatt/ui/ProjectFile.hpp"
#include "hatt/ui/SketchCircuit.hpp"

#include <QtTest>

#include <algorithm>

using namespace hatt::ui;

// End-to-end MVP flow on editor snapshots, without widgets: schematic -> ERC -> netlist -> PCB
// transfer -> routing -> DRC -> CAM files -> project file. Each step uses the same functions the
// host commands call, so a regression anywhere in the chain shows up here.
namespace {

SketchItem outline(double w, double h) {
    SketchItem item;
    item.kind = SketchItem::Kind::Polyline;
    item.variant = BoardOutlineVariant;
    item.closed = true;
    item.points = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    return item;
}

SketchItem track(QVector<QPointF> points, double width) {
    SketchItem item;
    item.kind = SketchItem::Kind::Wire;
    item.layer = BoardLayer::TopCopper;
    item.width = width;
    item.points = std::move(points);
    return item;
}

int errors(const CheckReport& report) { return report.count(CheckSeverity::Error); }

QStringList describe(const CheckReport& report) {
    QStringList lines;
    for (const auto& v : report.violations) lines << v.rule + QStringLiteral(": ") + v.message;
    return lines;
}

} // namespace

class MvpFlowTests final : public QObject {
    Q_OBJECT

private slots:
    void dividerFromSchematicToFabrication() {
        // 1. Schematic and ERC.
        const SketchDocument schematic = dcDividerExample();
        const CheckReport erc = runElectricalRuleCheck(schematic);
        QVERIFY2(errors(erc) == 0, qPrintable(describe(erc).join(QLatin1Char('\n'))));

        // 2. Netlist.
        QStringList netlistErrors;
        const QString netlist = netlistText(schematic, &netlistErrors);
        QVERIFY2(netlistErrors.isEmpty(), qPrintable(netlistErrors.join(QLatin1Char('\n'))));
        QVERIFY(netlist.contains(QStringLiteral("*NETS")));
        QVERIFY(netlist.contains(QStringLiteral("R1.2")) && netlist.contains(QStringLiteral("R2.1")));

        // 3. Transfer to the PCB inside an outline; move the parts off the top edge for routing room.
        BoardTransfer transfer = transferToBoard(schematic, {outline(80, 60)});
        QVERIFY2(transfer.errors.isEmpty(), qPrintable(transfer.errors.join(QLatin1Char('\n'))));
        QCOMPARE(transfer.added, 3);
        SketchDocument board = transfer.document;
        for (auto& item : board) {
            if (item.kind == SketchItem::Kind::Symbol) item.points[0] += QPointF(0, 15);
        }
        CheckReport drc = runDesignRuleCheck(schematic, board, DesignRules{});
        QVERIFY2(errors(drc) == 0, qPrintable(describe(drc).join(QLatin1Char('\n'))));
        QCOMPARE(std::count_if(drc.violations.begin(), drc.violations.end(),
                               [](const CheckViolation& v) { return v.rule == QLatin1String("drc.unrouted"); }),
                 3);
        QCOMPARE(boardGuidance(schematic, board).airwires.size(), 3);

        // 4. Route the three nets with orthogonal tracks on top copper (parts sit in one row).
        auto pad = [&board](const char* label, int index) {
            const auto part = std::find_if(board.begin(), board.end(), [label](const SketchItem& i) {
                return i.kind == SketchItem::Kind::Symbol && i.label == QLatin1String(label);
            });
            return itemPads(*part).value(index).center;
        };
        const QPointF r1a = pad("R1", 0), r1b = pad("R1", 1), r2a = pad("R2", 0), r2b = pad("R2", 1);
        const QPointF v1a = pad("V1", 0), v1b = pad("V1", 1);
        QVERIFY(r1a.x() < r1b.x() && r1b.x() < r2a.x() && r2a.x() < r2b.x() && r2b.x() < v1a.x() && v1a.x() < v1b.x());
        double top = r1a.y(), bottom = r1a.y();
        for (const QPointF& p : {r1b, r2a, r2b, v1a, v1b}) {
            top = std::min(top, p.y());
            bottom = std::max(bottom, p.y());
        }
        // V1+ to R1.1 below the row, R1.2 to R2.1 just above it, R2.2 to V1- further above.
        // Widths follow the default net classes: SIGNAL 0.3048 mm, POWER (ground) 0.635 mm.
        const DesignRules defaults;
        const double signal = netClassForNet(defaults, schematic, QStringLiteral("N1")).traceWidth;
        const double power = netClassForNet(defaults, schematic, QStringLiteral("0")).traceWidth;
        QVERIFY(power > signal);
        board.append(track({r1a, {r1a.x(), bottom + 3}, {v1a.x(), bottom + 3}, v1a}, signal));
        board.append(track({r1b, {r1b.x(), top - 2}, {r2a.x(), top - 2}, r2a}, signal));
        board.append(track({r2b, {r2b.x(), top - 4}, {v1b.x(), top - 4}, v1b}, power));
        drc = runDesignRuleCheck(schematic, board, DesignRules{});
        QVERIFY2(drc.violations.isEmpty(), qPrintable(describe(drc).join(QLatin1Char('\n'))));
        const BoardGuidance guidance = boardGuidance(schematic, board);
        QVERIFY2(guidance.errors.isEmpty(), qPrintable(guidance.errors.join(QLatin1Char('\n'))));
        QVERIFY(guidance.airwires.isEmpty());

        // 5. CAM: copper, silkscreen, outline and the two plated header holes.
        const CamOutput cam = buildCamOutput(board);
        QCOMPARE(cam.layers.size(), 9);
        const auto layer = [&cam](CamLayerKind kind) {
            return *std::find_if(cam.layers.begin(), cam.layers.end(), [kind](const CamLayer& l) { return l.kind == kind; });
        };
        const CamLayer topCopper = layer(CamLayerKind::TopCopper);
        const auto strokes = std::count_if(topCopper.primitives.begin(), topCopper.primitives.end(),
                                           [](const CamPrimitive& p) { return p.kind == CamPrimitive::Kind::Stroke; });
        QVERIFY(strokes >= 3);
        QVERIFY(!layer(CamLayerKind::Outline).primitives.isEmpty());
        QCOMPARE(cam.drills.size(), 2);
        const auto files = camFiles(cam, QStringLiteral("divider"), QStringLiteral("HattEDA test"));
        QCOMPARE(files.size(), 10);
        for (const auto& file : files) {
            QVERIFY2(!file.content.isEmpty(), qPrintable(file.fileName));
            QVERIFY(file.fileName.startsWith(QStringLiteral("divider")));
        }

        // 6. Assembly files: two BOM lines (R1+R2 share value and footprint) and three placements.
        const QVector<BomLine> bom = buildBom(schematic, {});
        QCOMPARE(bom.size(), 2);
        QCOMPARE(bom.first().references, QStringList({QStringLiteral("R1"), QStringLiteral("R2")}));
        const QVector<PlacementLine> placement = buildPlacement(board);
        QCOMPARE(placement.size(), 3);
        for (const auto& line : placement) QVERIFY(line.centre.y() < 0); // CAM coordinates, Y up
        QCOMPARE(bomCsv(bom).count('\n'), 3);
        QCOMPARE(placementCsv(placement).count('\n'), 4);

        // 7. The whole design survives the project file.
        ProjectData project;
        project.name = QStringLiteral("Divider");
        project.schematic = schematic;
        project.board = board;
        project.rules.clearance = 0.25;
        const ProjectLoad load = parseProject(serializeProject(project));
        QVERIFY2(load.ok(), qPrintable(load.error));
        QCOMPARE(load.project.board.size(), board.size());
        QVERIFY(load.project.rules == project.rules);
        const CheckReport reloaded = runDesignRuleCheck(load.project.schematic, load.project.board, load.project.rules);
        QVERIFY2(reloaded.violations.isEmpty(), qPrintable(describe(reloaded).join(QLatin1Char('\n'))));
    }
};

QTEST_GUILESS_MAIN(MvpFlowTests)
#include "MvpFlowTests.moc"
