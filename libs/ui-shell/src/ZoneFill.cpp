#include "hatt/ui/ZoneFill.hpp"

#include <QPainterPathStroker>

#include <algorithm>

namespace hatt::ui {
namespace {

QPainterPath polygonPath(const QVector<QPointF>& points) {
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    if (points.size() >= 3) path.addPolygon(QPolygonF(points + QVector<QPointF>{points.first()}));
    return path;
}

QPainterPath strokePath(const QVector<QPointF>& points, double width, bool closed) {
    QPainterPath line;
    if (points.isEmpty()) return line;
    line.moveTo(points.first());
    for (qsizetype i = 1; i < points.size(); ++i) line.lineTo(points[i]);
    if (closed) line.closeSubpath();
    if (points.size() == 1) line.lineTo(points.first() + QPointF(1e-6, 0.0));
    QPainterPathStroker stroker;
    stroker.setWidth(width);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(line);
}

// Grows a filled shape by `distance` on every side.
QPainterPath grown(const QPainterPath& shape, double distance) {
    if (distance <= 0.0) return shape;
    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * distance);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return shape.united(stroker.createStroke(shape));
}

// Shrinks a filled shape by `distance` on every side.
QPainterPath shrunk(const QPainterPath& shape, double distance) {
    if (distance <= 0.0) return shape;
    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * distance);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return shape.subtracted(stroker.createStroke(shape));
}

QVector<QPointF> closedOutline(const SketchItem& item) {
    if (item.kind == SketchItem::Kind::Rectangle && item.points.size() >= 2) {
        const QRectF rect = QRectF(item.points[0], item.points[1]).normalized();
        return {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
    }
    return item.points;
}

} // namespace

QVector<ZoneObstacle> boardCopperObstacles(const SketchDocument& board) {
    QVector<ZoneObstacle> obstacles;
    for (const SketchItem& item : board) {
        if (item.points.isEmpty()) continue;
        switch (item.kind) {
        case SketchItem::Kind::Symbol:
        case SketchItem::Kind::Pad:
        case SketchItem::Kind::Via:
            for (const PlacedPad& pad : itemPads(item)) {
                obstacles.append({item.id, polygonPath(padOutline(pad)), pad.layers, {}});
            }
            break;
        case SketchItem::Kind::Wire:
            if (isCopperLayer(item.layer)) {
                obstacles.append({item.id, strokePath(item.points, trackWidth(item), false), layerBit(item.layer), {}});
            }
            break;
        case SketchItem::Kind::Line:
        case SketchItem::Kind::Polyline:
        case SketchItem::Kind::Rectangle:
            if (isCopperLayer(item.layer) && item.variant != CopperZoneVariant && item.variant != BoardOutlineVariant) {
                obstacles.append({item.id, strokePath(closedOutline(item), 0.254, item.closed), layerBit(item.layer), {}});
            }
            break;
        default:
            break;
        }
    }
    return obstacles;
}

QVector<ZoneFillResult> fillZones(const SketchDocument& board, const QVector<ZoneObstacle>& obstacles,
                                  double clearance, double boardEdgeClearance) {
    QPainterPath boardArea;
    for (const SketchItem& item : board) {
        if (item.variant == BoardOutlineVariant && item.points.size() >= 3) {
            boardArea = boardArea.united(polygonPath(closedOutline(item)));
        }
    }
    if (!boardArea.isEmpty()) boardArea = shrunk(boardArea, boardEdgeClearance);

    QVector<ZoneFillResult> results;
    for (const SketchItem& zone : board) {
        if (zone.variant != CopperZoneVariant || zone.net.isEmpty() || !isCopperLayer(zone.layer) ||
            zone.points.size() < 3) {
            continue;
        }
        QPainterPath fill = polygonPath(zone.points);
        if (!boardArea.isEmpty()) fill = fill.intersected(boardArea);
        const QRectF reach = fill.boundingRect().adjusted(-clearance, -clearance, clearance, clearance);
        QPainterPath keepOut;
        for (const ZoneObstacle& obstacle : obstacles) {
            if ((obstacle.layers & layerBit(zone.layer)) == 0) continue;
            if (!obstacle.net.isEmpty() && obstacle.net == zone.net) continue;
            if (!obstacle.outline.boundingRect().intersects(reach)) continue;
            keepOut = keepOut.united(grown(obstacle.outline, clearance));
        }
        if (!keepOut.isEmpty()) fill = fill.subtracted(keepOut);
        fill = fill.simplified();
        fill.setFillRule(Qt::OddEvenFill);
        results.append({zone.id, zone.layer, zone.net, fill});
    }
    return results;
}

QVector<ZoneContour> zoneContours(const QPainterPath& fill) {
    // One contour per subpath. toFillPolygons() would join holes to their outline with a bridge,
    // which is not a valid Gerber region.
    QVector<ZoneContour> contours;
    const QPainterPath simple = fill.simplified();
    QPolygonF current;
    auto finish = [&] {
        if (current.size() > 3 && current.first() == current.last()) current.removeLast();
        if (current.size() >= 3) contours.append({current, 0});
        current.clear();
    };
    const QList<QPolygonF> flattened = simple.toSubpathPolygons();
    for (const QPolygonF& subpath : flattened) {
        current = subpath;
        finish();
    }
    for (int i = 0; i < contours.size(); ++i) {
        const QPointF probe = contours[i].polygon.first();
        const QRectF bounds = contours[i].polygon.boundingRect();
        for (int j = 0; j < contours.size(); ++j) {
            if (i == j) continue;
            const QRectF other = contours[j].polygon.boundingRect();
            // A contour lies inside another when the other's bounds contain it and its vertex is inside.
            if (other.contains(bounds) && other != bounds &&
                contours[j].polygon.containsPoint(probe, Qt::OddEvenFill)) {
                ++contours[i].depth;
            }
        }
    }
    std::stable_sort(contours.begin(), contours.end(),
                     [](const ZoneContour& a, const ZoneContour& b) { return a.depth < b.depth; });
    return contours;
}

} // namespace hatt::ui
