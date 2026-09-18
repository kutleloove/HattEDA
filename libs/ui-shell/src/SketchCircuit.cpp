#include "hatt/ui/SketchCircuit.hpp"
#include "hatt/ui/ComponentCatalog.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QPainterPath>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace hatt::ui {
namespace {
QString tr(const char* text) { return QCoreApplication::translate("hatt::ui::CircuitWorkflow", text); }
[[maybe_unused]] const char* SimulationTranslationSources[] = {
    QT_TRANSLATE_NOOP("hatt::ui::CircuitWorkflow", "unknown simulation model '%1'"),
    QT_TRANSLATE_NOOP("hatt::ui::CircuitWorkflow", "%1: %2"),
    QT_TRANSLATE_NOOP("hatt::ui::CircuitWorkflow", "%1: the DC model requires exactly two pins."),
    QT_TRANSLATE_NOOP("hatt::ui::CircuitWorkflow", "%1 pin %2 is not connected to another component or terminal."),
    QT_TRANSLATE_NOOP("hatt::ui::CircuitWorkflow", "The DC section at %1 pin %2 has no path to Ground."),
};
electrical::Point point(QPointF p) { return {p.x(), p.y()}; }
bool component(const SketchItem& item) {
    const auto* s = findSymbol(item.variant);
    return item.kind == SketchItem::Kind::Symbol && s &&
           s->workspace == Workspace::Schematic && s->category == SymbolCategory::Component;
}
// Schematic components that take part in the PCB (not excluded with "exclude from board").
bool boardComponent(const SketchItem& item) { return component(item) && !item.excludeFromBoard; }
QString pinKey(const QString& id, int number) { return id + QLatin1Char(':') + QString::number(number); }
// Designator order: prefix, then number (R2 before R10).
bool designatorLess(const QString& a, const QString& b) {
    auto split = [](const QString& text) {
        qsizetype digits = text.size();
        while (digits > 0 && text.at(digits - 1).isDigit()) --digits;
        return std::pair{text.left(digits), text.mid(digits).toLongLong()};
    };
    const auto [prefixA, numberA] = split(a);
    const auto [prefixB, numberB] = split(b);
    return prefixA != prefixB ? prefixA < prefixB : numberA < numberB;
}
QHash<QString, int> pinNets(const CircuitSnapshot& snapshot) {
    QHash<QString, int> result;
    for (int i = 0; i < static_cast<int>(snapshot.input.pins.size()); ++i) {
        const auto& p = snapshot.input.pins[i];
        result.insert(QString::fromStdString(p.component) + QLatin1Char(':') +
                          QString::fromStdString(p.number), snapshot.connectivity.pinNets[i]);
    }
    return result;
}
QStringList validateFootprint(const SketchItem& item) {
    QStringList errors;
    const auto* source = findSymbol(item.variant);
    const auto* footprint = findSymbol(item.footprint);
    if (!source || !footprint || footprint->workspace != Workspace::Board ||
        footprint->pins.size() != source->pins.size()) {
        errors << tr("%1: assign a footprint with matching pin count.").arg(item.label);
        return errors;
    }
    QSet<int> pads;
    for (int pad : item.pinPadMap) pads.insert(pad);
    if (item.pinPadMap.size() != source->pins.size() || pads.size() != source->pins.size() ||
        std::any_of(pads.begin(), pads.end(), [&](int p) { return p < 1 || p > footprint->pins.size(); })) {
        errors << tr("%1: pin-to-pad mapping must list each pad once.").arg(item.label);
    }
    return errors;
}
void collectSchematicInput(const SketchDocument& document, CircuitSnapshot& result) {
    QSet<QString> ids;
    QSet<QString> references;
    for (const auto& item : document) {
        if (item.kind == SketchItem::Kind::Wire) {
            electrical::Wire wire;
            for (auto p : item.points) wire.points.push_back(point(p));
            result.input.wires.push_back(std::move(wire));
            result.wireIds.push_back(item.id);
            continue;
        }
        if (item.kind != SketchItem::Kind::Symbol) continue;
        const auto* symbol = findSymbol(item.variant);
        if (!symbol || symbol->workspace != Workspace::Schematic || item.points.isEmpty()) {
            result.errors << tr("Unknown or invalid schematic symbol: %1").arg(item.variant);
            continue;
        }
        if (ids.contains(item.id) || item.id.isEmpty()) result.errors << tr("Duplicate or missing component identity.");
        ids.insert(item.id);
        for (int p = 0; p < symbol->pins.size(); ++p) {
            result.input.pins.push_back({item.id.toStdString(), std::to_string(p + 1),
                                         point(symbolToWorld(item, symbol->pins[p]))});
        }
        if (component(item)) {
            if (item.label.trimmed().isEmpty() || references.contains(item.label))
                result.errors << tr("Duplicate or missing reference: %1").arg(item.label);
            references.insert(item.label);
        } else if (!symbol->pins.isEmpty()) {
            const auto location = point(symbolToWorld(item, symbol->pins.first()));
            if (item.variant == QLatin1String("schematic.junction")) result.input.junctions.push_back(location);
            else if (item.variant == QLatin1String("schematic.ground")) result.input.names.push_back({location, "0"});
            else if (symbol->category == SymbolCategory::Terminal) {
                if (item.label.trimmed().isEmpty()) result.errors << tr("A port or power net needs a name.");
                else result.input.names.push_back({location, item.label.trimmed().toStdString()});
            }
        }
    }
}
}

