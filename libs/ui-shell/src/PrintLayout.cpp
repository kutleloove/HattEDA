#include "hatt/ui/PrintLayout.hpp"

#include "hatt/ui/LayerColors.hpp"

#include <QCoreApplication>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace hatt::ui {
namespace {

constexpr int OutlineBit = 1 << static_cast<int>(CamLayerKind::Outline);

QPainterPath flashPath(const CamAperture& aperture, QPointF at) {
    QPainterPath path;
    const double height = aperture.shape == CamApertureShape::Circle ? aperture.width : aperture.height;
    const QRectF box(at.x() - aperture.width / 2.0, at.y() - height / 2.0, aperture.width, height);
    switch (aperture.shape) {
    case CamApertureShape::Circle: path.addEllipse(box); break;
    case CamApertureShape::Rectangle: path.addRect(box); break;
    case CamApertureShape::Obround: {
        const double radius = std::min(box.width(), box.height()) / 2.0;
        path.addRoundedRect(box, radius, radius);
        break;
    }
    }
    return path;
}

QVector<CamLayerKind> selectedLayers(int layers) {
    QVector<CamLayerKind> result;
    for (int kind = 0; kind <= static_cast<int>(CamLayerKind::Outline); ++kind) {
        if (layers & (1 << kind)) result.append(static_cast<CamLayerKind>(kind));
    }
    return result;
}

// Layer masks of the artwork boxes of one copy.
QVector<int> copyGroups(const PrintSettings& settings) {
    if (settings.grouping == PrintGrouping::Overlay) return {settings.layers};
    QVector<int> groups;
    for (CamLayerKind kind : selectedLayers(settings.layers & ~OutlineBit)) {
        groups.append((1 << static_cast<int>(kind)) | (settings.layers & OutlineBit));
    }
    if (groups.isEmpty() && settings.layers != 0) groups.append(settings.layers);
    return groups;
}

QSizeF tileSize(const PrintSettings& settings, QSizeF artwork) {
    QSizeF size = settings.rotate ? QSizeF(artwork.height(), artwork.width()) : artwork;
    return {size.width() * settings.scale * settings.compensationX, size.height() * settings.scale * settings.compensationY};
}

BoardLayer boardLayerOf(CamLayerKind kind) {
    switch (kind) {
    case CamLayerKind::TopCopper: return BoardLayer::TopCopper;
    case CamLayerKind::BottomCopper: return BoardLayer::BottomCopper;
    case CamLayerKind::TopSilk: return BoardLayer::TopSilk;
    case CamLayerKind::BottomSilk: return BoardLayer::BottomSilk;
    case CamLayerKind::TopMask: return BoardLayer::TopResist;
    case CamLayerKind::BottomMask: return BoardLayer::BottomResist;
    case CamLayerKind::TopPaste: return BoardLayer::TopPaste;
    case CamLayerKind::BottomPaste: return BoardLayer::BottomPaste;
    case CamLayerKind::Outline: return BoardLayer::BoardEdge;
    }
    return BoardLayer::TopSilk;
}

} // namespace

QSizeF paperSize(PaperPreset preset) {
    switch (preset) {
    case PaperPreset::A3: return {297.0, 420.0};
    case PaperPreset::A4: return {210.0, 297.0};
    case PaperPreset::A5: return {148.0, 210.0};
    case PaperPreset::Letter: return {215.9, 279.4};
    case PaperPreset::Custom: return {210.0, 297.0};
    }
    return {210.0, 297.0};
}

QString paperName(PaperPreset preset) {
    switch (preset) {
    case PaperPreset::A3: return QStringLiteral("A3");
    case PaperPreset::A4: return QStringLiteral("A4");
    case PaperPreset::A5: return QStringLiteral("A5");
    case PaperPreset::Letter: return QStringLiteral("Letter");
    case PaperPreset::Custom: return QCoreApplication::translate("hatt::ui::PrintLayout", "Custom");
    }
    return {};
}

QSizeF pageSize(const PrintSettings& settings) {
    const QSizeF paper = settings.paper;
    const bool wide = paper.width() > paper.height();
    return settings.landscape != wide ? QSizeF(paper.height(), paper.width()) : paper;
}

