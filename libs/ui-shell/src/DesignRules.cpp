#include "hatt/ui/DesignRules.hpp"

#include "hatt/ui/SketchCircuit.hpp"

#include <QColor>
#include <QCoreApplication>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace hatt::ui {
namespace {

// Rule messages keep the DesignChecks context they were introduced with.
QString tr(const char* text) { return QCoreApplication::translate("hatt::ui::DesignChecks", text); }

constexpr double MaximumRuleValue = 100.0;

struct RegionToken {
    RuleRegion region;
    const char* token;
};
// Stable file tokens; never reuse or rename an entry.
constexpr RegionToken RegionTokens[] = {
    {RuleRegion::Board, "board"}, {RuleRegion::TopCopper, "top-copper"}, {RuleRegion::BottomCopper, "bottom-copper"}};

bool validLength(double value, bool allowZero) {
    return std::isfinite(value) && (allowZero ? value >= 0 : value > 0) && value <= MaximumRuleValue;
}

int regionLayers(RuleRegion region) {
    switch (region) {
    case RuleRegion::Board: return CopperLayerMask;
    case RuleRegion::TopCopper: return layerBit(BoardLayer::TopCopper);
    case RuleRegion::BottomCopper: return layerBit(BoardLayer::BottomCopper);
    }
    return CopperLayerMask;
}

double ruleValue(const ClearanceRule& rule, ClearanceObject a, ClearanceObject b) {
    if (a == ClearanceObject::Graphic || b == ClearanceObject::Graphic) return rule.graphic;
    if (a == ClearanceObject::Pad && b == ClearanceObject::Pad) return rule.padPad;
    if (a == ClearanceObject::Trace && b == ClearanceObject::Trace) return rule.traceTrace;
    return rule.padTrace;
}

// The rules that apply to one copper layer: its region rules when there are any, else Board rules.
QVector<const ClearanceRule*> rulesForLayer(const QVector<ClearanceRule>& rules, int layer) {
    QVector<const ClearanceRule*> region;
    QVector<const ClearanceRule*> board;
    for (const auto& rule : rules) {
        if (rule.region == RuleRegion::Board) board.append(&rule);
        else if (regionLayers(rule.region) & layer) region.append(&rule);
    }
    return region.isEmpty() ? board : region;
}

NetClass defaultClass(const QString& name, double traceWidth, double viaDiameter, double viaDrill) {
    NetClass netClass;
    netClass.name = name;
    netClass.traceWidth = traceWidth;
    netClass.viaDiameter = viaDiameter;
    netClass.viaDrill = viaDrill;
    return netClass;
}

// JSON helpers: a missing key keeps the target.
bool readNumber(const QJsonObject& object, const char* key, double& target) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined()) return true;
    if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
    target = value.toDouble();
    return true;
}

bool readText(const QJsonObject& object, const char* key, QString& target) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined()) return true;
    if (!value.isString()) return false;
    target = value.toString();
    return true;
}

bool readBool(const QJsonObject& object, const char* key, bool& target) {
    const QJsonValue value = object.value(QLatin1String(key));
    if (value.isUndefined()) return true;
    if (!value.isBool()) return false;
    target = value.toBool();
    return true;
}

QJsonArray layerTokens(int layers) {
    QJsonArray tokens;
    if (layers & layerBit(BoardLayer::TopCopper)) tokens.append(QStringLiteral("top-copper"));
    if (layers & layerBit(BoardLayer::BottomCopper)) tokens.append(QStringLiteral("bottom-copper"));
    return tokens;
}

} // namespace

QString ruleRegionToken(RuleRegion region) {
    for (const auto& entry : RegionTokens) {
        if (entry.region == region) return QString::fromLatin1(entry.token);
    }
    return QStringLiteral("board");
}

QString ruleRegionName(RuleRegion region) {
    switch (region) {
    case RuleRegion::Board: return tr("Board");
    case RuleRegion::TopCopper: return tr("Top copper");
    case RuleRegion::BottomCopper: return tr("Bottom copper");
    }
    return {};
}