CircuitSnapshot analyzeSchematic(const SketchDocument& document) {
    CircuitSnapshot result;
    collectSchematicInput(document, result);
    result.connectivity = electrical::buildConnectivity(result.input);
    for (const auto& error : result.connectivity.errors) result.errors << QString::fromStdString(error);
    if (!result.errors.isEmpty()) return result;
    result.dc.netCount = static_cast<int>(result.connectivity.nets.size());
    for (int n = 0; n < result.dc.netCount; ++n)
        if (result.connectivity.nets[n].name == "0") result.dc.ground = n;
    const auto nets = pinNets(result);
    for (const auto& item : document) {
        if (item.kind == SketchItem::Kind::Symbol && item.variant == QLatin1String("schematic.voltage-probe")) {
            const auto* probe = findSymbol(item.variant);
            result.probes.append({item.id, symbolToWorld(item, probe->pins.value(0)), nets.value(pinKey(item.id, 1), -1)});
        }
        if (!component(item)) continue;
        const auto* symbol = findSymbol(item.variant);
        QString model = symbol != nullptr ? symbol->simulationModel : QString();
        // Compatibility for the original four built-in symbols, whose stable ids predate the
        // explicit catalog contract.
        if (model.isEmpty() && item.variant == QLatin1String("schematic.resistor")) model = QStringLiteral("dc.resistor");
        if (model.isEmpty() && item.variant == QLatin1String("schematic.vdc")) model = QStringLiteral("dc.voltage-source");
        if (model.isEmpty() && item.variant == QLatin1String("schematic.capacitor")) model = QStringLiteral("dc.capacitor");
        if (model.isEmpty() && item.variant == QLatin1String("schematic.inductor")) model = QStringLiteral("dc.inductor");
        const auto* definition = findSimulationModel(model.isEmpty() ? QStringLiteral("none") : model);
        if (definition == nullptr || definition->support != AnalysisSupport::DcOperatingPoint) {
            const QString reason = definition == nullptr
                                       ? tr("unknown simulation model '%1'").arg(model)
                                       : definition->limitation;
            result.simulationErrors << tr("%1: %2").arg(item.label, reason);
            continue;
        }
        if (symbol == nullptr || symbol->pins.size() != 2) {
            result.simulationErrors << tr("%1: the DC model requires exactly two pins.").arg(item.label);
            continue;
        }
        electrical::DcElement e;
        if (model == QLatin1String("dc.resistor")) e.kind = electrical::DcKind::Resistor;
        else if (model == QLatin1String("dc.voltage-source")) e.kind = electrical::DcKind::VoltageSource;
        else if (model == QLatin1String("dc.current-source")) e.kind = electrical::DcKind::CurrentSource;
        else if (model == QLatin1String("dc.capacitor")) e.kind = electrical::DcKind::Capacitor;
        else if (model == QLatin1String("dc.inductor")) e.kind = electrical::DcKind::Inductor;
        else if (model == QLatin1String("dc.switch-open") || model == QLatin1String("dc.switch-closed")) {
            e.kind = electrical::DcKind::Resistor;
            e.value = definition->parameters.value(QStringLiteral("resistance"));
        }
        e.reference = item.label.toStdString();
        e.positive = nets.value(pinKey(item.id, 1), -1);
        e.negative = nets.value(pinKey(item.id, 2), -1);
        const bool fixedValue = model.startsWith(QLatin1String("dc.switch-"));
        if (!fixedValue && !electrical::parseSpiceValue(item.value.toStdString(), e.value))
            result.simulationErrors << tr("%1: invalid value '%2'.").arg(item.label, item.value);
        result.dc.elements.push_back(e);
    }
    if (!result.dc.elements.empty()) {
        // Proteus-style convenience: a closed circuit does not need an explicit Ground symbol.
        // Prefer the negative terminal of the first independent voltage source as the 0 V
        // reference; for source-less networks any element's negative terminal is deterministic.
        // This changes only the reported absolute node voltages, never voltage differences or
        // currents. An explicit Ground terminal always wins.
        if (result.dc.ground < 0) {
            const auto source = std::find_if(result.dc.elements.begin(), result.dc.elements.end(),
                                             [](const electrical::DcElement& element) {
                                                 return element.kind == electrical::DcKind::VoltageSource;
                                             });
            result.dc.ground = source != result.dc.elements.end()
                                   ? source->negative
                                   : result.dc.elements.front().negative;
        }
        if (result.dc.ground >= 0) {
            QVector<QVector<int>> adjacent(result.dc.netCount);
            for (const auto& e : result.dc.elements) {
                if (e.positive < 0 || e.negative < 0 || e.positive >= result.dc.netCount ||
                    e.negative >= result.dc.netCount) continue;
                adjacent[e.positive].append(e.negative);
                adjacent[e.negative].append(e.positive);
            }
            QVector<bool> reachable(result.dc.netCount, false);
            QVector<int> pending{result.dc.ground};
            reachable[result.dc.ground] = true;
            for (qsizetype i = 0; i < pending.size(); ++i) {
                for (int net : adjacent[pending[i]]) if (!reachable[net]) {
                    reachable[net] = true;
                    pending.append(net);
                }
            }
            bool reportedFloating = false;
            for (const auto& e : result.dc.elements) {
                for (int pin = 1; pin <= 2; ++pin) {
                    const int net = pin == 1 ? e.positive : e.negative;
                    if (net < 0 || net >= result.dc.netCount) continue;
                    const QString reference = QString::fromStdString(e.reference);
                    if (!reportedFloating && !reachable[net]) {
                        result.simulationErrors << tr("The DC section at %1 pin %2 has no path to Ground.")
                                                       .arg(reference).arg(pin);
                        reportedFloating = true;
                    }
                    if (result.connectivity.nets[net].pins.size() <= 1)
                        result.simulationErrors << tr("%1 pin %2 is not connected to another component or terminal.")
                                                       .arg(reference).arg(pin);
                }
            }
        }
    }
    // Nets no element touches (a lone probe, an unused port) are left out of the solve.
    QVector<int> dcIndex(result.dc.netCount, -1);
    auto use = [&](int net) {
        if (net >= 0 && dcIndex[net] < 0) {
            dcIndex[net] = static_cast<int>(result.dcNets.size());
            result.dcNets.append(net);
        }
    };
    use(result.dc.ground);
    for (const auto& e : result.dc.elements) { use(e.positive); use(e.negative); }
    for (auto& e : result.dc.elements) {
        e.positive = e.positive >= 0 ? dcIndex[e.positive] : -1;
        e.negative = e.negative >= 0 ? dcIndex[e.negative] : -1;
    }
    result.dc.ground = result.dc.ground >= 0 ? dcIndex[result.dc.ground] : -1;
    result.dc.netCount = static_cast<int>(result.dcNets.size());
    return result;
}

