#include "hatt/ui/DesignCanvas.hpp"
#include "hatt/ui/LayerColors.hpp"
#include "hatt/ui/PadStyles.hpp"
#include "hatt/ui/SketchCircuit.hpp"

#include <QFont>
#include <QFontMetricsF>
#include <QApplication>
#include <QContextMenuEvent>
#include <QTimer>
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
#include <array>
#include <cmath>
#include <optional>
#include <functional>
#include <limits>
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
    QColor guide;
    std::array<QColor, BoardLayerCount> layers;
};

// Top copper and bottom copper follow the design system's copper and secondary layer tokens.
// Items are drawn bottom side first, then the active side, so the active layer stays on top.
struct LayerView {
    int visible = AllLayersMask;
    BoardLayer active = BoardLayer::TopCopper;
};

bool bottomActive(const LayerView& view) { return isBottomLayer(view.active); }

CanvasColors canvasColors(Workspace workspace, const QPalette& palette) {
    const bool dark = palette.color(QPalette::Window).lightness() < 128;
    const bool board = workspace == Workspace::Board;
    CanvasColors colors;
    for (int layer = 0; layer < BoardLayerCount; ++layer) {
        colors.layers[layer] = boardLayerColor(static_cast<BoardLayer>(layer), dark);
    }
    if (dark) {
        colors.background = QColor(board ? "#080b0f" : "#0b1016");
        colors.gridMinor = QColor(board ? "#10171e" : "#131b23");
        colors.gridMajor = QColor(board ? "#1c2630" : "#202b36");
        colors.origin = QColor("#2f6f68");
        colors.stroke = QColor(board ? "#d9e1e8" : "#c9d4de");
        colors.wire = QColor(board ? "#ff4d4d" : "#8fd694");
        colors.copper = QColor("#ff4d4d");
        colors.graphics = QColor(board ? "#62b6ff" : "#c9d4de");
        colors.outline = QColor("#e9c46a");
        colors.selection = QColor("#18b6a4");
        colors.preview = QColor("#ffb45d");
        colors.label = QColor("#8fa0ae");
        colors.guide = QColor("#ff5fc8");
    } else {
        colors.background = QColor(board ? "#f4f6f7" : "#fbfbf8");
        colors.gridMinor = QColor(board ? "#e5e9ec" : "#eaece5");
        colors.gridMajor = QColor(board ? "#ced5db" : "#d4d8cd");
        colors.origin = QColor("#7fb8b1");
        colors.stroke = QColor(board ? "#3a4652" : "#2d3a45");
        colors.wire = QColor(board ? "#d11f1f" : "#2e7d32");
        colors.copper = QColor("#d11f1f");
        colors.graphics = QColor(board ? "#1f78c1" : "#2d3a45");
        colors.outline = QColor("#b8860b");
        colors.selection = QColor("#087f73");
        colors.preview = QColor("#c05f00");
        colors.label = QColor("#5e6b76");
        colors.guide = QColor("#c2187f");
    }
    return colors;
}

QPen strokePen(const QColor& color, double width, bool dashed = false) {
    QPen pen(color, width, dashed ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    return pen;
}

// Same size as the explicit junction symbol (0.5 mm radius), never smaller than the wire.
void drawJunctionDots(QPainter& painter, const QVector<QPointF>& points, const QColor& color,
                      double scale, const WorldToScreen& map) {
    const double radius = std::max(3.5, 0.5 * scale);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const QPointF& point : points) painter.drawEllipse(map(point), radius, radius);
    painter.setBrush(Qt::NoBrush);
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

constexpr int BottomSideMask = layerBit(BoardLayer::BottomCopper) | layerBit(BoardLayer::BottomSilk) |
                               layerBit(BoardLayer::BottomResist) | layerBit(BoardLayer::BottomPaste);
constexpr int TopSideMask = layerBit(BoardLayer::TopCopper) | layerBit(BoardLayer::TopSilk) |
                            layerBit(BoardLayer::TopResist) | layerBit(BoardLayer::TopPaste);

// 0 for items only on the inactive side, 1 otherwise (active side, both sides or board edge).
int drawRank(const SketchItem& item, const LayerView& view) {
    const int mask = itemLayerMask(item);
    const int inactive = bottomActive(view) ? TopSideMask : BottomSideMask;
    return (mask & inactive) != 0 && (mask & ~inactive) == 0 ? 0 : 1;
}

// Items entirely on the inactive board side are drawn translucent.
QColor sideColor(QColor color, int mask, const LayerView& view) {
    const int inactive = bottomActive(view) ? TopSideMask : BottomSideMask;
    if ((mask & inactive) != 0 && (mask & ~inactive) == 0) color.setAlpha(150);
    return color;
}

void drawPads(QPainter& painter, const QVector<PlacedPad>& pads, const CanvasColors& colors,
              const LayerView& view, bool selected, bool preview, double scale,
              const WorldToScreen& map) {
    const BoardLayer activeCopper = bottomActive(view) ? BoardLayer::BottomCopper : BoardLayer::TopCopper;
    for (const auto& pad : pads) {
        const int shown = pad.layers & view.visible & CopperLayerMask;
        if (shown == 0) continue;
        const bool onActive = (shown & layerBit(activeCopper)) != 0;
        const BoardLayer layer = onActive ? activeCopper : oppositeSideLayer(activeCopper);
        QColor fill = preview ? colors.preview : colors.layers[static_cast<int>(layer)];
        if (preview) fill.setAlpha(150);
        else if (!onActive) fill.setAlpha(150);
        QPolygonF outline;
        for (const QPointF& point : padOutline(pad)) outline << map(point);
        painter.setPen(selected && !preview ? strokePen(colors.selection, 2.0) : QPen(Qt::NoPen));
        painter.setBrush(fill);
        painter.drawPolygon(outline);
        if (pad.drill > 0.0) {
            const double radius = std::max(1.0, pad.drill / 2.0 * scale);
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.background);
            painter.drawEllipse(map(pad.center), radius, radius);
        }
        const double size = std::min(pad.width, pad.height) * scale;
        if (!preview && pad.number > 0 && size >= 14.0) {
            QFont font = painter.font();
            font.setPixelSize(std::clamp(static_cast<int>(size * 0.42), 8, 18));
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(pad.drill > 0.0 ? colors.label : colors.background);
            const QPointF center = map(pad.center);
            painter.drawText(QRectF(center - QPointF(size, size / 2), QSizeF(2 * size, size)),
                             Qt::AlignCenter, QString::number(pad.number));
        }
    }
    painter.setBrush(Qt::NoBrush);
}

