#pragma once

#include <QJsonObject>
#include <QPair>
#include <QString>
#include <QVector>

namespace hatt::agentic {

// Read/write seam an agent (embedded or external, via AgenticMcpServer) uses to drive HattEDA
// (ADR-0013). v1 only exposes what a human can already do from the UI: read-only snapshots and
// triggering existing stable `hatteda.action.*` ids, nothing finer-grained. Implementations own
// any block-list of actions that are unsafe to trigger unattended (e.g. ones that open a blocking
// confirm/discard/lock dialog an agent cannot answer); this interface itself is host-agnostic and
// has no HattEDA-specific policy.
class AgenticActionGateway {
public:
    virtual ~AgenticActionGateway() = default;

    // Actions currently safe to trigger: already enabled and not block-listed. Pairs of
    // (stable action id, human-readable label).
    [[nodiscard]] virtual QVector<QPair<QString, QString>> availableActions() const = 0;

    // Triggers the action `id` as if a user had clicked it. Returns false and, when `error` is
    // non-null, sets `*error` to a human-readable reason if the id is unknown, disabled, or
    // block-listed.
    virtual bool triggerAction(const QString& id, QString* error) = 0;

    // The open project, serialized the same way `.hatt` files are. An empty object while no
    // project is open.
    [[nodiscard]] virtual QJsonObject projectSnapshot() const = 0;

    // The most recent electrical/design rule check results. An empty object if design checks
    // have not run yet in this session (v1 limitation, ADR-0013).
    [[nodiscard]] virtual QJsonObject lastChecksReport() const = 0;
};

} // namespace hatt::agentic
