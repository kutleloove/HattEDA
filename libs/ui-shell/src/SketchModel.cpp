#include "hatt/ui/SketchModel.hpp"

#include <QCoreApplication>
#include <QPolygonF>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <initializer_list>

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

SymbolShape pad(QPointF center, double w, double h) {
    SymbolShape shape = rectangle(center.x() - w / 2, center.y() - h / 2, w, h);
    shape.copper = true;
    return shape;
}

SymbolShape roundPad(QPointF center, double diameter) {
    SymbolShape shape = circle(center, diameter / 2);
    shape.copper = true;
    return shape;
}

SymbolShape hole(QPointF center, double diameter) {
    SymbolShape shape = circle(center, diameter / 2);
    shape.hole = true;
    return shape;
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
    library.append(resistor);

    auto capacitor = define("schematic.capacitor", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Capacitor"),
                            W::Schematic, C::Component, "C");
    capacitor.shapes = {polyline({{-5.08, 0}, {-0.635, 0}}), polyline({{-0.635, -2.032}, {-0.635, 2.032}}),
                        polyline({{0.635, -2.032}, {0.635, 2.032}}), polyline({{0.635, 0}, {5.08, 0}})};
    capacitor.pins = {{-5.08, 0}, {5.08, 0}};
    library.append(capacitor);

    auto inductor = define("schematic.inductor", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Inductor"),
                           W::Schematic, C::Component, "L");
    inductor.shapes = {polyline({{-5.08, 0}, {-2.54, 0}}), polyline({{2.54, 0}, {5.08, 0}})};
    for (double x : {-1.905, -0.635, 0.635, 1.905}) {
        inductor.shapes.append(arc({x, 0}, 0.635, 180.0, -180.0, 8));
    }
    inductor.pins = {{-5.08, 0}, {5.08, 0}};
    library.append(inductor);

    auto diode = define("schematic.diode", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Diode"),
                        W::Schematic, C::Component, "D");
    diode.shapes = {polyline({{-5.08, 0}, {-1.27, 0}}), polyline({{1.27, 0}, {5.08, 0}}),
                    polygon({{-1.27, -1.524}, {-1.27, 1.524}, {1.27, 0}}),
                    polyline({{1.27, -1.524}, {1.27, 1.524}})};
    diode.pins = {{-5.08, 0}, {5.08, 0}};
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
    library.append(npn);

    auto opamp = define("schematic.opamp", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Operational amplifier"),
                        W::Schematic, C::Component, "U");
    opamp.shapes = {polygon({{-3.81, -5.08}, {-3.81, 5.08}, {5.08, 0}}),
                    polyline({{-7.62, -2.54}, {-3.81, -2.54}}), polyline({{-7.62, 2.54}, {-3.81, 2.54}}),
                    polyline({{5.08, 0}, {7.62, 0}}), polyline({{-3.2, -2.54}, {-2.2, -2.54}}),
                    polyline({{-3.2, 2.54}, {-2.2, 2.54}}), polyline({{-2.7, 2.04}, {-2.7, 3.04}})};
    opamp.pins = {{-7.62, -2.54}, {-7.62, 2.54}, {7.62, 0}};
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

    auto r0603 = define("board.r0603", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Resistor 0603"),
                        W::Board, C::Component, "R");
    r0603.shapes = {rectangle(-1.45, -0.8, 2.9, 1.6), pad({-0.8, 0}, 0.8, 0.95), pad({0.8, 0}, 0.8, 0.95)};
    r0603.pins = {{-0.8, 0}, {0.8, 0}};
    library.append(r0603);

    auto c0805 = define("board.c0805", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Capacitor 0805"),
                        W::Board, C::Component, "C");
    c0805.shapes = {rectangle(-1.8, -1.0, 3.6, 2.0), pad({-0.95, 0}, 1.0, 1.3), pad({0.95, 0}, 1.0, 1.3)};
    c0805.pins = {{-0.95, 0}, {0.95, 0}};
    library.append(c0805);

    auto sot23 = define("board.sot23", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOT-23"),
                        W::Board, C::Component, "Q");
    sot23.shapes = {rectangle(-1.6, -0.7, 3.2, 1.4), pad({-0.95, 1.1}, 0.8, 0.9),
                    pad({0.95, 1.1}, 0.8, 0.9), pad({0, -1.1}, 0.8, 0.9)};
    sot23.pins = {{-0.95, 1.1}, {0.95, 1.1}, {0, -1.1}};
    library.append(sot23);

    auto soic8 = define("board.soic8", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOIC-8"),
                        W::Board, C::Component, "U");
    soic8.shapes = {rectangle(-1.95, -2.5, 3.9, 5.0), circle({-1.4, -1.95}, 0.2, true)};
    for (double y : {-1.905, -0.635, 0.635, 1.905}) {
        soic8.shapes.append(pad({-2.7, y}, 1.55, 0.6));
        soic8.pins.append({-2.7, y});
    }
    for (double y : {1.905, 0.635, -0.635, -1.905}) {
        soic8.shapes.append(pad({2.7, y}, 1.55, 0.6));
        soic8.pins.append({2.7, y});
    }
    library.append(soic8);

    auto dip8 = define("board.dip8", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "DIP-8"),
                       W::Board, C::Component, "U");
    dip8.shapes = {rectangle(-2.6, -5.1, 5.2, 10.2)};
    for (double y : {-3.81, -1.27, 1.27, 3.81}) {
        dip8.shapes.append(roundPad({-3.81, y}, 1.6));
        dip8.shapes.append(hole({-3.81, y}, 0.8));
        dip8.pins.append({-3.81, y});
    }
    for (double y : {3.81, 1.27, -1.27, -3.81}) {
        dip8.shapes.append(roundPad({3.81, y}, 1.6));
        dip8.shapes.append(hole({3.81, y}, 0.8));
        dip8.pins.append({3.81, y});
    }
    library.append(dip8);

    auto header = define("board.header-1x4", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x4"),
                         W::Board, C::Component, "J");
    header.shapes = {rectangle(-1.27, -5.08, 2.54, 10.16)};
    for (double y : {-3.81, -1.27, 1.27, 3.81}) {
        header.shapes.append(roundPad({0, y}, 1.7));
        header.shapes.append(hole({0, y}, 1.0));
        header.pins.append({0, y});
    }
    library.append(header);

    auto via = define("board.via", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Via"),
                      W::Board, C::Terminal, "");
    via.shapes = {roundPad({0, 0}, 0.8), hole({0, 0}, 0.4)};
    via.pins = {{0, 0}};
    library.append(via);

    auto testPoint = define("board.test-point", QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Test point"),
                            W::Board, C::Terminal, "TP");
    testPoint.shapes = {roundPad({0, 0}, 1.5)};
    testPoint.pins = {{0, 0}};
    library.append(testPoint);

    auto mountingHole = define("board.mounting-hole",
                               QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Mounting hole"),
                               W::Board, C::Terminal, "H");
    mountingHole.shapes = {roundPad({0, 0}, 6.0), hole({0, 0}, 3.2)};
    mountingHole.pins = {{0, 0}};
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

double textWidth(const QString& text) {
    return std::max<qsizetype>(1, text.size()) * TextHeightMm * 0.55;
}

} // namespace

const QVector<SymbolDefinition>& symbolLibrary() {
    static const QVector<SymbolDefinition> library = buildLibrary();
    return library;
}

const SymbolDefinition* findSymbol(const QString& id) {
    for (const auto& symbol : symbolLibrary()) {
        if (symbol.id == id) {
            return &symbol;
        }
    }
    return nullptr;
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
    return QCoreApplication::translate("hatt::ui::SymbolLibrary", symbol.name);
}

QPointF symbolToWorld(const SketchItem& item, QPointF local) {
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
        const QRectF rect(item.points.first(), QSizeF(textWidth(item.label), TextHeightMm));
        appendPolyline(segments, {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()},
                       true);
        break;
    }
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
    if (item.kind == SketchItem::Kind::Symbol) {
        item.quarterTurns = (item.quarterTurns + 1) % 4;
    }
}

} // namespace hatt::ui
