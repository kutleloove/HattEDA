#pragma once

#include "hatt/ui/SketchModel.hpp"
#include "hatt/ui/ZoneFill.hpp"

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QVector>

namespace hatt::ui {

// CAM output for Kayra (Proteus ARES "Generate CADCAM output"): Gerber X2 layer files and an
// Excellon drill file built from the board snapshot. Geometry comes from the same helpers as the
// canvas (itemPads, trackWidth, symbol silkscreen), so what is drawn is what is fabricated.
// Coordinates are millimetres with Y pointing up (the editor's Y is negated).
//
// Interim scope: copper zones have no pour or clearance yet, so they are left out unless
// CamOptions::includeZones is set (they would short every net they cover); board text and
// footprint designators are written with the single-stroke font (StrokeFont.hpp); vias are
// tented (no solder mask opening) and every hole is plated.

enum class CamApertureShape { Circle, Rectangle, Obround };

struct CamAperture {
    CamApertureShape shape = CamApertureShape::Circle;
    double width = 0.0;  // mm; diameter for circles
    double height = 0.0; // mm; unused for circles

    friend bool operator==(const CamAperture&, const CamAperture&) = default;
};

// One drawing operation on a layer: a flash (pad), a stroke (track or outline drawn with a round
// aperture) or a filled region (copper zone, filled silkscreen).
struct CamPrimitive {
    enum class Kind { Flash, Stroke, Region };
    Kind kind = Kind::Flash;
    CamAperture aperture;
    QVector<QPointF> points; // Flash: one point; Stroke: polyline; Region: closed outline
    // Region only: clear polarity (Gerber LPC) removes copper drawn earlier on the layer. Poured zones
    // use it for knockouts and are written before every other primitive of the layer.
    bool clear = false;
};

enum class CamLayerKind { TopCopper, BottomCopper, TopSilk, BottomSilk, TopMask, BottomMask, TopPaste,
                          BottomPaste, Outline };

struct CamLayer {
    CamLayerKind kind = CamLayerKind::TopCopper;
    QVector<CamPrimitive> primitives;
};

struct CamDrillHit {
    QPointF at;      // mm, Y up
    double diameter = 0.0; // mm
};

struct CamOutput {
    QVector<CamLayer> layers; // always all nine, in CamLayerKind order
    QVector<CamDrillHit> drills;
    int skippedTexts = 0; // text on layers without a fabrication file
    int skippedZones = 0; // copper zones left out: no pour in CamOptions::zoneFills
};

struct CamOptions {
    // Poured copper (ZoneFill.hpp); zones listed here are exported with their clearances.
    QVector<ZoneFillResult> zoneFills;
    // Zones without a pour as solid regions. Off by default: a solid zone shorts every pad and
    // track it covers.
    bool includeZones = false;
    // Footprint designators (item labels) on the footprint's silkscreen, above the footprint.
    bool designators = true;
};

inline constexpr double CamDesignatorHeight = 1.0; // mm
inline constexpr double CamDesignatorGap = 0.3;    // mm between the footprint and its designator

struct CamFile {
    QString fileName; // e.g. "board-F_Cu.gtl"
    QByteArray content;
};

inline constexpr double CamSilkLineWidth = 0.15;
inline constexpr double CamOutlineLineWidth = 0.1;
inline constexpr double CamCopperLineWidth = 0.254;
inline constexpr double CamMaskExpansion = 0.05; // per side

[[nodiscard]] CamOutput buildCamOutput(const SketchDocument& board, const CamOptions& options = {});
[[nodiscard]] QString camLayerName(CamLayerKind kind);
// Gerber X2 text of one layer.
[[nodiscard]] QByteArray gerberLayer(const CamLayer& layer, const QString& generator);
// Excellon (metric, decimal coordinates) drill file with one tool per hole diameter.
[[nodiscard]] QByteArray excellonDrill(const QVector<CamDrillHit>& drills);
// Every layer file plus the drill file, named `<baseName>-<suffix>`.
[[nodiscard]] QVector<CamFile> camFiles(const CamOutput& output, const QString& baseName,
                                        const QString& generator);
// Atomically writes each file to an existing directory. Empty means success.
[[nodiscard]] QString writeCamFiles(const QVector<CamFile>& files, const QString& directory);

} // namespace hatt::ui
