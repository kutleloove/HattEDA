#include "hatt/ui/DesignRuleManager.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace hatt::ui {
namespace {

QDoubleSpinBox* lengthField(QWidget* parent, const QString& name, double minimum = 0.0) {
    auto* field = new QDoubleSpinBox(parent);
    field->setObjectName(name);
    field->setDecimals(3);
    field->setRange(minimum, 100.0);
    field->setSingleStep(0.05);
    field->setSuffix(QStringLiteral(" mm"));
    return field;
}

QPushButton* button(QWidget* parent, const QString& name, const QString& text) {
    auto* result = new QPushButton(text, parent);
    result->setObjectName(name);
    return result;
}

double parseLength(const QString& text, bool* ok) {
    const QString trimmed = text.trimmed();
    double value = QLocale::c().toDouble(trimmed, ok);
    if (!*ok) value = QLocale().toDouble(trimmed, ok);
    return value;
}

enum PairColumn { PairName, PairPositive, PairNegative, PairWidth, PairGap };

} // namespace

DesignRuleManagerDialog::DesignRuleManagerDialog(const DesignRules& rules, const QStringList& nets,
                                                 const QHash<QString, QString>& automaticClasses, QWidget* parent)
    : QDialog(parent), working_(rules), nets_(nets), automaticClasses_(automaticClasses) {
    setObjectName(QStringLiteral("DesignRuleManagerDialog"));
    setWindowTitle(tr("Design Rule Manager"));
    resize(720, 520);
    working_.clearanceRules = effectiveClearanceRules(rules);
    working_.netClasses = effectiveNetClasses(rules);
    nets_.sort();

    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("RuleTabs"));
    tabs->addTab(createRulesTab(), tr("Design Rules"));
    tabs->addTab(createNetClassesTab(), tr("Net Classes"));
    tabs->addTab(createPairsTab(), tr("Differential Pairs"));
    tabs->addTab(createDefaultsTab(), tr("Defaults"));
    layout->addWidget(tabs, 1);
    validation_ = new QLabel(this);
    validation_->setObjectName(QStringLiteral("RulesValidation"));
    validation_->setWordWrap(true);
    layout->addWidget(validation_);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons_);

    refreshRuleList();
    showRule(0);
    classCombo_->setCurrentIndex(0);
    showNetClass(0);
    validate();
}

