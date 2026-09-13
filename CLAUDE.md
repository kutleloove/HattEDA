# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

HattEDA is a Qt 6 Widgets (C++20) desktop EDA application: schematic & simulation, PCB/CAD, CAM, and manufacturing automation. The README and much of the project documentation are in Turkish. The authoritative architecture contract is `docs/HattEDA_Teknik_Gereksinimler_ve_Mimari_Spesifikasyon_v0.2.docx` (HATT-SPEC-0001).

The repo is currently a bootstrap vertical slice. The UI shell now contains a working **interim editor** (`DesignCanvas` + `SketchModel`, see ADR-0002) for both workspaces: symbol/footprint placement from a built-in library, wires/tracks, 2D graphics (line, polyline, rectangle, circle, arc, text; board outline and copper zone on the board), selection/move/rotate/duplicate/delete, align/distribute, snapping, measure, zoom/pan, and per-workspace undo/redo. Geometry is floating-point millimetres and undo is snapshot based.

**Not implemented yet:** project file formats (New/Open only set the project title and path and clear both canvases; `hatteda.action.save` is permanently disabled; nothing is read from or written to `.hatt`), a domain model (no fixed-point units, no nets/connectivity — wires are plain geometry), design rule/electrical checks (`hatteda.action.run-checks` is disabled), simulation, CAM/Gerber, and real plugin loading. Planned next tickets: `HATT-002` (IDs, `Result/Error`, logging), `HATT-003` (fixed-point units, geometry primitives), `HATT-004` (command/undo transactions), `HATT-032` (`.hattplug` manifest + compatibility preflight), `HATT-033` (Plugin API + `ContributionRegistry`), `HATT-034` (native Qt plugin loader + sample plugin).

## Build, run, test (Windows, PowerShell, from repo root)

The toolchain is pinned by absolute paths in `CMakePresets.json` (Qt 6.11.2 MinGW 64-bit at `C:\Qt\6.11.2\mingw_64`, MinGW 13.1, Ninja and CMake under `C:\Qt\Tools`). CMake is not assumed to be on PATH.

```powershell
# Configure + build (Debug, tests enabled)
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --preset windows-mingw-debug
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset debug

# Run (Qt and MinGW DLLs must be on PATH)
$env:PATH = "C:\Qt\6.11.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
& '.\build\windows-mingw-debug\apps\hatteda-desktop\hatteda.exe'

# All tests (test preset sets QT_QPA_PLATFORM=offscreen and Qt PATH)
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --preset debug

# A single QtTest function: run the test exe directly with the function name
$env:PATH = "C:\Qt\6.11.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"; $env:QT_QPA_PLATFORM = "offscreen"
& '.\build\windows-mingw-debug\tests\unit\hatt-ui-shell-tests.exe' toolWorkspaceIsOpenedOnce
& '.\build\windows-mingw-debug\tests\unit\hatt-design-canvas-tests.exe' movingSelectionIsOneUndoStep

# Release (BUILD_TESTING=OFF)
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --preset windows-mingw-release
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset release
```

Formatting follows `.clang-format` (LLVM base, 4-space indent, 100 columns, left pointer alignment). Targets compile with `-Wall -Wextra -Wpedantic` (`/W4 /permissive-` on MSVC).

## Layout and architecture

- `apps/hatteda-desktop/` — composition root only (`main.cpp`): sets app/org names, loads translation from `QSettings` key `appearance/language`, applies theme from `appearance/lightTheme`, shows `MainWindow`. Translations (`translations/hatteda_tr.ts`) are compiled via `qt_add_translations` into resources under `:/i18n/`.
- `libs/ui-shell/` — static target `hatt-ui-shell` (alias `HattEDA::UiShell`), namespace `hatt::ui`, public headers under `include/hatt/ui/`. Contains `MainWindow` (all shell chrome), `DesignCanvas` (editor widget), `SketchModel` (interim editor model + symbol library), and `Theme` (dark/light `QPalette` + stylesheet).
- `tests/unit/` — two QtTest executables registered with CTest: `hatt-ui-shell-tests` (`MainWindowTests.cpp`) and `hatt-design-canvas-tests` (`DesignCanvasTests.cpp`). New test files need to be added to `tests/unit/CMakeLists.txt`.
- `docs/adr/` — numbered architecture decision records; `docs/architecture/` — implementation notes; `docs/design-system.md` — color tokens and UI patterns.

