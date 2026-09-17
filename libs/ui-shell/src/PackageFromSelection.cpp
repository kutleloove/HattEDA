#include "hatt/ui/PackageFromSelection.hpp"

#include <QLineF>

#include <algorithm>
#include <functional>
#include <numeric>

namespace hatt::ui {
namespace {

bool isSilkLayer(BoardLayer layer) { return layer == BoardLayer::TopSilk || layer == BoardLayer::BottomSilk; }

bool isGraphic(SketchItem::Kind kind) {
    return kind == SketchItem::Kind::Line || kind == SketchItem::Kind::Polyline ||
           kind == SketchItem::Kind::Rectangle || kind == SketchItem::Kind::Circle ||
           kind == SketchItem::Kind::Arc;
}

bool samePoint(QPointF a, QPointF b) { return QLineF(a, b).length() < 1e-9; }

// Joins an item's outline segments back into one polyline.
SymbolShape outlineShape(const SketchItem& item) {
    SymbolShape shape;
    const auto segments = itemSegments(item);
    if (segments.isEmpty()) return shape;
    shape.points.append(segments.first().p1());
    for (const QLineF& segment : segments) {
        if (!samePoint(shape.points.last(), segment.p1())) shape.points.append(segment.p1());
        shape.points.append(segment.p2());
    }
    if (shape.points.size() > 2 && samePoint(shape.points.first(), shape.points.last())) {
        shape.points.removeLast();
        shape.closed = true;
    }
    return shape;
}

} // namespace

PackageExtraction extractPackage(const SketchDocument& document, const QList<int>& selection,
                                 PackageOrigin origin) {
    PackageExtraction result;
    QList<int> ordered = selection;
    std::sort(ordered.begin(), ordered.end());
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());

    QVector<PlacedPad> pads;
    QVector<SymbolShape> silk;
    bool top = false;
    bool bottom = false;
    for (int index : ordered) {
        if (index < 0 || index >= document.size()) continue;
        const SketchItem& item = document[index];
        if (item.kind == SketchItem::Kind::Pad || item.kind == SketchItem::Kind::Via) {
            const auto placed = itemPads(item);
            if (placed.isEmpty()) {
                ++result.ignoredItems;
                continue;
            }
            for (const auto& pad : placed) {
                if (pad.drill <= 0.0) {
                    top = top || (pad.layers & layerBit(BoardLayer::TopCopper)) != 0;
                    bottom = bottom || (pad.layers & layerBit(BoardLayer::BottomCopper)) != 0;
                }
                pads.append(pad);
            }
            result.usedItems.append(index);
        } else if (isGraphic(item.kind) && isSilkLayer(item.layer) && !isBoardOutline(item) &&
                   !isZoneVariant(item.variant)) {
            SymbolShape shape = outlineShape(item);
            if (shape.points.size() < 2) {
                ++result.ignoredItems;
                continue;
            }
            (item.layer == BoardLayer::BottomSilk ? bottom : top) = true;
            silk.append(shape);
            result.usedItems.append(index);
        } else {
            ++result.ignoredItems;
        }
    }
    if (pads.isEmpty()) return result;

