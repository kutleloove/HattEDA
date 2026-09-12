# ADR 0001 Qt desktop shell

## Status

Accepted for bootstrap.

## Decision

HattEDA uses C++20, Qt 6 Widgets, and a `QMainWindow` desktop shell. Mergen and Kayra are persistent primary workspaces. Temporary and plugin-provided full-screen tools are hosted in a closable tab container. Host-managed `QDockWidget` panels provide project, inspector, and output surfaces.

QML is not a dependency of the v1 shell or plugin SDK. It may be embedded later for isolated presentation needs after a separate decision.

## Consequences

Qt Widgets provides mature menu, toolbar, docking, and native plugin integration. Domain code remains independent of Qt UI types. Plugins will contribute descriptors and providers through a registry instead of receiving the main window.
