#include "hatt/ui/DesignCanvas.hpp"

#include <QFont>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QUndoCommand>
#include <QUndoStack>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

namespace hatt::ui {
namespace {

constexpr double Pi = 3.14159265358979323846;

using WorldToScreen = std::function<QPointF(QPointF)>;

class DocumentEditCommand final : public QUndoCommand {
public:
    DocumentEditCommand(DesignCanvas* canvas, const QString& text, SketchDocument beforeDocument,
                        QList<int> beforeSelection, SketchDocument afterDocument,
                        QList<int> afterSelection)
        : canvas_(canvas), before_(std::move(beforeDocument)),
          beforeSelection_(std::move(beforeSelection)), after_(std::move(afterDocument)),
          afterSelection_(std::move(afterSelection)) {
        setText(text);
    }

    void redo() override { canvas_->restore(after_, afterSelection_); }
    void undo() override { canvas_->restore(before_, beforeSelection_); }

private:
    DesignCanvas* canvas_;
    SketchDocument before_;
    QList<int> beforeSelection_;
    SketchDocument after_;
    QList<int> afterSelection_;
};

struct CanvasColors {
    QColor background;
    QColor gridMinor;
    QColor gridMajor;
    QColor origin;
    QColor stroke;
    QColor wire;
    QColor copper;
    QColor graphics;
    QColor outline;
    QColor selection;
    QColor preview;
    QColor label;
};

CanvasColors canvasColors(Workspace workspace, const QPalette& palette) {
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    const bool board = workspace == Workspace::Board;
    CanvasColors colors;
    if (dark) {
        colors.background = QColor(board ? "#080b0f" : "#0b1016");
        colors.gridMinor = QColor(board ? "#10171e" : "#131b23");
        colors.gridMajor = QColor(board ? "#1c2630" : "#202b36");
        colors.origin = QColor("#2f6f68");
        colors.stroke = QColor(board ? "#d9e1e8" : "#c9d4de");
        colors.wire = QColor(board ? "#f4a261" : "#8fd694");
        colors.copper = QColor("#f4a261");
        colors.graphics = QColor(board ? "#62b6ff" : "#c9d4de");
        colors.outline = QColor("#e9c46a");
        colors.selection = QColor("#18b6a4");
        colors.preview = QColor("#ffb45d");
        colors.label = QColor("#8fa0ae");
    } else {
        colors.background = QColor(board ? "#f4f6f7" : "#fbfbf8");
        colors.gridMinor = QColor(board ? "#e5e9ec" : "#eaece5");
        colors.gridMajor = QColor(board ? "#ced5db" : "#d4d8cd");
        colors.origin = QColor("#7fb8b1");
        colors.stroke = QColor(board ? "#3a4652" : "#2d3a45");
        colors.wire = QColor(board ? "#c96f28" : "#2e7d32");
        colors.copper = QColor("#d9822b");
        colors.graphics = QColor(board ? "#1f78c1" : "#2d3a45");
        colors.outline = QColor("#b8860b");
        colors.selection = QColor("#087f73");
        colors.preview = QColor("#c05f00");
        colors.label = QColor("#5e6b76");
    }
    return colors;
}

QPen strokePen(const QColor& color, double width, bool dashed = false) {
    QPen pen(color, width, dashed ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    return pen;
}

void drawSymbol(QPainter& painter, const SymbolDefinition& symbol, const WorldToScreen& map,
                const CanvasColors& colors, const QColor& stroke, double width, bool selected,
                bool preview) {
    for (const auto& shape : symbol.shapes) {
        QPolygonF polygon;
        polygon.reserve(shape.points.size());
        for (const QPointF& point : shape.points) {
            polygon << map(point);
        }
        if (shape.copper) {
            QColor copper = colors.copper;
            if (preview) {
                copper.setAlpha(110);
            }
            painter.setPen(selected ? strokePen(colors.selection, 1.5) : QPen(Qt::NoPen));
            painter.setBrush(copper);
        } else if (shape.hole) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.background);
        } else {
            painter.setPen(strokePen(stroke, width, preview));
            painter.setBrush(shape.filled ? QBrush(stroke) : QBrush(Qt::NoBrush));
        }
        if (shape.closed) {
            painter.drawPolygon(polygon);
        } else {
            painter.drawPolyline(polygon);
        }
    }
    painter.setBrush(Qt::NoBrush);
}

void drawItem(QPainter& painter, const SketchItem& item, const CanvasColors& colors, bool board,
              bool selected, bool preview, double scale, const WorldToScreen& map) {
    if (item.points.isEmpty()) {
        return;
    }
    auto pick = [&](const QColor& normal) {
        return preview ? colors.preview : selected ? colors.selection : normal;
    };
    const double width = selected ? 2.4 : 1.5;
    QPolygonF polygon;
    for (const QPointF& point : item.points) {
        polygon << map(point);
    }

    switch (item.kind) {
    case SketchItem::Kind::Symbol: {
        const auto* symbol = findSymbol(item.variant);
        if (symbol == nullptr) {
            break;
        }
        drawSymbol(painter, *symbol,
                   [&](QPointF local) { return map(symbolToWorld(item, local)); }, colors,
                   pick(colors.stroke), width, selected, preview);
        if (!item.label.isEmpty() && !preview) {
            const QRectF bounds = itemBounds(item);
            const QPointF topCenter = map(QPointF(bounds.center().x(), bounds.top()));
            QFont font = painter.font();
            font.setPixelSize(std::clamp(static_cast<int>(1.6 * scale), 9, 26));
            painter.setFont(font);
            painter.setPen(selected ? colors.selection : colors.label);
            painter.drawText(QRectF(topCenter.x() - 80, topCenter.y() - font.pixelSize() - 6, 160,
                                    font.pixelSize() + 4),
                             Qt::AlignHCenter | Qt::AlignBottom, item.label);
        }
        break;
    }
    case SketchItem::Kind::Wire:
        painter.setPen(strokePen(pick(colors.wire),
                                 board ? std::max(2.0, 0.3 * scale) : (selected ? 3.0 : 2.0),
                                 preview));
        painter.drawPolyline(polygon);
        break;
    case SketchItem::Kind::Line:
    case SketchItem::Kind::Polyline: {
        const bool outline = item.variant == BoardOutlineVariant;
        const bool zone = item.variant == CopperZoneVariant;
        const QColor color = pick(outline ? colors.outline : zone ? colors.copper : colors.graphics);
        painter.setPen(strokePen(color, outline ? std::max(width, 2.0) : width, preview));
        if (zone) {
            QColor fill = colors.copper;
            fill.setAlpha(preview ? 40 : 70);
            painter.setBrush(fill);
        }
        if (item.closed && polygon.size() > 2) {
            painter.drawPolygon(polygon);
        } else {
            painter.drawPolyline(polygon);
        }
        painter.setBrush(Qt::NoBrush);
        break;
    }
    case SketchItem::Kind::Rectangle:
        painter.setPen(strokePen(pick(colors.graphics), width, preview));
        painter.drawRect(QRectF(map(item.points.value(0)), map(item.points.value(1))).normalized());
        break;
    case SketchItem::Kind::Circle: {
        const double radius = QLineF(item.points.value(0), item.points.value(1)).length() * scale;
        painter.setPen(strokePen(pick(colors.graphics), width, preview));
        painter.drawEllipse(map(item.points.value(0)), radius, radius);
        break;
    }
    case SketchItem::Kind::Arc: {
        QPolygonF samples;
        for (const QPointF& point :
             arcSamples(item.points.value(0), item.points.value(1), item.points.value(2))) {
            samples << map(point);
        }
        painter.setPen(strokePen(pick(colors.graphics), width, preview));
        painter.drawPolyline(samples);
        break;
    }
    case SketchItem::Kind::Text: {
        QFont font = painter.font();
        font.setPixelSize(std::max(6, static_cast<int>(TextHeightMm * scale * 0.8)));
        painter.setFont(font);
        painter.setPen(pick(colors.graphics));
        painter.drawText(QRectF(map(item.points.first()), QSizeF(4000, TextHeightMm * scale)),
                         Qt::AlignLeft | Qt::AlignVCenter, item.label);
        break;
    }
    }

    if (selected && !preview) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.selection);
        const auto anchors = itemAnchors(item);
        for (qsizetype i = 0; i < std::min<qsizetype>(anchors.size(), 24); ++i) {
            painter.drawRect(QRectF(map(anchors[i]) - QPointF(3, 3), QSizeF(6, 6)));
        }
        painter.setBrush(Qt::NoBrush);
    }
}

