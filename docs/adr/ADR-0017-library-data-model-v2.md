# ADR-0017: Library data model v2 — data-driven built-in library, symbol variants, legacy alias migration

**Status:** Accepted (PR (a): model, loader, symbolVariant/v6, legacy alias. PR (b): folds
`ComponentCatalogEntry` into `LibraryDevice`, "Closes #61".)
**Date:** 2026-09-17 (PR (a)), 2026-09-18 (PR (b))
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
a `QMessageBox::warning` after the project opens). The placeholder's single pin is a known,
accepted limitation: a real multi-pin part reduced to a placeholder shows its wires as
disconnected, which the warning already calls out rather than silently fixing. Because
`libraryFromJson`/`registerProjectLibrary` always registers a file's own project devices/footprints
before `documentFromJson` validates any document item, a properly-declared custom device is never
shadowed by a placeholder regardless of registration order elsewhere in the process (a dedicated
test, `properlyDeclaredCustomDeviceNeverShadowedByAPlaceholder`, pins this down). Newly-saved projects always write the new `lib.*`
ids (nothing re-canonicalizes an *existing* item's stored id on load — only new placements use
`symbolLibrary()`, which only contains new ids); the alias table is read-direction only.

## Consequences

- New header/source (`LibraryModel.hpp/.cpp`) plus the JSON resource and loader; `ComponentLibrary.hpp`
  (ADR-0007 project devices) is unchanged by this issue.
- `MainWindowTests`/`DesignCanvasTests` fixtures that hard-code `schematic.*`/`board.*` ids keep
  working unchanged (old ids resolve via the alias table); new tests cover loading, alias resolution
  and variant serialization directly.
- Out of scope here (per the epic): real footprint dimensions (#63), variant-picker UI and
  simulation-driven animation (#64).

### 6. PR (b): `ComponentCatalog` consolidation

`ComponentCatalogEntry` (`DeviceDefinition device; CatalogCategory category; QString description;
QStringList keywords; QVector<PinElectricalType> pinTypes; QStringList footprintOptions; QString
symbolTemplate;`) is deleted. Its fields fold directly into `LibraryDevice`/`LibraryPin` instead of
a second, parallel struct:

- `CatalogCategory`, `PinElectricalType`, `AnalysisSupport` and `SimulationModelDefinition` move
  from `ComponentCatalog.hpp` to `LibraryModel.hpp` (`ComponentCatalog.hpp` now includes it); the
  `simulationModel`/`simulationModelCatalog()` id vocabulary itself is untouched, as agreed with
  #62.
- `LibraryDevice` gains `catalogCategory`, `description`, `keywords`, `manufacturer`, `partNumber`
  (only meaningful for `category == SymbolCategory::Component`; terminals/probes leave them
  default). `LibraryPin` gains a `type` (`PinElectricalType`), replacing the separate
  `pinTypes` vector that could desync from `pins` — the same kind of fix PR (a) already made for
  `symbolTemplate` (baked into `variants` instead of resolved at registration time). `symbolTemplate`
  and `footprintOptions` are dropped outright: the former was only ever a *build-time* geometry
  source (now folded into the generated `variants[0]`, see below), the latter duplicated
  `footprints[i].footprintId`.
- **`componentCatalog()` is now a filtered view**, not a second generated table: every
  `builtInLibraryData().devices` entry with `category == SymbolCategory::Component`. This is the
  single source `searchComponentCatalog()`, `findCatalogComponent()` and `MainWindow::pickDevices()`
  all read.
- **Merge vs. new id.** Of ComponentCatalog's ~63 devices, 8 are really the same part as one of PR
  (a)'s 17 literal devices (resistor, capacitor, inductor, generic diode, LED, generic NPN, generic
  ideal op-amp, DC voltage source) — identified by which catalog entry used that device's id as its
  `symbolTemplate`. Those 8 got the catalog's metadata merged onto the *existing* `lib.*` device
  (keeping its id and, critically, its existing default footprint first in `footprints`, so already-
  placed items and tests referencing e.g. `lib.footprint.r0603` as the default keep working). The
  other ~55 became new `lib.<family>.<part>` devices, `family` from `CatalogCategory`
  (`passive`/`diode`/`transistor`/`analog`/`digital`/`source`/`electromech`/`connector`). Their
  `variants[0]` geometry is whichever the old code would have registered: the symbol borrowed via
  `symbolTemplate` when set (copied once, not referenced), otherwise the generic `deviceSymbol()`
  box — baked in by a temporary, self-deleting conversion executable (same technique as PR (a)'s
  throwaway QtTest) that ran the *old*, still-intact `ComponentCatalog.cpp` once to compute exact
  geometry, merged it into PR (a)'s `builtin.json`, and wrote the result back before the old
  generator code was deleted.
- Similarly, ~90 `catalog.footprint.*` definitions merge into `builtin.json`. Three exactly collide
  by package name with one of PR (a)'s original 10 (`sot23`, `soic8`, `dip8`): the *existing*
  `lib.footprint.*` geometry wins (not reconciled against the catalog's own, possibly slightly
  different, parametric version of the same package — deliberately out of scope, see below), and
  `catalog.footprint.<name>` becomes a plain alias to it. All other catalog footprints become new
  `lib.footprint.<package>` entries with geometry baked once via the existing `footprintSymbol()`
  generator, same as PR (a) did for its 10.
- `catalog.device.*` and `catalog.footprint.*` join the alias table exactly like `schematic.*`/
  `board.*` did in PR (a) — read-direction only, resolved by `findSymbol()` itself.
- **`registerBuiltInCatalog()` is deleted**, along with its four call sites
  (`ComponentLibrary.cpp` ×2, `ProjectFile.cpp`, `SketchCircuit.cpp`) and the matching calls in
  `ComponentLibraryTests`/`SketchCircuitTests`. It used to register `ComponentCatalog`'s
  *separately generated* symbols into the runtime registry before any lookup; now every built-in
  symbol (base and catalog alike) is already part of `symbolLibrary()`
  (`builtInLibrary().symbols`), and `findSymbol()`/`symbolsFor()` read `symbolLibrary()`
  unconditionally on every call — there is no registration step left to guarantee.
  `pickableDevices()`/`footprintsWithPads()` in `ComponentLibrary.cpp` lose their now-redundant
  second loop over the old `componentCatalog()`/`footprintCatalog()` for the same reason (it would
  otherwise re-add the same symbols `symbolsFor()`/`symbolLibrary()` already returned, duplicating
  every catalog entry in the picker).
- **Geometry is still approximate.** The ~55 new devices and ~90 new footprints carry whatever
  geometry `ComponentCatalog.cpp`'s old generic generators already produced (a plain box, or a
  parametric IPC-agnostic footprint) — real IPC-7351B/JEDEC dimensions are #63's job, not this PR's.
  Nothing here claims otherwise; `LibraryFootprint::source` stays empty until #63 fills it in.

