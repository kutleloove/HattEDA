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
- `netClasses`: `NetClass { name, traceWidth, viaDiameter, viaDrill, neckWidth, clearance, layers,
  ratsnestColor, ratsnestHidden, nets }` (`clearance` since issue #39, see the amendment below). Empty means POWER (0.635 mm trace, 1.0/0.5 mm via) and
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

## Amendment (2026-09-15, issue #39): net class clearance and routing widths

Net classes decided sizes but not gaps, and routing ignored them. Per-net rules are expressed as a
class that lists the net.

- **Model:** `NetClass::clearance` (mm, 0 = the clearance rules only).
  - `netClassClearances(rules, schematic)` maps each net whose class sets a clearance to that
    value.
  - `netPairClearance(classClearances, ruleGap, netA, netB)` gives the required gap: the rule
    gap, raised to the class clearance of either net (unknown nets add nothing).
  - `largestClearance` includes class clearances, so the DRC search distance covers them.
  - `validateDesignRules` rejects negative or non-finite class clearances and values above 100 mm.
- **Storage:** `netClasses[].clearance` is written only when it is greater than 0. It is optional
  and additive, like the rest of the `rules` object: files without it read as 0, so no
  `formatVersion` bump.
  - An older build ignores the key. It then checks and pours with the rule clearances only, just
    as it ignores net classes altogether before this ADR.
- **DRC:**
  - Conductor pairs and pour-to-copper checks require `netPairClearance` of the two groups' single
    nets.
  - A gap below the rule stays `drc.clearance`. A gap that meets the rule but not the class
    clearance is the new `drc.net-class-clearance` (Error).
- **Pours:**
  - `ZonePourOptions::netClearances` grows each obstacle by `netPairClearance` of the zone's net
    and the obstacle's net.
  - `pourOptionsFor(rules, schematic)` fills it. The canvas, CAM and print layout use this
    overload, and the DRC sets it too, so they all see the same copper.
- **Routing:**
  - `boardRouteClasses(schematic, board, rules)` (BoardCopper.hpp) maps each pad, via
    (`routeClassKey(itemId, padIndex)`) and track (item id) whose touching group has one net to a
    `RouteClass { net, netClass, traceWidth, clearance }`. The clearance is the larger of the
    trace-to-trace rule and the class clearance.
  - `MainWindow::refreshRouteClasses` hands the map to `DesignCanvas::setRouteClasses` while board
    track mode is active (on entering the mode, on document edits and after the rules change).
  - `DesignCanvas::beginBoardRoute` then starts a track on class copper at the class trace width
    instead of the chosen track style. The width is still capped at 60 % of the pad's narrow side
    and by a narrower track it branches from. The router keeps the class clearance from other
    copper.
  - The canvas caption shows "Net <net> (<class>)" while such a route is drawn. A track started in
    free space, or on copper without a single known net, keeps the chosen style and the rule
    clearance.
- **UI:** the Net Classes tab has `NetClassClearance` ("design rules" at 0).

## Consequences

- Rules can be tightened per copper layer without touching the others; necked tracks near fine
  pitch pads are allowed down to the class neck width.
- Class clearances apply between different nets in both directions (the larger class wins). They
  are not per layer and do not change the board edge clearance.
- Differential pair routing/length checks, class-to-class clearance matrices, inner layers and
  design rule waivers remain future work.
- Tests:
  - `DesignChecksTests::clearanceRulesByRegionAndObject`
  - `netClassesAssignPowerSignalAndExplicitNets`
  - `designRulesJsonRoundTrip`
  - `drcUsesRegionRulesAndNetClasses`
  - `netClassClearanceIsCheckedPouredAndRouted`
  - `DesignCanvasTests::pcbRouteUsesNetClassWidthAndClearance`
  - `MainWindowTests::designRuleManagerEditsRulesClassesPairsAndDefaults`
  - `trackModeRoutesWithNetClassWidths`