QString measurementText(const QLineF& line) {
    const double dx = line.dx();
    const double dy = -line.dy();
    const double angle = std::atan2(dy, dx) * 180.0 / Pi;
    return DesignCanvas::tr("Distance %1 mm  ·  ΔX %2 mm  ·  ΔY %3 mm  ·  %4°")
        .arg(line.length(), 0, 'f', 3)
        .arg(dx, 0, 'f', 3)
        .arg(dy, 0, 'f', 3)
        .arg(angle, 0, 'f', 1);
}

bool samePoint(QPointF a, QPointF b) { return QLineF(a, b).length() < 1e-6; }

bool isTwoPointTool(CanvasTool tool) {
    return tool == CanvasTool::Line || tool == CanvasTool::Rectangle ||
           tool == CanvasTool::Circle || tool == CanvasTool::Measure;
}

bool isPathTool(CanvasTool tool) { return tool == CanvasTool::Wire || tool == CanvasTool::Polyline; }

} // namespace

DesignCanvas::DesignCanvas(Workspace workspace, QWidget* parent)
    : QWidget(parent), workspace_(workspace), undoStack_(new QUndoStack(this)),
      scale_(defaultScale()) {
    setObjectName(QStringLiteral("DesignCanvas"));
    setMinimumSize(480, 320);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::ArrowCursor);
}

DesignCanvas::~DesignCanvas() = default;

double DesignCanvas::defaultScale() const noexcept {
    return workspace_ == Workspace::Board ? 24.0 : 8.0;
}

double DesignCanvas::gridSize() const noexcept {
    return workspace_ == Workspace::Board ? 0.635 : 2.54;
}

int DesignCanvas::zoomPercent() const noexcept {
    return static_cast<int>(std::lround(scale_ / defaultScale() * 100.0));
}

bool DesignCanvas::hasPendingOperation() const noexcept {
    return !pending_.isEmpty() || drag_ == Drag::Move || drag_ == Drag::RubberBand;
}

QPointF DesignCanvas::worldToScreen(QPointF world) const { return world * scale_ + offset_; }

QPointF DesignCanvas::screenToWorld(QPointF screen) const { return (screen - offset_) / scale_; }

