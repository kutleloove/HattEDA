#pragma once

#include "hatt/ui/SketchModel.hpp"
#include "hatt/ui/Units.hpp"

#include <QLineF>
#include <QList>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <optional>

class QPainter;
class QUndoStack;
class QTimer;

namespace hatt::ui {

// Pad places a board pad of the PadStyle named by the tool variant; Via places a via.
enum class CanvasTool {
    Select,
    Symbol,
    Wire,
    Line,
    Polyline,
    Rectangle,
    Circle,
    Arc,
    Text,
    Measure,
    Pad,
    Via
};

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
    // Smart alignment guides against other objects' pins, vertices and origins.
    bool guides = true;
    // Grid step level 0..3 (Proteus style Ctrl+F1, F2, F3, F4); see DesignCanvas::gridStep.
    int gridLevel = 2;
};

// Gap between the object being moved or placed and its nearest neighbour in one direction,
// shown as a labelled dimension line (world coordinates, millimetres). `equal` marks gaps that
// match another gap; those matching gaps between other objects are reported too.
struct SpacingIndicator {
    QLineF line;
    double distance = 0.0;
    bool equal = false;
};

struct CanvasAnnotation {
    QPointF position; // world, millimetres
    QString text;
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
    [[nodiscard]] QVector<QLineF> activeGuides() const;
    // Left/right/up/down gaps while a selection is dragged or a symbol is being placed.
    [[nodiscard]] QVector<SpacingIndicator> activeSpacings() const;
    [[nodiscard]] LengthUnit lengthUnit() const noexcept { return unit_; }
    // Union of the selected items' bounds, or a null rectangle without a selection.
    [[nodiscard]] QRectF selectionBounds() const;

    static constexpr int GridLevelCount = 4;
    // Grid step in millimetres for a level: schematic 0.254 / 1.27 / 2.54 / 12.7,
    // board 0.127 / 0.254 / 0.635 / 1.27.
    [[nodiscard]] static double gridStep(Workspace workspace, int level);

    void setTool(CanvasTool tool, const QString& variant = {});
    // Symbol tool only: the next placements copy `item` (label, value, footprint links) instead of
    // creating a fresh part; each copy gets a new identity. Cleared by setTool.
    void setPlacementTemplate(const SketchItem& item);
    void setSnapSettings(const SnapSettings& settings);
    void setLengthUnit(LengthUnit unit);
    void cancelOperation();

    void selectAll();
    void clearSelection();
    void deleteSelection();
    void duplicateSelection();
    // Copies the selection into a rows × columns array (one undo step). The selection is the
    // top-left cell; `pitch` is the centre-to-centre step (Y positive down). Copies get fresh
    // identities and designators in row order, and everything ends up selected.
    void createArray(int rows, int columns, QPointF pitch);
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
    // Read-only overlay labels such as simulated probe voltages; not part of the document.
    void setAnnotations(const QVector<CanvasAnnotation>& annotations);
    [[nodiscard]] QVector<CanvasAnnotation> annotations() const { return annotations_; }

    static void paintSymbolPreview(QPainter& painter, const QRectF& target, const QString& symbolId,
                                   const QPalette& palette);
    // Draws a pad style (or a via when `padStyleId` is empty) centred in `target`.
    static void paintPadPreview(QPainter& painter, const QRectF& target, const QString& padStyleId,
                                const QPalette& palette);

    // Board layers (Kayra). New tracks, zones and SMD pads go to the active copper layer (a copper
    // layer on the active layer's side when a non-copper layer is active); graphics go to the
    // active layer, or the silk layer of its side while a copper layer is active; footprints are
    // placed on the bottom side while a bottom layer is active. Changing the copper layer while a
    // track is being routed inserts a via at the last corner. Hidden layers are neither drawn nor
    // hit-tested. Ignored by schematic canvases.
    [[nodiscard]] BoardLayer activeLayer() const noexcept { return activeLayer_; }
    void setActiveLayer(BoardLayer layer);
    [[nodiscard]] int visibleLayers() const noexcept { return visibleLayers_; }
    void setVisibleLayers(int mask);
    [[nodiscard]] double trackWidthSetting() const noexcept { return trackWidth_; }
    void setTrackWidth(double millimetres);
    void setViaSize(double diameter, double drill);
    // Layer colour for the palette's theme (dark or light).
    [[nodiscard]] static QColor layerColor(BoardLayer layer, const QPalette& palette);

signals:
    void activeLayerChanged(hatt::ui::BoardLayer layer);
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
    void keyReleaseEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    enum class SnapKind { None, Grid, Object, Center, Edge };
    enum class Drag { None, Move, RubberBand, Pan };
    // What a Drag::Move edits: the whole selection, or one segment/vertex of a single wire.
    enum class MoveKind { Selection, WireSegment, WireVertex };

    struct Snap {
        QPointF point;
        SnapKind kind = SnapKind::None;
        QPointF marker;           // where the snap indicator is drawn
        QVector<QLineF> guides;   // alignment guides, world coordinates
    };

