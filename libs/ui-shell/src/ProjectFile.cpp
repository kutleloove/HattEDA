#include "hatt/ui/ProjectFile.hpp"

#include "hatt/ui/ComponentLibrary.hpp"
#include "hatt/ui/ComponentCatalog.hpp"
#include "hatt/ui/DesignChecks.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include <cmath>

namespace hatt::ui {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("hatt::ui::ProjectFile", text); }

struct KindName {
    SketchItem::Kind kind;
    const char* name;
    int minimumPoints;
};

// Stable file names of item kinds; never reuse or rename an entry.
constexpr KindName KindNames[] = {
    {SketchItem::Kind::Symbol, "symbol", 1},     {SketchItem::Kind::Wire, "wire", 2},
    {SketchItem::Kind::Line, "line", 2},         {SketchItem::Kind::Polyline, "polyline", 2},
    {SketchItem::Kind::Rectangle, "rectangle", 2}, {SketchItem::Kind::Circle, "circle", 2},
    {SketchItem::Kind::Arc, "arc", 3},           {SketchItem::Kind::Text, "text", 1},
    // v2 kinds
    {SketchItem::Kind::Pad, "pad", 1},           {SketchItem::Kind::Via, "via", 1},
};

// Stable file names of BoardLayer values; never reuse or rename.
struct LayerName { BoardLayer layer; const char* name; };
constexpr LayerName LayerNames[] = {
    {BoardLayer::TopCopper,    "top-copper"},
    {BoardLayer::BottomCopper, "bottom-copper"},
    {BoardLayer::TopSilk,      "top-silk"},
    {BoardLayer::BottomSilk,   "bottom-silk"},
    {BoardLayer::TopResist,    "top-resist"},
    {BoardLayer::BottomResist, "bottom-resist"},
    {BoardLayer::TopPaste,     "top-paste"},
    {BoardLayer::BottomPaste,  "bottom-paste"},
    {BoardLayer::BoardEdge,    "board-edge"},
};

// Stable file names of PadShape values.
struct PadShapeName { PadShape shape; const char* name; };
constexpr PadShapeName PadShapeNames[] = {
    {PadShape::Round, "round"},
    {PadShape::Rect,  "rect"},
    {PadShape::Oval,  "oval"},
};

const KindName* kindByValue(SketchItem::Kind kind) {
    for (const auto& entry : KindNames) {
        if (entry.kind == kind) return &entry;
    }
    return nullptr;
}

const KindName* kindByName(const QString& name) {
    for (const auto& entry : KindNames) {
        if (name == QLatin1String(entry.name)) return &entry;
    }
    return nullptr;
}

const char* layerToString(BoardLayer layer) {
    for (const auto& entry : LayerNames)
        if (entry.layer == layer) return entry.name;
    return "top-copper";
}

BoardLayer layerFromString(const QString& s) {
    for (const auto& entry : LayerNames)
        if (s == QLatin1String(entry.name)) return entry.layer;
    return BoardLayer::TopCopper;
}

const char* padShapeToString(PadShape shape) {
    for (const auto& entry : PadShapeNames)
        if (entry.shape == shape) return entry.name;
    return "rect";
}

PadShape padShapeFromString(const QString& s) {
    for (const auto& entry : PadShapeNames)
        if (s == QLatin1String(entry.name)) return entry.shape;
    return PadShape::Rect;
}

QJsonObject padToJson(const PadDefinition& pad) {
    QJsonObject o;
    o[QStringLiteral("number")] = pad.number;
    o[QStringLiteral("shape")] = QLatin1String(padShapeToString(pad.shape));
    o[QStringLiteral("width")] = pad.width;
    o[QStringLiteral("height")] = pad.height;
    if (pad.drillDiameter != 0.0) o[QStringLiteral("drill")] = pad.drillDiameter;
    o[QStringLiteral("layers")] = pad.layers;
    return o;
}

