# ADR-0008: Design Checks (ERC and DRC) and Project Design Rules

**Status:** Accepted
**Date:** 2026-09-14
**Issue:** #31 (closes the ERC/DRC part of #7)

## Context

`hatteda.action.run-checks` existed as a disabled placeholder. The connection guide
(`boardGuidance`, ADR-0003) only draws airwires and lists a few blocking errors; it does not
check manufacturability (clearance, widths, holes, board edge) or schematic hygiene (unconnected
pins, dangling wires). Proteus/KiCad users expect an ERC report for the schematic, a DRC report
for the board, rules stored with the project and a click on a problem that shows it.

## Decision

### 1. Checks are pure functions over snapshots

`DesignChecks.hpp` (ui-shell, Qt Core/Gui types only, no widgets):

- `runElectricalRuleCheck(schematic)` and `runDesignRuleCheck(schematic, board, rules)` return a
  `CheckReport` of `CheckViolation { severity, workspace, rule, message, hasLocation, location,
  itemIds }`. They never modify documents and can run on any snapshot (tests, future CLI/CI).
- Rule ids are stable strings (`erc.*`, `drc.*`, listed in the header) so reports, filters and
  future waivers can refer to them; messages are translated.
- ERC reuses `analyzeSchematic` (the same connectivity as netlist, transfer and simulation).
  Connectivity errors (conflicting net names) are errors; unconnected pins, unused terminals,
  shorted components, dangling wire ends and missing values are warnings; a board component
  without a matching footprint or with an invalid pin-to-pad map is an error unless it is
  excluded from the board.
- DRC builds its own copper model from the #28/#30 helpers (`itemPads`, `padOutline`,
  `trackWidth`, `viaDiameter`, `viaDrill`, `itemCopperLayers`): tracks are segments widened by
  half their width, round/oval pads and vias are capsules, rectangular pads are polygons. Copper
  that touches on a shared copper layer is one conductor (union-find), which also covers tracks
  that cross or end inside a pad. Conductor groups get schematic nets from linked footprint pads
  (`sourceId` + `pinPadMap`). From that:
  - `drc.short`: a group with more than one schematic net;
  - `drc.clearance`: copper of different groups closer than `clearance` on a shared layer, except
    unrouted pads of the same single net;
  - `drc.net-class-clearance`: copper that meets the clearance rule but is closer than the net
    class clearance of either net (ADR-0010 amendment, issue #39);
  - `drc.unrouted`: a net whose pads are in more than one group;
  - `drc.track-width`, `drc.drill`, `drc.annular-ring` from the rules;
  - `drc.board-edge`: copper outside the closed outline or nearer than `boardEdgeClearance`;
  - `drc.overlap`: footprint bodies (silkscreen + pads) on the same side overlapping;
  - `drc.not-placed`, `drc.no-outline`, and `drc.netlist` when schematic errors hide net data.
- Copper zones (ADR-0009):
  - A zone with a net is checked as its pour (`pourZones` with the project clearance and board
    edge clearance, thermal reliefs and island removal). The fill joins every conductor it
    touches, so a ground pour routes its net.
  - Copper of another net that the fill touches is `drc.zone-short`.
  - Copper of another net closer than `clearance` is `drc.clearance`; a tolerance of
    max(0.02 mm, 10 % of the clearance) absorbs the curve flattening of grown round outlines.
  - A pour with no copper is `drc.zone-empty` (Warning).
  - A zone without a pour (no net) stays a solid polygon conductor on its layer. It gets
    `drc.zone-unfilled` (Warning), and joining nets through it is `drc.zone-short` rather than
    `drc.short`.
  - Zones are left out of the conductor-to-conductor clearance pairs and the board edge check;
    the pour already keeps the edge clearance.
  - Copper touching a keepout zone on the keepout's copper layer is `drc.keepout` (Error,
    ADR-0012). Empty copper zones are only a boundary and are not conductors.

### 2. Report workspace

`ChecksReport` is a tool workspace (`hatteda.tool.design-checks`, `ChecksTable`, `ChecksSummary`,
`ChecksRerun`), errors first. Activating a row switches to the violation's workspace and calls
`DesignCanvas::revealItems(ids, location)`, which selects the items and centres the view without
changing zoom. Running the checks again reuses the open workspace.

### 3. Design rules in the project

`DesignRules { clearance 0.2, minTrackWidth 0.15, minDrill 0.3, minAnnularRing 0.13,
boardEdgeClearance 0.3 }` (mm, defaults suitable for common 2-layer prototype services) is part
of `ProjectData` and edited under `hatteda.action.design-rules` (Design menu; since ADR-0010 the
Design Rule Manager, which adds region clearance rules, net classes, differential pairs and defaults).
`.hatt` stores it as an additive top-level object; no `formatVersion` bump:

```json
"rules": {"clearance": 0.2, "minTrackWidth": 0.15, "minDrill": 0.3,
          "minAnnularRing": 0.13, "boardEdgeClearance": 0.3}
```

A missing object or key keeps the default; wrong types, negative or non-finite values, values
above 100 mm, and a zero clearance or track width reject the file (`validateDesignRules`).
Editing the rules marks the project modified.

## Consequences

- DRC is O(n²) over copper objects with a bounding-box prefilter; fine for MVP boards, a spatial
  index is needed for large designs.
- Per-net-class rules, waivers, courtyards separate from silkscreen, edge connector exceptions,
  zone fills and differential pairs are future work; adding a waiver list or net classes needs a
  follow-up ADR because it changes stored fields.
- Tests: `hatt-design-checks-tests`, `ProjectFileTests::designRulesRoundTrip`,
  `MainWindowTests::designChecksReportAndRules`.
