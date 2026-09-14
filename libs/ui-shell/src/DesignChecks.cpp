#include "hatt/ui/DesignChecks.hpp"

#include "hatt/ui/ComponentLibrary.hpp"
#include "hatt/ui/SketchCircuit.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QLineF>
#include <QPolygonF>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>

namespace hatt::ui {
namespace {

QString tr(const char* text, int n = -1) {
    return QCoreApplication::translate("hatt::ui::DesignChecks", text, nullptr, n);
}

constexpr double Touch = 1e-6;

QString millimetres(double value) { return QStringLiteral("%1 mm").arg(QString::number(value, 'f', 3)); }

bool schematicComponent(const SketchItem& item) {
    const auto* symbol = findSymbol(item.variant);
    return item.kind == SketchItem::Kind::Symbol && symbol != nullptr &&
           symbol->workspace == Workspace::Schematic && symbol->category == SymbolCategory::Component;
}

QString pinKey(const QString& id, int number) { return id + QLatin1Char(':') + QString::number(number); }

void add(CheckReport& report, CheckSeverity severity, Workspace workspace, const char* rule,
         const QString& message, std::optional<QPointF> location = std::nullopt,
         QStringList itemIds = {}) {
    CheckViolation violation;
    violation.severity = severity;
    violation.workspace = workspace;
    violation.rule = QString::fromLatin1(rule);
    violation.message = message;
    violation.hasLocation = location.has_value();
    violation.location = location.value_or(QPointF());
    violation.itemIds = std::move(itemIds);
    report.violations.append(violation);
}

// ---- geometry --------------------------------------------------------------------------------

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

// A copper primitive: a segment widened by `radius` (tracks, round and oval pads) or a polygon
// (rectangular pads).
struct Primitive {
    QPointF a;
    QPointF b;
    double radius = 0.0;
    QPolygonF polygon; // empty for a widened segment
};

double polygonSegmentDistance(const QPolygonF& polygon, QPointF a, QPointF b) {
    if (polygon.containsPoint(a, Qt::OddEvenFill) || polygon.containsPoint(b, Qt::OddEvenFill)) return 0.0;
    double best = std::numeric_limits<double>::infinity();
    for (qsizetype i = 0; i < polygon.size(); ++i) {
        best = std::min(best, segmentDistance(polygon[i], polygon[(i + 1) % polygon.size()], a, b));
    }
    return best;
}

// Gap between two primitives; 0 when they touch or overlap.
double gap(const Primitive& p, const Primitive& q) {
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

Primitive padPrimitive(const PlacedPad& pad) {
    Primitive primitive;
    const double hw = pad.width / 2.0;
    const double hh = pad.height / 2.0;
    if (pad.shape == PadShape::Rect) {
        primitive.polygon = QPolygonF(padOutline(pad));
        primitive.a = primitive.b = pad.center;
        return primitive;
    }
    primitive.radius = std::min(hw, hh);
    const QPointF axis = hw >= hh ? QPointF(hw - primitive.radius, 0) : QPointF(0, hh - primitive.radius);
    primitive.a = pad.center - axis;
    primitive.b = pad.center + axis;
    return primitive;
}

QRectF primitiveBounds(const Primitive& primitive) {
    if (!primitive.polygon.isEmpty()) return primitive.polygon.boundingRect();
    const double r = primitive.radius;
    return QRectF(primitive.a, primitive.b).normalized().adjusted(-r, -r, r, r);
}

// ---- board copper ----------------------------------------------------------------------------

enum class CopperKind { Track, Pad, Via };

struct Copper {
    CopperKind kind = CopperKind::Track;
    QString itemId;
    QString name;    // "R1.2", "track", "via"
    int layers = 0;  // copper layer mask
    int net = -1;    // schematic net index (pads of linked footprints only)
    QPointF anchor;  // a point on the copper, for report locations
    QVector<Primitive> primitives;
    QRectF bounds;
};

double copperGap(const Copper& a, const Copper& b, QPointF* where = nullptr) {
    double best = std::numeric_limits<double>::infinity();
    for (const auto& p : a.primitives) {
        for (const auto& q : b.primitives) {
            const double d = gap(p, q);
            if (d < best) {
                best = d;
                if (where != nullptr) *where = (p.a + q.a) / 2.0;
            }
        }
    }
    return best;
}

struct Groups {
    std::vector<int> parent;
    explicit Groups(int count) : parent(count) { std::iota(parent.begin(), parent.end(), 0); }
    int root(int i) { return parent[i] == i ? i : parent[i] = root(parent[i]); }
    void join(int a, int b) { parent[std::max(root(a), root(b))] = std::min(root(a), root(b)); }
};

} // namespace

QString validateDesignRules(const DesignRules& rules) {
    for (double value : {rules.clearance, rules.minTrackWidth, rules.minDrill, rules.minAnnularRing,
                         rules.boardEdgeClearance}) {
        if (!std::isfinite(value) || value < 0) return tr("Design rules cannot be negative.");
        if (value > 100.0) return tr("Design rules must be at most 100 mm.");
    }
    if (rules.clearance <= 0) return tr("The clearance must be greater than zero.");
    if (rules.minTrackWidth <= 0) return tr("The minimum track width must be greater than zero.");
    return {};
}

int CheckReport::count(CheckSeverity severity) const {
    return static_cast<int>(std::count_if(violations.begin(), violations.end(),
                                          [severity](const CheckViolation& v) { return v.severity == severity; }));
}

CheckReport runElectricalRuleCheck(const SketchDocument& schematic) {
    CheckReport report;
    const auto E = CheckSeverity::Error;
    const auto W = CheckSeverity::Warning;
    const auto S = Workspace::Schematic;
    QHash<QString, const SketchItem*> items;
    QHash<QString, int> referenceCount;
    for (const auto& item : schematic) {
        items.insert(item.id, &item);
        if (schematicComponent(item) && !item.label.trimmed().isEmpty()) ++referenceCount[item.label.trimmed()];
    }

    for (const auto& item : schematic) {
        if (item.kind != SketchItem::Kind::Symbol || item.points.isEmpty()) continue;
        const auto* symbol = findSymbol(item.variant);
        if (symbol == nullptr) continue;
        const QPointF at = item.points.first();
        if (schematicComponent(item)) {
            const QString reference = item.label.trimmed();
            if (reference.isEmpty()) {
                add(report, E, S, "erc.analysis", tr("A %1 has no reference designator.").arg(symbolDisplayName(*symbol)), at, {item.id});
            } else if (referenceCount.value(reference) > 1) {
                add(report, E, S, "erc.analysis", tr("The reference %1 is used more than once.").arg(reference), at, {item.id});
            }
            if (!symbol->defaultValue.isEmpty() && item.value.trimmed().isEmpty()) {
                add(report, W, S, "erc.missing-value", tr("%1 has no value.").arg(reference), at, {item.id});
            }
            if (!item.excludeFromBoard) {
                const auto* footprint = findSymbol(item.footprint);
                if (footprint == nullptr || footprint->workspace != Workspace::Board ||
                    footprint->pins.size() != symbol->pins.size()) {
                    add(report, E, S, "erc.footprint",
                        tr("%1 needs a footprint with %2 pads (or exclude it from the board).").arg(reference).arg(symbol->pins.size()),
                        at, {item.id});
                } else if (item.pinPadMap.size() != symbol->pins.size() ||
                           !validatePinPadMap(item.pinPadMap, static_cast<int>(symbol->pins.size())).isEmpty()) {
                    add(report, E, S, "erc.footprint", tr("%1: the pin to pad map must use every pad once.").arg(reference), at, {item.id});
                }
            }
        } else if (symbol->category == SymbolCategory::Terminal && item.variant != QLatin1String("schematic.junction") &&
                   item.variant != QLatin1String("schematic.ground") && item.label.trimmed().isEmpty()) {
            add(report, E, S, "erc.analysis", tr("A port or power rail has no net name."), at, {item.id});
        }
    }

    const CircuitSnapshot snapshot = analyzeSchematic(schematic);
    for (const auto& error : snapshot.connectivity.errors) {
        const QString text = QString::fromStdString(error);
        const QString prefix = QStringLiteral("Conflicting net names:");
        if (text.startsWith(prefix)) {
            const QStringList names = text.mid(prefix.size()).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            std::optional<QPointF> at;
            QStringList ids;
            for (const auto& item : schematic) {
                if (item.kind == SketchItem::Kind::Symbol && names.contains(item.label.trimmed()) && !schematicComponent(item)) {
                    if (!at && !item.points.isEmpty()) at = item.points.first();
                    ids << item.id;
                }
            }
            add(report, E, S, "erc.analysis", tr("Different net names are connected: %1").arg(names.join(QStringLiteral(", "))), at, ids);
        } else {
            add(report, E, S, "erc.analysis", text);
        }
    }

    const auto& input = snapshot.input;
    const auto& connectivity = snapshot.connectivity;
    if (connectivity.pinNets.size() == input.pins.size()) {
        // Pins of each net and which of them belong to components.
        QVector<int> netPins(static_cast<int>(connectivity.nets.size()), 0);
        QVector<int> netComponentPins(static_cast<int>(connectivity.nets.size()), 0);
        QHash<QString, QVector<int>> itemNets;
        for (int p = 0; p < static_cast<int>(input.pins.size()); ++p) {
            const int net = connectivity.pinNets[p];
            if (net < 0) continue;
            const QString id = QString::fromStdString(input.pins[p].component);
            ++netPins[net];
            if (const auto* item = items.value(id); item != nullptr && schematicComponent(*item)) ++netComponentPins[net];
            itemNets[id].append(net);
        }
        QSet<QString> reportedTerminals;
        for (int p = 0; p < static_cast<int>(input.pins.size()); ++p) {
            const int net = connectivity.pinNets[p];
            const auto& pin = input.pins[p];
            const QString id = QString::fromStdString(pin.component);
            const auto* item = items.value(id);
            if (item == nullptr || net < 0) continue;
            const QPointF at(pin.position.x, pin.position.y);
            if (schematicComponent(*item)) {
                if (netPins[net] == 1) {
                    add(report, W, S, "erc.unconnected-pin",
                        tr("%1 pin %2 is not connected.").arg(item->label, QString::fromStdString(pin.number)), at, {id});
                }
            } else if (item->variant != QLatin1String("schematic.junction") && netComponentPins[net] == 0 &&
                       !reportedTerminals.contains(id)) {
                reportedTerminals.insert(id);
                const auto* symbol = findSymbol(item->variant);
                const QString name = item->label.trimmed().isEmpty() ? symbolDisplayName(*symbol) : item->label.trimmed();
                add(report, W, S, "erc.unused-terminal", tr("%1 is not connected to any component.").arg(name), at, {id});
            }
        }
        for (const auto& item : schematic) {
            if (!schematicComponent(item)) continue;
            const auto nets = itemNets.value(item.id);
            if (nets.size() >= 2 && std::all_of(nets.begin(), nets.end(), [&](int n) { return n == nets.first(); }) &&
                netPins[nets.first()] > 1) {
                add(report, W, S, "erc.shorted-component", tr("All pins of %1 are on the same net.").arg(item.label),
                    item.points.value(0), {item.id});
            }
        }
    }

    // Wire ends that touch no pin, junction or other wire.
    QVector<const SketchItem*> wires;
    for (const auto& item : schematic) {
        if (item.kind == SketchItem::Kind::Wire && item.points.size() >= 2) wires.append(&item);
    }
    auto onWire = [](QPointF p, const SketchItem& wire) {
        for (qsizetype i = 1; i < wire.points.size(); ++i) {
            if (pointSegmentDistance(p, wire.points[i - 1], wire.points[i]) <= Touch) return true;
        }
        return false;
    };
    for (const auto* wire : wires) {
        for (const QPointF end : {wire->points.first(), wire->points.last()}) {
            bool touches = std::any_of(input.pins.begin(), input.pins.end(), [&](const auto& pin) {
                return QLineF(end, QPointF(pin.position.x, pin.position.y)).length() <= Touch;
            });
            touches = touches || std::any_of(input.junctions.begin(), input.junctions.end(), [&](const auto& j) {
                          return QLineF(end, QPointF(j.x, j.y)).length() <= Touch;
                      });
            touches = touches || std::any_of(wires.begin(), wires.end(), [&](const SketchItem* other) {
                          return other != wire && onWire(end, *other);
                      });
            if (!touches) add(report, W, S, "erc.dangling-wire", tr("A wire end is not connected."), end, {wire->id});
        }
    }
    return report;
}

CheckReport runDesignRuleCheck(const SketchDocument& schematic, const SketchDocument& board,
                               const DesignRules& rules) {
    CheckReport report;
    const auto E = CheckSeverity::Error;
    const auto W = CheckSeverity::Warning;
    const auto B = Workspace::Board;

    // Board outline.
    const SketchItem* outline = nullptr;
    for (const auto& item : board) {
        if (item.variant == BoardOutlineVariant && (item.closed || item.kind == SketchItem::Kind::Rectangle)) {
            outline = &item;
            break;
        }
    }
    QVector<QLineF> edges;
    QPolygonF outlinePolygon;
    if (outline != nullptr) {
        edges = itemSegments(*outline);
        for (const QLineF& edge : edges) outlinePolygon << edge.p1();
    } else {
        add(report, W, B, "drc.no-outline", tr("The board has no closed board outline, so board edge clearance is not checked."));
    }

    // Schematic nets of footprint pads.
    const CircuitSnapshot circuit = analyzeSchematic(schematic);
    const bool netsKnown = circuit.errors.isEmpty();
    if (!netsKnown) {
        add(report, W, B, "drc.netlist", tr("Fix the schematic errors (run the electrical check) to check shorts and unrouted nets."));
    }
    QHash<QString, int> pinNets;
    for (int i = 0; netsKnown && i < static_cast<int>(circuit.input.pins.size()); ++i) {
        const auto& pin = circuit.input.pins[i];
        pinNets.insert(QString::fromStdString(pin.component) + QLatin1Char(':') + QString::fromStdString(pin.number),
                       circuit.connectivity.pinNets[i]);
    }
    QHash<QString, const SketchItem*> sources;
    for (const auto& item : schematic) {
        if (schematicComponent(item) && !item.excludeFromBoard) sources.insert(item.id, &item);
    }
    QSet<QString> placed;

    QVector<Copper> copper;
    for (const auto& item : board) {
        switch (item.kind) {
        case SketchItem::Kind::Wire: {
            if (item.points.size() < 2) break;
            const double width = trackWidth(item);
            if (width + 1e-9 < rules.minTrackWidth) {
                add(report, E, B, "drc.track-width",
                    tr("Track width %1 is below the minimum %2.").arg(millimetres(width), millimetres(rules.minTrackWidth)),
                    (item.points[0] + item.points[1]) / 2.0, {item.id});
            }
            Copper track;
            track.kind = CopperKind::Track;
            track.itemId = item.id;
            track.name = tr("track");
            track.layers = itemCopperLayers(item);
            track.anchor = item.points.first();
            for (qsizetype i = 1; i < item.points.size(); ++i) {
                track.primitives.append({item.points[i - 1], item.points[i], width / 2.0, {}});
            }
            if (track.layers != 0) copper.append(track);
            break;
        }
        case SketchItem::Kind::Symbol:
        case SketchItem::Kind::Pad:
        case SketchItem::Kind::Via: {
            const auto* footprint = item.kind == SketchItem::Kind::Symbol ? findSymbol(item.variant) : nullptr;
            if (item.kind == SketchItem::Kind::Symbol && (footprint == nullptr || footprint->workspace != Workspace::Board)) break;
            const auto* source = sources.value(item.sourceId, nullptr);
            if (source != nullptr) placed.insert(item.sourceId);
            const auto pads = itemPads(item);
            for (int index = 0; index < pads.size(); ++index) {
                const PlacedPad& pad = pads[index];
                Copper part;
                part.kind = item.kind == SketchItem::Kind::Via ? CopperKind::Via : CopperKind::Pad;
                part.itemId = item.id;
                part.name = item.kind == SketchItem::Kind::Via ? tr("via")
                            : item.kind == SketchItem::Kind::Pad
                                ? tr("pad %1").arg(pad.number)
                                : QStringLiteral("%1.%2").arg(item.label.isEmpty() ? tr("footprint") : item.label).arg(index + 1);
                part.layers = pad.layers & CopperLayerMask;
                part.anchor = pad.center;
                part.primitives = {padPrimitive(pad)};
                if (source != nullptr && netsKnown) {
                    const int pin = source->pinPadMap.indexOf(index + 1);
                    if (pin >= 0) part.net = pinNets.value(pinKey(source->id, pin + 1), -1);
                }
                if (pad.drill > 0) {
                    if (pad.drill + 1e-9 < rules.minDrill) {
                        add(report, E, B, "drc.drill",
                            tr("%1: hole %2 is below the minimum %3.").arg(part.name, millimetres(pad.drill), millimetres(rules.minDrill)),
                            pad.center, {item.id});
                    }
                    const double ring = (std::min(pad.width, pad.height) - pad.drill) / 2.0;
                    if (ring + 1e-9 < rules.minAnnularRing) {
                        add(report, E, B, "drc.annular-ring",
                            tr("%1: annular ring %2 is below the minimum %3.").arg(part.name, millimetres(std::max(0.0, ring)),
                                                                                   millimetres(rules.minAnnularRing)),
                            pad.center, {item.id});
                    }
                }
                if (part.layers != 0) copper.append(part);
            }
            break;
        }
        default:
            break;
        }
    }
    for (auto& part : copper) {
        QRectF bounds;
        for (const auto& primitive : part.primitives) bounds = bounds.isNull() ? primitiveBounds(primitive) : bounds.united(primitiveBounds(primitive));
        part.bounds = bounds;
    }

    // Copper that touches on a shared layer conducts.
    const int count = copper.size();
    Groups groups(count);
    QVector<QPair<int, int>> nearPairs;
    const double reach = std::max(rules.clearance, Touch);
    for (int a = 0; a < count; ++a) {
        for (int b = a + 1; b < count; ++b) {
            if ((copper[a].layers & copper[b].layers) == 0) continue;
            if (!copper[a].bounds.adjusted(-reach, -reach, reach, reach).intersects(copper[b].bounds)) continue;
            const double d = copperGap(copper[a], copper[b]);
            if (d <= Touch) groups.join(a, b);
            else if (d < rules.clearance) nearPairs.append({a, b});
        }
    }
    QHash<int, QSet<int>> groupNets;
    for (int i = 0; i < count; ++i) {
        if (copper[i].net >= 0) groupNets[groups.root(i)].insert(copper[i].net);
    }
    auto netName = [&](int net) {
        return net >= 0 && net < static_cast<int>(circuit.connectivity.nets.size())
                   ? QString::fromStdString(circuit.connectivity.nets[net].name)
                   : QString();
    };

    if (netsKnown) {
        QSet<int> shortedGroups;
        for (int i = 0; i < count; ++i) {
            const int root = groups.root(i);
            const auto nets = groupNets.value(root);
            if (nets.size() < 2 || shortedGroups.contains(root)) continue;
            shortedGroups.insert(root);
            QStringList names;
            QStringList ids;
            std::optional<QPointF> at;
            for (int j = 0; j < count; ++j) {
                if (groups.root(j) != root) continue;
                if (copper[j].net >= 0 && copper[j].net != copper[i].net && !at) at = copper[j].anchor;
                if (!ids.contains(copper[j].itemId)) ids << copper[j].itemId;
            }
            for (int net : nets) names << netName(net);
            names.sort();
            add(report, E, B, "drc.short", tr("Copper joins different nets: %1").arg(names.join(QStringLiteral(", "))),
                at.value_or(copper[i].anchor), ids);
        }
    }

    for (const auto& [a, b] : std::as_const(nearPairs)) {
        const int ra = groups.root(a);
        const int rb = groups.root(b);
        if (ra == rb) continue;
        const auto netsA = groupNets.value(ra);
        const auto netsB = groupNets.value(rb);
        // Unrouted pads of the same net may sit close together.
        if (netsA.size() == 1 && netsA == netsB) continue;
        QPointF where;
        const double d = copperGap(copper[a], copper[b], &where);
        add(report, E, B, "drc.clearance",
            tr("Clearance %1 between %2 and %3 is below %4.").arg(millimetres(d), copper[a].name, copper[b].name, millimetres(rules.clearance)),
            where, {copper[a].itemId, copper[b].itemId});
    }

    if (outline != nullptr && outlinePolygon.size() >= 3) {
        for (const auto& part : copper) {
            const bool inside = outlinePolygon.containsPoint(part.anchor, Qt::OddEvenFill);
            double nearest = std::numeric_limits<double>::infinity();
            for (const auto& primitive : part.primitives) {
                for (const QLineF& edge : edges) {
                    Primitive line{edge.p1(), edge.p2(), 0.0, {}};
                    nearest = std::min(nearest, gap(primitive, line));
                }
            }
            if (!inside) {
                add(report, E, B, "drc.board-edge", tr("%1 is outside the board outline.").arg(part.name), part.anchor, {part.itemId});
            } else if (nearest + 1e-9 < rules.boardEdgeClearance) {
                add(report, E, B, "drc.board-edge",
                    tr("%1 is %2 from the board edge; the minimum is %3.").arg(part.name, millimetres(nearest), millimetres(rules.boardEdgeClearance)),
                    part.anchor, {part.itemId});
            }
        }
    }

    // Footprint bodies on the same side.
    struct Body {
        const SketchItem* item;
        QRectF bounds;
    };
    QVector<Body> bodies;
    for (const auto& item : board) {
        if (item.kind != SketchItem::Kind::Symbol) continue;
        const auto* footprint = findSymbol(item.variant);
        if (footprint == nullptr || footprint->workspace != Workspace::Board || footprint->pins.isEmpty()) continue;
        QPolygonF points;
        for (const auto& shape : footprint->shapes) {
            if (shape.copper || shape.hole) continue;
            for (const QPointF& point : shape.points) points << symbolToWorld(item, point);
        }
        for (const auto& pad : itemPads(item)) points << padOutline(pad);
        if (points.size() >= 2) bodies.append({&item, points.boundingRect()});
    }
    for (int a = 0; a < bodies.size(); ++a) {
        for (int b = a + 1; b < bodies.size(); ++b) {
            if (bodies[a].item->onBottom != bodies[b].item->onBottom) continue;
            const QRectF overlap = bodies[a].bounds.intersected(bodies[b].bounds);
            if (overlap.width() > 0.01 && overlap.height() > 0.01) {
                add(report, W, B, "drc.overlap", tr("%1 and %2 overlap.").arg(bodies[a].item->label, bodies[b].item->label),
                    overlap.center(), {bodies[a].item->id, bodies[b].item->id});
            }
        }
    }

    // Pads of one schematic net that copper does not join yet.
    if (netsKnown) {
        QHash<int, QVector<int>> netCopper;
        for (int i = 0; i < count; ++i) {
            if (copper[i].net >= 0) netCopper[copper[i].net].append(i);
        }
        QList<int> nets = netCopper.keys();
        std::sort(nets.begin(), nets.end());
        for (int net : nets) {
            const auto members = netCopper.value(net);
            QSet<int> roots;
            std::optional<int> separate;
            for (int i : members) {
                if (!roots.isEmpty() && !roots.contains(groups.root(i)) && !separate) separate = i;
                roots.insert(groups.root(i));
            }
            if (roots.size() > 1) {
                add(report, W, B, "drc.unrouted",
                    tr("Net %1 is not fully routed: %n unconnected part(s).", static_cast<int>(roots.size()) - 1).arg(netName(net)),
                    copper[separate.value_or(members.first())].anchor, {copper[separate.value_or(members.first())].itemId});
            }
        }
    }

    for (const auto& item : schematic) {
        if (sources.contains(item.id) && !placed.contains(item.id)) {
            add(report, W, Workspace::Schematic, "drc.not-placed", tr("%1 is not placed on the board.").arg(item.label),
                item.points.value(0), {item.id});
        }
    }
    return report;
}

} // namespace hatt::ui
