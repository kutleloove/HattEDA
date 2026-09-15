#include "hatt/agentic/AgenticMcpServer.hpp"
#include "hatt/agentic/AgenticActionGateway.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLocalServer>
#include <QLocalSocket>

namespace hatt::agentic {

namespace {

constexpr int kHeaderSeparatorLength = 4; // "\r\n\r\n"

QJsonObject makeError(const QJsonValue& id, int code, const QString& message) {
    return QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                        {QStringLiteral("id"), id.isUndefined() ? QJsonValue() : id},
                        {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
                                                               {QStringLiteral("message"), message}}}};
}

QJsonObject makeResult(const QJsonValue& id, const QJsonValue& result) {
    return QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                        {QStringLiteral("id"), id.isUndefined() ? QJsonValue() : id},
                        {QStringLiteral("result"), result}};
}

// MCP tool call result: a single text content block, optionally flagged as a tool-level error
// (as opposed to a JSON-RPC protocol error, which uses `makeError`).
QJsonObject textContent(const QString& text, bool isError = false) {
    const QJsonObject block{{QStringLiteral("type"), QStringLiteral("text")},
                             {QStringLiteral("text"), text}};
    QJsonObject result{{QStringLiteral("content"), QJsonArray{block}}};
    if (isError) result.insert(QStringLiteral("isError"), true);
    return result;
}

QJsonObject jsonContent(const QJsonObject& data) {
    return textContent(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact)));
}

QJsonObject toolSchema(const QString& name, const QString& description, const QJsonObject& inputSchema) {
    return QJsonObject{{QStringLiteral("name"), name},
                        {QStringLiteral("description"), description},
                        {QStringLiteral("inputSchema"), inputSchema}};
}

} // namespace

AgenticMcpServer::AgenticMcpServer(AgenticActionGateway& gateway, QObject* parent)
    : QObject(parent), gateway_(gateway) {}

AgenticMcpServer::~AgenticMcpServer() { stop(); }

bool AgenticMcpServer::start(const QString& pipeName) {
    stop();
    // Clear a stale entry a crashed previous instance may have left behind.
    QLocalServer::removeServer(pipeName);
    server_ = new QLocalServer(this);
    connect(server_, &QLocalServer::newConnection, this, &AgenticMcpServer::handleNewConnection);
    if (!server_->listen(pipeName)) {
        delete server_;
        server_ = nullptr;
        return false;
    }
    return true;
}

void AgenticMcpServer::stop() {
    if (client_ != nullptr) {
        client_->disconnect(this);
        client_->close();
        client_->deleteLater();
        client_ = nullptr;
    }
    buffer_.clear();
    if (server_ != nullptr) {
        server_->disconnect(this);
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
}

bool AgenticMcpServer::isListening() const { return server_ != nullptr && server_->isListening(); }

void AgenticMcpServer::handleNewConnection() {
    if (server_ == nullptr) return;
    QLocalSocket* socket = server_->nextPendingConnection();
    if (socket == nullptr) return;
    // v1 serves one client at a time; a new connection replaces whatever was there before.
    if (client_ != nullptr) {
        client_->disconnect(this);
        client_->close();
        client_->deleteLater();
    }
    client_ = socket;
    buffer_.clear();
    connect(client_, &QLocalSocket::readyRead, this, &AgenticMcpServer::handleReadyRead);
    connect(client_, &QLocalSocket::disconnected, this, &AgenticMcpServer::handleDisconnected);
    emit clientConnected();
}

void AgenticMcpServer::handleReadyRead() {
    if (client_ == nullptr) return;
    buffer_.append(client_->readAll());
    processBuffer();
}

void AgenticMcpServer::handleDisconnected() {
    if (client_ != nullptr) {
        client_->deleteLater();
        client_ = nullptr;
    }
    buffer_.clear();
    emit clientDisconnected();
}

void AgenticMcpServer::processBuffer() {
    static const QByteArray kContentLengthPrefix = QByteArrayLiteral("Content-Length:");
    for (;;) {
        const int headerEnd = buffer_.indexOf("\r\n\r\n");
        if (headerEnd < 0) return; // wait for the rest of the header
        const QByteArray header = buffer_.left(headerEnd);
        int contentLength = -1;
        for (const QByteArray& line : header.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.startsWith(kContentLengthPrefix)) {
                bool ok = false;
                const int value = trimmed.mid(kContentLengthPrefix.size()).trimmed().toInt(&ok);
                if (ok) contentLength = value;
            }
        }
        if (contentLength < 0) {
            sendMessage(makeError(QJsonValue(), -32700,
                                   QStringLiteral("Parse error: missing Content-Length header")));
            buffer_.clear();
            return;
        }
        const int bodyStart = headerEnd + kHeaderSeparatorLength;
        if (buffer_.size() < bodyStart + contentLength) return; // wait for the rest of the body
        const QByteArray body = buffer_.mid(bodyStart, contentLength);
        buffer_.remove(0, bodyStart + contentLength);
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            sendMessage(makeError(QJsonValue(), -32700,
                                   QStringLiteral("Parse error: %1").arg(parseError.errorString())));
            continue;
        }
        dispatch(document.object());
    }
}

