#pragma once

#include "hatt/ui/GerberExport.hpp"

#include <QColor>
#include <QPainterPath>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QVector>

class QPainter;

namespace hatt::ui {

// Kayra print layout (Proteus ARES "Print Layout") for home etching: the board artwork drawn at
// true scale on a paper page, repeated as many times as fit ("one page, all copies"), optionally
// mirrored (toner transfer), inverted (photo resist) or in board colours. Geometry is the
// fabrication output (CamOutput), so a print matches the Gerber files. Units are millimetres.

enum class PaperPreset { A3, A4, A5, Letter, Custom };

[[nodiscard]] QSizeF paperSize(PaperPreset preset); // portrait, mm; Custom returns A4
[[nodiscard]] QString paperName(PaperPreset preset);

enum class PrintColours { Monochrome, Negative, Board };

// Where the selected layers go on the page: all layers overlaid in one artwork, or one artwork
// per layer next to each other (e.g. top and bottom copper on the same sheet).
enum class PrintGrouping { Overlay, SideBySide };

struct PrintSettings {
    QSizeF paper{210.0, 297.0}; // portrait width × height, mm
    bool landscape = false;
    double margin = 10.0;  // mm on every side
    double spacing = 5.0;  // mm between artworks
    int layers = (1 << static_cast<int>(CamLayerKind::BottomCopper)) | (1 << static_cast<int>(CamLayerKind::Outline));
    bool drills = true;    // punch marks at hole centres (holes are left open)
    PrintGrouping grouping = PrintGrouping::Overlay;
    PrintColours colours = PrintColours::Monochrome;
    bool mirror = false;   // flip left to right (toner transfer onto the copper side)
    bool rotate = false;   // turn the artwork 90°
    int columns = 1;
    int rows = 1;
    double scale = 1.0;    // 1 = true size
    // Printer correction, e.g. 1.005 when a 100 mm test line prints as 99.5 mm (not a design scale).
    double compensationX = 1.0;
    double compensationY = 1.0;
};

[[nodiscard]] QSizeF pageSize(const PrintSettings& settings); // with orientation applied

// Area of the artwork in CAM millimetres (Y up): the board outline when there is one, otherwise
// everything the selected layers draw. Null when there is nothing to print.
[[nodiscard]] QRectF artworkBounds(const CamOutput& output, int layers);

struct PrintTile {
    QRectF page;              // artwork box on the page, mm from the top-left corner
    CamLayerKind layer = CamLayerKind::TopCopper; // SideBySide: the layer of this copy
    int layers = 0;           // layer mask drawn in this box
};

// Artwork boxes in reading order: columns × rows copies, each copy made of one box (Overlay) or
// one box per selected layer (SideBySide). Boxes that would leave the printable area are dropped;
// `fitted` reports how many were requested but did not fit.
[[nodiscard]] QVector<PrintTile> layoutTiles(const PrintSettings& settings, QSizeF artwork, int* dropped = nullptr);

// Largest columns × rows that fits the printable area, trying both orientations of the artwork
// when `allowRotation` is set; `rotate` in the result tells which one won.
struct PrintFit {
    int columns = 0;
    int rows = 0;
    bool rotate = false;
};
[[nodiscard]] PrintFit fitCopies(const PrintSettings& settings, QSizeF artwork, bool allowRotation);

// Filled copper/silk/mask shape of one layer in CAM mm (strokes widened, clear regions removed).
[[nodiscard]] QPainterPath layerShape(const CamLayer& layer);

// Paints one page. `painter` maps millimetres by `dotsPerMm` (1 device unit = 1/dotsPerMm mm).
void paintPrintPage(QPainter& painter, const CamOutput& output, const PrintSettings& settings, double dotsPerMm,
                    bool previewFrame);

} // namespace hatt::ui