QWidget* DesignRuleManagerDialog::createRulesTab() {
    auto* page = new QWidget(this);
    auto* layout = new QHBoxLayout(page);
    auto* left = new QVBoxLayout;
    ruleList_ = new QListWidget(page);
    ruleList_->setObjectName(QStringLiteral("RuleList"));
    left->addWidget(ruleList_, 1);
    auto* ruleButtons = new QHBoxLayout;
    auto* ruleNew = button(page, QStringLiteral("RuleNew"), tr("New"));
    auto* ruleClone = button(page, QStringLiteral("RuleClone"), tr("Clone"));
    ruleDelete_ = button(page, QStringLiteral("RuleDelete"), tr("Delete"));
    for (auto* item : {ruleNew, ruleClone, ruleDelete_}) ruleButtons->addWidget(item);
    left->addLayout(ruleButtons);
    layout->addLayout(left, 1);

    auto* right = new QVBoxLayout;
    auto* ruleBox = new QGroupBox(tr("Clearances of the selected rule"), page);
    auto* form = new QFormLayout(ruleBox);
    ruleName_ = new QLineEdit(page);
    ruleName_->setObjectName(QStringLiteral("RuleName"));
    form->addRow(tr("Name"), ruleName_);
    ruleRegion_ = new QComboBox(page);
    ruleRegion_->setObjectName(QStringLiteral("RuleRegion"));
    for (RuleRegion region : {RuleRegion::Board, RuleRegion::TopCopper, RuleRegion::BottomCopper}) {
        ruleRegion_->addItem(ruleRegionName(region), static_cast<int>(region));
    }
    form->addRow(tr("Region"), ruleRegion_);
    padPad_ = lengthField(page, QStringLiteral("RulePadPad"));
    form->addRow(tr("Pad - pad"), padPad_);
    padTrace_ = lengthField(page, QStringLiteral("RulePadTrace"));
    form->addRow(tr("Pad - trace"), padTrace_);
    traceTrace_ = lengthField(page, QStringLiteral("RuleTraceTrace"));
    form->addRow(tr("Trace - trace"), traceTrace_);
    graphic_ = lengthField(page, QStringLiteral("RuleGraphic"));
    graphic_->setToolTip(tr("Copper graphics and zone pours to other copper"));
    form->addRow(tr("Graphics"), graphic_);
    edge_ = lengthField(page, QStringLiteral("RuleEdge"));
    form->addRow(tr("Board edge"), edge_);
    right->addWidget(ruleBox);

    auto* minimums = new QGroupBox(tr("Manufacturing minimums (whole board)"), page);
    auto* minimumForm = new QFormLayout(minimums);
    minTrack_ = lengthField(page, QStringLiteral("RulesTrackWidth"));
    minTrack_->setValue(working_.minTrackWidth);
    minimumForm->addRow(tr("Minimum track width"), minTrack_);
    minDrill_ = lengthField(page, QStringLiteral("RulesDrill"));
    minDrill_->setValue(working_.minDrill);
    minimumForm->addRow(tr("Minimum hole"), minDrill_);
    minRing_ = lengthField(page, QStringLiteral("RulesAnnularRing"));
    minRing_->setValue(working_.minAnnularRing);
    minimumForm->addRow(tr("Minimum annular ring"), minRing_);
    right->addWidget(minimums);
    right->addStretch();
    layout->addLayout(right, 1);

    connect(ruleList_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!loading_) showRule(row);
    });
    connect(ruleNew, &QPushButton::clicked, this, [this] {
        storeRule();
        ClearanceRule rule;
        int suffix = working_.clearanceRules.size() + 1;
        auto taken = [this](const QString& name) {
            return std::any_of(working_.clearanceRules.begin(), working_.clearanceRules.end(),
                               [&name](const ClearanceRule& r) { return r.name == name; });
        };
        do { rule.name = QStringLiteral("RULE%1").arg(suffix++); } while (taken(rule.name));
        working_.clearanceRules.append(rule);
        refreshRuleList();
        showRule(working_.clearanceRules.size() - 1);
    });
    connect(ruleClone, &QPushButton::clicked, this, [this] {
        storeRule();
        if (currentRule_ < 0) return;
        ClearanceRule rule = working_.clearanceRules[currentRule_];
        rule.name += tr(" copy");
        working_.clearanceRules.append(rule);
        refreshRuleList();
        showRule(working_.clearanceRules.size() - 1);
    });
    connect(ruleDelete_, &QPushButton::clicked, this, [this] {
        if (currentRule_ < 0 || working_.clearanceRules.size() <= 1) return;
        working_.clearanceRules.removeAt(currentRule_);
        currentRule_ = -1;
        refreshRuleList();
        showRule(0);
    });
    connect(ruleName_, &QLineEdit::textChanged, this, [this] { storeRule(); });
    connect(ruleRegion_, &QComboBox::currentIndexChanged, this, [this] { storeRule(); });
    for (auto* field : {padPad_, padTrace_, traceTrace_, graphic_, edge_, minTrack_, minDrill_, minRing_}) {
        connect(field, &QDoubleSpinBox::valueChanged, this, [this] { storeRule(); });
    }
    return page;
}

void DesignRuleManagerDialog::refreshRuleList() {
    loading_ = true;
    const QSignalBlocker blocker(ruleList_);
    ruleList_->clear();
    for (const auto& rule : working_.clearanceRules) {
        ruleList_->addItem(QStringLiteral("%1  ·  %2").arg(rule.name, ruleRegionName(rule.region)));
    }
    ruleDelete_->setEnabled(working_.clearanceRules.size() > 1);
    loading_ = false;
}

void DesignRuleManagerDialog::showRule(int index) {
    if (index < 0 || index >= working_.clearanceRules.size()) return;
    currentRule_ = index;
    loading_ = true;
    const ClearanceRule& rule = working_.clearanceRules[index];
    {
        const QSignalBlocker blocker(ruleList_);
        ruleList_->setCurrentRow(index);
    }
    ruleName_->setText(rule.name);
    ruleRegion_->setCurrentIndex(ruleRegion_->findData(static_cast<int>(rule.region)));
    padPad_->setValue(rule.padPad);
    padTrace_->setValue(rule.padTrace);
    traceTrace_->setValue(rule.traceTrace);
    graphic_->setValue(rule.graphic);
    edge_->setValue(rule.edge);
    loading_ = false;
    validate();
}

