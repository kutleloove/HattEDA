# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

HattEDA is a Qt 6 Widgets (C++20) desktop EDA application: schematic & simulation, PCB/CAD, CAM, and manufacturing automation. The README and much of the project documentation are in Turkish. The authoritative architecture contract is `docs/HattEDA_Teknik_Gereksinimler_ve_Mimari_Spesifikasyon_v0.2.docx` (HATT-SPEC-0001).

The repo has an interim schematic/PCB editor with snapshot undo, right-click cancellation/context properties, netlist calculation, PCB transfer/ratsnest, and a bounded DC operating-point solver (resistors, DC voltage sources, capacitors open, inductors short) with a live Start/Stop simulation mode (`hatteda.action.simulation-start|stop`, F12/Shift+F12) that shows voltage probe readings as `DesignCanvas::setAnnotations` overlays. Read `docs/architecture/circuit-workflow.md` and ADR-0003 before extending these features. `libs/electrical` is standard C++20 with no Qt dependency; `SketchCircuit` adapts UI snapshots and `CircuitWorkflow` coordinates host actions/reports. Projects persist in the interim `.hatt` JSON file (ADR-0004, `ProjectFile.hpp`: `serializeProject`/`parseProject`, `saveProjectFile` via `QSaveFile`, format version 1, readers reject newer versions); `MainWindow` owns New/Open/Save (`hatteda.action.save`)/Save as (`hatteda.action.save-as`), marks the window modified from the undo stacks' clean state and asks before discarding changes; tests redirect new projects with the `projects/location` setting. Project safety (ADR-0005, `ProjectSafety.hpp`, `ProjectGuard` reached via `MainWindow::projectGuard()`) keeps sidecars next to the project: `<name>.hatt.autosave` (recovery copy every `projects/autosaveMinutes`, default 2, 0 = off; offered on open as Restore / Open saved version / Cancel), `<name>.hatt.bak` (previous file, one generation) and `<name>.hatt.lock` (`QLockFile`; a second window gets Open anyway / Cancel, so tests that open one project in two windows must answer `hatteda.lock.open-anyway`). The guard always saves the `ProjectData` from `MainWindow::currentProjectData`. Any change to stored fields needs an ADR-0004 update and, if it changes existing meaning, a `formatVersion` bump. Full ERC/DRC, AC/transient/nonlinear simulation and real plugin loading are **not implemented yet**. HATT-002/003/004 remain the permanent identity/unit/transaction groundwork; do not treat the interim model as those completed contracts.

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

QtTest output is not captured by CTest on Windows (`--output-on-failure` shows nothing for QtTest executables), so `tests/unit/CMakeLists.txt` registers them via `hatt_add_qt_test(name)`, which passes `-o <build>/test-logs/<name>.txt,txt`; read those files when a test fails. Register new QtTest executables with `hatt_add_qt_test`.

Formatting follows `.clang-format` (LLVM base, 4-space indent, 100 columns, left pointer alignment). Targets compile with `-Wall -Wextra -Wpedantic` (`/W4 /permissive-` on MSVC).

### CI

`.github/workflows/ci.yml` (GitHub Actions, `windows-latest`) runs on pushes to `main`, `feature/**`, `work/**`, on PRs to `main`, and manually. It installs Qt 6.11.2 `win64_mingw` plus `tools_mingw1310` and `tools_ninja` with `jurplel/install-qt-action` (cached; aqtinstall is installed from a pinned git commit because the 3.3.0 release cannot resolve Qt 6.11), then configures `ci-mingw-debug` with `-DHATTEDA_WERROR=ON`, builds `ci-debug`, runs `ctest --preset ci-debug`, uploads `build/ci-mingw-debug/test-logs` on failure, and builds `ci-mingw-release`/`ci-release`. Action versions are pinned by commit SHA. Decision record: ADR-0005.

- CI presets inherit hidden `ci-mingw-base` and take the toolchain from environment variables instead of absolute paths: `QT_ROOT_DIR` (Qt kit, e.g. `C:\Qt\6.11.2\mingw_64`) and `HATTEDA_MINGW_DIR` (e.g. `C:\Qt\Tools\mingw1310_64`); `ninja` must be on PATH. The `windows-mingw-*` presets and `debug`/`release` build/test presets are the local ones and must keep working.
- `HATTEDA_WERROR` (CMake option, default OFF) sets `CMAKE_COMPILE_WARNING_AS_ERROR`. CI enables it, so new code must build warning-free with MinGW in Debug and Release.

