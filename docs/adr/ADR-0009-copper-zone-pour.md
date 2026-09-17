# ADR-0009: Copper Zone Pour

**Status:** Accepted
**Date:** 2026-09-14
**Related:** ADR-0006 (layers, pads), ADR-0008 (design checks, board copper model)

## Context

Copper zones (`Polyline` items with variant `copper-zone`) were plain outlines. Exported as solid
copper they shorted every net they covered, so fabrication export left them out and the DRC warned
that they were unfilled. A usable board needs ground and power pours that connect their own net and
keep the design rule clearance from everything else.

## Decision

### 1. Zone net: `SketchItem::net`

A copper zone names the net it is poured for in a new `SketchItem::net` string, written to `.hatt`
as `"net"` only when not empty. A non-string value is rejected on load. The field is additive: older
files read as zones without a net and older readers ignore it, so `formatVersion` stays 2. The net
is chosen in the zone's properties (`ItemZoneNet`) from the schematic's net names, or typed in.

### 2. Pour (`ZoneFill.hpp`)

`pourZones(schematic, board, clearance, boardEdgeClearance)` fills every zone that has a net and lies
on a copper layer:

- start from the zone polygon, intersected with the board outline shrunk by `boardEdgeClearance`
  when an outline exists;
- subtract the copper of every other net on the zone's layer grown by `clearance`
  (`QPainterPath` boolean operations with rounded `QPainterPathStroker` offsets);
- pads of the zone's own net connect through thermal reliefs: a gap ring of
  `max(clearance, thermalGap)` (default 0.3 mm) crossed by four spokes (`spokeWidth`, default
  0.4 mm) that stay inside the pour; own-net tracks and vias join solidly;
- parts narrower than `minimumWidth` (default 0.25 mm, a fixed default like the thermal settings) are
  removed by a morphological opening (shrink by half the width, grow back, intersect with the pour so
  it never grows into a clearance);
- pour regions (an outline minus the holes directly inside it) that touch no copper of the zone's
  net are removed as islands, so a zone whose net has no copper on the board pours nothing
  (`ZonePourOptions`).

Nets come from the shared board copper model (`BoardCopper.hpp`, ADR-0008), built without zones so a
zone never merges the groups it covers. A copper group takes its single schematic net; a group
without nets is treated as another net (kept clear) and a group joining several nets (a short) is
kept clear of every zone. Pours are derived data: they are recomputed by the host when either
document or the design rules change and are never stored.

### 3. Canvas and CAM

- `DesignCanvas::setZoneFills` receives the pours from `MainWindow::refreshZoneFills` and draws them
  under the board items of their side; zones with a net no longer get the outline wash.
- `CamOptions::zoneFills` exports pours. `zoneContours` splits a pour into nested contours; on each
  copper layer the pours are written first, even-depth contours as dark regions and odd-depth
  contours (holes) as clear-polarity (`%LPC*%`) regions, before every track and pad, so clear
  polarity never erases other copper. Zones without a pour are still left out of the Gerber files and
  reported (`FabricationZonesNotice`). The CAM preview renders each layer separately so holes show.

## Consequences

- Overlapping zones of different nets can clear each other's copper where their knockouts overlap;
  the DRC reports such shorts (`drc.zone-short`).
- The opening also rounds inside corners of the pour by half the minimum width. Spokes are axis
  aligned and can be cut by other nets' clearance, leaving a pad with fewer spokes; island removal
  still only drops regions that nothing of the net touches. The DRC evaluates the pour
  (`pourZones`) rather than the solid zone polygon.
- ADR-0012 adds a Zone mode, keepout zones that cut pours, non-copper area zones, and the Hatched
  and Empty fill styles. An Empty copper zone is not poured and does not conduct.
- Pouring cost grows with the copper near a zone; boards of MVP size pour in milliseconds.
