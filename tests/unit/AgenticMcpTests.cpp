#include "hatt/agentic/AgenticActionGateway.hpp"
#include "hatt/agentic/AgenticMcpServer.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QtTest>

using hatt::agentic::AgenticActionGateway;
using hatt::agentic::AgenticMcpServer;

namespace {

// A minimal, in-memory AgenticActionGateway test double: no MainWindow, no block-list, just
// enough to exercise AgenticMcpServer's JSON-RPC dispatch independently of hatt-ui-shell.
class FakeGateway final : public AgenticActionGateway {
public:
    [[nodiscard]] QVector<QPair<QString, QString>> availableActions() const override {
        return {{QStringLiteral("hatteda.action.zoom-in"), QStringLiteral("Zoom in")}};
    }

    bool triggerAction(const QString& id, QString* error) override {
        if (id == QStringLiteral("hatteda.action.zoom-in")) {
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

QByteArray frame(const QJsonObject& message) {
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

// Reads exactly one Content-Length-framed JSON-RPC message, waiting for more data as needed.
//
// Deliberately drives the general Qt event loop (`QCoreApplication::processEvents`) instead of
// `QLocalSocket::waitForReadyRead`: on Windows, a client and server `QLocalSocket` living in the
// same thread (as they do in this test) each wait on their own named-pipe handle, so blocking on
// one does not pump the other's I/O completion -- the server would never get a chance to read the
// request this test just wrote.
QJsonObject readFrame(QLocalSocket* socket) {
    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    int headerEnd = -1;
    while (headerEnd < 0) {
        buffer += socket->readAll();
        headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd >= 0) break;
        if (timer.hasExpired(2000)) return {};
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    const QByteArray header = buffer.left(headerEnd);
    int contentLength = -1;
    for (const QByteArray& line : header.split('\n')) {
        const QByteArray trimmed = line.trimmed();
        if (trimmed.startsWith("Content-Length:")) {
            contentLength = trimmed.mid(QByteArray("Content-Length:").size()).trimmed().toInt();
        }
    }
    const int bodyStart = headerEnd + 4;
    timer.restart();
    while (contentLength >= 0 && buffer.size() < bodyStart + contentLength) {
        buffer += socket->readAll();
        if (buffer.size() >= bodyStart + contentLength) break;
        if (timer.hasExpired(2000)) break;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return QJsonDocument::fromJson(buffer.mid(bodyStart, contentLength)).object();
}

} // namespace

class AgenticMcpTests final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void toolsListDescribesFourTools();
    void triggerActionSucceeds();
    void triggerActionReportsGatewayError();
    void unknownMethodReturnsJsonRpcError();
    void malformedFrameReturnsParseError();

private:
    FakeGateway gateway_;
    AgenticMcpServer* server_ = nullptr;
    QLocalSocket* client_ = nullptr;
};

void AgenticMcpTests::init() {
    gateway_ = FakeGateway{};
    const QString pipeName = QStringLiteral("hatteda-agentic-mcp-tests-%1-%2")
                                  .arg(QCoreApplication::applicationPid())
                                  .arg(QDateTime::currentMSecsSinceEpoch());
    server_ = new AgenticMcpServer(gateway_, this);
    QVERIFY(server_->start(pipeName));
    client_ = new QLocalSocket(this);
    // QLocalSocket's own wait calls do not necessarily pump events for other Qt objects (the
    // server's accept handshake is event-driven), so wait for the server's own signal rather than
    // just the client-side connected state.
    QSignalSpy connectedSpy(server_, &AgenticMcpServer::clientConnected);
    client_->connectToServer(pipeName);
    QVERIFY(client_->waitForConnected(2000));
    QVERIFY(connectedSpy.count() > 0 || connectedSpy.wait(2000));
}

void AgenticMcpTests::cleanup() {
    delete client_;
    client_ = nullptr;
    if (server_ != nullptr) {
        server_->stop();
        delete server_;
        server_ = nullptr;
    }
}

void AgenticMcpTests::toolsListDescribesFourTools() {
    client_->write(frame(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                      {QStringLiteral("id"), 1},
                                      {QStringLiteral("method"), QStringLiteral("tools/list")}}));
    const QJsonObject response = readFrame(client_);
    QCOMPARE(response.value(QStringLiteral("id")).toInt(), 1);
    const QJsonArray tools =
        response.value(QStringLiteral("result")).toObject().value(QStringLiteral("tools")).toArray();
    QCOMPARE(tools.size(), 4);
    QStringList names;
    for (const auto& tool : tools) {
        names << tool.toObject().value(QStringLiteral("name")).toString();
    }
    QVERIFY(names.contains(QStringLiteral("list_actions")));
    QVERIFY(names.contains(QStringLiteral("trigger_action")));
    QVERIFY(names.contains(QStringLiteral("get_project_snapshot")));
    QVERIFY(names.contains(QStringLiteral("get_last_checks_report")));
}

void AgenticMcpTests::triggerActionSucceeds() {
    client_->write(frame(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 2},
        {QStringLiteral("method"), QStringLiteral("tools/call")},
        {QStringLiteral("params"),
         QJsonObject{
             {QStringLiteral("name"), QStringLiteral("trigger_action")},
             {QStringLiteral("arguments"),
              QJsonObject{{QStringLiteral("actionId"), QStringLiteral("hatteda.action.zoom-in")}}}}}}));
    const QJsonObject response = readFrame(client_);
    QCOMPARE(gateway_.triggered, QStringList{QStringLiteral("hatteda.action.zoom-in")});
    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    QVERIFY(!result.value(QStringLiteral("isError")).toBool());
    const QJsonArray content = result.value(QStringLiteral("content")).toArray();
    QCOMPARE(content.size(), 1);
    QCOMPARE(content.first().toObject().value(QStringLiteral("text")).toString(), QStringLiteral("ok"));
}

void AgenticMcpTests::triggerActionReportsGatewayError() {
    client_->write(frame(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), 3},
        {QStringLiteral("method"), QStringLiteral("tools/call")},
        {QStringLiteral("params"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("trigger_action")},
                     {QStringLiteral("arguments"),
                      QJsonObject{{QStringLiteral("actionId"), QStringLiteral("hatteda.action.nope")}}}}}}));
    const QJsonObject response = readFrame(client_);
    QVERIFY(gateway_.triggered.isEmpty());
    const QJsonObject result = response.value(QStringLiteral("result")).toObject();
    QVERIFY(result.value(QStringLiteral("isError")).toBool());
    const QString text = result.value(QStringLiteral("content"))
                              .toArray()
                              .first()
                              .toObject()
                              .value(QStringLiteral("text"))
                              .toString();
    QVERIFY(text.contains(QStringLiteral("hatteda.action.nope")));
}

void AgenticMcpTests::unknownMethodReturnsJsonRpcError() {
    client_->write(frame(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                      {QStringLiteral("id"), 4},
                                      {QStringLiteral("method"), QStringLiteral("not/a/method")}}));
    const QJsonObject response = readFrame(client_);
    QVERIFY(response.contains(QStringLiteral("error")));
    QCOMPARE(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(),
             -32601);
}

void AgenticMcpTests::malformedFrameReturnsParseError() {
    const QByteArray body = "{not valid json}";
    client_->write("Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
    const QJsonObject response = readFrame(client_);
    QVERIFY(response.contains(QStringLiteral("error")));
    QCOMPARE(response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toInt(),
             -32700);
}

QTEST_MAIN(AgenticMcpTests)
#include "AgenticMcpTests.moc"
