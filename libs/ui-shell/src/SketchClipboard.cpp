#include "hatt/ui/SketchClipboard.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace hatt::ui {
namespace {

QJsonArray pointsToJson(const QVector<QPointF>& points) {
    QJsonArray array;
    for (const QPointF& point : points) array.append(QJsonArray{point.x(), point.y()});
    return array;
}

QVector<QPointF> pointsFromJson(const QJsonArray& array) {
    QVector<QPointF> points;
    for (const QJsonValue& value : array) {
        const QJsonArray pair = value.toArray();
        if (pair.size() != 2) return {};
        points.append({pair[0].toDouble(), pair[1].toDouble()});
    }
    return points;
}

QJsonObject padToJson(const PadDefinition& pad) {
    return {{QStringLiteral("number"), pad.number},
            {QStringLiteral("shape"), static_cast<int>(pad.shape)},
            {QStringLiteral("width"), pad.width},
            {QStringLiteral("height"), pad.height},
            {QStringLiteral("drillDiameter"), pad.drillDiameter},
            {QStringLiteral("layers"), pad.layers}};
}

PadDefinition padFromJson(const QJsonObject& object) {
    PadDefinition pad;
    pad.number = object.value(QStringLiteral("number")).toInt(1);
    pad.shape = static_cast<PadShape>(object.value(QStringLiteral("shape")).toInt());
    pad.width = object.value(QStringLiteral("width")).toDouble();
    pad.height = object.value(QStringLiteral("height")).toDouble();
    pad.drillDiameter = object.value(QStringLiteral("drillDiameter")).toDouble();
    pad.layers = object.value(QStringLiteral("layers")).toInt();
    return pad;
}

QJsonObject itemToClipboardJson(const SketchItem& item) {
    QJsonArray pinPadMap;
    for (int pad : item.pinPadMap) pinPadMap.append(pad);
    return {{QStringLiteral("kind"), static_cast<int>(item.kind)},
            {QStringLiteral("points"), pointsToJson(item.points)},
            {QStringLiteral("variant"), item.variant},
            {QStringLiteral("label"), item.label},
            {QStringLiteral("quarterTurns"), item.quarterTurns},
            {QStringLiteral("closed"), item.closed},
            {QStringLiteral("value"), item.value},
            {QStringLiteral("footprint"), item.footprint},
            {QStringLiteral("pinPadMap"), pinPadMap},
            {QStringLiteral("layer"), static_cast<int>(item.layer)},
            {QStringLiteral("onBottom"), item.onBottom},
            {QStringLiteral("excludeFromBoard"), item.excludeFromBoard},
            {QStringLiteral("mirroredX"), item.mirroredX},
            {QStringLiteral("mirroredY"), item.mirroredY},
            {QStringLiteral("pad"), padToJson(item.pad)},
            {QStringLiteral("drillDiameter"), item.drillDiameter},
            {QStringLiteral("width"), item.width},
            {QStringLiteral("net"), item.net},
            {QStringLiteral("fontFamily"), item.fontFamily},
            {QStringLiteral("zoneFill"), static_cast<int>(item.zoneFill)}};
}

// A fresh id per item; paste always regenerates ids (and designators) anyway, so the clipboard
// payload does not need to carry the copied ids or sourceId links across.
bool itemFromClipboardJson(const QJsonObject& object, SketchItem& item) {
    const int kind = object.value(QStringLiteral("kind")).toInt(-1);
    if (kind < 0 || kind > static_cast<int>(SketchItem::Kind::Via)) return false;
    item.kind = static_cast<SketchItem::Kind>(kind);
    item.points = pointsFromJson(object.value(QStringLiteral("points")).toArray());
    if (item.points.isEmpty()) return false;
    item.variant = object.value(QStringLiteral("variant")).toString();
    item.label = object.value(QStringLiteral("label")).toString();
    item.quarterTurns = object.value(QStringLiteral("quarterTurns")).toInt();
    item.closed = object.value(QStringLiteral("closed")).toBool();
    item.value = object.value(QStringLiteral("value")).toString();
    item.footprint = object.value(QStringLiteral("footprint")).toString();
    for (const QJsonValue& pad : object.value(QStringLiteral("pinPadMap")).toArray()) {
        item.pinPadMap.append(pad.toInt());
    }
    item.layer = static_cast<BoardLayer>(object.value(QStringLiteral("layer")).toInt());
    item.onBottom = object.value(QStringLiteral("onBottom")).toBool();
    item.excludeFromBoard = object.value(QStringLiteral("excludeFromBoard")).toBool();
    item.mirroredX = object.value(QStringLiteral("mirroredX")).toBool();
    item.mirroredY = object.value(QStringLiteral("mirroredY")).toBool();
    item.pad = padFromJson(object.value(QStringLiteral("pad")).toObject());
    item.drillDiameter = object.value(QStringLiteral("drillDiameter")).toDouble();
    item.width = object.value(QStringLiteral("width")).toDouble();
    item.net = object.value(QStringLiteral("net")).toString();
    item.fontFamily = object.value(QStringLiteral("fontFamily")).toString();
    item.zoneFill = static_cast<ZoneFillStyle>(object.value(QStringLiteral("zoneFill")).toInt());
    return true;
}

} // namespace

QByteArray encodeSketchClipboard(const SketchDocument& items, Workspace workspace) {
    QJsonArray array;
    for (const SketchItem& item : items) array.append(itemToClipboardJson(item));
    const QJsonObject root{{QStringLiteral("workspace"), static_cast<int>(workspace)},
                           {QStringLiteral("items"), array}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

SketchClipboardPayload decodeSketchClipboard(const QByteArray& data) {
    SketchClipboardPayload payload;
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return payload;
    const QJsonObject root = doc.object();
    const int workspace = root.value(QStringLiteral("workspace")).toInt(-1);
    if (workspace != static_cast<int>(Workspace::Schematic) && workspace != static_cast<int>(Workspace::Board)) {
        return payload;
    }
    const QJsonValue itemsValue = root.value(QStringLiteral("items"));
    if (!itemsValue.isArray() || itemsValue.toArray().isEmpty()) return payload;
    SketchDocument items;
    for (const QJsonValue& value : itemsValue.toArray()) {
        if (!value.isObject()) return payload;
        SketchItem item;
        if (!itemFromClipboardJson(value.toObject(), item)) return payload;
        items.append(item);
    }
    payload.items = items;
    payload.workspace = static_cast<Workspace>(workspace);
    payload.valid = true;
    return payload;
}

} // namespace hatt::ui
