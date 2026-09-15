#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QHash>
#include <QPainterPath>
#include <QPair>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace hatt::ui {

// Board copper as conductors with schematic nets (ADR-0008). Shared by the DRC and copper zone
// pouring: both need the same geometry, layers and net assignment.

// One piece of copper geometry in world millimetres: a segment widened by `radius` (tracks, round
// and oval pads, vias) or, when `polygon` is not empty, a polygon (rectangular pads, zones).
struct CopperShape {
    QPointF a;
    QPointF b;
    double radius = 0.0;
    QPolygonF polygon;
};

// Distance between the copper of two shapes; 0 when they touch or overlap.
[[nodiscard]] double copperShapeGap(const CopperShape& first, const CopperShape& second);
[[nodiscard]] QRectF copperShapeBounds(const CopperShape& shape);
// Filled outline of the shape grown by `expansion` on every side (rounded corners).
[[nodiscard]] QPainterPath copperShapePath(const CopperShape& shape, double expansion = 0.0);

enum class ConductorKind { Track, Pad, Via, Zone };

struct BoardConductor {
    ConductorKind kind = ConductorKind::Track;
    QString itemId;
    int padIndex = -1;    // index in itemPads for pads and vias
    QString name;         // translated: "R1.2", "track", "via", "pad 1", "copper zone"
    int layers = 0;       // copper layer mask
    int net = -1;         // schematic net of a linked footprint pad, else -1
    QPointF anchor;       // a point on the copper, for report locations
    QVector<CopperShape> shapes;
    QRectF bounds;
    double width = 0.0;   // tracks: copper width
    PlacedPad pad;        // pads and vias: the placed pad
};

struct BoardCopperModel {
    QVector<BoardConductor> conductors;
    // Schematic nets; `netsKnown` is false when the schematic has connectivity errors, then every
    // conductor has net -1.
    bool netsKnown = false;
    QStringList netNames;
    // Root conductor index of each conductor's touching group: copper that touches on a shared
    // copper layer conducts (tracks crossing or ending in a pad, zones over copper).
    QVector<int> groups;
    // Conductors on a shared layer closer than the requested distance without touching.
    QVector<QPair<int, int>> nearPairs;
    // Ids of schematic board components that have a footprint on the board.
    QStringList placedSources;

    // Schematic nets of a group (pads of linked footprints in it).
    [[nodiscard]] QVector<int> groupNets(int root) const;
};

// Builds the conductors of `board`. Tracks are Wire items on a copper layer, pads come from
// footprints, Pad and Via items (itemPads), zones are closed copper-zone polylines treated as solid
// polygons. `nearDistance` (e.g. the clearance) fills `nearPairs`; 0 skips that search.
[[nodiscard]] BoardCopperModel buildBoardCopperModel(const SketchDocument& schematic, const SketchDocument& board,
                                                     double nearDistance = 0.0);
[[nodiscard]] double conductorGap(const BoardConductor& first, const BoardConductor& second, QPointF* where = nullptr);

struct DesignRules;
struct RouteClass;
// Net class routing of the board's copper (issue #39), keyed by routeClassKey: every pad, via and
// track whose touching group has exactly one schematic net gets that net's class trace width and
// clearance (at least the trace-to-trace rule on its layers). Empty when the nets are unknown.
[[nodiscard]] QHash<QString, RouteClass> boardRouteClasses(const SketchDocument& schematic, const SketchDocument& board,
                                                           const DesignRules& rules);
// "<item id>:<pad index>" for pads and vias (index in itemPads), the item id for tracks.
[[nodiscard]] QString routeClassKey(const QString& itemId, int padIndex = -1);

} // namespace hatt::ui
