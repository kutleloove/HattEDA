#include "hatt/ui/DesignChecks.hpp"
#include "hatt/ui/FreeroutingRunner.hpp"
#include "hatt/ui/ProjectFile.hpp"
#include "hatt/ui/SketchCircuit.hpp"
#include "hatt/ui/SpecctraDsn.hpp"
#include "hatt/ui/SpecctraSes.hpp"

#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>

using namespace hatt::ui;

class FreeroutingRoundTripTests final : public QObject {
    Q_OBJECT

private slots:
    void routesExportedBoardWithInstalledFreerouting();
    void routesBottomCopperOnlyWithInstalledFreerouting();
    void routesProjectFromEnvironment();
};

void FreeroutingRoundTripTests::routesExportedBoardWithInstalledFreerouting() {
    const QString jar = qEnvironmentVariable("HATTEDA_FREEROUTING_JAR");
    if (jar.isEmpty() || !QFileInfo::exists(jar)) {
        QSKIP("Set HATTEDA_FREEROUTING_JAR to run the optional real Freerouting round trip.");
    }

    const SketchDocument schematic = dcDividerExample();
    SketchItem outline;
    outline.kind = SketchItem::Kind::Polyline;
    outline.variant = BoardOutlineVariant;
    outline.closed = true;
    outline.points = {{0, 0}, {80, 0}, {80, 55}, {0, 55}};
    const BoardTransfer transfer = transferToBoard(schematic, {outline});
    QVERIFY2(transfer.errors.isEmpty(), qPrintable(transfer.errors.join(QLatin1Char('\n'))));

    const SpecctraDsnResult exported =
        exportSpecctraDsn(schematic, transfer.document, DesignRules{}, QStringLiteral("round-trip"));
    QVERIFY2(exported.errors.isEmpty(), qPrintable(exported.errors.join(QLatin1Char('\n'))));

    FreeroutingRunner runner;
    QSignalSpy finished(&runner, &FreeroutingRunner::finished);
    FreeroutingRequest request;
    const QString java = qEnvironmentVariable("HATTEDA_FREEROUTING_JAVA");
    if (!java.isEmpty()) request.javaExecutable = java;
    request.jarPath = jar;
    request.dsn = exported.data;
    request.maxPasses = 20;
    request.timeoutMs = 120000;
    runner.start(request);
    QVERIFY2(finished.wait(request.timeoutMs + 5000), "Freerouting did not finish in time");

    const FreeroutingResult routed = qvariant_cast<FreeroutingResult>(finished.first().first());
    QVERIFY2(routed.error.isEmpty(), qPrintable(routed.error + QLatin1Char('\n') + routed.diagnostics));
    const SpecctraSesResult imported = importSpecctraSes(routed.ses);
    QVERIFY2(imported.errors.isEmpty(), qPrintable(imported.errors.join(QLatin1Char('\n'))));

    SketchDocument candidate = transfer.document;
    candidate += imported.routing;
    const CheckReport report = runDesignRuleCheck(schematic, candidate, DesignRules{});
    for (const auto& violation : report.violations) {
        QVERIFY2(violation.severity != CheckSeverity::Error, qPrintable(violation.message));
        QVERIFY2(violation.rule != QStringLiteral("drc.unrouted"), qPrintable(violation.message));
    }
}

