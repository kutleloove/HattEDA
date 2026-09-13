#pragma once
#include "hatt/ui/DesignCanvas.hpp"
#include <QObject>
#include <QPointer>
#include <atomic>
#include <functional>
#include <memory>

class QMenu;
class QTextEdit;
class QAction;
namespace hatt::ui {
class CircuitWorkflow final : public QObject {
    Q_OBJECT
public:
    using ShowReport = std::function<void(const QString&, const QString&, QWidget*)>;
    CircuitWorkflow(QWidget* host, QMenu* menu, DesignCanvas* schematic, DesignCanvas* board,
                    ShowReport showReport, std::function<bool()> projectOpen,
                    std::function<void()> showBoard);
    ~CircuitWorkflow() override;
public slots:
    void showNetlist();
    // Asks for a file and writes netlistText (plain text, .net).
    void exportNetlist();
    void updateBoard();
    // Places every board part that is not on the board yet (one undo step); returns how many.
    int autoPlace(double grid, double spacing);
    // Auto placer dialog (AutoPlacerDialog) with grid and spacing, remembered in QSettings.
    void showAutoPlacer();
    void runDc();
    void cancelDc();
    void loadExample();
private:
    void schematicChanged();
    void refreshGuidance();
    QString netlistHtml() const;
    QTextEdit* report(QPointer<QTextEdit>& slot, const QString& id, const QString& title);
    QWidget* host_;
    DesignCanvas* schematic_;
    DesignCanvas* board_;
    ShowReport showReport_;
    std::function<bool()> projectOpen_;
    std::function<void()> showBoard_;
    QPointer<QTextEdit> netlist_;
    QPointer<QTextEdit> simulation_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    QAction* run_ = nullptr;
    QAction* cancel_ = nullptr;
    bool running_ = false;
    quint64 revision_ = 0;
};
}