PadDefinition padFromJson(const QJsonObject& o) {
    PadDefinition pad;
    pad.number = o.value(QStringLiteral("number")).toInt(1);
    pad.shape = padShapeFromString(o.value(QStringLiteral("shape")).toString());
    pad.width = o.value(QStringLiteral("width")).toDouble(1.0);
    pad.height = o.value(QStringLiteral("height")).toDouble(pad.width);
    pad.drillDiameter = o.value(QStringLiteral("drill")).toDouble(0.0);
    pad.layers = o.value(QStringLiteral("layers")).toInt(
        1 << static_cast<int>(BoardLayer::TopCopper));
    return pad;
}

QJsonObject itemToJson(const SketchItem& item) {
    QJsonObject object;
    object[QStringLiteral("id")] = item.id;
    object[QStringLiteral("kind")] = QLatin1String(kindByValue(item.kind)->name);
    QJsonArray points;
    for (const QPointF& point : item.points) points.append(QJsonArray{point.x(), point.y()});
    object[QStringLiteral("points")] = points;
    // Optional v1 fields are written only when they differ from the default.
    if (!item.variant.isEmpty()) object[QStringLiteral("variant")] = item.variant;
    if (!item.label.isEmpty()) object[QStringLiteral("label")] = item.label;
    if (item.quarterTurns != 0) object[QStringLiteral("quarterTurns")] = item.quarterTurns;
    if (item.closed) object[QStringLiteral("closed")] = true;
    if (!item.value.isEmpty()) object[QStringLiteral("value")] = item.value;
    if (!item.footprint.isEmpty()) object[QStringLiteral("footprint")] = item.footprint;
    if (!item.pinPadMap.isEmpty()) {
        QJsonArray map;
        for (int pad : item.pinPadMap) map.append(pad);
        object[QStringLiteral("pinPadMap")] = map;
    }
    if (!item.sourceId.isEmpty()) object[QStringLiteral("sourceId")] = item.sourceId;
    // v2 fields
    if (item.layer != BoardLayer::TopCopper)
        object[QStringLiteral("layer")] = QLatin1String(layerToString(item.layer));
    if (item.onBottom) object[QStringLiteral("onBottom")] = true;
    if (item.excludeFromBoard) object[QStringLiteral("excludeFromBoard")] = true;
    if (item.kind == SketchItem::Kind::Pad)
        object[QStringLiteral("pad")] = padToJson(item.pad);
    if (item.kind == SketchItem::Kind::Via && item.drillDiameter != 0.0)
        object[QStringLiteral("drillDiameter")] = item.drillDiameter;
    if (item.width > 0.0) object[QStringLiteral("width")] = item.width;
    if (!item.net.isEmpty()) object[QStringLiteral("net")] = item.net;
    if (!item.fontFamily.isEmpty()) object[QStringLiteral("fontFamily")] = item.fontFamily;
    return object;
}

QJsonObject documentToJson(const SketchDocument& document) {
    QJsonArray items;
    for (const SketchItem& item : document) items.append(itemToJson(item));
    return {{QStringLiteral("items"), items}};
}

bool optionalString(const QJsonObject& object, const char* key, QString& target) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined()) return true;
    if (!value.isString()) return false;
    target = value.toString();
    return true;
}

