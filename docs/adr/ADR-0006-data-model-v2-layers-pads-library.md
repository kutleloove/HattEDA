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

`SymbolDefinition` gains a `pads` list. For now it coexists with the old `shapes` list (visual
rendering continues from `shapes`; pad-aware PCB algorithms use `pads`). Full visual migration
is deferred to Issue #28 (Pad tools).

### 3. `SketchItem::Kind::Pad` and `::Via`

Two new item kinds for placed pad and via items. `Pad` carries a `PadDefinition pad` field;
`Via` carries a `drillDiameter` field. Both use `points[0]` as the center.

### 4. New `SketchItem` fields (all items)

- `layer` (`BoardLayer`, default `TopCopper`) — active layer for board items
- `onBottom` (`bool`, default `false`) — footprint placed on bottom side
- `excludeFromBoard` (`bool`, default `false`) — schematic symbol excluded from PCB transfer,
  board component mode and the connection guide (edited in the properties dialog); it still
  takes part in the netlist and simulation

### 5. `ProjectLibrary`

Stored in `ProjectData` and written as the `"library"` object of the `.hatt` file.

- `devices` (#27): the Proteus ISIS style pick list — ids of built-in schematic components
  (`schematic.*`, category Component) offered by schematic component mode, written as
  `"library": {"devices": ["schematic.resistor", ...]}`. The saved list always contains every device
  the schematic uses (`projectDeviceList`); a device can only be removed from the list while no
  part uses it. Unknown or non-component ids and a non-object library are rejected on load. A
  missing `library` or `devices` (files written before #27) reads as an empty list, so no format
  version bump was needed: the field is additive and older v2 readers ignore it.
- User-created devices and packages are added by #29.

Related defaults (not stored): `SymbolDefinition::defaultValue` / `defaultFootprint` give newly
placed schematic parts a value and a pin-count-matching footprint with a one-to-one pin-to-pad
map, so a part can reach the PCB without editing its properties.

### 6. `.hatt` format version 2

`formatVersion` is bumped from 1 to 2. The reader accepts both v1 and v2:
- v1 files parse normally; new fields take their default values (silent upgrade)
- v2 adds optional fields per item; missing fields are silently skipped
- Files with `formatVersion > 2` are rejected with a user-visible error

## Consequences

- `SketchItem` is larger; snapshot-based undo memory cost increases slightly
- `kindByValue()` and `kindByName()` in `ProjectFile.cpp` must not be reused for `Pad`/`Via`
  tokens or layer token strings — they are stable file format identifiers
- Issue #28 (Pad tools) must complete the visual migration of `SymbolShape copper=true` to
  `PadDefinition` rendering
- Connectivity and DC solver (ADR-0003) continue to use `pins`; pad-aware routing and ERC/DRC
  are Issues #31 scope
- HATT-002/003/004 remain the permanent domain groundwork; this ADR extends the interim model
  only