QVector<QPointF> schematicJunctions(const SketchDocument& document) {
    CircuitSnapshot snapshot;
    collectSchematicInput(document, snapshot);
    QVector<QPointF> result;
    for (const auto p : electrical::junctionPoints(snapshot.input)) result.append(QPointF(p.x, p.y));
    return result;
}

QHash<QString, QString> schematicWireNets(const SketchDocument& document) {
    QHash<QString, QString> result;
    const CircuitSnapshot snapshot = analyzeSchematic(document);
    if (!snapshot.errors.isEmpty()) return result;
    const auto& wireNets = snapshot.connectivity.wireNets;
    for (qsizetype i = 0; i < snapshot.wireIds.size() && i < static_cast<qsizetype>(wireNets.size()); ++i) {
        const int net = wireNets[static_cast<std::size_t>(i)];
        if (net < 0 || snapshot.wireIds[i].isEmpty() ||
            net >= static_cast<int>(snapshot.connectivity.nets.size())) {
            continue;
        }
        result.insert(snapshot.wireIds[i], QString::fromStdString(snapshot.connectivity.nets[net].name));
    }
    return result;
}

BoardTransfer transferToBoard(const SketchDocument& schematic, const SketchDocument& board) {
    BoardTransfer result;
    result.document = board;
    const auto circuit = analyzeSchematic(schematic);
    result.errors = circuit.errors;
    QHash<QString, const SketchItem*> sources;
    for (const auto& item : schematic) if (boardComponent(item)) {
        sources.insert(item.id, &item);
        result.errors.append(validateFootprint(item));
    }
    if (sources.isEmpty()) result.errors << tr("Place schematic components before updating the PCB.");
    QSet<QString> linked;
    QSet<QString> boardIds;
    for (const auto& item : board) {
        if (boardIds.contains(item.id)) result.errors << tr("Duplicate board item identity.");
        boardIds.insert(item.id);
        if (item.sourceId.isEmpty()) continue;
        if (!sources.contains(item.sourceId)) result.errors << tr("%1: linked schematic component was removed; review this footprint.").arg(item.label);
        if (linked.contains(item.sourceId)) result.errors << tr("Duplicate PCB component link: %1").arg(item.label);
        linked.insert(item.sourceId);
    }
    if (!result.errors.isEmpty()) return result;
    for (const auto& item : schematic) {
        if (!boardComponent(item)) continue;
        auto found = std::find_if(result.document.begin(), result.document.end(), [&](const auto& b) { return b.sourceId == item.id; });
        if (found == result.document.end()) continue;
        if (found->variant != item.footprint || found->pinPadMap != item.pinPadMap) {
            result.errors << tr("%1: footprint or pin mapping changed; review existing routing before replacing it.").arg(item.label);
            continue;
        }
        if (found->label != item.label || found->value != item.value) {
            found->label = item.label;
            found->value = item.value;
            ++result.updated;
        }
    }
    if (!result.errors.isEmpty()) { result.document = board; result.updated = 0; return result; }
    const auto waiting = unplacedBoardParts(schematic, result.document);
    result.added = waiting.parts.size();
    result.document = autoPlaceParts(result.document, waiting.parts, 1.27);
    return result;
}

