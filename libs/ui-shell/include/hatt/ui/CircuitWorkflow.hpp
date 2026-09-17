#pragma once
#include "hatt/ui/DesignCanvas.hpp"
#include "hatt/ui/DesignRules.hpp"
#include <QObject>
#include <QPointer>
#include <atomic>
#include <functional>
#include <memory>

class QMenu;
class QTextEdit;
class QAction;
class QTimer;
class QProgressDialog;
namespace hatt::ui {
class FreeroutingRunner;
class CircuitWorkflow final : public QObject {
    Q_OBJECT
public:
    using ShowReport = std::function<void(const QString&, const QString&, QWidget*)>;
    CircuitWorkflow(QWidget* host, QMenu* menu, DesignCanvas* schematic, DesignCanvas* board,
                    ShowReport showReport, std::function<bool()> projectOpen,
                    std::function<void()> showBoard,
                    std::function<DesignRules()> designRules = [] { return DesignRules{}; },
                    std::function<void()> configureAutorouter = [] {});
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
    // Circuit > Auto Router... (hatteda.action.auto-route): hands off to `configureAutorouter_`
    // (issue #49; MainWindow opens the Design Rules dialog's Autorouter tab) rather than showing its
    // own settings dialog. Routing itself starts from there via `runAutorouter()`.
    void showAutorouter();
    // Exports the current unrouted board to a local Freerouting process (settings read from
    // QSettings pcb/freerouting/*, as left by the Design Rules dialog's Autorouter tab) and imports
    // its SES result after HattEDA validation. The imported routing is one undo step.
    void runAutorouter();
    void runDc();
    void cancelDc();
    // Interactive simulation (Proteus play/stop): solves the DC operating point, shows voltage
    // probe readings on the schematic and re-solves after every schematic edit until stopped.
    void startSimulation();
    void stopSimulation();
    [[nodiscard]] bool simulationRunning() const noexcept { return live_; }
    // Enables run/stop actions for the current project state (the host calls it on project changes).
    void updateSimulationActions();
    void loadExample();
signals:
    void statusMessage(const QString& message);
    void simulationStateChanged(bool running);
private:
    // Starts a solve; `reveal` opens the results workspace (one-shot runs), live runs stay on the
    // schematic and only reveal the report for errors.
    void solve(bool reveal);
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
    std::function<DesignRules()> designRules_;
    std::function<void()> configureAutorouter_;
    QPointer<QTextEdit> netlist_;
    QPointer<QTextEdit> simulation_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    QAction* run_ = nullptr;
    QAction* cancel_ = nullptr;
    QAction* start_ = nullptr;
    QAction* stop_ = nullptr;
    QAction* autoroute_ = nullptr;
    FreeroutingRunner* autorouter_ = nullptr;
    QPointer<QProgressDialog> autorouteProgress_;
    QTimer* resolveTimer_ = nullptr;
    bool live_ = false;
    bool running_ = false;
    quint64 revision_ = 0;
};
}
