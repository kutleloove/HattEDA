#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QPainterPath>
#include <QPolygonF>
#include <QString>
#include <QVector>

namespace hatt::ui {

// Copper pour for Kayra copper zones (ADR-0009). A zone with a net is filled on its copper layer:
// the zone outline, kept `boardEdgeClearance` inside the board outline when one exists, minus the
// copper of every other net grown by `clearance`. Copper of the zone's own net stays connected
// (solid, no thermal reliefs yet). A zone without a net is not filled.
// Coordinates are editor millimetres (Y down).

struct ZoneObstacle {
    QString itemId;
    QPainterPath outline; // copper shape in world mm
    int layers = 0;       // BoardLayer mask the copper conducts on
    QString net;          // empty = unknown net, always kept clear of pours
};

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
                                                double clearance, double boardEdgeClearance);

// Contours of a fill with their nesting depth: even depth adds copper, odd depth removes it.
// Drawing contours in increasing depth reproduces the fill (used for Gerber polarity).
struct ZoneContour {
    QPolygonF polygon;
    int depth = 0;
};
[[nodiscard]] QVector<ZoneContour> zoneContours(const QPainterPath& fill);

} // namespace hatt::ui