void drawItem(QPainter& painter, const SketchItem& item, const CanvasColors& colors, bool board,
              bool selected, bool preview, double scale, const WorldToScreen& map,
              const LayerView& view = {}) {
    if (item.points.isEmpty()) {
        return;
    }
    auto pick = [&](const QColor& normal) {
        return preview ? colors.preview : selected ? colors.selection : normal;
    };
    // Board graphics take the colour of their layer; the board outline is always the edge layer.
    auto graphicsColor = [&] {
        if (!board) return colors.graphics;
        const BoardLayer layer = item.variant == BoardOutlineVariant ? BoardLayer::BoardEdge : item.layer;
        return sideColor(colors.layers[static_cast<int>(layer)], layerBit(layer), view);
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
        const BoardLayer silk = item.onBottom ? BoardLayer::BottomSilk : BoardLayer::TopSilk;
        const bool silkVisible = !board || (view.visible & layerBit(silk)) != 0;
        if (silkVisible) {
            const QColor stroke =
                board ? sideColor(colors.layers[static_cast<int>(silk)], layerBit(silk), view)
                      : colors.stroke;
            drawSymbol(painter, *symbol,
                       [&](QPointF local) { return map(symbolToWorld(item, local)); }, colors,
                       pick(stroke), width, selected, preview);
        }
        if (board) {
            drawPads(painter, itemPads(item), colors, view, selected, preview, scale, map);
        }
        if (!item.label.isEmpty() && !preview && silkVisible) {
            const QRectF bounds = itemBounds(item);
            const QPointF topCenter = map(QPointF(bounds.center().x(), bounds.top()));
            QFont font = painter.font();
            font.setPixelSize(std::clamp(static_cast<int>(1.6 * scale), 9, 26));
            painter.setFont(font);
            painter.setPen(selected ? colors.selection : colors.label);
            painter.drawText(QRectF(topCenter.x() - 80, topCenter.y() - font.pixelSize() - 6, 160,
                                    font.pixelSize() + 4),
                             Qt::AlignHCenter | Qt::AlignBottom,
                             item.value.isEmpty() ? item.label : item.label + QStringLiteral("  ") + item.value);
        }
        if (selected && !preview && !board) {
            QFont font = painter.font();
            font.setPixelSize(10);
            painter.setFont(font);
            painter.setPen(colors.selection);
            for (int pin = 0; pin < symbol->pins.size(); ++pin)
                painter.drawText(map(symbolToWorld(item, symbol->pins[pin])) + QPointF(5, -5),
                                 QString::number(pin + 1));
        }
        break;
    }
    case SketchItem::Kind::Wire:
        if (board) {
            // Tracks are drawn at their copper width in the colour of their layer.
            const QColor color = sideColor(colors.layers[static_cast<int>(item.layer)],
                                           layerBit(item.layer), view);
            painter.setPen(strokePen(pick(color), std::max(2.0, trackWidth(item) * scale), preview));
        } else {
            painter.setPen(strokePen(pick(colors.wire), selected ? 3.0 : 2.0, preview));
        }
        painter.drawPolyline(polygon);
        break;
    case SketchItem::Kind::Line:
    case SketchItem::Kind::Polyline: {
        const bool outline = item.variant == BoardOutlineVariant;
        const bool zone = item.variant == CopperZoneVariant;
        const QColor base = board ? graphicsColor() : outline ? colors.outline : colors.graphics;
        const QColor color = pick(base);
        painter.setPen(strokePen(color, outline ? std::max(width, 2.0) : width, preview));
        if (zone) {
            QColor fill = board ? base : colors.copper;
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
        painter.setPen(strokePen(pick(graphicsColor()), width, preview));
        painter.drawRect(QRectF(map(item.points.value(0)), map(item.points.value(1))).normalized());
        break;
    case SketchItem::Kind::Circle: {
        const double radius = QLineF(item.points.value(0), item.points.value(1)).length() * scale;
        painter.setPen(strokePen(pick(graphicsColor()), width, preview));
        painter.drawEllipse(map(item.points.value(0)), radius, radius);
        break;
    }
    case SketchItem::Kind::Arc: {
        QPolygonF samples;
        for (const QPointF& point :
             arcSamples(item.points.value(0), item.points.value(1), item.points.value(2))) {
            samples << map(point);
        }
        painter.setPen(strokePen(pick(graphicsColor()), width, preview));
        painter.drawPolyline(samples);
        break;
    }
    case SketchItem::Kind::Text: {
        QFont font = painter.font();
        font.setPixelSize(std::max(6, static_cast<int>(TextHeightMm * scale * 0.8)));
        painter.setFont(font);
        painter.setPen(pick(graphicsColor()));
        painter.drawText(QRectF(map(item.points.first()), QSizeF(4000, TextHeightMm * scale)),
                         Qt::AlignLeft | Qt::AlignVCenter, item.label);
        break;
    }
    case SketchItem::Kind::Pad:
    case SketchItem::Kind::Via:
        drawPads(painter, itemPads(item), colors, view, selected, preview, scale, map);
        break;
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

QString measurementText(const QLineF& line, LengthUnit unit) {
    const double dx = line.dx();
    const double dy = -line.dy();
    const double angle = std::atan2(dy, dx) * 180.0 / Pi;
    return DesignCanvas::tr("Distance %1  ·  ΔX %2  ·  ΔY %3  ·  %4°")
        .arg(formatLength(line.length(), unit), formatLength(dx, unit), formatLength(dy, unit))
        .arg(angle, 0, 'f', 1);
}

// Spacing is measured between bounding boxes of non-wire items; wires connect objects rather
// than being spaced from them.
QVector<QRectF> spacingBoxes(const SketchDocument& document, const QList<int>& skipped) {
    QVector<QRectF> boxes;
    for (int i = 0; i < document.size(); ++i) {
        const SketchItem& item = document[i];
        if (skipped.contains(i) || item.kind == SketchItem::Kind::Wire || item.points.isEmpty()) {
            continue;
        }
        boxes.append(itemBounds(item));
    }
    return boxes;
}

enum SpacingSide { Left, Right, Up, Down };
using NearestGaps = std::array<std::optional<SpacingIndicator>, 4>;

constexpr double GapTolerance = 1e-4;
bool sameGap(double a, double b) { return std::abs(a - b) < GapTolerance; }
bool horizontalGap(const SpacingIndicator& gap) { return std::abs(gap.line.dy()) < 1e-9; }

// Nearest gap on each side of `moving` to a box overlapping it across that gap (as design tools
// show spacing). Lines run from the left/upper box edge to the right/lower one.
NearestGaps nearestGaps(const QVector<QRectF>& boxes, const QRectF& moving) {
    NearestGaps best;
    auto consider = [&](SpacingSide side, double gap, QLineF line) {
        if (gap < 1e-6 || (best[side] && gap >= best[side]->distance)) return;
        best[side] = SpacingIndicator{line, gap};
    };
    for (const QRectF& other : boxes) {
        if (other.top() < moving.bottom() && other.bottom() > moving.top()) {
            const double y = (std::max(other.top(), moving.top()) +
                              std::min(other.bottom(), moving.bottom())) / 2.0;
            if (other.right() <= moving.left()) {
                consider(Left, moving.left() - other.right(),
                         QLineF(other.right(), y, moving.left(), y));
            } else if (other.left() >= moving.right()) {
                consider(Right, other.left() - moving.right(),
                         QLineF(moving.right(), y, other.left(), y));
            }
        }
        if (other.left() < moving.right() && other.right() > moving.left()) {
            const double x = (std::max(other.left(), moving.left()) +
                              std::min(other.right(), moving.right())) / 2.0;
            if (other.bottom() <= moving.top()) {
                consider(Up, moving.top() - other.bottom(),
                         QLineF(x, other.bottom(), x, moving.top()));
            } else if (other.top() >= moving.bottom()) {
                consider(Down, other.top() - moving.bottom(),
                         QLineF(x, moving.bottom(), x, other.top()));
            }
        }
    }
    return best;
}

// Gaps between neighbouring boxes: each box to its nearest right and lower neighbour.
QVector<SpacingIndicator> referenceGaps(const QVector<QRectF>& boxes) {
    QVector<SpacingIndicator> gaps;
    auto add = [&gaps](const std::optional<SpacingIndicator>& gap) {
        if (!gap) return;
        const bool known = std::any_of(gaps.begin(), gaps.end(), [&](const SpacingIndicator& g) {
            return QLineF(g.line.p1(), gap->line.p1()).length() < 1e-6 &&
                   QLineF(g.line.p2(), gap->line.p2()).length() < 1e-6;
        });
        if (!known) gaps.append(*gap);
    };
    for (const QRectF& box : boxes) {
        const NearestGaps nearest = nearestGaps(boxes, box);
        add(nearest[Right]);
        add(nearest[Down]);
    }
    return gaps;
}

// Gaps around `moving`, flagged equal when they match the opposite gap or a gap between other
// objects; the matching gaps between other objects are appended so both sides of the match show.
QVector<SpacingIndicator> spacingIndicators(const SketchDocument& document, const QList<int>& skipped,
                                            const QRectF& moving) {
    const QVector<QRectF> boxes = spacingBoxes(document, skipped);
    NearestGaps gaps = nearestGaps(boxes, moving);
    const QVector<SpacingIndicator> references = referenceGaps(boxes);
    QVector<SpacingIndicator> matched;
    for (int side = Left; side <= Down; ++side) {
        auto& gap = gaps[side];
        if (!gap) continue;
        const auto& opposite = gaps[side ^ 1];
        gap->equal = opposite && sameGap(opposite->distance, gap->distance);
        for (const SpacingIndicator& reference : references) {
            if (horizontalGap(reference) != horizontalGap(*gap) ||
                !sameGap(reference.distance, gap->distance)) {
                continue;
            }
            gap->equal = true;
            const bool known = std::any_of(matched.begin(), matched.end(), [&](const auto& m) {
                return m.line == reference.line;
            });
            if (!known) matched.append({reference.line, reference.distance, true});
        }
    }
    QVector<SpacingIndicator> result;
    for (const auto& gap : gaps) {
        if (gap) result.append(*gap);
    }
    return result + matched;
}

SketchItem copiedItem(const SketchDocument& document, const SketchItem& item, QPointF offset) {
    SketchItem copy = item;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.sourceId.clear();
    translateItem(copy, offset);
    if (copy.kind == SketchItem::Kind::Symbol) {
        const auto* symbol = findSymbol(copy.variant);
        if (symbol != nullptr && !symbol->prefix.isEmpty()) {
            copy.label = nextDesignator(document, symbol->prefix);
        }
    }
    return copy;
}

bool samePoint(QPointF a, QPointF b) { return QLineF(a, b).length() < 1e-6; }

bool sameGeometry(const SketchDocument& a, const SketchDocument& b) {
    if (a.size() != b.size()) return false;
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (a[i].points.size() != b[i].points.size()) return false;
        for (qsizetype p = 0; p < a[i].points.size(); ++p) {
            if (!samePoint(a[i].points[p], b[i].points[p])) return false;
        }
    }
    return true;
}

bool isTwoPointTool(CanvasTool tool) {
    return tool == CanvasTool::Line || tool == CanvasTool::Rectangle ||
           tool == CanvasTool::Circle || tool == CanvasTool::Measure;
}

bool isPathTool(CanvasTool tool) { return tool == CanvasTool::Wire || tool == CanvasTool::Polyline; }

} // namespace

DesignCanvas::DesignCanvas(Workspace workspace, QWidget* parent)
    : QWidget(parent), workspace_(workspace), undoStack_(new QUndoStack(this)),
      unit_(displayUnit(workspace, LengthUnit::Millimetre)), scale_(defaultScale()) {
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

double DesignCanvas::gridStep(Workspace workspace, int level) {
    static constexpr double schematic[GridLevelCount] = {0.254, 1.27, 2.54, 12.7};
    static constexpr double board[GridLevelCount] = {0.127, 0.254, 0.635, 1.27};
    const int index = std::clamp(level, 0, GridLevelCount - 1);
    return workspace == Workspace::Board ? board[index] : schematic[index];
}

double DesignCanvas::gridSize() const noexcept { return gridStep(workspace_, snap_.gridLevel); }

QVector<QLineF> DesignCanvas::activeGuides() const {
    if (drag_ == Drag::Move) return moveGuides_;
    return hoverValid_ && drag_ == Drag::None && tool_ != CanvasTool::Select ? hover_.guides
                                                                             : QVector<QLineF>();
}

QVector<SpacingIndicator> DesignCanvas::activeSpacings() const {
    QRectF moving;
    if (drag_ == Drag::Move && moveKind_ == MoveKind::Selection &&
        moveDocument_.size() == items_.size()) {
        for (int index : selection_) {
            const SketchItem& item = moveDocument_[index];
            if (item.kind == SketchItem::Kind::Wire || item.points.isEmpty()) continue;
            moving = moving.isNull() ? itemBounds(item) : moving.united(itemBounds(item));
        }
        return moving.isNull() ? QVector<SpacingIndicator>{}
                               : spacingIndicators(moveDocument_, selection_, moving);
    }
    if (hoverValid_ && drag_ == Drag::None && tool_ == CanvasTool::Symbol &&
        findSymbol(variant_) != nullptr) {
        SketchItem preview;
        preview.kind = SketchItem::Kind::Symbol;
        preview.variant = variant_;
        preview.points = {hover_.point};
        preview.quarterTurns = placementTurns_;
        return spacingIndicators(items_, {}, itemBounds(preview));
    }
    return {};
}

void DesignCanvas::setLengthUnit(LengthUnit unit) {
    unit_ = unit;
    update();
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
        return tr("Click to select and drag to move; connected wires follow and guides show "
                  "alignment, Shift keeps the move horizontal or vertical. Drag a wire segment or "
                  "corner to reshape it. Drag on empty space for box selection; Shift or Ctrl adds "
                  "to the selection. Arrow keys nudge, Ctrl+R rotates, Delete removes.");
    case CanvasTool::Symbol:
        if (const auto* symbol = findSymbol(variant_)) {
            return tr("Click to place %1. Guides show alignment with other pins and pins join when "
                      "close. Ctrl+R rotates before placing. Esc returns to selection.")
                .arg(symbolDisplayName(*symbol));
        }
        return tr("Choose an object from the list.");
    case CanvasTool::Wire:
        return workspace_ == Workspace::Board
                   ? tr("Click to start a track and click to add corners. The track ends on a pad "
                        "automatically; double-click or Enter finishes, right-click cancels.")
                   : tr("Click a pin to start a wire and click the target; right-angle corners are "
                        "added automatically (hold Ctrl for a free angle). The wire ends on a pin "
                        "automatically; double-click or Enter finishes, right-click cancels.");
    case CanvasTool::Line:
    case CanvasTool::Rectangle:
        return tr("Drag, or click the start point and then the end point.");
    case CanvasTool::Circle:
        return tr("Drag from the centre, or click the centre and then a point on the circle.");
    case CanvasTool::Polyline:
        return tr("Click to add vertices. Double-click or Enter finishes; right-click cancels. Backspace "
                  "removes the last vertex.");
    case CanvasTool::Arc:
        return tr("Click the start point, the end point, then a point the arc passes through.");
    case CanvasTool::Text:
        return tr("Click to place a text label.");
    case CanvasTool::Measure:
        return tr("Drag, or click two points, to measure. Measurements are not added to the design.");
    case CanvasTool::Pad:
        if (const auto style = findPadStyleEntry(variant_)) {
            return tr("Click to place a %1. SMD pads go to the active copper layer; pads are numbered "
                      "in placement order. Ctrl+R rotates before placing.")
                .arg(style->builtIn ? style->name.toLower() : style->name);
        }
        return tr("Choose a pad from the list.");
    case CanvasTool::Via:
        return tr("Click to place a via that joins the top and bottom copper layers. While routing, "
                  "Space, Page Up or Page Down changes the layer and adds a via automatically.");
    }
    return {};
}

void DesignCanvas::setActiveLayer(BoardLayer layer) {
    if (layer == activeLayer_) return;
    const BoardLayer previousCopper = routeLayer();
    const bool routing = workspace_ == Workspace::Board && tool_ == CanvasTool::Wire && !pending_.isEmpty();
    activeLayer_ = layer;
    if (routing && routeLayer() != previousCopper) {
        const QPointF at = pending_.last();
        const bool returning = pending_.size() == 1 && !routePieces_.isEmpty() &&
                               routePieces_.last().kind == SketchItem::Kind::Via &&
                               samePoint(routePieces_.last().points.first(), at);
        if (returning) {
            // Switching straight back removes the via that the previous switch added.
            activeLayer_ = previousCopper;
            revertRouteLayerChange();
            activeLayer_ = layer;
        } else {
            if (pending_.size() >= 2) {
                SketchItem track = pendingTrack(pending_);
                track.layer = previousCopper;
                routePieces_.append(track);
            }
            // A via is only needed where nothing already joins both copper layers.
            bool joined = false;
            for (const SketchDocument* document : {&items_, &routePieces_}) {
                for (const auto& item : *document) {
                    for (const auto& pad : itemPads(item)) {
                        joined = joined || ((pad.layers & CopperLayerMask) == CopperLayerMask &&
                                            samePoint(pad.center, at));
                    }
                }
            }
            if (!joined) routePieces_.append(pendingVia(at));
            pending_ = {at};
        }
    }
    emit activeLayerChanged(activeLayer_);
    update();
}

bool DesignCanvas::revertRouteLayerChange() {
    if (routePieces_.isEmpty() || pending_.size() != 1) return false;
    const QPointF at = pending_.first();
    if (routePieces_.last().kind == SketchItem::Kind::Via &&
        samePoint(routePieces_.last().points.first(), at)) {
        routePieces_.removeLast();
    }
    if (!routePieces_.isEmpty() && routePieces_.last().kind == SketchItem::Kind::Wire &&
        samePoint(routePieces_.last().points.last(), at)) {
        const SketchItem track = routePieces_.takeLast();
        pending_ = track.points;
        activeLayer_ = track.layer;
    } else {
        activeLayer_ = oppositeSideLayer(routeLayer());
    }
    return true;
}

void DesignCanvas::setVisibleLayers(int mask) {
    visibleLayers_ = mask & AllLayersMask;
    QList<int> selection = selection_;
    selection.removeIf([this](int index) { return !itemVisible(items_[index]); });
    setSelection(selection);
    update();
}

void DesignCanvas::setTrackWidth(double millimetres) {
    if (millimetres > 0.0) trackWidth_ = millimetres;
    update();
}

void DesignCanvas::setViaSize(double diameter, double drill) {
    if (diameter > 0.0 && drill > 0.0 && drill < diameter) {
        viaDiameter_ = diameter;
        viaDrill_ = drill;
    }
    update();
}

QColor DesignCanvas::layerColor(BoardLayer layer, const QPalette& palette) {
    return boardLayerColor(layer, palette.color(QPalette::Window).lightness() < 128);
}

bool DesignCanvas::itemVisible(const SketchItem& item) const {
    return workspace_ != Workspace::Board || (itemLayerMask(item) & visibleLayers_) != 0;
}

BoardLayer DesignCanvas::routeLayer() const noexcept {
    if (isCopperLayer(activeLayer_)) return activeLayer_;
    return isBottomLayer(activeLayer_) ? BoardLayer::BottomCopper : BoardLayer::TopCopper;
}

BoardLayer DesignCanvas::graphicsLayer() const noexcept {
    if (!isCopperLayer(activeLayer_)) return activeLayer_;
    return activeLayer_ == BoardLayer::BottomCopper ? BoardLayer::BottomSilk : BoardLayer::TopSilk;
}

SketchItem DesignCanvas::pendingTrack(const QVector<QPointF>& points) const {
    SketchItem track;
    track.kind = SketchItem::Kind::Wire;
    track.points = points;
    simplifyPath(track.points);
    track.variant = variant_;
    track.layer = routeLayer();
    track.width = trackWidth_;
    return track;
}

SketchItem DesignCanvas::pendingVia(QPointF at) const {
    SketchItem via;
    via.kind = SketchItem::Kind::Via;
    via.points = {at};
    via.width = viaDiameter_;
    via.drillDiameter = viaDrill_;
    return via;
}

void DesignCanvas::setTool(CanvasTool tool, const QString& variant) {
    if (contextMenuTimer_) contextMenuTimer_->stop();
    cancelOperation();
    tool_ = tool;
    variant_ = variant;
    placementTemplate_.reset();
    placementTurns_ = 0;
    hasMeasurement_ = false;
    if (tool_ != CanvasTool::Select) {
        clearSelection();
    }
    setCursor(tool_ == CanvasTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    emit statusMessage(toolHint());
    update();
}

void DesignCanvas::setPlacementTemplate(const SketchItem& item) {
    if (tool_ == CanvasTool::Symbol && item.variant == variant_) placementTemplate_ = item;
}

void DesignCanvas::setSnapSettings(const SnapSettings& settings) {
    snap_ = settings;
    update();
}

void DesignCanvas::cancelOperation() {
    if (contextMenuTimer_) contextMenuTimer_->stop();
    pending_.clear();
    routePieces_.clear();
    pressGesture_ = false;
    if (drag_ == Drag::Move || drag_ == Drag::RubberBand) {
        drag_ = Drag::None;
    }
    moveDelta_ = {};
    moveDocument_.clear();
    moveGuides_.clear();
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
        if (itemVisible(items_[i])) all.append(i);
    }
    setSelection(all);
}

void DesignCanvas::clearSelection() { setSelection({}); }

void DesignCanvas::selectItem(int index) { setSelection({index}); }

void DesignCanvas::editItemProperties(int index, const QString& label, QPointF position,
                                      int quarterTurns) {
    if (index < 0 || index >= items_.size() || items_[index].points.isEmpty()) return;
    auto properties = items_[index];
    properties.label = label;
    properties.points[0] = position;
    properties.quarterTurns = quarterTurns;
    editItemProperties(index, properties);
}

void DesignCanvas::editItemProperties(int index, const SketchItem& properties) {
    const QPointF position = properties.points.value(0);
    if (index < 0 || index >= items_.size() || items_[index].points.isEmpty() ||
        !std::isfinite(position.x()) || !std::isfinite(position.y())) return;
    SketchDocument document = items_;
    auto& item = document[index];
    const int turns = ((properties.quarterTurns % 4) + 4) % 4;
    const QPointF anchor = item.points.first();
    if (item.label == properties.label && anchor == position && item.quarterTurns == turns &&
        item.value == properties.value && item.footprint == properties.footprint &&
        item.pinPadMap == properties.pinPadMap && item.excludeFromBoard == properties.excludeFromBoard &&
        item.layer == properties.layer && item.onBottom == properties.onBottom &&
        item.pad == properties.pad && item.width == properties.width &&
        item.drillDiameter == properties.drillDiameter) return;
    const int delta = (turns - item.quarterTurns + 4) % 4;
    for (int i = 0; i < delta; ++i) rotateItemQuarterTurn(item, anchor);
    item.quarterTurns = turns;
    translateItem(item, position - item.points.first());
    item.label = properties.label;
    item.value = properties.value;
    item.footprint = properties.footprint;
    item.pinPadMap = properties.pinPadMap;
    item.excludeFromBoard = properties.excludeFromBoard;
    item.layer = properties.layer;
    item.onBottom = properties.onBottom;
    item.pad = properties.pad;
    item.width = properties.width;
    item.drillDiameter = properties.drillDiameter;
    pushEdit(tr("Edit properties"), document, {index});
}

void DesignCanvas::restore(const SketchDocument& document, const QList<int>& selection) {
    if (contextMenuTimer_) contextMenuTimer_->stop();
    pending_.clear();
    routePieces_.clear();
    pressGesture_ = false;
    if (drag_ != Drag::Pan) {
        drag_ = Drag::None;
    }
    moveDelta_ = {};
    moveDocument_.clear();
    moveGuides_.clear();
    items_ = document;
    selection_ = selection;
    selection_.removeIf([this](int index) { return index < 0 || index >= items_.size(); });
    emit selectionChanged(static_cast<int>(selection_.size()));
    emit documentChanged();
    update();
}

void DesignCanvas::applyDocumentEdit(const QString& title, const SketchDocument& document) {
    cancelOperation();
    pushEdit(title, document, {});
}

void DesignCanvas::setAirwires(const QVector<QLineF>& lines) {
    airwires_ = lines;
    update();
}

void DesignCanvas::setAnnotations(const QVector<CanvasAnnotation>& annotations) {
    annotations_ = annotations;
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
        document.append(copiedItem(document, items_[index], offset));
        copies.append(static_cast<int>(document.size() - 1));
    }
    pushEdit(tr("Duplicate"), document, copies);
}

