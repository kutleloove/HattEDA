# HattEDA desktop design system

## Direction

HattEDA uses a modern engineering-workstation visual language. The interface prioritizes long-session comfort, canvas contrast, information density, and predictable plugin surfaces over decorative dashboard patterns.

## Tokens

- Canvas: `#0b1016`
- Application surface: `#11161d`
- Elevated surface: `#18212a`
- Border: `#26313d`
- Primary text: `#e8edf2`
- Secondary text: `#7f8d9a`
- Brand and selection: `#18b6a4`
- Board layer colours (dark / light theme; Proteus ARES and KiCad convention, user-adjustable in
  View › Layer colours...):

  | Layer | Dark | Light |
  | --- | --- | --- |
  | Top copper | `#ff4d4d` | `#d11f1f` |
  | Bottom copper | `#4d8dff` | `#1f4fd1` |
  | Top silk | `#f2f2f2` | `#3a3a3a` |
  | Bottom silk | `#c9a3e6` | `#7a48a3` |
  | Top resist | `#4cc38a` | `#1d8a5c` |
  | Bottom resist | `#5cc8c8` | `#1f8080` |
  | Top paste | `#a9b4be` | `#66727d` |
  | Bottom paste | `#8f99cc` | `#4d5891` |
  | Board edge | `#e9c46a` | `#b8860b` |

  The inactive board side is drawn at reduced opacity; layer swatches in `BoardLayerPanel` always
  use the same colours as the canvas.
- Corner radius: 4 px for dense controls and 8 px for canvas surfaces
- Base spacing: 4 px, composed primarily as 8, 12, 16, 24 px

## Patterns

Primary workspaces use document-style tabs above the canvas. Commands occupy a compact horizontal strip. The left edge combines a narrow tool-mode rail with one contextual browser; HattEDA does not surround the canvas with permanently open Project, Inspector, and Output docks. Full workflow tools replace the central canvas through Tool Workspace Host instead of splitting it.

### Icons

Commands and tool modes use one coherent, code-drawn line icon set: each action carries an `iconKind` property and `makeIcon(kind, color)` paints it with the current palette's icon color. Icons are regenerated on palette changes, so dark and light themes need no separate assets. Icon-only buttons always have a tooltip that names the command and its shortcut (for example "Duplicate (Ctrl+D)"). Arbitrary Unicode symbols must not be used as icons; `45°` on a snap toggle is a text label, not an icon.

### Command strip

`CommandBar` holds icon-only `command` tool buttons in three groups separated by `CommandDivider`: Undo / Redo · Zoom in / Zoom out / Fit to design · Rotate 90° / Duplicate / Delete. "Run design checks" sits at the right edge with icon and text; it stays disabled until checks exist. Commands that need a selection are disabled when nothing is selected.

### Tool rail and shortcuts

`ToolRail` shows one `rail` button per tool mode, backed by a single exclusive action group. The checked mode uses the brand color together with a border.

| Mode | Shortcut |
| --- | --- |
| Selection | V |
| Component | A |
| Package (Kayra only) | K |
| Wire and track | W |
| Via (Kayra only) | I |
| Pad (Kayra only) | O |
| Terminal and port | R |
| Probe (Mergen only; disabled in Kayra) | P |
| 2D graphics | D |
| Measure | M |

Editing shortcuts: Undo (platform standard), Redo (platform standard and Ctrl+Y), Delete, Duplicate Ctrl+D, Rotate 90° Ctrl+R (also rotates a symbol before placement), Select all, Zoom in (platform standard and Ctrl+=), Zoom out, Fit to design Home / Ctrl+0. On the canvas: Esc cancels step by step and finally returns to Selection, Enter or double-click finishes wires and polylines, Backspace removes the last vertex, arrow keys nudge the selection by one grid step (Shift: five), Shift while dragging locks the move to one axis, Ctrl while drawing a wire allows a free angle. Snap grid step: Ctrl+F1 (finest), F2, F3 (default), F4 (coarsest); alignment guides are drawn as dashed magenta lines (`guide` canvas colour) in both themes. While moving or placing, the gap to the nearest neighbouring object on each side is shown as a solid `guide`-coloured dimension line with end ticks and a filled label (canvas background colour text) placed beside the line; a gap equal to another gap uses a heavier line and longer ticks, and the matching gaps are shown with it. Edit › Create array... builds rows × columns copies. Lengths are shown in mil on the schematic and in the user's PCB unit (mm or in) on the board.

`ContextPanel` next to the rail shows the active mode name, a preview of the chosen object, a context hint for the active tool, and the `ObjectSelector` list (hidden for modes without objects). In Kayra, track, via and pad modes list their styles in `ObjectSelector` with a `RoutingStyleBar` (New style / Edit / Delete; built-in styles cannot be edited), and `BoardLayerPanel` sits at the bottom left as in Proteus ARES: the `LayerVisibility` list above the `ActiveLayer` selector, both with layer colour swatches. The board canvas caption names the active layer and, while routing, the track width.

Selection always combines color with a structural cue such as a left rail or top tab border. This keeps workspace state distinguishable when color perception is limited.

Plugin-provided UI must inherit the application palette and typography. Plugins provide content widgets and descriptors; they do not restyle the application shell.

The schematic and PCB canvases derive their surface and grid contrast from the active application palette. Dark and light themes are first-class settings. User-facing strings must use Qt translation contexts; compiled translations are loaded from the application resources according to the saved language preference.

### Alignment strip

Placement and drawing share a collapsible bottom `AlignmentBar`. Its left side (`SNAP`) holds checkable `snap` toggle buttons, persisted per user:

| Toggle | Object name | Default | Effect |
| --- | --- | --- | --- |
| Grid | `hatteda.snap.grid` | on | Snap points to the grid |
| Objects | `hatteda.snap.objects` | on | Snap to pins, pads, vertices and corners |
| Edges | `hatteda.snap.edges` | off | Snap to the nearest point on object edges |
| Centres | `hatteda.snap.centers` | off | Snap to object centres |
| 45° | `hatteda.snap.diagonal` | on | Constrain wires and lines to 45° steps |
| Orthogonal | `hatteda.snap.orthogonal` | off | Constrain wires and lines to horizontal and vertical |

Grid, Objects, Edges and Centres are independent. 45° and Orthogonal are mutually exclusive: turning one on turns the other off; both may be off.

The right side (`ALIGN`) holds icon-only align left / horizontal centres / right, align top / vertical centres / bottom, and distribute horizontally / vertically. Align needs at least two selected objects, distribute at least three. A final "Hide" button collapses the strip to a single "Snapping and alignment" button. Alignment and distribution commands appear only in this strip (and the Design menu) so they do not permanently consume the primary command bar.

## Circuit workflow additions

Right-click cancels active placement/drawing and returns to Select. An idle single right-click opens the host context menu after the system double-click interval; double right-click deletes only the target and is undoable. Properties include label/text, position, supported rotation, value, footprint and explicit pin-to-pad mapping. CircuitMenu hosts netlist, PCB update and bounded DC simulation commands. Dashed airwires distinguish unrouted connections from solid copper. Reports follow both themes. See architecture/circuit-workflow.md and ADR-0003.
