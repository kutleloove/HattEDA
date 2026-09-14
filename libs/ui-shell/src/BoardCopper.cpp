#include "hatt/ui/BoardCopper.hpp"

#include "hatt/ui/SketchCircuit.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QLineF>
#include <QPainterPathStroker>
#include <QSet>

#include <algorithm>
#include <limits>
#include <numeric>

namespace hatt::ui {
namespace {

// Conductor names keep the DesignChecks translation context they were introduced with.
QString tr(const char* text) { return QCoreApplication::translate("hatt::ui::DesignChecks", text); }

constexpr double Touch = 1e-6;

bool schematicComponent(const SketchItem& item) {
    const auto* symbol = findSymbol(item.variant);
    return item.kind == SketchItem::Kind::Symbol && symbol != nullptr &&
           symbol->workspace == Workspace::Schematic && symbol->category == SymbolCategory::Component;
}

double pointSegmentDistance(QPointF p, QPointF a, QPointF b) {
    const QPointF ab = b - a;
    const double length2 = QPointF::dotProduct(ab, ab);
    const double t = length2 <= 0 ? 0.0 : std::clamp(QPointF::dotProduct(p - a, ab) / length2, 0.0, 1.0);
    return QLineF(p, a + ab * t).length();
}

double segmentDistance(QPointF a, QPointF b, QPointF c, QPointF d) {
    if (QLineF(a, b).intersects(QLineF(c, d), nullptr) == QLineF::BoundedIntersection) return 0.0;
    return std::min({pointSegmentDistance(a, c, d), pointSegmentDistance(b, c, d),
                     pointSegmentDistance(c, a, b), pointSegmentDistance(d, a, b)});
}

double polygonSegmentDistance(const QPolygonF& polygon, QPointF a, QPointF b) {
    if (polygon.containsPoint(a, Qt::OddEvenFill) || polygon.containsPoint(b, Qt::OddEvenFill)) return 0.0;
    double best = std::numeric_limits<double>::infinity();
    for (qsizetype i = 0; i < polygon.size(); ++i) {
        best = std::min(best, segmentDistance(polygon[i], polygon[(i + 1) % polygon.size()], a, b));
    }
    return best;
}

CopperShape padShape(const PlacedPad& pad) {
    CopperShape shape;
    const double hw = pad.width / 2.0;
    const double hh = pad.height / 2.0;
    if (pad.shape == PadShape::Rect) {
        shape.polygon = QPolygonF(padOutline(pad));
        shape.a = shape.b = pad.center;
        return shape;
    }
    shape.radius = std::min(hw, hh);
    const QPointF axis = hw >= hh ? QPointF(hw - shape.radius, 0) : QPointF(0, hh - shape.radius);
    shape.a = pad.center - axis;
    shape.b = pad.center + axis;
    return shape;
}

struct Groups {
    std::vector<int> parent;
    explicit Groups(int count) : parent(count) { std::iota(parent.begin(), parent.end(), 0); }
    int root(int i) { return parent[i] == i ? i : parent[i] = root(parent[i]); }
    void join(int a, int b) { parent[std::max(root(a), root(b))] = std::min(root(a), root(b)); }
};

} // namespace

double copperShapeGap(const CopperShape& p, const CopperShape& q) {
    double core = 0.0;
    if (p.polygon.isEmpty() && q.polygon.isEmpty()) {
        core = segmentDistance(p.a, p.b, q.a, q.b);
    } else if (p.polygon.isEmpty()) {
        core = polygonSegmentDistance(q.polygon, p.a, p.b);
    } else if (q.polygon.isEmpty()) {
        core = polygonSegmentDistance(p.polygon, q.a, q.b);
    } else {
        core = std::numeric_limits<double>::infinity();
        for (const QPointF& corner : p.polygon) {
            if (q.polygon.containsPoint(corner, Qt::OddEvenFill)) return 0.0;
        }
        for (qsizetype i = 0; i < q.polygon.size(); ++i) {
            core = std::min(core, polygonSegmentDistance(p.polygon, q.polygon[i], q.polygon[(i + 1) % q.polygon.size()]));
        }
    }
    return std::max(0.0, core - p.radius - q.radius);
}

QRectF copperShapeBounds(const CopperShape& shape) {
    if (!shape.polygon.isEmpty()) return shape.polygon.boundingRect();
    const double r = shape.radius;
    return QRectF(shape.a, shape.b).normalized().adjusted(-r, -r, r, r);
}

QPainterPath copperShapePath(const CopperShape& shape, double expansion) {
    const double grow = std::max(0.0, expansion);
    QPainterPath path;
    path.setFillRule(Qt::WindingFill);
    if (!shape.polygon.isEmpty()) {
        path.addPolygon(shape.polygon);
        path.closeSubpath();
        if (grow > 0) {
            QPainterPathStroker stroker;
            stroker.setWidth(2.0 * grow);
            stroker.setJoinStyle(Qt::RoundJoin);
            stroker.setCapStyle(Qt::RoundCap);
            path = path.united(stroker.createStroke(path));
        }
        return path;
    }
    const double radius = shape.radius + grow;
    if (radius <= 0) return path;
    if (QLineF(shape.a, shape.b).length() <= Touch) {
        path.addEllipse(shape.a, radius, radius);
        return path;
    }
    QPainterPath line(shape.a);
    line.lineTo(shape.b);
    QPainterPathStroker stroker;
    stroker.setWidth(2.0 * radius);
    stroker.setCapStyle(Qt::RoundCap);
    return stroker.createStroke(line).simplified();
}

QVector<int> BoardCopperModel::groupNets(int root) const {
    QVector<int> nets;
    for (int i = 0; i < conductors.size(); ++i) {
        if (groups.value(i) == root && conductors[i].net >= 0 && !nets.contains(conductors[i].net)) {
            nets.append(conductors[i].net);
        }
    }
    std::sort(nets.begin(), nets.end());
    return nets;
}

double conductorGap(const BoardConductor& a, const BoardConductor& b, QPointF* where) {
    double best = std::numeric_limits<double>::infinity();
    for (const auto& p : a.shapes) {
        for (const auto& q : b.shapes) {
            const double d = copperShapeGap(p, q);
            if (d < best) {
                best = d;
                if (where != nullptr) *where = (p.a + q.a) / 2.0;
            }
        }
    }
    return best;
}

BoardCopperModel buildBoardCopperModel(const SketchDocument& schematic, const SketchDocument& board, double nearDistance) {
    BoardCopperModel model;
    const CircuitSnapshot circuit = analyzeSchematic(schematic);
    model.netsKnown = circuit.errors.isEmpty();
    for (const auto& net : circuit.connectivity.nets) model.netNames << QString::fromStdString(net.name);
    QHash<QString, int> pinNets;
    for (int i = 0; model.netsKnown && i < static_cast<int>(circuit.input.pins.size()); ++i) {
        const auto& pin = circuit.input.pins[i];
        pinNets.insert(QString::fromStdString(pin.component) + QLatin1Char(':') + QString::fromStdString(pin.number),
                       circuit.connectivity.pinNets[i]);
    }
    QHash<QString, const SketchItem*> sources;
    for (const auto& item : schematic) {
        if (schematicComponent(item) && !item.excludeFromBoard) sources.insert(item.id, &item);
    }

    auto& conductors = model.conductors;
    for (const auto& item : board) {
        switch (item.kind) {
        case SketchItem::Kind::Wire: {
            if (item.points.size() < 2) break;
            BoardConductor track;
            track.kind = ConductorKind::Track;
            track.itemId = item.id;
            track.name = tr("track");
            track.layers = itemCopperLayers(item);
            track.anchor = item.points.first();
            track.width = trackWidth(item);
            for (qsizetype i = 1; i < item.points.size(); ++i) {
                track.shapes.append({item.points[i - 1], item.points[i], track.width / 2.0, {}});
            }
            if (track.layers != 0) conductors.append(track);
            break;
        }
        case SketchItem::Kind::Symbol:
        case SketchItem::Kind::Pad:
        case SketchItem::Kind::Via: {
            const auto* footprint = item.kind == SketchItem::Kind::Symbol ? findSymbol(item.variant) : nullptr;
            if (item.kind == SketchItem::Kind::Symbol && (footprint == nullptr || footprint->workspace != Workspace::Board)) break;
            const auto* source = sources.value(item.sourceId, nullptr);
            if (source != nullptr && !model.placedSources.contains(item.sourceId)) model.placedSources << item.sourceId;
            const auto pads = itemPads(item);
            for (int index = 0; index < pads.size(); ++index) {
                const PlacedPad& pad = pads[index];
                BoardConductor part;
                part.kind = item.kind == SketchItem::Kind::Via ? ConductorKind::Via : ConductorKind::Pad;
                part.itemId = item.id;
                part.padIndex = index;
                part.name = item.kind == SketchItem::Kind::Via ? tr("via")
                            : item.kind == SketchItem::Kind::Pad
                                ? tr("pad %1").arg(pad.number)
                                : QStringLiteral("%1.%2").arg(item.label.isEmpty() ? tr("footprint") : item.label).arg(index + 1);
                part.layers = pad.layers & CopperLayerMask;
                part.anchor = pad.center;
                part.pad = pad;
                part.shapes = {padShape(pad)};
                if (source != nullptr && model.netsKnown) {
                    const int pin = source->pinPadMap.indexOf(index + 1);
                    if (pin >= 0) {
                        part.net = pinNets.value(source->id + QLatin1Char(':') + QString::number(pin + 1), -1);
                    }
                }
                if (part.layers != 0) conductors.append(part);
            }
            break;
        }
        case SketchItem::Kind::Polyline: {
            if (item.variant != CopperZoneVariant || item.points.size() < 3) break;
            BoardConductor zone;
            zone.kind = ConductorKind::Zone;
            zone.itemId = item.id;
            zone.name = tr("copper zone");
            zone.layers = itemCopperLayers(item);
            zone.anchor = item.points.first();
            CopperShape area;
            area.polygon = QPolygonF(item.points);
            area.a = area.b = area.polygon.boundingRect().center();
            zone.shapes = {area};
            if (zone.layers != 0) conductors.append(zone);
            break;
        }
        default:
            break;
        }
    }
    for (auto& part : conductors) {
        QRectF bounds;
        for (const auto& shape : part.shapes) bounds = bounds.isNull() ? copperShapeBounds(shape) : bounds.united(copperShapeBounds(shape));
        part.bounds = bounds;
    }

    const int count = conductors.size();
    Groups groups(count);
    const double reach = std::max(nearDistance, Touch);
    for (int a = 0; a < count; ++a) {
        for (int b = a + 1; b < count; ++b) {
            if ((conductors[a].layers & conductors[b].layers) == 0) continue;
            if (!conductors[a].bounds.adjusted(-reach, -reach, reach, reach).intersects(conductors[b].bounds)) continue;
            const double d = conductorGap(conductors[a], conductors[b]);
            if (d <= Touch) groups.join(a, b);
            else if (d < nearDistance) model.nearPairs.append({a, b});
        }
    }
    model.groups.resize(count);
    for (int i = 0; i < count; ++i) model.groups[i] = groups.root(i);
    return model;
}

} // namespace hatt::ui