QString validateDesignRules(const DesignRules& rules) {
    for (double value : {rules.clearance, rules.minTrackWidth, rules.minDrill, rules.minAnnularRing,
                         rules.boardEdgeClearance}) {
        if (!std::isfinite(value) || value < 0) return tr("Design rules cannot be negative.");
        if (value > MaximumRuleValue) return tr("Design rules must be at most 100 mm.");
    }
    if (rules.clearance <= 0) return tr("The clearance must be greater than zero.");
    if (rules.minTrackWidth <= 0) return tr("The minimum track width must be greater than zero.");

    QSet<QString> names;
    for (const auto& rule : rules.clearanceRules) {
        if (rule.name.trimmed().isEmpty()) return tr("Every design rule needs a name.");
        if (names.contains(rule.name)) return tr("The design rule name %1 is used twice.").arg(rule.name);
        names.insert(rule.name);
        for (double value : {rule.padPad, rule.padTrace, rule.traceTrace, rule.graphic}) {
            if (!validLength(value, false)) return tr("Clearances of the rule %1 must be between 0 and 100 mm.").arg(rule.name);
        }
        if (!validLength(rule.edge, true)) return tr("Clearances of the rule %1 must be between 0 and 100 mm.").arg(rule.name);
    }

    names.clear();
    QHash<QString, QString> assigned;
    for (const auto& netClass : rules.netClasses) {
        if (netClass.name.trimmed().isEmpty()) return tr("Every net class needs a name.");
        if (names.contains(netClass.name)) return tr("The net class name %1 is used twice.").arg(netClass.name);
        names.insert(netClass.name);
        if (!validLength(netClass.traceWidth, false) || !validLength(netClass.viaDiameter, false) ||
            !validLength(netClass.viaDrill, false) || !validLength(netClass.neckWidth, true) ||
            !validLength(netClass.clearance, true)) {
            return tr("Sizes of the net class %1 must be between 0 and 100 mm.").arg(netClass.name);
        }
        if (netClass.viaDrill >= netClass.viaDiameter) return tr("The via drill of the net class %1 must be smaller than the via.").arg(netClass.name);
        if (netClass.neckWidth > netClass.traceWidth) return tr("The neck of the net class %1 cannot be wider than its trace.").arg(netClass.name);
        if (netClass.layers == 0 || (netClass.layers & ~CopperLayerMask) != 0) {
            return tr("The net class %1 must allow at least one copper layer.").arg(netClass.name);
        }
        if (!netClass.ratsnestColor.isEmpty() && !QColor::isValidColorName(netClass.ratsnestColor)) {
            return tr("The ratsnest colour of the net class %1 is invalid.").arg(netClass.name);
        }
        for (const QString& net : netClass.nets) {
            if (net.trimmed().isEmpty()) return tr("The net class %1 lists an empty net name.").arg(netClass.name);
            if (assigned.contains(net)) return tr("The net %1 is in the classes %2 and %3.").arg(net, assigned.value(net), netClass.name);
            assigned.insert(net, netClass.name);
        }
    }

    names.clear();
    for (const auto& pair : rules.differentialPairs) {
        if (pair.name.trimmed().isEmpty()) return tr("Every differential pair needs a name.");
        if (names.contains(pair.name)) return tr("The differential pair name %1 is used twice.").arg(pair.name);
        names.insert(pair.name);
        if (pair.positiveNet.trimmed().isEmpty() || pair.negativeNet.trimmed().isEmpty() || pair.positiveNet == pair.negativeNet) {
            return tr("The differential pair %1 needs two different nets.").arg(pair.name);
        }
        if (!validLength(pair.width, false) || !validLength(pair.gap, false)) {
            return tr("Sizes of the differential pair %1 must be between 0 and 100 mm.").arg(pair.name);
        }
    }

    const RuleDefaults& d = rules.defaults;
    if (!validLength(d.thermalGap, false) || !validLength(d.spokeWidth, false) || !validLength(d.solderResistGuard, true) ||
        !validLength(d.silkClearance, true) || !validLength(d.curveTolerance, false)) {
        return tr("Default sizes must be between 0 and 100 mm.");
    }
    return {};
}

QVector<ClearanceRule> effectiveClearanceRules(const DesignRules& rules) {
    if (!rules.clearanceRules.isEmpty()) return rules.clearanceRules;
    ClearanceRule rule;
    rule.padPad = rule.padTrace = rule.traceTrace = rule.graphic = rules.clearance;
    rule.edge = rules.boardEdgeClearance;
    return {rule};
}

QVector<NetClass> effectiveNetClasses(const DesignRules& rules) {
    if (!rules.netClasses.isEmpty()) return rules.netClasses;
    return {defaultClass(PowerNetClass, 0.635, 1.0, 0.5), defaultClass(SignalNetClass, DefaultTrackWidth, DefaultViaDiameter, DefaultViaDrill)};
}