QString DesignCanvas::toolHint() const {
    switch (tool_) {
    case CanvasTool::Select:
        return tr("Click to select and drag to move. Drag on empty space for box selection; Shift "
                  "or Ctrl adds to the selection. Arrow keys nudge, Ctrl+R rotates, Delete removes.");
    case CanvasTool::Symbol:
        if (const auto* symbol = findSymbol(variant_)) {
            return tr("Click to place %1. Ctrl+R rotates before placing. Esc returns to selection.")
                .arg(symbolDisplayName(*symbol));
        }
        return tr("Choose an object from the list.");
    case CanvasTool::Wire:
        return workspace_ == Workspace::Board
                   ? tr("Click to start a track and click to add corners. The track ends on a pad "
                        "automatically; double-click, Enter or right-click also finishes it.")
                   : tr("Click to start a wire and click to add corners. The wire ends on a pin "
                        "automatically; double-click, Enter or right-click also finishes it.");
    case CanvasTool::Line:
    case CanvasTool::Rectangle:
        return tr("Drag, or click the start point and then the end point.");
    case CanvasTool::Circle:
        return tr("Drag from the centre, or click the centre and then a point on the circle.");
    case CanvasTool::Polyline:
        return tr("Click to add vertices. Double-click, Enter or right-click finishes; Backspace "
                  "removes the last vertex.");
    case CanvasTool::Arc:
        return tr("Click the start point, the end point, then a point the arc passes through.");
    case CanvasTool::Text:
        return tr("Click to place a text label.");
    case CanvasTool::Measure:
        return tr("Drag, or click two points, to measure. Measurements are not added to the design.");
    }
    return {};
}

void DesignCanvas::setTool(CanvasTool tool, const QString& variant) {
    cancelOperation();
    tool_ = tool;
    variant_ = variant;
    placementTurns_ = 0;
    hasMeasurement_ = false;
    if (tool_ != CanvasTool::Select) {
        clearSelection();
    }
    setCursor(tool_ == CanvasTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    emit statusMessage(toolHint());
    update();
}

void DesignCanvas::setSnapSettings(const SnapSettings& settings) {
    snap_ = settings;
    update();
}

void DesignCanvas::cancelOperation() {
    pending_.clear();
    pressGesture_ = false;
    if (drag_ == Drag::Move || drag_ == Drag::RubberBand) {
        drag_ = Drag::None;
    }
    moveDelta_ = {};
    update();
}

void DesignCanvas::setSelection(QList<int> selection) {
    std::sort(selection.begin(), selection.end());
    selection.erase(std::unique(selection.begin(), selection.end()), selection.end());
    selection.removeIf([this](int index) { return index < 0 || index >= items_.size(); });
    if (selection == selection_) {
        return;
    }
    selection_ = selection;
    emit selectionChanged(static_cast<int>(selection_.size()));
    update();
}

void DesignCanvas::selectAll() {
    QList<int> all;
    for (int i = 0; i < items_.size(); ++i) {
        all.append(i);
    }
    setSelection(all);
}

void DesignCanvas::clearSelection() { setSelection({}); }

void DesignCanvas::restore(const SketchDocument& document, const QList<int>& selection) {
    pending_.clear();
    pressGesture_ = false;
    if (drag_ != Drag::Pan) {
        drag_ = Drag::None;
    }
    moveDelta_ = {};
    items_ = document;
    selection_ = selection;
    selection_.removeIf([this](int index) { return index < 0 || index >= items_.size(); });
    emit selectionChanged(static_cast<int>(selection_.size()));
    update();
}

void DesignCanvas::pushEdit(const QString& text, const SketchDocument& document,
                            const QList<int>& selection) {
    undoStack_->push(new DocumentEditCommand(this, text, items_, selection_, document, selection));
}

void DesignCanvas::deleteSelection() {
    if (selection_.isEmpty()) {
        return;
    }
    SketchDocument document = items_;
    QList<int> ordered = selection_;
    std::sort(ordered.begin(), ordered.end(), std::greater<>());
    for (int index : ordered) {
        document.removeAt(index);
    }
    pushEdit(tr("Delete"), document, {});
}

void DesignCanvas::duplicateSelection() {
    if (selection_.isEmpty()) {
        return;
    }
    SketchDocument document = items_;
    QList<int> copies;
    const QPointF offset(gridSize() * 2, gridSize() * 2);
    for (int index : selection_) {
        SketchItem copy = items_[index];
        translateItem(copy, offset);
        if (copy.kind == SketchItem::Kind::Symbol) {
            const auto* symbol = findSymbol(copy.variant);
            if (symbol != nullptr && !symbol->prefix.isEmpty()) {
                copy.label = nextDesignator(document, symbol->prefix);
            }
        }
        document.append(copy);
        copies.append(static_cast<int>(document.size() - 1));
    }
    pushEdit(tr("Duplicate"), document, copies);
}

void DesignCanvas::rotateSelection() {
    if (selection_.isEmpty()) {
        if (tool_ == CanvasTool::Symbol) {
            placementTurns_ = (placementTurns_ + 1) % 4;
            update();
        }
        return;
    }
    QRectF bounds = itemBounds(items_[selection_.first()]);
    for (int index : selection_) {
        bounds = bounds.united(itemBounds(items_[index]));
    }
    const QPointF pivot = snap_.grid ? snapToGrid(bounds.center()) : bounds.center();
    SketchDocument document = items_;
    for (int index : selection_) {
        rotateItemQuarterTurn(document[index], pivot);
    }
    pushEdit(tr("Rotate"), document, selection_);
}

void DesignCanvas::nudgeSelection(QPointF delta) {
    if (selection_.isEmpty() || delta.isNull()) {
        return;
    }
    SketchDocument document = items_;
    for (int index : selection_) {
        translateItem(document[index], delta);
    }
    pushEdit(tr("Move"), document, selection_);
}

void DesignCanvas::align(AlignOperation operation) {
    const bool distribute = operation == AlignOperation::DistributeHorizontally ||
                            operation == AlignOperation::DistributeVertically;
    if (selection_.size() < (distribute ? 3 : 2)) {
        return;
    }
    QVector<QRectF> bounds;
    QRectF all;
    for (int index : selection_) {
        const QRectF itemRect = itemBounds(items_[index]);
        bounds.append(itemRect);
        all = bounds.size() == 1 ? itemRect : all.united(itemRect);
    }

    QVector<QPointF> deltas(bounds.size());
    switch (operation) {
    case AlignOperation::Left:
        for (qsizetype i = 0; i < bounds.size(); ++i) deltas[i] = {all.left() - bounds[i].left(), 0};
        break;
    case AlignOperation::HorizontalCenter:
        for (qsizetype i = 0; i < bounds.size(); ++i)
            deltas[i] = {all.center().x() - bounds[i].center().x(), 0};
        break;
    case AlignOperation::Right:
        for (qsizetype i = 0; i < bounds.size(); ++i) deltas[i] = {all.right() - bounds[i].right(), 0};
        break;
    case AlignOperation::Top:
        for (qsizetype i = 0; i < bounds.size(); ++i) deltas[i] = {0, all.top() - bounds[i].top()};
        break;
    case AlignOperation::VerticalCenter:
        for (qsizetype i = 0; i < bounds.size(); ++i)
            deltas[i] = {0, all.center().y() - bounds[i].center().y()};
        break;
    case AlignOperation::Bottom:
        for (qsizetype i = 0; i < bounds.size(); ++i) deltas[i] = {0, all.bottom() - bounds[i].bottom()};
        break;
    case AlignOperation::DistributeHorizontally:
    case AlignOperation::DistributeVertically: {
        const bool horizontal = operation == AlignOperation::DistributeHorizontally;
        QVector<qsizetype> order(bounds.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) {
            return horizontal ? bounds[a].left() < bounds[b].left() : bounds[a].top() < bounds[b].top();
        });
        double occupied = 0;
        for (const QRectF& rect : bounds) {
            occupied += horizontal ? rect.width() : rect.height();
        }
        const double span = horizontal ? all.width() : all.height();
        const double gap = (span - occupied) / static_cast<double>(bounds.size() - 1);
        double cursor = horizontal ? all.left() : all.top();
        for (qsizetype index : order) {
            const QRectF& rect = bounds[index];
            deltas[index] = horizontal ? QPointF(cursor - rect.left(), 0) : QPointF(0, cursor - rect.top());
            cursor += (horizontal ? rect.width() : rect.height()) + gap;
        }
        break;
    }
    }

    SketchDocument document = items_;
    bool moved = false;
    for (qsizetype i = 0; i < selection_.size(); ++i) {
        if (QLineF(QPointF(), deltas[i]).length() > 1e-9) {
            translateItem(document[selection_[i]], deltas[i]);
            moved = true;
        }
    }
    if (!moved) {
        emit statusMessage(tr("The selected objects are already aligned."));
        return;
    }
    pushEdit(distribute ? tr("Distribute") : tr("Align"), document, selection_);
}

