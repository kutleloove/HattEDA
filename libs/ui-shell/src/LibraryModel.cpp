#include "hatt/ui/LibraryModel.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>

// Q_INIT_RESOURCE must be called from outside any namespace (it names a global-scope function
// the qrc compiler generated); wrapped here so hatt::ui::loadBuiltInLibrary() can call it.
static void hattEdaInitLibraryResource() { Q_INIT_RESOURCE(builtin_library); }

namespace hatt::ui {
namespace {

// The built-in library's display names live in the JSON resource as plain English source text
// (LibraryDevice::displayNameKey / LibraryFootprint::displayNameKey), translated at load time via
// QCoreApplication::translate("hatt::ui::SymbolLibrary", key). This inventory exists only so
// lupdate can still discover them (it scans QT_TRANSLATE_NOOP calls in source, not JSON), matching
// ComponentCatalog.cpp's CatalogTranslationSources. Keep it in sync with builtin.json.
[[maybe_unused]] const char* BuiltInLibraryTranslationSources[] = {
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Resistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "DC voltage source"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Capacitor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Inductor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Diode"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "LED"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "NPN transistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Operational amplifier"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Integrated circuit (8 pins)"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Input port"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Output port"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Bidirectional port"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Power rail"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Ground"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Junction"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Voltage probe"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Current probe"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x2"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Resistor 0603"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Capacitor 0805"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOT-23"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOIC-8"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "DIP-8"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x4"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Via"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Test point"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Mounting hole"),
    // #61 PR (b): ComponentCatalog's device/footprint names, folded into this same library.
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "1N4007"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "1N4148"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "2N2222 / PN2222A"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "2N7000"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "555 timer"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "5V1 Zener"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "7805"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "AND gate"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Adjustable linear regulator"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Axial DO-35, 10.16 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Axial DO-41, 12.70 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "BAT54"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "BC547"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "BC557"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Battery"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Bridge rectifier"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Bridge rectifier, 4 pin"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Buffer"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Buzzer"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Capacitor, polarized"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Comparator"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Crystal"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "D flip-flop"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "DC current source"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "DC motor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "DIP"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Digital clock"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Fixed linear regulator"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Fuse"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "IRLZ44N"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "LDR / photoresistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "LED 3 mm, 2.54 mm pitch"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "LED 5 mm, 2.54 mm pitch"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "LM317"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "LM358"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "LM393"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "N-channel JFET"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "N-channel MOSFET"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "NAND gate"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "NE555"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "NOR gate"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "NOT gate"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "NTC thermistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "OR gate"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "P-channel MOSFET"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "PNP transistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "PTC thermistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Photodiode"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x1, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x10, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x11, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x12, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x13, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x14, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x15, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x16, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x17, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x18, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x19, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x2, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x20, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x3, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x4, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x5, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x6, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x7, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x8, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 1x9, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 2x1, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 2x10, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 2x2, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 2x3, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 2x4, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 2x5, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 2x6, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 2x7, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 2x8, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pin header 2x9, 2.54 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Potentiometer"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Pulse / clock source"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Push button"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "QFN-16 (perimeter pads)"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "QFN-20 (perimeter pads)"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "QFN-24 (perimeter pads)"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "QFN-32 (perimeter pads)"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "QFN-48 (perimeter pads)"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Radial capacitor, 2 mm pitch"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Radial capacitor, 2.5 mm pitch"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Radial capacitor, 5 mm pitch"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Radial capacitor, 7.5 mm pitch"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Relay"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Relay, generic 5 pin"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SMA / DO-214AC"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SMB / DO-214AA"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SMD passive 0201"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SMD passive 0402"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SMD passive 0603"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SMD passive 0805"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SMD passive 1206"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SMD passive 1210"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOD-123"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOD-323"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOIC-14"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOIC-16"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOIC-20"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOIC-28"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SOT-223"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SPST switch"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SSOP-14"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SSOP-16"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SSOP-20"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SSOP-24"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SSOP-28"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "SSOP-8"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Schottky diode"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Sine voltage source"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TO-220-3 vertical"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TO-252 / DPAK"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TO-92 inline"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TQFP-100"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TQFP-32"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TQFP-44"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TQFP-48"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TQFP-64"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TSSOP-14"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TSSOP-16"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TSSOP-20"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TSSOP-24"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TSSOP-28"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "TSSOP-8"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Terminal block"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Terminal block 2 pin, 5.08 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Terminal block 3 pin, 5.08 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Terminal block 4 pin, 5.08 mm"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Transformer"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Tri-state buffer"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Trimmer potentiometer, 3 pin"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "XOR gate"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Zener diode"),
};

QJsonArray pointToJson(QPointF p) { return {p.x(), p.y()}; }

bool pointFromJson(const QJsonValue& value, QPointF& out) {
    const QJsonArray a = value.toArray();
    if (a.size() != 2) return false;
    out = {a[0].toDouble(), a[1].toDouble()};
    return true;
}

QJsonArray pointsToJson(const QVector<QPointF>& points) {
    QJsonArray a;
    for (QPointF p : points) a.append(pointToJson(p));
    return a;
}

bool pointsFromJson(const QJsonValue& value, QVector<QPointF>& out) {
    if (!value.isArray()) return false;
    for (const QJsonValue& v : value.toArray()) {
        QPointF p;
        if (!pointFromJson(v, p)) return false;
        out.append(p);
    }
    return true;
}

QJsonObject shapeToJson(const SymbolShape& s) {
    QJsonObject o;
    o[QStringLiteral("points")] = pointsToJson(s.points);
    if (s.closed) o[QStringLiteral("closed")] = true;
    if (s.filled) o[QStringLiteral("filled")] = true;
    if (s.copper) o[QStringLiteral("copper")] = true;
    if (s.hole) o[QStringLiteral("hole")] = true;
    return o;
}

bool shapeFromJson(const QJsonValue& value, SymbolShape& out) {
    if (!value.isObject()) return false;
    const QJsonObject o = value.toObject();
    if (!pointsFromJson(o.value(QStringLiteral("points")), out.points)) return false;
    out.closed = o.value(QStringLiteral("closed")).toBool();
    out.filled = o.value(QStringLiteral("filled")).toBool();
    out.copper = o.value(QStringLiteral("copper")).toBool();
    out.hole = o.value(QStringLiteral("hole")).toBool();
    return true;
}

QJsonArray shapesToJson(const QVector<SymbolShape>& shapes) {
    QJsonArray a;
    for (const auto& s : shapes) a.append(shapeToJson(s));
    return a;
}

bool shapesFromJson(const QJsonValue& value, QVector<SymbolShape>& out) {
    if (!value.isArray()) return false;
    for (const QJsonValue& v : value.toArray()) {
        SymbolShape s;
        if (!shapeFromJson(v, s)) return false;
        out.append(s);
    }
    return true;
}

const char* padShapeToken(PadShape shape) {
    switch (shape) {
    case PadShape::Round: return "round";
    case PadShape::Rect: return "rect";
    case PadShape::Oval: return "oval";
    }
    return "rect";
}

bool padShapeFromToken(const QString& token, PadShape& out) {
    if (token == QLatin1String("round")) { out = PadShape::Round; return true; }
    if (token == QLatin1String("rect")) { out = PadShape::Rect; return true; }
    if (token == QLatin1String("oval")) { out = PadShape::Oval; return true; }
    return false;
}

QJsonObject padToJson(const PadDefinition& p) {
    return {{QStringLiteral("number"), p.number},
            {QStringLiteral("shape"), QLatin1String(padShapeToken(p.shape))},
            {QStringLiteral("width"), p.width},
            {QStringLiteral("height"), p.height},
            {QStringLiteral("drill"), p.drillDiameter},
            {QStringLiteral("layers"), p.layers}};
}

bool padFromJson(const QJsonValue& value, PadDefinition& out) {
    if (!value.isObject()) return false;
    const QJsonObject o = value.toObject();
    out.number = o.value(QStringLiteral("number")).toInt(1);
    if (!padShapeFromToken(o.value(QStringLiteral("shape")).toString(), out.shape)) return false;
    out.width = o.value(QStringLiteral("width")).toDouble();
    out.height = o.value(QStringLiteral("height")).toDouble();
    out.drillDiameter = o.value(QStringLiteral("drill")).toDouble();
    out.layers = o.value(QStringLiteral("layers")).toInt();
    return true;
}

const char* categoryToken(SymbolCategory c) {
    switch (c) {
    case SymbolCategory::Component: return "component";
    case SymbolCategory::Terminal: return "terminal";
    case SymbolCategory::Probe: return "probe";
    }
    return "component";
}

bool categoryFromToken(const QString& token, SymbolCategory& out) {
    if (token == QLatin1String("component")) { out = SymbolCategory::Component; return true; }
    if (token == QLatin1String("terminal")) { out = SymbolCategory::Terminal; return true; }
    if (token == QLatin1String("probe")) { out = SymbolCategory::Probe; return true; }
    return false;
}

const char* catalogCategoryToken(CatalogCategory c) {
    switch (c) {
    case CatalogCategory::Passive: return "passive";
    case CatalogCategory::Diode: return "diode";
    case CatalogCategory::Transistor: return "transistor";
    case CatalogCategory::Analog: return "analog";
    case CatalogCategory::Digital: return "digital";
    case CatalogCategory::Source: return "source";
    case CatalogCategory::Electromechanical: return "electromechanical";
    case CatalogCategory::Connector: return "connector";
    }
    return "passive";
}

bool catalogCategoryFromToken(const QString& token, CatalogCategory& out) {
    static const QHash<QString, CatalogCategory> map{
        {QStringLiteral("passive"), CatalogCategory::Passive},
        {QStringLiteral("diode"), CatalogCategory::Diode},
        {QStringLiteral("transistor"), CatalogCategory::Transistor},
        {QStringLiteral("analog"), CatalogCategory::Analog},
        {QStringLiteral("digital"), CatalogCategory::Digital},
        {QStringLiteral("source"), CatalogCategory::Source},
        {QStringLiteral("electromechanical"), CatalogCategory::Electromechanical},
        {QStringLiteral("connector"), CatalogCategory::Connector},
    };
    const auto it = map.constFind(token);
    if (it == map.constEnd()) return false;
    out = it.value();
    return true;
}

const char* pinElectricalTypeToken(PinElectricalType t) {
    switch (t) {
    case PinElectricalType::Passive: return "passive";
    case PinElectricalType::Input: return "input";
    case PinElectricalType::Output: return "output";
    case PinElectricalType::PowerInput: return "power-input";
    case PinElectricalType::PowerOutput: return "power-output";
    case PinElectricalType::OpenCollector: return "open-collector";
    case PinElectricalType::NoConnect: return "no-connect";
    }
    return "passive";
}

bool pinElectricalTypeFromToken(const QString& token, PinElectricalType& out) {
    static const QHash<QString, PinElectricalType> map{
        {QStringLiteral("passive"), PinElectricalType::Passive},
        {QStringLiteral("input"), PinElectricalType::Input},
        {QStringLiteral("output"), PinElectricalType::Output},
        {QStringLiteral("power-input"), PinElectricalType::PowerInput},
        {QStringLiteral("power-output"), PinElectricalType::PowerOutput},
        {QStringLiteral("open-collector"), PinElectricalType::OpenCollector},
        {QStringLiteral("no-connect"), PinElectricalType::NoConnect},
    };
    const auto it = map.constFind(token);
    if (it == map.constEnd()) return false;
    out = it.value();
    return true;
}

QJsonObject variantToJson(const LibrarySymbolVariant& v) {
    QJsonObject o;
    o[QStringLiteral("id")] = v.id;
    o[QStringLiteral("shapes")] = shapesToJson(v.shapes);
    o[QStringLiteral("pinPositions")] = pointsToJson(v.pinPositions);
    if (!v.animationToken.isEmpty()) o[QStringLiteral("animationToken")] = v.animationToken;
    return o;
}

bool variantFromJson(const QJsonValue& value, LibrarySymbolVariant& out, QStringList& errors) {
    if (!value.isObject()) { errors << QStringLiteral("variant is not an object"); return false; }
    const QJsonObject o = value.toObject();
    out.id = o.value(QStringLiteral("id")).toString();
    if (out.id.isEmpty()) { errors << QStringLiteral("variant has no id"); return false; }
    if (!shapesFromJson(o.value(QStringLiteral("shapes")), out.shapes)) {
        errors << QStringLiteral("%1: invalid shapes").arg(out.id);
        return false;
    }
    if (!pointsFromJson(o.value(QStringLiteral("pinPositions")), out.pinPositions)) {
        errors << QStringLiteral("%1: invalid pinPositions").arg(out.id);
        return false;
    }
    out.animationToken = o.value(QStringLiteral("animationToken")).toString();
    return true;
}

QJsonObject deviceToJson(const LibraryDevice& d) {
    QJsonObject o;
    o[QStringLiteral("id")] = d.id;
    o[QStringLiteral("category")] = QLatin1String(categoryToken(d.category));
    o[QStringLiteral("prefix")] = d.prefix;
    if (!d.defaultValue.isEmpty()) o[QStringLiteral("defaultValue")] = d.defaultValue;
    if (!d.simulationModel.isEmpty()) o[QStringLiteral("simulationModel")] = d.simulationModel;
    if (!d.displayNameKey.isEmpty()) o[QStringLiteral("displayNameKey")] = d.displayNameKey;
    QJsonArray pins;
    for (const auto& p : d.pins) {
        pins.append(QJsonObject{{QStringLiteral("name"), p.name}, {QStringLiteral("number"), p.number},
                                 {QStringLiteral("type"), QLatin1String(pinElectricalTypeToken(p.type))}});
    }
    o[QStringLiteral("pins")] = pins;
    QJsonArray variants;
    for (const auto& v : d.variants) variants.append(variantToJson(v));
    o[QStringLiteral("variants")] = variants;
    QJsonArray footprints;
    for (const auto& f : d.footprints) {
        QJsonObject fo{{QStringLiteral("footprintId"), f.footprintId}};
        if (!f.pinPadMap.isEmpty()) {
            QJsonArray map;
            for (int n : f.pinPadMap) map.append(n);
            fo[QStringLiteral("pinPadMap")] = map;
        }
        footprints.append(fo);
    }
    o[QStringLiteral("footprints")] = footprints;
    if (d.category == SymbolCategory::Component) {
        o[QStringLiteral("catalogCategory")] = QLatin1String(catalogCategoryToken(d.catalogCategory));
        if (!d.description.isEmpty()) o[QStringLiteral("description")] = d.description;
        if (!d.keywords.isEmpty()) o[QStringLiteral("keywords")] = QJsonArray::fromStringList(d.keywords);
        if (!d.manufacturer.isEmpty()) o[QStringLiteral("manufacturer")] = d.manufacturer;
        if (!d.partNumber.isEmpty()) o[QStringLiteral("partNumber")] = d.partNumber;
    }
    return o;
}

bool deviceFromJson(const QJsonValue& value, LibraryDevice& out, QStringList& errors) {
    if (!value.isObject()) { errors << QStringLiteral("device is not an object"); return false; }
    const QJsonObject o = value.toObject();
    out.id = o.value(QStringLiteral("id")).toString();
    if (out.id.isEmpty()) { errors << QStringLiteral("device has no id"); return false; }
    if (!categoryFromToken(o.value(QStringLiteral("category")).toString(), out.category)) {
        errors << QStringLiteral("%1: invalid category").arg(out.id);
        return false;
    }
    out.prefix = o.value(QStringLiteral("prefix")).toString();
    out.defaultValue = o.value(QStringLiteral("defaultValue")).toString();
    out.simulationModel = o.value(QStringLiteral("simulationModel")).toString();
    out.displayNameKey = o.value(QStringLiteral("displayNameKey")).toString();
    for (const QJsonValue& pv : o.value(QStringLiteral("pins")).toArray()) {
        const QJsonObject po = pv.toObject();
        LibraryPin pin;
        pin.name = po.value(QStringLiteral("name")).toString();
        pin.number = po.value(QStringLiteral("number")).toInt();
        if (po.contains(QStringLiteral("type")) &&
            !pinElectricalTypeFromToken(po.value(QStringLiteral("type")).toString(), pin.type)) {
            errors << QStringLiteral("%1: invalid pin type").arg(out.id);
            return false;
        }
        out.pins.append(pin);
    }
    if (out.pins.isEmpty()) { errors << QStringLiteral("%1: no pins").arg(out.id); return false; }
    for (const QJsonValue& vv : o.value(QStringLiteral("variants")).toArray()) {
        LibrarySymbolVariant variant;
        if (!variantFromJson(vv, variant, errors)) return false;
        if (variant.pinPositions.size() != out.pins.size()) {
            errors << QStringLiteral("%1: variant %2 has %3 pin positions for %4 pins")
                          .arg(out.id, variant.id).arg(variant.pinPositions.size()).arg(out.pins.size());
            return false;
        }
        out.variants.append(variant);
    }
    if (out.variants.isEmpty()) { errors << QStringLiteral("%1: no variants").arg(out.id); return false; }
    for (const QJsonValue& fv : o.value(QStringLiteral("footprints")).toArray()) {
        const QJsonObject fo = fv.toObject();
        LibraryFootprintOption option;
        option.footprintId = fo.value(QStringLiteral("footprintId")).toString();
        if (option.footprintId.isEmpty()) { errors << QStringLiteral("%1: footprint option with no id").arg(out.id); return false; }
        for (const QJsonValue& n : fo.value(QStringLiteral("pinPadMap")).toArray()) option.pinPadMap.append(n.toInt());
        out.footprints.append(option);
    }
    if (o.contains(QStringLiteral("catalogCategory")) &&
        !catalogCategoryFromToken(o.value(QStringLiteral("catalogCategory")).toString(), out.catalogCategory)) {
        errors << QStringLiteral("%1: invalid catalogCategory").arg(out.id);
        return false;
    }
    out.description = o.value(QStringLiteral("description")).toString();
    for (const QJsonValue& kv : o.value(QStringLiteral("keywords")).toArray()) out.keywords << kv.toString();
    out.manufacturer = o.value(QStringLiteral("manufacturer")).toString();
    out.partNumber = o.value(QStringLiteral("partNumber")).toString();
    return true;
}

QJsonObject footprintToJson(const LibraryFootprint& f) {
    QJsonObject o;
    o[QStringLiteral("id")] = f.id;
    if (!f.displayNameKey.isEmpty()) o[QStringLiteral("displayNameKey")] = f.displayNameKey;
    if (f.category != SymbolCategory::Component) o[QStringLiteral("category")] = QLatin1String(categoryToken(f.category));
    if (!f.prefix.isEmpty()) o[QStringLiteral("prefix")] = f.prefix;
    o[QStringLiteral("shapes")] = shapesToJson(f.shapes);
    o[QStringLiteral("pins")] = pointsToJson(f.pins);
    QJsonArray pads;
    for (const auto& p : f.pads) pads.append(padToJson(p));
    o[QStringLiteral("pads")] = pads;
    if (!f.source.isEmpty()) o[QStringLiteral("source")] = f.source;
    return o;
}

bool footprintFromJson(const QJsonValue& value, LibraryFootprint& out, QStringList& errors) {
    if (!value.isObject()) { errors << QStringLiteral("footprint is not an object"); return false; }
    const QJsonObject o = value.toObject();
    out.id = o.value(QStringLiteral("id")).toString();
    if (out.id.isEmpty()) { errors << QStringLiteral("footprint has no id"); return false; }
    out.displayNameKey = o.value(QStringLiteral("displayNameKey")).toString();
    out.category = SymbolCategory::Component;
    if (o.contains(QStringLiteral("category")) &&
        !categoryFromToken(o.value(QStringLiteral("category")).toString(), out.category)) {
        errors << QStringLiteral("%1: invalid category").arg(out.id);
        return false;
    }
    out.prefix = o.value(QStringLiteral("prefix")).toString();
    if (!shapesFromJson(o.value(QStringLiteral("shapes")), out.shapes)) {
        errors << QStringLiteral("%1: invalid shapes").arg(out.id);
        return false;
    }
    if (!pointsFromJson(o.value(QStringLiteral("pins")), out.pins)) {
        errors << QStringLiteral("%1: invalid pins").arg(out.id);
        return false;
    }
    for (const QJsonValue& pv : o.value(QStringLiteral("pads")).toArray()) {
        PadDefinition pad;
        if (!padFromJson(pv, pad)) { errors << QStringLiteral("%1: invalid pad").arg(out.id); return false; }
        out.pads.append(pad);
    }
    if (out.pads.size() != out.pins.size()) {
        errors << QStringLiteral("%1: %2 pads for %3 pins").arg(out.id).arg(out.pads.size()).arg(out.pins.size());
        return false;
    }
    out.source = o.value(QStringLiteral("source")).toString();
    return true;
}

} // namespace

const LibrarySymbolVariant* LibraryDevice::variant(const QString& id) const {
    for (const auto& v : variants) {
        if (v.id == id) return &v;
    }
    return variants.isEmpty() ? nullptr : &variants.first();
}

QJsonObject libraryDataToJson(const LibraryData& data) {
    QJsonArray devices;
    for (const auto& d : data.devices) devices.append(deviceToJson(d));
    QJsonArray footprints;
    for (const auto& f : data.footprints) footprints.append(footprintToJson(f));
    QJsonArray aliases;
    for (const auto& a : data.aliases) aliases.append(QJsonObject{{QStringLiteral("oldId"), a.oldId}, {QStringLiteral("newId"), a.newId}});
    return {{QStringLiteral("devices"), devices}, {QStringLiteral("footprints"), footprints}, {QStringLiteral("aliases"), aliases}};
}

LibraryData libraryDataFromJson(const QJsonObject& root, QStringList& errors) {
    LibraryData data;
    for (const QJsonValue& dv : root.value(QStringLiteral("devices")).toArray()) {
        LibraryDevice device;
        if (deviceFromJson(dv, device, errors)) data.devices.append(device);
    }
    for (const QJsonValue& fv : root.value(QStringLiteral("footprints")).toArray()) {
        LibraryFootprint footprint;
        if (footprintFromJson(fv, footprint, errors)) data.footprints.append(footprint);
    }
    for (const QJsonValue& av : root.value(QStringLiteral("aliases")).toArray()) {
        const QJsonObject ao = av.toObject();
        const QString oldId = ao.value(QStringLiteral("oldId")).toString();
        const QString newId = ao.value(QStringLiteral("newId")).toString();
        if (oldId.isEmpty() || newId.isEmpty()) { errors << QStringLiteral("invalid alias entry"); continue; }
        data.aliases.append({oldId, newId});
    }
    return data;
}

SymbolDefinition deviceVariantToSymbolDefinition(const LibraryDevice& device, const LibrarySymbolVariant& variant) {
    SymbolDefinition symbol;
    symbol.id = device.id;
    symbol.workspace = Workspace::Schematic;
    symbol.category = device.category;
    symbol.prefix = device.prefix;
    symbol.shapes = variant.shapes;
    symbol.pins = variant.pinPositions;
    symbol.defaultValue = device.defaultValue;
    symbol.simulationModel = device.simulationModel;
    symbol.displayName = device.displayNameKey.isEmpty()
        ? QString()
        : QCoreApplication::translate("hatt::ui::SymbolLibrary", device.displayNameKey.toUtf8().constData());
    if (!device.footprints.isEmpty()) {
        symbol.defaultFootprint = device.footprints.first().footprintId;
        symbol.defaultPinPadMap = device.footprints.first().pinPadMap;
    }
    return symbol;
}

SymbolDefinition footprintToSymbolDefinition(const LibraryFootprint& footprint) {
    SymbolDefinition symbol;
    symbol.id = footprint.id;
    symbol.workspace = Workspace::Board;
    symbol.category = footprint.category;
    symbol.prefix = footprint.prefix;
    symbol.shapes = footprint.shapes;
    symbol.pins = footprint.pins;
    symbol.pads = footprint.pads;
    symbol.displayName = footprint.displayNameKey.isEmpty()
        ? QString()
        : QCoreApplication::translate("hatt::ui::SymbolLibrary", footprint.displayNameKey.toUtf8().constData());
    return symbol;
}

namespace {

LibraryData loadBuiltInLibraryData() {
    // The resource is compiled into the static hatt-ui-shell library; Qt does not auto-register a
    // static library's resources into whichever final executable links it, so every consumer
    // (hatteda.exe, every test binary) needs this explicit call once before the qrc path resolves.
    hattEdaInitLibraryResource();
    QFile file(QStringLiteral(":/library/builtin.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "hatteda: built-in library resource missing";
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "hatteda: built-in library resource is malformed:" << parseError.errorString();
        return {};
    }
    QStringList errors;
    LibraryData data = libraryDataFromJson(doc.object(), errors);
    for (const auto& message : errors) qWarning() << "hatteda: built-in library:" << message;
    return data;
}

BuiltInLibrary toRuntimeRegistry(const LibraryData& data) {
    BuiltInLibrary result;
    for (const auto& device : data.devices) {
        if (const auto* variant = device.variant(QStringLiteral("standard"))) {
            result.symbols.append(deviceVariantToSymbolDefinition(device, *variant));
        }
    }
    for (const auto& footprint : data.footprints) {
        result.symbols.append(footprintToSymbolDefinition(footprint));
    }
    for (const auto& alias : data.aliases) {
        result.aliases.insert(alias.oldId, alias.newId);
    }
    return result;
}

} // namespace

const LibraryData& builtInLibraryData() {
    static const LibraryData data = loadBuiltInLibraryData();
    return data;
}

const BuiltInLibrary& builtInLibrary() {
    static const BuiltInLibrary library = toRuntimeRegistry(builtInLibraryData());
    return library;
}

} // namespace hatt::ui
