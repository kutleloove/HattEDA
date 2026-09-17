#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QString>

namespace hatt::ui {

// Display units only. The document always stores millimetres.
enum class LengthUnit { Millimetre, Inch, Mil };

// Schematics are shown in mil, matching the 100 mil (2.54 mm) grid; the board follows the user's
// preference (millimetres or inches).
[[nodiscard]] LengthUnit displayUnit(Workspace workspace, LengthUnit boardPreference);

[[nodiscard]] double toDisplayUnit(double millimetres, LengthUnit unit);
[[nodiscard]] double fromDisplayUnit(double value, LengthUnit unit);
// Unit symbol: "mm", "in", "mil".
[[nodiscard]] QString unitSymbol(LengthUnit unit);
// Fraction digits that resolve the finest grid step: mm 3, in 4, mil 1.
[[nodiscard]] int unitDecimals(LengthUnit unit);

// Value without trailing zeros plus unit, for labels: "2.54 mm", "0.1 in", "100 mil".
[[nodiscard]] QString formatLength(double millimetres, LengthUnit unit);
// Fixed-width value without unit, for live readouts that must not jitter: "2.540".
[[nodiscard]] QString formatCoordinate(double millimetres, LengthUnit unit);

// Persisted board preference (QSettings "editor/units/board": "mm" or "in").
[[nodiscard]] QString unitSettingValue(LengthUnit unit);
[[nodiscard]] LengthUnit unitFromSetting(const QString& value);

} // namespace hatt::ui
