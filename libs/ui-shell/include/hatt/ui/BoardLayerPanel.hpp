#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QWidget>

class QComboBox;
class QListWidget;

namespace hatt::ui {

// Kayra (board) layer controls at the bottom of the context panel, as in Proteus ARES: the layer
// visibility list with theme-aware layer colours and, at the very bottom, the active layer that
// new tracks, pads and 2D graphics go on. The host connects the signals to the board canvas.
// The choices persist in QSettings under `editor/board/`: `activeLayer` (BoardLayer index) and
// `visibleLayers` (layer mask).
class BoardLayerPanel final : public QWidget {
    Q_OBJECT

public:
    explicit BoardLayerPanel(QWidget* parent = nullptr);

    [[nodiscard]] BoardLayer activeLayer() const;
    [[nodiscard]] int visibleLayers() const;

    // Updates the controls without emitting signals (e.g. after the canvas changed the layer).
    void setActiveLayer(BoardLayer layer);
    void setVisibleLayers(int mask);
    // Redraws the layer swatches (theme change or edited layer colours).
    void refreshColors();

signals:
    void activeLayerChanged(hatt::ui::BoardLayer layer);
    void visibleLayersChanged(int mask);

protected:
    void changeEvent(QEvent* event) override;

private:
    void storeVisibility();

    QComboBox* activeLayer_ = nullptr;
    QListWidget* visibility_ = nullptr;
};

} // namespace hatt::ui