void DesignCanvas::createArray(int rows, int columns, QPointF pitch) {
    if (selection_.isEmpty() || rows < 1 || columns < 1 || rows * columns < 2) {
        return;
    }
    SketchDocument document = items_;
    QList<int> selection = selection_;
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            if (row == 0 && column == 0) continue;
            const QPointF offset(column * pitch.x(), row * pitch.y());
            for (int index : selection_) {
                document.append(copiedItem(document, items_[index], offset));
                selection.append(static_cast<int>(document.size() - 1));
            }
        }
    }
    pushEdit(tr("Create array"), document, selection);
}

QRectF DesignCanvas::selectionBounds() const {
    QRectF bounds;
    for (int index : selection_) {
        const QRectF item = itemBounds(items_[index]);
        bounds = bounds.isNull() ? item : bounds.united(item);
    }
    return bounds;
}

void DesignCanvas::rotateSelection() {
    if (selection_.isEmpty()) {
        if (tool_ == CanvasTool::Symbol || tool_ == CanvasTool::Pad) {
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
    pushEdit(tr("Move"), moveItemsKeepingConnections(items_, selection_, delta, gridSize()),
             selection_);
}

SketchDocument DesignCanvas::movedDocument(QPointF delta) const {
    switch (moveKind_) {
    case MoveKind::WireSegment:
        return dragWireSegment(items_, moveWire_, movePart_, delta, gridSize());
    case MoveKind::WireVertex:
        return dragWireVertex(items_, moveWire_, movePart_, delta, gridSize());
    case MoveKind::Selection:
        break;
    }
    return moveItemsKeepingConnections(items_, selection_, delta, gridSize());
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
        !pending_.isEmpty() && !routesWire()) {
        return &pending_.last();
    }
    return nullptr;
}

DesignCanvas::Snap DesignCanvas::snap(QPointF screen, const QPointF* origin,
                                      bool ignoreSelection) const {
    const QPointF world = screenToWorld(screen);
    const double tolerance = 10.0 / scale_;
    Snap result{world, SnapKind::None, world, {}};
    double best = tolerance;
    // A track being routed only connects to copper on its own layer.
    const bool routingTrack = workspace_ == Workspace::Board && tool_ == CanvasTool::Wire;
    auto skipped = [&](int index) {
        const SketchItem& item = items_[index];
        return (ignoreSelection && selection_.contains(index)) || !itemVisible(item) ||
               (routingTrack && (itemCopperLayers(item) & layerBit(routeLayer())) == 0);
    };

    if (snap_.objects) {
        for (int i = 0; i < items_.size(); ++i) {
            if (skipped(i)) continue;
            for (const QPointF& anchor : itemAnchors(items_[i])) {
                const double distance = QLineF(world, anchor).length();
                if (distance < best) {
                    best = distance;
                    result = {anchor, SnapKind::Object, anchor, {}};
                }
            }
        }
        if (tool_ == CanvasTool::Polyline && pending_.size() >= 3) {
            const double distance = QLineF(world, pending_.first()).length();
            if (distance < best) {
                best = distance;
                result = {pending_.first(), SnapKind::Object, pending_.first(), {}};
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
                result = {center, SnapKind::Center, center, {}};
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
                    result = {nearest, SnapKind::Edge, nearest, {}};
                }
            }
        }
    }
    if (result.kind != SnapKind::None) {
        result.marker = result.point;
        return result;
    }
    result.point = snap_.grid ? snapToGrid(world) : world;
    result.kind = snap_.grid ? SnapKind::Grid : SnapKind::None;
    if (snap_.guides) {
        QVector<QPointF> targets = guideTargets(ignoreSelection ? selection_ : QList<int>());
        targets += pending_;
        result.point += guideShift({result.point}, QPointF(), targets);
        if (origin != nullptr) {
            result.point = constrainAngle(result.point, *origin);
        }
        result.guides = guideLines({result.point}, QPointF(), targets);
    } else if (origin != nullptr) {
        result.point = constrainAngle(result.point, *origin);
    }
    result.marker = result.point;
    return result;
}

