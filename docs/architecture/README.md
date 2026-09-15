# Desktop shell architecture

The desktop executable is a composition root. Reusable shell behavior lives in the `hatt-ui-shell` CMake target. Domain libraries must not depend on this target.

`MainWindow` owns host-managed Qt chrome. Primary workspaces are persistent pages in a stacked widget. Tool workspaces are closable tabs. Dock creation and stable object names remain host responsibilities so layout persistence and future plugin disable/safe-mode flows can be controlled centrally.

The current code is a bootstrap vertical slice, not the public plugin API. External contributions must wait for `ContributionRegistry`; feature code must not call `QMainWindow::addDockWidget`, `addToolBar`, or mutate menus directly.

## Editor surfaces

`EditorSurfaces` is a stacked widget holding `PrimaryWorkspaceHost` and `ToolWorkspaceHost`. `PrimaryWorkspaceHost` contains exactly two `DesignCanvas` instances created in the `MainWindow` constructor: `Workspace::Schematic` (Mergen, index 0) and `Workspace::Board` (Kayra, index 1). Switching workspace calls `workspaceChanged()`, which activates that canvas's undo stack, cancels pending operations on the other canvas, and re-applies the current tool mode (falling back to Select when Probe is not available on the board).

## Tool model

All tool modes are checkable `QAction`s in one exclusive `QActionGroup`:

| Action id | Mode | Shortcut |
| --- | --- | --- |
| `hatteda.tool.select` | Select | V |
| `hatteda.tool.component` | Component | A |
| `hatteda.tool.connect` | Wire / track | W |
| `hatteda.tool.terminal` | Terminal / port | R |
| `hatteda.tool.probe` | Probe (Mergen only) | P |
| `hatteda.tool.draw` | 2D graphics | D |
| `hatteda.tool.measure` | Measure | M |

The `ToolRail` buttons use these actions as their default action, so rail, menu and keyboard state cannot diverge. A mode is not yet a concrete canvas tool: `activateToolMode` fills `ObjectSelector` for the mode and workspace (component mode: the project's picked devices from `projectDeviceList` in the schematic, and in the board the schematic parts not placed yet from `unplacedBoardParts`, placed through `DesignCanvas::setPlacementTemplate`; terminal/probe modes: symbols of the matching `SymbolCategory`; or the draw tools Line, Polyline, Rectangle, Circle, Arc, Text, and on the board also Board outline; Kayra zone mode: Copper zone, Keepout zone and Area zone as `CanvasTool::Zone` variants, with the board's zones in `ZoneList`, ADR-0012), and `applyObjectSelection` translates mode + selected row into `DesignCanvas::setTool(CanvasTool, variant)`. The selected row is remembered per mode and workspace.

Canvas commands (`hatteda.action.delete`, `duplicate`, `rotate`, `select-all`, `zoom-in`, `zoom-out`, `fit`, and `hatteda.align.*`) only act on `editingCanvas()`, which is `nullptr` on the welcome page or while a tool workspace is shown. `hatteda.action.run-checks` exists but is disabled because checks are not implemented.

## Project files

Projects are stored in the interim `.hatt` JSON format described in ADR-0004 (`ProjectFile.hpp`). `MainWindow` keeps the open file in `projectPath_`:

- **New project:** `createNewProject` writes an empty file immediately and suggests a name that does not exist yet. It uses the folder in `projects/location` and asks before replacing a file.
- **Open and Recent:** `openProject` and the Recent list call `openProjectFile`. On error it shows a warning and leaves the open project untouched.
- **Save:** `hatteda.action.save` (Ctrl+S) and `hatteda.action.save-as` (Ctrl+Shift+S) go through `writeProject`, which marks both undo stacks clean.
- **Unsaved changes:** `QUndoStack::cleanChanged` drives `updateProjectState`: the title shows `[*]` and Save is enabled while a project is open. `maybeSaveChanges` (Save / Discard / Cancel) guards New, Open, Recent and `closeEvent`. File › Quit (`hatteda.action.quit`, Ctrl+Q) closes the window and gets the same prompt. A failed Save in the prompt cancels the operation.
- **Safety (ADR-0005):** `ProjectGuard` (`ProjectSafety.hpp`) handles the safety files next to the project:
  - **Autosave:** writes `<name>.hatt.autosave` every `projects/autosaveMinutes` minutes (default 2, 0 disables it), only while dirty and changed.
  - **Recovery:** `openProjectFile` offers a newer, differing and valid recovery file (Restore / Open saved version / Cancel). Restore keeps the path and leaves the window modified.
  - **Backup:** `ProjectGuard::save` keeps `<name>.hatt.bak` (one generation) before overwriting.
  - **Lock:** `<name>.hatt.lock` (`QLockFile`) is held while the project is open. A project locked by another live process asks Open anyway / Cancel.
  - **Hooks:** `MainWindow` calls the guard from `openProjectFile` (`confirmLock`, `resolveRecovery`), `activateProject` and `saveProjectAs` (`projectActivated`), `writeProject` and `createNewProject` (`save`, `projectSaved`), `maybeSaveChanges` (`discardRecovery`) and `closeEvent` (`projectClosed`). The snapshot is `currentProjectData(path)`, the same data Save writes.
  - **Recent:** a missing Recent file shows a message and offers to remove the entry (`openRecentProject`).

