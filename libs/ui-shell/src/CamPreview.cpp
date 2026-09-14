#include "hatt/ui/CamPreview.hpp"

#include "hatt/ui/LayerColors.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace hatt::ui {
namespace {

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

// Bottom layers first, then top, so the top side stays readable.
constexpr CamLayerKind DrawOrder[] = {CamLayerKind::BottomPaste, CamLayerKind::BottomMask, CamLayerKind::BottomCopper,
                                      CamLayerKind::BottomSilk,  CamLayerKind::TopPaste,   CamLayerKind::TopMask,
                                      CamLayerKind::TopCopper,   CamLayerKind::TopSilk,    CamLayerKind::Outline};

QRectF apertureRect(const CamAperture& aperture, QPointF center) {
    const double height = aperture.shape == CamApertureShape::Circle ? aperture.width : aperture.height;
    return {center.x() - aperture.width / 2.0, center.y() - height / 2.0, aperture.width, height};
}

} // namespace

CamPreview::CamPreview(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("CamPreview"));
    setMinimumSize(240, 180);
    setMouseTracking(false);
}

void CamPreview::setOutput(const CamOutput& output) {
    output_ = output;
    bounds_ = {};
    auto include = [this](const QRectF& rect) { bounds_ = bounds_.isNull() ? rect : bounds_.united(rect); };
    for (const CamLayer& layer : output_.layers) {
        for (const CamPrimitive& primitive : layer.primitives) {
            const double margin = primitive.kind == CamPrimitive::Kind::Region ? 0.0 : primitive.aperture.width / 2.0;
            for (const QPointF& point : primitive.points) {
                include(QRectF(point - QPointF(margin, margin), QSizeF(2 * margin, 2 * margin)));
            }
        }
    }
    for (const CamDrillHit& hit : output_.drills) {
        include(QRectF(hit.at - QPointF(hit.diameter, hit.diameter) / 2.0, QSizeF(hit.diameter, hit.diameter)));
    }
    fitToOutput();
}

bool CamPreview::isLayerVisible(CamLayerKind kind) const {
    return (hiddenLayers_ & (1 << static_cast<int>(kind))) == 0;
}

void CamPreview::setLayerVisible(CamLayerKind kind, bool visible) {
    const int bit = 1 << static_cast<int>(kind);
    hiddenLayers_ = visible ? hiddenLayers_ & ~bit : hiddenLayers_ | bit;
    update();
}

void CamPreview::setDrillsVisible(bool visible) {
    drillsVisible_ = visible;
    update();
}

void CamPreview::fitToOutput() {
    const QRectF area = QRectF(rect()).adjusted(16, 16, -16, -16);
    if (bounds_.isNull() || area.width() <= 0 || area.height() <= 0) {
        scale_ = 10.0;
        offset_ = QRectF(rect()).center();
        update();
        return;
    }
    const double width = std::max(bounds_.width(), 1.0);
    const double height = std::max(bounds_.height(), 1.0);
    scale_ = std::min(area.width() / width, area.height() / height);
    // CAM Y points up: the bounds centre maps to the widget centre with Y flipped.
    offset_ = area.center() - QPointF(bounds_.center().x() * scale_, -bounds_.center().y() * scale_);
    update();
}

QPointF CamPreview::toScreen(QPointF cam) const { return offset_ + QPointF(cam.x() * scale_, -cam.y() * scale_); }

void CamPreview::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    painter.fillRect(rect(), dark ? QColor(QStringLiteral("#080b0f")) : QColor(QStringLiteral("#f4f6f7")));

    // World transform: millimetres with Y up.
    painter.translate(offset_);
    painter.scale(scale_, -scale_);
    for (CamLayerKind kind : DrawOrder) {
        if (!isLayerVisible(kind)) continue;
        QColor color = boardLayerColor(boardLayerOf(kind), dark);
        color.setAlpha(kind == CamLayerKind::TopMask || kind == CamLayerKind::BottomMask ||
                               kind == CamLayerKind::TopPaste || kind == CamLayerKind::BottomPaste
                           ? 110
                           : 210);
        for (const CamPrimitive& primitive : output_.layers.value(static_cast<int>(kind)).primitives) {
            switch (primitive.kind) {
            case CamPrimitive::Kind::Flash: {
                const QRectF shape = apertureRect(primitive.aperture, primitive.points.value(0));
                painter.setPen(Qt::NoPen);
                painter.setBrush(color);
                if (primitive.aperture.shape == CamApertureShape::Rectangle) {
                    painter.drawRect(shape);
                } else {
                    const double radius = std::min(shape.width(), shape.height()) / 2.0;
                    painter.drawRoundedRect(shape, radius, radius);
                }
                break;
            }
            case CamPrimitive::Kind::Stroke:
                painter.setPen(QPen(color, primitive.aperture.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(Qt::NoBrush);
                painter.drawPolyline(QPolygonF(primitive.points));
                break;
            case CamPrimitive::Kind::Region:
                painter.setPen(Qt::NoPen);
                painter.setBrush(color);
                painter.drawPolygon(QPolygonF(primitive.points));
                break;
            }
        }
    }
    if (drillsVisible_) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(dark ? QColor(QStringLiteral("#080b0f")) : QColor(QStringLiteral("#ffffff")));
        for (const CamDrillHit& hit : output_.drills) painter.drawEllipse(hit.at, hit.diameter / 2.0, hit.diameter / 2.0);
    }
    painter.resetTransform();
    if (bounds_.isNull()) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, tr("Nothing to fabricate on this board yet"));
    }
}

void CamPreview::wheelEvent(QWheelEvent* event) {
    const double factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    const QPointF cursor = event->position();
    const double next = std::clamp(scale_ * factor, 0.5, 2000.0);
    offset_ = cursor - (cursor - offset_) * (next / scale_);
    scale_ = next;
    update();
    event->accept();
}

void CamPreview::mousePressEvent(QMouseEvent* event) {
    lastMouse_ = event->position();
    if (event->button() == Qt::RightButton || event->type() == QEvent::MouseButtonDblClick) fitToOutput();
}

void CamPreview::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & (Qt::LeftButton | Qt::MiddleButton)) {
        offset_ += event->position() - lastMouse_;
        lastMouse_ = event->position();
        update();
    }
}

void CamPreview::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    fitToOutput();
}

} // namespace hatt::ui
