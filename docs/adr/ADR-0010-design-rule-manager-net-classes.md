# ADR-0010: Design Rule Manager, Region Clearance Rules and Net Classes

**Status:** Accepted  
**Date:** 2026-09-15  
**Extends:** ADR-0008 (design checks), ADR-0009 (zone pours)

## Context

ADR-0008 stored one copper clearance and one board edge clearance. Users coming from Proteus ARES
expect its Design Rule Manager:
- clearances per region (whole board or one copper layer) and per object pair (pad-pad,
  pad-trace, trace-trace, graphics, edge);
- net classes (POWER, SIGNAL, user classes) that decide trace and via sizes, necking, allowed
  layers and ratsnest display per net ("copper thickness according to the netlist");
- differential pairs;
- defaults for thermal reliefs, solder resist guard and silkscreen clearance.

## Decision

### Model (`DesignRules.hpp`)

`DesignRules` keeps the five ADR-0008 values and adds:
- `clearanceRules`: `ClearanceRule { name, region (Board | TopCopper | BottomCopper), padPad,
  padTrace, traceTrace, graphic, edge }`. Empty means one DEFAULT Board rule built from `clearance`
  and `boardEdgeClearance` (`effectiveClearanceRules`).
- `netClasses`: `NetClass { name, traceWidth, viaDiameter, viaDrill, neckWidth, layers,
  ratsnestColor, ratsnestHidden, nets }`. Empty means POWER (0.635 mm trace, 1.0/0.5 mm via) and
  SIGNAL (0.3048 mm, 0.8/0.4 mm) (`effectiveNetClasses`).
- `differentialPairs`: `DifferentialPair { name, positiveNet, negativeNet, width, gap }`. Stored and
  edited only; routing and checks follow later.
- `defaults`: `RuleDefaults { thermalRelief, thermalGap, spokeWidth, solderResistGuard,
  silkClearance, curveTolerance }`.

Resolution:
- `clearanceBetween(rules, layerMask, a, b)` takes, for every shared copper layer, that layer's
  region rules when there are any, otherwise the Board rules.
  - Pad–pad uses `padPad` (vias count as pads), trace–trace uses `traceTrace`, mixed pairs use
    `padTrace`, and anything involving a graphic or zone uses `graphic`.
  - The largest applicable value wins, so a pair on both layers is held to the stricter layer.
- `edgeClearance(rules, layerMask)` resolves `edge` the same way.
- `netClassAssignments`/`netClassForNet`:
  - An explicit `nets` entry decides the class.
  - Otherwise a net that contains a ground or power rail symbol (or is named `0`) is POWER and
    every other net is SIGNAL, when those classes exist; otherwise the first class.
  - A net may be listed in only one class.

`validateDesignRules` checks:
- names are present and unique;
- every size is finite and at most 100 mm, and gaps are greater than 0;
- the via drill is smaller than the via, and the neck is no wider than the trace;
- each net class allows at least one copper layer, and ratsnest colours are valid;
- differential pairs use two different nets.

### Storage

The `.hatt` "rules" object gains optional `clearanceRules`, `netClasses`, `differentialPairs`
and `defaults`:
- Region tokens are `board`, `top-copper` and `bottom-copper`; class layers are
  `["top-copper", "bottom-copper"]`.
- Each part is written only when it differs from the default, so existing projects keep their
  bytes. No `formatVersion` bump.
- Wrong types, unknown tokens and invalid values reject the file.
- JSON lives in `designRulesToJson`/`designRulesFromJson`; `ProjectFile.cpp` only delegates to
  them.

### UI

`DesignRuleManagerDialog` replaces the ADR-0008 dialog under `hatteda.action.design-rules`, with
tabs Design Rules, Net Classes, Differential Pairs and Defaults. Object names are listed in the
header.

When the dialog returns:
- A single DEFAULT Board rule with equal gaps, and the untouched default classes, are stored in
  the compact form.
- Otherwise `clearance`/`boardEdgeClearance` become the largest gap and edge of the rules, for
  code that still has one clearance.

### Checks and consumers

- DRC:
  - Conductor pairs and pours use `clearanceBetween` with the object kinds and shared layers.
  - The board edge uses `edgeClearance`.
  - New warnings: `drc.net-class-width`, for a track narrower than its class trace (or neck when
    set); and `drc.net-class-layer`, for a track on a layer its class does not allow.
  - DRC pours use the graphic clearance and the rule defaults.
- Routing defaults, ratsnest colours, pour and CAM mask settings read the same API; they are
  wired by their owners (canvas, `ZoneFill`, `GerberExport`).

## Consequences

- Rules can be tightened per copper layer without touching the others; necked tracks near fine
  pitch pads are allowed down to the class neck width.
- Differential pair routing/length checks, per-class clearances, inner layers and design rule
  waivers remain future work.
- Tests:
  - `DesignChecksTests::clearanceRulesByRegionAndObject`
  - `netClassesAssignPowerSignalAndExplicitNets`
  - `designRulesJsonRoundTrip`
  - `drcUsesRegionRulesAndNetClasses`
  - `MainWindowTests::designRuleManagerEditsRulesClassesPairsAndDefaults`
