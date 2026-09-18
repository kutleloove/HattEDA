#include "hatt/ui/ComponentLibrary.hpp"

#include <QCoreApplication>
#include <QRectF>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace hatt::ui {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("hatt::ui::ComponentLibrary", text); }

struct StyleToken {
    PackageStyle style;
    const char* token;
};
// Stable file tokens; never reuse or rename an entry.
constexpr StyleToken StyleTokens[] = {
    {PackageStyle::None, "none"},         {PackageStyle::TwoTerminal, "two-terminal"},
    {PackageStyle::SingleRow, "single-row"}, {PackageStyle::DualRow, "dual-row"},
    {PackageStyle::QuadRow, "quad-row"},
};

constexpr double SilkMargin = 0.25;
constexpr double SchematicGrid = 2.54;

struct PlacedPad {
    QPointF center;
    double sizeX = 0.0;
    double sizeY = 0.0;
};

double roundTo(double value, double step) { return std::round(value / step) * step; }

QVector<PlacedPad> placePads(const FootprintParams& p) {
    QVector<PlacedPad> pads;
    const int n = p.padCount;
    const double half = p.rowSpacing / 2.0;
    const double w = p.padWidth;
    const double l = p.shape == PadShape::Round ? p.padWidth : p.padLength;
    auto offset = [&](int index, int count) { return (index - (count - 1) / 2.0) * p.pitch; };
    switch (p.style) {
    case PackageStyle::TwoTerminal:
        pads = {{{-half, 0.0}, w, l}, {{half, 0.0}, w, l}};
        break;
    case PackageStyle::SingleRow:
        for (int i = 0; i < n; ++i) pads.append({{0.0, offset(i, n)}, w, l});
        break;
    case PackageStyle::DualRow: {
        const int m = n / 2;
        for (int i = 0; i < m; ++i) pads.append({{-half, offset(i, m)}, w, l});
        for (int i = 0; i < m; ++i) pads.append({{half, -offset(i, m)}, w, l});
        break;
    }
    case PackageStyle::QuadRow: {
        const int k = n / 4;
        for (int i = 0; i < k; ++i) pads.append({{-half, offset(i, k)}, w, l});
        for (int i = 0; i < k; ++i) pads.append({{offset(i, k), half}, l, w});
        for (int i = 0; i < k; ++i) pads.append({{half, -offset(i, k)}, w, l});
        for (int i = 0; i < k; ++i) pads.append({{-offset(i, k), -half}, l, w});
        break;
    }
    case PackageStyle::None:
        break;
    }
    return pads;
}

QRectF padRect(const PlacedPad& pad) {
    return {pad.center.x() - pad.sizeX / 2, pad.center.y() - pad.sizeY / 2, pad.sizeX, pad.sizeY};
}

SymbolShape rectangleShape(const QRectF& rect) {
    SymbolShape shape;
    shape.points = {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
    shape.closed = true;
    return shape;
}

SymbolShape circleShape(QPointF center, double radius, bool filled) {
    SymbolShape shape;
    for (int i = 0; i < 16; ++i) {
        const double angle = 2.0 * std::numbers::pi * i / 16.0;
        shape.points.append(center + QPointF(std::cos(angle), std::sin(angle)) * radius);
    }
    shape.closed = true;
    shape.filled = filled;
    return shape;
}

SymbolShape lineShape(QPointF a, QPointF b) {
    SymbolShape shape;
    shape.points = {a, b};
    return shape;
}

bool geometryKnown(const DeviceSpec& s) {
    return s.package != PackageStyle::None || s.pitch > 0 || s.rowSpacing > 0 || s.bodyWidth > 0 ||
           s.bodyLength > 0 || s.leadWidth > 0 || s.leadLength > 0;
}

bool styleFits(PackageStyle style, int pads) {
    switch (style) {
    case PackageStyle::TwoTerminal: return pads == 2;
    case PackageStyle::SingleRow: return pads >= 1;
    case PackageStyle::DualRow: return pads >= 2 && pads % 2 == 0;
    case PackageStyle::QuadRow: return pads >= 4 && pads % 4 == 0;
    case PackageStyle::None: return false;
    }
    return false;
}

} // namespace

