#pragma once

#include "hatt/ui/SketchModel.hpp"

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

// Where a footprint's designator goes, shared by the canvas label and the silkscreen output so both
// agree: centred above the footprint, or, for footprints turned 90° or 270°, rotated to read upwards
// and centred left of it. 180° keeps the text horizontal so it never reads upside down.
struct DesignatorPlacement {
    QPointF centre;        // editor mm, centre of the text box
    bool vertical = false; // rotated 90° counter-clockwise (reads bottom to top)
};
[[nodiscard]] DesignatorPlacement designatorPlacement(const SketchItem& footprint, double textHeight, double gap);

// Stroke polylines of `text` centred on `placement`; bottom-side text is mirrored about the centre so
// it reads correctly from the bottom of the board.
[[nodiscard]] QVector<QVector<QPointF>> placedStrokeText(const QString& text, const DesignatorPlacement& placement,
                                                         double height, bool mirrored);

} // namespace hatt::ui