```powershell
$env:QT_ROOT_DIR = 'C:\Qt\6.11.2\mingw_64'; $env:HATTEDA_MINGW_DIR = 'C:\Qt\Tools\mingw1310_64'; $env:PATH = "C:\Qt\Tools\Ninja;$env:PATH"
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --preset ci-mingw-debug -DHATTEDA_WERROR=ON
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset ci-debug
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --preset ci-debug
```

## Layout and architecture

- `apps/hatteda-desktop/` — composition root only (`main.cpp`): sets app/org names, loads translation from `QSettings` key `appearance/language`, applies theme from `appearance/lightTheme`, shows `MainWindow`. Translations (`translations/hatteda_tr.ts`) are compiled via `qt_add_translations` into resources under `:/i18n/`.
- `libs/ui-shell/` — static target `hatt-ui-shell` (alias `HattEDA::UiShell`), namespace `hatt::ui`, public headers under `include/hatt/ui/`. Contains `MainWindow` (all shell chrome), `DesignCanvas` (editor widget), `SketchModel` (interim editor model + symbol library), and `Theme` (dark/light `QPalette` + stylesheet).
- `tests/unit/` — QtTest executables registered with CTest, including `hatt-ui-shell-tests` (`MainWindowTests.cpp`), `hatt-design-canvas-tests` (`DesignCanvasTests.cpp`), `hatt-project-file-tests` and `hatt-project-safety-tests` (`ProjectSafetyTests.cpp`; drives dialogs with a polling `onNextModal` helper). New test files need to be added to `tests/unit/CMakeLists.txt`.
- `docs/adr/` — numbered architecture decision records; `docs/architecture/` — implementation notes; `docs/design-system.md` — color tokens and UI patterns.

### Shell model (`MainWindow`)

