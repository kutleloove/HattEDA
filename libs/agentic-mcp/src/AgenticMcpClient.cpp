#include "hatt/agentic/AgenticMcpClient.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>

namespace hatt::agentic {

namespace {

constexpr int kHeaderSeparatorLength = 4; // "\r\n\r\n"

QByteArray frame(const QJsonObject& message) {
    const QByteArray body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    return "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

} // namespace

AgenticMcpClient::AgenticMcpClient(QObject* parent) : QObject(parent) {}

AgenticMcpClient::~AgenticMcpClient() { disconnectFromServer(); }

bool AgenticMcpClient::connectToServer(const QString& pipeName, int timeoutMs) {
    disconnectFromServer();
    lastError_.clear();
    socket_ = new QLocalSocket(this);
    connect(socket_, &QLocalSocket::readyRead, this, &AgenticMcpClient::handleReadyRead);
    connect(socket_, &QLocalSocket::disconnected, this, &AgenticMcpClient::handleDisconnected);
    connect(socket_, &QLocalSocket::errorOccurred, this, &AgenticMcpClient::handleErrorOccurred);
    socket_->connectToServer(pipeName);
    if (!socket_->waitForConnected(timeoutMs)) {
        lastError_ = socket_->errorString();
        socket_->disconnect(this);
        socket_->deleteLater();
        socket_ = nullptr;
        return false;
    }
    emit connected();
    return true;
}

void AgenticMcpClient::disconnectFromServer() {
    if (socket_ != nullptr) {
        socket_->disconnect(this);
        socket_->close();
        socket_->deleteLater();
        socket_ = nullptr;
    }
    buffer_.clear();
    pendingMessages_.clear();
}

bool AgenticMcpClient::isConnected() const {
    return socket_ != nullptr && socket_->state() == QLocalSocket::ConnectedState;
}

QJsonObject AgenticMcpClient::initialize(int timeoutMs) {
    const QJsonObject params{
        {QStringLiteral("protocolVersion"), QStringLiteral("2024-11-05")},
        {QStringLiteral("capabilities"), QJsonObject{}},
        {QStringLiteral("clientInfo"),
         QJsonObject{{QStringLiteral("name"), QStringLiteral("hatteda-embedded-agent")},
                     {QStringLiteral("version"), QStringLiteral("1")}}}};
    return extractResult(sendRequestAndWait(QStringLiteral("initialize"), params, timeoutMs));
}

QJsonArray AgenticMcpClient::listTools(int timeoutMs) {
    const QJsonObject result =
        extractResult(sendRequestAndWait(QStringLiteral("tools/list"), QJsonObject{}, timeoutMs));
    return result.value(QStringLiteral("tools")).toArray();
}

QJsonObject AgenticMcpClient::callTool(const QString& name, const QJsonObject& arguments,
                                       int timeoutMs) {
    const QJsonObject params{{QStringLiteral("name"), name},
                              {QStringLiteral("arguments"), arguments}};
    return extractResult(sendRequestAndWait(QStringLiteral("tools/call"), params, timeoutMs));
}

QString AgenticMcpClient::lastError() const { return lastError_; }

QJsonObject AgenticMcpClient::sendRequestAndWait(const QString& method, const QJsonObject& params,
                                                  int timeoutMs) {
    lastError_.clear();
    if (socket_ == nullptr || socket_->state() != QLocalSocket::ConnectedState) {
        lastError_ = QStringLiteral("Not connected to an agentic MCP server");
        return {};
    }
    const int id = nextId_++;
    QJsonObject request{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                        {QStringLiteral("id"), id},
                        {QStringLiteral("method"), method}};
    if (!params.isEmpty()) request.insert(QStringLiteral("params"), params);
    socket_->write(frame(request));

    QElapsedTimer timer;
    timer.start();
    for (;;) {
        for (int i = 0; i < pendingMessages_.size(); ++i) {
            if (pendingMessages_.at(i).value(QStringLiteral("id")).toInt() == id) {
                const QJsonObject message = pendingMessages_.takeAt(i);
                return message;
            }
        }
        if (socket_ == nullptr || socket_->state() != QLocalSocket::ConnectedState) {
            lastError_ = QStringLiteral("Disconnected while waiting for a response");
            return {};
        }
        if (timer.hasExpired(timeoutMs)) {
            lastError_ = QStringLiteral("Timed out waiting for a response to '%1'").arg(method);
            return {};
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
}

QJsonObject AgenticMcpClient::extractResult(const QJsonObject& response) {
    if (response.isEmpty()) return {}; // lastError_ already set by sendRequestAndWait
    if (response.contains(QStringLiteral("error"))) {
        const QJsonObject error = response.value(QStringLiteral("error")).toObject();
        lastError_ = error.value(QStringLiteral("message")).toString();
        if (lastError_.isEmpty()) lastError_ = QStringLiteral("Server returned a JSON-RPC error");
        return {};
    }
    return response.value(QStringLiteral("result")).toObject();
}

void AgenticMcpClient::handleReadyRead() {
    if (socket_ == nullptr) return;
    buffer_.append(socket_->readAll());
    processBuffer();
}

void AgenticMcpClient::handleDisconnected() { emit disconnected(); }

void AgenticMcpClient::handleErrorOccurred(QLocalSocket::LocalSocketError /*socketError*/) {
    if (socket_ != nullptr) lastError_ = socket_->errorString();
}

void AgenticMcpClient::processBuffer() {
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
            buffer_.clear();
            return;
        }
        const int bodyStart = headerEnd + kHeaderSeparatorLength;
        if (buffer_.size() < bodyStart + contentLength) return; // wait for the rest of the body
        const QByteArray body = buffer_.mid(bodyStart, contentLength);
        buffer_.remove(0, bodyStart + contentLength);
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) continue;
        const QJsonObject message = document.object();
        pendingMessages_.append(message);
        emit messageReceived(message);
    }
}

} // namespace hatt::agentic
