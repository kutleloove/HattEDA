#pragma once

#include "hatt/ui/SketchModel.hpp"

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
// Interim scope: copper zones are exported as solid regions without clearance, text is not
// exported, vias are tented (no solder mask opening) and every hole is plated.

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
    int skippedTexts = 0;
};

struct CamFile {
    QString fileName; // e.g. "board-F_Cu.gtl"
    QByteArray content;
};

inline constexpr double CamSilkLineWidth = 0.15;
inline constexpr double CamOutlineLineWidth = 0.1;
inline constexpr double CamCopperLineWidth = 0.254;
inline constexpr double CamMaskExpansion = 0.05; // per side

[[nodiscard]] CamOutput buildCamOutput(const SketchDocument& board);
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
