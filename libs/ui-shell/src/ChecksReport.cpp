#include "hatt/ui/ChecksReport.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace hatt::ui {
namespace {

enum Column { SeverityColumn, AreaColumn, MessageColumn };
constexpr int RowRole = Qt::UserRole + 1;

} // namespace

ChecksReport::ChecksReport(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    auto* header = new QHBoxLayout;
    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("ChecksSummary"));
    summary_->setWordWrap(true);
    header->addWidget(summary_, 1);
    auto* rerun = new QPushButton(tr("Run again"), this);
    rerun->setObjectName(QStringLiteral("ChecksRerun"));
    connect(rerun, &QPushButton::clicked, this, &ChecksReport::rerunRequested);
    header->addWidget(rerun);
    layout->addLayout(header);

    table_ = new QTreeWidget(this);
    table_->setObjectName(QStringLiteral("ChecksTable"));
    table_->setRootIsDecorated(false);
    table_->setUniformRowHeights(true);
    table_->setHeaderLabels({tr("Severity"), tr("Check"), tr("Problem")});
    table_->header()->setSectionResizeMode(MessageColumn, QHeaderView::Stretch);
    layout->addWidget(table_, 1);
    auto* hint = new QLabel(tr("Click a problem to show it in the schematic or on the PCB."), this);
    hint->setObjectName(QStringLiteral("ChecksHint"));
    layout->addWidget(hint);
    connect(table_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* item) {
        activateRow(item->data(0, RowRole).toInt());
    });
    connect(table_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        activateRow(item->data(0, RowRole).toInt());
    });
}

void ChecksReport::setResults(const CheckReport& electrical, const CheckReport& design) {
    violations_ = electrical.violations;
    violations_ += design.violations;
    // Errors first; ERC (schematic) before DRC within a severity, keeping the check order.
    std::stable_sort(violations_.begin(), violations_.end(), [](const CheckViolation& a, const CheckViolation& b) {
        return a.severity == CheckSeverity::Error && b.severity != CheckSeverity::Error;
    });
    table_->clear();
    int errors = 0;
    for (int row = 0; row < violations_.size(); ++row) {
        const CheckViolation& violation = violations_[row];
        const bool error = violation.severity == CheckSeverity::Error;
        errors += error ? 1 : 0;
        auto* item = new QTreeWidgetItem(table_);
        item->setText(SeverityColumn, error ? tr("Error") : tr("Warning"));
        item->setText(AreaColumn, violation.rule.startsWith(QStringLiteral("erc.")) ? tr("ERC · schematic")
                                  : violation.workspace == Workspace::Schematic      ? tr("DRC · schematic")
                                                                                     : tr("DRC · PCB"));
        item->setText(MessageColumn, violation.message);
        item->setToolTip(MessageColumn, violation.rule);
        item->setData(0, RowRole, row);
        QFont font = item->font(SeverityColumn);
        font.setBold(error);
        item->setFont(SeverityColumn, font);
    }
    const int warnings = static_cast<int>(violations_.size()) - errors;
    summary_->setText(violations_.isEmpty()
                          ? tr("No problems found.")
                          : tr("%1 error(s), %2 warning(s).").arg(errors).arg(warnings));
}

void ChecksReport::activateRow(int row) {
    if (row < 0 || row >= violations_.size()) return;
    const CheckViolation& violation = violations_[row];
    if (violation.hasLocation || !violation.itemIds.isEmpty()) emit violationActivated(violation);
}

} // namespace hatt::ui
