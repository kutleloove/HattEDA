#include "hatt/ui/AgenticGateway.hpp"
#include "hatt/ui/MainWindow.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using hatt::ui::MainWindow;
using hatt::ui::MainWindowAgenticGateway;

// Covers ADR-0013 decision 4: actions that would open a blocking confirm/discard/lock dialog
// must never be triggered by an agentic caller. hatt-ui-shell-tests already covers the rest of
// MainWindow; this file stays focused on the gateway's block-list.
class AgenticGatewayTests final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void blockedActionsAreRejectedWithoutTriggeringTheUnderlyingAction();
    void availableActionsNeverIncludeBlockedIds();
    void snapshotAndChecksReportAreEmptyWithoutAProject();

private:
    QTemporaryDir settingsDir_;
};

void AgenticGatewayTests::initTestCase() {
    QVERIFY(settingsDir_.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("HattEDA-Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("hatt-agentic-gateway-tests"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir_.path());
}

void AgenticGatewayTests::blockedActionsAreRejectedWithoutTriggeringTheUnderlyingAction() {
    MainWindow window;
    MainWindowAgenticGateway gateway(&window);

    const QStringList blockedIds = {QStringLiteral("hatteda.action.quit"),
                                     QStringLiteral("hatteda.action.new-project"),
                                     QStringLiteral("hatteda.action.open-project"),
                                     QStringLiteral("hatteda.action.open-recent")};
    for (const QString& id : blockedIds) {
        QVERIFY2(MainWindowAgenticGateway::blockedActionIds().contains(id),
                 qPrintable(QStringLiteral("%1 should be block-listed").arg(id)));
        QAction* target = window.findChild<QAction*>(id);
        QVERIFY2(target != nullptr, qPrintable(id));
        // If the gateway ever fell through to QAction::trigger() for a blocked id, this signal
        // would fire and its slot would show the blocking dialog the agent cannot answer.
        QSignalSpy spy(target, &QAction::triggered);

        QString error;
        QVERIFY(!gateway.triggerAction(id, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(spy.count(), 0);
    }
    // No project was created (New project's dialog never opened).
    QVERIFY(window.projectPath().isEmpty());
}

void AgenticGatewayTests::availableActionsNeverIncludeBlockedIds() {
    MainWindow window;
    MainWindowAgenticGateway gateway(&window);

    const auto actions = gateway.availableActions();
    QVERIFY(!actions.isEmpty());
    for (const auto& action : actions) {
        QVERIFY(!MainWindowAgenticGateway::blockedActionIds().contains(action.first));
        QVERIFY(action.first.startsWith(QStringLiteral("hatteda.action.")));
    }
}

void AgenticGatewayTests::snapshotAndChecksReportAreEmptyWithoutAProject() {
    MainWindow window;
    MainWindowAgenticGateway gateway(&window);

    QVERIFY(gateway.projectSnapshot().isEmpty());
    QVERIFY(gateway.lastChecksReport().isEmpty());
}

QTEST_MAIN(AgenticGatewayTests)
#include "AgenticGatewayTests.moc"
