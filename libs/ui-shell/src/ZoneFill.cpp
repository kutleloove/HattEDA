#include "hatt/ui/ZoneFill.hpp"

#include "hatt/ui/BoardCopper.hpp"
#include "hatt/ui/DesignRules.hpp"

#include <QCoreApplication>
#include <QPainterPathStroker>

#include <algorithm>
#include <cmath>

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
                obstacles.append({item.id, polygonPath(padOutline(pad)), pad.layers, {},
                                  item.kind != SketchItem::Kind::Via});
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
            if (isCopperLayer(item.layer) && !isZoneVariant(item.variant) && item.variant != BoardOutlineVariant) {
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
                                  const ZonePourOptions& options) {
    const double clearance = options.clearance;
    QPainterPath boardArea;
    for (const SketchItem& item : board) {
        if (item.variant == BoardOutlineVariant && item.points.size() >= 3) {
            boardArea = boardArea.united(polygonPath(closedOutline(item)));
        }
    }
    if (!boardArea.isEmpty()) boardArea = shrunk(boardArea, options.boardEdgeClearance);
    // Keepout zones by copper layer: no pour enters them.
    QPainterPath keepouts[BoardLayerCount];
    for (const SketchItem& item : board) {
        if (item.variant == KeepoutZoneVariant && isCopperLayer(item.layer) && item.points.size() >= 3) {
            QPainterPath& keepout = keepouts[static_cast<int>(item.layer)];
            keepout = keepout.united(polygonPath(item.points));
        }
    }

    QVector<ZoneFillResult> results;
    for (const SketchItem& zone : board) {
        if (zone.variant != CopperZoneVariant || zone.net.isEmpty() || zone.zoneFill == ZoneFillStyle::Empty ||
            !isCopperLayer(zone.layer) || zone.points.size() < 3) {
            continue;
        }
        QPainterPath fill = polygonPath(zone.points);
        if (!boardArea.isEmpty()) fill = fill.intersected(boardArea);
        if (const QPainterPath& keepout = keepouts[static_cast<int>(zone.layer)]; !keepout.isEmpty()) {
            fill = fill.subtracted(keepout);
        }
        double largestGap = clearance;
        for (const double classGap : options.netClearances) largestGap = std::max(largestGap, classGap);
        const double reachDistance = std::max(largestGap, options.thermalGap) + options.spokeWidth;
        const QRectF reach =
            fill.boundingRect().adjusted(-reachDistance, -reachDistance, reachDistance, reachDistance);
        QPainterPath keepOut;
        QVector<const ZoneObstacle*> ownCopper;
        for (const ZoneObstacle& obstacle : obstacles) {
            if ((obstacle.layers & layerBit(zone.layer)) == 0) continue;
            if (!obstacle.outline.boundingRect().intersects(reach)) continue;
            if (!obstacle.net.isEmpty() && obstacle.net == zone.net) {
                ownCopper.append(&obstacle);
                continue;
            }
            keepOut = keepOut.united(
                grown(obstacle.outline, netPairClearance(options.netClearances, clearance, zone.net, obstacle.net)));
        }
        if (!keepOut.isEmpty()) fill = fill.subtracted(keepOut);

        if (options.thermalReliefs) {
            // Gap ring around each own-net pad, then four spokes back to the pour (only where the pour
            // already was, so spokes never cross another net's clearance).
            const QPainterPath allowed = fill;
            const double gap = std::max(clearance, options.thermalGap);
            QPainterPath gaps;
            QPainterPath spokes;
            for (const ZoneObstacle* copper : ownCopper) {
                if (!copper->pad) continue;
                gaps = gaps.united(grown(copper->outline, gap));
                const QRectF bounds = copper->outline.boundingRect();
                const QPointF centre = bounds.center();
                const double reachX = bounds.width() / 2.0 + gap + options.spokeWidth;
                const double reachY = bounds.height() / 2.0 + gap + options.spokeWidth;
                const double half = options.spokeWidth / 2.0;
                spokes.addRect(QRectF(centre.x() - reachX, centre.y() - half, 2.0 * reachX, 2.0 * half));
                spokes.addRect(QRectF(centre.x() - half, centre.y() - reachY, 2.0 * half, 2.0 * reachY));
            }
            if (!gaps.isEmpty()) {
                spokes.setFillRule(Qt::WindingFill);
                fill = fill.subtracted(gaps).united(spokes.simplified().intersected(allowed));
            }
        }
        fill = fill.simplified();
        fill.setFillRule(Qt::OddEvenFill);

        if (options.minimumWidth > 0.0 && !fill.isEmpty()) {
            // Morphological opening; intersecting with the pour keeps stroker approximations from
            // growing copper back into a clearance.
            const double half = options.minimumWidth / 2.0;
            QPainterPath opened = grown(shrunk(fill, half), half).intersected(fill).simplified();
            opened.setFillRule(Qt::OddEvenFill);
            fill = opened;
        }

        if (options.removeIslands) {
            // Rebuild the pour from its regions (an even-depth contour minus the holes right inside it),
            // keeping only regions that touch copper of the zone's net.
            const QVector<ZoneContour> contours = zoneContours(fill);
            QPainterPath kept;
            kept.setFillRule(Qt::OddEvenFill);
            for (const ZoneContour& outer : contours) {
                if (outer.depth % 2 != 0) continue;
                QPainterPath region = polygonPath(outer.polygon);
                for (const ZoneContour& hole : contours) {
                    if (hole.depth != outer.depth + 1) continue;
                    if (!outer.polygon.containsPoint(hole.polygon.first(), Qt::OddEvenFill)) continue;
                    region = region.subtracted(polygonPath(hole.polygon));
                }
                const bool connected = std::any_of(ownCopper.begin(), ownCopper.end(), [&region](const ZoneObstacle* c) {
                    return region.intersects(c->outline);
                });
                if (connected) kept = kept.united(region);
            }
            fill = kept.simplified();
            fill.setFillRule(Qt::OddEvenFill);
        }
        if (zone.zoneFill == ZoneFillStyle::Hatched) fill = hatchedArea(fill, options.hatchPitch, options.hatchWidth);
        results.append({zone.id, zone.layer, zone.net, fill});
    }
    return results;
}

