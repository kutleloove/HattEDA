#include "hatt/ui/ProjectFile.hpp"

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
            !optionalString(object, "sourceId", item.sourceId)) {
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

} // namespace

QByteArray serializeProject(const ProjectData& project) {
    QJsonObject root;
    root[QStringLiteral("format")] = ProjectFormatName;
    root[QStringLiteral("formatVersion")] = ProjectFormatVersion;
    root[QStringLiteral("name")] = project.name;
    root[QStringLiteral("schematic")] = documentToJson(project.schematic);
    root[QStringLiteral("board")] = documentToJson(project.board);
    root[QStringLiteral("library")] = QJsonObject{}; // v2 placeholder; extended in #27/#29
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
    QString error = documentFromJson(root.value(QStringLiteral("schematic")), Workspace::Schematic,
                                     tr("schematic"), result.project.schematic);
    if (error.isEmpty()) {
        error = documentFromJson(root.value(QStringLiteral("board")), Workspace::Board, tr("board"),
                                 result.project.board);
    }
    if (!error.isEmpty()) {
        result.project = {};
        result.error = error;
    }
    // v2: library stub (currently empty; no error if absent for v1 compat)
    // Extended in Issues #27/#29.
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