void AgenticMcpServer::dispatch(const QJsonObject& request) {
    const QJsonValue id = request.value(QStringLiteral("id"));
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonObject params = request.value(QStringLiteral("params")).toObject();

    if (method == QStringLiteral("initialize")) {
        const QJsonObject result{
            {QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05")},
            {QStringLiteral("capabilities"), QJsonObject{{QStringLiteral("tools"), QJsonObject{}}}},
            {QStringLiteral("serverInfo"),
             QJsonObject{{QStringLiteral("name"), QStringLiteral("hatteda-agentic-mcp")},
                         {QStringLiteral("version"), QStringLiteral("1")}}}};
        sendMessage(makeResult(id, result));
        return;
    }

    if (method == QStringLiteral("tools/list")) {
        const QJsonObject emptySchema{{QStringLiteral("type"), QStringLiteral("object")},
                                       {QStringLiteral("properties"), QJsonObject{}}};
        const QJsonObject triggerSchema{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{{QStringLiteral("actionId"),
                          QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("actionId")}}};
        const QJsonArray tools{
            toolSchema(QStringLiteral("list_actions"),
                       QStringLiteral("Lists hatteda.action.* ids currently safe to trigger "
                                      "(enabled, not block-listed)."),
                       emptySchema),
            toolSchema(QStringLiteral("trigger_action"),
                       QStringLiteral("Triggers a hatteda.action.* id as a user click would."),
                       triggerSchema),
            toolSchema(QStringLiteral("get_project_snapshot"),
                       QStringLiteral("Returns the open project as .hatt JSON, or an empty object "
                                      "if none is open."),
                       emptySchema),
            toolSchema(QStringLiteral("get_last_checks_report"),
                       QStringLiteral("Returns the most recent ERC/DRC results, or an empty "
                                      "object if design checks have not run yet."),
                       emptySchema)};
        sendMessage(makeResult(id, QJsonObject{{QStringLiteral("tools"), tools}}));
        return;
    }

    if (method == QStringLiteral("tools/call")) {
        const QString name = params.value(QStringLiteral("name")).toString();
        const QJsonObject arguments = params.value(QStringLiteral("arguments")).toObject();
        if (name == QStringLiteral("list_actions")) {
            QJsonArray items;
            const auto actions = gateway_.availableActions();
            for (const auto& action : actions) {
                items.append(QJsonObject{{QStringLiteral("id"), action.first},
                                          {QStringLiteral("label"), action.second}});
            }
            sendMessage(makeResult(id, jsonContent(QJsonObject{{QStringLiteral("actions"), items}})));
            return;
        }
        if (name == QStringLiteral("trigger_action")) {
            const QString actionId = arguments.value(QStringLiteral("actionId")).toString();
            if (actionId.isEmpty()) {
                sendMessage(makeResult(id, textContent(QStringLiteral("actionId is required"), true)));
                return;
            }
            QString error;
            const bool ok = gateway_.triggerAction(actionId, &error);
            sendMessage(makeResult(id, textContent(ok ? QStringLiteral("ok") : error, !ok)));
            return;
        }
        if (name == QStringLiteral("get_project_snapshot")) {
            sendMessage(makeResult(id, jsonContent(gateway_.projectSnapshot())));
            return;
        }
        if (name == QStringLiteral("get_last_checks_report")) {
            sendMessage(makeResult(id, jsonContent(gateway_.lastChecksReport())));
            return;
        }
        sendMessage(makeError(id, -32602, QStringLiteral("Unknown tool: %1").arg(name)));
        return;
    }

    sendMessage(makeError(id, -32601, QStringLiteral("Method not found: %1").arg(method)));
}

void AgenticMcpServer::sendMessage(const QJsonObject& message) {
    if (client_ == nullptr) return;
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    const QByteArray framed =
        "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    client_->write(framed);
}

} // namespace hatt::agentic
