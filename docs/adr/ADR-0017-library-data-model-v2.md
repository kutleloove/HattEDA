# ADR-0017: Library data model v2 — data-driven built-in library, symbol variants, legacy alias migration

**Status:** Accepted (PR (a): model, loader, symbolVariant/v6, legacy alias. ComponentCatalog
consolidation — folding `ComponentCatalogEntry` into `LibraryDevice` so `componentCatalog()`,
`searchComponentCatalog()` and `PickDevicesDialog` read one model — is in scope for #61 too, per
follow-up direction, as a separate PR (b), "Closes #61".)
**Date:** 2026-09-17
**Issue:** #61 (epic #60)

## Context

`symbolLibrary()` (`SketchModel.cpp`) is 17 schematic symbols (`schematic.*`) and 12 footprints
(`board.*`) as literal, hand-written `SymbolDefinition` C++ values compiled into the binary.
Separately, `ComponentCatalog.hpp` already generates a larger, more structured set of parts
(`catalog.device.*`) from compact in-code family tables — `DeviceDefinition` + `CatalogCategory` +
`SimulationModelDefinition` — and registers them into the same runtime symbol registry
(`registerSymbols`/`findSymbol`) that `symbolLibrary()` populates. Both are still compiled-in data,
not an external, versioned resource, and `SymbolDefinition` has no notion of more than one visual
variant per device. The epic (#60) wants the entire built-in library rebuilt on real dimensions with
simulation models, KiCad import and circuit templates; this issue is the data model those depend on.

## Decision

### 1. New structures (`LibraryModel.hpp`, `libs/ui-shell`)

A device, its symbol variants and its footprint options become three distinct records, replacing a
single flat `SymbolDefinition` as the *authoring* format (the runtime registry still speaks
`SymbolDefinition`; see §3):

- **`LibraryDevice`**: `id`, `SymbolCategory`, designator `prefix`, `defaultValue`,
  `simulationModel` (empty = explicitly "cannot simulate", reported at Start — unchanged id
  namespace from `ComponentCatalog`; I will not touch that namespace without checking first, since
  #62 shares it), an ordered `pins` list (`name`, 1-based `number`, stable across every variant),
  a `variants` list (≥ 1, first the default) and a `footprints` list (footprint id + optional
  pin→pad map; first is the default, same pad-count rule already used for `DeviceDefinition`).
- **`LibrarySymbolVariant`**: `id` (e.g. `standard`, `animated`), `shapes`, `pinPositions` parallel
  to `LibraryDevice::pins` (same order/count, different layout per variant), and an
  `animationToken` string (e.g. `led-glow`; empty = static) as the #64 hook — model and
  serialization only, no animation behavior in this issue.
- **`LibraryFootprint`**: `id`, `shapes`, `pins`, `pads` (unchanged from today's footprint shape),
  plus a `source` string recording the standard/datasheet it was derived from (epic acceptance
  criterion). Content itself (real IPC-7351B dimensions) is #63.

### 2. Identity scheme

`lib.<family>.<part>` for devices (e.g. `lib.passive.resistor`, `lib.terminal.power`,
`lib.probe.voltage`); footprints get their own flat namespace, `lib.footprint.<package>` (e.g.
`lib.footprint.r0603`), since a footprint is now a reusable option shared by several devices rather
than one device's fixed `defaultFootprint`. `project.device.`/`project.footprint.` (ADR-0007) and
`catalog.device.` (`ComponentCatalog`) namespaces are untouched.

### 3. Source format and runtime bridge

The built-in library ships as JSON in a Qt resource (`qt_add_resources`, `BASE`/`PREFIX` mapping it
to `:/library/builtin.json`; new to `hatt-ui-shell`, no external dependency — Qt6 native), loaded
once (`builtInLibrary()`, a function-local `static`) by a loader that converts each `LibraryDevice`'s
`"standard"` `LibrarySymbolVariant` and each `LibraryFootprint` into the existing `SymbolDefinition`
and appends them to the list `symbolLibrary()` returns — so `DesignCanvas`, connectivity, Gerber
export etc. keep reading `findSymbol`/`symbolLibrary()` exactly as now; the new model changes
*authoring and storage*, not the runtime rendering/connectivity path. Because the resource is
compiled into the static `hatt-ui-shell` library, Qt does not auto-register it into whichever final
executable links that library (a known static-library-resource gotcha); the loader calls
`Q_INIT_RESOURCE` once via a free function outside any namespace (the macro requires global scope).
Display names stay translatable: the JSON stores plain English source text
(`displayNameKey`), translated at load time via `QCoreApplication::translate("hatt::ui::SymbolLibrary", key)`,
with a `QT_TRANSLATE_NOOP` inventory array (matching `ComponentCatalog.cpp`'s own
`CatalogTranslationSources` trick) so `lupdate` still finds them. `symbolLibrary()`'s former 17+12
built-ins (`buildLibrary()`, deleted) migrated into this JSON with their existing (approximate)
geometry byte-for-byte unchanged, generated once by a throwaway conversion test rather than
hand-transcribed, to avoid transcription risk on the curved shapes (arcs/circles). Re-dimensioning
them is #63. `ComponentCatalog`'s larger generated table also migrates onto this same model, as PR
(b) (see Status).

### 4. `SketchItem::symbolVariant` and format version 6

New optional field, default `"standard"`, chosen at placement and editable like footprint today.
Same tiered `requiredFormatVersion` pattern #59 introduced: highest applicable tier wins —
`ProjectVariantFormatVersion = 6` when any item sets a non-default `symbolVariant`, else version 5
(mirrored item), else 4 (zone feature), else base 3. An older reader ignoring `symbolVariant` would
silently show the wrong shape/pin layout, the same "changes meaning" case as mirroring.

### 5. Legacy alias migration

`aliases` in the resource maps every old `schematic.*`/`board.*` id (and, after PR (b), `catalog.device.*`)
to its `lib.*` replacement. **`findSymbol()` itself** consults it on a registry miss (one place, not
`ProjectFile.cpp` alone) — so *every* caller (rendering, connectivity, export, existing tests that
still construct items with old ids) transparently sees the current symbol, not just the file reader;
this is simpler than the file-reader-only design first proposed and needed no test file updated
except where a test asserted the exact *id string* the system itself now returns (`toolVariant()`,
`library.devices`), not merely supplied one as input. This is a **behavior change** from ADR-0004's
current strict "unknown symbol rejects the file": `ProjectFile.cpp`'s reader now only rejects when
`findSymbol` (registry + alias) truly misses, in which case the item becomes a visible placeholder —
a minimal dashed box, one pin at the anchor, registered under the item's own unresolved id so every
other reader of the document stays safe too — instead of rejecting the whole file, and the load
produces a non-fatal warning (`ProjectLoad::warnings`, surfaced by `MainWindow::openProjectFile` as
a `QMessageBox::warning` after the project opens). Newly-saved projects always write the new `lib.*`
ids (nothing re-canonicalizes an *existing* item's stored id on load — only new placements use
`symbolLibrary()`, which only contains new ids); the alias table is read-direction only.

## Consequences

- New header/source (`LibraryModel.hpp/.cpp`) plus the JSON resource and loader; `ComponentLibrary.hpp`
  (ADR-0007 project devices) and `ComponentCatalog.hpp` are unchanged by this issue.
- `MainWindowTests`/`DesignCanvasTests` fixtures that hard-code `schematic.*`/`board.*` ids keep
  working unchanged (old ids resolve via the alias table); new tests cover loading, alias resolution
  and variant serialization directly.
- Out of scope here (per the epic): real footprint dimensions (#63), variant-picker UI and
  simulation-driven animation (#64).

## Validation

`ProjectFileTests`: `unknownSymbolLoadsAsPlaceholderWithWarning` (unmapped id → placeholder, one
warning naming it, byte-identical re-save of the untouched item), `legacyIdResolvesWithoutWarningOrPlaceholder`
(a known legacy id → no warning, `findSymbol` resolves it), `formatVersionPicksTheHighestFeatureInUse`
(variant > mirror > zone > base priority, extended for the v6 tier), `v1FileIsUpgradedToV2` (default
`symbolVariant` on upgrade). `hatt-component-library-tests`/`hatt-design-canvas-tests`/
`hatt-ui-shell-tests` exercise the migrated library through the existing suite; the handful of
assertions that compared an exact *id string* the system returns (not merely supplied one) were
updated to the new `lib.*` ids.
