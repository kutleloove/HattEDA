#include "hatt/ui/SketchModel.hpp"
#include "hatt/ui/LibraryModel.hpp"

#include <QCoreApplication>
#include <QPolygonF>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <vector>

namespace hatt::ui {
namespace {

constexpr double Pi = 3.14159265358979323846;

// v2 pad helpers — build PadDefinition for footprint pad lists.
PadDefinition makePad(int number, double w, double h) {
    PadDefinition p;
    p.number = number;
    p.shape = PadShape::Rect;
    p.width = w;
    p.height = h;
    p.drillDiameter = 0.0;
    p.layers = (1 << static_cast<int>(BoardLayer::TopCopper));
    return p;
}

PadDefinition makeRoundPad(int number, double diameter) {
    PadDefinition p;
    p.number = number;
    p.shape = PadShape::Round;
    p.width = diameter;
    p.height = diameter;
    p.drillDiameter = 0.0;
    p.layers = (1 << static_cast<int>(BoardLayer::TopCopper));
    return p;
}

PadDefinition makeThroughPad(int number, double diameter, double drill) {
    const int bothCopper = (1 << static_cast<int>(BoardLayer::TopCopper))
                         | (1 << static_cast<int>(BoardLayer::BottomCopper));
    PadDefinition p;
    p.number = number;
    p.shape = PadShape::Round;
    p.width = diameter;
    p.height = diameter;
    p.drillDiameter = drill;
    p.layers = bothCopper;
    return p;
}

PadDefinition makeSquareThroughPad(int number, double size, double drill) {
    const int bothCopper = (1 << static_cast<int>(BoardLayer::TopCopper))
                         | (1 << static_cast<int>(BoardLayer::BottomCopper));
    PadDefinition p;
    p.number = number;
    p.shape = PadShape::Rect;
    p.width = size;
    p.height = size;
    p.drillDiameter = drill;
    p.layers = bothCopper;
    return p;
}

QPointF rotateQuarter(QPointF point) { return {-point.y(), point.x()}; }

void appendPolyline(QVector<QLineF>& segments, const QVector<QPointF>& points, bool closed) {
    for (qsizetype i = 1; i < points.size(); ++i) {
        segments.append(QLineF(points[i - 1], points[i]));
    }
    if (closed && points.size() > 2) {
        segments.append(QLineF(points.last(), points.first()));
    }
}

// Box of a Text item: `width` is the text height (0 = TextHeightMm). Wide enough for both the
// schematic's proportional font and the board's single-stroke font (StrokeFont: 1 unit per 6).
QSizeF textBox(const SketchItem& item) {
    const double height = item.width > 0.0 ? item.width : TextHeightMm;
    const auto glyphs = static_cast<double>(std::max<qsizetype>(1, item.label.size()));
    return {std::max(glyphs * height * 0.55, (6.0 * glyphs - 2.0) / 6.0 * height), height};
}

} // namespace

const QVector<SymbolDefinition>& symbolLibrary() {
    // Data-driven since #61 (ADR-0017): loaded from the embedded builtin.json resource by
    // LibraryModel.hpp, not built from C++ literals.
    return builtInLibrary().symbols;
}

namespace {
// Project-defined symbols by id. Replaced definitions move to `retired` so earlier pointers stay
// valid; the registry is only touched from the GUI thread.
struct SymbolRegistry {
    std::map<QString, std::unique_ptr<SymbolDefinition>> symbols;
    std::vector<std::unique_ptr<SymbolDefinition>> retired;
};
SymbolRegistry& registry() {
    static SymbolRegistry instance;
    return instance;
}
} // namespace

const SymbolDefinition* findSymbol(const QString& id) {
    for (const auto& symbol : symbolLibrary()) {
        if (symbol.id == id) {
            return &symbol;
        }
    }
    const auto found = registry().symbols.find(id);
    if (found != registry().symbols.end()) {
        return found->second.get();
    }
    // Legacy id (schematic.*, board.*; #61/ADR-0017): resolved once here so every caller --
    // rendering, connectivity, existing tests, ProjectFile's reader -- transparently sees the
    // current lib.* symbol without special-casing old ids at each call site.
    const auto alias = builtInLibrary().aliases.constFind(id);
    if (alias != builtInLibrary().aliases.constEnd() && alias.value() != id) {
        return findSymbol(alias.value());
    }
    return nullptr;
}

void registerSymbols(const QVector<SymbolDefinition>& symbols) {
    auto& entries = registry();
    for (const auto& symbol : symbols) {
        auto& slot = entries.symbols[symbol.id];
        if (slot) entries.retired.push_back(std::move(slot));
        slot = std::make_unique<SymbolDefinition>(symbol);
    }
}

QList<const SymbolDefinition*> symbolsFor(Workspace workspace, SymbolCategory category) {
    QList<const SymbolDefinition*> result;
    for (const auto& symbol : symbolLibrary()) {
        if (symbol.workspace == workspace && symbol.category == category) {
            result.append(&symbol);
        }
    }
    return result;
}

QString symbolDisplayName(const SymbolDefinition& symbol) {
    if (!symbol.displayName.isEmpty()) return symbol.displayName;
    return QCoreApplication::translate("hatt::ui::SymbolLibrary", symbol.name);
}

bool isPickableDevice(const QString& id) {
    const auto* symbol = findSymbol(id);
    return symbol != nullptr && symbol->workspace == Workspace::Schematic &&
           symbol->category == SymbolCategory::Component;
}

QStringList placedDevices(const SketchDocument& schematic) {
    QStringList result;
    for (const auto& item : schematic) {
        if (item.kind == SketchItem::Kind::Symbol && isPickableDevice(item.variant) &&
            !result.contains(item.variant)) {
            result.append(item.variant);
        }
    }
    return result;
}

QStringList projectDeviceList(const ProjectLibrary& library, const SketchDocument& schematic) {
    QStringList result;
    for (const auto& id : library.devices + placedDevices(schematic)) {
        if (isPickableDevice(id) && !result.contains(id)) result.append(id);
    }
    return result;
}

BoardLayer oppositeSideLayer(BoardLayer layer) noexcept {
    switch (layer) {
    case BoardLayer::TopCopper: return BoardLayer::BottomCopper;
    case BoardLayer::BottomCopper: return BoardLayer::TopCopper;
    case BoardLayer::TopSilk: return BoardLayer::BottomSilk;
    case BoardLayer::BottomSilk: return BoardLayer::TopSilk;
    case BoardLayer::TopResist: return BoardLayer::BottomResist;
    case BoardLayer::BottomResist: return BoardLayer::TopResist;
    case BoardLayer::TopPaste: return BoardLayer::BottomPaste;
    case BoardLayer::BottomPaste: return BoardLayer::TopPaste;
    case BoardLayer::BoardEdge: break;
    }
    return BoardLayer::BoardEdge;
}

int mirroredLayerMask(int mask) noexcept {
    int result = 0;
    for (int index = 0; index < BoardLayerCount; ++index) {
        const auto layer = static_cast<BoardLayer>(index);
        if (mask & layerBit(layer)) result |= layerBit(oppositeSideLayer(layer));
    }
    return result;
}

QString boardLayerName(BoardLayer layer) {
    const char* name = "";
    switch (layer) {
    case BoardLayer::TopCopper: name = QT_TRANSLATE_NOOP("hatt::ui::BoardLayer", "Top copper"); break;
    case BoardLayer::BottomCopper: name = QT_TRANSLATE_NOOP("hatt::ui::BoardLayer", "Bottom copper"); break;
    case BoardLayer::TopSilk: name = QT_TRANSLATE_NOOP("hatt::ui::BoardLayer", "Top silk"); break;
    case BoardLayer::BottomSilk: name = QT_TRANSLATE_NOOP("hatt::ui::BoardLayer", "Bottom silk"); break;
    case BoardLayer::TopResist: name = QT_TRANSLATE_NOOP("hatt::ui::BoardLayer", "Top resist"); break;
    case BoardLayer::BottomResist: name = QT_TRANSLATE_NOOP("hatt::ui::BoardLayer", "Bottom resist"); break;
    case BoardLayer::TopPaste: name = QT_TRANSLATE_NOOP("hatt::ui::BoardLayer", "Top paste"); break;
    case BoardLayer::BottomPaste: name = QT_TRANSLATE_NOOP("hatt::ui::BoardLayer", "Bottom paste"); break;
    case BoardLayer::BoardEdge: name = QT_TRANSLATE_NOOP("hatt::ui::BoardLayer", "Board edge"); break;
    }
    return QCoreApplication::translate("hatt::ui::BoardLayer", name);
}

const QVector<TrackStyle>& trackStyles() {
    static const QVector<TrackStyle> styles = {
        {"T8", 0.2032},  {"T10", 0.254}, {"T12", 0.3048}, {"T15", 0.381},
        {"T20", 0.508},  {"T25", 0.635}, {"T30", 0.762},  {"T40", 1.016},
        {"T50", 1.27},   {"T70", 1.778}, {"T100", 2.54},
    };
    return styles;
}

const QVector<ViaStyle>& viaStyles() {
    static const QVector<ViaStyle> styles = {
        {"V24", 0.6, 0.3}, {"V32", 0.8, 0.4}, {"V40", 1.0, 0.5}, {"V50", 1.27, 0.7}, {"V70", 1.8, 1.0},
    };
    return styles;
}

const QVector<PadStyle>& padStyles() {
    static const QVector<PadStyle> styles = [] {
        QVector<PadStyle> result;
        result.append({"pad.round", QT_TRANSLATE_NOOP("hatt::ui::PadStyle", "Round through-hole pad"),
                       makeThroughPad(1, 1.6, 0.8)});
        result.append({"pad.square", QT_TRANSLATE_NOOP("hatt::ui::PadStyle", "Square through-hole pad"),
                       makeSquareThroughPad(1, 1.6, 0.8)});
        PadDefinition oval = makeThroughPad(1, 1.6, 0.8);
        oval.shape = PadShape::Oval;
        oval.height = 2.4;
        result.append({"pad.oval", QT_TRANSLATE_NOOP("hatt::ui::PadStyle", "Oval (DIL) pad"), oval});
        result.append({"pad.smd", QT_TRANSLATE_NOOP("hatt::ui::PadStyle", "SMD rectangular pad"),
                       makePad(1, 1.0, 1.5)});
        result.append({"pad.smd-round", QT_TRANSLATE_NOOP("hatt::ui::PadStyle", "SMD round pad"),
                       makeRoundPad(1, 1.2)});
        // Card edge connector finger (Proteus ARES edge connector pad), 2.54 mm pitch contacts.
        result.append({"pad.edge", QT_TRANSLATE_NOOP("hatt::ui::PadStyle", "Edge connector pad"),
                       makePad(1, 1.78, 7.0)});
        return result;
    }();
    return styles;
}

const PadStyle* findPadStyle(const QString& id) {
    for (const auto& style : padStyles()) {
        if (id == QLatin1String(style.id)) return &style;
    }
    return nullptr;
}

QString padStyleDisplayName(const PadStyle& style) {
    return QCoreApplication::translate("hatt::ui::PadStyle", style.name);
}

double trackWidth(const SketchItem& item) {
    return item.width > 0.0 ? item.width : DefaultTrackWidth;
}

double viaDiameter(const SketchItem& item) {
    return item.width > 0.0 ? item.width : DefaultViaDiameter;
}

double viaDrill(const SketchItem& item) {
    return item.drillDiameter > 0.0 ? item.drillDiameter : DefaultViaDrill;
}

QVector<PlacedPad> itemPads(const SketchItem& item) {
    QVector<PlacedPad> result;
    if (item.points.isEmpty()) return result;
    auto place = [&](const PadDefinition& definition, QPointF center, int turns, bool bottom) {
        PlacedPad pad;
        pad.center = center;
        pad.shape = definition.shape;
        const bool swapped = (turns % 4 + 4) % 2 == 1;
        pad.width = swapped ? definition.height : definition.width;
        pad.height = swapped ? definition.width : definition.height;
        pad.drill = definition.drillDiameter;
        pad.layers = bottom ? mirroredLayerMask(definition.layers) : definition.layers;
        pad.number = definition.number;
        result.append(pad);
    };
    switch (item.kind) {
    case SketchItem::Kind::Symbol:
        if (const auto* symbol = findSymbol(item.variant); symbol != nullptr &&
                                                            symbol->workspace == Workspace::Board) {
            const auto count = std::min(symbol->pads.size(), symbol->pins.size());
            for (qsizetype i = 0; i < count; ++i) {
                place(symbol->pads[i], symbolToWorld(item, symbol->pins[i]), item.quarterTurns,
                      item.onBottom);
            }
        }
        break;
    case SketchItem::Kind::Pad: {
        PadDefinition definition = item.pad;
        if (definition.drillDiameter <= 0.0) {
            // An SMD pad lives on the copper layer of the item.
            definition.layers = layerBit(isCopperLayer(item.layer) ? item.layer : BoardLayer::TopCopper);
        }
        place(definition, item.points.first(), item.quarterTurns, false);
        break;
    }
    case SketchItem::Kind::Via: {
        PadDefinition definition;
        definition.shape = PadShape::Round;
        definition.width = definition.height = viaDiameter(item);
        definition.drillDiameter = viaDrill(item);
        definition.layers = CopperLayerMask;
        place(definition, item.points.first(), 0, false);
        break;
    }
    default:
        break;
    }
    return result;
}

QVector<QPointF> padOutline(const PlacedPad& pad) {
    const double hw = pad.width / 2.0;
    const double hh = pad.height / 2.0;
    QVector<QPointF> points;
    switch (pad.shape) {
    case PadShape::Rect:
        points = {pad.center + QPointF(-hw, -hh), pad.center + QPointF(hw, -hh),
                  pad.center + QPointF(hw, hh), pad.center + QPointF(-hw, hh)};
        break;
    case PadShape::Round:
    case PadShape::Oval: {
        // A stadium: two half circles joined along the longer axis (a circle when square).
        const double radius = std::min(hw, hh);
        const QPointF axis = hw >= hh ? QPointF(hw - radius, 0) : QPointF(0, hh - radius);
        const double start = hw >= hh ? -Pi / 2 : 0.0;
        constexpr int steps = 12;
        for (int i = 0; i <= steps; ++i) {
            const double angle = start + Pi * i / steps;
            points.append(pad.center + axis + QPointF(radius * std::cos(angle), radius * std::sin(angle)));
        }
        for (int i = 0; i <= steps; ++i) {
            const double angle = start + Pi + Pi * i / steps;
            points.append(pad.center - axis + QPointF(radius * std::cos(angle), radius * std::sin(angle)));
        }
        break;
    }
    }
    return points;
}

int itemCopperLayers(const SketchItem& item) {
    switch (item.kind) {
    case SketchItem::Kind::Wire:
        return isCopperLayer(item.layer) ? layerBit(item.layer) : 0;
    case SketchItem::Kind::Symbol:
    case SketchItem::Kind::Pad:
    case SketchItem::Kind::Via: {
        int layers = 0;
        for (const auto& pad : itemPads(item)) layers |= pad.layers & CopperLayerMask;
        return layers;
    }
    case SketchItem::Kind::Polyline:
        // Empty copper zones are only a boundary; keepouts and area zones never conduct.
        return item.variant == CopperZoneVariant && item.zoneFill != ZoneFillStyle::Empty && isCopperLayer(item.layer)
                   ? layerBit(item.layer)
                   : 0;
    default:
        return 0;
    }
}

bool isZoneVariant(const QString& variant) {
    return variant == CopperZoneVariant || variant == KeepoutZoneVariant || variant == AreaZoneVariant;
}

QString zoneFillStyleToken(ZoneFillStyle style) {
    switch (style) {
    case ZoneFillStyle::Solid: return QStringLiteral("solid");
    case ZoneFillStyle::Hatched: return QStringLiteral("hatched");
    case ZoneFillStyle::Empty: return QStringLiteral("empty");
    }
    return QStringLiteral("solid");
}

std::optional<ZoneFillStyle> zoneFillStyleFromToken(const QString& token) {
    for (const ZoneFillStyle style : {ZoneFillStyle::Solid, ZoneFillStyle::Hatched, ZoneFillStyle::Empty}) {
        if (token == zoneFillStyleToken(style)) return style;
    }
    return std::nullopt;
}

QString zoneFillStyleName(ZoneFillStyle style) {
    const char* name = "";
    switch (style) {
    case ZoneFillStyle::Solid: name = QT_TRANSLATE_NOOP("hatt::ui::ZoneFill", "Solid"); break;
    case ZoneFillStyle::Hatched: name = QT_TRANSLATE_NOOP("hatt::ui::ZoneFill", "Hatched"); break;
    case ZoneFillStyle::Empty: name = QT_TRANSLATE_NOOP("hatt::ui::ZoneFill", "Empty"); break;
    }
    return QCoreApplication::translate("hatt::ui::ZoneFill", name);
}

int itemLayerMask(const SketchItem& item) {
    switch (item.kind) {
    case SketchItem::Kind::Symbol: {
        int layers = layerBit(item.onBottom ? BoardLayer::BottomSilk : BoardLayer::TopSilk);
        for (const auto& pad : itemPads(item)) layers |= pad.layers;
        return layers;
    }
    case SketchItem::Kind::Pad:
    case SketchItem::Kind::Via:
        return itemCopperLayers(item);
    default:
        if (item.variant == BoardOutlineVariant) return layerBit(BoardLayer::BoardEdge);
        return layerBit(item.layer);
    }
}

QPointF symbolToWorld(const SketchItem& item, QPointF local) {
    // Bottom side footprints are seen from the top, so they are mirrored left to right. Schematic
    // components (#8) mirror independently, on either axis.
    if (item.onBottom || item.mirroredX) local.setX(-local.x());
    if (item.mirroredY) local.setY(-local.y());
    for (int turn = 0; turn < item.quarterTurns % 4; ++turn) {
        local = rotateQuarter(local);
    }
    return item.points.value(0) + local;
}

QVector<QLineF> itemSegments(const SketchItem& item) {
    QVector<QLineF> segments;
    if (item.points.isEmpty()) {
        return segments;
    }
    switch (item.kind) {
    case SketchItem::Kind::Symbol:
        if (const auto* symbol = findSymbol(item.variant)) {
            for (const auto& shape : symbol->shapes) {
                QVector<QPointF> points;
                points.reserve(shape.points.size());
                for (const QPointF& point : shape.points) {
                    points.append(symbolToWorld(item, point));
                }
                appendPolyline(segments, points, shape.closed);
            }
        }
        for (const auto& pad : itemPads(item)) appendPolyline(segments, padOutline(pad), true);
        break;
    case SketchItem::Kind::Wire:
    case SketchItem::Kind::Line:
    case SketchItem::Kind::Polyline:
        appendPolyline(segments, item.points, item.closed);
        break;
    case SketchItem::Kind::Rectangle: {
        const QRectF rect = QRectF(item.points.value(0), item.points.value(1)).normalized();
        appendPolyline(segments, {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()},
                       true);
        break;
    }
    case SketchItem::Kind::Circle: {
        const QPointF center = item.points.value(0);
        const double radius = QLineF(center, item.points.value(1)).length();
        QVector<QPointF> points;
        for (int i = 0; i < 36; ++i) {
            const double angle = 2 * Pi * i / 36;
            points.append(center + QPointF(radius * std::cos(angle), radius * std::sin(angle)));
        }
        appendPolyline(segments, points, true);
        break;
    }
    case SketchItem::Kind::Arc:
        appendPolyline(segments,
                       arcSamples(item.points.value(0), item.points.value(1), item.points.value(2)),
                       false);
        break;
    case SketchItem::Kind::Text: {
        const QRectF rect(item.points.first(), textBox(item));
        appendPolyline(segments, {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()},
                       true);
        break;
    }
    case SketchItem::Kind::Pad:
    case SketchItem::Kind::Via:
        for (const auto& pad : itemPads(item)) appendPolyline(segments, padOutline(pad), true);
        break;
    }
    return segments;
}

bool isBoardOutline(const SketchItem& item) {
    if (item.variant != BoardOutlineVariant && item.layer != BoardLayer::BoardEdge) return false;
    switch (item.kind) {
    case SketchItem::Kind::Polyline:
        return item.closed && item.points.size() >= 3;
    case SketchItem::Kind::Rectangle:
    case SketchItem::Kind::Circle:
        return item.points.size() >= 2;
    default:
        return false;
    }
}

QVector<QPointF> boardOutlinePoints(const SketchItem& item) {
    if (!isBoardOutline(item)) return {};
    const QVector<QLineF> segments = itemSegments(item);
    if (segments.size() < 3 ||
        QLineF(segments.last().p2(), segments.first().p1()).length() > 1e-6) {
        return {};
    }
    QVector<QPointF> points;
    points.reserve(segments.size());
    for (const QLineF& segment : segments) points.append(segment.p1());
    return points;
}

QVector<QPointF> itemAnchors(const SketchItem& item) {
    switch (item.kind) {
    case SketchItem::Kind::Symbol: {
        QVector<QPointF> anchors;
        if (const auto* symbol = findSymbol(item.variant)) {
            for (const QPointF& pin : symbol->pins) {
                anchors.append(symbolToWorld(item, pin));
            }
        }
        if (anchors.isEmpty()) {
            anchors.append(item.points.value(0));
        }
        return anchors;
    }
    case SketchItem::Kind::Rectangle: {
        const QRectF rect = QRectF(item.points.value(0), item.points.value(1)).normalized();
        return {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
    }
    case SketchItem::Kind::Circle: {
        const QPointF center = item.points.value(0);
        const double radius = QLineF(center, item.points.value(1)).length();
        return {center, center + QPointF(radius, 0), center + QPointF(0, radius),
                center - QPointF(radius, 0), center - QPointF(0, radius)};
    }
    case SketchItem::Kind::Pad:
    case SketchItem::Kind::Via:
        return {item.points.value(0)};
    default:
        return item.points;
    }
}

QSizeF textBoxSize(const SketchItem& item) { return textBox(item); }

QRectF itemBounds(const SketchItem& item) {
    QPolygonF points;
    for (const QLineF& segment : itemSegments(item)) {
        points << segment.p1() << segment.p2();
    }
    for (const QPointF& point : item.points) {
        points << point;
    }
    return points.boundingRect();
}

QVector<QPointF> arcSamples(QPointF start, QPointF through, QPointF end, int segments) {
    const double ax = start.x(), ay = start.y();
    const double mx = through.x(), my = through.y();
    const double bx = end.x(), by = end.y();
    const double d = 2 * (ax * (my - by) + mx * (by - ay) + bx * (ay - my));
    if (std::abs(d) < 1e-9) {
        return {start, end};
    }
    const double a2 = ax * ax + ay * ay;
    const double m2 = mx * mx + my * my;
    const double b2 = bx * bx + by * by;
    const QPointF center((a2 * (my - by) + m2 * (by - ay) + b2 * (ay - my)) / d,
                         (a2 * (bx - mx) + m2 * (ax - bx) + b2 * (mx - ax)) / d);
    const double radius = QLineF(center, start).length();
    auto angleOf = [&center](QPointF point) {
        return std::atan2(point.y() - center.y(), point.x() - center.x());
    };
    auto normalized = [](double angle) {
        while (angle < 0) angle += 2 * Pi;
        while (angle >= 2 * Pi) angle -= 2 * Pi;
        return angle;
    };
    const double startAngle = angleOf(start);
    const double toEnd = normalized(angleOf(end) - startAngle);
    const double toThrough = normalized(angleOf(through) - startAngle);
    const double span = toThrough <= toEnd ? toEnd : toEnd - 2 * Pi;

    QVector<QPointF> points;
    points.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i) {
        const double angle = startAngle + span * i / segments;
        points.append(center + QPointF(radius * std::cos(angle), radius * std::sin(angle)));
    }
    return points;
}

double distanceToSegment(QPointF point, const QLineF& segment, QPointF* nearest) {
    const QPointF delta = segment.p2() - segment.p1();
    const double lengthSquared = QPointF::dotProduct(delta, delta);
    double t = 0.0;
    if (lengthSquared > 1e-12) {
        t = std::clamp(QPointF::dotProduct(point - segment.p1(), delta) / lengthSquared, 0.0, 1.0);
    }
    const QPointF projection = segment.p1() + delta * t;
    if (nearest != nullptr) {
        *nearest = projection;
    }
    return QLineF(point, projection).length();
}

QString nextDesignator(const SketchDocument& document, const QString& prefix) {
    if (prefix.isEmpty()) {
        return {};
    }
    const QRegularExpression pattern(QStringLiteral("^%1(\\d+)$")
                                         .arg(QRegularExpression::escape(prefix)));
    int highest = 0;
    for (const auto& item : document) {
        const auto match = pattern.match(item.label);
        if (item.kind == SketchItem::Kind::Symbol && match.hasMatch()) {
            highest = std::max(highest, match.captured(1).toInt());
        }
    }
    return prefix + QString::number(highest + 1);
}

void translateItem(SketchItem& item, QPointF delta) {
    for (QPointF& point : item.points) {
        point += delta;
    }
}

void rotateItemQuarterTurn(SketchItem& item, QPointF pivot) {
    for (QPointF& point : item.points) {
        point = pivot + rotateQuarter(point - pivot);
    }
    if (item.kind == SketchItem::Kind::Symbol || item.kind == SketchItem::Kind::Pad) {
        item.quarterTurns = (item.quarterTurns + 1) % 4;
    }
}

void mirrorItem(SketchItem& item, QPointF pivot, bool flipX) {
    for (QPointF& point : item.points) {
        if (flipX) point.setX(2 * pivot.x() - point.x());
        else point.setY(2 * pivot.y() - point.y());
    }
    if (item.kind == SketchItem::Kind::Symbol) {
        if (flipX) item.mirroredX = !item.mirroredX;
        else item.mirroredY = !item.mirroredY;
    }
}

namespace {

constexpr double Epsilon = 1e-6;

struct PointMove {
    QPointF from;
    QPointF to;
};

bool coincident(QPointF a, QPointF b) { return QLineF(a, b).length() < Epsilon; }
bool isHorizontal(QPointF a, QPointF b) {
    return std::abs(a.y() - b.y()) < Epsilon && std::abs(a.x() - b.x()) >= Epsilon;
}
bool isVertical(QPointF a, QPointF b) {
    return std::abs(a.x() - b.x()) < Epsilon && std::abs(a.y() - b.y()) >= Epsilon;
}
bool isWire(const SketchItem& item) {
    return item.kind == SketchItem::Kind::Wire && item.points.size() >= 2;
}

// Items whose anchors are connection points: symbol pins, footprint pads, pads and vias.
bool isConnector(const SketchItem& item) {
    return item.kind == SketchItem::Kind::Symbol || item.kind == SketchItem::Kind::Pad ||
           item.kind == SketchItem::Kind::Via;
}

bool onPin(const SketchDocument& document, QPointF point) {
    for (const auto& item : document) {
        if (!isConnector(item)) continue;
        for (const QPointF& anchor : itemAnchors(item)) {
            if (coincident(anchor, point)) return true;
        }
    }
    return false;
}

// True when a pin or another wire touches `point`. With `ignoreWireEnds`, another wire that only
// ends there does not count, because it follows the point instead of holding it in place.
bool attachedElsewhere(const SketchDocument& document, int wire, QPointF point,
                       bool ignoreWireEnds) {
    if (onPin(document, point)) return true;
    for (int i = 0; i < document.size(); ++i) {
        const auto& item = document[i];
        if (i == wire || !isWire(item)) continue;
        const bool atEnd = coincident(item.points.first(), point) || coincident(item.points.last(), point);
        if (ignoreWireEnds && atEnd) continue;
        for (qsizetype p = 1; p < item.points.size(); ++p) {
            if (distanceToSegment(point, QLineF(item.points[p - 1], item.points[p])) < Epsilon) {
                return true;
            }
        }
    }
    return false;
}

double bendCoordinate(double from, double to, double grid) {
    double middle = (from + to) / 2.0;
    if (grid > 0.0) middle = std::round(middle / grid) * grid;
    return std::clamp(middle, std::min(from, to), std::max(from, to));
}

// Moves the first (or last) vertex of a wire to `target` while keeping an axis-aligned end run
// axis-aligned.
void stretchWireEnd(QVector<QPointF>& points, bool atStart, QPointF target,
                    const std::function<bool(QPointF)>& pinned, double grid) {
    if (!atStart) std::reverse(points.begin(), points.end());
    const QPointF start = points[0];
    const QPointF next = points[1];
    const QPointF delta = target - start;
    const bool freeCorner = points.size() >= 3 && !pinned(next);
    points[0] = target;
    if (isHorizontal(start, next) && std::abs(delta.y()) >= Epsilon) {
        if (freeCorner && isVertical(next, points[2])) {
            points[1].ry() += delta.y();
        } else {
            const double x = bendCoordinate(target.x(), next.x(), grid);
            points.insert(1, QPointF(x, next.y()));
            points.insert(1, QPointF(x, target.y()));
        }
    } else if (isVertical(start, next) && std::abs(delta.x()) >= Epsilon) {
        if (freeCorner && isHorizontal(next, points[2])) {
            points[1].rx() += delta.x();
        } else {
            const double y = bendCoordinate(target.y(), next.y(), grid);
            points.insert(1, QPointF(next.x(), y));
            points.insert(1, QPointF(target.x(), y));
        }
    }
    if (!atStart) std::reverse(points.begin(), points.end());
}

// Makes unfixed wires follow moved points. Interior vertices on a moved point move with it; wire
// ends stretch. A wire whose both ends move by the same offset is translated as a whole.
void followMovedPoints(SketchDocument& result, const SketchDocument& original,
                       const QSet<int>& fixed, const QVector<PointMove>& moves,
                       QSet<int>& modified, double grid) {
    if (moves.isEmpty()) return;
    auto moveAt = [&moves](QPointF point) -> const PointMove* {
        for (const auto& move : moves) {
            if (coincident(move.from, point)) return &move;
        }
        return nullptr;
    };
    for (int i = 0; i < original.size(); ++i) {
        if (fixed.contains(i) || !isWire(original[i])) continue;
        const QVector<QPointF>& before = original[i].points;
        QVector<QPointF>& points = result[i].points;
        const PointMove* head = moveAt(before.first());
        const PointMove* tail = moveAt(before.last());
        bool changed = false;
        for (qsizetype p = 1; p + 1 < before.size(); ++p) {
            if (const PointMove* move = moveAt(before[p])) {
                points[p] = move->to;
                changed = true;
            }
        }
        if (head != nullptr && tail != nullptr && !changed &&
            coincident(head->to - head->from, tail->to - tail->from)) {
            translateItem(result[i], head->to - head->from);
            modified.insert(i);
            continue;
        }
        auto pinned = [&original, i](QPointF point) { return attachedElsewhere(original, i, point, false); };
        if (head != nullptr) {
            stretchWireEnd(points, true, head->to, pinned, grid);
            changed = true;
        }
        if (tail != nullptr) {
            stretchWireEnd(points, false, tail->to, pinned, grid);
            changed = true;
        }
        if (changed) modified.insert(i);
    }
}

void removeDuplicateVertices(QVector<QPointF>& points) {
    for (qsizetype p = 1; p < points.size() && points.size() > 2;) {
        if (coincident(points[p - 1], points[p])) {
            points.removeAt(p == points.size() - 1 ? p - 1 : p);
        } else {
            ++p;
        }
    }
}

// Removes straight-through corners for which `keep` returns false.
void removeStraightVertices(QVector<QPointF>& points, const std::function<bool(QPointF)>& keep) {
    for (qsizetype p = 1; p + 1 < points.size();) {
        const QPointF in = points[p] - points[p - 1];
        const QPointF out = points[p + 1] - points[p];
        const double cross = in.x() * out.y() - in.y() * out.x();
        const double scale =
            std::max(1.0, QLineF(QPointF(), in).length() + QLineF(QPointF(), out).length());
        const bool straight = std::abs(cross) < Epsilon * scale && QPointF::dotProduct(in, out) > 0;
        if (straight && !keep(points[p])) {
            points.removeAt(p);
        } else {
            ++p;
        }
    }
}

// Removes zero-length segments and straight-through corners that nothing else connects to.
void simplifyWires(SketchDocument& document, const QSet<int>& wires) {
    for (int index : wires) {
        QVector<QPointF>& points = document[index].points;
        removeDuplicateVertices(points);
        removeStraightVertices(points, [&document, index](QPointF point) {
            return attachedElsewhere(document, index, point, false);
        });
    }
}

bool validWire(const SketchDocument& document, int wire) {
    return wire >= 0 && wire < document.size() && isWire(document[wire]);
}

bool onPath(const QVector<QPointF>& points, QPointF point) {
    for (qsizetype p = 1; p < points.size(); ++p) {
        if (distanceToSegment(point, QLineF(points[p - 1], points[p])) < Epsilon) return true;
    }
    return false;
}

// Ends of other, unfixed wires that form a T join on `wire` between its vertices. Vertex joins are
// already followed through the vertex moves; ends held by a pin stay where they are.
QVector<QPointF> teeEnds(const SketchDocument& document, int wire, const QSet<int>& fixed) {
    QVector<QPointF> ends;
    const QVector<QPointF>& path = document[wire].points;
    for (int i = 0; i < document.size(); ++i) {
        if (i == wire || fixed.contains(i) || !isWire(document[i])) continue;
        for (QPointF end : {document[i].points.first(), document[i].points.last()}) {
            const bool atVertex = std::any_of(path.begin(), path.end(),
                                              [end](QPointF v) { return coincident(v, end); });
            if (atVertex || !onPath(path, end) || onPin(document, end)) continue;
            if (std::none_of(ends.begin(), ends.end(), [end](QPointF e) { return coincident(e, end); }))
                ends.append(end);
        }
    }
    return ends;
}

QPointF nearestOnPath(const QVector<QPointF>& points, QPointF point) {
    QPointF best = point;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (qsizetype p = 1; p < points.size(); ++p) {
        QPointF nearest;
        const double distance = distanceToSegment(point, QLineF(points[p - 1], points[p]), &nearest);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = nearest;
        }
    }
    return best;
}

// T joins on a reshaped wire: an end still on the new path stays, otherwise it moves to the
// nearest point of the new path.
void followTeeJoins(const SketchDocument& original, const SketchDocument& result, int wire,
                    QVector<PointMove>& moves) {
    for (QPointF end : teeEnds(original, wire, {})) {
        const bool moved = std::any_of(moves.begin(), moves.end(),
                                       [end](const PointMove& m) { return coincident(m.from, end); });
        if (moved || onPath(result[wire].points, end)) continue;
        moves.append({end, nearestOnPath(result[wire].points, end)});
    }
}

} // namespace