QVector<ZoneFillResult> fillZones(const SketchDocument& board, const QVector<ZoneObstacle>& obstacles,
                                  double clearance, double boardEdgeClearance) {
    ZonePourOptions options;
    options.clearance = clearance;
    options.boardEdgeClearance = boardEdgeClearance;
    options.thermalReliefs = false;
    options.removeIslands = false;
    options.minimumWidth = 0.0;
    return fillZones(board, obstacles, options);
}

QVector<ZoneObstacle> netCopperObstacles(const SketchDocument& schematic, const SketchDocument& board) {
    SketchDocument withoutZones;
    withoutZones.reserve(board.size());
    bool hasZone = false;
    for (const SketchItem& item : board) {
        if (isZoneVariant(item.variant)) {
            hasZone = hasZone || item.variant == CopperZoneVariant;
            continue;
        }
        withoutZones.append(item);
    }
    QVector<ZoneObstacle> obstacles;
    if (!hasZone) return obstacles;
    const BoardCopperModel model = buildBoardCopperModel(schematic, withoutZones);
    for (qsizetype i = 0; i < model.conductors.size(); ++i) {
        const BoardConductor& conductor = model.conductors[i];
        if (conductor.kind == ConductorKind::Zone) continue;
        ZoneObstacle obstacle;
        obstacle.itemId = conductor.itemId;
        obstacle.layers = conductor.layers;
        obstacle.pad = conductor.kind == ConductorKind::Pad;
        if (model.netsKnown) {
            const QVector<int> nets = model.groupNets(model.groups.value(i, static_cast<int>(i)));
            if (nets.size() == 1) {
                obstacle.net = model.netNames.value(nets.first());
            } else if (nets.size() > 1) {
                obstacle.net = QStringLiteral("\x01short"); // never a zone net
            }
        }
        for (const CopperShape& shape : conductor.shapes) {
            obstacle.outline = obstacle.outline.united(copperShapePath(shape));
        }
        obstacles.append(obstacle);
    }
    return obstacles;
}

QVector<ZoneFillResult> pourZones(const SketchDocument& schematic, const SketchDocument& board,
                                  const ZonePourOptions& options) {
    const bool anyNet = std::any_of(board.begin(), board.end(), [](const SketchItem& item) {
        return item.variant == CopperZoneVariant && !item.net.isEmpty() && item.zoneFill != ZoneFillStyle::Empty;
    });
    if (!anyNet) return {};
    return fillZones(board, netCopperObstacles(schematic, board), options);
}

