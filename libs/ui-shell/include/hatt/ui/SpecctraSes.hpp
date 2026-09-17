#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QByteArray>
#include <QStringList>

namespace hatt::ui {

struct SpecctraSesResult {
    SketchDocument routing;
    QStringList errors;
};

// Reads only the routed copper boundary of a Specctra session. Placement and metadata are
// deliberately ignored: the HattEDA project remains authoritative for everything except the
// proposed tracks and vias.
[[nodiscard]] SpecctraSesResult importSpecctraSes(const QByteArray& data);

// Specctra stores integer coordinates, so a returned endpoint can be a fraction of a micrometre
// away from its HattEDA pad. Snap only points already within `tolerance` to exact pad centres.
[[nodiscard]] SketchDocument snapSpecctraRoutingToPads(const SketchDocument& routing,
                                                       const SketchDocument& board,
                                                       double tolerance = 0.01);

} // namespace hatt::ui