## Undo

Each `DesignCanvas` owns a `QUndoStack`. Both stacks are registered in one `QUndoGroup`; `hatteda.action.undo` and `hatteda.action.redo` are created by the group and therefore always target the active workspace. Opening a tool workspace sets the active stack to `nullptr`. Opening or creating a project replaces both documents with the file contents, clears both stacks and marks them clean.

Document edits are snapshot based (ADR-0002): `DesignCanvas::pushEdit` pushes a `DocumentEditCommand` that stores the complete `SketchDocument` and selection before and after the change. `undo()`/`redo()` call `DesignCanvas::restore`, which replaces document and selection without creating a new undo entry and without changing the active tool.

## DesignCanvas

`DesignCanvas` is a custom-painted `QWidget`. World coordinates are floating-point millimetres; `worldToScreen`/`screenToWorld` apply a scale and offset. The grid is 2.54 mm on the schematic and 0.635 mm on the board.

Tool state machine (`CanvasTool`):

- `Select`: click selects, Shift/Ctrl adds, drag moves (one undo step for the whole drag), drag on empty space is rubber-band selection; arrow keys nudge by one grid step (Shift: five). Moves keep connections (ISIS style, `moveItemsKeepingConnections`): wires whose ends sit on a moved pin or wire vertex stretch, keeping horizontal/vertical runs orthogonal by sliding a free corner or inserting a grid-aligned dogleg. Dragging a single (not multi-selected) wire reshapes it instead of lifting it off its pins: near a vertex it moves that vertex (`dragWireVertex`, wires joined there follow), elsewhere it moves the segment (`dragWireSegment`, perpendicular only for axis-aligned segments; ends held by pins or other wires gain stubs). Redundant straight-through corners and zero-length segments are removed afterwards. Item count and order never change during these edits. T joins follow too: a wire end resting between the vertices of a moved or reshaped wire moves with a translated wire or dragged segment, and otherwise stays on (or moves to the nearest point of) the reshaped wire, so a junction dot never silently becomes a crossing; ends held by a pin stay.
- `Symbol`: click places the symbol given by the tool variant with an auto-incremented designator (`nextDesignator`); Ctrl+R rotates before placing.
- `Wire`: click adds corners; finishes on a pin/pad automatically, or on double-click/Enter. Right-click cancels and returns to Select. Schematic wires (and board tracks while the `orthogonal` snap is on) are routed Proteus style: each click inserts right-angle corners from `orthogonalRoute`, leaving a pin along its outward axis (`pinDirectionAt`) or continuing the previous segment, and entering a target pin along its axis (dogleg when both prefer the same axis). Holding Ctrl places a free-angle segment. In the schematic a corner placed on an existing wire joins it: `splitPathAtWires` splits the new wire there so both pieces end on the wire (a T join under ADR-0003), while a wire passing straight over another stays an unconnected crossing. Joins are shown as filled junction dots from `schematicJunctions` / `electrical::junctionPoints` (a wire end where three or more branches meet); the wire being drawn previews its future dots in the preview colour.
- `Line`, `Rectangle`, `Circle`: drag, or two clicks.
- `Polyline`: click adds vertices; double-click/Enter finishes; right-click cancels; Backspace removes the last vertex.
- `Arc`: start point, end point, then a through point.
- `Text`: click places a label.
- `Measure`: drag or two clicks; the document is not changed.

Esc cancels a pending operation, then clears a measurement, then clears the selection, and finally emits `selectToolRequested` so `MainWindow` returns to Select mode.

