#include "hatt/ui/DesignChecks.hpp"

#include "hatt/ui/BoardCopper.hpp"
#include "hatt/ui/ComponentLibrary.hpp"
#include "hatt/ui/SketchCircuit.hpp"
#include "hatt/ui/ZoneFill.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
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

ClearanceObject clearanceObject(ConductorKind kind) {
    switch (kind) {
    case ConductorKind::Track: return ClearanceObject::Trace;
    case ConductorKind::Pad:
    case ConductorKind::Via: return ClearanceObject::Pad;
    case ConductorKind::Zone: return ClearanceObject::Graphic;
    }
    return ClearanceObject::Trace;
}

double pointSegmentDistance(QPointF p, QPointF a, QPointF b) {
    const QPointF ab = b - a;
    const double length2 = QPointF::dotProduct(ab, ab);
    const double t = length2 <= 0 ? 0.0 : std::clamp(QPointF::dotProduct(p - a, ab) / length2, 0.0, 1.0);
    return QLineF(p, a + ab * t).length();
}

} // namespace

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
    QPainterPath boardArea;
    QVector<QLineF> edges;
    for (const auto& item : board) {
        const QVector<QPointF> outline = boardOutlinePoints(item);
        if (outline.isEmpty()) continue;
        QPainterPath area;
        area.addPolygon(QPolygonF(outline));
        area.closeSubpath();
        boardArea = boardArea.united(area);
        edges += itemSegments(item);
    }
    if (boardArea.isEmpty()) {
        add(report, W, B, "drc.no-outline", tr("The board has no closed board outline, so board edge clearance is not checked."));
    }

    // Zones with a net are poured (ZoneFill.hpp, ADR-0009) and checked as their fill; zones without
    // a pour stay solid polygons in the copper model.
    ZonePourOptions pourOptions;
    pourOptions.clearance = clearanceBetween(rules, CopperLayerMask, ClearanceObject::Graphic, ClearanceObject::Pad);
    pourOptions.boardEdgeClearance = edgeClearance(rules, CopperLayerMask);
    pourOptions.thermalReliefs = rules.defaults.thermalRelief;
    pourOptions.thermalGap = std::max(rules.defaults.thermalGap, pourOptions.clearance);
    pourOptions.spokeWidth = rules.defaults.spokeWidth;
    // Net class clearances (issue #39) raise the rule gap for the nets of their classes.
    const QHash<QString, double> classClearances = netClassClearances(rules, schematic);
    pourOptions.netClearances = classClearances;
    const QVector<ZoneFillResult> pours = pourZones(schematic, board, pourOptions);
    QSet<QString> poured;
    for (const auto& pour : pours) poured.insert(pour.zoneId);
    SketchDocument copperBoard;
    for (const auto& item : board) {
        if (!poured.contains(item.id)) copperBoard.append(item);
    }
    const BoardCopperModel model = buildBoardCopperModel(schematic, copperBoard, largestClearance(rules));
    const auto& copper = model.conductors;
    const int count = copper.size();
    if (!model.netsKnown) {
        add(report, W, B, "drc.netlist", tr("Fix the schematic errors (run the electrical check) to check shorts and unrouted nets."));
    }
    auto netName = [&model](int net) { return model.netNames.value(net); };

    // Touching groups, extended by pours: a pour joins every conductor its fill touches.
    QVector<int> group = model.groups;
    QSet<int> shortReported; // group roots whose short was reported through a pour
    auto joinRoots = [&group, &shortReported](const QSet<int>& roots) {
        if (roots.isEmpty()) return -1;
        const int target = *std::min_element(roots.begin(), roots.end());
        for (int& root : group) {
            if (roots.contains(root)) root = target;
        }
        for (int root : roots) {
            if (shortReported.remove(root)) shortReported.insert(target);
        }
        return target;
    };
    auto groupNets = [&group, &copper](int root) {
        QVector<int> nets;
        for (int i = 0; i < group.size(); ++i) {
            if (group[i] == root && copper[i].net >= 0 && !nets.contains(copper[i].net)) nets.append(copper[i].net);
        }
        std::sort(nets.begin(), nets.end());
        return nets;
    };
    for (const auto& pour : pours) {
        const int layer = layerBit(pour.layer);
        const QRectF fillBounds = pour.fill.boundingRect();
        QSet<int> touched;
        QStringList ids{pour.zoneId};
        for (int i = 0; i < count; ++i) {
            if ((copper[i].layers & layer) == 0) continue;
            const double rule = clearanceBetween(rules, layer, ClearanceObject::Graphic, clearanceObject(copper[i].kind));
            const QVector<int> copperNets = groupNets(group[i]);
            const QString copperNet = copperNets.size() == 1 ? netName(copperNets.first()) : QString();
            const double required = netPairClearance(classClearances, rule, pour.net, copperNet);
            const double pourTolerance = std::max(0.02, required * 0.1);
            const QRectF reach = copper[i].bounds.adjusted(-required, -required, required, required);
            if (!fillBounds.intersects(reach)) continue;
            bool touches = false;
            bool tooClose = false;
            for (const auto& shape : copper[i].shapes) {
                touches = touches || pour.fill.intersects(copperShapePath(shape));
                // The pour subtracts the clearance from other nets; allow a tolerance for the curve
                // flattening of grown round outlines (stroker offsets are approximate).
                tooClose = tooClose || pour.fill.intersects(copperShapePath(shape, std::max(0.0, required - pourTolerance)));
            }
            if (touches) {
                touched.insert(group[i]);
                if (!ids.contains(copper[i].itemId)) ids << copper[i].itemId;
            } else if (tooClose && copperNet != pour.net) {
                // Below the rule gap is a plain clearance error; between the rule and the class gap it
                // is a net class clearance error.
                bool belowRule = required <= rule;
                for (const auto& shape : copper[i].shapes) {
                    const double ruleTolerance = std::max(0.02, rule * 0.1);
                    belowRule = belowRule || pour.fill.intersects(copperShapePath(shape, std::max(0.0, rule - ruleTolerance)));
                }
                if (belowRule) {
                    add(report, E, B, "drc.clearance",
                        tr("Clearance between %1 and %2 is below %3.").arg(tr("copper zone"), copper[i].name, millimetres(rule)),
                        copper[i].anchor, {pour.zoneId, copper[i].itemId});
                } else {
                    add(report, E, B, "drc.net-class-clearance",
                        tr("Clearance between %1 and %2 is below the net class clearance %3.")
                            .arg(tr("copper zone"), copper[i].name, millimetres(required)),
                        copper[i].anchor, {pour.zoneId, copper[i].itemId});
                }
            }
        }
        if (pour.fill.isEmpty()) {
            add(report, W, B, "drc.zone-empty",
                tr("This copper zone pours no copper: no copper of net %1 is under it.").arg(pour.net),
                std::nullopt, {pour.zoneId});
        }
        int root = joinRoots(touched);
        if (root < 0) continue;
        // A pour touching copper of another net is a short through the zone (only possible when the
        // pour and the copper disagree, e.g. copper added after the fill or rounding).
        if (model.netsKnown) {
            QStringList foreign;
            for (int net : groupNets(root)) {
                if (netName(net) != pour.net) foreign << netName(net);
            }
            if (!foreign.isEmpty()) {
                foreign.prepend(pour.net);
                foreign.sort();
                add(report, E, B, "drc.zone-short", tr("A copper zone joins different nets: %1").arg(foreign.join(QStringLiteral(", "))),
                    pour.fill.boundingRect().center(), ids);
                shortReported.insert(root);
            }
        }
    }

    // Net classes of schematic nets (explicit, else POWER/SIGNAL).
    const QVector<NetClass> netClasses = effectiveNetClasses(rules);
    const QHash<QString, QString> classOfNet = model.netsKnown ? netClassAssignments(rules, schematic) : QHash<QString, QString>{};
    auto classFor = [&](int index) -> const NetClass* {
        const QVector<int> nets = groupNets(group[index]);
        if (nets.size() != 1) return nullptr;
        const QString name = classOfNet.value(netName(nets.first()));
        for (const auto& netClass : netClasses) {
            if (netClass.name == name) return &netClass;
        }
        return nullptr;
    };

    // Per conductor: widths, holes, net classes and unpoured zones.
    for (int index = 0; index < count; ++index) {
        const BoardConductor& part = copper[index];
        switch (part.kind) {
        case ConductorKind::Track: {
            const CopperShape& first = part.shapes.first();
            const QPointF middle = (first.a + first.b) / 2.0;
            if (part.width + 1e-9 < rules.minTrackWidth) {
                add(report, E, B, "drc.track-width",
                    tr("Track width %1 is below the minimum %2.").arg(millimetres(part.width), millimetres(rules.minTrackWidth)),
                    middle, {part.itemId});
            }
            if (const NetClass* netClass = classFor(index)) {
                // A neck may narrow the class trace near pads, but never below the neck width.
                const double required = netClass->neckWidth > 0 ? netClass->neckWidth : netClass->traceWidth;
                const QString net = netName(groupNets(group[index]).first());
                if (part.width + 1e-9 < required) {
                    add(report, W, B, "drc.net-class-width",
                        tr("Track on net %1 is %2 wide; the %3 net class needs %4.")
                            .arg(net, millimetres(part.width), netClass->name, millimetres(required)),
                        middle, {part.itemId});
                }
                if ((part.layers & ~netClass->layers) != 0) {
                    add(report, W, B, "drc.net-class-layer",
                        tr("Track on net %1 uses a copper layer the %2 net class does not allow.").arg(net, netClass->name),
                        middle, {part.itemId});
                }
            }
            break;
        }
        case ConductorKind::Pad:
        case ConductorKind::Via:
            if (part.pad.drill > 0) {
                if (part.pad.drill + 1e-9 < rules.minDrill) {
                    add(report, E, B, "drc.drill",
                        tr("%1: hole %2 is below the minimum %3.").arg(part.name, millimetres(part.pad.drill), millimetres(rules.minDrill)),
                        part.anchor, {part.itemId});
                }
                const double ring = (std::min(part.pad.width, part.pad.height) - part.pad.drill) / 2.0;
                if (ring + 1e-9 < rules.minAnnularRing) {
                    add(report, E, B, "drc.annular-ring",
                        tr("%1: annular ring %2 is below the minimum %3.").arg(part.name, millimetres(std::max(0.0, ring)),
                                                                               millimetres(rules.minAnnularRing)),
                        part.anchor, {part.itemId});
                }
            }
            break;
        case ConductorKind::Zone:
            // Only zones without a pour remain in the model: no net assigned or not on copper.
            add(report, W, B, "drc.zone-unfilled",
                tr("This copper zone has no net, so it is not poured: it is solid copper without clearance and is left out of the Gerber files."),
                part.anchor, {part.itemId});
            break;
        }
    }

    // Keepout zones (ADR-0012): pours already stay out; tracks, pads, vias and unpoured zones must not
    // enter the keepout on its copper layer.
    for (const SketchItem& keepout : board) {
        if (keepout.variant != KeepoutZoneVariant || !isCopperLayer(keepout.layer) || keepout.points.size() < 3) continue;
        QPainterPath area;
        area.addPolygon(QPolygonF(keepout.points + QVector<QPointF>{keepout.points.first()}));
        const QRectF bounds = area.boundingRect();
        for (int index = 0; index < count; ++index) {
            const BoardConductor& part = copper[index];
            if ((part.layers & layerBit(keepout.layer)) == 0 || !part.bounds.intersects(bounds)) continue;
            const bool inside = std::any_of(part.shapes.begin(), part.shapes.end(),
                                            [&area](const CopperShape& shape) { return area.intersects(copperShapePath(shape)); });
            if (inside) {
                add(report, E, B, "drc.keepout", tr("The keepout zone on %1 contains copper: %2.").arg(boardLayerName(keepout.layer), part.name),
                    part.anchor, {keepout.id, part.itemId});
            }
        }
    }

    // Groups joining several nets.
    if (model.netsKnown) {
        QSet<int> reported;
        for (int i = 0; i < count; ++i) {
            const int root = group[i];
            if (reported.contains(root) || shortReported.contains(root)) continue;
            const QVector<int> nets = groupNets(root);
            if (nets.size() < 2) continue;
            reported.insert(root);
            QStringList names;
            QStringList ids;
            std::optional<QPointF> at;
            bool throughZone = false;
            for (int j = 0; j < count; ++j) {
                if (group[j] != root) continue;
                if (copper[j].net >= 0 && copper[j].net != nets.first() && !at) at = copper[j].anchor;
                if (!ids.contains(copper[j].itemId)) ids << copper[j].itemId;
                throughZone = throughZone || copper[j].kind == ConductorKind::Zone;
            }
            for (int net : nets) names << netName(net);
            names.sort();
            if (throughZone) {
                add(report, E, B, "drc.zone-short",
                    tr("A copper zone joins different nets: %1").arg(names.join(QStringLiteral(", "))),
                    at.value_or(copper[i].anchor), ids);
            } else {
                add(report, E, B, "drc.short", tr("Copper joins different nets: %1").arg(names.join(QStringLiteral(", "))),
                    at.value_or(copper[i].anchor), ids);
            }
        }
    }

    // Copper of different conductors too close together. Zones are not poured, so they are skipped.
    for (const auto& [a, b] : model.nearPairs) {
        if (copper[a].kind == ConductorKind::Zone || copper[b].kind == ConductorKind::Zone) continue;
        const int ra = group[a];
        const int rb = group[b];
        if (ra == rb) continue;
        const QVector<int> netsA = groupNets(ra);
        // Unrouted pads of the same net may sit close together.
        if (netsA.size() == 1 && netsA == groupNets(rb)) continue;
        const double rule = clearanceBetween(rules, copper[a].layers & copper[b].layers, clearanceObject(copper[a].kind),
                                             clearanceObject(copper[b].kind));
        const QVector<int> netsB = groupNets(rb);
        const double required = netPairClearance(classClearances, rule, netsA.size() == 1 ? netName(netsA.first()) : QString(),
                                                 netsB.size() == 1 ? netName(netsB.first()) : QString());
        QPointF where;
        const double d = conductorGap(copper[a], copper[b], &where);
        if (d + 1e-9 >= required) continue;
        if (d + 1e-9 < rule) {
            add(report, E, B, "drc.clearance",
                tr("Clearance %1 between %2 and %3 is below %4.").arg(millimetres(d), copper[a].name, copper[b].name, millimetres(rule)),
                where, {copper[a].itemId, copper[b].itemId});
        } else {
            add(report, E, B, "drc.net-class-clearance",
                tr("Clearance %1 between %2 and %3 is below the net class clearance %4.")
                    .arg(millimetres(d), copper[a].name, copper[b].name, millimetres(required)),
                where, {copper[a].itemId, copper[b].itemId});
        }
    }

    if (!boardArea.isEmpty()) {
        for (const auto& part : copper) {
            // Zones are usually drawn up to the outline; pouring will apply the edge clearance.
            if (part.kind == ConductorKind::Zone) continue;
            const bool inside = boardArea.contains(part.anchor);
            double nearest = std::numeric_limits<double>::infinity();
            for (const auto& shape : part.shapes) {
                for (const QLineF& edge : edges) {
                    nearest = std::min(nearest, copperShapeGap(shape, CopperShape{edge.p1(), edge.p2(), 0.0, {}}));
                }
            }
            if (!inside) {
                add(report, E, B, "drc.board-edge", tr("%1 is outside the board outline.").arg(part.name), part.anchor, {part.itemId});
            } else if (const double required = edgeClearance(rules, part.layers); nearest + 1e-9 < required) {
                add(report, E, B, "drc.board-edge",
                    tr("%1 is %2 from the board edge; the minimum is %3.").arg(part.name, millimetres(nearest), millimetres(required)),
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
    if (model.netsKnown) {
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
                if (!roots.isEmpty() && !roots.contains(group[i]) && !separate) separate = i;
                roots.insert(group[i]);
            }
            if (roots.size() > 1) {
                const int shown = separate.value_or(members.first());
                add(report, W, B, "drc.unrouted",
                    tr("Net %1 is not fully routed: %n unconnected part(s).", static_cast<int>(roots.size()) - 1).arg(netName(net)),
                    copper[shown].anchor, {copper[shown].itemId});
            }
        }
    }

    for (const auto& item : schematic) {
        if (schematicComponent(item) && !item.excludeFromBoard && !model.placedSources.contains(item.id)) {
            add(report, W, Workspace::Schematic, "drc.not-placed", tr("%1 is not placed on the board.").arg(item.label),
                item.points.value(0), {item.id});
        }
    }
    return report;
}

} // namespace hatt::ui
