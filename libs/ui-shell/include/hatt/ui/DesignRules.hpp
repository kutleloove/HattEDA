#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

namespace hatt::ui {

// Project design rules (ADR-0008, Design Rule Manager in ADR-0010), millimetres. Stored in the
// `.hatt` "rules" object; every part below except the five global values is optional in the file.

// Where a clearance rule applies. Board covers every copper layer; a copper layer region overrides
// Board on that layer.
enum class RuleRegion { Board, TopCopper, BottomCopper };

// Proteus style clearance rule: the gaps between pads (vias count as pads), traces and copper
// graphics (zones) of different nets, and from copper to the board edge.
struct ClearanceRule {
    QString name = QStringLiteral("DEFAULT");
    RuleRegion region = RuleRegion::Board;
    double padPad = 0.2;
    double padTrace = 0.2;
    double traceTrace = 0.2;
    double graphic = 0.2;
    double edge = 0.3;

    friend bool operator==(const ClearanceRule&, const ClearanceRule&) = default;
};

// A net class: the track and via used when routing its nets, the narrowest allowed neck near pads,
// the copper layers its tracks may use and how its ratsnest is shown. `nets` lists explicitly
// assigned nets; unassigned nets fall back to POWER (nets with a ground or power rail symbol) or
// SIGNAL (see netClassForNet). Tracks routed from copper of a class net start at `traceWidth`, and
// `clearance` (issue #39) keeps copper of other nets at least that far from the class's copper on
// top of the clearance rules.
struct NetClass {
    QString name;
    double traceWidth = 0.3048;
    double viaDiameter = 0.8;
    double viaDrill = 0.4;
    double neckWidth = 0.0; // 0 = no necking below traceWidth
    double clearance = 0.0; // 0 = only the clearance rules
    int layers = CopperLayerMask;
    QString ratsnestColor;  // "#rrggbb", empty = the theme colour
    bool ratsnestHidden = false;
    QStringList nets;

    friend bool operator==(const NetClass&, const NetClass&) = default;
};

struct DifferentialPair {
    QString name;
    QString positiveNet;
    QString negativeNet;
    double width = 0.2;
    double gap = 0.2;

    friend bool operator==(const DifferentialPair&, const DifferentialPair&) = default;
};

// Defaults used by pours and CAM.
struct RuleDefaults {
    bool thermalRelief = true;
    double thermalGap = 0.3;
    double spokeWidth = 0.4;
    double solderResistGuard = 0.05; // mask expansion per side
    double silkClearance = 0.1;      // silkscreen to pads
    double curveTolerance = 0.01;

    friend bool operator==(const RuleDefaults&, const RuleDefaults&) = default;
};

struct DesignRules {
    // Global values (ADR-0008). Without explicit clearance rules `clearance` and
    // `boardEdgeClearance` form the DEFAULT Board rule.
    double clearance = 0.2;
    double minTrackWidth = 0.15;
    double minDrill = 0.3;
    double minAnnularRing = 0.13;
    double boardEdgeClearance = 0.3;
    QVector<ClearanceRule> clearanceRules; // empty = the DEFAULT rule from the global values
    QVector<NetClass> netClasses;          // empty = POWER and SIGNAL defaults
    QVector<DifferentialPair> differentialPairs;
    RuleDefaults defaults;

    friend bool operator==(const DesignRules&, const DesignRules&) = default;
};

inline const QString PowerNetClass = QStringLiteral("POWER");
inline const QString SignalNetClass = QStringLiteral("SIGNAL");

// Translated reason why the rules are unusable, or an empty string.
[[nodiscard]] QString validateDesignRules(const DesignRules& rules);

[[nodiscard]] QVector<ClearanceRule> effectiveClearanceRules(const DesignRules& rules);
[[nodiscard]] QVector<NetClass> effectiveNetClasses(const DesignRules& rules);

enum class ClearanceObject { Pad, Trace, Graphic };
// Required gap between two objects on `layerMask` (the copper layers they share): for every shared
// layer the Board rules and that layer's region rules apply; the largest value wins.
[[nodiscard]] double clearanceBetween(const DesignRules& rules, int layerMask, ClearanceObject a, ClearanceObject b);
[[nodiscard]] double edgeClearance(const DesignRules& rules, int layerMask);
// Largest gap any rule can require (search distance for the DRC).
[[nodiscard]] double largestClearance(const DesignRules& rules);

// Net class of each schematic net name. Explicit assignments first; otherwise POWER for nets that
// contain a ground or power rail symbol and SIGNAL for the rest (when those classes exist; else the
// first class).
[[nodiscard]] QHash<QString, QString> netClassAssignments(const DesignRules& rules, const SketchDocument& schematic);
[[nodiscard]] NetClass netClassForNet(const DesignRules& rules, const SketchDocument& schematic, const QString& net);
// Class clearance of every net whose class sets one (NetClass::clearance > 0).
[[nodiscard]] QHash<QString, double> netClassClearances(const DesignRules& rules, const SketchDocument& schematic);
// Gap required between copper of nets `a` and `b` (empty = unknown net): the rule gap, raised to the
// class clearance of either net.
[[nodiscard]] double netPairClearance(const QHash<QString, double>& classClearances, double ruleClearance,
                                      const QString& a, const QString& b);

// How a track started on existing board copper is routed (issue #39): the net, its class, the class
// trace width and the gap the router keeps from other copper.
struct RouteClass {
    QString net;
    QString netClass;
    double traceWidth = 0.0;
    double clearance = 0.0; // the larger of the rule clearance and the class clearance

    friend bool operator==(const RouteClass&, const RouteClass&) = default;
};

[[nodiscard]] QString ruleRegionToken(RuleRegion region);
[[nodiscard]] QString ruleRegionName(RuleRegion region); // translated

// `.hatt` "rules" object. Optional parts are written only when they differ from the defaults, so
// projects without them keep their bytes.
[[nodiscard]] QJsonObject designRulesToJson(const DesignRules& rules);
// Reads `value` into `rules` (missing keys keep defaults); returns a translated error or empty.
[[nodiscard]] QString designRulesFromJson(const QJsonValue& value, DesignRules& rules);

} // namespace hatt::ui