QString newCustomDeviceId() {
    return CustomDevicePrefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString newCustomFootprintId() {
    return CustomFootprintPrefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString packageStyleToken(PackageStyle style) {
    for (const auto& entry : StyleTokens) {
        if (entry.style == style) return QString::fromLatin1(entry.token);
    }
    return QStringLiteral("none");
}

bool packageStyleFromToken(const QString& token, PackageStyle& style) {
    for (const auto& entry : StyleTokens) {
        if (token == QLatin1String(entry.token)) {
            style = entry.style;
            return true;
        }
    }
    return false;
}

QString validateFootprintParams(const FootprintParams& p) {
    const int n = p.padCount;
    if (n < 1 || n > MaxGeneratedPads) return tr("The pad count must be between 1 and %1.").arg(MaxGeneratedPads);
    switch (p.style) {
    case PackageStyle::None:
        return tr("Choose a pad arrangement.");
    case PackageStyle::TwoTerminal:
        if (n != 2) return tr("A two-terminal footprint has exactly 2 pads.");
        break;
    case PackageStyle::SingleRow:
        break;
    case PackageStyle::DualRow:
        if (!styleFits(p.style, n)) return tr("A dual-row footprint needs an even number of pads.");
        break;
    case PackageStyle::QuadRow:
        if (!styleFits(p.style, n)) return tr("A quad footprint needs a pad count divisible by 4.");
        break;
    }
    const bool rows = p.style != PackageStyle::TwoTerminal;
    const int perRow = p.style == PackageStyle::DualRow ? n / 2 : p.style == PackageStyle::QuadRow ? n / 4 : n;
    if (rows && perRow > 1 && !(p.pitch > 0)) return tr("The pitch must be greater than zero.");
    if (p.style != PackageStyle::SingleRow && !(p.rowSpacing > 0))
        return tr("The row spacing must be greater than zero.");
    if (!(p.padWidth > 0) || (p.shape != PadShape::Round && !(p.padLength > 0)))
        return tr("Pad sizes must be greater than zero.");
    if (p.drill < 0 || p.bodyWidth < 0 || p.bodyLength < 0) return tr("Sizes cannot be negative.");
    const double smallest = p.shape == PadShape::Round ? p.padWidth : std::min(p.padWidth, p.padLength);
    if (p.drill > 0 && p.drill >= smallest) return tr("The drill must be smaller than the pad.");
    const auto pads = placePads(p);
    for (int a = 0; a < pads.size(); ++a) {
        const QRectF first = padRect(pads[a]).adjusted(1e-6, 1e-6, -1e-6, -1e-6);
        for (int b = a + 1; b < pads.size(); ++b) {
            if (first.intersects(padRect(pads[b]))) {
                return tr("Pads %1 and %2 overlap; increase the pitch or row spacing, or make the pads smaller.")
                    .arg(a + 1)
                    .arg(b + 1);
            }
        }
    }
    return {};
}

QString validateExplicitFootprint(const FootprintDefinition& footprint) {
    const int n = footprint.pads.size();
    if (n < 1 || n > MaxGeneratedPads) return tr("The pad count must be between 1 and %1.").arg(MaxGeneratedPads);
    if (footprint.pins.size() != n) return tr("Every pad needs a position.");
    QVector<bool> used(n + 1, false);
    for (int i = 0; i < n; ++i) {
        const PadDefinition& pad = footprint.pads[i];
        const QPointF at = footprint.pins[i];
        if (!std::isfinite(at.x()) || !std::isfinite(at.y())) return tr("Pad %1 has an invalid position.").arg(i + 1);
        if (pad.number < 1 || pad.number > n || used[pad.number])
            return tr("Pad numbers must run from 1 to %1 without repeats.").arg(n);
        used[pad.number] = true;
        if (!(pad.width > 0) || !(pad.height > 0) || !std::isfinite(pad.width) || !std::isfinite(pad.height))
            return tr("Pad sizes must be greater than zero.");
        if (!(pad.drillDiameter >= 0) || !std::isfinite(pad.drillDiameter)) return tr("Sizes cannot be negative.");
        if (pad.drillDiameter > 0 && pad.drillDiameter >= std::min(pad.width, pad.height))
            return tr("The drill must be smaller than the pad.");
        if (pad.layers == 0) return tr("Pad %1 is on no copper layer.").arg(pad.number);
    }
    for (const SymbolShape& shape : footprint.shapes) {
        for (const QPointF& point : shape.points) {
            if (!std::isfinite(point.x()) || !std::isfinite(point.y()))
                return tr("The silkscreen has an invalid point.");
        }
    }
    return {};
}

QString validatePinPadMap(const QVector<int>& map, int pinCount) {
    if (map.isEmpty()) return {};
    if (map.size() != pinCount) return tr("Every pin needs a pad.");
    QVector<bool> used(pinCount + 1, false);
    for (int i = 0; i < map.size(); ++i) {
        const int pad = map[i];
        if (pad < 1 || pad > pinCount) return tr("Pin %1 is mapped to a pad that does not exist.").arg(i + 1);
        if (used[pad]) return tr("Pad %1 is used by more than one pin.").arg(pad);
        used[pad] = true;
    }
    return {};
}

SymbolDefinition footprintSymbol(const FootprintDefinition& footprint) {
    const FootprintParams& p = footprint.params;
    SymbolDefinition symbol;
    symbol.id = footprint.id;
    symbol.displayName = footprint.name;
    symbol.workspace = Workspace::Board;
    symbol.category = SymbolCategory::Component;
    if (footprint.isExplicit()) {
        // Explicit pads are stored in pad number order so pinPadMap numbers index `pins`.
        QVector<int> order(footprint.pads.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&footprint](int a, int b) {
            return footprint.pads[a].number < footprint.pads[b].number;
        });
        symbol.shapes = footprint.shapes;
        for (int index : order) {
            symbol.pins.append(footprint.pins.value(index));
            symbol.pads.append(footprint.pads[index]);
        }
        return symbol;
    }
    const auto placed = placePads(p);
    const bool throughHole = p.drill > 0;
    const int copper = throughHole ? (1 << static_cast<int>(BoardLayer::TopCopper)) |
                                         (1 << static_cast<int>(BoardLayer::BottomCopper))
                                   : (1 << static_cast<int>(BoardLayer::TopCopper));
    QRectF padBounds;
    for (int i = 0; i < placed.size(); ++i) {
        PadDefinition pad;
        pad.number = i + 1;
        // Through-hole pin 1 is square, as usual for DIP and headers.
        pad.shape = throughHole && p.shape == PadShape::Round && i == 0 ? PadShape::Rect : p.shape;
        pad.width = placed[i].sizeX;
        pad.height = placed[i].sizeY;
        pad.drillDiameter = p.drill;
        pad.layers = copper;
        symbol.pins.append(placed[i].center);
        symbol.pads.append(pad);
        padBounds = padBounds.isNull() ? padRect(placed[i]) : padBounds.united(padRect(placed[i]));
    }
    const QRectF body = p.bodyWidth > 0 && p.bodyLength > 0
                            ? QRectF(-p.bodyWidth / 2, -p.bodyLength / 2, p.bodyWidth, p.bodyLength)
                            : padBounds.adjusted(-SilkMargin, -SilkMargin, SilkMargin, SilkMargin);
    symbol.shapes.append(rectangleShape(body));
    if (!placed.isEmpty()) {
        const QRectF first = padRect(placed.first());
        symbol.shapes.append(circleShape(first.topLeft() - QPointF(0.45, 0.45), 0.2, true));
    }
    return symbol;
}

SymbolDefinition deviceSymbol(const DeviceDefinition& device) {
    SymbolDefinition symbol;
    symbol.id = device.id;
    symbol.displayName = device.name;
    symbol.workspace = Workspace::Schematic;
    symbol.category = SymbolCategory::Component;
    symbol.prefix = device.prefix;
    symbol.defaultValue = device.defaultValue;
    symbol.defaultFootprint = device.footprint;
    symbol.simulationModel = device.simulationModel;
    const int n = std::clamp(device.pinCount, 1, MaxGeneratedPads);
    if (validatePinPadMap(device.pinPadMap, n).isEmpty()) symbol.defaultPinPadMap = device.pinPadMap;
    if (n <= 2) {
        symbol.shapes = {rectangleShape(QRectF(-2.54, -1.27, 5.08, 2.54)), lineShape({-5.08, 0}, {-2.54, 0})};
        symbol.pins = {{-5.08, 0}};
        if (n == 2) {
            symbol.shapes.append(lineShape({2.54, 0}, {5.08, 0}));
            symbol.pins.append({5.08, 0});
        }
        return symbol;
    }
    const int left = (n + 1) / 2;
    const int right = n - left;
    const double top = -std::floor((left - 1) / 2.0) * SchematicGrid;
    const double bottom = top + (left - 1) * SchematicGrid;
    symbol.shapes = {rectangleShape(QRectF(-5.08, top - SchematicGrid, 10.16, bottom - top + 2 * SchematicGrid)),
                     circleShape({-3.81, top - 1.27}, 0.45, true)};
    for (int i = 0; i < left; ++i) {
        const double y = top + i * SchematicGrid;
        symbol.shapes.append(lineShape({-7.62, y}, {-5.08, y}));
        symbol.pins.append({-7.62, y});
    }
    for (int i = 0; i < right; ++i) {
        const double y = top + (right - 1 - i) * SchematicGrid;
        symbol.shapes.append(lineShape({5.08, y}, {7.62, y}));
        symbol.pins.append({7.62, y});
    }
    return symbol;
}

double recommendedCopperWidth(double current, double copperOunces, double temperatureRise) {
    if (!(current > 0) || !(copperOunces > 0) || !(temperatureRise > 0)) return 0.0;
    // IPC-2221 external conductors: I = 0.048 · ΔT^0.44 · A^0.725, A in square mils.
    const double area = std::pow(current / (0.048 * std::pow(temperatureRise, 0.44)), 1.0 / 0.725);
    const double thicknessMils = 1.378 * copperOunces;
    return area / thicknessMils * 0.0254;
}

FootprintSuggestion suggestFootprint(const DeviceDefinition& device) {
    FootprintSuggestion result;
    const DeviceSpec& s = device.spec;
    const int n = std::clamp(device.pinCount, 1, MaxGeneratedPads);
    const bool known = geometryKnown(s);
    result.fromDatasheet = known;
    FootprintParams& p = result.params;
    p.padCount = n;

    PackageStyle style = s.package;
    if (style == PackageStyle::None) {
        style = n == 2 && known && !s.throughHole ? PackageStyle::TwoTerminal
                : n >= 4 && n % 2 == 0          ? PackageStyle::DualRow
                                                : PackageStyle::SingleRow;
    } else if (!styleFits(style, n)) {
        result.notes << tr("The datasheet package does not fit %1 pins; a single row is used.").arg(n);
        style = PackageStyle::SingleRow;
    }
    p.style = style;
    // Without datasheet data the generic starting point is a 2.54 mm through-hole part.
    const bool throughHole = s.throughHole || !known;
    p.pitch = s.pitch > 0 ? s.pitch : throughHole ? 2.54 : 1.27;
    if (style == PackageStyle::TwoTerminal) {
        p.rowSpacing = s.rowSpacing > 0 ? s.rowSpacing : s.bodyWidth > 0 ? s.bodyWidth : 1.6;
    } else if (s.rowSpacing > 0) {
        p.rowSpacing = s.rowSpacing;
    } else if (throughHole) {
        p.rowSpacing = n >= 24 ? 15.24 : 7.62;
    } else {
        p.rowSpacing = s.bodyWidth > 0 ? s.bodyWidth + std::max(s.leadLength, 1.0) : 5.4;
    }
    p.bodyWidth = s.bodyWidth;
    p.bodyLength = s.bodyLength;

    const bool singlePad = style == PackageStyle::TwoTerminal || n == 1;
    const double maxAlong = singlePad ? 1e9 : p.pitch - 0.2;
    if (throughHole) {
        p.drill = s.leadWidth > 0 ? std::ceil((s.leadWidth + 0.3) * 10.0 - 1e-9) / 10.0 : 0.8;
        p.shape = PadShape::Round;
        p.padWidth = std::min(std::max(p.drill + 0.6, 1.6), std::max(p.drill + 0.3, maxAlong));
        p.padLength = p.padWidth;
    } else {
        p.drill = 0.0;
        p.shape = PadShape::Rect;
        if (style == PackageStyle::TwoTerminal) {
            p.padWidth = s.leadLength > 0 ? s.leadLength + 0.3 : std::max(0.6, p.rowSpacing * 0.5);
            p.padLength = s.bodyLength > 0 ? s.bodyLength + 0.2 : 1.0;
        } else {
            p.padWidth = s.leadLength > 0 ? s.leadLength + 0.6 : style == PackageStyle::SingleRow ? 1.0 : 1.5;
            p.padLength = std::min(s.leadWidth > 0 ? s.leadWidth + 0.15 : p.pitch * 0.6, maxAlong);
        }
    }

    if (s.pinCurrent > 0) {
        const double copper = recommendedCopperWidth(s.pinCurrent);
        result.notes << tr("%1 A per pin needs copper at least %2 mm wide (IPC-2221, 1 oz, 10 °C rise); "
                           "use traces at least this wide.")
                            .arg(s.pinCurrent)
                            .arg(copper, 0, 'f', 2);
        double& along = throughHole ? p.padWidth : p.padLength;
        const double widened = std::min(std::max(along, copper), maxAlong);
        if (widened < copper) {
            result.notes << tr("The %1 mm pitch limits pads to %2 mm; widen the copper outside the pad row.")
                                .arg(p.pitch)
                                .arg(widened, 0, 'f', 2);
        }
        along = widened;
        if (throughHole) p.padLength = p.padWidth;
    }
    for (double* value : {&p.pitch, &p.rowSpacing, &p.padWidth, &p.padLength, &p.drill}) {
        *value = roundTo(*value, 0.01);
    }
    const QString problem = validateFootprintParams(p);
    if (!problem.isEmpty()) result.notes << problem;
    return result;
}

void registerProjectLibrary(const ProjectLibrary& library) {
    QVector<SymbolDefinition> symbols;
    for (const auto& footprint : library.customFootprints) symbols.append(footprintSymbol(footprint));
    for (const auto& device : library.customDevices) symbols.append(deviceSymbol(device));
    registerSymbols(symbols);
}

QList<const SymbolDefinition*> footprintsWithPads(const ProjectLibrary& library, int padCount) {
    // symbolLibrary() already contains every built-in footprint (#61 PR (b) folded
    // ComponentCatalog's footprints into the same builtin.json); only project-specific footprints
    // need a separate lookup through the runtime registry.
    QList<const SymbolDefinition*> result;
    for (const auto& symbol : symbolLibrary()) {
        if (symbol.workspace == Workspace::Board && symbol.pins.size() == padCount) result.append(&symbol);
    }
    for (const auto& footprint : library.customFootprints) {
        const auto* symbol = findSymbol(footprint.id);
        if (symbol != nullptr && symbol->pins.size() == padCount) result.append(symbol);
    }
    return result;
}

QList<const SymbolDefinition*> pickableDevices(const ProjectLibrary& library) {
    // symbolsFor() already contains every built-in Component-category device, catalog ones
    // included (#61 PR (b)); only project-specific devices need a separate registry lookup.
    QList<const SymbolDefinition*> result = symbolsFor(Workspace::Schematic, SymbolCategory::Component);
    for (const auto& device : library.customDevices) {
        if (const auto* symbol = findSymbol(device.id)) result.append(symbol);
    }
    return result;
}

} // namespace hatt::ui