double clearanceBetween(const DesignRules& rules, int layerMask, ClearanceObject a, ClearanceObject b) {
    const QVector<ClearanceRule> all = effectiveClearanceRules(rules);
    double result = 0.0;
    bool found = false;
    for (BoardLayer layer : {BoardLayer::TopCopper, BoardLayer::BottomCopper}) {
        if ((layerMask & layerBit(layer)) == 0) continue;
        for (const auto* rule : rulesForLayer(all, layerBit(layer))) {
            result = std::max(result, ruleValue(*rule, a, b));
            found = true;
        }
    }
    return found ? result : rules.clearance;
}

double edgeClearance(const DesignRules& rules, int layerMask) {
    const QVector<ClearanceRule> all = effectiveClearanceRules(rules);
    double result = 0.0;
    bool found = false;
    for (BoardLayer layer : {BoardLayer::TopCopper, BoardLayer::BottomCopper}) {
        if ((layerMask & layerBit(layer)) == 0) continue;
        for (const auto* rule : rulesForLayer(all, layerBit(layer))) {
            result = std::max(result, rule->edge);
            found = true;
        }
    }
    return found ? result : rules.boardEdgeClearance;
}

double largestClearance(const DesignRules& rules) {
    double result = rules.clearance;
    for (const auto& rule : effectiveClearanceRules(rules)) {
        result = std::max({result, rule.padPad, rule.padTrace, rule.traceTrace, rule.graphic});
    }
    for (const auto& netClass : rules.netClasses) result = std::max(result, netClass.clearance);
    return result;
}

QHash<QString, QString> netClassAssignments(const DesignRules& rules, const SketchDocument& schematic) {
    const QVector<NetClass> classes = effectiveNetClasses(rules);
    QHash<QString, QString> explicitClass;
    bool hasPower = false;
    bool hasSignal = false;
    for (const auto& netClass : classes) {
        for (const QString& net : netClass.nets) explicitClass.insert(net, netClass.name);
        hasPower = hasPower || netClass.name == PowerNetClass;
        hasSignal = hasSignal || netClass.name == SignalNetClass;
    }
    const CircuitSnapshot snapshot = analyzeSchematic(schematic);
    QHash<QString, QString> variants;
    for (const auto& item : schematic) variants.insert(item.id, item.variant);
    QSet<int> powerNets;
    if (snapshot.connectivity.pinNets.size() == snapshot.input.pins.size()) {
        for (int p = 0; p < static_cast<int>(snapshot.input.pins.size()); ++p) {
            const QString variant = variants.value(QString::fromStdString(snapshot.input.pins[p].component));
            if (variant == QLatin1String("schematic.power") || variant == QLatin1String("schematic.ground")) {
                powerNets.insert(snapshot.connectivity.pinNets[p]);
            }
        }
    }
    QHash<QString, QString> result;
    const QString fallback = classes.isEmpty() ? QString() : classes.first().name;
    for (int n = 0; n < static_cast<int>(snapshot.connectivity.nets.size()); ++n) {
        const QString net = QString::fromStdString(snapshot.connectivity.nets[n].name);
        if (explicitClass.contains(net)) result.insert(net, explicitClass.value(net));
        else if (powerNets.contains(n) || net == QLatin1String("0")) result.insert(net, hasPower ? PowerNetClass : fallback);
        else result.insert(net, hasSignal ? SignalNetClass : fallback);
    }
    // Explicit assignments of nets the schematic does not have (yet) still count.
    for (auto it = explicitClass.cbegin(); it != explicitClass.cend(); ++it) {
        if (!result.contains(it.key())) result.insert(it.key(), it.value());
    }
    return result;
}

NetClass netClassForNet(const DesignRules& rules, const SketchDocument& schematic, const QString& net) {
    const QVector<NetClass> classes = effectiveNetClasses(rules);
    const QString name = netClassAssignments(rules, schematic).value(net);
    for (const auto& netClass : classes) {
        if (netClass.name == name) return netClass;
    }
    for (const auto& netClass : classes) {
        if (netClass.name == SignalNetClass) return netClass;
    }
    return classes.isEmpty() ? NetClass{} : classes.first();
}

