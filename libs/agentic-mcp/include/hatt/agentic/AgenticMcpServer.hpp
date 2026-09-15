#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

namespace hatt::agentic {

class AgenticActionGateway;

// Local-only MCP server (ADR-0009): listens on a `QLocalServer` (a named pipe on Windows, a Unix
// domain socket elsewhere), never a public network listener. Frames JSON-RPC 2.0 messages the way
// MCP's stdio transport does (`Content-Length: N\r\n\r\n<json>`). v1 accepts one client connection
// at a time and supports the `initialize`, `tools/list` and `tools/call` methods, exposing four
// tools backed by `AgenticActionGateway`: `list_actions`, `trigger_action`, `get_project_snapshot`
// and `get_last_checks_report`.
//
// The caller owns `gateway` and must keep it alive for as long as the server runs.
class AgenticMcpServer final : public QObject {
    Q_OBJECT

public:
    explicit AgenticMcpServer(AgenticActionGateway& gateway, QObject* parent = nullptr);
    ~AgenticMcpServer() override;

    // Starts listening on `pipeName`. Returns false if the listen call fails (e.g. another
    // instance is already using the name).
    bool start(const QString& pipeName);
    // Disconnects any client and stops listening. Safe to call when not started.
    void stop();
    [[nodiscard]] bool isListening() const;

signals:
    // A client connected (replacing any previous one) or the current client disconnected. Useful
    // for a future "agent connected" status indicator; also lets tests synchronize with the
    // (event-driven) accept handshake instead of guessing a timeout.
    void clientConnected();
    void clientDisconnected();

private slots:
    void handleNewConnection();
    void handleReadyRead();
    void handleDisconnected();

private:
    void processBuffer();
    void dispatch(const QJsonObject& request);
    void sendMessage(const QJsonObject& message);

    AgenticActionGateway& gateway_;
    QLocalServer* server_ = nullptr;
    QLocalSocket* client_ = nullptr;
    QByteArray buffer_;
};

} // namespace hatt::agentic