SketchDocument moveItemsKeepingConnections(const SketchDocument& document, const QList<int>& items,
                                           QPointF delta, double grid) {
    SketchDocument result = document;
    if (QLineF(QPointF(), delta).length() < Epsilon) return result;
    QSet<int> moving;
    QVector<PointMove> moves;
    for (int index : items) {
        if (index < 0 || index >= document.size() || moving.contains(index)) continue;
        moving.insert(index);
        translateItem(result[index], delta);
        const SketchItem& item = document[index];
        if (isConnector(item) || item.kind == SketchItem::Kind::Wire) {
            for (const QPointF& anchor : itemAnchors(item)) moves.append({anchor, anchor + delta});
        }
    }
    for (int index : moving) {
        if (!isWire(document[index])) continue;
        for (QPointF end : teeEnds(document, index, moving)) moves.append({end, end + delta});
    }
    QSet<int> modified;
    followMovedPoints(result, document, moving, moves, modified, grid);
    simplifyWires(result, modified);
    return result;
}

SketchDocument dragWireSegment(const SketchDocument& document, int wire, int segment, QPointF delta,
                               double grid) {
    SketchDocument result = document;
    if (!validWire(document, wire) || segment < 0 || segment + 1 >= document[wire].points.size()) {
        return result;
    }
    const QVector<QPointF>& before = document[wire].points;
    const QPointF a = before[segment];
    const QPointF b = before[segment + 1];
    const bool axisAligned = isHorizontal(a, b) || isVertical(a, b);
    QPointF offset = delta;
    if (isHorizontal(a, b)) offset.setX(0.0);
    if (isVertical(a, b)) offset.setY(0.0);
    if (QLineF(QPointF(), offset).length() < Epsilon) return result;

    // A segment end slides with the segment unless it is held by a pin or another wire, or moving
    // it would bend the neighbouring axis-aligned segment.
    auto slides = [&](qsizetype vertex, qsizetype neighbour) {
        if (attachedElsewhere(document, wire, before[vertex], true)) return false;
        if (vertex == 0 || vertex == before.size() - 1 || !axisAligned) return true;
        const QPointF run = before[neighbour] - before[vertex];
        return std::abs(run.x() * offset.y() - run.y() * offset.x()) < Epsilon;
    };

    QVector<QPointF> points(before.begin(), before.begin() + segment);
    QVector<PointMove> moves;
    if (slides(segment, segment - 1)) {
        moves.append({a, a + offset});
    } else {
        points.append(a);
    }
    points.append(a + offset);
    points.append(b + offset);
    if (slides(segment + 1, segment + 2)) {
        moves.append({b, b + offset});
    } else {
        points.append(b);
    }
    points.append(QVector<QPointF>(before.begin() + segment + 2, before.end()));
    result[wire].points = points;
    // T joins on the dragged segment move with it; others stay on (or snap back onto) the wire.
    for (QPointF end : teeEnds(document, wire, {})) {
        if (distanceToSegment(end, QLineF(a, b)) < Epsilon) moves.append({end, end + offset});
    }
    followTeeJoins(document, result, wire, moves);

    QSet<int> modified{wire};
    followMovedPoints(result, document, {wire}, moves, modified, grid);
    simplifyWires(result, modified);
    return result;
}