QHash<QString, double> netClassClearances(const DesignRules& rules, const SketchDocument& schematic) {
    QHash<QString, double> byClass;
    for (const auto& netClass : effectiveNetClasses(rules)) {
        if (netClass.clearance > 0.0) byClass.insert(netClass.name, netClass.clearance);
    }
    QHash<QString, double> result;
    if (byClass.isEmpty()) return result; // skips the netlist when no class sets a clearance
    const QHash<QString, QString> assignments = netClassAssignments(rules, schematic);
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        if (const auto found = byClass.constFind(it.value()); found != byClass.cend()) result.insert(it.key(), *found);
    }
    return result;
}

double netPairClearance(const QHash<QString, double>& classClearances, double ruleClearance, const QString& a,
                        const QString& b) {
    return std::max({ruleClearance, classClearances.value(a, 0.0), classClearances.value(b, 0.0)});
}

QJsonObject designRulesToJson(const DesignRules& rules) {
    QJsonObject object{{QStringLiteral("clearance"), rules.clearance},
                       {QStringLiteral("minTrackWidth"), rules.minTrackWidth},
                       {QStringLiteral("minDrill"), rules.minDrill},
                       {QStringLiteral("minAnnularRing"), rules.minAnnularRing},
                       {QStringLiteral("boardEdgeClearance"), rules.boardEdgeClearance}};
    if (!rules.clearanceRules.isEmpty()) {
        QJsonArray list;
        for (const auto& rule : rules.clearanceRules) {
            list.append(QJsonObject{{QStringLiteral("name"), rule.name},
                                    {QStringLiteral("region"), ruleRegionToken(rule.region)},
                                    {QStringLiteral("padPad"), rule.padPad},
                                    {QStringLiteral("padTrace"), rule.padTrace},
                                    {QStringLiteral("traceTrace"), rule.traceTrace},
                                    {QStringLiteral("graphic"), rule.graphic},
                                    {QStringLiteral("edge"), rule.edge}});
        }
        object[QStringLiteral("clearanceRules")] = list;
    }
    if (!rules.netClasses.isEmpty()) {
        QJsonArray list;
        for (const auto& netClass : rules.netClasses) {
            QJsonObject entry{{QStringLiteral("name"), netClass.name},
                              {QStringLiteral("traceWidth"), netClass.traceWidth},
                              {QStringLiteral("viaDiameter"), netClass.viaDiameter},
                              {QStringLiteral("viaDrill"), netClass.viaDrill},
                              {QStringLiteral("layers"), layerTokens(netClass.layers)},
                              {QStringLiteral("nets"), QJsonArray::fromStringList(netClass.nets)}};
            if (netClass.neckWidth > 0) entry[QStringLiteral("neckWidth")] = netClass.neckWidth;
            if (netClass.clearance > 0) entry[QStringLiteral("clearance")] = netClass.clearance;
            if (!netClass.ratsnestColor.isEmpty()) entry[QStringLiteral("ratsnestColor")] = netClass.ratsnestColor;
            if (netClass.ratsnestHidden) entry[QStringLiteral("ratsnestHidden")] = true;
            list.append(entry);
        }
        object[QStringLiteral("netClasses")] = list;
    }
    if (!rules.differentialPairs.isEmpty()) {
        QJsonArray list;
        for (const auto& pair : rules.differentialPairs) {
            list.append(QJsonObject{{QStringLiteral("name"), pair.name},
                                    {QStringLiteral("positiveNet"), pair.positiveNet},
                                    {QStringLiteral("negativeNet"), pair.negativeNet},
                                    {QStringLiteral("width"), pair.width},
                                    {QStringLiteral("gap"), pair.gap}});
        }
        object[QStringLiteral("differentialPairs")] = list;
    }
    if (!(rules.defaults == RuleDefaults{})) {
        const RuleDefaults& d = rules.defaults;
        object[QStringLiteral("defaults")] = QJsonObject{{QStringLiteral("thermalRelief"), d.thermalRelief},
                                                         {QStringLiteral("thermalGap"), d.thermalGap},
                                                         {QStringLiteral("spokeWidth"), d.spokeWidth},
                                                         {QStringLiteral("solderResistGuard"), d.solderResistGuard},
                                                         {QStringLiteral("silkClearance"), d.silkClearance},
                                                         {QStringLiteral("curveTolerance"), d.curveTolerance}};
    }
    return object;
}