// Reads one workspace; `where` names it in error messages.
QString documentFromJson(const QJsonValue& value, Workspace workspace, const QString& where,
                         SketchDocument& document) {
    if (!value.isObject() || !value.toObject().value(QStringLiteral("items")).isArray()) {
        return tr("The %1 section is missing or invalid.").arg(where);
    }
    const QJsonArray items = value.toObject().value(QStringLiteral("items")).toArray();
    QSet<QString> ids;
    for (qsizetype index = 0; index < items.size(); ++index) {
        const QString at = tr("%1 item %2").arg(where).arg(index + 1);
        if (!items[index].isObject()) return tr("%1 is not an object.").arg(at);
        const QJsonObject object = items[index].toObject();
        SketchItem item;

        const QString id = object.value(QStringLiteral("id")).toString();
        if (QUuid::fromString(id).isNull()) return tr("%1 has no valid id.").arg(at);
        if (ids.contains(id)) return tr("%1 repeats the id %2.").arg(at, id);
        ids.insert(id);
        item.id = id;

        const KindName* kind = kindByName(object.value(QStringLiteral("kind")).toString());
        if (kind == nullptr) return tr("%1 has an unknown kind.").arg(at);
        item.kind = kind->kind;

        const QJsonValue points = object.value(QStringLiteral("points"));
        if (!points.isArray()) return tr("%1 has no point list.").arg(at);
        for (const QJsonValue& point : points.toArray()) {
            const QJsonArray pair = point.toArray();
            if (!point.isArray() || pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble() ||
                !std::isfinite(pair[0].toDouble()) || !std::isfinite(pair[1].toDouble())) {
                return tr("%1 has an invalid point.").arg(at);
            }
            item.points.append({pair[0].toDouble(), pair[1].toDouble()});
        }
        if (item.points.size() < kind->minimumPoints) return tr("%1 has too few points.").arg(at);

        if (!optionalString(object, "variant", item.variant) ||
            !optionalString(object, "label", item.label) ||
            !optionalString(object, "value", item.value) ||
            !optionalString(object, "footprint", item.footprint) ||
            !optionalString(object, "sourceId", item.sourceId) ||
            !optionalString(object, "fontFamily", item.fontFamily)) {
            return tr("%1 has a text field of the wrong type.").arg(at);
        }
        const QJsonValue turns = object.value(QStringLiteral("quarterTurns"));
        if (!turns.isUndefined()) {
            if (!turns.isDouble() || turns.toDouble() != std::floor(turns.toDouble())) {
                return tr("%1 has an invalid rotation.").arg(at);
            }
            item.quarterTurns = (turns.toInt() % 4 + 4) % 4;
        }
        const QJsonValue closed = object.value(QStringLiteral("closed"));
        if (!closed.isUndefined()) {
            if (!closed.isBool()) return tr("%1 has an invalid closed flag.").arg(at);
            item.closed = closed.toBool();
        }
        const QJsonValue map = object.value(QStringLiteral("pinPadMap"));
        if (!map.isUndefined()) {
            if (!map.isArray()) return tr("%1 has an invalid pin to pad map.").arg(at);
            for (const QJsonValue& pad : map.toArray()) {
                if (!pad.isDouble() || pad.toDouble() != std::floor(pad.toDouble())) {
                    return tr("%1 has an invalid pin to pad map.").arg(at);
                }
                item.pinPadMap.append(pad.toInt());
            }
        }
        // v2 optional fields; silently ignored on v1 files (they won't be present).
        const QJsonValue layerVal = object.value(QStringLiteral("layer"));
        if (!layerVal.isUndefined())
            item.layer = layerFromString(layerVal.toString());
        const QJsonValue onBottom = object.value(QStringLiteral("onBottom"));
        if (!onBottom.isUndefined()) item.onBottom = onBottom.toBool();
        const QJsonValue exclude = object.value(QStringLiteral("excludeFromBoard"));
        if (!exclude.isUndefined()) item.excludeFromBoard = exclude.toBool();
        const QJsonValue padVal = object.value(QStringLiteral("pad"));
        if (!padVal.isUndefined() && padVal.isObject())
            item.pad = padFromJson(padVal.toObject());
        const QJsonValue drillVal = object.value(QStringLiteral("drillDiameter"));
        if (!drillVal.isUndefined()) item.drillDiameter = drillVal.toDouble();
        // Track width / via diameter (#28); absent in older files, which read as the default.
        const QJsonValue widthVal = object.value(QStringLiteral("width"));
        if (!widthVal.isUndefined()) {
            if (!widthVal.isDouble() || !std::isfinite(widthVal.toDouble()) || widthVal.toDouble() < 0.0) {
                return tr("%1 has an invalid width.").arg(at);
            }
            item.width = widthVal.toDouble();
        }
        // Copper zone net (ADR-0009); absent in older files, which read as an unpoured zone.
        const QJsonValue netVal = object.value(QStringLiteral("net"));
        if (!netVal.isUndefined()) {
            if (!netVal.isString()) return tr("%1 has an invalid net.").arg(at);
            item.net = netVal.toString();
        }
        if (item.kind == SketchItem::Kind::Symbol) {
            const auto* symbol = findSymbol(item.variant);
            if (symbol == nullptr || symbol->workspace != workspace) {
                return tr("%1 uses the unknown symbol '%2'.").arg(at, item.variant);
            }
        }
        document.append(item);
    }
    return {};
}

