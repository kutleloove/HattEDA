#include "hatt/ui/SketchModel.hpp"

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

SymbolShape polyline(std::initializer_list<QPointF> points) {
    SymbolShape shape;
    shape.points = points;
    return shape;
}

SymbolShape polygon(std::initializer_list<QPointF> points, bool filled = false) {
    SymbolShape shape;
    shape.points = points;
    shape.closed = true;
    shape.filled = filled;
    return shape;
}

SymbolShape rectangle(double x, double y, double w, double h) {
    return polygon({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}});
}

SymbolShape arc(QPointF center, double radius, double startDegrees, double spanDegrees,
                int segments = 12) {
    SymbolShape shape;
    for (int i = 0; i <= segments; ++i) {
        const double angle = (startDegrees + spanDegrees * i / segments) * Pi / 180.0;
        shape.points.append(center + QPointF(radius * std::cos(angle), -radius * std::sin(angle)));
    }
    return shape;
}

SymbolShape circle(QPointF center, double radius, bool filled = false) {
    SymbolShape shape = arc(center, radius, 0.0, 360.0, 28);
    shape.points.removeLast();
    shape.closed = true;
    shape.filled = filled;
    return shape;
}

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

SymbolDefinition define(const char* id, const char* name, Workspace workspace,
                        SymbolCategory category, const char* prefix, const char* label = "") {
    SymbolDefinition symbol;
    symbol.id = QString::fromLatin1(id);
    symbol.name = name;
    symbol.workspace = workspace;
    symbol.category = category;
    symbol.prefix = QString::fromLatin1(prefix);
    symbol.defaultLabel = QString::fromLatin1(label);
    return symbol;
}