BoardParts unplacedBoardParts(const SketchDocument& schematic, const SketchDocument& board) {
    BoardParts result;
    QSet<QString> linked;
    for (const auto& item : board) if (!item.sourceId.isEmpty()) linked.insert(item.sourceId);
    for (const auto& item : schematic) {
        if (!boardComponent(item) || linked.contains(item.id)) continue;
        const auto problems = validateFootprint(item);
        if (!problems.isEmpty()) {
            result.problems.append(problems);
            continue;
        }
        SketchItem footprint;
        footprint.kind = SketchItem::Kind::Symbol;
        footprint.variant = item.footprint;
        footprint.label = item.label;
        footprint.value = item.value;
        footprint.sourceId = item.id;
        footprint.pinPadMap = item.pinPadMap;
        footprint.points = {{0, 0}};
        result.parts.append(footprint);
    }
    std::stable_sort(result.parts.begin(), result.parts.end(),
                     [](const SketchItem& a, const SketchItem& b) { return designatorLess(a.label, b.label); });
    return result;
}

SketchDocument autoPlaceParts(const SketchDocument& board, const SketchDocument& parts, double grid,
                              double spacing) {
    const double gap = std::max(0.0, spacing);
    SketchDocument result = board;
    QVector<QRectF> occupied;
    struct PlacementRegion {
        QPainterPath shape;
        QRectF bounds;
    };
    QVector<PlacementRegion> regions;
    for (const auto& item : board) {
        const QVector<QPointF> outline = boardOutlinePoints(item);
        if (!outline.isEmpty()) {
            QPainterPath shape(outline.first());
            for (qsizetype i = 1; i < outline.size(); ++i) shape.lineTo(outline[i]);
            shape.closeSubpath();
            regions.append({shape, shape.boundingRect()});
        }
        // The outline and zones surround parts, so they do not block placement.
        if (outline.isEmpty() && !isZoneVariant(item.variant)) occupied.append(itemBounds(item));
    }
    if (regions.isEmpty()) {
        double right = 0.0;
        for (const auto& rect : occupied) right = std::max(right, rect.right());
        const QRectF fallback(occupied.isEmpty() ? 10.0 : right + 10.0, 10.0, 60.0, 1e6);
        QPainterPath shape;
        shape.addRect(fallback);
        regions.append({shape, fallback});
    }
    auto snapUp = [grid](double value) { return grid > 0 ? std::ceil(value / grid - 1e-9) * grid : value; };
    const double step = grid > 0 ? grid : 0.254;
    for (SketchItem part : parts) {
        part.points = {{0, 0}};
        const QRectF local = itemBounds(part);
        bool fitted = false;
        int attempts = 0;
        for (const PlacementRegion& region : regions) {
            const double firstX = snapUp(region.bounds.left() + gap - local.left());
            const double firstY = snapUp(region.bounds.top() + gap - local.top());
            const double lastX = region.bounds.right() - gap - local.right();
            const double lastY = region.bounds.bottom() - gap - local.bottom();
            for (double y = firstY; y <= lastY + 1e-9 && !fitted && attempts < 250000; y += step) {
                for (double x = firstX; x <= lastX + 1e-9 && attempts < 250000; x += step) {
                    ++attempts;
                    const QPointF origin(x, y);
                    const QRectF placed = local.translated(origin);
                    const QRectF envelope = placed.adjusted(-gap, -gap, gap, gap);
                    if (!region.shape.contains(envelope)) continue;
                    const bool blocked = std::any_of(occupied.cbegin(), occupied.cend(),
                                                     [&](const QRectF& rect) {
                        return rect.adjusted(-gap / 2, -gap / 2, gap / 2, gap / 2)
                            .intersects(placed.adjusted(-gap / 2, -gap / 2, gap / 2, gap / 2));
                    });
                    if (blocked) continue;
                    part.points = {origin};
                    occupied.append(placed);
                    result.append(part);
                    fitted = true;
                    break;
                }
            }
            if (fitted) break;
        }
    }
    return result;
}

