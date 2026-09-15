#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QPainterPath>
#include <QPolygonF>
#include <QString>
#include <QVector>

namespace hatt::ui {

// Copper pour for Kayra copper zones (ADR-0009). A zone with a net is filled on its copper layer:
// the zone outline, kept `boardEdgeClearance` inside the board outline when one exists, minus the
// copper of every other net grown by `clearance`. Pads of the zone's own net connect through thermal
// reliefs (a gap ring crossed by four spokes); tracks and vias of the net join solidly. Slivers
// narrower than the minimum width and pour islands that touch no copper of the net are removed.
// A zone without a net is not filled.
// Zone kinds and fill styles (ADR-0012): Empty copper zones are not poured, Hatched pours keep a grid
// of bars inside a border, keepout zones on the pour's layer are cut out of it, and area zones on
// silk, resist or paste layers are filled without clearances (areaZoneFills).
// Coordinates are editor millimetres (Y down).

struct ZoneObstacle {
    QString itemId;
    QPainterPath outline; // copper shape in world mm
    int layers = 0;       // BoardLayer mask the copper conducts on
    QString net;          // empty = unknown net, always kept clear of pours
    bool pad = false;     // pads (not tracks or vias) of the zone's net get thermal reliefs
};

struct ZonePourOptions {
    double clearance = 0.2;
    double boardEdgeClearance = 0.3;
    bool thermalReliefs = true;
    double thermalGap = 0.3;   // mm between a pad and the pour, at least the clearance
    double spokeWidth = 0.4;   // mm
    bool removeIslands = true; // drop pour regions without copper of the zone's net
    // Pour parts narrower than this are removed (shrink by half, grow back, never beyond the pour),
    // so fabrication never gets copper slivers; 0 keeps them. Spokes must be at least this wide.
    double minimumWidth = 0.25;
    // Hatched zones: bar pitch and the width of the bars and of the border (mm).
    double hatchPitch = 1.0;
    double hatchWidth = 0.3;
};

struct DesignRules;
// Pour settings from the project's design rules (DesignRules.hpp, ADR-0010): graphic-to-pad clearance
// and edge clearance over both copper layers, and the thermal defaults. The canvas, CAM, print
// layout and DRC all pour with these so they see the same copper.
[[nodiscard]] ZonePourOptions pourOptionsFor(const DesignRules& rules);

struct ZoneFillResult {
    QString zoneId;
    BoardLayer layer = BoardLayer::TopCopper;
    QString net;
    QPainterPath fill; // simplified, odd-even
};

// Copper of pads, footprint pads, vias, tracks and copper graphics, without nets. Callers that know
// the netlist set ZoneObstacle::net.
[[nodiscard]] QVector<ZoneObstacle> boardCopperObstacles(const SketchDocument& board);

[[nodiscard]] QVector<ZoneFillResult> fillZones(const SketchDocument& board, const QVector<ZoneObstacle>& obstacles,
                                                const ZonePourOptions& options);
// Solid pour without thermal reliefs or island removal.
[[nodiscard]] QVector<ZoneFillResult> fillZones(const SketchDocument& board, const QVector<ZoneObstacle>& obstacles,
                                                double clearance, double boardEdgeClearance);

// Copper with nets from the board copper model (BoardCopper.hpp). Zones are left out of the model,
// so a zone never merges the groups it covers. A group's net is its single schematic net; a group
// without nets gets an empty net and a group joining several nets (a short) a name that matches no
// zone, so pours keep clear of both.
[[nodiscard]] QVector<ZoneObstacle> netCopperObstacles(const SketchDocument& schematic, const SketchDocument& board);
// Pours every zone of `board` with thermal reliefs and island removal.
[[nodiscard]] QVector<ZoneFillResult> pourZones(const SketchDocument& schematic, const SketchDocument& board,
                                                const ZonePourOptions& options);
// Same with the default thermal and island settings and the project's clearances.
[[nodiscard]] QVector<ZoneFillResult> pourZones(const SketchDocument& schematic, const SketchDocument& board,
                                                double clearance, double boardEdgeClearance);

// Hatch of a filled shape: horizontal and vertical bars of `width` on a `pitch` grid aligned to the
// origin, plus a border of `width` along every edge, all clipped to the shape.
[[nodiscard]] QPainterPath hatchedArea(const QPainterPath& area, double pitch, double width);

// Filled areas of the non-copper area zones (AreaZoneVariant) on their own layer: the zone polygon
// for Solid, its hatch for Hatched; Empty zones are left out (only their boundary is drawn).
[[nodiscard]] QVector<ZoneFillResult> areaZoneFills(const SketchDocument& board, const ZonePourOptions& options = {});

// One zone list row (Proteus ARES style): "GND=POWER, Solid" for a copper zone with a net and its
// net class, "No net, Empty" without one, "Keepout" and "Area, Hatched" for the non-copper kinds.
// The layer is not part of the summary.
[[nodiscard]] QString zoneSummary(const SketchItem& zone, const DesignRules& rules, const SketchDocument& schematic);
// Translated kind of a zone item: copper zone, keepout zone or area zone.
[[nodiscard]] QString zoneKindName(const QString& variant);

// Contours of a fill with their nesting depth: even depth adds copper, odd depth removes it.
// Drawing contours in increasing depth reproduces the fill (used for Gerber polarity).
struct ZoneContour {
    QPolygonF polygon;
    int depth = 0;
};
[[nodiscard]] QVector<ZoneContour> zoneContours(const QPainterPath& fill);

} // namespace hatt::ui
