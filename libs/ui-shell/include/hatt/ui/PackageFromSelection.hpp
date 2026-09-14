#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QList>

namespace hatt::ui {

// Proteus ARES "Make Package" and "Decompose" for Kayra. Both work on editor snapshots only; the
// host stores the footprint in the project library (ComponentLibrary.hpp) and applies the
// document through the canvas undo stack.

enum class PackageOrigin { FirstPad, PadCentre };

struct PackageExtraction {
    // Explicit geometry (pads, pins, silk shapes) relative to the origin, seen from the top.
    // `id` and `name` are left empty for the host.
    FootprintDefinition footprint;
    QPointF origin;          // world position of the footprint origin
    bool mirrored = false;   // drawn on the bottom side only: geometry was flipped to the top view
    bool renumbered = false; // pad numbers were not 1..n without repeats and were renumbered
    QList<int> usedItems;    // document indices that became pads or silkscreen
    int ignoredItems = 0;    // selected items that cannot be part of a package (tracks, text, ...)
};

// Pads, vias (as round through-hole pads) and silkscreen graphics of the selection become a
// package. Pad numbers are kept when they run 1..n; otherwise pads are renumbered in their
// existing number order. The result has no pads when the selection contains none.
[[nodiscard]] PackageExtraction extractPackage(const SketchDocument& document, const QList<int>& selection,
                                               PackageOrigin origin);

// `document` with the used items replaced by one placed footprint `footprintId` at the origin
// (on the bottom side when the extraction was mirrored). The new item is the last one.
[[nodiscard]] SketchDocument replaceWithPackage(const SketchDocument& document,
                                                const PackageExtraction& extraction,
                                                const QString& footprintId);

// Selected board footprints are replaced by their pads (Pad items keeping numbers, sizes, drills
// and copper layers) and silkscreen outlines (Line/Polyline items on the footprint's silk layer).
// Returns the number of decomposed footprints; `document` is unchanged when it is 0.
int decomposePackages(SketchDocument& document, const QList<int>& selection);

} // namespace hatt::ui