void DesignRuleManagerDialog::storeRule() {
    if (loading_) return;
    working_.minTrackWidth = minTrack_->value();
    working_.minDrill = minDrill_->value();
    working_.minAnnularRing = minRing_->value();
    if (currentRule_ >= 0 && currentRule_ < working_.clearanceRules.size()) {
        ClearanceRule& rule = working_.clearanceRules[currentRule_];
        rule.name = ruleName_->text().trimmed();
        rule.region = static_cast<RuleRegion>(ruleRegion_->currentData().toInt());
        rule.padPad = padPad_->value();
        rule.padTrace = padTrace_->value();
        rule.traceTrace = traceTrace_->value();
        rule.graphic = graphic_->value();
        rule.edge = edge_->value();
        if (auto* item = ruleList_->item(currentRule_)) {
            item->setText(QStringLiteral("%1  ·  %2").arg(rule.name, ruleRegionName(rule.region)));
        }
    }
    validate();
}

QWidget* DesignRuleManagerDialog::createNetClassesTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    auto* top = new QHBoxLayout;
    classCombo_ = new QComboBox(page);
    classCombo_->setObjectName(QStringLiteral("NetClassCombo"));
    for (const auto& netClass : working_.netClasses) classCombo_->addItem(netClass.name);
    top->addWidget(new QLabel(tr("Net class"), page));
    top->addWidget(classCombo_, 1);
    auto* classNew = button(page, QStringLiteral("NetClassNew"), tr("New..."));
    classDelete_ = button(page, QStringLiteral("NetClassDelete"), tr("Delete"));
    top->addWidget(classNew);
    top->addWidget(classDelete_);
    layout->addLayout(top);

    auto* columns = new QHBoxLayout;
    auto* styles = new QGroupBox(tr("Routing"), page);
    auto* form = new QFormLayout(styles);
    traceWidth_ = lengthField(page, QStringLiteral("NetClassTraceWidth"));
    form->addRow(tr("Trace width"), traceWidth_);
    neckWidth_ = lengthField(page, QStringLiteral("NetClassNeckWidth"));
    neckWidth_->setSpecialValueText(tr("no neck"));
    form->addRow(tr("Neck width"), neckWidth_);
    classClearance_ = lengthField(page, QStringLiteral("NetClassClearance"));
    classClearance_->setSpecialValueText(tr("design rules"));
    classClearance_->setToolTip(tr("Copper of other nets keeps at least this gap from the nets of this class, "
                                   "on top of the clearance rules. Tracks started on these nets route at the "
                                   "class trace width."));
    form->addRow(tr("Clearance"), classClearance_);
    viaDiameter_ = lengthField(page, QStringLiteral("NetClassViaDiameter"));
    form->addRow(tr("Via diameter"), viaDiameter_);
    viaDrill_ = lengthField(page, QStringLiteral("NetClassViaDrill"));
    form->addRow(tr("Via drill"), viaDrill_);
    classTop_ = new QCheckBox(tr("Top copper"), page);
    classTop_->setObjectName(QStringLiteral("NetClassTop"));
    classBottom_ = new QCheckBox(tr("Bottom copper"), page);
    classBottom_->setObjectName(QStringLiteral("NetClassBottom"));
    auto* layers = new QHBoxLayout;
    layers->addWidget(classTop_);
    layers->addWidget(classBottom_);
    form->addRow(tr("Layers"), layers);
    ratsnestColor_ = new QLineEdit(page);
    ratsnestColor_->setObjectName(QStringLiteral("NetClassRatsnestColor"));
    ratsnestColor_->setPlaceholderText(tr("theme colour, or #rrggbb"));
    form->addRow(tr("Ratsnest colour"), ratsnestColor_);
    ratsnestHidden_ = new QCheckBox(tr("Hide ratsnest"), page);
    ratsnestHidden_->setObjectName(QStringLiteral("NetClassRatsnestHidden"));
    form->addRow(QString(), ratsnestHidden_);
    columns->addWidget(styles, 1);

    auto* assignment = new QGroupBox(tr("Nets"), page);
    auto* assignLayout = new QHBoxLayout(assignment);
    auto* availableColumn = new QVBoxLayout;
    availableColumn->addWidget(new QLabel(tr("Other nets"), page));
    availableNets_ = new QListWidget(page);
    availableNets_->setObjectName(QStringLiteral("NetClassAvailableNets"));
    availableNets_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    availableColumn->addWidget(availableNets_);
    assignLayout->addLayout(availableColumn);
    auto* moveColumn = new QVBoxLayout;
    moveColumn->addStretch();
    auto* assign = button(page, QStringLiteral("NetClassAssign"), tr("Add →"));
    auto* unassign = button(page, QStringLiteral("NetClassUnassign"), tr("← Remove"));
    moveColumn->addWidget(assign);
    moveColumn->addWidget(unassign);
    moveColumn->addStretch();
    assignLayout->addLayout(moveColumn);
    auto* classColumn = new QVBoxLayout;
    classColumn->addWidget(new QLabel(tr("In this class"), page));
    classNets_ = new QListWidget(page);
    classNets_->setObjectName(QStringLiteral("NetClassNets"));
    classNets_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    classColumn->addWidget(classNets_);
    assignLayout->addLayout(classColumn);
    columns->addWidget(assignment, 1);
    layout->addLayout(columns, 1);
    auto* hint = new QLabel(tr("Nets without an explicit class use POWER when they contain a ground or power rail, "
                               "otherwise SIGNAL."),
                            page);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(classCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!loading_) showNetClass(index);
    });
    connect(classNew, &QPushButton::clicked, this, [this] {
        storeNetClass();
        NetClass netClass = currentClass_ >= 0 ? working_.netClasses[currentClass_] : NetClass{};
        netClass.nets.clear();
        int suffix = working_.netClasses.size() + 1;
        auto taken = [this](const QString& name) {
            return std::any_of(working_.netClasses.begin(), working_.netClasses.end(),
                               [&name](const NetClass& c) { return c.name == name; });
        };
        do { netClass.name = QStringLiteral("CLASS%1").arg(suffix++); } while (taken(netClass.name));
        working_.netClasses.append(netClass);
        loading_ = true;
        classCombo_->addItem(netClass.name);
        loading_ = false;
        classCombo_->setCurrentIndex(classCombo_->count() - 1);
    });
    connect(classDelete_, &QPushButton::clicked, this, [this] {
        if (currentClass_ < 0 || working_.netClasses.size() <= 1) return;
        const QString name = working_.netClasses[currentClass_].name;
        if (name == PowerNetClass || name == SignalNetClass) return;
        working_.netClasses.removeAt(currentClass_);
        loading_ = true;
        classCombo_->removeItem(currentClass_);
        currentClass_ = -1;
        loading_ = false;
        classCombo_->setCurrentIndex(0);
        showNetClass(0);
    });
    connect(assign, &QPushButton::clicked, this, [this] {
        if (currentClass_ < 0) return;
        for (auto* item : availableNets_->selectedItems()) {
            const QString net = item->data(Qt::UserRole).toString();
            for (auto& netClass : working_.netClasses) netClass.nets.removeAll(net);
            working_.netClasses[currentClass_].nets.append(net);
        }
        refreshNetLists();
        validate();
    });
    connect(unassign, &QPushButton::clicked, this, [this] {
        if (currentClass_ < 0) return;
        for (auto* item : classNets_->selectedItems()) working_.netClasses[currentClass_].nets.removeAll(item->text());
        refreshNetLists();
        validate();
    });
    for (auto* field : {traceWidth_, neckWidth_, classClearance_, viaDiameter_, viaDrill_}) {
        connect(field, &QDoubleSpinBox::valueChanged, this, [this] { storeNetClass(); });
    }
    for (auto* box : {classTop_, classBottom_, ratsnestHidden_}) {
        connect(box, &QCheckBox::toggled, this, [this] { storeNetClass(); });
    }
    connect(ratsnestColor_, &QLineEdit::textChanged, this, [this] { storeNetClass(); });
    return page;
}