void DesignCanvas::zoomAround(QPointF screen, double factor) {
    const QPointF world = screenToWorld(screen);
    scale_ = std::clamp(scale_ * factor, defaultScale() * 0.1, defaultScale() * 20.0);
    offset_ = screen - world * scale_;
    emit zoomChanged(zoomPercent());
    update();
}

void DesignCanvas::zoomIn() { zoomAround(QRectF(rect()).center(), 1.25); }

void DesignCanvas::zoomOut() { zoomAround(QRectF(rect()).center(), 1.0 / 1.25); }

void DesignCanvas::zoomToFit() {
    if (items_.isEmpty()) {
        scale_ = defaultScale();
        offset_ = QPointF(48.0, 48.0);
    } else {
        QRectF bounds = itemBounds(items_.first());
        for (const auto& item : items_) {
            bounds = bounds.united(itemBounds(item));
        }
        bounds.adjust(-gridSize() * 2, -gridSize() * 2, gridSize() * 2, gridSize() * 2);
        const double fit = std::min((width() - 40.0) / std::max(bounds.width(), 1e-3),
                                    (height() - 40.0) / std::max(bounds.height(), 1e-3));
        scale_ = std::clamp(fit, defaultScale() * 0.1, defaultScale() * 20.0);
        offset_ = QPointF(width() / 2.0, height() / 2.0) - bounds.center() * scale_;
    }
    emit zoomChanged(zoomPercent());
    update();
}

QPointF DesignCanvas::snapToGrid(QPointF world) const {
    const double grid = gridSize();
    return {std::round(world.x() / grid) * grid, std::round(world.y() / grid) * grid};
}

QPointF DesignCanvas::constrainAngle(QPointF point, QPointF origin) const {
    if (!snap_.orthogonal && !snap_.diagonal) {
        return point;
    }
    const QPointF delta = point - origin;
    const double ax = std::abs(delta.x());
    const double ay = std::abs(delta.y());
    if (ax < 1e-9 && ay < 1e-9) {
        return point;
    }
    if (snap_.orthogonal) {
        return ax >= ay ? origin + QPointF(delta.x(), 0) : origin + QPointF(0, delta.y());
    }
    const double angle = std::atan2(ay, ax);
    if (angle < Pi / 8) {
        return origin + QPointF(delta.x(), 0);
    }
    if (angle > 3 * Pi / 8) {
        return origin + QPointF(0, delta.y());
    }
    double magnitude = (ax + ay) / 2;
    if (snap_.grid) {
        magnitude = std::max(gridSize(), std::round(magnitude / gridSize()) * gridSize());
    }
    return origin + QPointF(std::copysign(magnitude, delta.x()), std::copysign(magnitude, delta.y()));
}

