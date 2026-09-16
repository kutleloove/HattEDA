#pragma once

#include "hatt/ui/DesignRules.hpp"
#include "hatt/ui/SketchModel.hpp"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace hatt::ui {

// Interchange boundary for external PCB autorouters. The generated DSN is transient and never
// becomes the HattEDA project format; imported routing must still pass HattEDA connectivity/DRC.
struct SpecctraDsnResult {
    QByteArray data;
    QStringList errors;
};

[[nodiscard]] SpecctraDsnResult exportSpecctraDsn(const SketchDocument& schematic,
                                                  const SketchDocument& board,
                                                  const DesignRules& rules,
                                                  const QString& designName);

} // namespace hatt::ui
