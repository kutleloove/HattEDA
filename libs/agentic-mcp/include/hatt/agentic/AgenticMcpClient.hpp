#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QVector>

namespace hatt::agentic {

// Client side of the local MCP server (ADR-0009): connects a `QLocalSocket` to the named pipe an
// `AgenticMcpServer` is listening on and speaks the same Content-Length-framed JSON-RPC 2.0. Used
// by the embedded agent demonstrator (`apps/hatteda-embedded-agent`) and by tests that exercise a
// real client-server round trip; an external MCP-speaking agent (Claude Code, etc.) would use its
// own MCP client instead of this class.
//
// The blocking-style methods below (`connectToServer`, `initialize`, `listTools`, `callTool`)
// pump `QCoreApplication::processEvents` internally rather than using `QLocalSocket::waitForReadyRead`,
// because on Windows a client and server `QLocalSocket` living in the same thread (as in the unit
// tests) each wait on their own named-pipe I/O completion, so blocking on one does not give the
// other a chance to process its side (see `AgenticMcpTests.cpp`'s `readFrame` helper, which this
// mirrors). This means these methods are safe to call from a plain `QCoreApplication` event loop
// or from a test, but must not be called from a non-Qt thread with no event loop at all.
//
// For callers that want to drive the protocol asynchronously instead, `messageReceived` emits
// every parsed JSON-RPC message (request-response matching is then the caller's job).
class AgenticMcpClient final : public QObject {
    Q_OBJECT

public:
    static constexpr int kDefaultTimeoutMs = 3000;

    explicit AgenticMcpClient(QObject* parent = nullptr);
    ~AgenticMcpClient() override;

    // Connects to the local MCP server listening on `pipeName` (the name `AgenticMcpServer::start`
    // was given, e.g. "hatteda-agentic-mcp-<pid>"). Returns false with `lastError()` set on
    // failure (no such server, timeout, ...). Any previous connection is closed first.
    bool connectToServer(const QString& pipeName, int timeoutMs = kDefaultTimeoutMs);
    // Closes the connection, if any. Safe to call when not connected.
    void disconnectFromServer();
    [[nodiscard]] bool isConnected() const;

    // MCP `initialize` handshake. Returns the server's result object (protocolVersion,
    // capabilities, serverInfo), or an empty object with `lastError()` set on failure.
    QJsonObject initialize(int timeoutMs = kDefaultTimeoutMs);

    // `tools/list`. Returns the tool descriptors, or an empty array with `lastError()` set on
    // failure.
    QJsonArray listTools(int timeoutMs = kDefaultTimeoutMs);

    // `tools/call`. Returns the tool result object (`{content: [...], isError?: bool}`) on a
    // successful round trip, even when the tool itself reports `isError: true` -- that is a
    // tool-level failure the caller should inspect, not a transport/protocol failure. Returns an
    // empty object with `lastError()` set when the round trip itself fails (not connected,
    // timeout, or a JSON-RPC-level error such as an unknown tool name).
    QJsonObject callTool(const QString& name, const QJsonObject& arguments = {},
                         int timeoutMs = kDefaultTimeoutMs);

    // Reason the last operation failed, if any. Cleared at the start of each call above.
    [[nodiscard]] QString lastError() const;

signals:
    void connected();
    void disconnected();
    // Emitted for every complete JSON-RPC message parsed off the wire, in addition to it being
    // consumed by whichever blocking call (if any) is waiting for it.
    void messageReceived(const QJsonObject& message);

private slots:
    void handleReadyRead();
    void handleDisconnected();
    void handleErrorOccurred(QLocalSocket::LocalSocketError socketError);

private:
    // Sends a JSON-RPC request with a fresh id and pumps the event loop until a message with a
    // matching id arrives or `timeoutMs` elapses. Returns the raw JSON-RPC response object (which
    // may contain "error" instead of "result"), or an empty object with `lastError()` set.
    QJsonObject sendRequestAndWait(const QString& method, const QJsonObject& params, int timeoutMs);
    // Extracts `result` from a JSON-RPC response, or sets `lastError()` from `error`/an empty
    // response and returns an empty object.
    QJsonObject extractResult(const QJsonObject& response);
    void processBuffer();

    QLocalSocket* socket_ = nullptr;
    QByteArray buffer_;
    QVector<QJsonObject> pendingMessages_;
    int nextId_ = 1;
    QString lastError_;
};

} // namespace hatt::agentic