Snapping is configured through `SnapSettings` (`grid`, `objects`, `edges`, `centers`, `diagonal`, `orthogonal`, `guides`, `gridLevel`), which `MainWindow::applySnapSettings` pushes to both canvases from the `AlignmentBar` toggles.

- `gridLevel` (0–3) selects the snap step via `DesignCanvas::gridStep`: schematic 0.254 / 1.27 / 2.54 / 12.7 mm, board 0.127 / 0.254 / 0.635 / 1.27 mm (default level 2). Actions `hatteda.grid.step-1..4` (Ctrl+F1, F2, F3, F4 as in Proteus) live in View › Snap grid and in the `GridStepButton` menu next to the grid toggle; the level is persisted as `editor/snap/gridLevel`.
- Spacing indicators (design-tool style): while a selection is dragged or a symbol is being placed, `DesignCanvas::activeSpacings` finds the nearest non-wire neighbour on each side whose bounding box overlaps across the gap and draws a labelled dimension line in the `guide` colour. Wires are ignored and a containing box (e.g. the board outline) never counts as a neighbour. Gaps are flagged `equal` (heavier line) when they match the opposite gap or a gap between two other neighbouring objects, and those matching gaps are drawn too. With `guides` on, moving or placing also snaps to equal spacing (`spacingShift`): within 8 px a side gap is made equal to an existing gap, or the object is centred between two neighbours. Pin joins take precedence, and with grid snap on only shifts that keep the reference point on the grid are taken.
- Array (`hatteda.action.array`, Edit and object context menu): `MainWindow::showArrayDialog` asks rows, columns and centre-to-centre pitch in the display unit (default: selection size rounded up to the grid plus one step) and calls `DesignCanvas::createArray`, which copies the selection row by row with fresh ids and designators as one undo step.
- Display units (`Units.hpp`): the document always stores millimetres. The schematic shows mil (the 2.54 mm grid is 100 mil); the PCB follows the user preference `editor/units/board` (`mm` or `in`), chosen in View › PCB units (`hatteda.units.board-mm|board-in`). The unit applies to the coordinate readout, measurement, spacing labels, grid step labels and the property dialog position fields.
- `guides` enables smart alignment guides: while placing a symbol, moving a selection, dragging wires or drawing, a point (or any pin/origin of the moving group) within 8 px of the X or Y of another object's pin, vertex or symbol origin is pulled onto it and a dashed guide is drawn (`activeGuides`). While placing or moving, a pin within 10 px of another pin or wire vertex joins it exactly (`snapPlacement`). Shift while moving locks the move to horizontal or vertical.

## SketchModel

`SketchModel.hpp` holds the interim editor model and built-in library: `QPointF` millimetres, index-based selection, session UUIDs, values and explicit footprint/pin mappings. `SketchCircuit` adapts snapshots to the Qt-free `hatt-electrical` connectivity/DC library. Persistent identity/units/transactions remain HATT-002/003/004 work. See [circuit workflow](circuit-workflow.md) and ADR-0003 for UI commands, right-click behavior, PCB guidance and simulation limits.

## Tests

- `hatt-connectivity-tests`, `hatt-dc-solver-tests`, `hatt-sketch-circuit-tests`: electrical topology, DC reference circuits and UI-adapter/PCB/async workflow regressions.
- `hatt-project-file-tests` (`tests/unit/ProjectFileTests.cpp`): `.hatt` round trip, version and validation rules, atomic save.
- `hatt-project-safety-tests` (`tests/unit/ProjectSafetyTests.cpp`): autosave and recovery (Restore / Open saved version / damaged file), `.bak` backup, project lock between windows and stale locks, missing Recent entries, a failed Save in the unsaved-changes prompt.

- `hatt-ui-shell-tests` (`tests/unit/MainWindowTests.cpp`): welcome page, exclusive tool modes, primary/tool workspaces, tool actions driving the canvas and object selector, Probe unavailable in Kayra, undo following the active workspace.
- `hatt-design-canvas-tests` (`tests/unit/DesignCanvasTests.cpp`): placement and designators, undo keeping the tool, wire grid/pin snapping, polyline editing, rectangle/line input, orthogonal constraint, move as one undo step, wires following moved symbols, wire segment/corner dragging, delete/undo selection restore, rubber band, align/distribute, rotation, measure, Esc behaviour.