QJsonArray pointsToJson(const QVector<QPointF>& points) {
    QJsonArray array;
    for (const QPointF& point : points) array.append(QJsonArray{point.x(), point.y()});
    return array;
}

bool pointFromJson(const QJsonValue& value, QPointF& point) {
    const QJsonArray pair = value.toArray();
    if (!value.isArray() || pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble() ||
        !std::isfinite(pair[0].toDouble()) || !std::isfinite(pair[1].toDouble())) {
        return false;
    }
    point = {pair[0].toDouble(), pair[1].toDouble()};
    return true;
}

QJsonObject footprintToJson(const FootprintDefinition& footprint) {
    if (footprint.isExplicit()) {
        QJsonArray pads;
        for (int i = 0; i < footprint.pads.size(); ++i) {
            QJsonObject pad = padToJson(footprint.pads[i]);
            const QPointF at = footprint.pins.value(i);
            pad[QStringLiteral("at")] = QJsonArray{at.x(), at.y()};
            pads.append(pad);
        }
        QJsonArray shapes;
        for (const SymbolShape& shape : footprint.shapes) {
            QJsonObject object{{QStringLiteral("points"), pointsToJson(shape.points)}};
            if (shape.closed) object[QStringLiteral("closed")] = true;
            if (shape.filled) object[QStringLiteral("filled")] = true;
            shapes.append(object);
        }
        return {{QStringLiteral("id"), footprint.id},
                {QStringLiteral("name"), footprint.name},
                {QStringLiteral("pads"), pads},
                {QStringLiteral("shapes"), shapes}};
    }
    const FootprintParams& p = footprint.params;
    return {{QStringLiteral("id"), footprint.id},
            {QStringLiteral("name"), footprint.name},
            {QStringLiteral("style"), packageStyleToken(p.style)},
            {QStringLiteral("padCount"), p.padCount},
            {QStringLiteral("pitch"), p.pitch},
            {QStringLiteral("rowSpacing"), p.rowSpacing},
            {QStringLiteral("padShape"), QLatin1String(padShapeToString(p.shape))},
            {QStringLiteral("padWidth"), p.padWidth},
            {QStringLiteral("padLength"), p.padLength},
            {QStringLiteral("drill"), p.drill},
            {QStringLiteral("bodyWidth"), p.bodyWidth},
            {QStringLiteral("bodyLength"), p.bodyLength}};
}

QJsonObject deviceToJson(const DeviceDefinition& device) {
    QJsonObject object{{QStringLiteral("id"), device.id},
                       {QStringLiteral("name"), device.name},
                       {QStringLiteral("prefix"), device.prefix},
                       {QStringLiteral("pinCount"), device.pinCount}};
    if (!device.defaultValue.isEmpty()) object[QStringLiteral("value")] = device.defaultValue;
    if (!device.footprint.isEmpty()) object[QStringLiteral("footprint")] = device.footprint;
    if (!device.pinNames.isEmpty()) object[QStringLiteral("pinNames")] = QJsonArray::fromStringList(device.pinNames);
    if (!device.simulationModel.isEmpty())
        object[QStringLiteral("simulationModel")] = device.simulationModel;
    if (!device.pinPadMap.isEmpty()) {
        QJsonArray map;
        for (int pad : device.pinPadMap) map.append(pad);
        object[QStringLiteral("pinPadMap")] = map;
    }
    const DeviceSpec& s = device.spec;
    QJsonObject spec;
    auto text = [&spec](const char* key, const QString& value) {
        if (!value.isEmpty()) spec[QLatin1String(key)] = value;
    };
    auto number = [&spec](const char* key, double value) {
        if (value != 0.0) spec[QLatin1String(key)] = value;
    };
    text("manufacturer", s.manufacturer);
    text("partNumber", s.partNumber);
    text("datasheet", s.datasheet);
    if (s.package != PackageStyle::None) spec[QStringLiteral("package")] = packageStyleToken(s.package);
    if (s.throughHole) spec[QStringLiteral("throughHole")] = true;
    number("pitch", s.pitch);
    number("rowSpacing", s.rowSpacing);
    number("bodyWidth", s.bodyWidth);
    number("bodyLength", s.bodyLength);
    number("leadWidth", s.leadWidth);
    number("leadLength", s.leadLength);
    number("pinCurrent", s.pinCurrent);
    if (!spec.isEmpty()) object[QStringLiteral("spec")] = spec;
    return object;
}