    // Result of snapping a group of points that move together (placement preview or drag).
    struct Placement {
        QPointF offset;
        SnapKind kind = SnapKind::None;
        QPointF marker;
        QVector<QLineF> guides;
    };

    struct WirePart {
        MoveKind kind = MoveKind::Selection;
        int index = -1;
    };

    [[nodiscard]] Snap snap(QPointF screen, const QPointF* constraintOrigin = nullptr,
                            bool ignoreSelection = false) const;
    [[nodiscard]] Placement snapPlacement(const QVector<QPointF>& points, qsizetype connectorsFrom,
                                          QPointF rawOffset, const QList<int>& skipped,
                                          bool movingExisting) const;
    [[nodiscard]] Snap symbolPlacement(QPointF screen) const;
    // Shift (within 8 px) that makes a gap of `moving` equal an existing gap between other
    // objects, or centres it between two neighbours. With grid snap on only shifts that keep
    // `reference` on the grid are used. Null when nothing matches or guides are off.
    [[nodiscard]] QPointF spacingShift(const QList<int>& skipped, const QRectF& moving,
                                       QPointF reference) const;
    [[nodiscard]] QVector<QPointF> guideTargets(const QList<int>& skipped) const;
    [[nodiscard]] QPointF guideShift(const QVector<QPointF>& points, QPointF offset,
                                     const QVector<QPointF>& targets) const;
    [[nodiscard]] static QVector<QLineF> guideLines(const QVector<QPointF>& points, QPointF offset,
                                                    const QVector<QPointF>& targets);
    [[nodiscard]] bool routesWire() const;
    [[nodiscard]] QVector<QPointF> routeTo(QPointF point) const;
    void appendPathPoint(const Snap& point);
    [[nodiscard]] QPointF snapToGrid(QPointF world) const;
    [[nodiscard]] QPointF constrainAngle(QPointF point, QPointF origin) const;
    [[nodiscard]] const QPointF* constraintOrigin() const;
    [[nodiscard]] int hitTest(QPointF screen) const;
    [[nodiscard]] WirePart wirePartAt(int wire, QPointF screen) const;
    [[nodiscard]] SketchDocument movedDocument(QPointF delta) const;
    [[nodiscard]] double defaultScale() const noexcept;
    void updateSelectCursor(QPointF screen);

    void pushEdit(const QString& text, const SketchDocument& document, const QList<int>& selection);
    void setSelection(QList<int> selection);
    void placeSymbol(QPointF world);
    void placePad(QPointF world);
    void placeVia(QPointF world);
    [[nodiscard]] bool itemVisible(const SketchItem& item) const;
    // Copper layer used for tracks, zones and SMD pads.
    [[nodiscard]] BoardLayer routeLayer() const noexcept;
    [[nodiscard]] BoardLayer graphicsLayer() const noexcept;
    [[nodiscard]] SketchItem pendingTrack(const QVector<QPointF>& points) const;
    [[nodiscard]] SketchItem pendingVia(QPointF at) const;
    // Undoes the last layer change of the route being drawn; false when there was none.
    bool revertRouteLayerChange();
    void placeText(QPointF world);
    void finishTwoPoint(QPointF world);
    void finishPath();
    void nudgeSelection(QPointF delta);
    void zoomAround(QPointF screen, double factor);

    Workspace workspace_;
    QUndoStack* undoStack_;
    SketchDocument items_;
    QVector<QLineF> airwires_;
    QVector<CanvasAnnotation> annotations_;
    QList<int> selection_;
    CanvasTool tool_ = CanvasTool::Select;
    QString variant_;
    std::optional<SketchItem> placementTemplate_;
    SnapSettings snap_;
    LengthUnit unit_;
    int placementTurns_ = 0;

    double scale_;
    QPointF offset_{48.0, 48.0};

    QVector<QPointF> pending_;
    // Tracks and vias of the route being drawn that are already on another layer.
    SketchDocument routePieces_;
    BoardLayer activeLayer_ = BoardLayer::TopCopper;
    int visibleLayers_ = AllLayersMask;
    double trackWidth_ = DefaultTrackWidth;
    double viaDiameter_ = DefaultViaDiameter;
    double viaDrill_ = DefaultViaDrill;
    bool pressGesture_ = false;
    QPointF pressScreen_;

    Drag drag_ = Drag::None;
    QPointF dragStartScreen_;
    QPointF dragCurrentScreen_;
    QPointF dragStartWorld_;
    QPointF moveReference_;
    QPointF moveDelta_;
    MoveKind moveKind_ = MoveKind::Selection;
    int moveWire_ = -1;
    int movePart_ = -1;
    SketchDocument moveDocument_;
    QVector<QLineF> moveGuides_;
    QPointF moveMarker_;
    bool moveJoined_ = false;
    bool freeAngle_ = false;
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