QVector<QPointF> DesignCanvas::guideTargets(const QList<int>& skipped) const {
    QVector<QPointF> targets;
    for (int i = 0; i < items_.size(); ++i) {
        if (skipped.contains(i) || !itemVisible(items_[i])) continue;
        const SketchItem& item = items_[i];
        targets += itemAnchors(item);
        if (item.kind == SketchItem::Kind::Symbol && !item.points.isEmpty()) {
            targets.append(item.points.first());
        }
    }
    return targets;
}

QPointF DesignCanvas::guideShift(const QVector<QPointF>& points, QPointF offset,
                                 const QVector<QPointF>& targets) const {
    const double tolerance = 8.0 / scale_;
    double bestX = tolerance;
    double bestY = tolerance;
    QPointF shift;
    for (const QPointF& point : points) {
        const QPointF moved = point + offset;
        for (const QPointF& target : targets) {
            const double dx = target.x() - moved.x();
            const double dy = target.y() - moved.y();
            if (std::abs(dx) < bestX) {
                bestX = std::abs(dx);
                shift.setX(dx);
            }
            if (std::abs(dy) < bestY) {
                bestY = std::abs(dy);
                shift.setY(dy);
            }
        }
    }
    return shift;
}

QVector<QLineF> DesignCanvas::guideLines(const QVector<QPointF>& points, QPointF offset,
                                         const QVector<QPointF>& targets) {
    // For every aligned coordinate keep the guide to the nearest target on that line.
    QVector<QLineF> vertical;
    QVector<QLineF> horizontal;
    auto keepNearest = [](QVector<QLineF>& lines, const QLineF& line, bool alongX) {
        for (QLineF& existing : lines) {
            const bool sameLine = alongX ? std::abs(existing.p1().y() - line.p1().y()) < 1e-6
                                         : std::abs(existing.p1().x() - line.p1().x()) < 1e-6;
            if (sameLine) {
                if (line.length() < existing.length()) existing = line;
                return;
            }
        }
        lines.append(line);
    };
    for (const QPointF& point : points) {
        const QPointF moved = point + offset;
        for (const QPointF& target : targets) {
            if (samePoint(moved, target)) continue;
            if (std::abs(target.x() - moved.x()) < 1e-6) {
                keepNearest(vertical, QLineF(target, moved), false);
            }
            if (std::abs(target.y() - moved.y()) < 1e-6) {
                keepNearest(horizontal, QLineF(target, moved), true);
            }
        }
    }
    return vertical + horizontal;
}

