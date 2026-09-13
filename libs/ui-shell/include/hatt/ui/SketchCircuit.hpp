#pragma once

#include "hatt/ui/SketchModel.hpp"
#include "hatt/electrical/Circuit.hpp"
#include <QStringList>

namespace hatt::ui {

struct CircuitSnapshot {
    electrical::ConnectivityInput input;
    electrical::ConnectivityResult connectivity;
    electrical::DcCircuit dc;
    QStringList errors;
    QStringList simulationErrors;
};

CircuitSnapshot analyzeSchematic(const SketchDocument& document);
// World positions of the automatic junction dots of a schematic (see electrical::junctionPoints).
QVector<QPointF> schematicJunctions(const SketchDocument& document);
SketchDocument dcDividerExample();

struct BoardTransfer {
    SketchDocument document;
    QStringList errors;
    int added = 0;
    int updated = 0;
};
BoardTransfer transferToBoard(const SketchDocument& schematic, const SketchDocument& board);
struct BoardGuidance {
    QVector<QLineF> airwires;
    QStringList errors;
};
BoardGuidance boardGuidance(const SketchDocument& schematic, const SketchDocument& board);

} // namespace hatt::ui
