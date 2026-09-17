#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace hatt::ui {

// Assembly outputs next to the Gerber/drill files: a bill of materials from the schematic and a
// pick-and-place (centroid) list from the board. Plain CSV (RFC 4180 quoting, UTF-8, CRLF), the
// format assembly services accept.

struct BomLine {
    QStringList references; // natural order: R2 before R10
    QString value;
    QString footprint;      // symbol id, e.g. "board.r0603"
    QString footprintName;  // display name
    QString device;         // schematic symbol id
    QString deviceName;
    QString manufacturer;   // project devices only (DeviceSpec)
    QString partNumber;
    [[nodiscard]] int quantity() const { return static_cast<int>(references.size()); }
};

// Schematic components grouped by device, value and footprint, sorted by first reference.
// Components excluded from the board are not assembled and are left out.
[[nodiscard]] QVector<BomLine> buildBom(const SketchDocument& schematic, const ProjectLibrary& library);
[[nodiscard]] QByteArray bomCsv(const QVector<BomLine>& lines);

struct PlacementLine {
    QString reference;
    QString value;
    QString footprint;     // display name
    QPointF centre;        // mm, Y up (CAM coordinates): centre of the pad bounds, else the origin
    double rotation = 0.0; // degrees counter-clockwise as seen from the top
    bool bottom = false;
};

// Footprints on the board (Symbol items with a board footprint and pads), sorted by reference.
[[nodiscard]] QVector<PlacementLine> buildPlacement(const SketchDocument& board);
[[nodiscard]] QByteArray placementCsv(const QVector<PlacementLine>& lines);

// Natural designator comparison: prefix, then number (R2 < R10 < U1).
[[nodiscard]] bool referenceLess(const QString& a, const QString& b);

} // namespace hatt::ui