void DesignRuleManagerDialog::showNetClass(int index) {
    if (index < 0 || index >= working_.netClasses.size()) return;
    currentClass_ = index;
    loading_ = true;
    const NetClass& netClass = working_.netClasses[index];
    traceWidth_->setValue(netClass.traceWidth);
    neckWidth_->setValue(netClass.neckWidth);
    classClearance_->setValue(netClass.clearance);
    viaDiameter_->setValue(netClass.viaDiameter);
    viaDrill_->setValue(netClass.viaDrill);
    classTop_->setChecked(netClass.layers & layerBit(BoardLayer::TopCopper));
    classBottom_->setChecked(netClass.layers & layerBit(BoardLayer::BottomCopper));
    ratsnestColor_->setText(netClass.ratsnestColor);
    ratsnestHidden_->setChecked(netClass.ratsnestHidden);
    classDelete_->setEnabled(netClass.name != PowerNetClass && netClass.name != SignalNetClass);
    loading_ = false;
    refreshNetLists();
    validate();
}

void DesignRuleManagerDialog::storeNetClass() {
    if (loading_ || currentClass_ < 0 || currentClass_ >= working_.netClasses.size()) return;
    NetClass& netClass = working_.netClasses[currentClass_];
    netClass.traceWidth = traceWidth_->value();
    netClass.neckWidth = neckWidth_->value();
    netClass.clearance = classClearance_->value();
    netClass.viaDiameter = viaDiameter_->value();
    netClass.viaDrill = viaDrill_->value();
    netClass.layers = (classTop_->isChecked() ? layerBit(BoardLayer::TopCopper) : 0) |
                      (classBottom_->isChecked() ? layerBit(BoardLayer::BottomCopper) : 0);
    netClass.ratsnestColor = ratsnestColor_->text().trimmed();
    netClass.ratsnestHidden = ratsnestHidden_->isChecked();
    validate();
}