const QPointF* DesignCanvas::constraintOrigin() const {
    if ((isPathTool(tool_) || tool_ == CanvasTool::Line || tool_ == CanvasTool::Measure) &&
        !pending_.isEmpty()) {
        return &pending_.last();
    }
    return nullptr;
}

DesignCanvas::Snap DesignCanvas::snap(QPointF screen, const QPointF* origin,
                                      bool ignoreSelection) const {
    const QPointF world = screenToWorld(screen);
    const double tolerance = 10.0 / scale_;
    Snap result{world, SnapKind::None};
    double best = tolerance;
    auto skipped = [&](int index) { return ignoreSelection && selection_.contains(index); };

    if (snap_.objects) {
        for (int i = 0; i < items_.size(); ++i) {
            if (skipped(i)) continue;
            for (const QPointF& anchor : itemAnchors(items_[i])) {
                const double distance = QLineF(world, anchor).length();
                if (distance < best) {
                    best = distance;
                    result = {anchor, SnapKind::Object};
                }
            }
        }
        if (tool_ == CanvasTool::Polyline && pending_.size() >= 3) {
            const double distance = QLineF(world, pending_.first()).length();
            if (distance < best) {
                best = distance;
                result = {pending_.first(), SnapKind::Object};
            }
        }
    }
    if (result.kind == SnapKind::None && snap_.centers) {
        for (int i = 0; i < items_.size(); ++i) {
            if (skipped(i)) continue;
            const QPointF center = items_[i].kind == SketchItem::Kind::Circle
                                       ? items_[i].points.value(0)
                                       : itemBounds(items_[i]).center();
            const double distance = QLineF(world, center).length();
            if (distance < best) {
                best = distance;
                result = {center, SnapKind::Center};
            }
        }
    }
    if (result.kind == SnapKind::None && snap_.edges) {
        for (int i = 0; i < items_.size(); ++i) {
            if (skipped(i)) continue;
            for (const QLineF& segment : itemSegments(items_[i])) {
                QPointF nearest;
                const double distance = distanceToSegment(world, segment, &nearest);
                if (distance < best) {
                    best = distance;
                    result = {nearest, SnapKind::Edge};
                }
            }
        }
    }
    if (result.kind != SnapKind::None) {
        return result;
    }
    result.point = snap_.grid ? snapToGrid(world) : world;
    result.kind = snap_.grid ? SnapKind::Grid : SnapKind::None;
    if (origin != nullptr) {
        result.point = constrainAngle(result.point, *origin);
    }
    return result;
}

int DesignCanvas::hitTest(QPointF screen) const {
    const QPointF world = screenToWorld(screen);
    const double tolerance = 6.0 / scale_;
    for (int i = static_cast<int>(items_.size()) - 1; i >= 0; --i) {
        const SketchItem& item = items_[i];
        if (item.kind == SketchItem::Kind::Symbol || item.kind == SketchItem::Kind::Text) {
            if (itemBounds(item).adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(world)) {
                return i;
            }
            continue;
        }
        if (item.variant == CopperZoneVariant &&
            QPolygonF(item.points).containsPoint(world, Qt::OddEvenFill)) {
            return i;
        }
        for (const QLineF& segment : itemSegments(item)) {
            if (distanceToSegment(world, segment) <= tolerance) {
                return i;
            }
        }
    }
    return -1;
}

void DesignCanvas::placeSymbol(QPointF world) {
    const auto* symbol = findSymbol(variant_);
    if (symbol == nullptr) {
        emit statusMessage(tr("Choose an object from the list first."));
        return;
    }
    SketchItem item;
    item.kind = SketchItem::Kind::Symbol;
    item.points = {world};
    item.variant = variant_;
    item.quarterTurns = placementTurns_;
    item.label = symbol->prefix.isEmpty() ? symbol->defaultLabel
                                          : nextDesignator(items_, symbol->prefix);
    SketchDocument document = items_;
    document.append(item);
    pushEdit(tr("Place %1").arg(item.label.isEmpty() ? symbolDisplayName(*symbol) : item.label),
             document, {});
}

void DesignCanvas::placeText(QPointF world) {
    bool accepted = false;
    const QString text = QInputDialog::getText(this, tr("Place text"), tr("Text:"),
                                               QLineEdit::Normal, QString(), &accepted)
                             .trimmed();
    if (!accepted || text.isEmpty()) {
        return;
    }
    SketchItem item;
    item.kind = SketchItem::Kind::Text;
    item.points = {world};
    item.label = text;
    SketchDocument document = items_;
    document.append(item);
    pushEdit(tr("Place text"), document, {});
}

void DesignCanvas::finishTwoPoint(QPointF world) {
    const QPointF start = pending_.value(0);
    pending_.clear();
    pressGesture_ = false;
    if (samePoint(start, world)) {
        update();
        return;
    }
    if (tool_ == CanvasTool::Measure) {
        hasMeasurement_ = true;
        measurement_ = QLineF(start, world);
        emit statusMessage(measurementText(measurement_));
        update();
        return;
    }
    SketchItem item;
    item.points = {start, world};
    QString text;
    switch (tool_) {
    case CanvasTool::Rectangle:
        item.kind = SketchItem::Kind::Rectangle;
        text = tr("Draw rectangle");
        break;
    case CanvasTool::Circle:
        item.kind = SketchItem::Kind::Circle;
        text = tr("Draw circle");
        break;
    default:
        item.kind = SketchItem::Kind::Line;
        text = tr("Draw line");
        break;
    }
    SketchDocument document = items_;
    document.append(item);
    pushEdit(text, document, {});
}