DesignCanvas::Placement DesignCanvas::snapPlacement(const QVector<QPointF>& points,
                                                    qsizetype connectorsFrom, QPointF rawOffset,
                                                    const QList<int>& skipped,
                                                    bool movingExisting) const {
    Placement result;
    if (points.isEmpty()) return result;
    const QPointF reference = points.first();
    QVector<QPointF> targets = guideTargets(skipped);
    // Wire ends sitting on a moving pin follow it, so they must not attract or guide the move.
    if (movingExisting) {
        targets.removeIf([&points](QPointF target) {
            return std::any_of(points.begin(), points.end(),
                               [target](QPointF point) { return samePoint(point, target); });
        });
    }

    // Pin to pin/vertex: the closest connector within reach lands exactly on its target.
    if (snap_.objects) {
        double best = 10.0 / scale_;
        for (qsizetype i = std::min(connectorsFrom, points.size() - 1); i < points.size(); ++i) {
            const QPointF moved = points[i] + rawOffset;
            for (const QPointF& target : targets) {
                const double distance = QLineF(moved, target).length();
                if (distance < best) {
                    best = distance;
                    result.offset = target - points[i];
                    result.kind = SnapKind::Object;
                    result.marker = target;
                }
            }
        }
    }
    if (result.kind == SnapKind::None) {
        result.offset = snap_.grid ? snapToGrid(reference + rawOffset) - reference : rawOffset;
        result.kind = snap_.grid ? SnapKind::Grid : SnapKind::None;
        if (snap_.guides) {
            result.offset += guideShift(points, result.offset, targets);
        }
        result.marker = reference + result.offset;
    }
    if (snap_.guides) {
        result.guides = guideLines(points, result.offset, targets);
    }
    return result;
}

DesignCanvas::Snap DesignCanvas::symbolPlacement(QPointF screen) const {
    const QPointF world = screenToWorld(screen);
    QVector<QPointF> points{world};
    if (const auto* symbol = findSymbol(variant_)) {
        SketchItem item;
        item.kind = SketchItem::Kind::Symbol;
        item.variant = variant_;
        item.points = {world};
        item.quarterTurns = placementTurns_;
        item.onBottom = workspace_ == Workspace::Board && isBottomLayer(activeLayer_);
        for (const QPointF& pin : symbol->pins) {
            points.append(symbolToWorld(item, pin));
        }
    }
    Placement placement = snapPlacement(points, 1, QPointF(), {}, false);
    if (placement.kind != SnapKind::Object && findSymbol(variant_) != nullptr) {
        SketchItem preview;
        preview.kind = SketchItem::Kind::Symbol;
        preview.variant = variant_;
        preview.points = {world + placement.offset};
        preview.quarterTurns = placementTurns_;
        preview.onBottom = workspace_ == Workspace::Board && isBottomLayer(activeLayer_);
        const QPointF shift = spacingShift({}, itemBounds(preview), world + placement.offset);
        if (!shift.isNull()) {
            placement.offset += shift;
            placement.marker += shift;
            placement.guides = guideLines(points, placement.offset, guideTargets({}));
        }
    }
    return {world + placement.offset, placement.kind, placement.marker, placement.guides};
}

QPointF DesignCanvas::spacingShift(const QList<int>& skipped, const QRectF& moving,
                                   QPointF reference) const {
    if (!snap_.guides || moving.isNull()) return {};
    const QVector<QRectF> boxes = spacingBoxes(items_, skipped);
    const NearestGaps gaps = nearestGaps(boxes, moving);
    const QVector<SpacingIndicator> references = referenceGaps(boxes);
    const double tolerance = 8.0 / scale_;
    auto onGrid = [this](double coordinate) {
        const double step = gridSize();
        return std::abs(coordinate - std::round(coordinate / step) * step) < 1e-6;
    };
    auto axisShift = [&](bool horizontal) {
        const auto& before = gaps[horizontal ? Left : Up];
        const auto& after = gaps[horizontal ? Right : Down];
        const double low = horizontal ? moving.left() : moving.top();
        const double high = horizontal ? moving.right() : moving.bottom();
        auto coordinate = [horizontal](QPointF p) { return horizontal ? p.x() : p.y(); };
        QVector<double> candidates;
        for (const SpacingIndicator& gap : references) {
            if (horizontalGap(gap) != horizontal) continue;
            if (before) candidates.append(coordinate(before->line.p1()) + gap.distance - low);
            if (after) candidates.append(coordinate(after->line.p2()) - gap.distance - high);
        }
        if (before && after) {
            candidates.append((coordinate(before->line.p1()) + coordinate(after->line.p2())) / 2.0 -
                              (low + high) / 2.0);
        }
        double best = 0.0;
        double bestDistance = tolerance;
        for (double candidate : candidates) {
            if (std::abs(candidate) >= bestDistance) continue;
            if (snap_.grid && !onGrid(coordinate(reference) + candidate)) continue;
            best = candidate;
            bestDistance = std::abs(candidate);
        }
        return best;
    };
    return {axisShift(true), axisShift(false)};
}

bool DesignCanvas::routesWire() const {
    return tool_ == CanvasTool::Wire && !freeAngle_ &&
           (workspace_ == Workspace::Schematic || snap_.orthogonal);
}

QVector<QPointF> DesignCanvas::routeTo(QPointF point) const {
    if (!routesWire() || pending_.isEmpty()) return {};
    const QPointF from = pending_.last();
    QPointF leaving;
    if (pending_.size() >= 2) {
        const QPointF run = from - pending_[pending_.size() - 2];
        if (std::abs(run.y()) < 1e-6) leaving = QPointF(std::copysign(1.0, run.x()), 0);
        else if (std::abs(run.x()) < 1e-6) leaving = QPointF(0, std::copysign(1.0, run.y()));
    } else {
        leaving = pinDirectionAt(items_, from);
    }
    return orthogonalRoute(from, point, leaving, pinDirectionAt(items_, point), gridSize());
}

void DesignCanvas::appendPathPoint(const Snap& point) {
    pending_ += routeTo(point.point);
    pending_.append(point.point);
}