QString netlistText(const SketchDocument& schematic, QStringList* errors) {
    const auto snapshot = analyzeSchematic(schematic);
    if (errors) *errors = snapshot.errors;
    if (!snapshot.errors.isEmpty()) return {};
    QHash<QString, QString> references;
    for (const auto& item : schematic) references.insert(item.id, item.label);
    QString text = QStringLiteral("* HattEDA netlist\n* %1 nets\n").arg(snapshot.connectivity.nets.size());
    text += QStringLiteral("*PARTS\n");
    for (const auto& item : schematic) {
        if (!component(item)) continue;
        text += QStringLiteral("%1 %2 %3 %4\n").arg(item.label, item.variant,
                                                    item.value.isEmpty() ? QStringLiteral("-") : item.value,
                                                    item.footprint.isEmpty() ? QStringLiteral("-") : item.footprint);
    }
    text += QStringLiteral("*NETS\n");
    for (const auto& net : snapshot.connectivity.nets) {
        QStringList members;
        for (int p : net.pins) {
            const auto& pin = snapshot.input.pins[p];
            const QString reference = references.value(QString::fromStdString(pin.component));
            // Ports, rails and probes name nets but are not parts.
            if (reference.isEmpty() || !std::any_of(schematic.begin(), schematic.end(), [&](const SketchItem& item) {
                    return item.id == QString::fromStdString(pin.component) && component(item);
                })) continue;
            members << reference + QLatin1Char('.') + QString::fromStdString(pin.number);
        }
        if (members.isEmpty()) continue;
        text += QStringLiteral("%1: %2\n").arg(QString::fromStdString(net.name), members.join(QLatin1Char(' ')));
    }
    return text;
}

