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

The `ToolRail` buttons use these actions as their default action, so rail, menu and keyboard state cannot diverge. A mode is not yet a concrete canvas tool: `activateToolMode` fills `ObjectSelector` for the mode and workspace (symbols of the matching `SymbolCategory`, or the draw tools Line, Polyline, Rectangle, Circle, Arc, Text, and on the board also Board outline and Copper zone), and `applyObjectSelection` translates mode + selected row into `DesignCanvas::setTool(CanvasTool, variant)`. The selected row is remembered per mode and workspace.

Canvas commands (`hatteda.action.delete`, `duplicate`, `rotate`, `select-all`, `zoom-in`, `zoom-out`, `fit`, and `hatteda.align.*`) only act on `editingCanvas()`, which is `nullptr` on the welcome page or while a tool workspace is shown. `hatteda.action.save` and `hatteda.action.run-checks` exist but are disabled because project files and checks are not implemented.

## Undo

Each `DesignCanvas` owns a `QUndoStack`. Both stacks are registered in one `QUndoGroup`; `hatteda.action.undo` and `hatteda.action.redo` are created by the group and therefore always target the active workspace. Opening a tool workspace sets the active stack to `nullptr`. Opening or creating a project clears both documents and both stacks.

Document edits are snapshot based (ADR-0002): `DesignCanvas::pushEdit` pushes a `DocumentEditCommand` that stores the complete `SketchDocument` and selection before and after the change. `undo()`/`redo()` call `DesignCanvas::restore`, which replaces document and selection without creating a new undo entry and without changing the active tool.

## DesignCanvas

`DesignCanvas` is a custom-painted `QWidget`. World coordinates are floating-point millimetres; `worldToScreen`/`screenToWorld` apply a scale and offset. The grid is 2.54 mm on the schematic and 0.635 mm on the board.

Tool state machine (`CanvasTool`):

- `Select`: click selects, Shift/Ctrl adds, drag moves (one undo step for the whole drag), drag on empty space is rubber-band selection; arrow keys nudge by one grid step (Shift: five).
- `Symbol`: click places the symbol given by the tool variant with an auto-incremented designator (`nextDesignator`); Ctrl+R rotates before placing.
- `Wire`: click adds corners; finishes on a pin/pad automatically, or on double-click, Enter or right-click.
- `Line`, `Rectangle`, `Circle`: drag, or two clicks.
- `Polyline`: click adds vertices; double-click, Enter or right-click finishes; Backspace removes the last vertex.
- `Arc`: start point, end point, then a through point.
- `Text`: click places a label.
- `Measure`: drag or two clicks; the document is not changed.

Esc cancels a pending operation, then clears a measurement, then clears the selection, and finally emits `selectToolRequested` so `MainWindow` returns to Select mode.

Snapping is configured through `SnapSettings` (`grid`, `objects`, `edges`, `centers`, `diagonal`, `orthogonal`), which `MainWindow::applySnapSettings` pushes to both canvases from the `AlignmentBar` toggles.

## SketchModel

`SketchModel.hpp` holds the interim editor model and the built-in symbol library. It has no Qt Widgets dependency, but it is not a domain model: it uses `QPointF` millimetres, index-based selection and no identifiers, nets or connectivity. `SymbolDefinition` entries (`schematic.*` symbols, `board.*` footprints) carry shapes, pins, a designator prefix and a `SymbolCategory`. It is replaced by the HATT-003/HATT-004 domain model and command transactions as described in ADR-0002.

## Tests

- `hatt-ui-shell-tests` (`tests/unit/MainWindowTests.cpp`): welcome page, exclusive tool modes, primary/tool workspaces, tool actions driving the canvas and object selector, Probe unavailable in Kayra, undo following the active workspace.
- `hatt-design-canvas-tests` (`tests/unit/DesignCanvasTests.cpp`): placement and designators, undo keeping the tool, wire grid/pin snapping, polyline editing, rectangle/line input, orthogonal constraint, move as one undo step, delete/undo selection restore, rubber band, align/distribute, rotation, measure, Esc behaviour.
