#pragma once

#include "hatt/ui/GerberExport.hpp"

#include <QWidget>

namespace hatt::ui {

// Graphical preview of fabrication output (objectName "CamPreview"), drawn from the same
// CamOutput primitives that are written to the Gerber and drill files, in the board layer colours.
// The view fits the output; the mouse wheel zooms around the cursor and dragging pans.
class CamPreview final : public QWidget {
    Q_OBJECT

public:
    explicit CamPreview(QWidget* parent = nullptr);

    void setOutput(const CamOutput& output);
    [[nodiscard]] bool isLayerVisible(CamLayerKind kind) const;
    void setLayerVisible(CamLayerKind kind, bool visible);
    void setDrillsVisible(bool visible);
    // Output bounds in CAM millimetres (Y up); null when there is nothing to show.
    [[nodiscard]] QRectF outputBounds() const { return bounds_; }
    void fitToOutput();

protected:
    void paintEvent(QPaintEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    [[nodiscard]] QPointF toScreen(QPointF cam) const;

    CamOutput output_;
    QRectF bounds_;
    int hiddenLayers_ = 0; // bit per CamLayerKind
    bool drillsVisible_ = true;
    double scale_ = 10.0; // pixels per mm
    QPointF offset_;      // screen position of CAM origin
    QPointF lastMouse_;
};

} // namespace hatt::ui