SketchDocument dragWireVertex(const SketchDocument& document, int wire, int vertex, QPointF delta,
                              double grid) {
    SketchDocument result = document;
    if (!validWire(document, wire) || vertex < 0 || vertex >= document[wire].points.size() ||
        QLineF(QPointF(), delta).length() < Epsilon) {
        return result;
    }
    const QPointF from = document[wire].points[vertex];
    result[wire].points[vertex] = from + delta;
    QVector<PointMove> moves;
    if (!onPin(document, from)) moves.append({from, from + delta});
    followTeeJoins(document, result, wire, moves);
    QSet<int> modified{wire};
    followMovedPoints(result, document, {wire}, moves, modified, grid);
    simplifyWires(result, modified);
    return result;
}

QVector<QPointF> orthogonalRoute(QPointF from, QPointF to, QPointF leaving, QPointF entering,
                                 double grid) {
    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    if (std::abs(dx) < Epsilon || std::abs(dy) < Epsilon) {
        return {};
    }
    enum class Leg { None, Horizontal, Vertical };
    // A preferred direction pointing away from the other end flips to the other axis, so the
    // route turns immediately instead of running back through the symbol.
    auto firstLeg = [](QPointF direction, QPointF along) {
        if (std::abs(direction.x()) >= Epsilon) {
            return direction.x() * along.x() > 0 ? Leg::Horizontal : Leg::Vertical;
        }
        if (std::abs(direction.y()) >= Epsilon) {
            return direction.y() * along.y() > 0 ? Leg::Vertical : Leg::Horizontal;
        }
        return Leg::None;
    };
    const Leg start = firstLeg(leaving, QPointF(dx, dy));
    const Leg end = firstLeg(entering, QPointF(-dx, -dy));
    const QPointF horizontalFirst(to.x(), from.y());
    const QPointF verticalFirst(from.x(), to.y());
    if (start == Leg::Horizontal && end == Leg::Horizontal) {
        const double x = bendCoordinate(from.x(), to.x(), grid);
        return {QPointF(x, from.y()), QPointF(x, to.y())};
    }
    if (start == Leg::Vertical && end == Leg::Vertical) {
        const double y = bendCoordinate(from.y(), to.y(), grid);
        return {QPointF(from.x(), y), QPointF(to.x(), y)};
    }
    if (start == Leg::Horizontal) return {horizontalFirst};
    if (start == Leg::Vertical) return {verticalFirst};
    if (end == Leg::Horizontal) return {verticalFirst};
    if (end == Leg::Vertical) return {horizontalFirst};
    return {std::abs(dx) >= std::abs(dy) ? horizontalFirst : verticalFirst};
}

