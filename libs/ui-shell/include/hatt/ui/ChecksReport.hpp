#pragma once

#include "hatt/ui/DesignChecks.hpp"

#include <QWidget>

class QLabel;
class QTreeWidget;

namespace hatt::ui {

// Tool workspace `hatteda.tool.design-checks`: the ERC and DRC results in one list
// (`ChecksTable`), errors first. Activating a row that has a location or items emits
// violationActivated so the host can show it on the canvas.
class ChecksReport final : public QWidget {
    Q_OBJECT

public:
    explicit ChecksReport(QWidget* parent = nullptr);

    void setResults(const CheckReport& electrical, const CheckReport& design);
    [[nodiscard]] const QVector<CheckViolation>& violations() const { return violations_; }
    // Activates row `row` as if the user clicked it.
    void activateRow(int row);

signals:
    void rerunRequested();
    void violationActivated(const hatt::ui::CheckViolation& violation);

private:
    QLabel* summary_ = nullptr;
    QTreeWidget* table_ = nullptr;
    QVector<CheckViolation> violations_;
};

} // namespace hatt::ui