void FreeroutingRoundTripTests::routesBottomCopperOnlyWithInstalledFreerouting() {
    const QString jar = qEnvironmentVariable("HATTEDA_FREEROUTING_JAR");
    if (jar.isEmpty() || !QFileInfo::exists(jar)) {
        QSKIP("Set HATTEDA_FREEROUTING_JAR to run the optional single-layer round trip.");
    }
    const SketchDocument schematic = dcDividerExample();
    SketchItem outline;
    outline.kind = SketchItem::Kind::Rectangle;
    outline.layer = BoardLayer::BoardEdge;
    outline.points = {{0, 0}, {100, 70}};
    SketchDocument parts = unplacedBoardParts(schematic, {}).parts;
    for (SketchItem& part : parts) part.variant = QStringLiteral("board.dip8");
    const SketchDocument board = autoPlaceParts({outline}, parts, 1.27, 2.54);
    QCOMPARE(board.size(), 4);

    DesignRules rules;
    rules.netClasses = effectiveNetClasses(rules);
    for (NetClass& netClass : rules.netClasses)
        netClass.layers = layerBit(BoardLayer::BottomCopper);
    const SpecctraDsnResult exported =
        exportSpecctraDsn(schematic, board, rules, QStringLiteral("bottom-only"));
    QVERIFY2(exported.errors.isEmpty(), qPrintable(exported.errors.join(QLatin1Char('\n'))));

    FreeroutingRunner runner;
    QSignalSpy finished(&runner, &FreeroutingRunner::finished);
    FreeroutingRequest request;
    const QString java = qEnvironmentVariable("HATTEDA_FREEROUTING_JAVA");
    if (!java.isEmpty()) request.javaExecutable = java;
    request.jarPath = jar;
    request.dsn = exported.data;
    request.maxPasses = 20;
    request.timeoutMs = 120000;
    runner.start(request);
    QVERIFY2(finished.wait(request.timeoutMs + 5000), "Freerouting did not finish in time");
    const FreeroutingResult routed = qvariant_cast<FreeroutingResult>(finished.first().first());
    QVERIFY2(routed.error.isEmpty(), qPrintable(routed.error + QLatin1Char('\n') + routed.diagnostics));
    const SpecctraSesResult imported = importSpecctraSes(routed.ses);
    QVERIFY2(imported.errors.isEmpty(), qPrintable(imported.errors.join(QLatin1Char('\n'))));
    for (const SketchItem& item : imported.routing) {
        QCOMPARE(item.layer, BoardLayer::BottomCopper);
        QVERIFY(item.kind != SketchItem::Kind::Via);
    }
}

void FreeroutingRoundTripTests::routesProjectFromEnvironment() {
    const QString projectPath = qEnvironmentVariable("HATTEDA_AUTOROUTE_PROJECT");
    const QString jar = qEnvironmentVariable("HATTEDA_FREEROUTING_JAR");
    const QString java = qEnvironmentVariable("HATTEDA_FREEROUTING_JAVA");
    if (projectPath.isEmpty() || jar.isEmpty() || java.isEmpty()) {
        QSKIP("Set HATTEDA_AUTOROUTE_PROJECT, HATTEDA_FREEROUTING_JAR and HATTEDA_FREEROUTING_JAVA.");
    }
    const ProjectLoad loaded = loadProjectFile(projectPath);
    QVERIFY2(loaded.ok(), qPrintable(loaded.error));
    SketchDocument unroutedBoard;
    for (const auto& item : loaded.project.board) {
        if (item.kind != SketchItem::Kind::Wire && item.kind != SketchItem::Kind::Via) {
            unroutedBoard.append(item);
        }
    }
    const SpecctraDsnResult exported = exportSpecctraDsn(
        loaded.project.schematic, unroutedBoard, loaded.project.rules, loaded.project.name);
    QVERIFY2(exported.errors.isEmpty(), qPrintable(exported.errors.join(QLatin1Char('\n'))));

    FreeroutingRunner runner;
    QSignalSpy finished(&runner, &FreeroutingRunner::finished);
    FreeroutingRequest request;
    request.javaExecutable = java;
    request.jarPath = jar;
    request.dsn = exported.data;
    request.maxPasses = 100;
    request.timeoutMs = 120000;
    runner.start(request);
    QVERIFY2(finished.wait(request.timeoutMs + 5000), "Freerouting did not finish in time");
    const FreeroutingResult routed = qvariant_cast<FreeroutingResult>(finished.first().first());
    QVERIFY2(routed.error.isEmpty(), qPrintable(routed.error + QLatin1Char('\n') + routed.diagnostics));
    const SpecctraSesResult imported = importSpecctraSes(routed.ses);
    QVERIFY2(imported.errors.isEmpty(), qPrintable(imported.errors.join(QLatin1Char('\n'))));

    SketchDocument candidate = unroutedBoard;
    candidate += snapSpecctraRoutingToPads(imported.routing, unroutedBoard);
    const CheckReport report =
        runDesignRuleCheck(loaded.project.schematic, candidate, loaded.project.rules);
    for (const auto& violation : report.violations) {
        qInfo().noquote() << violation.rule << violation.message;
        QVERIFY2(violation.severity != CheckSeverity::Error, qPrintable(violation.message));
        QVERIFY2(violation.rule != QStringLiteral("drc.unrouted"), qPrintable(violation.message));
    }
    const BoardGuidance guidance = boardGuidance(loaded.project.schematic, candidate);
    QVERIFY2(guidance.airwires.isEmpty(),
             qPrintable(QStringLiteral("%1 airwire(s) remain").arg(guidance.airwires.size())));
}

QTEST_GUILESS_MAIN(FreeroutingRoundTripTests)
#include "FreeroutingRoundTripTests.moc"