// Reads a non-negative finite length; a missing key keeps `target`.
bool readLength(const QJsonObject& object, const char* key, double& target) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined()) return true;
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0) return false;
    target = value.toDouble();
    return true;
}

// Reads the library before the documents: custom symbols must be registered so that document items
// using them validate.
QString libraryFromJson(const QJsonValue& value, ProjectLibrary& library) {
    registerBuiltInCatalog();
    // The library is optional: v1 files and early v2 files have none or an empty object.
    if (value.isUndefined()) return {};
    if (!value.isObject()) return tr("The library section is invalid.");
    const QJsonObject object = value.toObject();
    const QJsonValue devices = object.value(QStringLiteral("devices"));
    const QJsonValue footprints = object.value(QStringLiteral("customFootprints"));
    const QJsonValue customDevices = object.value(QStringLiteral("customDevices"));
    for (const QJsonValue& list : {devices, footprints, customDevices}) {
        if (!list.isUndefined() && !list.isArray()) return tr("The library section is invalid.");
    }
    QSet<QString> ids;
    for (const QJsonValue& entry : footprints.toArray()) {
        const QJsonObject o = entry.toObject();
        FootprintDefinition footprint;
        footprint.id = o.value(QStringLiteral("id")).toString();
        footprint.name = o.value(QStringLiteral("name")).toString();
        const bool identified = entry.isObject() && footprint.id.startsWith(CustomFootprintPrefix) &&
                                !ids.contains(footprint.id) && !footprint.name.trimmed().isEmpty();
        if (identified && o.contains(QStringLiteral("pads"))) {
            // Explicit geometry (Make Package): pads with positions and silkscreen shapes.
            const QJsonValue pads = o.value(QStringLiteral("pads"));
            const QJsonValue shapes = o.value(QStringLiteral("shapes"));
            bool valid = pads.isArray() && (shapes.isUndefined() || shapes.isArray());
            for (const QJsonValue& padValue : pads.toArray()) {
                const QJsonObject pad = padValue.toObject();
                QPointF at;
                valid = valid && padValue.isObject() && pad.value(QStringLiteral("number")).isDouble() &&
                        pad.value(QStringLiteral("width")).isDouble() &&
                        pad.value(QStringLiteral("height")).isDouble() &&
                        pad.value(QStringLiteral("layers")).isDouble() &&
                        pointFromJson(pad.value(QStringLiteral("at")), at);
                footprint.pads.append(padFromJson(pad));
                footprint.pins.append(at);
            }
            for (const QJsonValue& shapeValue : shapes.toArray()) {
                const QJsonObject object = shapeValue.toObject();
                SymbolShape shape;
                valid = valid && shapeValue.isObject() && object.value(QStringLiteral("points")).isArray();
                for (const QJsonValue& pointValue : object.value(QStringLiteral("points")).toArray()) {
                    QPointF point;
                    valid = valid && pointFromJson(pointValue, point);
                    shape.points.append(point);
                }
                shape.closed = object.value(QStringLiteral("closed")).toBool();
                shape.filled = object.value(QStringLiteral("filled")).toBool();
                footprint.shapes.append(shape);
            }
            if (!valid) return tr("The library has an invalid footprint '%1'.").arg(footprint.name);
            const QString problem = validateExplicitFootprint(footprint);
            if (!problem.isEmpty()) return tr("The footprint '%1' is invalid: %2").arg(footprint.name, problem);
            ids.insert(footprint.id);
            library.customFootprints.append(footprint);
            continue;
        }
        FootprintParams& p = footprint.params;
        const QJsonValue count = o.value(QStringLiteral("padCount"));
        const bool valid = entry.isObject() && footprint.id.startsWith(CustomFootprintPrefix) && !ids.contains(footprint.id) &&
                           !footprint.name.trimmed().isEmpty() &&
                           packageStyleFromToken(o.value(QStringLiteral("style")).toString(), p.style) && count.isDouble() &&
                           count.toDouble() == std::floor(count.toDouble()) && readLength(o, "pitch", p.pitch) &&
                           readLength(o, "rowSpacing", p.rowSpacing) && readLength(o, "padWidth", p.padWidth) &&
                           readLength(o, "padLength", p.padLength) && readLength(o, "drill", p.drill) &&
                           readLength(o, "bodyWidth", p.bodyWidth) && readLength(o, "bodyLength", p.bodyLength);
        if (!valid) return tr("The library has an invalid footprint '%1'.").arg(footprint.name);
        p.padCount = count.toInt();
        p.shape = padShapeFromString(o.value(QStringLiteral("padShape")).toString());
        const QString problem = validateFootprintParams(p);
        if (!problem.isEmpty()) return tr("The footprint '%1' is invalid: %2").arg(footprint.name, problem);
        ids.insert(footprint.id);
        library.customFootprints.append(footprint);
    }
    registerProjectLibrary(library);
    for (const QJsonValue& entry : customDevices.toArray()) {
        const QJsonObject o = entry.toObject();
        DeviceDefinition device;
        device.id = o.value(QStringLiteral("id")).toString();
        device.name = o.value(QStringLiteral("name")).toString();
        device.prefix = o.value(QStringLiteral("prefix")).toString();
        device.defaultValue = o.value(QStringLiteral("value")).toString();
        device.footprint = o.value(QStringLiteral("footprint")).toString();
        device.simulationModel = o.value(QStringLiteral("simulationModel")).toString();
        const QJsonValue count = o.value(QStringLiteral("pinCount"));
        const QJsonObject spec = o.value(QStringLiteral("spec")).toObject();
        DeviceSpec& s = device.spec;
        s.manufacturer = spec.value(QStringLiteral("manufacturer")).toString();
        s.partNumber = spec.value(QStringLiteral("partNumber")).toString();
        s.datasheet = spec.value(QStringLiteral("datasheet")).toString();
        s.throughHole = spec.value(QStringLiteral("throughHole")).toBool();
        bool valid = entry.isObject() && device.id.startsWith(CustomDevicePrefix) && !ids.contains(device.id) &&
                     !device.name.trimmed().isEmpty() && !device.prefix.isEmpty() && count.isDouble() &&
                     count.toDouble() == std::floor(count.toDouble()) && count.toInt() >= 1 &&
                     count.toInt() <= MaxGeneratedPads && readLength(spec, "pitch", s.pitch) &&
                     readLength(spec, "rowSpacing", s.rowSpacing) && readLength(spec, "bodyWidth", s.bodyWidth) &&
                     readLength(spec, "bodyLength", s.bodyLength) && readLength(spec, "leadWidth", s.leadWidth) &&
                     readLength(spec, "leadLength", s.leadLength) && readLength(spec, "pinCurrent", s.pinCurrent);
        const QJsonValue package = spec.value(QStringLiteral("package"));
        valid = valid && (package.isUndefined() || packageStyleFromToken(package.toString(), s.package));
        device.pinCount = count.toInt();
        for (const QJsonValue& name : o.value(QStringLiteral("pinNames")).toArray()) {
            valid = valid && name.isString();
            device.pinNames << name.toString();
        }
        valid = valid && device.pinNames.size() <= device.pinCount;
        const QJsonValue map = o.value(QStringLiteral("pinPadMap"));
        valid = valid && (map.isUndefined() || map.isArray());
        for (const QJsonValue& pad : map.toArray()) {
            valid = valid && pad.isDouble() && pad.toDouble() == std::floor(pad.toDouble());
            device.pinPadMap.append(pad.toInt());
        }
        valid = valid && validatePinPadMap(device.pinPadMap, device.pinCount).isEmpty();
        valid = valid && (device.simulationModel.isEmpty() ||
                          findSimulationModel(device.simulationModel) != nullptr);
        if (valid && !device.footprint.isEmpty()) {
            const auto* footprint = findSymbol(device.footprint);
            valid = footprint != nullptr && footprint->workspace == Workspace::Board &&
                    footprint->pins.size() == device.pinCount;
        }
        if (!valid) return tr("The library has an invalid device '%1'.").arg(device.name);
        ids.insert(device.id);
        library.customDevices.append(device);
    }
    registerProjectLibrary(library);
    for (const QJsonValue& device : devices.toArray()) {
        if (!device.isString() || !isPickableDevice(device.toString())) {
            return tr("The library lists the unknown device '%1'.").arg(device.toString());
        }
        if (!library.devices.contains(device.toString())) library.devices.append(device.toString());
    }
    return {};
}

