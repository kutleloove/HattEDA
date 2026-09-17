#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QString>
#include <QStringList>

namespace hatt::ui {

// Project-defined devices and footprints (ADR-0007): generated schematic symbols and parametric
// footprints. Qt Core types only; dialogs live in LibraryDialogs.hpp.

inline const QString CustomDevicePrefix = QStringLiteral("project.device.");
inline const QString CustomFootprintPrefix = QStringLiteral("project.footprint.");
inline constexpr int MaxGeneratedPads = 400;

[[nodiscard]] QString newCustomDeviceId();
[[nodiscard]] QString newCustomFootprintId();

// Stable file tokens of PackageStyle.
[[nodiscard]] QString packageStyleToken(PackageStyle style);
[[nodiscard]] bool packageStyleFromToken(const QString& token, PackageStyle& style);

// Translated reason why the parameters cannot make a footprint (wrong pad count for the style,
// non-positive sizes, a drill larger than its pad, overlapping pads), or an empty string.
[[nodiscard]] QString validateFootprintParams(const FootprintParams& params);
// Same for an explicit footprint (FootprintDefinition::isExplicit): 1..MaxGeneratedPads pads with a
// position each, pad numbers 1..n used once, positive finite sizes, drills smaller than the pad and
// at least one copper layer.
[[nodiscard]] QString validateExplicitFootprint(const FootprintDefinition& footprint);
// Translated reason why `map` (DeviceDefinition::pinPadMap) is not a pin to pad assignment for
// `pinCount` pins, or an empty string; an empty map is valid (pin N to pad N).
[[nodiscard]] QString validatePinPadMap(const QVector<int>& map, int pinCount);

// Board symbol of a footprint. Explicit footprints copy their shapes and pads, ordered by pad
// number. Generated ones get a silkscreen body and pin 1 mark in `shapes`, pad centres in `pins`
// and pad definitions in `pads` (same order, numbered from 1). Pads are placed as follows:
// two-terminal at x = ±rowSpacing/2; single row downwards along Y; dual row pads 1..n/2 down the
// left row then up the right row; quad row counter-clockwise from the top of the left row.
[[nodiscard]] SymbolDefinition footprintSymbol(const FootprintDefinition& footprint);

// Schematic symbol of a device: two-pin devices are a small horizontal box, larger ones an IC box
// with pins on the 2.54 mm grid, left row downwards and right row upwards.
[[nodiscard]] SymbolDefinition deviceSymbol(const DeviceDefinition& device);

// Minimum external copper width in mm that carries `current` amperes (IPC-2221, 1 oz copper,
// 10 °C temperature rise by default). 0 for a non-positive current.
[[nodiscard]] double recommendedCopperWidth(double current, double copperOunces = 1.0,
                                            double temperatureRise = 10.0);

// Footprint suggested for a device. The pad count always equals the pin count. Datasheet data in
// `device.spec` (package, pitch, row spacing, body, leads, through-hole) sets the geometry; a
// pin current widens pads to recommendedCopperWidth where the pitch allows. Without data the
// result is a 2.54 mm through-hole single row (two pins) or DIP style dual row (even counts ≥ 4).
struct FootprintSuggestion {
    FootprintParams params;
    bool fromDatasheet = false; // at least one geometric datasheet value was used
    QStringList notes;          // translated remarks, e.g. a current the pitch cannot carry
};
[[nodiscard]] FootprintSuggestion suggestFootprint(const DeviceDefinition& device);

// Registers the symbols of all custom devices and footprints so findSymbol resolves them.
void registerProjectLibrary(const ProjectLibrary& library);

// Board footprints with `padCount` pads: built-in ones followed by the project's custom ones.
[[nodiscard]] QList<const SymbolDefinition*> footprintsWithPads(const ProjectLibrary& library,
                                                                int padCount);
// Schematic components offered by the pick dialog: built-in ones and the project's custom devices.
[[nodiscard]] QList<const SymbolDefinition*> pickableDevices(const ProjectLibrary& library);

} // namespace hatt::ui
