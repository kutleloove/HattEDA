#include "hatt/ui/SpecctraDsn.hpp"

#include "hatt/ui/BoardCopper.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QSet>
#include <QTextStream>

#include <cmath>

namespace hatt::ui {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("hatt::ui::SpecctraDsn", text); }

QString quoted(QString value) {
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}

qint64 coordinate(double millimetres) { return std::llround(millimetres * 1000.0); }

QString shape(const PlacedPad& pad, const QString& layer) {
    const qint64 width = coordinate(pad.width);
    const qint64 height = coordinate(pad.height);
    if (pad.shape == PadShape::Rect) {
        return QStringLiteral("    (shape (rect %1 %2 %3 %4 %5))\n")
            .arg(layer)
            .arg(-width / 2)
            .arg(-height / 2)
            .arg(width / 2)
            .arg(height / 2);
    }
    if (std::abs(pad.width - pad.height) < 1e-6) {
        return QStringLiteral("    (shape (circle %1 %2))\n").arg(layer).arg(width);
    }
    const bool horizontal = width >= height;
    const qint64 thickness = std::min(width, height);
    const qint64 run = std::max(width, height) - thickness;
    return QStringLiteral("    (shape (path %1 %2 %3 %4 %5 %6))\n")
        .arg(layer)
        .arg(thickness)
        .arg(horizontal ? -run / 2 : 0)
        .arg(horizontal ? 0 : -run / 2)
        .arg(horizontal ? run / 2 : 0)
        .arg(horizontal ? 0 : run / 2);
}

} // namespace

