#pragma once

#include <QPointF>
#include <QString>
#include <QVector>

namespace hatt::ui {

// Minimal single-stroke font for fabrication output (silkscreen designators and board text), so
// Gerber files do not depend on system fonts. Glyphs are drawn on a 4 × 6 unit grid with a
// 2 unit gap; lowercase letters use the uppercase glyphs and unknown characters become a box.
// Coordinates follow the editor: millimetres, Y pointing down, `topLeft` at the top-left corner.

// Width of `text` at `height` millimetres.
[[nodiscard]] double strokeTextWidth(const QString& text, double height);
// Polylines of `text`.
[[nodiscard]] QVector<QVector<QPointF>> strokeText(const QString& text, QPointF topLeft, double height);

} // namespace hatt::ui
