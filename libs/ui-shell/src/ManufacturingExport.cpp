#include "hatt/ui/ManufacturingExport.hpp"

#include <QHash>
#include <QPolygonF>

#include <algorithm>

namespace hatt::ui {
namespace {

QString csvField(const QString& text) {
    if (!text.contains(QLatin1Char(',')) && !text.contains(QLatin1Char('"')) && !text.contains(QLatin1Char('\n')) &&
        !text.contains(QLatin1Char('\r'))) {
        return text;
    }
    QString quoted = text;
    quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

void appendRow(QByteArray& csv, const QStringList& fields) {
    QStringList escaped;
    for (const QString& field : fields) escaped << csvField(field);
    csv += escaped.join(QLatin1Char(',')).toUtf8();
    csv += "\r\n";
}

QString millimetres(double value) { return QString::number(value, 'f', 4); }

} // namespace

bool referenceLess(const QString& a, const QString& b) {
    auto split = [](const QString& text) {
        qsizetype digits = text.size();
        while (digits > 0 && text.at(digits - 1).isDigit()) --digits;
        return std::pair{text.left(digits), text.mid(digits).toLongLong()};
    };
    const auto [prefixA, numberA] = split(a);
    const auto [prefixB, numberB] = split(b);
    if (prefixA != prefixB) return prefixA < prefixB;
    if (numberA != numberB) return numberA < numberB;
    return a < b;
}

QVector<BomLine> buildBom(const SketchDocument& schematic, const ProjectLibrary& library) {
    QHash<QString, const DeviceDefinition*> devices;
    for (const auto& device : library.customDevices) devices.insert(device.id, &device);
    QVector<BomLine> lines;
    QHash<QString, int> index;
    for (const auto& item : schematic) {
        const auto* symbol = findSymbol(item.variant);
        if (item.kind != SketchItem::Kind::Symbol || symbol == nullptr || symbol->workspace != Workspace::Schematic ||
            symbol->category != SymbolCategory::Component || item.excludeFromBoard) {
            continue;
        }
        const QString value = item.value.trimmed();
        const QString key = item.variant + QChar(0x1f) + value + QChar(0x1f) + item.footprint;
        if (!index.contains(key)) {
            BomLine line;
            line.value = value;
            line.device = item.variant;
            line.deviceName = symbolDisplayName(*symbol);
            line.footprint = item.footprint;
            if (const auto* footprint = findSymbol(item.footprint)) line.footprintName = symbolDisplayName(*footprint);
            if (const auto* device = devices.value(item.variant, nullptr)) {
                line.manufacturer = device->spec.manufacturer;
                line.partNumber = device->spec.partNumber;
            }
            index.insert(key, lines.size());
            lines.append(line);
        }
        lines[index.value(key)].references << (item.label.trimmed().isEmpty() ? QStringLiteral("?") : item.label.trimmed());
    }
    for (auto& line : lines) std::sort(line.references.begin(), line.references.end(), referenceLess);
    std::sort(lines.begin(), lines.end(), [](const BomLine& a, const BomLine& b) {
        return referenceLess(a.references.first(), b.references.first());
    });
    return lines;
}

// Column names and layer words stay English: assembly services import them by name.
QByteArray bomCsv(const QVector<BomLine>& lines) {
    QByteArray csv;
    appendRow(csv, {QStringLiteral("Item"), QStringLiteral("Quantity"), QStringLiteral("References"), QStringLiteral("Value"), QStringLiteral("Footprint"), QStringLiteral("Device"),
                    QStringLiteral("Manufacturer"), QStringLiteral("Part number")});
    int item = 1;
    for (const auto& line : lines) {
        appendRow(csv, {QString::number(item++), QString::number(line.quantity()), line.references.join(QLatin1Char(' ')),
                        line.value, line.footprintName.isEmpty() ? line.footprint : line.footprintName, line.deviceName,
                        line.manufacturer, line.partNumber});
    }
    return csv;
}

QVector<PlacementLine> buildPlacement(const SketchDocument& board) {
    QVector<PlacementLine> lines;
    for (const auto& item : board) {
        const auto* footprint = findSymbol(item.variant);
        if (item.kind != SketchItem::Kind::Symbol || footprint == nullptr || footprint->workspace != Workspace::Board ||
            item.points.isEmpty()) {
            continue;
        }
        const auto pads = itemPads(item);
        if (pads.isEmpty()) continue; // footprints without pads have nothing to place
        QPolygonF points;
        for (const auto& pad : pads) points << padOutline(pad);
        const QPointF centre = points.boundingRect().center();
        PlacementLine line;
        line.reference = item.label.trimmed().isEmpty() ? QStringLiteral("?") : item.label.trimmed();
        line.value = item.value;
        line.footprint = symbolDisplayName(*footprint);
        line.centre = QPointF(centre.x(), -centre.y());
        // rotateQuarterTurn turns clockwise on screen (Y down), so each turn is -90° counter-clockwise.
        line.rotation = (4 - ((item.quarterTurns % 4) + 4) % 4) % 4 * 90.0;
        line.bottom = item.onBottom;
        lines.append(line);
    }
    std::sort(lines.begin(), lines.end(), [](const PlacementLine& a, const PlacementLine& b) {
        return referenceLess(a.reference, b.reference);
    });
    return lines;
}

QByteArray placementCsv(const QVector<PlacementLine>& lines) {
    QByteArray csv;
    appendRow(csv, {QStringLiteral("Designator"), QStringLiteral("Value"), QStringLiteral("Package"), QStringLiteral("Mid X (mm)"), QStringLiteral("Mid Y (mm)"), QStringLiteral("Rotation"),
                    QStringLiteral("Layer")});
    for (const auto& line : lines) {
        appendRow(csv, {line.reference, line.value, line.footprint, millimetres(line.centre.x()),
                        millimetres(line.centre.y()), QString::number(line.rotation, 'f', 0),
                        line.bottom ? QStringLiteral("Bottom") : QStringLiteral("Top")});
    }
    return csv;
}

} // namespace hatt::ui
