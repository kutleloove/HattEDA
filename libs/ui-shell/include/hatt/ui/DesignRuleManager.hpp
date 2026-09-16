#pragma once

#include "hatt/ui/DesignRules.hpp"

#include <QDialog>
#include <QHash>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace hatt::ui {

// Proteus ARES style Design Rule Manager (ADR-0010), `DesignRuleManagerDialog`, tabs `RuleTabs`:
//  - Design Rules: clearance rules (`RuleList`, `RuleNew`/`RuleClone`/`RuleDelete`, `RuleName`,
//    `RuleRegion`, `RulePadPad`, `RulePadTrace`, `RuleTraceTrace`, `RuleGraphic`, `RuleEdge`) and
//    the global minimums (`RulesTrackWidth`, `RulesDrill`, `RulesAnnularRing`).
//  - Net Classes (`NetClassCombo`, `NetClassNew`/`NetClassDelete`, `NetClassTraceWidth`,
//    `NetClassViaDiameter`, `NetClassViaDrill`, `NetClassNeckWidth`, `NetClassClearance`,
//    `NetClassTop`/`NetClassBottom`,
//    `NetClassRatsnestColor`, `NetClassRatsnestHidden`, `NetClassAvailableNets`, `NetClassNets`,
//    `NetClassAssign`/`NetClassUnassign`).
//  - Differential Pairs (`PairTable`: name, positive net, negative net, width, gap; `PairAdd`,
//    `PairRemove`).
//  - Defaults (`DefaultThermalRelief`, `DefaultThermalGap`, `DefaultSpokeWidth`,
//    `DefaultSolderResist`, `DefaultSilkClearance`, `DefaultCurveTolerance`).
//  - Autorouter (issue #49; `AutorouterLayers`, `AutorouterPasses`, `AutorouterTimeout`,
//    `AutorouterThreads`, `AutorouterUpdateStrategy`, `AutorouterSelectionStrategy`): where/how the
//    external Freerouting engine (ADR-0014) routes. These values are a machine-local run preference,
//    never project data — read from and written to `QSettings` `pcb/freerouting/*` (the same keys
//    `CircuitWorkflow` always used), never into `DesignRules`/the `.hatt` file. The `RouteBoard`
//    button next to OK/Cancel accepts the dialog (applying any rule edits, same as OK) and sets
//    `routeRequested()` so the host can start routing immediately with the settings just chosen.
// OK and Route Board are disabled while validateDesignRules fails; the reason is shown in
// `RulesValidation`.
class DesignRuleManagerDialog final : public QDialog {
    Q_OBJECT

public:
    // `nets` are the schematic net names, `automaticClasses` their POWER/SIGNAL fallback classes.
    // `openAutorouterTab` selects the Autorouter tab initially (used when the dialog is opened from
    // Circuit > Auto Router... rather than Design > Design rules...).
    DesignRuleManagerDialog(const DesignRules& rules, const QStringList& nets,
                            const QHash<QString, QString>& automaticClasses, QWidget* parent = nullptr,
                            bool openAutorouterTab = false);

    // The edited rules. A single DEFAULT Board rule with equal gaps and the unchanged default net
    // classes are stored in the compact form (global values only) so unchanged projects keep their
    // file contents.
    [[nodiscard]] DesignRules rules() const;

    // True when the dialog was accepted via the Route Board button rather than OK: the host should
    // apply rules() and then start autorouting with the current pcb/freerouting/* QSettings.
    [[nodiscard]] bool routeRequested() const noexcept { return routeRequested_; }

private:
    QWidget* createRulesTab();
    QWidget* createNetClassesTab();
    QWidget* createPairsTab();
    QWidget* createDefaultsTab();
    QWidget* createAutorouterTab();
    void showRule(int index);
    void storeRule();
    void refreshRuleList();
    void showNetClass(int index);
    void storeNetClass();
    void refreshNetLists();
    void refreshAutorouterLayers();
    void persistAutorouterSettings();
    void validate();

    DesignRules working_;
    QStringList nets_;
    QHash<QString, QString> automaticClasses_;
    bool loading_ = false;
    int currentRule_ = -1;
    int currentClass_ = -1;

    QListWidget* ruleList_ = nullptr;
    QPushButton* ruleDelete_ = nullptr;
    QLineEdit* ruleName_ = nullptr;
    QComboBox* ruleRegion_ = nullptr;
    QDoubleSpinBox* padPad_ = nullptr;
    QDoubleSpinBox* padTrace_ = nullptr;
    QDoubleSpinBox* traceTrace_ = nullptr;
    QDoubleSpinBox* graphic_ = nullptr;
    QDoubleSpinBox* edge_ = nullptr;
    QDoubleSpinBox* minTrack_ = nullptr;
    QDoubleSpinBox* minDrill_ = nullptr;
    QDoubleSpinBox* minRing_ = nullptr;

    QComboBox* classCombo_ = nullptr;
    QPushButton* classDelete_ = nullptr;
    QDoubleSpinBox* traceWidth_ = nullptr;
    QDoubleSpinBox* viaDiameter_ = nullptr;
    QDoubleSpinBox* viaDrill_ = nullptr;
    QDoubleSpinBox* neckWidth_ = nullptr;
    QDoubleSpinBox* classClearance_ = nullptr;
    QCheckBox* classTop_ = nullptr;
    QCheckBox* classBottom_ = nullptr;
    QLineEdit* ratsnestColor_ = nullptr;
    QCheckBox* ratsnestHidden_ = nullptr;
    QListWidget* availableNets_ = nullptr;
    QListWidget* classNets_ = nullptr;

    QTableWidget* pairTable_ = nullptr;

    QCheckBox* thermalRelief_ = nullptr;
    QDoubleSpinBox* thermalGap_ = nullptr;
    QDoubleSpinBox* spokeWidth_ = nullptr;
    QDoubleSpinBox* solderResist_ = nullptr;
    QDoubleSpinBox* silkClearance_ = nullptr;
    QDoubleSpinBox* curveTolerance_ = nullptr;

    QSpinBox* autorouterPasses_ = nullptr;
    QSpinBox* autorouterTimeout_ = nullptr;
    QSpinBox* autorouterThreads_ = nullptr;
    QComboBox* autorouterUpdateStrategy_ = nullptr;
    QComboBox* autorouterSelectionStrategy_ = nullptr;

    QLabel* validation_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    QPushButton* routeBoard_ = nullptr;
    bool routeRequested_ = false;
};

} // namespace hatt::ui
