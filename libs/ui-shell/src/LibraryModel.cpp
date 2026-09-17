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
    for (const auto& p : d.pins) pins.append(QJsonObject{{QStringLiteral("name"), p.name}, {QStringLiteral("number"), p.number}});
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
        out.pins.append({po.value(QStringLiteral("name")).toString(), po.value(QStringLiteral("number")).toInt()});
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

BuiltInLibrary loadBuiltInLibrary() {
    // The resource is compiled into the static hatt-ui-shell library; Qt does not auto-register a
    // static library's resources into whichever final executable links it, so every consumer
    // (hatteda.exe, every test binary) needs this explicit call once before the qrc path resolves.
    hattEdaInitLibraryResource();
    BuiltInLibrary result;
    QFile file(QStringLiteral(":/library/builtin.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "hatteda: built-in library resource missing";
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "hatteda: built-in library resource is malformed:" << parseError.errorString();
        return result;
    }
    QStringList errors;
    const LibraryData data = libraryDataFromJson(doc.object(), errors);
    for (const auto& message : errors) qWarning() << "hatteda: built-in library:" << message;
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

const BuiltInLibrary& builtInLibrary() {
    static const BuiltInLibrary library = loadBuiltInLibrary();
    return library;
}

} // namespace hatt::ui
