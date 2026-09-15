#include "hatt/ui/AgenticGateway.hpp"
#include "hatt/ui/MainWindow.hpp"
#include "hatt/ui/ProjectFile.hpp"

#include <QAction>
#include <QJsonArray>
#include <QJsonDocument>

namespace hatt::ui {

const QSet<QString>& MainWindowAgenticGateway::blockedActionIds() {
    static const QSet<QString> blocked{
        QStringLiteral("hatteda.action.quit"),
        QStringLiteral("hatteda.action.new-project"),
        QStringLiteral("hatteda.action.open-project"),
        QStringLiteral("hatteda.action.open-recent"),
    };
    return blocked;
}

MainWindowAgenticGateway::MainWindowAgenticGateway(MainWindow* window) : window_(window) {}

QVector<QPair<QString, QString>> MainWindowAgenticGateway::availableActions() const {
    QVector<QPair<QString, QString>> result;
    if (window_ == nullptr) return result;
    const auto& blocked = blockedActionIds();
    for (QAction* action : window_->findChildren<QAction*>()) {
        const QString id = action->objectName();
        if (!id.startsWith(QStringLiteral("hatteda.action."))) continue;
        if (blocked.contains(id)) continue;
        if (!action->isEnabled()) continue;
        result.append({id, action->text()});
    }
    return result;
}

bool MainWindowAgenticGateway::triggerAction(const QString& id, QString* error) {
    if (window_ == nullptr) {
        if (error != nullptr) *error = QStringLiteral("No window to act on.");
        return false;
    }
    if (blockedActionIds().contains(id)) {
        if (error != nullptr) {
            *error = QStringLiteral(
                          "Action '%1' is blocked for agentic callers: it can open a blocking "
                          "dialog an unattended caller cannot answer (ADR-0013).")
                         .arg(id);
        }
        return false;
    }
    if (!id.startsWith(QStringLiteral("hatteda.action."))) {
        if (error != nullptr) *error = QStringLiteral("Not a triggerable action id: %1").arg(id);
        return false;
    }
    QAction* action = window_->findChild<QAction*>(id);
    if (action == nullptr) {
        if (error != nullptr) *error = QStringLiteral("Unknown action id: %1").arg(id);
        return false;
    }
    if (!action->isEnabled()) {
        if (error != nullptr) *error = QStringLiteral("Action '%1' is currently disabled.").arg(id);
        return false;
    }
    action->trigger();
    return true;
}

QJsonObject MainWindowAgenticGateway::projectSnapshot() const {
    if (window_ == nullptr || window_->projectPath().isEmpty()) return {};
    const QByteArray bytes = serializeProject(window_->currentProjectData());
    return QJsonDocument::fromJson(bytes).object();
}

QJsonObject MainWindowAgenticGateway::lastChecksReport() const {
    if (window_ == nullptr || !window_->hasDesignChecksReport()) return {};
    QJsonArray violations;
    for (const CheckViolation& violation : window_->lastCheckViolations()) {
        violations.append(QJsonObject{
            {QStringLiteral("severity"), violation.severity == CheckSeverity::Error
                                              ? QStringLiteral("error")
                                              : QStringLiteral("warning")},
            {QStringLiteral("workspace"), violation.workspace == Workspace::Schematic
                                               ? QStringLiteral("schematic")
                                               : QStringLiteral("board")},
            {QStringLiteral("rule"), violation.rule},
            {QStringLiteral("message"), violation.message},
        });
    }
    return QJsonObject{{QStringLiteral("violations"), violations}};
}

} // namespace hatt::ui
