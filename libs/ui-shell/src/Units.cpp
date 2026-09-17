#include "hatt/ui/Units.hpp"

#include <cmath>

namespace hatt::ui {
namespace {
constexpr double MillimetresPerInch = 25.4;
} // namespace

LengthUnit displayUnit(Workspace workspace, LengthUnit boardPreference) {
    return workspace == Workspace::Schematic ? LengthUnit::Mil : boardPreference;
}

double toDisplayUnit(double millimetres, LengthUnit unit) {
    switch (unit) {
    case LengthUnit::Inch:
        return millimetres / MillimetresPerInch;
    case LengthUnit::Mil:
        return millimetres / MillimetresPerInch * 1000.0;
    case LengthUnit::Millimetre:
        break;
    }
    return millimetres;
}

double fromDisplayUnit(double value, LengthUnit unit) {
    switch (unit) {
    case LengthUnit::Inch:
        return value * MillimetresPerInch;
    case LengthUnit::Mil:
        return value * MillimetresPerInch / 1000.0;
    case LengthUnit::Millimetre:
        break;
    }
    return value;
}

QString unitSymbol(LengthUnit unit) {
    switch (unit) {
    case LengthUnit::Inch:
        return QStringLiteral("in");
    case LengthUnit::Mil:
        return QStringLiteral("mil");
    case LengthUnit::Millimetre:
        break;
    }
    return QStringLiteral("mm");
}

int unitDecimals(LengthUnit unit) {
    switch (unit) {
    case LengthUnit::Inch:
        return 4;
    case LengthUnit::Mil:
        return 1;
    case LengthUnit::Millimetre:
        break;
    }
    return 3;
}

QString formatCoordinate(double millimetres, LengthUnit unit) {
    const double value = toDisplayUnit(millimetres, unit);
    // Avoid "-0.000" for values that round to zero.
    const double rounded = std::abs(value) < 0.5 * std::pow(10.0, -unitDecimals(unit)) ? 0.0 : value;
    return QString::number(rounded, 'f', unitDecimals(unit));
}

QString formatLength(double millimetres, LengthUnit unit) {
    QString text = formatCoordinate(millimetres, unit);
    if (text.contains(QLatin1Char('.'))) {
        while (text.endsWith(QLatin1Char('0'))) text.chop(1);
        if (text.endsWith(QLatin1Char('.'))) text.chop(1);
    }
    return text + QLatin1Char(' ') + unitSymbol(unit);
}

QString unitSettingValue(LengthUnit unit) { return unitSymbol(unit); }

LengthUnit unitFromSetting(const QString& value) {
    return value == QLatin1String("in") ? LengthUnit::Inch : LengthUnit::Millimetre;
}

} // namespace hatt::ui