void DesignRuleManagerDialog::refreshNetLists() {
    availableNets_->clear();
    classNets_->clear();
    if (currentClass_ < 0) return;
    const NetClass& current = working_.netClasses[currentClass_];
    QHash<QString, QString> explicitClass;
    for (const auto& netClass : working_.netClasses) {
        for (const QString& net : netClass.nets) explicitClass.insert(net, netClass.name);
    }
    QStringList all = nets_;
    for (const QString& net : current.nets) {
        if (!all.contains(net)) all.append(net);
    }
    for (const QString& net : std::as_const(all)) {
        if (current.nets.contains(net)) {
            classNets_->addItem(net);
            continue;
        }
        const QString owner = explicitClass.value(net);
        const QString shown = owner.isEmpty() ? tr("%1 (auto: %2)").arg(net, automaticClasses_.value(net, SignalNetClass))
                                              : tr("%1 (%2)").arg(net, owner);
        auto* item = new QListWidgetItem(shown, availableNets_);
        item->setData(Qt::UserRole, net);
    }
}

QWidget* DesignRuleManagerDialog::createPairsTab() {
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);
    pairTable_ = new QTableWidget(0, 5, page);
    pairTable_->setObjectName(QStringLiteral("PairTable"));
    pairTable_->setHorizontalHeaderLabels({tr("Name"), tr("Positive net"), tr("Negative net"), tr("Width (mm)"), tr("Gap (mm)")});
    pairTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    pairTable_->verticalHeader()->hide();
    for (const auto& pair : working_.differentialPairs) {
        const int row = pairTable_->rowCount();
        pairTable_->insertRow(row);
        pairTable_->setItem(row, PairName, new QTableWidgetItem(pair.name));
        pairTable_->setItem(row, PairPositive, new QTableWidgetItem(pair.positiveNet));
        pairTable_->setItem(row, PairNegative, new QTableWidgetItem(pair.negativeNet));
        pairTable_->setItem(row, PairWidth, new QTableWidgetItem(QString::number(pair.width)));
        pairTable_->setItem(row, PairGap, new QTableWidgetItem(QString::number(pair.gap)));
    }
    layout->addWidget(pairTable_, 1);
    auto* pairButtons = new QHBoxLayout;
    auto* add = button(page, QStringLiteral("PairAdd"), tr("Add"));
    auto* remove = button(page, QStringLiteral("PairRemove"), tr("Remove"));
    pairButtons->addWidget(add);
    pairButtons->addWidget(remove);
    pairButtons->addStretch();
    layout->addLayout(pairButtons);
    auto* hint = new QLabel(tr("Differential pairs are stored with the project; routing and checks for them come later."), page);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    connect(add, &QPushButton::clicked, this, [this] {
        const int row = pairTable_->rowCount();
        pairTable_->insertRow(row);
        pairTable_->setItem(row, PairName, new QTableWidgetItem(QStringLiteral("PAIR%1").arg(row + 1)));
        pairTable_->setItem(row, PairPositive, new QTableWidgetItem(nets_.value(0)));
        pairTable_->setItem(row, PairNegative, new QTableWidgetItem(nets_.value(1)));
        pairTable_->setItem(row, PairWidth, new QTableWidgetItem(QStringLiteral("0.2")));
        pairTable_->setItem(row, PairGap, new QTableWidgetItem(QStringLiteral("0.2")));
        validate();
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (pairTable_->currentRow() >= 0) pairTable_->removeRow(pairTable_->currentRow());
        validate();
    });
    connect(pairTable_, &QTableWidget::cellChanged, this, [this] { validate(); });
    return page;
}

