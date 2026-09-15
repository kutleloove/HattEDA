#pragma once

#include "hatt/agentic/AgenticActionGateway.hpp"

#include <QSet>

namespace hatt::ui {

class MainWindow;

// `hatt-ui-shell`'s `AgenticActionGateway` implementation (ADR-0013): looks `hatteda.action.*`
// ids up on the given `MainWindow` and triggers them like a click would, rejects a block-list of
// actions that would open a blocking confirm/discard/lock dialog an agent cannot answer, and
// serializes the open project / last design-checks report to JSON.
class MainWindowAgenticGateway final : public hatt::agentic::AgenticActionGateway {
public:
    // `window` must outlive this gateway.
    explicit MainWindowAgenticGateway(MainWindow* window);

    [[nodiscard]] QVector<QPair<QString, QString>> availableActions() const override;
    bool triggerAction(const QString& id, QString* error) override;
    [[nodiscard]] QJsonObject projectSnapshot() const override;
    [[nodiscard]] QJsonObject lastChecksReport() const override;

    // Action ids never triggered through the agentic surface, even when enabled (ADR-0013
    // decision 4): quitting the app, and New/Open project, which both route through
    // `MainWindow::maybeSaveChanges` (a blocking Save/Discard/Cancel dialog once the project has
    // unsaved changes) and Open project additionally through `ProjectGuard::confirmLock` (a
    // blocking "Open anyway" dialog when another window already holds the project's lock file) --
    // none of which an unattended agent caller can answer.
    [[nodiscard]] static const QSet<QString>& blockedActionIds();

private:
    MainWindow* window_ = nullptr;
};

} // namespace hatt::ui