ZonePourOptions pourOptionsFor(const DesignRules& rules) {
    ZonePourOptions options;
    options.clearance = clearanceBetween(rules, CopperLayerMask, ClearanceObject::Graphic, ClearanceObject::Pad);
    options.boardEdgeClearance = edgeClearance(rules, CopperLayerMask);
    options.thermalReliefs = rules.defaults.thermalRelief;
    options.thermalGap = std::max(rules.defaults.thermalGap, options.clearance);
    options.spokeWidth = rules.defaults.spokeWidth;
    return options;
}

ZonePourOptions pourOptionsFor(const DesignRules& rules, const SketchDocument& schematic) {
    ZonePourOptions options = pourOptionsFor(rules);
    options.netClearances = netClassClearances(rules, schematic);
    return options;
}

QVector<ZoneFillResult> pourZones(const SketchDocument& schematic, const SketchDocument& board, double clearance,
                                  double boardEdgeClearance) {
    ZonePourOptions options;
    options.clearance = clearance;
    options.boardEdgeClearance = boardEdgeClearance;
    return pourZones(schematic, board, options);
}

QPainterPath hatchedArea(const QPainterPath& area, double pitch, double width) {
    if (area.isEmpty() || pitch <= 0.0 || width <= 0.0) return area;
    const QRectF bounds = area.boundingRect();
    const double half = width / 2.0;
    // Bars of one direction never overlap each other, so each set is a valid path as it is.
    QPainterPath horizontal;
    QPainterPath vertical;
    for (double y = std::floor(bounds.top() / pitch) * pitch; y <= bounds.bottom() + half; y += pitch) {
        horizontal.addRect(QRectF(bounds.left() - width, y - half, bounds.width() + 2.0 * width, width));
    }
    for (double x = std::floor(bounds.left() / pitch) * pitch; x <= bounds.right() + half; x += pitch) {
        vertical.addRect(QRectF(x - half, bounds.top() - width, width, bounds.height() + 2.0 * width));
    }
    const QPainterPath border = area.subtracted(shrunk(area, width));
    QPainterPath hatch =
        area.intersected(horizontal).united(area.intersected(vertical)).united(border).simplified();
    hatch.setFillRule(Qt::OddEvenFill);
    return hatch;
}

QVector<ZoneFillResult> areaZoneFills(const SketchDocument& board, const ZonePourOptions& options) {
    QVector<ZoneFillResult> results;
    for (const SketchItem& zone : board) {
        if (zone.variant != AreaZoneVariant || zone.zoneFill == ZoneFillStyle::Empty || isCopperLayer(zone.layer) ||
            zone.layer == BoardLayer::BoardEdge || zone.points.size() < 3) {
            continue;
        }
        QPainterPath fill = polygonPath(zone.points).simplified();
        fill.setFillRule(Qt::OddEvenFill);
        if (zone.zoneFill == ZoneFillStyle::Hatched) fill = hatchedArea(fill, options.hatchPitch, options.hatchWidth);
        results.append({zone.id, zone.layer, {}, fill});
    }
    return results;
}

QString zoneKindName(const QString& variant) {
    if (variant == KeepoutZoneVariant) return QCoreApplication::translate("hatt::ui::ZoneFill", "Keepout zone");
    if (variant == AreaZoneVariant) return QCoreApplication::translate("hatt::ui::ZoneFill", "Area zone");
    return QCoreApplication::translate("hatt::ui::ZoneFill", "Copper zone");
}

QString zoneSummary(const SketchItem& zone, const DesignRules& rules, const SketchDocument& schematic) {
    const QString style = zoneFillStyleName(zone.zoneFill);
    if (zone.variant == KeepoutZoneVariant) return QCoreApplication::translate("hatt::ui::ZoneFill", "Keepout");
    if (zone.variant == AreaZoneVariant) return QCoreApplication::translate("hatt::ui::ZoneFill", "Area, %1").arg(style);
    if (zone.net.isEmpty()) return QCoreApplication::translate("hatt::ui::ZoneFill", "No net, %1").arg(style);
    const NetClass netClass = netClassForNet(rules, schematic, zone.net);
    const QString net = netClass.name.isEmpty() ? zone.net : zone.net + QLatin1Char('=') + netClass.name;
    return QStringLiteral("%1, %2").arg(net, style);
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