// Design rules (ADR-0008) are optional; a missing object or key keeps the default value.
// The Design Rule Manager parts (ADR-0010) live in DesignRules.cpp.
QString rulesFromJson(const QJsonValue& value, DesignRules& rules) { return designRulesFromJson(value, rules); }

} // namespace

QByteArray serializeProject(const ProjectData& project) {
    QJsonObject root;
    root[QStringLiteral("format")] = ProjectFormatName;
    root[QStringLiteral("formatVersion")] = ProjectFormatVersion;
    root[QStringLiteral("name")] = project.name;
    root[QStringLiteral("schematic")] = documentToJson(project.schematic);
    root[QStringLiteral("board")] = documentToJson(project.board);
    QJsonObject library{{QStringLiteral("devices"), QJsonArray::fromStringList(project.library.devices)}};
    if (!project.library.customFootprints.isEmpty()) {
        QJsonArray footprints;
        for (const auto& footprint : project.library.customFootprints) footprints.append(footprintToJson(footprint));
        library[QStringLiteral("customFootprints")] = footprints;
    }
    if (!project.library.customDevices.isEmpty()) {
        QJsonArray devices;
        for (const auto& device : project.library.customDevices) devices.append(deviceToJson(device));
        library[QStringLiteral("customDevices")] = devices;
    }
    root[QStringLiteral("library")] = library;
    root[QStringLiteral("rules")] = designRulesToJson(project.rules);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ProjectLoad parseProject(const QByteArray& bytes) {
    ProjectLoad result;
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        result.error = tr("The file is not a HattEDA project (invalid JSON).");
        return result;
    }
    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("format")).toString() != ProjectFormatName) {
        result.error = tr("The file is not a HattEDA project.");
        return result;
    }
    const QJsonValue version = root.value(QStringLiteral("formatVersion"));
    if (!version.isDouble() || version.toDouble() != std::floor(version.toDouble()) ||
        version.toInt() < 1) {
        result.error = tr("The project format version is missing or invalid.");
        return result;
    }
    if (version.toInt() > ProjectFormatVersion) {
        result.error = tr("The project was saved by a newer HattEDA (format %1; this version reads "
                          "up to %2). Update HattEDA to open it.")
                           .arg(version.toInt())
                           .arg(ProjectFormatVersion);
        return result;
    }
    // v1 files are silently upgraded: new v2 fields take their defaults during item parsing.
    result.project.name = root.value(QStringLiteral("name")).toString();
    QString error = libraryFromJson(root.value(QStringLiteral("library")), result.project.library);
    if (error.isEmpty()) error = rulesFromJson(root.value(QStringLiteral("rules")), result.project.rules);
    if (error.isEmpty()) {
        error = documentFromJson(root.value(QStringLiteral("schematic")), Workspace::Schematic,
                                 tr("schematic"), result.project.schematic);
    }
    if (error.isEmpty()) {
        error = documentFromJson(root.value(QStringLiteral("board")), Workspace::Board, tr("board"),
                                 result.project.board);
    }
    if (!error.isEmpty()) {
        result.project = {};
        result.error = error;
    }
    return result;
}

QString saveProjectFile(const QString& path, const ProjectData& project) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return tr("Cannot write %1: %2").arg(path, file.errorString());
    }
    file.write(serializeProject(project));
    if (!file.commit()) {
        return tr("Cannot write %1: %2").arg(path, file.errorString());
    }
    return {};
}

ProjectLoad loadProjectFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        ProjectLoad result;
        result.error = tr("Cannot open %1: %2").arg(path, file.errorString());
        return result;
    }
    return parseProject(file.readAll());
}

} // namespace hatt::ui