SpecctraDsnResult exportSpecctraDsn(const SketchDocument& schematic,
                                    const SketchDocument& board, const DesignRules& rules,
                                    const QString& designName) {
    SpecctraDsnResult result;
    const QString ruleProblem = validateDesignRules(rules);
    if (!ruleProblem.isEmpty()) result.errors << ruleProblem;

    const BoardCopperModel copper = buildBoardCopperModel(schematic, board);
    if (!copper.netsKnown) {
        result.errors << tr("The schematic must pass connectivity checks before autorouting.");
    }

    QVector<QPointF> outline;
    for (const auto& item : board) {
        const QVector<QPointF> candidate = boardOutlinePoints(item);
        if (candidate.isEmpty()) continue;
        if (!outline.isEmpty()) {
            result.errors << tr("Autorouting currently requires exactly one closed board outline.");
            break;
        }
        outline = candidate;
    }
    if (outline.isEmpty()) result.errors << tr("Draw a closed board outline before autorouting.");
    for (const auto& item : board) {
        if (item.kind == SketchItem::Kind::Wire || item.kind == SketchItem::Kind::Via ||
            item.kind == SketchItem::Kind::Pad || isZoneVariant(item.variant)) {
            result.errors
                << tr("This first autorouter export supports unrouted boards without vias or zones.");
            break;
        }
    }
    if (!result.errors.isEmpty()) return result;

    struct Component {
        const SketchItem* item = nullptr;
        QString reference;
        QString image;
        QVector<PlacedPad> pads;
    };
    QVector<Component> components;
    QHash<QString, QString> referenceByItem;
    QSet<QString> references;
    for (const auto& item : board) {
        if (item.kind != SketchItem::Kind::Symbol || item.points.isEmpty()) continue;
        const QVector<PlacedPad> pads = itemPads(item);
        if (pads.isEmpty()) continue;
        QString reference = item.label.trimmed();
        if (reference.isEmpty()) reference = QStringLiteral("HATT%1").arg(components.size() + 1);
        if (references.contains(reference)) {
            result.errors << tr("Duplicate board reference: %1").arg(reference);
            continue;
        }
        references.insert(reference);
        referenceByItem.insert(item.id, reference);
        components.append({&item, reference, QStringLiteral("HATT_IMAGE_%1").arg(components.size() + 1), pads});
    }
    if (!result.errors.isEmpty()) return result;

    QVector<QStringList> netPins(copper.netNames.size());
    QVector<NetClass> netClasses(copper.netNames.size());
    int activeLayers = 0;
    QSet<QString> incompatiblePads;
    for (const auto& conductor : copper.conductors) {
        if (conductor.kind != ConductorKind::Pad || conductor.net < 0 ||
            conductor.net >= netPins.size()) {
            continue;
        }
        const QString reference = referenceByItem.value(conductor.itemId);
        if (reference.isEmpty()) continue;
        netPins[conductor.net] << reference + QLatin1Char('-') +
                                     QString::number(conductor.pad.number);
    }
    for (int net = 0; net < netPins.size(); ++net) {
        if (netPins[net].size() < 2) continue;
        netClasses[net] = netClassForNet(rules, schematic, copper.netNames.value(net));
        activeLayers |= netClasses[net].layers & CopperLayerMask;
    }
    if (activeLayers == 0) {
        result.errors << tr("The board has no routable net on an enabled copper layer.");
        return result;
    }
    for (const auto& conductor : copper.conductors) {
        if (conductor.kind != ConductorKind::Pad || conductor.net < 0 ||
            conductor.net >= netPins.size() || netPins[conductor.net].size() < 2) {
            continue;
        }
        if ((conductor.layers & netClasses[conductor.net].layers & CopperLayerMask) != 0) continue;
        const QString reference = referenceByItem.value(conductor.itemId);
        const QString pad = reference + QLatin1Char('-') + QString::number(conductor.pad.number);
        if (!incompatiblePads.contains(pad)) {
            incompatiblePads.insert(pad);
            result.errors << tr("%1 is not on a copper layer allowed by the %2 net class.")
                                 .arg(pad, netClasses[conductor.net].name);
        }
    }
    if (!result.errors.isEmpty()) return result;

    QString text;
    QTextStream out(&text);
    out << "(pcb " << quoted(designName.isEmpty() ? QStringLiteral("HattEDA") : designName) << "\n"
        << "  (parser (string_quote \") (space_in_quoted_tokens on)"
           " (host_cad \"HattEDA\") (host_version \"0.1\"))\n"
        << "  (resolution um 1)\n  (unit um)\n  (structure\n";
    int layerIndex = 0;
    if (activeLayers & layerBit(BoardLayer::TopCopper)) {
        out << "    (layer F.Cu (type signal) (property (index " << layerIndex++ << ")))\n";
    }
    if (activeLayers & layerBit(BoardLayer::BottomCopper)) {
        out << "    (layer B.Cu (type signal) (property (index " << layerIndex++ << ")))\n";
    }
    out << "    (boundary (path pcb 0";
    for (const QPointF& point : outline) {
        out << ' ' << coordinate(point.x()) << ' ' << coordinate(-point.y());
    }
    out << ' ' << coordinate(outline.first().x()) << ' '
        << coordinate(-outline.first().y()) << "))\n"
        << "    (via \"HATT_VIA\")\n"
        << "    (rule (width " << coordinate(rules.minTrackWidth) << ") (clearance "
        << coordinate(rules.clearance) << "))\n  )\n";

    out << "  (placement\n";
    for (const Component& component : components) {
        const QPointF origin = component.item->points.first();
        out << "    (component " << quoted(component.image) << " (place "
            << quoted(component.reference) << ' ' << coordinate(origin.x()) << ' '
            << coordinate(-origin.y()) << " front 0))\n";
    }
    out << "  )\n  (library\n";
    for (const Component& component : components) {
        const QPointF origin = component.item->points.first();
        out << "    (image " << quoted(component.image) << "\n";
        for (int index = 0; index < component.pads.size(); ++index) {
            const PlacedPad& pad = component.pads[index];
            const QString padstack = QStringLiteral("HATT_PAD_%1_%2").arg(component.image).arg(index + 1);
            out << "      (pin " << quoted(padstack) << ' ' << pad.number << ' '
                << coordinate(pad.center.x() - origin.x()) << ' '
                << coordinate(-(pad.center.y() - origin.y())) << ")\n";
        }
        out << "    )\n";
    }
    for (const Component& component : components) {
        for (int index = 0; index < component.pads.size(); ++index) {
            const PlacedPad& pad = component.pads[index];
            const QString padstack = QStringLiteral("HATT_PAD_%1_%2").arg(component.image).arg(index + 1);
            out << "    (padstack " << quoted(padstack) << "\n";
            if ((activeLayers & pad.layers & layerBit(BoardLayer::TopCopper)) != 0)
                out << shape(pad, QStringLiteral("F.Cu"));
            if ((activeLayers & pad.layers & layerBit(BoardLayer::BottomCopper)) != 0)
                out << shape(pad, QStringLiteral("B.Cu"));
            out << "      (attach off)\n    )\n";
        }
    }
    out << "    (padstack \"HATT_VIA\"\n";
    if (activeLayers & layerBit(BoardLayer::TopCopper))
        out << "      (shape (circle F.Cu 800))\n";
    if (activeLayers & layerBit(BoardLayer::BottomCopper))
        out << "      (shape (circle B.Cu 800))\n";
    out << "      (attach off)\n    )\n  )\n  (network\n";
    for (int net = 0; net < netPins.size(); ++net) {
        if (netPins[net].size() < 2) continue;
        out << "    (net " << quoted(copper.netNames.value(net)) << " (pins "
            << netPins[net].join(QLatin1Char(' ')) << "))\n";
    }
    for (int net = 0; net < netPins.size(); ++net) {
        if (netPins[net].size() < 2) continue;
        const QString netName = copper.netNames.value(net);
        const NetClass& netClass = netClasses[net];
        const double width = std::max(rules.minTrackWidth, netClass.traceWidth);
        const double clearance = std::max(rules.clearance, netClass.clearance);
        out << "    (class " << quoted(QStringLiteral("HATT_NET_%1").arg(net + 1)) << ' '
            << quoted(netName) << " (circuit";
        if ((netClass.layers & CopperLayerMask) == CopperLayerMask)
            out << " (use_via \"HATT_VIA\")";
        out << " (use_layer";
        if (netClass.layers & layerBit(BoardLayer::TopCopper)) out << " F.Cu";
        if (netClass.layers & layerBit(BoardLayer::BottomCopper)) out << " B.Cu";
        out << ")) (rule (width "
            << coordinate(width) << ") (clearance " << coordinate(clearance) << ")))\n";
    }
    out << "  )\n  (wiring)\n)\n";
    result.data = text.toUtf8();
    return result;
}

} // namespace hatt::ui