int DesignCanvas::hitTest(QPointF screen) const {
    const QPointF world = screenToWorld(screen);
    const double tolerance = 6.0 / scale_;
    const bool board = workspace_ == Workspace::Board;
    const LayerView view{visibleLayers_, activeLayer_};
    auto hits = [&](const SketchItem& item) {
        if (item.kind == SketchItem::Kind::Symbol || item.kind == SketchItem::Kind::Text) {
            return itemBounds(item).adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(world);
        }
        if (item.variant == CopperZoneVariant &&
            QPolygonF(item.points).containsPoint(world, Qt::OddEvenFill)) {
            return true;
        }
        if (item.kind == SketchItem::Kind::Pad || item.kind == SketchItem::Kind::Via) {
            for (const auto& pad : itemPads(item)) {
                if (QPolygonF(padOutline(pad)).containsPoint(world, Qt::OddEvenFill)) return true;
            }
        }
        const double reach =
            tolerance + (board && item.kind == SketchItem::Kind::Wire ? trackWidth(item) / 2.0 : 0.0);
        for (const QLineF& segment : itemSegments(item)) {
            if (distanceToSegment(world, segment) <= reach) {
                return true;
            }
        }
        return false;
    };
    // On the board the active side is on top, so it is hit first.
    for (int rank = 1; rank >= (board ? 0 : 1); --rank) {
        for (int i = static_cast<int>(items_.size()) - 1; i >= 0; --i) {
            const SketchItem& item = items_[i];
            if (!itemVisible(item) || (board && drawRank(item, view) != rank)) continue;
            if (hits(item)) return i;
        }
    }
    return -1;
}

DesignCanvas::WirePart DesignCanvas::wirePartAt(int wire, QPointF screen) const {
    if (wire < 0 || wire >= items_.size() || items_[wire].kind != SketchItem::Kind::Wire) {
        return {};
    }
    const QPointF world = screenToWorld(screen);
    const QVector<QPointF>& points = items_[wire].points;
    WirePart part;
    double best = 6.0 / scale_;
    for (int i = 0; i < points.size(); ++i) {
        const double distance = QLineF(world, points[i]).length();
        if (distance <= best) {
            best = distance;
            part = {MoveKind::WireVertex, i};
        }
    }
    if (part.kind == MoveKind::WireVertex) {
        return part;
    }
    best = std::numeric_limits<double>::infinity();
    for (int i = 1; i < points.size(); ++i) {
        const double distance = distanceToSegment(world, QLineF(points[i - 1], points[i]));
        if (distance < best) {
            best = distance;
            part = {MoveKind::WireSegment, i - 1};
        }
    }
    return part;
}

void DesignCanvas::updateSelectCursor(QPointF screen) {
    const int hit = hitTest(screen);
    Qt::CursorShape shape = Qt::ArrowCursor;
    if (hit >= 0 && items_[hit].kind == SketchItem::Kind::Wire &&
        !(selection_.size() > 1 && selection_.contains(hit))) {
        const WirePart part = wirePartAt(hit, screen);
        if (part.kind == MoveKind::WireVertex) {
            shape = Qt::SizeAllCursor;
        } else if (part.kind == MoveKind::WireSegment) {
            const QPointF a = items_[hit].points[part.index];
            const QPointF b = items_[hit].points[part.index + 1];
            shape = std::abs(a.y() - b.y()) < 1e-6   ? Qt::SizeVerCursor
                    : std::abs(a.x() - b.x()) < 1e-6 ? Qt::SizeHorCursor
                                                     : Qt::SizeAllCursor;
        }
    }
    if (cursor().shape() != shape) {
        setCursor(shape);
    }
}

void DesignCanvas::placeSymbol(QPointF world) {
    const auto* symbol = findSymbol(variant_);
    if (symbol == nullptr) {
        emit statusMessage(tr("Choose an object from the list first."));
        return;
    }
    SketchItem item;
    if (placementTemplate_ && placementTemplate_->variant == variant_) {
        const QString id = item.id;
        item = *placementTemplate_;
        item.id = id;
    } else {
        item.label = symbol->prefix.isEmpty() ? symbol->defaultLabel
                                              : nextDesignator(items_, symbol->prefix);
        item.value = symbol->defaultValue;
        const auto* footprint = findSymbol(symbol->defaultFootprint);
        if (footprint != nullptr && footprint->pins.size() == symbol->pins.size()) {
            item.footprint = footprint->id;
            if (symbol->defaultPinPadMap.size() == symbol->pins.size()) {
                item.pinPadMap = symbol->defaultPinPadMap;
            } else {
                for (int pad = 1; pad <= symbol->pins.size(); ++pad) item.pinPadMap.append(pad);
            }
        }
    }
    item.kind = SketchItem::Kind::Symbol;
    item.points = {world};
    item.variant = variant_;
    item.quarterTurns = placementTurns_;
    item.onBottom = workspace_ == Workspace::Board && isBottomLayer(activeLayer_);
    SketchDocument document = items_;
    document.append(item);
    pushEdit(tr("Place %1").arg(item.label.isEmpty() ? symbolDisplayName(*symbol) : item.label),
             document, {});
}

void DesignCanvas::placePad(QPointF world) {
    const auto style = findPadStyleEntry(variant_);
    if (!style) {
        emit statusMessage(tr("Choose a pad from the list first."));
        return;
    }
    SketchItem item;
    item.kind = SketchItem::Kind::Pad;
    item.points = {world};
    item.pad = style->pad;
    item.quarterTurns = placementTurns_;
    item.layer = item.pad.drillDiameter > 0.0 ? BoardLayer::TopCopper : routeLayer();
    int number = 0;
    for (const auto& other : items_) {
        if (other.kind == SketchItem::Kind::Pad) number = std::max(number, other.pad.number);
    }
    item.pad.number = number + 1;
    SketchDocument document = items_;
    document.append(item);
    pushEdit(tr("Place pad %1").arg(item.pad.number), document, {});
}

void DesignCanvas::placeVia(QPointF world) {
    SketchDocument document = items_;
    document.append(pendingVia(world));
    pushEdit(tr("Place via"), document, {});
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
    if (workspace_ == Workspace::Board) item.layer = graphicsLayer();
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
        emit statusMessage(measurementText(measurement_, unit_));
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
    if (workspace_ == Workspace::Board) item.layer = graphicsLayer();
    SketchDocument document = items_;
    document.append(item);
    pushEdit(text, document, {});
}