BoardGuidance boardGuidance(const SketchDocument& schematic, const SketchDocument& board) {
    BoardGuidance result;
    const auto circuit = analyzeSchematic(schematic);
    result.errors = circuit.errors;
    if (!result.errors.isEmpty()) return result;
    const auto nets = pinNets(circuit);
    QHash<QString, const SketchItem*> sources;
    for (const auto& item : schematic) if (boardComponent(item)) sources.insert(item.id, &item);
    electrical::ConnectivityInput copper;
    QVector<int> expected;
    QVector<QPointF> locations;
    QSet<QString> links;
    // Track segments with their copper layer bit; only crossings on one layer conduct.
    QVector<QPair<QLineF, unsigned>> segments;
    for (const auto& item : board) {
        if (item.kind == SketchItem::Kind::Wire) {
            electrical::Wire wire;
            for (auto p : item.points) wire.points.push_back(point(p));
            wire.layers = static_cast<unsigned>(itemCopperLayers(item));
            copper.wires.push_back(wire);
            for (int i = 1; i < item.points.size(); ++i)
                segments.append({QLineF(item.points[i - 1], item.points[i]), wire.layers});
        }
        // Vias and free pads join the tracks on their copper layers at their centre.
        if (item.kind == SketchItem::Kind::Via || item.kind == SketchItem::Kind::Pad) {
            for (const auto& pad : itemPads(item)) {
                copper.junctions.push_back(point(pad.center));
                copper.junctionLayers.push_back(static_cast<unsigned>(pad.layers & CopperLayerMask));
            }
        }
        if (item.kind != SketchItem::Kind::Symbol) continue;
        const auto* footprint = findSymbol(item.variant);
        if (!footprint || footprint->workspace != Workspace::Board) continue;
        const auto* source = sources.value(item.sourceId, nullptr);
        if (!item.sourceId.isEmpty() && (!source || links.contains(item.sourceId))) {
            result.errors << tr("Missing or duplicate PCB source link: %1").arg(item.label);
        }
        if (source) {
            links.insert(item.sourceId);
            if (item.variant != source->footprint || item.pinPadMap != source->pinPadMap)
                result.errors << tr("%1: PCB mapping is out of date; update the PCB.").arg(item.label);
        }
        const QVector<PlacedPad> placedPads = itemPads(item);
        for (int pad = 0; pad < footprint->pins.size(); ++pad) {
            const QPointF location = symbolToWorld(item, footprint->pins[pad]);
            // SMD pads conduct on one side only (mirrored for bottom side parts).
            const unsigned layers = pad < placedPads.size()
                                        ? static_cast<unsigned>(placedPads[pad].layers & CopperLayerMask)
                                        : static_cast<unsigned>(CopperLayerMask);
            copper.pins.push_back({item.id.toStdString(), std::to_string(pad + 1), point(location), layers});
            locations.append(location);
            int net = -1;
            if (source) {
                const int pin = source->pinPadMap.indexOf(pad + 1);
                if (pin >= 0) net = nets.value(pinKey(source->id, pin + 1), -1);
            }
            expected.append(net);
        }
    }
    for (auto it = sources.cbegin(); it != sources.cend(); ++it)
        if (!links.contains(it.key())) result.errors << tr("%1: not yet transferred to PCB.").arg(it.value()->label);
    // Tracks crossing on the same copper layer physically connect; other layers pass over.
    copper.junctionLayers.resize(copper.junctions.size(), electrical::AllLayers);
    for (int i = 0; i < segments.size(); ++i) for (int j = i + 1; j < segments.size(); ++j) {
        const unsigned shared = segments[i].second & segments[j].second;
        QPointF p;
        if (shared != 0 && segments[i].first.intersects(segments[j].first, &p) == QLineF::BoundedIntersection) {
            copper.junctions.push_back(point(p));
            copper.junctionLayers.push_back(shared);
        }
    }
    const auto routed = electrical::buildConnectivity(copper);
    for (const auto& e : routed.errors) result.errors << QString::fromStdString(e);
    if (!routed.errors.empty()) return result;
    for (const auto& net : routed.nets) {
        QSet<int> designNets;
        for (int pin : net.pins) if (expected[pin] >= 0) designNets.insert(expected[pin]);
        if (designNets.size() > 1) result.errors << tr("PCB short: copper joins different schematic nets.");
    }
    for (int n = 0; n < static_cast<int>(circuit.connectivity.nets.size()); ++n) {
        QVector<int> pads;
        for (int p = 0; p < expected.size(); ++p) if (expected[p] == n) pads.append(p);
        if (pads.size() < 2) continue;
        const QString netName = QString::fromStdString(circuit.connectivity.nets[n].name);
        QSet<int> joined;
        joined.insert(routed.pinNets[pads.first()]);
        for (;;) {
            double best = std::numeric_limits<double>::infinity();
            int from = -1, to = -1;
            for (int a : pads) if (joined.contains(routed.pinNets[a]))
                for (int b : pads) if (!joined.contains(routed.pinNets[b])) {
                    const double d = QLineF(locations[a], locations[b]).length();
                    if (d < best) { best = d; from = a; to = b; }
                }
            if (to < 0) break;
            result.airwires.append({QLineF(locations[from], locations[to]), netName});
            joined.insert(routed.pinNets[to]);
        }
    }
    result.errors.removeDuplicates();
    return result;
}

