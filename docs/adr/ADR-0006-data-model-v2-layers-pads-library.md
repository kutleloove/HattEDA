# ADR-0006: Data Model v2 — PCB Layers, Pads, Via, and Project Library

**Status:** Accepted  
**Date:** 2026-09-13  
**Issue:** #25

## Context

The interim editor model (`SketchModel`, ADR-0002) treated all board geometry as plain
floating-point shapes, with copper pads represented as `SymbolShape` values with `copper=true`.
This was sufficient for visual rendering and early ratsnest work (ADR-0003) but prevented
implementing:

- Multi-layer PCB features (active layer selection, bottom-side placement, via drill holes)
- Structured pad/via items that carry pad number, shape, size, and drill information
- A project library for user-created devices and packages (Issues #27, #29)

## Decision

### 1. `BoardLayer` enum

Adds nine stable layer identifiers with Proteus-convention names:
`TopCopper`, `BottomCopper`, `TopSilk`, `BottomSilk`, `TopResist`, `BottomResist`,
`TopPaste`, `BottomPaste`, `BoardEdge`.

File tokens (stable, never reuse): `top-copper`, `bottom-copper`, `top-silk`, `bottom-silk`,
`top-resist`, `bottom-resist`, `top-paste`, `bottom-paste`, `board-edge`.

### 2. `PadDefinition` struct

Carries pad number (1-based), shape (`Round`/`Rect`/`Oval`), width, height, drill diameter,
and a `layers` bitmask (`1 << int(BoardLayer)`). Shape file tokens: `round`, `rect`, `oval`.

`SymbolDefinition` gains a `pads` list, parallel to `pins` (`pads[i]` sits at `pins[i]`). Since
#28 footprint copper is drawn, hit-tested and connected only from `pads`; `shapes` carries the
silkscreen outline only (see §7).

### 3. `SketchItem::Kind::Pad` and `::Via`

Two new item kinds for placed pad and via items. `Pad` carries a `PadDefinition pad` field;
`Via` carries a `drillDiameter` field. Both use `points[0]` as the center.

### 4. New `SketchItem` fields (all items)

- `layer` (`BoardLayer`, default `TopCopper`) — active layer for board items
- `onBottom` (`bool`, default `false`) — footprint placed on bottom side
- `excludeFromBoard` (`bool`, default `false`) — schematic symbol excluded from PCB transfer,
  board component mode and the connection guide (edited in the properties dialog); it still
  takes part in the netlist and simulation
- `width` (`double` mm, default `0`, added by #28) — copper width of a board track and outer
  diameter of a via; `0` means the default for the kind (T12 = 0.3048 mm track, 0.8 mm via).
  Written as `"width"` only when greater than zero; a negative or non-numeric value is rejected
  on load. The field is additive, so no format version bump was needed.

### 5. `ProjectLibrary`

Stored in `ProjectData` and written as the `"library"` object of the `.hatt` file.

- `devices` (#27): the Proteus ISIS style pick list — ids of built-in schematic components
  (`schematic.*`, category Component) offered by schematic component mode, written as
  `"library": {"devices": ["schematic.resistor", ...]}`. The saved list always contains every device
  the schematic uses (`projectDeviceList`); a device can only be removed from the list while no
  part uses it. Unknown or non-component ids and a non-object library are rejected on load. A
  missing `library` or `devices` (files written before #27) reads as an empty list, so no format
  version bump was needed: the field is additive and older v2 readers ignore it.
- User-created devices and footprints (`customDevices`, `customFootprints`) are added by #29,
  see ADR-0007.

Related defaults (not stored): `SymbolDefinition::defaultValue` / `defaultFootprint` give newly
placed schematic parts a value and a pin-count-matching footprint with a one-to-one pin-to-pad
map, so a part can reach the PCB without editing its properties.

### 6. `.hatt` format version 2

`formatVersion` is bumped from 1 to 2. The reader accepts both v1 and v2:
- v1 files parse normally; new fields take their default values (silent upgrade)
- v2 adds optional fields per item; missing fields are silently skipped
- Files newer than the reader's current `ProjectFormatVersion` are rejected with a user-visible
  error (the current version and later additions are tracked by ADR-0004)

### 7. Layer behaviour, pad tools and Make Package (#28, #30)

- **Geometry helpers** (`SketchModel.hpp`): `itemPads` gives world-space pads of footprints
  (rotated, and mirrored left to right with `mirroredLayerMask` when `onBottom`), placed pads and
  vias; `padOutline`, `itemCopperLayers`, `itemLayerMask`, `trackWidth`, `viaDiameter` and
  `viaDrill` are the single source for drawing, hit testing, connectivity and DRC. A drilled pad
  conducts on both copper layers; an SMD `Pad` item conducts on its `layer`.
- **Active layer** (`DesignCanvas::activeLayer`): new tracks, zones and SMD pads go to the active
  copper layer (the copper layer of its side when a non-copper layer is active); 2D graphics go to
  the active layer, or the silk layer of its side while copper is active; the board outline always
  goes to `BoardEdge`; footprints are placed on the bottom side while a bottom layer is active.
  Space swaps top and bottom copper, Page Up / Page Down pick them. Changing the copper layer
  while routing ends the track piece at the last corner, adds a via there and continues on the
  new layer; the whole route is one undo step and Backspace reverts the last layer change.
- **Visibility**: hidden layers are not drawn, hit-tested, snapped to or selectable. The inactive
  side is drawn first and dimmed so the active side stays on top.
- **Styles** (application preferences, not stored in `.hatt`): track styles T8–T100, via styles
  V24–V70 and pad styles (round, square, oval/DIL, SMD rectangular and round, edge connector)
  plus user-defined styles in QSettings `editor/board/customTrackStyles`, `customViaStyles` and
  `customPadStyles`. Placed items copy their sizes, so editing or deleting a style never changes
  a document. Layer colours (defaults: top copper red, bottom copper blue) can be overridden per
  theme under `appearance/layerColors/<dark|light>/<layer>`.
- **Connectivity** (`hatt-electrical`): `Pin::layers`, `Wire::layers` and
  `ConnectivityInput::junctionLayers` are copper masks; anchors, wires and junctions only join
  when their masks overlap. Vias and through-hole pads are junctions on both copper layers, so
  tracks on different layers only connect through them.
- **Make Package / Decompose** (`PackageFromSelection.hpp`): selected pads, vias and silkscreen
  graphics become an explicit `FootprintDefinition` (ADR-0007) relative to pad 1 or the pad
  centre; a drawing on the bottom side only is stored as seen from the top and placed mirrored;
  pad numbers that do not run 1..n are renumbered. Decompose replaces placed footprints with
  `Pad` items and silkscreen lines at the same positions and sizes.

## Consequences

- `SketchItem` is larger; snapshot-based undo memory cost increases slightly
- `kindByValue()` and `kindByName()` in `ProjectFile.cpp` must not be reused for `Pad`/`Via`
  tokens or layer token strings — they are stable file format identifiers
- Built-in footprints no longer carry `SymbolShape copper=true` shapes; the `copper` and `hole`
  flags remain only for compatibility and are ignored by board rendering
- Board algorithms (connection guide, DRC in #31, CAM) must read copper through `itemPads` and
  the layer masks rather than item kinds or shapes
- HATT-002/003/004 remain the permanent domain groundwork; this ADR extends the interim model
  only