void DesignCanvas::finishPath() {
    QVector<QPointF> points = pending_;
    pending_.clear();
    bool closed = false;
    if (tool_ == CanvasTool::Polyline) {
        if (points.size() >= 4 && samePoint(points.first(), points.last())) {
            points.removeLast();
            closed = true;
        }
        closed = closed || variant_ == BoardOutlineVariant || variant_ == CopperZoneVariant;
    }
    if (points.size() < (closed ? 3 : 2)) {
        update();
        return;
    }
    SketchItem item;
    item.kind = tool_ == CanvasTool::Wire ? SketchItem::Kind::Wire : SketchItem::Kind::Polyline;
    item.points = points;
    item.variant = variant_;
    item.closed = closed;
    QString text;
    if (tool_ == CanvasTool::Wire) {
        text = workspace_ == Workspace::Board ? tr("Route track") : tr("Draw wire");
    } else if (variant_ == BoardOutlineVariant) {
        text = tr("Draw board outline");
    } else if (variant_ == CopperZoneVariant) {
        text = tr("Draw copper zone");
    } else {
        text = tr("Draw polyline");
    }
    SketchDocument document = items_;
    document.append(item);
    pushEdit(text, document, {});
}

void DesignCanvas::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    const QPointF position = event->position();

    if (event->button() == Qt::MiddleButton) {
        drag_ = Drag::Pan;
        dragStartScreen_ = position;
        panStartOffset_ = offset_;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() == Qt::RightButton) {
        if (isPathTool(tool_) && pending_.size() >= 2) {
            finishPath();
        } else {
            cancelOperation();
        }
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }

    switch (tool_) {
    case CanvasTool::Select: {
        const int hit = hitTest(position);
        const bool additive = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
        if (hit >= 0) {
            if (additive) {
                QList<int> selection = selection_;
                if (selection.contains(hit)) {
                    selection.removeAll(hit);
                } else {
                    selection.append(hit);
                }
                setSelection(selection);
                return;
            }
            if (!selection_.contains(hit)) {
                setSelection({hit});
            }
            drag_ = Drag::Move;
            dragStartScreen_ = position;
            dragStartWorld_ = screenToWorld(position);
            moveReference_ = items_[hit].points.value(0);
            moveDelta_ = {};
        } else {
            if (!additive) {
                clearSelection();
            }
            drag_ = Drag::RubberBand;
            dragStartScreen_ = position;
            dragCurrentScreen_ = position;
        }
        break;
    }
    case CanvasTool::Symbol:
        placeSymbol(snap(position).point);
        break;
    case CanvasTool::Text:
        placeText(snap(position).point);
        break;
    case CanvasTool::Line:
    case CanvasTool::Rectangle:
    case CanvasTool::Circle:
    case CanvasTool::Measure: {
        const Snap point = snap(position, constraintOrigin());
        if (pending_.isEmpty()) {
            pending_ = {point.point};
            pressGesture_ = true;
            pressScreen_ = position;
            hasMeasurement_ = false;
        } else {
            finishTwoPoint(point.point);
        }
        break;
    }
    case CanvasTool::Wire:
    case CanvasTool::Polyline: {
        const Snap point = snap(position, constraintOrigin());
        if (pending_.isEmpty()) {
            pending_.append(point.point);
        } else if (samePoint(point.point, pending_.last())) {
            finishPath();
        } else {
            const bool closing = tool_ == CanvasTool::Polyline && pending_.size() >= 3 &&
                                 samePoint(point.point, pending_.first());
            pending_.append(point.point);
            if (closing ||
                (tool_ == CanvasTool::Wire && point.kind == SnapKind::Object && pending_.size() >= 2)) {
                finishPath();
            }
        }
        break;
    }
    case CanvasTool::Arc: {
        const Snap point = snap(position);
        if (pending_.isEmpty() || !samePoint(point.point, pending_.last())) {
            pending_.append(point.point);
        }
        if (pending_.size() == 3) {
            SketchItem item;
            item.kind = SketchItem::Kind::Arc;
            item.points = {pending_[0], pending_[2], pending_[1]};
            pending_.clear();
            SketchDocument document = items_;
            document.append(item);
            pushEdit(tr("Draw arc"), document, {});
        }
        break;
    }
    }
    update();
}

void DesignCanvas::mouseMoveEvent(QMouseEvent* event) {
    const QPointF position = event->position();
    switch (drag_) {
    case Drag::Pan:
        offset_ = panStartOffset_ + (position - dragStartScreen_);
        update();
        return;
    case Drag::Move: {
        if (moveDelta_.isNull() && QLineF(dragStartScreen_, position).length() < 3.0) {
            return;
        }
        const QPointF target = moveReference_ + (screenToWorld(position) - dragStartWorld_);
        moveDelta_ = snap(worldToScreen(target), nullptr, true).point - moveReference_;
        emit cursorMoved(moveReference_ + moveDelta_);
        update();
        return;
    }
    case Drag::RubberBand:
        dragCurrentScreen_ = position;
        update();
        return;
    case Drag::None:
        break;
    }
    hover_ = snap(position, constraintOrigin());
    hoverValid_ = true;
    emit cursorMoved(hover_.point);
    if (tool_ == CanvasTool::Measure && pending_.size() == 1) {
        emit statusMessage(measurementText(QLineF(pending_.first(), hover_.point)));
    }
    update();
}

