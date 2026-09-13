#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QLineF>
#include <QList>
#include <QPointF>
#include <QString>
#include <QWidget>

class QPainter;
class QUndoStack;
class QTimer;

namespace hatt::ui {

enum class CanvasTool { Select, Symbol, Wire, Line, Polyline, Rectangle, Circle, Arc, Text, Measure };

enum class AlignOperation {
    Left,
    HorizontalCenter,
    Right,
    Top,
    VerticalCenter,
    Bottom,
    DistributeHorizontally,
    DistributeVertically
};

struct SnapSettings {
    bool grid = true;
    bool objects = true;
    bool edges = false;
    bool centers = false;
    bool diagonal = true;
    bool orthogonal = false;
};

class DesignCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit DesignCanvas(Workspace workspace, QWidget* parent = nullptr);
    ~DesignCanvas() override;

    [[nodiscard]] Workspace workspace() const noexcept { return workspace_; }
    [[nodiscard]] QUndoStack* undoStack() const noexcept { return undoStack_; }
    [[nodiscard]] const SketchDocument& document() const noexcept { return items_; }
    [[nodiscard]] QList<int> selection() const { return selection_; }
    [[nodiscard]] CanvasTool tool() const noexcept { return tool_; }
    [[nodiscard]] QString toolVariant() const { return variant_; }
    [[nodiscard]] QString toolHint() const;
    [[nodiscard]] double gridSize() const noexcept;
    [[nodiscard]] int zoomPercent() const noexcept;
    [[nodiscard]] bool hasPendingOperation() const noexcept;
    [[nodiscard]] QPointF worldToScreen(QPointF world) const;
    [[nodiscard]] QPointF screenToWorld(QPointF screen) const;

    void setTool(CanvasTool tool, const QString& variant = {});
    void setSnapSettings(const SnapSettings& settings);
    void cancelOperation();

    void selectAll();
    void clearSelection();
    void deleteSelection();
    void duplicateSelection();
    void rotateSelection();
    void selectItem(int index);
    void editItemProperties(int index, const QString& label, QPointF position, int quarterTurns);
    void editItemProperties(int index, const SketchItem& properties);
    void align(AlignOperation operation);

    void zoomIn();
    void zoomOut();
    void zoomToFit();

    // Replaces document and selection without creating an undo entry. Used by undo commands.
    void restore(const SketchDocument& document, const QList<int>& selection);
    void applyDocumentEdit(const QString& title, const SketchDocument& document);
    void setAirwires(const QVector<QLineF>& lines);
    [[nodiscard]] QVector<QLineF> airwires() const { return airwires_; }

    static void paintSymbolPreview(QPainter& painter, const QRectF& target, const QString& symbolId,
                                   const QPalette& palette);

signals:
    void documentChanged();
    void selectionChanged(int count);
    void cursorMoved(QPointF world);
    void statusMessage(const QString& message);
    void zoomChanged(int percent);
    void selectToolRequested();
    void contextMenuRequested(QPoint globalPosition, int itemIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class SnapKind { None, Grid, Object, Center, Edge };
    enum class Drag { None, Move, RubberBand, Pan };

    struct Snap {
        QPointF point;
        SnapKind kind = SnapKind::None;
    };

    [[nodiscard]] Snap snap(QPointF screen, const QPointF* constraintOrigin = nullptr,
                            bool ignoreSelection = false) const;
    [[nodiscard]] QPointF snapToGrid(QPointF world) const;
    [[nodiscard]] QPointF constrainAngle(QPointF point, QPointF origin) const;
    [[nodiscard]] const QPointF* constraintOrigin() const;
    [[nodiscard]] int hitTest(QPointF screen) const;
    [[nodiscard]] double defaultScale() const noexcept;

    void pushEdit(const QString& text, const SketchDocument& document, const QList<int>& selection);
    void setSelection(QList<int> selection);
    void placeSymbol(QPointF world);
    void placeText(QPointF world);
    void finishTwoPoint(QPointF world);
    void finishPath();
    void nudgeSelection(QPointF delta);
    void zoomAround(QPointF screen, double factor);

    Workspace workspace_;
    QUndoStack* undoStack_;
    SketchDocument items_;
    QVector<QLineF> airwires_;
    QList<int> selection_;
    CanvasTool tool_ = CanvasTool::Select;
    QString variant_;
    SnapSettings snap_;
    int placementTurns_ = 0;

    double scale_;
    QPointF offset_{48.0, 48.0};

    QVector<QPointF> pending_;
    bool pressGesture_ = false;
    QPointF pressScreen_;

    Drag drag_ = Drag::None;
    QPointF dragStartScreen_;
    QPointF dragCurrentScreen_;
    QPointF dragStartWorld_;
    QPointF moveReference_;
    QPointF moveDelta_;
    QPointF panStartOffset_;

    bool hoverValid_ = false;
    Snap hover_;
    bool hasMeasurement_ = false;
    QLineF measurement_;
    QTimer* contextMenuTimer_ = nullptr;
    QPoint contextMenuPosition_;
    int contextMenuItem_ = -1;
};

} // namespace hatt::ui