QWidget* DesignRuleManagerDialog::createDefaultsTab() {
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    const RuleDefaults& d = working_.defaults;
    thermalRelief_ = new QCheckBox(tr("Thermal relief spokes between pads and pours"), page);
    thermalRelief_->setObjectName(QStringLiteral("DefaultThermalRelief"));
    thermalRelief_->setChecked(d.thermalRelief);
    form->addRow(tr("Relief"), thermalRelief_);
    thermalGap_ = lengthField(page, QStringLiteral("DefaultThermalGap"));
    thermalGap_->setValue(d.thermalGap);
    form->addRow(tr("Thermal gap"), thermalGap_);
    spokeWidth_ = lengthField(page, QStringLiteral("DefaultSpokeWidth"));
    spokeWidth_->setValue(d.spokeWidth);
    form->addRow(tr("Spoke width"), spokeWidth_);
    solderResist_ = lengthField(page, QStringLiteral("DefaultSolderResist"));
    solderResist_->setValue(d.solderResistGuard);
    form->addRow(tr("Solder resist guard"), solderResist_);
    silkClearance_ = lengthField(page, QStringLiteral("DefaultSilkClearance"));
    silkClearance_->setValue(d.silkClearance);
    form->addRow(tr("Silkscreen to pad"), silkClearance_);
    curveTolerance_ = lengthField(page, QStringLiteral("DefaultCurveTolerance"));
    curveTolerance_->setValue(d.curveTolerance);
    form->addRow(tr("Curve tolerance"), curveTolerance_);
    connect(thermalRelief_, &QCheckBox::toggled, this, [this] { validate(); });
    for (auto* field : {thermalGap_, spokeWidth_, solderResist_, silkClearance_, curveTolerance_}) {
        connect(field, &QDoubleSpinBox::valueChanged, this, [this] { validate(); });
    }
    return page;
}

DesignRules DesignRuleManagerDialog::rules() const {
    DesignRules result = working_;
    result.minTrackWidth = minTrack_->value();
    result.minDrill = minDrill_->value();
    result.minAnnularRing = minRing_->value();

    result.differentialPairs.clear();
    for (int row = 0; row < pairTable_->rowCount(); ++row) {
        auto text = [this, row](int column) {
            const auto* item = pairTable_->item(row, column);
            return item != nullptr ? item->text().trimmed() : QString();
        };
        DifferentialPair pair;
        pair.name = text(PairName);
        pair.positiveNet = text(PairPositive);
        pair.negativeNet = text(PairNegative);
        bool widthOk = false;
        bool gapOk = false;
        pair.width = parseLength(text(PairWidth), &widthOk);
        pair.gap = parseLength(text(PairGap), &gapOk);
        if (!widthOk) pair.width = -1.0; // rejected by validateDesignRules
        if (!gapOk) pair.gap = -1.0;
        result.differentialPairs.append(pair);
    }

    RuleDefaults& d = result.defaults;
    d.thermalRelief = thermalRelief_->isChecked();
    d.thermalGap = thermalGap_->value();
    d.spokeWidth = spokeWidth_->value();
    d.solderResistGuard = solderResist_->value();
    d.silkClearance = silkClearance_->value();
    d.curveTolerance = curveTolerance_->value();

    // Global values follow the rules: the largest gap and edge of any rule, used by code that has
    // one clearance only.
    result.clearance = 0.0;
    result.boardEdgeClearance = 0.0;
    for (const auto& rule : result.clearanceRules) {
        result.clearance = std::max({result.clearance, rule.padPad, rule.padTrace, rule.traceTrace, rule.graphic});
        result.boardEdgeClearance = std::max(result.boardEdgeClearance, rule.edge);
    }
    if (result.clearanceRules.size() == 1) {
        const ClearanceRule& only = result.clearanceRules.first();
        if (only.name == ClearanceRule{}.name && only.region == RuleRegion::Board && only.padPad == only.padTrace &&
            only.padTrace == only.traceTrace && only.traceTrace == only.graphic) {
            result.clearanceRules.clear(); // the compact form: DEFAULT from the global values
        }
    }
    if (result.netClasses == effectiveNetClasses(DesignRules{})) result.netClasses.clear();
    return result;
}

void DesignRuleManagerDialog::validate() {
    if (validation_ == nullptr || buttons_ == nullptr) return;
    const QString problem = validateDesignRules(rules());
    validation_->setText(problem);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

} // namespace hatt::ui
