#pragma once

#include "hatt/ui/DesignRules.hpp"
#include "hatt/ui/SketchModel.hpp"

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace hatt::ui {

// Electrical rule check (schematic) and design rule check (board), ADR-0008. Both work on editor
// snapshots and never change the documents.

enum class CheckSeverity { Error, Warning };

struct CheckViolation {
    CheckSeverity severity = CheckSeverity::Error;
    Workspace workspace = Workspace::Schematic;
    // Stable rule id, e.g. "erc.unconnected-pin" or "drc.clearance".
    QString rule;
    QString message; // translated
    bool hasLocation = false;
    QPointF location; // world millimetres of `workspace`
    QStringList itemIds;
};

struct CheckReport {
    QVector<CheckViolation> violations;
    [[nodiscard]] int count(CheckSeverity severity) const;
};

// ERC rules (schematic):
//  erc.analysis          Error    connectivity problems: missing/duplicate references, unnamed ports,
//                                 conflicting net names
//  erc.unconnected-pin   Warning  a component pin that no wire or other pin reaches
//  erc.unused-terminal   Warning  a port, rail or probe that reaches no component pin
//  erc.shorted-component Warning  every pin of a multi-pin component is on the same net
//  erc.dangling-wire     Warning  a wire end that touches nothing
//  erc.missing-value     Warning  a component whose symbol has a default value but no value
//  erc.footprint         Error    a board component without a footprint of the same pin count or
//                                 with an invalid pin-to-pad map
[[nodiscard]] CheckReport runElectricalRuleCheck(const SketchDocument& schematic);

// DRC rules (board):
//  drc.no-outline        Warning  the board has no closed outline
//  drc.track-width       Error    a track narrower than minTrackWidth
//  drc.drill             Error    a pad or via hole smaller than minDrill
//  drc.annular-ring      Error    copper around a hole narrower than minAnnularRing
//  drc.clearance         Error    copper of different nets closer than the clearance rule for the
//                                 shared layers and object kinds (pad-pad, pad-trace, trace-trace, graphic)
//  drc.net-class-clearance Error  copper of different nets that meets the clearance rule but is closer
//                                 than the clearance of either net's class
//  drc.net-class-width   Warning  a track narrower than its net class trace (or neck) width
//  drc.net-class-layer   Warning  a track on a copper layer its net class does not allow
//  drc.short             Error    copper joining different schematic nets
//  drc.zone-short        Error    a copper zone (solid, not poured) joining different schematic nets
//  drc.zone-unfilled     Warning  every copper zone: it is solid copper without clearance
//  drc.board-edge        Error    copper closer than boardEdgeClearance to the outline or outside it
//  drc.overlap           Warning  footprints on the same side whose bodies overlap
//  drc.unrouted          Warning  pads of one schematic net that copper does not join yet
//  drc.not-placed        Warning  a board component of the schematic without a footprint on the board
[[nodiscard]] CheckReport runDesignRuleCheck(const SketchDocument& schematic,
                                             const SketchDocument& board, const DesignRules& rules);

} // namespace hatt::ui