void DesignCanvas::mouseReleaseEvent(QMouseEvent* event) {
    const QPointF position = event->position();
    if (event->button() == Qt::MiddleButton && drag_ == Drag::Pan) {
        drag_ = Drag::None;
        setCursor(tool_ == CanvasTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (drag_ == Drag::Move) {
        drag_ = Drag::None;
        const QPointF delta = moveDelta_;
        moveDelta_ = {};
        nudgeSelection(delta);
        update();
        return;
    }
    if (drag_ == Drag::RubberBand) {
        drag_ = Drag::None;
        const QRectF band = QRectF(dragStartScreen_, position).normalized();
        if (band.width() > 3 || band.height() > 3) {
            const QRectF worldBand(screenToWorld(band.topLeft()), screenToWorld(band.bottomRight()));
            QList<int> selection = selection_;
            for (int i = 0; i < items_.size(); ++i) {
                const QRectF bounds = itemBounds(items_[i]).adjusted(-1e-6, -1e-6, 1e-6, 1e-6);
                if (worldBand.intersects(bounds)) {
                    selection.append(i);
                }
            }
            setSelection(selection);
        }
        update();
        return;
    }
    if (pressGesture_) {
        pressGesture_ = false;
        if (isTwoPointTool(tool_) && pending_.size() == 1 &&
            QLineF(pressScreen_, position).length() > 4.0) {
            finishTwoPoint(snap(position, constraintOrigin()).point);
        }
    }
    update();
}

void DesignCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isPathTool(tool_)) {
        if (!pending_.isEmpty()) {
            const Snap point = snap(event->position(), constraintOrigin());
            if (!samePoint(point.point, pending_.last())) {
                pending_.append(point.point);
            }
            finishPath();
        }
        return;
    }
    mousePressEvent(event);
}

void DesignCanvas::wheelEvent(QWheelEvent* event) {
    const double steps = event->angleDelta().y() / 120.0;
    if (std::abs(steps) < 1e-9) {
        event->ignore();
        return;
    }
    zoomAround(event->position(), std::pow(1.2, steps));
    event->accept();
}

void DesignCanvas::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Escape:
        if (hasPendingOperation()) {
            cancelOperation();
        } else if (hasMeasurement_) {
            hasMeasurement_ = false;
            update();
        } else if (!selection_.isEmpty()) {
            clearSelection();
        } else if (tool_ != CanvasTool::Select) {
            emit selectToolRequested();
        }
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (isPathTool(tool_)) {
            finishPath();
            return;
        }
        break;
    case Qt::Key_Backspace:
        if (isPathTool(tool_) && !pending_.isEmpty()) {
            pending_.removeLast();
            update();
            return;
        }
        break;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        if (tool_ == CanvasTool::Select && !selection_.isEmpty()) {
            const double step = gridSize() * ((event->modifiers() & Qt::ShiftModifier) ? 5 : 1);
            const QPointF delta = event->key() == Qt::Key_Left    ? QPointF(-step, 0)
                                  : event->key() == Qt::Key_Right ? QPointF(step, 0)
                                  : event->key() == Qt::Key_Up    ? QPointF(0, -step)
                                                                  : QPointF(0, step);
            nudgeSelection(delta);
            return;
        }
        break;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void DesignCanvas::leaveEvent(QEvent* event) {
    hoverValid_ = false;
    update();
    QWidget::leaveEvent(event);
}

void DesignCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const CanvasColors colors = canvasColors(workspace_, palette());
    const bool board = workspace_ == Workspace::Board;
    const WorldToScreen map = [this](QPointF world) { return worldToScreen(world); };
    painter.fillRect(rect(), colors.background);

    double step = gridSize();
    while (step * scale_ < 8.0) {
        step *= 5.0;
    }
    const double pixelStep = step * scale_;
    const auto firstColumn = static_cast<long long>(std::floor(-offset_.x() / pixelStep));
    const auto lastColumn = static_cast<long long>(std::ceil((width() - offset_.x()) / pixelStep));
    for (long long i = firstColumn; i <= lastColumn; ++i) {
        const double x = static_cast<double>(i) * pixelStep + offset_.x();
        painter.setPen(i % 5 == 0 ? colors.gridMajor : colors.gridMinor);
        painter.drawLine(QLineF(x, 0, x, height()));
    }
    const auto firstRow = static_cast<long long>(std::floor(-offset_.y() / pixelStep));
    const auto lastRow = static_cast<long long>(std::ceil((height() - offset_.y()) / pixelStep));
    for (long long i = firstRow; i <= lastRow; ++i) {
        const double y = static_cast<double>(i) * pixelStep + offset_.y();
        painter.setPen(i % 5 == 0 ? colors.gridMajor : colors.gridMinor);
        painter.drawLine(QLineF(0, y, width(), y));
    }
    painter.setPen(QPen(colors.origin, 1.5));
    painter.drawLine(QLineF(offset_.x() - 8, offset_.y(), offset_.x() + 8, offset_.y()));
    painter.drawLine(QLineF(offset_.x(), offset_.y() - 8, offset_.x(), offset_.y() + 8));

    painter.setRenderHint(QPainter::Antialiasing);
    for (int i = 0; i < items_.size(); ++i) {
        const bool selected = selection_.contains(i);
        if (selected && drag_ == Drag::Move && !moveDelta_.isNull()) {
            SketchItem moved = items_[i];
            translateItem(moved, moveDelta_);
            drawItem(painter, moved, colors, board, true, false, scale_, map);
        } else {
            drawItem(painter, items_[i], colors, board, selected, false, scale_, map);
        }
    }

    auto drawMeasurement = [&](const QLineF& line) {
        const QPointF a = map(line.p1());
        const QPointF b = map(line.p2());
        painter.setPen(strokePen(colors.preview, 1.5));
        painter.drawLine(a, b);
        QLineF normal = QLineF(a, b).normalVector();
        normal.setLength(6);
        const QPointF tick = normal.p2() - normal.p1();
        painter.drawLine(a - tick, a + tick);
        painter.drawLine(b - tick, b + tick);
        const QString text = tr("%1 mm").arg(line.length(), 0, 'f', 3);
        QFont font = painter.font();
        font.setPixelSize(12);
        painter.setFont(font);
        const QRectF label(QLineF(a, b).center() + QPointF(8, -24), QSizeF(96, 20));
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.background);
        painter.drawRoundedRect(label, 3, 3);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(colors.preview);
        painter.drawText(label, Qt::AlignCenter, text);
    };

    if (hoverValid_ && drag_ == Drag::None) {
        SketchItem preview;
        bool hasPreview = false;
        switch (tool_) {
        case CanvasTool::Symbol:
            if (findSymbol(variant_) != nullptr) {
                preview.kind = SketchItem::Kind::Symbol;
                preview.points = {hover_.point};
                preview.variant = variant_;
                preview.quarterTurns = placementTurns_;
                hasPreview = true;
            }
            break;
        case CanvasTool::Line:
        case CanvasTool::Rectangle:
        case CanvasTool::Circle:
            if (pending_.size() == 1) {
                preview.kind = tool_ == CanvasTool::Line        ? SketchItem::Kind::Line
                               : tool_ == CanvasTool::Rectangle ? SketchItem::Kind::Rectangle
                                                                : SketchItem::Kind::Circle;
                preview.points = {pending_.first(), hover_.point};
                hasPreview = true;
            }
            break;
        case CanvasTool::Wire:
        case CanvasTool::Polyline:
            if (!pending_.isEmpty()) {
                preview.kind = tool_ == CanvasTool::Wire ? SketchItem::Kind::Wire
                                                         : SketchItem::Kind::Polyline;
                preview.points = pending_;
                preview.points.append(hover_.point);
                preview.variant = variant_;
                hasPreview = true;
            }
            break;
        case CanvasTool::Arc:
            if (pending_.size() == 1) {
                preview.kind = SketchItem::Kind::Line;
                preview.points = {pending_.first(), hover_.point};
                hasPreview = true;
            } else if (pending_.size() == 2) {
                preview.kind = SketchItem::Kind::Arc;
                preview.points = {pending_[0], hover_.point, pending_[1]};
                hasPreview = true;
            }
            break;
        case CanvasTool::Measure:
            if (pending_.size() == 1) {
                drawMeasurement(QLineF(pending_.first(), hover_.point));
            }
            break;
        case CanvasTool::Select:
        case CanvasTool::Text:
            break;
        }
        if (hasPreview) {
            drawItem(painter, preview, colors, board, false, true, scale_, map);
        }
    }
    if (hasMeasurement_) {
        drawMeasurement(measurement_);
    }

    if (drag_ == Drag::RubberBand) {
        QColor fill = colors.selection;
        fill.setAlpha(36);
        painter.setBrush(fill);
        painter.setPen(strokePen(colors.selection, 1.0, true));
        painter.drawRect(QRectF(dragStartScreen_, dragCurrentScreen_).normalized());
        painter.setBrush(Qt::NoBrush);
    }

    if (hoverValid_ && drag_ == Drag::None && tool_ != CanvasTool::Select) {
        const QPointF point = map(hover_.point);
        painter.setPen(strokePen(colors.preview, 1.5));
        switch (hover_.kind) {
        case SnapKind::Object:
            painter.drawRect(QRectF(point - QPointF(6, 6), QSizeF(12, 12)));
            break;
        case SnapKind::Center:
            painter.drawEllipse(point, 6, 6);
            painter.drawLine(point - QPointF(3, 0), point + QPointF(3, 0));
            painter.drawLine(point - QPointF(0, 3), point + QPointF(0, 3));
            break;
        case SnapKind::Edge:
            painter.drawLine(point - QPointF(5, 5), point + QPointF(5, 5));
            painter.drawLine(point - QPointF(5, -5), point + QPointF(5, -5));
            break;
        case SnapKind::Grid:
        case SnapKind::None:
            painter.drawLine(point - QPointF(4, 0), point + QPointF(4, 0));
            painter.drawLine(point - QPointF(0, 4), point + QPointF(0, 4));
            break;
        }
    }

    QFont captionFont = painter.font();
    captionFont.setPixelSize(11);
    painter.setFont(captionFont);
    painter.setPen(colors.label);
    painter.drawText(QRectF(12, height() - 26, width() - 24, 18), Qt::AlignLeft | Qt::AlignVCenter,
                     tr("%1  ·  Grid %2 mm  ·  %3%")
                         .arg(board ? tr("PCB layout") : tr("Schematic sheet"))
                         .arg(gridSize(), 0, 'f', board ? 3 : 2)
                         .arg(zoomPercent()));
}

void DesignCanvas::paintSymbolPreview(QPainter& painter, const QRectF& target,
                                      const QString& symbolId, const QPalette& palette) {
    const auto* symbol = findSymbol(symbolId);
    if (symbol == nullptr || target.isEmpty()) {
        return;
    }
    QPolygonF points;
    for (const auto& shape : symbol->shapes) {
        for (const QPointF& point : shape.points) {
            points << point;
        }
    }
    QRectF bounds = points.boundingRect();
    bounds.adjust(-0.6, -0.6, 0.6, 0.6);
    const double scale = std::min(target.width() / bounds.width(), target.height() / bounds.height());
    const QPointF offset = target.center() - bounds.center() * scale;
    const CanvasColors colors = canvasColors(symbol->workspace, palette);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    drawSymbol(painter, *symbol, [scale, offset](QPointF point) { return point * scale + offset; },
               colors, colors.stroke, 1.4, false, false);
    painter.restore();
}

} // namespace hatt::ui