    // Keep pad numbers that already run 1..n; otherwise renumber in number, then selection order.
    const int count = static_cast<int>(pads.size());
    QVector<bool> used(count + 1, false);
    bool valid = true;
    for (const auto& pad : pads) {
        if (pad.number < 1 || pad.number > count || used[pad.number]) {
            valid = false;
            break;
        }
        used[pad.number] = true;
    }
    if (!valid) {
        QVector<int> order(count);
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(),
                         [&pads](int a, int b) { return pads[a].number < pads[b].number; });
        for (int rank = 0; rank < count; ++rank) pads[order[rank]].number = rank + 1;
        result.renumbered = true;
    }

    if (origin == PackageOrigin::FirstPad) {
        const auto first = std::find_if(pads.begin(), pads.end(), [](const PlacedPad& pad) { return pad.number == 1; });
        result.origin = first->center;
    } else {
        QRectF bounds;
        for (const auto& pad : pads) {
            const QRectF rect(pad.center - QPointF(pad.width, pad.height) / 2.0, QSizeF(pad.width, pad.height));
            bounds = bounds.isNull() ? rect : bounds.united(rect);
        }
        result.origin = bounds.center();
    }

    // A package drawn only on the bottom side is stored as seen from the top and placed mirrored.
    result.mirrored = bottom && !top;
    auto local = [&](QPointF world) {
        QPointF point = world - result.origin;
        if (result.mirrored) point.setX(-point.x());
        return point;
    };
    FootprintDefinition& footprint = result.footprint;
    for (const auto& pad : pads) {
        PadDefinition definition;
        definition.number = pad.number;
        definition.shape = pad.shape;
        definition.width = pad.width;
        definition.height = pad.height;
        definition.drillDiameter = pad.drill;
        definition.layers = result.mirrored ? mirroredLayerMask(pad.layers) : pad.layers;
        footprint.pins.append(local(pad.center));
        footprint.pads.append(definition);
    }
    for (SymbolShape shape : silk) {
        for (QPointF& point : shape.points) point = local(point);
        footprint.shapes.append(shape);
    }
    return result;
}

SketchDocument replaceWithPackage(const SketchDocument& document, const PackageExtraction& extraction,
                                  const QString& footprintId) {
    SketchDocument result = document;
    QList<int> removed = extraction.usedItems;
    std::sort(removed.begin(), removed.end(), std::greater<>());
    for (int index : removed) {
        if (index >= 0 && index < result.size()) result.removeAt(index);
    }
    SketchItem item;
    item.kind = SketchItem::Kind::Symbol;
    item.variant = footprintId;
    item.points = {extraction.origin};
    item.onBottom = extraction.mirrored;
    item.layer = extraction.mirrored ? BoardLayer::BottomCopper : BoardLayer::TopCopper;
    result.append(item);
    return result;
}

int decomposePackages(SketchDocument& document, const QList<int>& selection) {
    QList<int> ordered = selection;
    std::sort(ordered.begin(), ordered.end(), std::greater<>());
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());
    int decomposed = 0;
    for (int index : ordered) {
        if (index < 0 || index >= document.size()) continue;
        const SketchItem footprint = document[index];
        const auto* symbol = footprint.kind == SketchItem::Kind::Symbol ? findSymbol(footprint.variant) : nullptr;
        if (symbol == nullptr || symbol->workspace != Workspace::Board) continue;

        SketchDocument parts;
        for (const auto& placed : itemPads(footprint)) {
            SketchItem pad;
            pad.kind = SketchItem::Kind::Pad;
            pad.points = {placed.center};
            pad.pad.number = placed.number;
            pad.pad.shape = placed.shape;
            pad.pad.width = placed.width;
            pad.pad.height = placed.height;
            pad.pad.drillDiameter = placed.drill;
            pad.pad.layers = placed.layers;
            pad.layer = (placed.layers & layerBit(BoardLayer::TopCopper)) != 0 ? BoardLayer::TopCopper
                                                                               : BoardLayer::BottomCopper;
            parts.append(pad);
        }
        const BoardLayer silkLayer = footprint.onBottom ? BoardLayer::BottomSilk : BoardLayer::TopSilk;
        for (const auto& shape : symbol->shapes) {
            if (shape.points.size() < 2 || shape.copper || shape.hole) continue;
            SketchItem outline;
            const bool line = shape.points.size() == 2 && !shape.closed;
            outline.kind = line ? SketchItem::Kind::Line : SketchItem::Kind::Polyline;
            outline.closed = shape.closed && !line;
            outline.layer = silkLayer;
            for (const QPointF& point : shape.points) outline.points.append(symbolToWorld(footprint, point));
            parts.append(outline);
        }
        document.removeAt(index);
        for (qsizetype i = 0; i < parts.size(); ++i) document.insert(index + i, parts[i]);
        ++decomposed;
    }
    return decomposed;
}

} // namespace hatt::ui
