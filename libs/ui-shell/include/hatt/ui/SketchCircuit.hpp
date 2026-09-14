#pragma once

#include "hatt/ui/SketchModel.hpp"
#include "hatt/electrical/Circuit.hpp"
#include <QStringList>

namespace hatt::ui {

struct CircuitSnapshot {
    electrical::ConnectivityInput input;
    electrical::ConnectivityResult connectivity;
    electrical::DcCircuit dc;
    // Connectivity net index of each DC net (nets that no element touches are not solved).
    QVector<int> dcNets;
    QStringList errors;
    QStringList simulationErrors;
    // Voltage probes: schematic item id, world position of the probe pin and its net index.
    struct Probe {
        QString id;
        QPointF position;
        int net = -1;
    };
    QVector<Probe> probes;
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
// Adds footprints for schematic components that are not on the board yet (placed by
// autoPlaceParts) and refreshes labels/values of linked footprints.
BoardTransfer transferToBoard(const SketchDocument& schematic, const SketchDocument& board);

// Schematic components waiting for PCB placement, Proteus ARES style.
struct BoardParts {
    // Ready-to-place footprints (variant, label, value, sourceId, pinPadMap), in designator order.
    SketchDocument parts;
    // Components that cannot be placed yet, e.g. without a valid footprint.
    QStringList problems;
};
// Components excluded from the board and components already linked by a footprint are skipped.
BoardParts unplacedBoardParts(const SketchDocument& schematic, const SketchDocument& board);
// Returns `board` with `parts` appended at positions that do not overlap existing items: in rows
// inside the board outline when there is one, otherwise to the right of the design. Origins are
// rounded to `grid` (0 disables rounding); `spacing` is the clearance between part bounds and to
// the outline.
SketchDocument autoPlaceParts(const SketchDocument& board, const SketchDocument& parts,
                              double grid = 0.0, double spacing = 2.54);

// Plain-text netlist (one line per net: name and component.pin members) for export.
QString netlistText(const SketchDocument& schematic, QStringList* errors = nullptr);
struct BoardGuidance {
    QVector<QLineF> airwires;
    QStringList errors;
};
BoardGuidance boardGuidance(const SketchDocument& schematic, const SketchDocument& board);

} // namespace hatt::ui
