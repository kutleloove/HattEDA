# ADR-0012: Zone Mode, Zone Kinds and Fill Styles

**Status:** Accepted
**Date:** 2026-09-15
**Extends:** ADR-0009 (copper zone pour), ADR-0010 (design rules, net classes), ADR-0004 (`.hatt`)
**Issue:** #34

## Context

Copper zones were drawn with the generic Polyline tool from the 2D graphics list. Nothing on the
screen showed which zones a board had, which net each pours or how it is filled. Proteus ARES users
expect:
- a separate Zone mode;
- a zone list with rows such as "GND=POWER, Solid";
- keepout areas that pours and copper must avoid;
- filled non-copper areas (silkscreen, solder resist, paste);
- fill styles other than solid.

## Decision

### Zone kinds (`SketchModel.hpp`)

Zones stay closed `Polyline` items. Their variant names the kind:
- `copper-zone` (`CopperZoneVariant`, unchanged): on a copper layer, poured for its `net`
  (ADR-0009).
- `keepout-zone` (`KeepoutZoneVariant`): on a copper layer. It never conducts.
  - Pours of that layer are cut by the keepout polygon (`fillZones`).
  - The DRC reports every track, pad, via or unpoured zone that touches it on that layer as
    `drc.keepout` (Error). The row reveals both the keepout and the copper.
  - The interactive router treats it as an obstacle, like a copper zone.
  - It is not fabricated.
- `area-zone` (`AreaZoneVariant`): a filled area on a silk, resist or paste layer, never on
  copper or the board edge.
  - `areaZoneFills` builds its fill without clearances.
  - CAM writes the fill as regions on the zone's layer before the other primitives of that layer,
    with holes in clear polarity, like pours.
  - A filled resist area is a mask opening; a filled paste area is a stencil opening.

`isZoneVariant` covers all three kinds for hit testing, placement and package building.

### Fill style (`SketchItem::zoneFill`)

`ZoneFillStyle { Solid, Hatched, Empty }` applies to copper and area zones. The file tokens are
`solid`, `hatched` and `empty`.
- **Solid:** the ADR-0009 pour, or the whole area.
- **Hatched:** the solid result clipped to `hatchedArea`. That is horizontal and vertical bars of
  `hatchWidth` (0.3 mm) on a `hatchPitch` (1.0 mm) grid aligned to the origin, plus a border of
  `hatchWidth` along every edge. For pours, hatching comes after thermal reliefs, the minimum
  width opening and island removal, so the border keeps spokes and pads joined.
- **Empty:** only the boundary.
  - An Empty copper zone is not poured. `itemCopperLayers` returns 0, so it is not a conductor,
    gets no `drc.zone-unfilled` and is neither exported nor counted as a skipped zone.
  - An Empty area zone is not fabricated.

### Zone mode (UI)

- `hatteda.tool.zone`: "Zone mode", shortcut Z, icon `zone`.
  - It joins the exclusive tool group after Pad mode.
  - Like Package, Via and Pad mode, it is Kayra only. Switching to Mergen returns to Select.
- The 2D graphics list no longer offers "Copper zone"; "Board outline" stays there.
- `ObjectSelector` rows set `CanvasTool::Zone` with the kind as variant: Copper zone, Keepout zone
  and Area zone (silk, resist, paste). The row icons are `zone`, `keepout` and `area`.
- `CanvasTool::Zone` clicks corners like Polyline. Clicking the first corner, double-click or
  Enter closes the zone.
  - `DesignCanvas::zoneLayer` puts copper and keepout zones on the active copper layer, and area
    zones on the active non-copper layer (the top silk when that is the board edge).
- `ZoneList` (under `ZONES`, visible in Kayra zone mode) has one row per zone:
  - the text is `zoneSummary` plus `  ·  <layer>`;
  - `zoneSummary` gives "GND=POWER, Solid" (net and `netClassForNet` class), "No net, Empty",
    "Keepout" or "Area, Hatched";
  - clicking a row selects the zone (`revealItems`), and double-clicking opens its properties;
  - the selected board zone is the current row;
  - the list is rebuilt in `refreshZoneFills`, which runs when either document or the design
    rules change.
- Item properties:
  - copper zones: copper layer, `ItemZoneNet` and `ItemZoneFill`;
  - keepouts: copper layer only;
  - area zones: a silk, resist or paste layer and `ItemZoneFill`.
- The canvas draws:
  - pours and area fills from `setZoneFills` (the host adds `areaZoneFills`);
  - keepouts with a diagonal hatch brush;
  - Empty zones as their outline only.

### Storage (`.hatt` format version 4)

- `zoneFill` is written only when it is not `solid`. Wrong types or unknown tokens reject the file.
- Version 3 readers would read keepout and area zones as copper or silkscreen outlines, and hatched
  or empty zones as solid pours. So these features change the meaning of existing data.
- `requiredFormatVersion(project)` writes version 4 only when the project has a keepout zone, an
  area zone or a zone fill other than Solid. Every other project still writes version 3, byte for
  byte as before, and opens in older builds. `ProjectFormatVersion` (4) is the newest version this
  build reads.

## Consequences

- Keepouts apply to one copper layer; a keepout for both sides is two zones.
- Keepouts do not block component placement; the DRC only reports the copper inside them.
- Hatch pitch and width are fixed defaults, like the thermal settings. They are not yet in the
  Design Rule Manager.
- Area zones have no clearance to pads; resist and paste openings are drawn as given.
- Zone priority, per-zone clearance and converting a zone between kinds remain future work.
- Tests:
  - `ZoneFillTests::emptyZonesAreNotPouredAndDoNotConduct`
  - `hatchedPourKeepsBarsAndBorder`
  - `keepoutZonesCutPoursOnTheirLayer`
  - `keepoutZonesReportCopperInside`
  - `areaZonesFillTheirLayerAndExport`
  - `zoneSummaryShowsNetClassAndFill`
  - `zoneKindsAndFillRoundTripWithFormatVersion`
  - `MainWindowTests::zoneModeDrawsZonesAndListsThem`
  - `ProjectFileTests::writesVersionedHeaderAndOmitsDefaults` (version 3 without zone features)