SketchDocument dcDividerExample() {
    SketchDocument document;
    auto symbol = [&](QString variant, QString label, QPointF location, int turns, QString value) {
        SketchItem item;
        item.kind = SketchItem::Kind::Symbol;
        item.variant = variant; item.label = label; item.points = {location};
        item.quarterTurns = turns; item.value = value;
        if (variant == QLatin1String("schematic.resistor")) item.footprint = QStringLiteral("board.r0603");
        if (variant == QLatin1String("schematic.vdc")) item.footprint = QStringLiteral("board.header-1x2");
        if (!item.footprint.isEmpty()) item.pinPadMap = {1, 2};
        document.append(item);
    };
    symbol(QStringLiteral("schematic.vdc"), QStringLiteral("V1"), {20.32, 30.48}, 0, QStringLiteral("5"));
    symbol(QStringLiteral("schematic.resistor"), QStringLiteral("R1"), {45.72, 20.32}, 0, QStringLiteral("1k"));
    symbol(QStringLiteral("schematic.resistor"), QStringLiteral("R2"), {60.96, 30.48}, 1, QStringLiteral("1k"));
    symbol(QStringLiteral("schematic.ground"), {}, {20.32, 45.72}, 0, {});
    auto wire = [&](std::initializer_list<QPointF> points) {
        SketchItem item; item.kind = SketchItem::Kind::Wire; item.points = points; document.append(item);
    };
    wire({{20.32, 25.4}, {20.32, 20.32}, {40.64, 20.32}});
    wire({{50.8, 20.32}, {60.96, 20.32}, {60.96, 25.4}});
    wire({{20.32, 35.56}, {20.32, 45.72}, {60.96, 45.72}, {60.96, 35.56}});
    return document;
}

} // namespace hatt::ui