QPointF pinDirectionAt(const SketchDocument& document, QPointF point) {
    for (const auto& item : document) {
        if (item.kind != SketchItem::Kind::Symbol) continue;
        const auto* symbol = findSymbol(item.variant);
        if (symbol == nullptr) continue;
        for (const QPointF& pin : symbol->pins) {
            if (!coincident(symbolToWorld(item, pin), point)) continue;
            const QPointF outward = point - itemBounds(item).center();
            if (std::abs(outward.x()) < Epsilon && std::abs(outward.y()) < Epsilon) return {};
            return std::abs(outward.x()) >= std::abs(outward.y())
                       ? QPointF(std::copysign(1.0, outward.x()), 0.0)
                       : QPointF(0.0, std::copysign(1.0, outward.y()));
        }
    }
    return {};
}

void simplifyPath(QVector<QPointF>& points) {
    removeDuplicateVertices(points);
    removeStraightVertices(points, [](QPointF) { return false; });
}

QVector<QVector<QPointF>> splitPathAtWires(const SketchDocument& document,
                                           const QVector<QPointF>& path) {
    auto onExistingWire = [&](QPointF point) {
        for (const auto& item : document) {
            if (item.kind != SketchItem::Kind::Wire) continue;
            for (const QLineF& segment : itemSegments(item))
                if (distanceToSegment(point, segment) < Epsilon) return true;
        }
        return false;
    };
    QVector<QVector<QPointF>> pieces;
    QVector<QPointF> piece;
    for (qsizetype i = 0; i < path.size(); ++i) {
        if (!piece.isEmpty() && coincident(piece.last(), path[i])) continue;
        piece.append(path[i]);
        if (i > 0 && i + 1 < path.size() && piece.size() >= 2 && onExistingWire(path[i])) {
            pieces.append(piece);
            piece = {path[i]};
        }
    }
    if (piece.size() >= 2) pieces.append(piece);
    return pieces;
}

} // namespace hatt::ui