- `ApplicationPages` stacked widget: index 0 is `WelcomePage` (no project chrome; the window currently creates no `QDockWidget`s); index 1 is the editor. `activateProject` switches to the editor, resets both canvases with `restore({}, {})` and clears their undo stacks.
- Editor layout (top to bottom): `DocumentBar` (Mergen/Kayra `documentTab` buttons, `CompactProjectTitle`), `CommandBar` (icon-only `command` buttons: undo/redo · zoom-in/zoom-out/fit · rotate/duplicate/delete, plus run-checks), body (`ToolRail` + `ContextPanel` with `ActiveModeLabel`, `ToolPreview`, `ContextHint`, `ObjectSelector` + `EditorSurfaces`), and `AlignmentBar` (snap toggles + align/distribute).
- `EditorSurfaces` stacks `PrimaryWorkspaceHost` and `ToolWorkspaceHost`. Two **persistent primary workspaces** in `PrimaryWorkspaceHost`: Mergen (schematic & simulation, index 0) and Kayra (PCB/CAD/CAM, index 1), each a `DesignCanvas` (objectName `DesignCanvas`). **Tool workspaces** (Gerber viewer, simulation reports, future plugin tools) are closable tabs in `ToolWorkspaceHost`, opened via `openToolWorkspace(stableId, ...)`, which dedupes by stable id (e.g. `hatteda.tool.simulation-diagnostics`) set as the content widget's `objectName`.
- **Tool model:** one exclusive `QActionGroup` (`toolActions_`) holds the seven mode actions `hatteda.tool.select|component|connect|terminal|probe|draw|measure` (shortcuts V, A, W, R, P, D, M). The same `QAction`s back the rail buttons, so rail, menu and shortcut can never disagree. `activateToolMode` rebuilds `ObjectSelector` (component mode, Proteus style: schematic shows the project pick list `MainWindow::projectDevices()` = `projectDeviceList(ProjectLibrary, schematic)` with `DeviceBar` buttons `hatteda.devices.pick` (`PickDevicesDialog`: `DeviceSearch`, `DeviceResults`) / `hatteda.devices.remove`, persisted as `.hatt` `library.devices`; the board shows `unplacedBoardParts` rows placed via `DesignCanvas::setPlacementTemplate`, refreshed on `documentChanged` by `refreshComponentList`. Terminal/probe modes list symbols of the mode's `SymbolCategory`; draw mode the draw tool list), and `applyObjectSelection` maps the mode + selected row to `DesignCanvas::setTool(CanvasTool, variant)`. The last row per (mode, workspace) is remembered. Probe mode is disabled in Kayra. Canvas Esc with nothing pending emits `selectToolRequested`, which returns to Select mode.
- **Undo:** each `DesignCanvas` owns a `QUndoStack`; `MainWindow` adds both to one `QUndoGroup` and makes the active workspace's stack active in `workspaceChanged` (active stack is `nullptr` while a tool workspace is shown). `hatteda.action.undo`/`redo` come from `QUndoGroup::createUndoAction/createRedoAction` (redo also gets Ctrl+Y). Undo does not change the active tool.
- Other stable action ids: `hatteda.action.delete|duplicate|rotate|select-all|zoom-in|zoom-out|fit|save|quit|run-checks`, `hatteda.align.left|hcenter|right|top|vcenter|bottom|distribute-h|distribute-v`. Canvas actions only run through `editingCanvas()` (editor page shown and a primary workspace, not a tool workspace, is current). Enablement is in `updateEditActions` (align needs ≥ 2 selected, distribute ≥ 3).
- Snap toggles in `AlignmentBar` are `QPushButton`s named `hatteda.snap.<key>` with properties `snap=true` and `snapKey` (`grid`, `objects`, `edges`, `centers`, `guides`, `diagonal`, `orthogonal`), persisted in `QSettings` under `editor/snap/<key>`. `diagonal` (45°) and `orthogonal` are mutually exclusive. The grid step level (Proteus style `hatteda.grid.step-1..4` = Ctrl+F1/F2/F3/F4, `GridStepButton` menu) is persisted as `editor/snap/gridLevel`. Display units (`Units.hpp`, document stays in mm): schematic canvases show mil, board canvases the preference `editor/units/board` (`mm`/`in`) from the `hatteda.units.board-mm|board-in` actions (View › PCB units); `MainWindow::applyLengthUnits` pushes it via `DesignCanvas::setLengthUnit`. `DesignCanvas::activeSpacings` gives the labelled neighbour gaps drawn while moving or placing (`equal` flags matching gaps; `spacingShift` snaps to equal spacing when guides are on). `hatteda.action.array` opens `ArrayDialog` (`ArrayRows`, `ArrayColumns`, `ArrayPitchX`, `ArrayPitchY`) and calls `DesignCanvas::createArray`. `applySnapSettings` pushes one `SnapSettings` to both canvases.
- Command icons are drawn in code by `makeIcon(kind, color)` from each action's `iconKind` property and regenerated on palette change (`refreshIcons`) so both themes work.
- Tests locate widgets by `objectName` (e.g. `ApplicationPages`, `ToolWorkspaceHost`, `ObjectSelector`) and by dynamic properties (e.g. `rail`); `Theme` styles by the same property/object-name selectors (`command`, `rail`, `snap`, `documentTab`, `quiet`, `#CommandBar`, `#AlignmentBar`, `#DesignCanvas`, ...), and `MainWindow` itself relies on `snapKey`, `iconKind` and the `hatteda.*` action ids. Treat these names as a contract: renaming them breaks tests, styling, and future layout persistence.

### Editor (`DesignCanvas`, `SketchModel`)

- `SketchModel.hpp` is the **interim** editor model (ADR-0002): `Workspace {Schematic, Board}`, `SketchItem` (`Kind`: Symbol, Wire, Line, Polyline, Rectangle, Circle, Arc, Text; `points` in floating-point mm, `variant`, `label`, `quarterTurns`, `closed`), `SketchDocument = QVector<SketchItem>` (selection is a list of indices into it). Board outline and copper zone are `Polyline` items with variant `board-outline` / `copper-zone`.
- Built-in, compiled-in symbol library (`symbolLibrary()`, `findSymbol`, `symbolsFor(workspace, category)`): `SymbolDefinition` with id (`schematic.*` e.g. `schematic.resistor`, `schematic.voltage-probe`; `board.*` footprints e.g. `board.r0603`, `board.soic8`, `board.via`), `SymbolCategory` (Component, Terminal, Probe), designator `prefix`, shapes (copper/hole flags) and pins. Display names translate in context `hatt::ui::SymbolLibrary`. Free geometry helpers: `itemSegments`, `itemAnchors`, `itemBounds`, `arcSamples`, `distanceToSegment`, `nextDesignator`, `translateItem`, `rotateItemQuarterTurn`.
- `DesignCanvas` is a painted `QWidget` (no `QGraphicsScene`). Tool state machine over `CanvasTool` (Select, Symbol, Wire, Line, Polyline, Rectangle, Circle, Arc, Text, Measure): pending points in `pending_`, drags (`Move`, `RubberBand`, `Pan`). Two-point tools accept drag or two clicks; Wire/Polyline add vertices by click and finish with double-click/Enter; right-click cancels and returns to Select, Backspace removes the last vertex; wires end automatically on a pin/pad; a schematic wire corner placed on another wire splits the new wire there (`splitPathAtWires`) so it forms a T join, and joins are painted as junction dots from `schematicJunctions` (plain crossings get none); Arc is start, end, through-point; Measure never changes the document. Esc cancels in order: pending operation → measurement → selection → request Select tool. Arrow keys nudge by one grid step (Shift ×5). Grid step comes from `SnapSettings::gridLevel` via `DesignCanvas::gridStep` (default 2.54 mm schematic / 0.635 mm board). Schematic wires are routed at right angles (`orthogonalRoute`, Ctrl = free angle); placement/moves use alignment guides and pin joining (`snapPlacement`), Shift locks a move to one axis; moves keep wire connections (`moveItemsKeepingConnections`) and a lone wire drag reshapes it (`dragWireSegment`/`dragWireVertex`).
- Snapping (`SnapSettings`): grid, objects (pins/pads/vertices/corners), edges (nearest point on segments), centres; `diagonal`/`orthogonal` constrain wire/line direction relative to the previous point.
- Every document change goes through `pushEdit(text, newDocument, newSelection)`, which pushes a `DocumentEditCommand` holding full before/after `SketchDocument` + selection snapshots; `redo`/`undo` call `restore(...)`, which replaces document and selection without creating an undo entry. A whole drag-move is one undo step; a click without movement creates none.

## Architecture rules (from README, ADR-0001, architecture notes)

- Mergen/Kayra are marketing names; code uses technical names (`SchematicWorkspace`, `BoardWorkspace`, `ToolWorkspaceHost`) and must not turn marketing names into domain class names.
- UI event handlers carry no domain business logic. Domain libraries must not depend on `hatt-ui-shell` or on Qt UI types.
- Only the host creates docks, toolbars, and menus, with stable object names. Feature/plugin code must not call `QMainWindow::addDockWidget`, `addToolBar`, or mutate menus directly; contributions will go through `ContributionRegistry`.
- Plugins never get mutable domain pointers: reads via snapshots/DTOs, writes via command/transaction services. Plugin UI inherits the app palette and must not restyle the shell.
- `.hatt`, `.hattc`, `.hattplug` are separate, versioned formats.
- `SketchModel` is interim UI-shell state (ADR-0002/0003): do not persist it or expose it to plugins. Connectivity and DC algorithms live in the Qt-free `hatt-electrical` library; only `SketchCircuit` adapts editor snapshots. Permanent identities/units/transactions remain HATT-002/003/004.
- AI and cloud features must never be required for the app to run.
- No new dependency, persistent format field, or cross-layer dependency without an ADR and tests.
- QML is not a dependency of the v1 shell or plugin SDK.
- UI: user-facing strings go through `tr()` (add Turkish entries to `translations/hatteda_tr.ts`); dark and light themes are both first-class; selection state must pair color with a structural cue (left rail / top border); command icons are code-drawn via `iconKind`/`makeIcon` with a tooltip that includes the shortcut — never ad-hoc Unicode symbols as icons; alignment/distribution commands live only in the collapsible bottom alignment strip.