QVector<SymbolDefinition> buildLibrary() {
    using W = Workspace;
    using C = SymbolCategory;
    QVector<SymbolDefinition> library;

    auto resistor = define("schematic.resistor", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Resistor"),
                           W::Schematic, C::Component, "R");
    resistor.shapes = {rectangle(-2.54, -1.016, 5.08, 2.032), polyline({{-5.08, 0}, {-2.54, 0}}),
                       polyline({{2.54, 0}, {5.08, 0}})};
    resistor.pins = {{-5.08, 0}, {5.08, 0}};
    resistor.defaultValue = QStringLiteral("1k");
    resistor.defaultFootprint = QStringLiteral("board.r0603");
    library.append(resistor);

    auto voltage = define("schematic.vdc", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "DC voltage source"),
                          W::Schematic, C::Component, "V");
    voltage.shapes = {circle({0, 0}, 2.54), polyline({{0, -5.08}, {0, -2.54}}),
                      polyline({{0, 2.54}, {0, 5.08}}), polyline({{-0.8, -1}, {0.8, -1}}),
                      polyline({{0, -1.8}, {0, -0.2}}), polyline({{-0.8, 1}, {0.8, 1}})};
    voltage.pins = {{0, -5.08}, {0, 5.08}};
    voltage.defaultValue = QStringLiteral("5");
    voltage.defaultFootprint = QStringLiteral("board.header-1x2");
    library.append(voltage);

    auto capacitor = define("schematic.capacitor", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Capacitor"),
                            W::Schematic, C::Component, "C");
    capacitor.shapes = {polyline({{-5.08, 0}, {-0.635, 0}}), polyline({{-0.635, -2.032}, {-0.635, 2.032}}),
                        polyline({{0.635, -2.032}, {0.635, 2.032}}), polyline({{0.635, 0}, {5.08, 0}})};
    capacitor.pins = {{-5.08, 0}, {5.08, 0}};
    capacitor.defaultValue = QStringLiteral("100n");
    capacitor.defaultFootprint = QStringLiteral("board.c0805");
    library.append(capacitor);

    auto inductor = define("schematic.inductor", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Inductor"),
                           W::Schematic, C::Component, "L");
    inductor.shapes = {polyline({{-5.08, 0}, {-2.54, 0}}), polyline({{2.54, 0}, {5.08, 0}})};
    for (double x : {-1.905, -0.635, 0.635, 1.905}) {
        inductor.shapes.append(arc({x, 0}, 0.635, 180.0, -180.0, 8));
    }
    inductor.pins = {{-5.08, 0}, {5.08, 0}};
    inductor.defaultValue = QStringLiteral("10u");
    inductor.defaultFootprint = QStringLiteral("board.c0805");
    library.append(inductor);

    auto diode = define("schematic.diode", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Diode"),
                        W::Schematic, C::Component, "D");
    diode.shapes = {polyline({{-5.08, 0}, {-1.27, 0}}), polyline({{1.27, 0}, {5.08, 0}}),
                    polygon({{-1.27, -1.524}, {-1.27, 1.524}, {1.27, 0}}),
                    polyline({{1.27, -1.524}, {1.27, 1.524}})};
    diode.pins = {{-5.08, 0}, {5.08, 0}};
    diode.defaultFootprint = QStringLiteral("board.c0805");
    library.append(diode);

    auto led = diode;
    led.id = QStringLiteral("schematic.led");
    led.name = QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "LED");
    for (double dx : {0.0, 1.3}) {
        led.shapes.append(polyline({{-0.2 + dx, -2.0}, {1.0 + dx, -3.2}}));
        led.shapes.append(polyline({{0.35 + dx, -3.15}, {1.0 + dx, -3.2}, {0.95 + dx, -2.55}}));
    }
    library.append(led);

    auto npn = define("schematic.npn", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "NPN transistor"),
                      W::Schematic, C::Component, "Q");
    npn.shapes = {circle({0.9, 0}, 3.3), polyline({{-5.08, 0}, {-0.635, 0}}),
                  polyline({{-0.635, -1.905}, {-0.635, 1.905}}),
                  polyline({{-0.635, -0.8}, {2.54, -2.8}, {2.54, -5.08}}),
                  polyline({{-0.635, 0.8}, {2.54, 2.8}, {2.54, 5.08}}),
                  polygon({{2.54, 2.8}, {1.35, 2.75}, {1.95, 1.8}}, true)};
    npn.pins = {{-5.08, 0}, {2.54, -5.08}, {2.54, 5.08}};
    npn.defaultFootprint = QStringLiteral("board.sot23");
    library.append(npn);

    auto opamp = define("schematic.opamp", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Operational amplifier"),
                        W::Schematic, C::Component, "U");
    opamp.shapes = {polygon({{-3.81, -5.08}, {-3.81, 5.08}, {5.08, 0}}),
                    polyline({{-7.62, -2.54}, {-3.81, -2.54}}), polyline({{-7.62, 2.54}, {-3.81, 2.54}}),
                    polyline({{5.08, 0}, {7.62, 0}}), polyline({{-3.2, -2.54}, {-2.2, -2.54}}),
                    polyline({{-3.2, 2.54}, {-2.2, 2.54}}), polyline({{-2.7, 2.04}, {-2.7, 3.04}})};
    opamp.pins = {{-7.62, -2.54}, {-7.62, 2.54}, {7.62, 0}};
    opamp.defaultFootprint = QStringLiteral("board.sot23");
    library.append(opamp);

    auto ic = define("schematic.ic8", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Integrated circuit (8 pins)"),
                     W::Schematic, C::Component, "U");
    ic.shapes = {rectangle(-5.08, -7.62, 10.16, 12.7), circle({-3.81, -6.35}, 0.45)};
    for (double y : {-5.08, -2.54, 0.0, 2.54}) {
        ic.shapes.append(polyline({{-7.62, y}, {-5.08, y}}));
        ic.shapes.append(polyline({{5.08, y}, {7.62, y}}));
        ic.pins.append({-7.62, y});
    }
    for (double y : {2.54, 0.0, -2.54, -5.08}) {
        ic.pins.append({7.62, y});
    }
    ic.defaultFootprint = QStringLiteral("board.soic8");
    library.append(ic);

    auto input = define("schematic.input", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Input port"),
                        W::Schematic, C::Terminal, "", "IN");
    input.shapes = {polygon({{-6.35, -1.016}, {-2.286, -1.016}, {-1.27, 0}, {-2.286, 1.016}, {-6.35, 1.016}}),
                    polyline({{-1.27, 0}, {0, 0}})};
    input.pins = {{0, 0}};
    library.append(input);

    auto output = define("schematic.output", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Output port"),
                         W::Schematic, C::Terminal, "", "OUT");
    output.shapes = {polygon({{-1.27, -1.016}, {-5.334, -1.016}, {-6.35, 0}, {-5.334, 1.016}, {-1.27, 1.016}}),
                     polyline({{-1.27, 0}, {0, 0}})};
    output.pins = {{0, 0}};
    library.append(output);

    auto bidirectional = define("schematic.bidirectional",
                                QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Bidirectional port"),
                                W::Schematic, C::Terminal, "", "IO");
    bidirectional.shapes = {polygon({{-1.27, 0}, {-2.286, -1.016}, {-5.334, -1.016}, {-6.35, 0},
                                     {-5.334, 1.016}, {-2.286, 1.016}}),
                            polyline({{-1.27, 0}, {0, 0}})};
    bidirectional.pins = {{0, 0}};
    library.append(bidirectional);

    auto power = define("schematic.power", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Power rail"),
                        W::Schematic, C::Terminal, "", "VCC");
    power.shapes = {polyline({{0, 0}, {0, -2.54}}), polyline({{-1.524, -2.54}, {1.524, -2.54}})};
    power.pins = {{0, 0}};
    library.append(power);

    auto ground = define("schematic.ground", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Ground"),
                         W::Schematic, C::Terminal, "", "");
    ground.shapes = {polyline({{0, 0}, {0, 2.54}}), polyline({{-1.905, 2.54}, {1.905, 2.54}}),
                     polyline({{-1.143, 3.302}, {1.143, 3.302}}), polyline({{-0.381, 4.064}, {0.381, 4.064}})};
    ground.pins = {{0, 0}};
    library.append(ground);

    auto junction = define("schematic.junction", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Junction"),
                           W::Schematic, C::Terminal, "", "");
    junction.shapes = {circle({0, 0}, 0.5, true)};
    junction.pins = {{0, 0}};
    library.append(junction);

    auto voltageProbe = define("schematic.voltage-probe",
                               QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Voltage probe"),
                               W::Schematic, C::Probe, "VP");
    voltageProbe.shapes = {polyline({{0, 0}, {1.6, -1.6}}), circle({2.6, -2.6}, 1.4),
                           polyline({{2.0, -3.3}, {2.6, -1.9}, {3.2, -3.3}})};
    voltageProbe.pins = {{0, 0}};
    library.append(voltageProbe);

    auto currentProbe = define("schematic.current-probe",
                               QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Current probe"),
                               W::Schematic, C::Probe, "IP");
    currentProbe.shapes = {circle({0, 0}, 1.6), polyline({{-1.0, 0}, {1.0, 0}}),
                           polyline({{0.35, -0.55}, {1.0, 0}, {0.35, 0.55}})};
    currentProbe.pins = {{0, 0}};
    library.append(currentProbe);

    // --- Board footprints ---
    // `shapes` is the silkscreen outline; copper is drawn from `pads`, one pad per pin.

    auto header2 = define("board.header-1x2", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x2"),
                           W::Board, C::Component, "J");
    header2.shapes = {rectangle(-1.27, -1.27, 5.08, 2.54)};
    header2.pins = {{0, 0}, {2.54, 0}};
    header2.pads = {makeSquareThroughPad(1, 1.6, 0.8), makeThroughPad(2, 1.6, 0.8)};
    library.append(header2);

    auto r0603 = define("board.r0603", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Resistor 0603"),
                        W::Board, C::Component, "R");
    r0603.shapes = {rectangle(-1.45, -0.8, 2.9, 1.6)};
    r0603.pins = {{-0.8, 0}, {0.8, 0}};
    r0603.pads = {makePad(1, 0.8, 0.95), makePad(2, 0.8, 0.95)};
    library.append(r0603);

    auto c0805 = define("board.c0805", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Capacitor 0805"),
                        W::Board, C::Component, "C");
    c0805.shapes = {rectangle(-1.8, -1.0, 3.6, 2.0)};
    c0805.pins = {{-0.95, 0}, {0.95, 0}};
    c0805.pads = {makePad(1, 1.0, 1.3), makePad(2, 1.0, 1.3)};
    library.append(c0805);

    auto sot23 = define("board.sot23", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOT-23"),
                        W::Board, C::Component, "Q");
    sot23.shapes = {rectangle(-1.6, -0.7, 3.2, 1.4)};
    sot23.pins = {{-0.95, 1.1}, {0.95, 1.1}, {0, -1.1}};
    sot23.pads = {makePad(1, 0.8, 0.9), makePad(2, 0.8, 0.9), makePad(3, 0.8, 0.9)};
    library.append(sot23);

    auto soic8 = define("board.soic8", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOIC-8"),
                        W::Board, C::Component, "U");
    soic8.shapes = {rectangle(-1.95, -2.5, 3.9, 5.0), circle({-1.4, -1.95}, 0.2, true)};
    int soic8PadNum = 1;
    for (double y : {-1.905, -0.635, 0.635, 1.905}) {
        soic8.pins.append({-2.7, y});
        soic8.pads.append(makePad(soic8PadNum++, 1.55, 0.6));
    }
    for (double y : {1.905, 0.635, -0.635, -1.905}) {
        soic8.pins.append({2.7, y});
        soic8.pads.append(makePad(soic8PadNum++, 1.55, 0.6));
    }
    library.append(soic8);

    auto dip8 = define("board.dip8", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "DIP-8"),
                       W::Board, C::Component, "U");
    dip8.shapes = {rectangle(-2.6, -5.1, 5.2, 10.2)};
    int dip8PadNum = 1;
    for (double y : {-3.81, -1.27, 1.27, 3.81}) {
        dip8.pins.append({-3.81, y});
        dip8.pads.append(dip8PadNum == 1 ? makeSquareThroughPad(dip8PadNum++, 1.6, 0.8)
                                         : makeThroughPad(dip8PadNum++, 1.6, 0.8));
    }
    for (double y : {3.81, 1.27, -1.27, -3.81}) {
        dip8.pins.append({3.81, y});
        dip8.pads.append(makeThroughPad(dip8PadNum++, 1.6, 0.8));
    }
    library.append(dip8);

    auto header = define("board.header-1x4", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x4"),
                         W::Board, C::Component, "J");
    header.shapes = {rectangle(-1.27, -5.08, 2.54, 10.16)};
    int headerPadNum = 1;
    for (double y : {-3.81, -1.27, 1.27, 3.81}) {
        header.pins.append({0, y});
        header.pads.append(headerPadNum == 1 ? makeSquareThroughPad(headerPadNum++, 1.7, 1.0)
                                             : makeThroughPad(headerPadNum++, 1.7, 1.0));
    }
    library.append(header);

    // Kept so older files that placed the via symbol still open; new vias are Kind::Via items.
    auto via = define("board.via", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Via"),
                      W::Board, C::Terminal, "");
    via.pins = {{0, 0}};
    via.pads = {makeThroughPad(1, 0.8, 0.4)};
    library.append(via);

    auto testPoint = define("board.test-point", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Test point"),
                            W::Board, C::Terminal, "TP");
    testPoint.pins = {{0, 0}};
    testPoint.pads = {makeRoundPad(1, 1.5)};
    library.append(testPoint);

    auto mountingHole = define("board.mounting-hole",
                               QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Mounting hole"),
                               W::Board, C::Terminal, "H");
    mountingHole.shapes = {circle({0, 0}, 3.4)};
    mountingHole.pins = {{0, 0}};
    mountingHole.pads = {makeThroughPad(1, 6.0, 3.2)};
    library.append(mountingHole);

    return library;
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
    static const QVector<SymbolDefinition> library = buildLibrary();
    return library;
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
    return found != registry().symbols.end() ? found->second.get() : nullptr;
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
    // Bottom side footprints are seen from the top, so they are mirrored left to right.
    if (item.onBottom) local.setX(-local.x());
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