void DesignCanvas::finishPath() {
    QVector<QPointF> points = pending_;
    pending_.clear();
    if (tool_ == CanvasTool::Wire && workspace_ == Workspace::Schematic) {
        // A corner placed on another wire joins it (T junction) instead of silently crossing.
        const auto pieces = splitPathAtWires(items_, points);
        if (pieces.size() > 1) {
            SketchDocument document = items_;
            for (QVector<QPointF> piece : pieces) {
                simplifyPath(piece);
                if (piece.size() < 2) continue;
                SketchItem item;
                item.kind = SketchItem::Kind::Wire;
                item.points = piece;
                item.variant = variant_;
                document.append(item);
            }
            pushEdit(tr("Draw wire"), document, {});
            return;
        }
    }
    if (tool_ == CanvasTool::Wire && workspace_ == Workspace::Board) {
        // Pieces already routed on other layers, their vias and the last piece form one edit.
        SketchDocument pieces = routePieces_;
        routePieces_.clear();
        SketchItem track = pendingTrack(points);
        if (track.points.size() >= 2) pieces.append(track);
        if (pieces.isEmpty() || (pieces.size() == 1 && pieces.first().kind == SketchItem::Kind::Via)) {
            update();
            return;
        }
        pushEdit(tr("Route track"), items_ + pieces, {});
        return;
    }
    if (tool_ == CanvasTool::Wire) {
        simplifyPath(points);
    }
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
    if (workspace_ == Workspace::Board) {
        item.layer = variant_ == BoardOutlineVariant ? BoardLayer::BoardEdge
                     : variant_ == CopperZoneVariant ? routeLayer()
                                                     : graphicsLayer();
    }
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
        if (tool_ != CanvasTool::Select || hasPendingOperation() || hasMeasurement_) {
            cancelOperation();
            setTool(CanvasTool::Select);
            emit selectToolRequested();
            return;
        }
        if (!contextMenuTimer_) {
            contextMenuTimer_ = new QTimer(this);
            contextMenuTimer_->setSingleShot(true);
            connect(contextMenuTimer_, &QTimer::timeout, this, [this] {
                emit contextMenuRequested(contextMenuPosition_, contextMenuItem_);
            });
        }
        contextMenuPosition_ = event->globalPosition().toPoint();
        contextMenuItem_ = hitTest(position);
        contextMenuTimer_->start(QApplication::doubleClickInterval());
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (contextMenuTimer_) contextMenuTimer_->stop();

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
            moveDocument_.clear();
            moveKind_ = MoveKind::Selection;
            moveWire_ = -1;
            movePart_ = -1;
            // A lone wire is reshaped (ISIS style) instead of being lifted off its pins.
            if (items_[hit].kind == SketchItem::Kind::Wire && selection_.size() == 1) {
                const WirePart part = wirePartAt(hit, position);
                if (part.kind != MoveKind::Selection) {
                    moveKind_ = part.kind;
                    moveWire_ = hit;
                    movePart_ = part.index;
                    moveReference_ = items_[hit].points.value(part.index);
                }
            }
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
        placeSymbol(symbolPlacement(position).point);
        break;
    case CanvasTool::Pad:
        placePad(snap(position).point);
        break;
    case CanvasTool::Via:
        placeVia(snap(position).point);
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
        freeAngle_ = event->modifiers() & Qt::ControlModifier;
        const Snap point = snap(position, constraintOrigin());
        if (pending_.isEmpty()) {
            pending_.append(point.point);
        } else if (samePoint(point.point, pending_.last())) {
            finishPath();
        } else {
            const bool closing = tool_ == CanvasTool::Polyline && pending_.size() >= 3 &&
                                 samePoint(point.point, pending_.first());
            appendPathPoint(point);
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
            if (workspace_ == Workspace::Board) item.layer = graphicsLayer();
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
        QPointF raw = screenToWorld(position) - dragStartWorld_;
        const bool axisLock = event->modifiers() & Qt::ShiftModifier;
        const bool horizontalLock = std::abs(raw.x()) >= std::abs(raw.y());
        if (axisLock) {
            raw = horizontalLock ? QPointF(raw.x(), 0) : QPointF(0, raw.y());
        }
        if (moveKind_ == MoveKind::Selection) {
            QVector<QPointF> points{moveReference_};
            for (int index : selection_) {
                const SketchItem& item = items_[index];
                if (item.kind == SketchItem::Kind::Symbol || item.kind == SketchItem::Kind::Wire) {
                    points += itemAnchors(item);
                }
            }
            const Placement placement =
                snapPlacement(points, points.size() > 1 ? 1 : 0, raw, selection_, true);
            moveDelta_ = placement.offset;
            moveGuides_ = placement.guides;
            moveMarker_ = placement.marker;
            moveJoined_ = placement.kind == SnapKind::Object;
            // Pin joins win; otherwise match an existing gap (equal spacing) when close to one.
            QRectF moving;
            for (int index : selection_) {
                const SketchItem& item = items_[index];
                if (item.kind == SketchItem::Kind::Wire || item.points.isEmpty()) continue;
                const QRectF bounds = itemBounds(item).translated(moveDelta_);
                moving = moving.isNull() ? bounds : moving.united(bounds);
            }
            const QPointF shift =
                moveJoined_ ? QPointF() : spacingShift(selection_, moving, moveReference_ + moveDelta_);
            if (!shift.isNull()) {
                moveDelta_ += shift;
                moveMarker_ += shift;
                QVector<QPointF> targets = guideTargets(selection_);
                targets.removeIf([&points](QPointF target) {
                    return std::any_of(points.begin(), points.end(),
                                       [target](QPointF point) { return samePoint(point, target); });
                });
                moveGuides_ = snap_.guides ? guideLines(points, moveDelta_, targets) : QVector<QLineF>();
            }
        } else {
            const Snap target = snap(worldToScreen(moveReference_ + raw), nullptr, true);
            moveDelta_ = target.point - moveReference_;
            moveGuides_ = target.guides;
            moveMarker_ = target.marker;
            moveJoined_ = target.kind == SnapKind::Object;
        }
        if (axisLock) {
            moveDelta_ = horizontalLock ? QPointF(moveDelta_.x(), 0) : QPointF(0, moveDelta_.y());
            moveJoined_ = false;
            moveGuides_ = guideLines({moveReference_}, moveDelta_,
                                     guideTargets(selection_));
        }
        moveDocument_ = movedDocument(moveDelta_);
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
    if (tool_ == CanvasTool::Select) {
        updateSelectCursor(position);
    }
    freeAngle_ = event->modifiers() & Qt::ControlModifier;
    hover_ = tool_ == CanvasTool::Symbol ? symbolPlacement(position)
                                         : snap(position, constraintOrigin());
    hoverValid_ = true;
    emit cursorMoved(hover_.point);
    if (tool_ == CanvasTool::Measure && pending_.size() == 1) {
        emit statusMessage(measurementText(QLineF(pending_.first(), hover_.point), unit_));
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
        const SketchDocument document =
            moveDelta_.isNull() ? SketchDocument() : std::move(moveDocument_);
        const bool reshaped = moveKind_ != MoveKind::Selection;
        moveDelta_ = {};
        moveDocument_.clear();
        moveGuides_.clear();
        if (!document.isEmpty() && !sameGeometry(document, items_)) {
            const QString text = !reshaped                        ? tr("Move")
                                 : workspace_ == Workspace::Board ? tr("Drag track")
                                                                  : tr("Drag wire");
            pushEdit(text, document, selection_);
        }
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
                if (worldBand.intersects(bounds) && itemVisible(items_[i])) {
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
    if (event->button() == Qt::RightButton && contextMenuTimer_ &&
        contextMenuTimer_->isActive() && tool_ == CanvasTool::Select) {
        contextMenuTimer_->stop();
        const int hit = hitTest(event->position());
        if (hit >= 0 && hit == contextMenuItem_) {
            setSelection({hit});
            deleteSelection();
        }
        return;
    }
    if (event->button() == Qt::LeftButton && isPathTool(tool_)) {
        if (!pending_.isEmpty()) {
            const Snap point = snap(event->position(), constraintOrigin());
            if (!samePoint(point.point, pending_.last())) {
                appendPathPoint(point);
            }
            finishPath();
        }
        return;
    }
    mousePressEvent(event);
}

void DesignCanvas::contextMenuEvent(QContextMenuEvent* event) {
    // Mouse context events are generated separately by Windows after right release.
    // Our timer arbitrates those gestures; only keyboard requests bypass it.
    event->accept();
    if (event->reason() != QContextMenuEvent::Keyboard) return;
    if (contextMenuTimer_) contextMenuTimer_->stop();
    if (tool_ != CanvasTool::Select || hasPendingOperation()) {
        setTool(CanvasTool::Select);
        emit selectToolRequested();
    }
    const int index = selection_.isEmpty() ? -1 : selection_.first();
    const QPoint position = index >= 0 ? worldToScreen(items_[index].points.value(0)).toPoint()
                                      : rect().center();
    emit contextMenuRequested(mapToGlobal(position), index);
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

void DesignCanvas::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Control && freeAngle_) {
        freeAngle_ = false;
        update();
    }
    QWidget::keyReleaseEvent(event);
}

void DesignCanvas::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Control:
        if (tool_ == CanvasTool::Wire && !freeAngle_) {
            freeAngle_ = true;
            update();
        }
        break;
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
        if (isPathTool(tool_) && pending_.size() == 1 && revertRouteLayerChange()) {
            emit activeLayerChanged(activeLayer_);
            update();
            return;
        }
        if (isPathTool(tool_) && !pending_.isEmpty()) {
            pending_.removeLast();
            update();
            return;
        }
        break;
    case Qt::Key_Space:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        if (workspace_ == Workspace::Board && !event->isAutoRepeat()) {
            // Proteus ARES style: Space flips between the copper layers, Page Up/Down pick one.
            setActiveLayer(event->key() == Qt::Key_PageUp     ? BoardLayer::TopCopper
                           : event->key() == Qt::Key_PageDown ? BoardLayer::BottomCopper
                                                              : oppositeSideLayer(routeLayer()));
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
    painter.setPen(strokePen(colors.preview, 1.2, true));
    for (const auto& line : airwires_) painter.drawLine(QLineF(map(line.p1()), map(line.p2())));
    const bool dragging = drag_ == Drag::Move && moveDocument_.size() == items_.size();
    const SketchDocument& shown = dragging ? moveDocument_ : items_;
    const LayerView view{visibleLayers_, activeLayer_};
    // Board: the inactive side first, then the active side; hidden layers are skipped.
    for (int rank = board ? 0 : 1; rank <= 1; ++rank) {
        for (int i = 0; i < shown.size(); ++i) {
            if (board && (drawRank(shown[i], view) != rank || !itemVisible(shown[i]))) continue;
            drawItem(painter, shown[i], colors, board, selection_.contains(i), false, scale_, map, view);
        }
    }
    for (const auto& piece : routePieces_) {
        drawItem(painter, piece, colors, board, false, false, scale_, map, view);
    }
    // Board copper joins are shown by pads and vias, so only schematic joins need a visible dot.
    const QVector<QPointF> junctions = board ? QVector<QPointF>{} : schematicJunctions(shown);
    drawJunctionDots(painter, junctions, colors.wire, scale_, map);

    // Simulation readouts (e.g. probe voltages): a filled tag next to the point.
    if (!annotations_.isEmpty()) {
        QFont font = painter.font();
        font.setPixelSize(12);
        font.setBold(true);
        painter.setFont(font);
        const QFontMetricsF metrics(font);
        for (const auto& annotation : annotations_) {
            const QPointF anchor = map(annotation.position);
            const QRectF tag(anchor + QPointF(10, -26),
                             QSizeF(metrics.horizontalAdvance(annotation.text) + 12, metrics.height() + 6));
            painter.setPen(QPen(colors.guide, 1.2));
            painter.drawLine(anchor, QPointF(tag.left(), tag.bottom()));
            painter.setBrush(colors.background);
            painter.drawRoundedRect(tag, 3, 3);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(colors.guide);
            painter.drawText(tag, Qt::AlignCenter, annotation.text);
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
        const QString text = formatLength(line.length(), unit_);
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
                preview.onBottom = board && isBottomLayer(activeLayer_);
                hasPreview = true;
            }
            break;
        case CanvasTool::Pad:
            if (const auto style = findPadStyleEntry(variant_)) {
                preview.kind = SketchItem::Kind::Pad;
                preview.points = {hover_.point};
                preview.pad = style->pad;
                preview.pad.number = 0;
                preview.quarterTurns = placementTurns_;
                preview.layer = routeLayer();
                hasPreview = true;
            }
            break;
        case CanvasTool::Via:
            preview = pendingVia(hover_.point);
            hasPreview = true;
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
                preview.points = pending_ + routeTo(hover_.point);
                preview.points.append(hover_.point);
                preview.variant = variant_;
                if (board && tool_ == CanvasTool::Wire) {
                    // Drawn in the colour of its layer so the active copper layer is obvious.
                    preview.layer = routeLayer();
                    preview.width = trackWidth_;
                    drawItem(painter, preview, colors, board, false, false, scale_, map, view);
                } else {
                    hasPreview = true;
                }
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
            drawItem(painter, preview, colors, board, false, true, scale_, map, view);
        }
        if (hasPreview && tool_ == CanvasTool::Wire && !board) {
            // Show where the wire being drawn will join, before it is committed.
            SketchDocument joined = items_;
            for (const auto& piece : splitPathAtWires(items_, preview.points)) {
                SketchItem wire = preview;
                wire.points = piece;
                joined.append(wire);
            }
            QVector<QPointF> added;
            for (const QPointF& point : schematicJunctions(joined)) {
                const bool existing = std::any_of(junctions.begin(), junctions.end(), [&](QPointF p) {
                    return QLineF(p, point).length() < 1e-6;
                });
                if (!existing) added.append(point);
            }
            drawJunctionDots(painter, added, colors.preview, scale_, map);
        }
    }
    if (hasMeasurement_) {
        drawMeasurement(measurement_);
    }

    const QVector<QLineF> guides = activeGuides();
    if (!guides.isEmpty()) {
        painter.setPen(strokePen(colors.guide, 1.0, true));
        for (const QLineF& guide : guides) {
            QLineF line(map(guide.p1()), map(guide.p2()));
            const double length = line.length();
            if (length < 1e-6) continue;
            const QPointF extension = (line.p2() - line.p1()) * (14.0 / length);
            painter.drawLine(QLineF(line.p1() - extension, line.p2() + extension));
            painter.drawLine(line.p1() - QPointF(3, 3), line.p1() + QPointF(3, 3));
            painter.drawLine(line.p1() - QPointF(3, -3), line.p1() + QPointF(3, -3));
        }
    }
    const QVector<SpacingIndicator> spacings = activeSpacings();
    if (!spacings.isEmpty()) {
        QFont font = painter.font();
        font.setPixelSize(11);
        painter.setFont(font);
        const QFontMetricsF metrics(font);
        for (const SpacingIndicator& spacing : spacings) {
            const QPointF a = map(spacing.line.p1());
            const QPointF b = map(spacing.line.p2());
            const bool horizontal = std::abs(a.y() - b.y()) < std::abs(a.x() - b.x());
            // Equal gaps are emphasised with a heavier line and longer ticks.
            const double tickLength = spacing.equal ? 6.0 : 4.0;
            const QPointF tick = horizontal ? QPointF(0, tickLength) : QPointF(tickLength, 0);
            painter.setPen(strokePen(colors.guide, spacing.equal ? 2.2 : 1.0));
            painter.drawLine(a, b);
            painter.drawLine(a - tick, a + tick);
            painter.drawLine(b - tick, b + tick);
            const QString text = formatLength(spacing.distance, unit_);
            const QSizeF size(metrics.horizontalAdvance(text) + 10, metrics.height() + 4);
            // Beside the line so the label never covers the gap it measures.
            const QPointF centre = QLineF(a, b).center() +
                                   (horizontal ? QPointF(0, -size.height() / 2 - 5)
                                               : QPointF(size.width() / 2 + 5, 0));
            const QRectF label(centre - QPointF(size.width() / 2, size.height() / 2), size);
            painter.setPen(Qt::NoPen);
            painter.setBrush(colors.guide);
            painter.drawRoundedRect(label, 3, 3);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(colors.background);
            painter.drawText(label, Qt::AlignCenter, text);
        }
    }
    if (drag_ == Drag::Move && moveJoined_ && !moveDelta_.isNull()) {
        painter.setPen(strokePen(colors.preview, 1.5));
        painter.drawRect(QRectF(map(moveMarker_) - QPointF(6, 6), QSizeF(12, 12)));
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
        const QPointF point = map(hover_.marker);
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
                     tr("%1  ·  Grid %2  ·  %3%")
                         .arg(!board ? tr("Schematic sheet")
                              : tool_ == CanvasTool::Wire
                                  ? tr("PCB layout  ·  %1  ·  Track %2")
                                        .arg(boardLayerName(routeLayer()), formatLength(trackWidth_, unit_))
                              : tool_ == CanvasTool::Via
                                  ? tr("PCB layout  ·  %1  ·  Via %2 / %3")
                                        .arg(boardLayerName(activeLayer_), formatLength(viaDiameter_, unit_),
                                             formatLength(viaDrill_, unit_))
                                  : tr("PCB layout  ·  %1").arg(boardLayerName(activeLayer_)))
                         .arg(formatLength(gridSize(), unit_))
                         .arg(zoomPercent()));
}

namespace {

// Draws `item` scaled to fit `target` (symbol and pad previews in the object selector).
void paintItemPreview(QPainter& painter, const QRectF& target, const SketchItem& item,
                      Workspace workspace, const QPalette& palette) {
    QRectF bounds = itemBounds(item);
    bounds.adjust(-0.6, -0.6, 0.6, 0.6);
    const double scale = std::min(target.width() / bounds.width(), target.height() / bounds.height());
    const QPointF offset = target.center() - bounds.center() * scale;
    const CanvasColors colors = canvasColors(workspace, palette);
    const WorldToScreen map = [scale, offset](QPointF point) { return point * scale + offset; };
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (item.kind == SketchItem::Kind::Symbol) {
        if (const auto* symbol = findSymbol(item.variant)) {
            const QColor stroke = workspace == Workspace::Board
                                      ? colors.layers[static_cast<int>(BoardLayer::TopSilk)]
                                      : colors.stroke;
            drawSymbol(painter, *symbol, map, colors, stroke, 1.4, false, false);
        }
        if (workspace == Workspace::Board) {
            QVector<PlacedPad> pads = itemPads(item);
            for (auto& pad : pads) pad.number = 0;
            drawPads(painter, pads, colors, {}, false, false, scale, map);
        }
    } else {
        QVector<PlacedPad> pads = itemPads(item);
        for (auto& pad : pads) pad.number = 0;
        drawPads(painter, pads, colors, {}, false, false, scale, map);
    }
    painter.restore();
}

} // namespace

void DesignCanvas::paintSymbolPreview(QPainter& painter, const QRectF& target,
                                      const QString& symbolId, const QPalette& palette) {
    const auto* symbol = findSymbol(symbolId);
    if (symbol == nullptr || target.isEmpty()) {
        return;
    }
    SketchItem item;
    item.kind = SketchItem::Kind::Symbol;
    item.variant = symbolId;
    item.points = {QPointF()};
    paintItemPreview(painter, target, item, symbol->workspace, palette);
}

void DesignCanvas::paintPadPreview(QPainter& painter, const QRectF& target,
                                   const QString& padStyleId, const QPalette& palette) {
    if (target.isEmpty()) return;
    SketchItem item;
    item.points = {QPointF()};
    if (const auto style = findPadStyleEntry(padStyleId)) {
        item.kind = SketchItem::Kind::Pad;
        item.pad = style->pad;
    } else {
        item.kind = SketchItem::Kind::Via;
    }
    paintItemPreview(painter, target, item, Workspace::Board, palette);
}

} // namespace hatt::ui