### Shell model (`MainWindow`)

- `ApplicationPages` stacked widget: index 0 is `WelcomePage` (no project chrome; the window currently creates no `QDockWidget`s); index 1 is the editor. `activateProject` switches to the editor, resets both canvases with `restore({}, {})` and clears their undo stacks.
- Editor layout (top to bottom): `DocumentBar` (Mergen/Kayra `documentTab` buttons, `CompactProjectTitle`), `CommandBar` (icon-only `command` buttons: undo/redo · zoom-in/zoom-out/fit · rotate/duplicate/delete, plus run-checks), body (`ToolRail` + `ContextPanel` with `ActiveModeLabel`, `ToolPreview`, `ContextHint`, `ObjectSelector` + `EditorSurfaces`), and `AlignmentBar` (snap toggles + align/distribute).
- `EditorSurfaces` stacks `PrimaryWorkspaceHost` and `ToolWorkspaceHost`. Two **persistent primary workspaces** in `PrimaryWorkspaceHost`: Mergen (schematic & simulation, index 0) and Kayra (PCB/CAD/CAM, index 1), each a `DesignCanvas` (objectName `DesignCanvas`). **Tool workspaces** (Gerber viewer, simulation reports, future plugin tools) are closable tabs in `ToolWorkspaceHost`, opened via `openToolWorkspace(stableId, ...)`, which dedupes by stable id (e.g. `hatteda.tool.simulation-diagnostics`) set as the content widget's `objectName`.
- **Tool model:** one exclusive `QActionGroup` (`toolActions_`) holds the seven mode actions `hatteda.tool.select|component|connect|terminal|probe|draw|measure` (shortcuts V, A, W, R, P, D, M). The same `QAction`s back the rail buttons, so rail, menu and shortcut can never disagree. `activateToolMode` rebuilds `ObjectSelector` (symbols of the mode's `SymbolCategory` for the active workspace, or the draw tool list), and `applyObjectSelection` maps the mode + selected row to `DesignCanvas::setTool(CanvasTool, variant)`. The last row per (mode, workspace) is remembered. Probe mode is disabled in Kayra. Canvas Esc with nothing pending emits `selectToolRequested`, which returns to Select mode.
- **Undo:** each `DesignCanvas` owns a `QUndoStack`; `MainWindow` adds both to one `QUndoGroup` and makes the active workspace's stack active in `workspaceChanged` (active stack is `nullptr` while a tool workspace is shown). `hatteda.action.undo`/`redo` come from `QUndoGroup::createUndoAction/createRedoAction` (redo also gets Ctrl+Y). Undo does not change the active tool.
- Other stable action ids: `hatteda.action.delete|duplicate|rotate|select-all|zoom-in|zoom-out|fit|save|run-checks`, `hatteda.align.left|hcenter|right|top|vcenter|bottom|distribute-h|distribute-v`. Canvas actions only run through `editingCanvas()` (editor page shown and a primary workspace, not a tool workspace, is current). Enablement is in `updateEditActions` (align needs ≥ 2 selected, distribute ≥ 3).
- Snap toggles in `AlignmentBar` are `QPushButton`s named `hatteda.snap.<key>` with properties `snap=true` and `snapKey` (`grid`, `objects`, `edges`, `centers`, `diagonal`, `orthogonal`), persisted in `QSettings` under `editor/snap/<key>`. `diagonal` (45°) and `orthogonal` are mutually exclusive. `applySnapSettings` pushes one `SnapSettings` to both canvases.
- Command icons are drawn in code by `makeIcon(kind, color)` from each action's `iconKind` property and regenerated on palette change (`refreshIcons`) so both themes work.
- Tests locate widgets by `objectName` (e.g. `ApplicationPages`, `ToolWorkspaceHost`, `ObjectSelector`) and by dynamic properties (e.g. `rail`); `Theme` styles by the same property/object-name selectors (`command`, `rail`, `snap`, `documentTab`, `quiet`, `#CommandBar`, `#AlignmentBar`, `#DesignCanvas`, ...), and `MainWindow` itself relies on `snapKey`, `iconKind` and the `hatteda.*` action ids. Treat these names as a contract: renaming them breaks tests, styling, and future layout persistence.

### Editor (`DesignCanvas`, `SketchModel`)

- `SketchModel.hpp` is the **interim** editor model (ADR-0002): `Workspace {Schematic, Board}`, `SketchItem` (`Kind`: Symbol, Wire, Line, Polyline, Rectangle, Circle, Arc, Text; `points` in floating-point mm, `variant`, `label`, `quarterTurns`, `closed`), `SketchDocument = QVector<SketchItem>` (selection is a list of indices into it). Board outline and copper zone are `Polyline` items with variant `board-outline` / `copper-zone`.
- Built-in, compiled-in symbol library (`symbolLibrary()`, `findSymbol`, `symbolsFor(workspace, category)`): `SymbolDefinition` with id (`schematic.*` e.g. `schematic.resistor`, `schematic.voltage-probe`; `board.*` footprints e.g. `board.r0603`, `board.soic8`, `board.via`), `SymbolCategory` (Component, Terminal, Probe), designator `prefix`, shapes (copper/hole flags) and pins. Display names translate in context `hatt::ui::SymbolLibrary`. Free geometry helpers: `itemSegments`, `itemAnchors`, `itemBounds`, `arcSamples`, `distanceToSegment`, `nextDesignator`, `translateItem`, `rotateItemQuarterTurn`.
- `DesignCanvas` is a painted `QWidget` (no `QGraphicsScene`). Tool state machine over `CanvasTool` (Select, Symbol, Wire, Line, Polyline, Rectangle, Circle, Arc, Text, Measure): pending points in `pending_`, drags (`Move`, `RubberBand`, `Pan`). Two-point tools accept drag or two clicks; Wire/Polyline add vertices by click and finish with double-click/Enter/right-click, Backspace removes the last vertex; wires end automatically on a pin/pad; Arc is start, end, through-point; Measure never changes the document. Esc cancels in order: pending operation → measurement → selection → request Select tool. Arrow keys nudge by one grid step (Shift ×5). Grid is 2.54 mm (schematic) / 0.635 mm (board).
- Snapping (`SnapSettings`): grid, objects (pins/pads/vertices/corners), edges (nearest point on segments), centres; `diagonal`/`orthogonal` constrain wire/line direction relative to the previous point.
- Every document change goes through `pushEdit(text, newDocument, newSelection)`, which pushes a `DocumentEditCommand` holding full before/after `SketchDocument` + selection snapshots; `redo`/`undo` call `restore(...)`, which replaces document and selection without creating an undo entry. A whole drag-move is one undo step; a click without movement creates none.

## Architecture rules (from README, ADR-0001, architecture notes)

- Mergen/Kayra are marketing names; code uses technical names (`SchematicWorkspace`, `BoardWorkspace`, `ToolWorkspaceHost`) and must not turn marketing names into domain class names.
- UI event handlers carry no domain business logic. Domain libraries must not depend on `hatt-ui-shell` or on Qt UI types.
- Only the host creates docks, toolbars, and menus, with stable object names. Feature/plugin code must not call `QMainWindow::addDockWidget`, `addToolBar`, or mutate menus directly; contributions will go through `ContributionRegistry`.
- Plugins never get mutable domain pointers: reads via snapshots/DTOs, writes via command/transaction services. Plugin UI inherits the app palette and must not restyle the shell.
- `.hatt`, `.hattc`, `.hattplug` are separate, versioned formats.
- `SketchModel` is interim UI-shell state (ADR-0002): do not persist it to a file format, do not expose it to plugins, and do not build domain features (nets, DRC, simulation) on top of it; those wait for HATT-003/HATT-004.
- AI and cloud features must never be required for the app to run.
- No new dependency, persistent format field, or cross-layer dependency without an ADR and tests.
- QML is not a dependency of the v1 shell or plugin SDK.
- UI: user-facing strings go through `tr()` (add Turkish entries to `translations/hatteda_tr.ts`); dark and light themes are both first-class; selection state must pair color with a structural cue (left rail / top border); command icons are code-drawn via `iconKind`/`makeIcon` with a tooltip that includes the shortcut — never ad-hoc Unicode symbols as icons; alignment/distribution commands live only in the collapsible bottom alignment strip.
