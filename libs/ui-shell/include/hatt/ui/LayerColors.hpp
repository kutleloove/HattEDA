#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QColor>

class QWidget;

namespace hatt::ui {

// Board layer colours. Defaults follow Proteus ARES / KiCad (top copper red, bottom copper blue);
// the user can override any layer per theme under QSettings
// `appearance/layerColors/<dark|light>/<layer index>`.
[[nodiscard]] QColor defaultLayerColor(BoardLayer layer, bool dark);
[[nodiscard]] QColor boardLayerColor(BoardLayer layer, bool dark);
void setLayerColorOverride(BoardLayer layer, bool dark, const QColor& color);
void clearLayerColorOverrides(bool dark);

// Modal editor ("LayerColorsDialog": one "LayerColor.<index>" button per layer, "LayerColorsReset").
// Returns true when colours were changed.
bool editLayerColorsDialog(QWidget* parent, bool dark);

} // namespace hatt::ui