QString designRulesFromJson(const QJsonValue& value, DesignRules& rules) {
    if (value.isUndefined()) return {};
    const QString invalid = tr("The design rules section is invalid.");
    if (!value.isObject()) return invalid;
    const QJsonObject object = value.toObject();
    bool valid = readNumber(object, "clearance", rules.clearance) && readNumber(object, "minTrackWidth", rules.minTrackWidth) &&
                 readNumber(object, "minDrill", rules.minDrill) && readNumber(object, "minAnnularRing", rules.minAnnularRing) &&
                 readNumber(object, "boardEdgeClearance", rules.boardEdgeClearance);
    for (const char* key : {"clearanceRules", "netClasses", "differentialPairs"}) {
        const QJsonValue list = object.value(QLatin1String(key));
        valid = valid && (list.isUndefined() || list.isArray());
    }
    if (!valid) return invalid;

    for (const QJsonValue& entry : object.value(QStringLiteral("clearanceRules")).toArray()) {
        const QJsonObject o = entry.toObject();
        ClearanceRule rule;
        QString region = ruleRegionToken(rule.region);
        bool regionKnown = false;
        valid = entry.isObject() && readText(o, "name", rule.name) && readText(o, "region", region) &&
                readNumber(o, "padPad", rule.padPad) && readNumber(o, "padTrace", rule.padTrace) &&
                readNumber(o, "traceTrace", rule.traceTrace) && readNumber(o, "graphic", rule.graphic) &&
                readNumber(o, "edge", rule.edge);
        for (const auto& token : RegionTokens) {
            if (region == QLatin1String(token.token)) {
                rule.region = token.region;
                regionKnown = true;
            }
        }
        if (!valid || !regionKnown) return invalid;
        rules.clearanceRules.append(rule);
    }
    for (const QJsonValue& entry : object.value(QStringLiteral("netClasses")).toArray()) {
        const QJsonObject o = entry.toObject();
        NetClass netClass;
        valid = entry.isObject() && readText(o, "name", netClass.name) && readNumber(o, "traceWidth", netClass.traceWidth) &&
                readNumber(o, "viaDiameter", netClass.viaDiameter) && readNumber(o, "viaDrill", netClass.viaDrill) &&
                readNumber(o, "neckWidth", netClass.neckWidth) && readNumber(o, "clearance", netClass.clearance) &&
                readText(o, "ratsnestColor", netClass.ratsnestColor) &&
                readBool(o, "ratsnestHidden", netClass.ratsnestHidden);
        const QJsonValue layers = o.value(QStringLiteral("layers"));
        if (!layers.isUndefined()) {
            valid = valid && layers.isArray();
            netClass.layers = 0;
            for (const QJsonValue& token : layers.toArray()) {
                if (token.toString() == QLatin1String("top-copper")) netClass.layers |= layerBit(BoardLayer::TopCopper);
                else if (token.toString() == QLatin1String("bottom-copper")) netClass.layers |= layerBit(BoardLayer::BottomCopper);
                else valid = false;
            }
        }
        const QJsonValue nets = o.value(QStringLiteral("nets"));
        valid = valid && (nets.isUndefined() || nets.isArray());
        for (const QJsonValue& net : nets.toArray()) {
            valid = valid && net.isString();
            netClass.nets << net.toString();
        }
        if (!valid) return invalid;
        rules.netClasses.append(netClass);
    }
    for (const QJsonValue& entry : object.value(QStringLiteral("differentialPairs")).toArray()) {
        const QJsonObject o = entry.toObject();
        DifferentialPair pair;
        valid = entry.isObject() && readText(o, "name", pair.name) && readText(o, "positiveNet", pair.positiveNet) &&
                readText(o, "negativeNet", pair.negativeNet) && readNumber(o, "width", pair.width) && readNumber(o, "gap", pair.gap);
        if (!valid) return invalid;
        rules.differentialPairs.append(pair);
    }
    const QJsonValue defaults = object.value(QStringLiteral("defaults"));
    if (!defaults.isUndefined()) {
        const QJsonObject o = defaults.toObject();
        RuleDefaults& d = rules.defaults;
        valid = defaults.isObject() && readBool(o, "thermalRelief", d.thermalRelief) && readNumber(o, "thermalGap", d.thermalGap) &&
                readNumber(o, "spokeWidth", d.spokeWidth) && readNumber(o, "solderResistGuard", d.solderResistGuard) &&
                readNumber(o, "silkClearance", d.silkClearance) && readNumber(o, "curveTolerance", d.curveTolerance);
        if (!valid) return invalid;
    }
    const QString problem = validateDesignRules(rules);
    return problem.isEmpty() ? QString() : tr("The design rules are invalid: %1").arg(problem);
}

} // namespace hatt::ui
