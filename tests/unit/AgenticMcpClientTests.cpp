#include "hatt/agentic/AgenticActionGateway.hpp"
#include "hatt/agentic/AgenticMcpClient.hpp"
#include "hatt/agentic/AgenticMcpServer.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QtTest>

using hatt::agentic::AgenticActionGateway;
using hatt::agentic::AgenticMcpClient;
using hatt::agentic::AgenticMcpServer;

namespace {

// Same minimal in-memory AgenticActionGateway test double as AgenticMcpTests.cpp, kept local so
// this file exercises a real client-server round trip on its own.
class FakeGateway final : public AgenticActionGateway {
public:
    [[nodiscard]] QVector<QPair<QString, QString>> availableActions() const override {
        return {{QStringLiteral("hatteda.action.zoom-in"), QStringLiteral("Zoom in")},
                {QStringLiteral("hatteda.action.undo"), QStringLiteral("Undo")}};
    }

    bool triggerAction(const QString& id, QString* error) override {
        if (id == QStringLiteral("hatteda.action.zoom-in") ||
            id == QStringLiteral("hatteda.action.undo")) {
            triggered.append(id);
            return true;
        }
        if (error != nullptr) *error = QStringLiteral("Unknown action: %1").arg(id);
        return false;
    }

    [[nodiscard]] QJsonObject projectSnapshot() const override {
        return QJsonObject{{QStringLiteral("name"), QStringLiteral("Demo")}};
    }

    [[nodiscard]] QJsonObject lastChecksReport() const override { return QJsonObject{}; }

    QStringList triggered;
};

QString uniquePipeName() {
    return QStringLiteral("hatteda-agentic-mcp-client-tests-%1-%2")
        .arg(QCoreApplication::applicationPid())
        .arg(QDateTime::currentMSecsSinceEpoch());
}

} // namespace

class AgenticMcpClientTests final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void connectingToAMissingServerFails();
    void initializeReturnsServerInfo();
    void listToolsReturnsTheFourTools();
    void callToolListActionsReturnsGatewayActions();
    void callToolTriggerActionSucceeds();
    void callToolTriggerActionReportsGatewayError();
    void callToolWithUnknownNameReportsProtocolError();

private:
    FakeGateway gateway_;
    AgenticMcpServer* server_ = nullptr;
    QString pipeName_;
};

void AgenticMcpClientTests::init() {
    gateway_ = FakeGateway{};
    pipeName_ = uniquePipeName();
    server_ = new AgenticMcpServer(gateway_, this);
    QVERIFY(server_->start(pipeName_));
}

void AgenticMcpClientTests::cleanup() {
    if (server_ != nullptr) {
        server_->stop();
        delete server_;
        server_ = nullptr;
    }
}

void AgenticMcpClientTests::connectingToAMissingServerFails() {
    AgenticMcpClient client;
    QVERIFY(!client.connectToServer(QStringLiteral("hatteda-agentic-mcp-no-such-server"), 500));
    QVERIFY(!client.lastError().isEmpty());
    QVERIFY(!client.isConnected());
}

void AgenticMcpClientTests::initializeReturnsServerInfo() {
    AgenticMcpClient client;
    QVERIFY(client.connectToServer(pipeName_));
    QVERIFY(client.isConnected());

    const QJsonObject result = client.initialize();
    QVERIFY2(client.lastError().isEmpty(), qPrintable(client.lastError()));
    QCOMPARE(result.value(QStringLiteral("protocolVersion")).toString(),
             QStringLiteral("2024-11-05"));
    QCOMPARE(result.value(QStringLiteral("serverInfo"))
                 .toObject()
                 .value(QStringLiteral("name"))
                 .toString(),
             QStringLiteral("hatteda-agentic-mcp"));
}

void AgenticMcpClientTests::listToolsReturnsTheFourTools() {
    AgenticMcpClient client;
    QVERIFY(client.connectToServer(pipeName_));
    QVERIFY(!client.initialize().isEmpty());

    const QJsonArray tools = client.listTools();
    QVERIFY2(client.lastError().isEmpty(), qPrintable(client.lastError()));
    QCOMPARE(tools.size(), 4);
    QStringList names;
    for (const auto& tool : tools) names << tool.toObject().value(QStringLiteral("name")).toString();
    QVERIFY(names.contains(QStringLiteral("list_actions")));
    QVERIFY(names.contains(QStringLiteral("trigger_action")));
    QVERIFY(names.contains(QStringLiteral("get_project_snapshot")));
    QVERIFY(names.contains(QStringLiteral("get_last_checks_report")));
}

void AgenticMcpClientTests::callToolListActionsReturnsGatewayActions() {
    AgenticMcpClient client;
    QVERIFY(client.connectToServer(pipeName_));
    QVERIFY(!client.initialize().isEmpty());

    const QJsonObject result = client.callTool(QStringLiteral("list_actions"));
    QVERIFY2(client.lastError().isEmpty(), qPrintable(client.lastError()));
    QVERIFY(!result.value(QStringLiteral("isError")).toBool());
    const QString text = result.value(QStringLiteral("content"))
                              .toArray()
                              .first()
                              .toObject()
                              .value(QStringLiteral("text"))
                              .toString();
    QVERIFY(text.contains(QStringLiteral("hatteda.action.zoom-in")));
    QVERIFY(text.contains(QStringLiteral("hatteda.action.undo")));
}

void AgenticMcpClientTests::callToolTriggerActionSucceeds() {
    AgenticMcpClient client;
    QVERIFY(client.connectToServer(pipeName_));
    QVERIFY(!client.initialize().isEmpty());

    const QJsonObject result = client.callTool(
        QStringLiteral("trigger_action"),
        QJsonObject{{QStringLiteral("actionId"), QStringLiteral("hatteda.action.zoom-in")}});
    QVERIFY2(client.lastError().isEmpty(), qPrintable(client.lastError()));
    QVERIFY(!result.value(QStringLiteral("isError")).toBool());
    QCOMPARE(gateway_.triggered, QStringList{QStringLiteral("hatteda.action.zoom-in")});
}

void AgenticMcpClientTests::callToolTriggerActionReportsGatewayError() {
    AgenticMcpClient client;
    QVERIFY(client.connectToServer(pipeName_));
    QVERIFY(!client.initialize().isEmpty());

    const QJsonObject result = client.callTool(
        QStringLiteral("trigger_action"),
        QJsonObject{{QStringLiteral("actionId"), QStringLiteral("hatteda.action.nope")}});
    // The round trip itself succeeded; the failure is reported inside the tool result, not as a
    // transport-level error.
    QVERIFY2(client.lastError().isEmpty(), qPrintable(client.lastError()));
    QVERIFY(result.value(QStringLiteral("isError")).toBool());
    QVERIFY(gateway_.triggered.isEmpty());
    const QString text = result.value(QStringLiteral("content"))
                              .toArray()
                              .first()
                              .toObject()
                              .value(QStringLiteral("text"))
                              .toString();
    QVERIFY(text.contains(QStringLiteral("hatteda.action.nope")));
}

void AgenticMcpClientTests::callToolWithUnknownNameReportsProtocolError() {
    AgenticMcpClient client;
    QVERIFY(client.connectToServer(pipeName_));
    QVERIFY(!client.initialize().isEmpty());

    const QJsonObject result = client.callTool(QStringLiteral("not_a_tool"));
    QVERIFY(result.isEmpty());
    QVERIFY(!client.lastError().isEmpty());
}

QTEST_MAIN(AgenticMcpClientTests)
#include "AgenticMcpClientTests.moc"
