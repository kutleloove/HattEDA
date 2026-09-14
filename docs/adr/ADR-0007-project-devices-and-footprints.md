# ADR-0007: Project Devices and Footprints (Make Device / Make Package)

**Status:** Accepted  
**Date:** 2026-09-14  
**Issue:** #29

## Context

The built-in symbol library (`symbolLibrary()`) is compiled in and immutable. Users need to add
parts that the library lacks, as in Proteus ISIS "Make Device" and ARES "Make Package":

- a schematic device with a known pin count, optional datasheet data and a footprint;
- a footprint either generated from a few dimensions or drawn on the board (pads + silkscreen);
- when a footprint is made for a device, the device's data must be visible next to the editor.
  If the datasheet gives package geometry the footprint is generated from it, a pin current
  suggests pad and trace widths, and without any data only the pin count is known, so the
  footprint gets exactly that many pads.

The library is still the interim `SketchModel` (ADR-0002); permanent library identities stay
HATT-002.

## Decision

### 1. Runtime symbol registry

`registerSymbols(QVector<SymbolDefinition>)` (`SketchModel.hpp`) adds or replaces project
symbols; `findSymbol` searches the built-in list first, then the registry. A replaced definition
is retired, not freed, so pointers returned by `findSymbol` stay valid for the process lifetime
(canvases and dialogs hold them). `SymbolDefinition` gains `displayName` (untranslated name of
project symbols) and `defaultPinPadMap`.

`ComponentLibrary.hpp` turns project definitions into symbols (`registerProjectLibrary`), which
`MainWindow::activateProject`, `newDevice`/`newFootprint` and `parseProject` call. Project ids use
the prefixes `project.device.` and `project.footprint.` followed by a UUID, so they can never
collide with built-in ids.

### 2. Definitions (`ProjectLibrary`)

- `DeviceDefinition`: `id`, `name`, designator `prefix` (1–8 letters), `defaultValue`,
  `footprint` (optional, same pad count), `pinCount` (1–400), `pinNames` (≤ pinCount),
  `pinPadMap` (empty = pin N to pad N; otherwise a permutation of 1..pinCount, checked by
  `validatePinPadMap`) and `DeviceSpec` (manufacturer, part number, datasheet link, package
  style, through-hole, pitch, row spacing, body, lead width/length, current per pin; 0 or empty
  means unknown). The schematic symbol is generated (`deviceSymbol`): up to two pins a small box,
  otherwise an IC box with the left row downwards and the right row upwards on the 2.54 mm grid.
- `FootprintDefinition` is one of two forms:
  - **generated** from `FootprintParams` (style `two-terminal|single-row|dual-row|quad-row`, pad
    count, pitch, row spacing, pad shape/size, drill, body). `validateFootprintParams` rejects
    wrong pad counts for the style, non-positive sizes, drills not smaller than the pad and
    overlapping pads. Pads are numbered counter-clockwise from pin 1 (DIP/SOIC/QFP convention);
    through-hole pin 1 is square.
  - **explicit** (`isExplicit()`: `pads` not empty), for packages drawn on the board (#28 Make
    Package): silkscreen `shapes`, pad centres in `pins` and `PadDefinition pads` parallel to
    `pins`, in mm relative to the origin as seen from the top. `validateExplicitFootprint`
    requires pad numbers 1..n used once, positive sizes, drills smaller than the pad and a copper
    layer. `footprintSymbol` orders them by pad number so pin-to-pad maps index `pins`.
  Both forms produce silkscreen-only `shapes` plus `pins`/`pads`; copper is drawn from `pads`
  (ADR-0006 §2, #28).

### 3. Footprint suggestion

`suggestFootprint(device)` always uses `padCount = pinCount`. Datasheet geometry sets style,
pitch, row spacing (from the body plus lead length when no spacing is given), drill
(lead width + 0.3 mm, rounded up to 0.1 mm) and SMD pad sizes from the leads. Without any
geometry the start is a generic 2.54 mm through-hole single row (dual row for even counts ≥ 4).
A pin current widens pads to `recommendedCopperWidth` (IPC-2221 external copper,
I = 0.048·ΔT^0.44·A^0.725, 1 oz, ΔT = 10 °C) as far as the pitch allows (gap ≥ 0.2 mm) and adds a
note with the minimum trace width. This is guidance, not a DRC rule (#31).

### 4. UI

- `DeviceEditorDialog` (`hatteda.devices.new`, Design › New device…): device fields, datasheet
  group, `DeviceFootprint` (footprints with the same pad count), `DeviceCreateFootprint` and the
  `DevicePinMap` table (`DevicePinPad<N>` spin boxes). The new device is added to the project pick
  list.
- `FootprintEditorDialog` (Design › New footprint…, or from a device): live preview; opened from a
  device the pad count is locked to the pin count and `DeviceInfoBox` on the right shows the
  datasheet data, minimum copper width, suggestion notes, or that only the pin count is known.

### 5. `.hatt` storage

Additive fields in `library` (no `formatVersion` bump; files without them read as before, older
v2 readers ignore unknown keys):

```json
"library": {
  "devices": ["project.device.<uuid>"],
  "customFootprints": [
    {"id": "project.footprint.<uuid>", "name": "SOIC-8", "style": "dual-row", "padCount": 8,
     "pitch": 1.27, "rowSpacing": 5.4, "padShape": "rect", "padWidth": 1.5, "padLength": 0.6,
     "drill": 0, "bodyWidth": 3.9, "bodyLength": 0},
    {"id": "project.footprint.<uuid>", "name": "Jack",
     "pads": [{"number": 1, "shape": "round", "width": 1.8, "height": 1.8, "drill": 1.0,
               "layers": 3, "at": [-3.5, 0]}],
     "shapes": [{"points": [[-5, -3], [5, -3]], "closed": true}]}
  ],
  "customDevices": [
    {"id": "project.device.<uuid>", "name": "LM358", "prefix": "IC", "pinCount": 8,
     "value": "LM358", "footprint": "project.footprint.<uuid>", "pinNames": ["OUT1"],
     "pinPadMap": [3, 2, 1, 4, 5, 6, 7, 8],
     "spec": {"manufacturer": "TI", "package": "dual-row", "pitch": 1.27, "pinCurrent": 0.04}}
  ]
}
```

The library is read before the documents: footprints are validated and registered first, then
devices (their footprint must exist with the same pad count), then the pick list, so document
items using project symbols validate. Any invalid row rejects the file with a message naming it.

## Consequences

- Project symbols live in a process-wide registry; opening another project re-registers its
  definitions (ids are UUIDs, so projects cannot shadow each other's parts).
- Editing or deleting an existing device/footprint is not implemented yet; definitions are only
  added. Parts already placed keep their own `footprint` and `pinPadMap` fields.
- Only generated schematic symbols are supported; drawing a custom schematic symbol is future
  work and will need an explicit symbol form like explicit footprints.
- Tests: `hatt-component-library-tests`, `ProjectFileTests::customLibraryRoundTrips` /
  `invalidCustomLibraryRowsAreRejected`, `MainWindowTests::newDeviceCreatesFootprintAndPinMap`.
