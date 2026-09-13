#include "hatt/ui/CircuitWorkflow.hpp"
#include "hatt/ui/SketchCircuit.hpp"
#include <QAction>
#include <QMenu>
#include <QMessageBox>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <exception>

namespace hatt::ui {
namespace {
QString messages(const QStringList& errors) {
    QString html = QStringLiteral("<ul>");
    for (const auto& error : errors) html += QStringLiteral("<li>%1</li>").arg(error.toHtmlEscaped());
    return html + QStringLiteral("</ul>");
}
}
CircuitWorkflow::CircuitWorkflow(QWidget* host, QMenu* menu, DesignCanvas* schematic, DesignCanvas* board,
                                 ShowReport showReport, std::function<bool()> projectOpen,
                                 std::function<void()> showBoard)
    : QObject(host), host_(host), schematic_(schematic), board_(board),
      showReport_(std::move(showReport)), projectOpen_(std::move(projectOpen)), showBoard_(std::move(showBoard)) {
    setObjectName(QStringLiteral("CircuitWorkflow"));
    auto add = [&](const char* id, const QString& title, auto callback) {
        auto* action = new QAction(title, host);
        action->setObjectName(QString::fromLatin1(id));
        menu->addAction(action);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    auto* net = add("hatteda.action.netlist", tr("Show netlist"), &CircuitWorkflow::showNetlist);
    auto* transfer = add("hatteda.action.update-pcb", tr("Update PCB from schematic"), &CircuitWorkflow::updateBoard);
    menu->addSeparator();
    run_ = add("hatteda.action.run-dc", tr("Run DC operating point"), &CircuitWorkflow::runDc);
    cancel_ = add("hatteda.action.cancel-dc", tr("Cancel simulation"), &CircuitWorkflow::cancelDc);
    auto* example = add("hatteda.action.dc-example", tr("Load DC divider example (empty schematic)"), &CircuitWorkflow::loadExample);
    cancel_->setEnabled(false);
    connect(menu, &QMenu::aboutToShow, this, [this, net, transfer, example] {
        const bool open = projectOpen_();
        net->setEnabled(open); transfer->setEnabled(open); example->setEnabled(open && !running_);
        run_->setEnabled(open && !running_); cancel_->setEnabled(running_);
    });
    connect(schematic_, &DesignCanvas::documentChanged, this, &CircuitWorkflow::schematicChanged);
    connect(board_, &DesignCanvas::documentChanged, this, &CircuitWorkflow::refreshGuidance);
}
CircuitWorkflow::~CircuitWorkflow() { cancelDc(); }

QTextEdit* CircuitWorkflow::report(QPointer<QTextEdit>& slot, const QString& id, const QString& title) {
    if (!slot) { slot = new QTextEdit; slot->setReadOnly(true); }
    // Host deduplicates by stable id and owns the workspace.
    showReport_(id, title, slot);
    return slot;
}

QString CircuitWorkflow::netlistHtml() const {
    const auto snapshot = analyzeSchematic(schematic_->document());
    QString html = QStringLiteral("<h2>%1</h2>").arg(tr("Schematic netlist").toHtmlEscaped());
    if (!snapshot.errors.isEmpty()) return html + messages(snapshot.errors);
    QHash<QString, QString> labels;
    for (const auto& item : schematic_->document()) {
        const auto* symbol = findSymbol(item.variant);
        labels.insert(item.id, item.label.isEmpty() && symbol ? symbolDisplayName(*symbol) : item.label);
    }
    html += QStringLiteral("<p>%1</p>").arg(tr("%1 nets. Net names and connections refresh after edits and undo.")
                                              .arg(snapshot.connectivity.nets.size()).toHtmlEscaped());
    html += QStringLiteral("<table border='1' cellpadding='6'><tr><th>%1</th><th>%2</th></tr>")
                .arg(tr("Net").toHtmlEscaped(), tr("Pins").toHtmlEscaped());
    for (const auto& net : snapshot.connectivity.nets) {
        QStringList pins;
        for (int p : net.pins) {
            const auto& pin = snapshot.input.pins[p];
            pins << labels.value(QString::fromStdString(pin.component)).toHtmlEscaped() + QLatin1Char('.') +
                        QString::fromStdString(pin.number).toHtmlEscaped();
        }
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>")
                    .arg(QString::fromStdString(net.name).toHtmlEscaped(), pins.join(QStringLiteral(", ")));
    }
    html += QStringLiteral("</table><h3>%1</h3>").arg(tr("PCB connection guide").toHtmlEscaped());
    const auto guide = boardGuidance(schematic_->document(), board_->document());
    html += QStringLiteral("<p>%1</p>").arg(tr("%1 unrouted connections.").arg(guide.airwires.size()).toHtmlEscaped());
    html += messages(guide.errors);
    html += QStringLiteral("<p>%1</p>").arg(tr("This is a connection guide for the current single copper layer, not a full ERC/DRC check. Project saving is not implemented yet.").toHtmlEscaped());
    return html;
}
void CircuitWorkflow::showNetlist() {
    if (!projectOpen_()) return;
    report(netlist_, QStringLiteral("hatteda.tool.netlist"), tr("Netlist"))->setHtml(netlistHtml());
}
void CircuitWorkflow::schematicChanged() {
    ++revision_;
    cancelDc();
    if (simulation_) simulation_->setHtml(QStringLiteral("<p>%1</p>")
        .arg(tr("Circuit changed. Previous results are out of date; run simulation again.").toHtmlEscaped()));
    refreshGuidance();
}
void CircuitWorkflow::refreshGuidance() {
    const auto guide = boardGuidance(schematic_->document(), board_->document());
    board_->setAirwires(guide.airwires);
    if (netlist_) netlist_->setHtml(netlistHtml());
}
void CircuitWorkflow::updateBoard() {
    if (!projectOpen_()) return;
    const auto transfer = transferToBoard(schematic_->document(), board_->document());
    if (!transfer.errors.isEmpty()) {
        QMessageBox::warning(host_, tr("PCB update"), transfer.errors.join(QLatin1Char('\n')));
        return;
    }
    if (transfer.added || transfer.updated)
        board_->applyDocumentEdit(tr("Update PCB from schematic"), transfer.document);
    refreshGuidance();
    showBoard_();
    if (transfer.added) board_->zoomToFit();
}
void CircuitWorkflow::loadExample() {
    if (!projectOpen_() || running_) return;
    if (!schematic_->document().isEmpty()) {
        QMessageBox::information(host_, tr("DC example"), tr("The example requires an empty schematic. Existing objects were preserved."));
        return;
    }
    schematic_->applyDocumentEdit(tr("Load DC divider example"), dcDividerExample());
    schematic_->zoomToFit();
}
void CircuitWorkflow::cancelDc() { if (cancelled_) cancelled_->store(true); }
void CircuitWorkflow::runDc() {
    if (!projectOpen_() || running_) return;
    const auto snapshot = analyzeSchematic(schematic_->document());
    auto* output = report(simulation_, QStringLiteral("hatteda.tool.dc-results"), tr("DC operating point"));
    const QString heading = QStringLiteral("<h2>%1</h2><p>%2</p>").arg(tr("DC operating point").toHtmlEscaped(),
        tr("Supported: resistors and independent DC voltage sources. Values use SI/SPICE suffixes (1k, 5, 1meg); unit labels are omitted.").toHtmlEscaped());
    if (!snapshot.errors.isEmpty() || !snapshot.simulationErrors.isEmpty()) {
        output->setHtml(heading + messages(snapshot.errors + snapshot.simulationErrors)); return;
    }
    running_ = true; run_->setEnabled(false); cancel_->setEnabled(true);
    output->setHtml(heading + QStringLiteral("<p>%1</p>").arg(tr("Running…").toHtmlEscaped()));
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    auto flag = cancelled_;
    auto result = std::make_shared<electrical::DcResult>();
    const quint64 revision = revision_;
    auto* worker = QThread::create([snapshot, flag, result] {
        try { *result = electrical::solveDc(snapshot.dc, flag.get()); }
        catch (const std::exception& e) { result->error = e.what(); }
        catch (...) { result->error = "Simulation failed."; }
    });
    // Worker owns immutable data only and can finish safely after the host closes.
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this, result, snapshot, flag, revision, heading] {
        running_ = false; cancel_->setEnabled(false); run_->setEnabled(projectOpen_());
        if (!simulation_ || revision != revision_) return;
        if (flag->load()) { simulation_->setHtml(heading + tr("Simulation cancelled.").toHtmlEscaped()); return; }
        if (!result->success) { simulation_->setHtml(heading + messages({QString::fromStdString(result->error)})); return; }
        QString html = heading + QStringLiteral("<table border='1' cellpadding='6'><tr><th>%1</th><th>V</th></tr>").arg(tr("Net").toHtmlEscaped());
        for (int n = 0; n < static_cast<int>(result->voltages.size()); ++n)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>")
                        .arg(QString::fromStdString(snapshot.connectivity.nets[n].name).toHtmlEscaped())
                        .arg(result->voltages[n], 0, 'g', 9);
        html += QStringLiteral("</table><h3>%1</h3><table border='1' cellpadding='6'>").arg(tr("Currents (pin 1 → pin 2)").toHtmlEscaped());
        for (int e = 0; e < static_cast<int>(result->currents.size()); ++e)
            html += QStringLiteral("<tr><td>%1</td><td>%2 A</td></tr>")
                        .arg(QString::fromStdString(snapshot.dc.elements[e].reference).toHtmlEscaped())
                        .arg(result->currents[e], 0, 'g', 9);
        simulation_->setHtml(html + QStringLiteral("</table>"));
    });
    worker->start();
    QTimer::singleShot(10000, this, [flag] { flag->store(true); });
}
}