## Validation

`ProjectFileTests`: `unknownSymbolLoadsAsPlaceholderWithWarning` (unmapped id → placeholder, one
warning naming it, byte-identical re-save of the untouched item), `legacyIdResolvesWithoutWarningOrPlaceholder`
(a known legacy id → no warning, `findSymbol` resolves it), `properlyDeclaredCustomDeviceNeverShadowedByAPlaceholder`
(a project's own, properly-declared custom device is never reduced to a placeholder),
`formatVersionPicksTheHighestFeatureInUse`
(variant > mirror > zone > base priority, extended for the v6 tier), `v1FileIsUpgradedToV2` (default
`symbolVariant` on upgrade). `hatt-component-library-tests`/`hatt-design-canvas-tests`/
`hatt-ui-shell-tests` exercise the migrated library through the existing suite; the handful of
assertions that compared an exact *id string* the system returns (not merely supplied one) were
updated to the new `lib.*` ids.

PR (b) adds: `ComponentLibraryTests::builtInCatalogIsConsistentAndSearchable` (extended — every
`componentCatalog()` entry has a unique id, ≥ 1 pin, a resolvable `simulationModel`, a resolvable
symbol with matching pin count, and every one of its `footprints` resolves too, with a valid
pin→pad map), `everyLegacyCatalogIdResolvesThroughTheAliasTable` (every `catalog.device.*`/
`catalog.footprint.*` alias, not just the two spot-checked above, resolves via `findSymbol` to its
stated `lib.*` id); `ProjectFileTests::catalogDeviceIdResolvesOnAColdProjectLoad` (a review
follow-up: `parseProject()` with a `catalog.device.*` id, as the first thing this executable does
with the library, to confirm removing `registerBuiltInCatalog()` left no hidden ordering dependency
on the project-load path); `MainWindowTests::catalogPickerShowsAllSuitablePackagesForADevice` (a
review follow-up: drives the real "Pick devices" dialog and checks the details panel's "Suitable
packages" line still lists every one of a multi-footprint device's options, not just the default,
after `ComponentCatalogEntry::footprintOptions` was dropped in favor of `footprints[i].footprintId`).
`SketchCircuitTests::detectsCopperShortAndUnsupportedSimulation`'s expected message changed from the
generic "no model" text to `nonlinear.diode`'s own "not implemented yet" limitation, now that
`lib.diode.standard` carries its real model id instead of an unset one — forward-compatible with
#62 giving that model real `DcOperatingPoint` support later.
