// Demonstrator MCP client (ADR-0009). This program is deliberately NOT the embedded, in-app
// agent described in ADR-0009 decision 3 -- it has no LLM reasoning, no orchestration, and no way
// to discover a running HattEDA instance's server on its own. It only proves the round trip:
// connect to the local agentic MCP server by pipe name, do the MCP `initialize` handshake, call
// the `list_actions` tool, print the result, and exit. Building the real embedded agent (its
// reasoning/orchestration, and how it finds the pipe name of "the" running instance) is separate
// follow-up work; see ADR-0009 "Out of scope" #37.
//
// `MainWindow` currently names its pipe "hatteda-agentic-mcp-<pid>" (only when the user has
// turned on the `agentic/mcpEnabled` setting), so there is no stable, discoverable name to default
// to here -- the caller must pass it explicitly.

#include "hatt/agentic/AgenticMcpClient.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

using hatt::agentic::AgenticMcpClient;

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hatteda-embedded-agent"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Demonstrator MCP client: connects to a running HattEDA instance's local agentic MCP "
        "server, lists available actions, and exits. Not the real embedded agent -- no LLM "
        "reasoning or orchestration (ADR-0009, issue #37)."));
    parser.addHelpOption();
    const QCommandLineOption pipeOption(
        QStringList{QStringLiteral("pipe")},
        QStringLiteral("Local MCP server pipe name (a running instance with "
                        "'agentic/mcpEnabled' on listens on 'hatteda-agentic-mcp-<pid>'). Falls "
                        "back to the HATTEDA_AGENTIC_PIPE environment variable. There is no "
                        "discovery mechanism yet: the name must be known out of band."),
        QStringLiteral("name"));
    parser.addOption(pipeOption);
    parser.process(application);

    QString pipeName = parser.value(pipeOption);
    if (pipeName.isEmpty()) pipeName = qEnvironmentVariable("HATTEDA_AGENTIC_PIPE");
    if (pipeName.isEmpty()) {
        QTextStream(stderr) << "error: no pipe name given. Pass --pipe <name> or set "
                                "HATTEDA_AGENTIC_PIPE. Enable 'agentic/mcpEnabled' in a running "
                                "HattEDA instance first.\n";
        return 1;
    }

    AgenticMcpClient client;
    if (!client.connectToServer(pipeName)) {
        QTextStream(stderr) << "error: could not connect to '" << pipeName
                             << "': " << client.lastError() << "\n";
        return 1;
    }

    const QJsonObject initializeResult = client.initialize();
    if (initializeResult.isEmpty() && !client.lastError().isEmpty()) {
        QTextStream(stderr) << "error: initialize failed: " << client.lastError() << "\n";
        return 1;
    }

    const QJsonObject listActionsResult = client.callTool(QStringLiteral("list_actions"));
    if (listActionsResult.isEmpty() && !client.lastError().isEmpty()) {
        QTextStream(stderr) << "error: list_actions failed: " << client.lastError() << "\n";
        return 1;
    }

    QTextStream out(stdout);
    out << "initialize -> "
        << QString::fromUtf8(QJsonDocument(initializeResult).toJson(QJsonDocument::Compact))
        << "\n";
    out << "list_actions -> "
        << QString::fromUtf8(QJsonDocument(listActionsResult).toJson(QJsonDocument::Compact))
        << "\n";
    return 0;
}
