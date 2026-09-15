# ADR-0009: Agentic Usage Mode (Embedded Agent and External MCP Interface)

**Status:** Proposed
**Date:** 2026-09-15
**Issue:** #37

## Context

The user wants HattEDA usable by LLM agents in two ways: (1) an embedded, in-app agent/automation
assistant, and (2) an interface external tools (Claude Code, OpenAI Codex, Google Antigravity,
Hermes, etc.) can drive programmatically, most likely a local MCP server.

The architecture already anticipates external contributors, but the pieces it relies on are not
built yet:

- `ContributionRegistry` (README, `docs/architecture/README.md`) is the promised seam for
  external contributions; feature code must not call `addDockWidget`/`addToolBar`/mutate menus
  directly. It does not exist yet (tracked as `HATT-033`).
- The plugin manifest/trust format `.hattplug` and a native plugin loader are `HATT-032`/`HATT-034`,
  also not built.
- A command/transaction bus — the thing plugins are supposed to write through instead of holding
  mutable domain pointers — is `HATT-004`, not built either. Today, edits go straight through
  `DesignCanvas`/`MainWindow` calls and land in a `QUndoStack` as before/after `SketchDocument`
  snapshots (`DocumentEditCommand`, ADR-0002). This is an interim editor-model mechanism, not the
  permanent transaction contract; CLAUDE.md is explicit that the interim model must not be treated
  as HATT-002/003/004 completed.
- Existing rules that any agentic design must keep: plugins never get mutable domain pointers
  (reads via snapshot/DTO, writes via command/transaction services — README); AI/cloud features
  must never be required for the app to run.

The risk this ADR is written to avoid: building the agentic read/write surface directly against
current internal state (canvas internals, `SketchDocument`) would (a) violate the
plugin-isolation rule the project already committed to, and (b) create a second, informally
specified "command API" that later has to be reconciled with the real command/transaction bus
once `HATT-004` lands. Issue #37 is a large decision the director asked to be scoped by ADR before
any code is written.

## Decision

### 1. v1 scope is deliberately thin: read snapshots + dispatch existing named actions

No new mutation surface is invented for v1. An external or embedded agent gets exactly two kinds
of access, both of which already exist in some form:

- **Read:** snapshot/report DTOs that already exist or are natural extensions of ones that do —
  `ProjectData`, `CheckReport` (`DesignChecks.hpp` — already pure functions over snapshots),
  netlist/BOM, current selection, canvas export/screenshot. No new mutable references are ever
  handed out.
- **Write:** trigger the same stable `hatteda.action.*` action ids a human triggers from the UI
  (catalogued in `MainWindow`), nothing finer-grained. This reuses `QAction::trigger()` and the
  enablement gates already in place (e.g. `updateEditActions`, `editingCanvas()`), so an agent
  cannot do anything a human could not do from the current screen state, and no new command bus
  has to be built to ship v1. v1 dispatches `hatteda.action.*` ids only; `hatteda.tool.*`
  mode-switch actions (which change what the object selector and canvas clicks mean, not a
  discrete document edit) are deferred to a follow-up once the read DTOs describe editor
  mode/selection state well enough for an agent to use them meaningfully.

This intentionally excludes fine-grained edits ("move this resistor 2 mm", "add a wire here").
That capability waits for `HATT-004`; see "Out of scope" below.

### 2. Transport: local-only MCP server, off by default

A local MCP server (local named pipe/socket via `QLocalServer`, JSON-RPC 2.0 framed like MCP's
stdio transport with `Content-Length` headers; not a public network listener) exposes the read/
dispatch surface from (1). It is gated by an explicit opt-in `QSettings` key
(`agentic/mcpEnabled`, default off), consistent with "AI/cloud must never be required to run the
app" — the app's core function must be identical whether or not the server is running.

### 3. Embedded agent uses the same surface as external agents

The in-app "embedded agent" is just another client of the read-DTO + action-dispatch interface
from (1), not a privileged shortcut with direct access. One boundary to reason about and audit,
not two. The embedded agent itself is out of scope for the v1 implementation (see below); this
ADR only commits to it being a client of the same local MCP server, out-of-process, once built.

### 4. Security/permission model for v1

- Every write is an action trigger gated by the same `isEnabled()` checks the UI already respects
  — an agent cannot act outside what the current screen state allows a human to do.
- **Block-list (resolved for v1):** actions that quit the app or that open a blocking modal the
  agent cannot answer (save/discard confirmation, project-lock "open anyway", any confirm/warning
  dialog gating New/Open project) are rejected by the gateway before `QAction::trigger()` is
  called, rather than left to stall on a dialog. This answers open question 2 below: yes, some
  actions reachable by a human are block-listed for agent callers in v1. The concrete list is
  implemented in `hatt-ui-shell` (`MainWindowAgenticGateway`), not in the transport-agnostic
  `hatt-agentic-mcp` library, because it is specific to HattEDA's current dialog flows, not a
  general MCP concept.
- No actor/audit log for v1 (open question, listed below).
- No remote/networked access; that is explicitly a separate future ADR if ever proposed.

### 5. Out of scope for this ADR (future work, own ADRs/issues)

- Full plugin API / `ContributionRegistry` (`HATT-033`).
- `.hattplug` manifest, trust/signing model, native loader (`HATT-032`, `HATT-034`).
- Command/transaction bus replacing `QUndoStack` snapshot diffing (`HATT-004`) — once it exists,
  the MCP/agent write surface should be re-plumbed onto it instead of action-id dispatch.
- Fine-grained write DTOs (arbitrary document mutation) beyond triggering named actions.
- Actor/audit logging for agent-originated actions.
- Multi-agent or agent+human concurrent editing and conflict resolution.
- Remote/networked MCP access.
- The embedded, in-app agent itself (its orchestration/LLM client). This ADR only fixes that it
  will be an out-of-process client of the same local MCP server as external agents; building it is
  a separate follow-up issue.

## Open questions for the director/user

1. Is action-id dispatch (vs. waiting for `HATT-004` to do fine-grained edits) an acceptable v1
   scope, given it cannot express "edit this one property" without a matching UI action existing?
2. ~~Should specific actions be block-listed for agent callers even though a human can reach
   them~~ — resolved above: yes, `hatteda.action.quit` and any action that opens a blocking
   confirm/discard/lock dialog are block-listed in v1.
3. Where should the embedded agent's own reasoning/orchestration run — in-process (linking an LLM
   client into `hatteda-desktop`) or out-of-process talking to itself over the same local MCP
   server as external agents? (This ADR assumes out-of-process, for one boundary; flagging it as
   a decision, not a given.)

## Consequences

- Ships a useful, narrow agentic surface without a new persisted project format (the opt-in flag
  is `QSettings`, not `.hatt`, so no ADR-0004 format bump) and without pre-committing to a command
  API that might not match `HATT-004` once it lands.
- Capability is limited until `HATT-004`/`HATT-033` exist: no arbitrary document edits, no true
  plugin-grade contributions.
- Follow-up issues once this ADR is accepted: local MCP server skeleton + read DTOs + action
  dispatch (this issue, #37, v1); embedded agent client of the same server; later, re-plumb onto
  `HATT-004` when it lands.