QRectF artworkBounds(const CamOutput& output, int layers) {
    // Points are tracked by hand: QRectF::united ignores zero-size rectangles.
    double left = 0, top = 0, right = 0, bottom = 0;
    bool any = false;
    auto include = [&](QPointF a, QPointF b) {
        if (!any) {
            left = std::min(a.x(), b.x());
            right = std::max(a.x(), b.x());
            top = std::min(a.y(), b.y());
            bottom = std::max(a.y(), b.y());
            any = true;
            return;
        }
        left = std::min({left, a.x(), b.x()});
        right = std::max({right, a.x(), b.x()});
        top = std::min({top, a.y(), b.y()});
        bottom = std::max({bottom, a.y(), b.y()});
    };
    const CamLayer outline = output.layers.value(static_cast<int>(CamLayerKind::Outline));
    // The board outline decides the artwork size, so every copy lines up with its neighbours.
    for (const CamPrimitive& primitive : outline.primitives) {
        for (const QPointF& point : primitive.points) include(point, point);
    }
    if (!any) {
        for (CamLayerKind kind : selectedLayers(layers)) {
            const QRectF shape = layerShape(output.layers.value(static_cast<int>(kind))).boundingRect();
            if (shape.width() > 0 || shape.height() > 0) include(shape.topLeft(), shape.bottomRight());
        }
    }
    if (!any || (right - left <= 0 && bottom - top <= 0)) return {};
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

QVector<PrintTile> layoutTiles(const PrintSettings& settings, QSizeF artwork, int* dropped) {
    QVector<PrintTile> tiles;
    if (dropped != nullptr) *dropped = 0;
    const QVector<int> groups = copyGroups(settings);
    if (groups.isEmpty() || artwork.isEmpty()) return tiles;
    const QSizeF page = pageSize(settings);
    const QRectF printable(settings.margin, settings.margin, page.width() - 2 * settings.margin,
                           page.height() - 2 * settings.margin);
    const QSizeF tile = tileSize(settings, artwork);
    const double copyWidth = groups.size() * tile.width() + (groups.size() - 1) * settings.spacing;
    for (int row = 0; row < std::max(0, settings.rows); ++row) {
        for (int column = 0; column < std::max(0, settings.columns); ++column) {
            const double x = printable.left() + column * (copyWidth + settings.spacing);
            const double y = printable.top() + row * (tile.height() + settings.spacing);
            for (qsizetype group = 0; group < groups.size(); ++group) {
                const QRectF box(x + group * (tile.width() + settings.spacing), y, tile.width(), tile.height());
                if (box.right() > printable.right() + 1e-6 || box.bottom() > printable.bottom() + 1e-6) {
                    if (dropped != nullptr) ++*dropped;
                    continue;
                }
                const int mask = groups[group];
                const auto layers = selectedLayers(mask & ~OutlineBit);
                tiles.append({box, layers.isEmpty() ? CamLayerKind::Outline : layers.first(), mask});
            }
        }
    }
    return tiles;
}

PrintFit fitCopies(const PrintSettings& settings, QSizeF artwork, bool allowRotation) {
    PrintFit best;
    const int groups = static_cast<int>(copyGroups(settings).size());
    if (groups == 0 || artwork.isEmpty()) return best;
    const QSizeF page = pageSize(settings);
    const double width = page.width() - 2 * settings.margin;
    const double height = page.height() - 2 * settings.margin;
    for (bool rotate : {false, true}) {
        if (rotate && !allowRotation) continue;
        PrintSettings candidate = settings;
        candidate.rotate = rotate;
        const QSizeF tile = tileSize(candidate, artwork);
        const double copyWidth = groups * tile.width() + (groups - 1) * settings.spacing;
        const int columns = static_cast<int>(std::floor((width + settings.spacing + 1e-9) / (copyWidth + settings.spacing)));
        const int rows = static_cast<int>(std::floor((height + settings.spacing + 1e-9) / (tile.height() + settings.spacing)));
        if (columns > 0 && rows > 0 && columns * rows > best.columns * best.rows) best = {columns, rows, rotate};
    }
    return best;
}

QPainterPath layerShape(const CamLayer& layer) {
    QPainterPath shape;
    shape.setFillRule(Qt::WindingFill);
    for (const CamPrimitive& primitive : layer.primitives) {
        switch (primitive.kind) {
        case CamPrimitive::Kind::Flash:
            shape.addPath(flashPath(primitive.aperture, primitive.points.value(0)));
            break;
        case CamPrimitive::Kind::Stroke: {
            if (primitive.points.isEmpty()) break;
            QPainterPath line;
            line.moveTo(primitive.points.first());
            for (qsizetype i = 1; i < primitive.points.size(); ++i) line.lineTo(primitive.points[i]);
            if (primitive.points.size() == 1) line.lineTo(primitive.points.first() + QPointF(1e-6, 0));
            QPainterPathStroker stroker;
            stroker.setWidth(primitive.aperture.width);
            stroker.setCapStyle(Qt::RoundCap);
            stroker.setJoinStyle(Qt::RoundJoin);
            shape.addPath(stroker.createStroke(line));
            break;
        }
        case CamPrimitive::Kind::Region: {
            QPainterPath region;
            region.addPolygon(QPolygonF(primitive.points));
            region.closeSubpath();
            if (primitive.clear) {
                shape = shape.simplified().subtracted(region);
                shape.setFillRule(Qt::WindingFill);
            } else {
                shape.addPath(region);
            }
            break;
        }
        }
    }
    return shape;
}

void paintPrintPage(QPainter& painter, const CamOutput& output, const PrintSettings& settings, double dotsPerMm,
                    bool previewFrame) {
    const QSizeF page = pageSize(settings);
    // Millimetres on top of whatever the caller already set up (e.g. the preview's centring).
    const QTransform toDevice = QTransform::fromScale(dotsPerMm, dotsPerMm) * painter.transform();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setTransform(toDevice);
    painter.fillRect(QRectF(QPointF(0, 0), page), Qt::white);
    if (previewFrame) {
        QPen margin(QColor(QStringLiteral("#8a96a3")), 0.3, Qt::DashLine);
        painter.setPen(margin);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(settings.margin, settings.margin, page.width() - 2 * settings.margin,
                                page.height() - 2 * settings.margin));
    }

    const QRectF bounds = artworkBounds(output, settings.layers);
    if (bounds.isNull()) {
        painter.restore();
        return;
    }
    QVector<QPainterPath> shapes(static_cast<int>(CamLayerKind::Outline) + 1);
    for (CamLayerKind kind : selectedLayers(settings.layers)) {
        shapes[static_cast<int>(kind)] = layerShape(output.layers.value(static_cast<int>(kind)));
    }
    QPainterPath holes;
    for (const CamDrillHit& hit : output.drills) holes.addEllipse(hit.at, hit.diameter / 2.0, hit.diameter / 2.0);

    const double width = bounds.width();
    const double height = bounds.height();
    for (const PrintTile& tile : layoutTiles(settings, bounds.size())) {
        // CAM (Y up) → artwork box: Y flipped, optional mirror and quarter turn, scale and
        // printer compensation, then placed on the page.
        QTransform artwork = QTransform::fromTranslate(-bounds.left(), -bounds.bottom()) * QTransform::fromScale(1, -1);
        if (settings.mirror) artwork *= QTransform(-1, 0, 0, 1, width, 0);
        if (settings.rotate) artwork *= QTransform(0, 1, -1, 0, height, 0);
        artwork *= QTransform::fromScale(settings.scale * settings.compensationX, settings.scale * settings.compensationY);
        artwork *= QTransform::fromTranslate(tile.page.left(), tile.page.top());

        painter.setTransform(toDevice);
        const bool negative = settings.colours == PrintColours::Negative;
        if (negative) painter.fillRect(tile.page, Qt::black);
        painter.setTransform(artwork * toDevice);
        painter.setPen(Qt::NoPen);
        // Bottom layers first so top copper and silk stay readable in board colours.
        const CamLayerKind order[] = {CamLayerKind::BottomPaste, CamLayerKind::BottomMask, CamLayerKind::BottomCopper,
                                      CamLayerKind::BottomSilk,  CamLayerKind::TopPaste,   CamLayerKind::TopMask,
                                      CamLayerKind::TopCopper,   CamLayerKind::TopSilk,    CamLayerKind::Outline};
        for (CamLayerKind kind : order) {
            if ((tile.layers & (1 << static_cast<int>(kind))) == 0) continue;
            QColor colour = negative ? Qt::white : Qt::black;
            if (settings.colours == PrintColours::Board) colour = boardLayerColor(boardLayerOf(kind), false);
            painter.fillPath(shapes[static_cast<int>(kind)], colour);
        }
        if (settings.drills && (tile.layers & ~OutlineBit) != 0) {
            painter.fillPath(holes, negative ? Qt::black : Qt::white);
        }
    }
    painter.restore();
}

} // namespace hatt::ui
