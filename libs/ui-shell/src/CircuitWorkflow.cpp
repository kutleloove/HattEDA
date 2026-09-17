#include "hatt/ui/CircuitWorkflow.hpp"
#include "hatt/ui/DesignChecks.hpp"
#include "hatt/ui/FreeroutingRunner.hpp"
#include "hatt/ui/SketchCircuit.hpp"
#include "hatt/ui/SpecctraDsn.hpp"
#include "hatt/ui/SpecctraSes.hpp"
#include <QAction>
#include <QCoreApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QProgressDialog>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <algorithm>
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
                                 std::function<void()> showBoard, std::function<DesignRules()> designRules,
                                 std::function<void()> configureAutorouter)
    : QObject(host), host_(host), schematic_(schematic), board_(board),
      showReport_(std::move(showReport)), projectOpen_(std::move(projectOpen)),
      showBoard_(std::move(showBoard)), designRules_(std::move(designRules)),
      configureAutorouter_(std::move(configureAutorouter)) {
    setObjectName(QStringLiteral("CircuitWorkflow"));
    auto add = [&](const char* id, const QString& title, auto callback) {
        auto* action = new QAction(title, host);
        action->setObjectName(QString::fromLatin1(id));
        menu->addAction(action);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    auto* net = add("hatteda.action.netlist", tr("Show netlist"), &CircuitWorkflow::showNetlist);
    auto* exportNet = add("hatteda.action.export-netlist", tr("Export netlist..."), &CircuitWorkflow::exportNetlist);
    menu->addSeparator();
    auto* transfer = add("hatteda.action.update-pcb", tr("Netlist to PCB"), &CircuitWorkflow::updateBoard);
    transfer->setShortcut(QKeySequence(QStringLiteral("Alt+A")));
    transfer->setToolTip(tr("Update PCB from schematic: refresh linked footprints and auto place new parts"));
    auto* placer = add("hatteda.action.auto-place", tr("Auto placer..."), &CircuitWorkflow::showAutoPlacer);
    autoroute_ = add("hatteda.action.auto-route", tr("Auto Router..."), &CircuitWorkflow::showAutorouter);
    autoroute_->setToolTip(tr("Configure and run the PCB Auto Router; HattEDA validates the result with DRC"));
    menu->addSeparator();
    start_ = add("hatteda.action.simulation-start", tr("Start simulation"), &CircuitWorkflow::startSimulation);
    start_->setProperty("iconKind", QStringLiteral("play"));
    start_->setShortcut(QKeySequence(QStringLiteral("F12")));
    start_->setToolTip(tr("Start simulation (F12): probe voltages update while you edit"));
    stop_ = add("hatteda.action.simulation-stop", tr("Stop simulation"), &CircuitWorkflow::stopSimulation);
    stop_->setProperty("iconKind", QStringLiteral("stop"));
    stop_->setShortcut(QKeySequence(QStringLiteral("Shift+F12")));
    stop_->setToolTip(tr("Stop simulation (Shift+F12)"));
    run_ = add("hatteda.action.run-dc", tr("Run DC operating point"), &CircuitWorkflow::runDc);
    cancel_ = add("hatteda.action.cancel-dc", tr("Cancel simulation"), &CircuitWorkflow::cancelDc);
    auto* example = add("hatteda.action.dc-example", tr("Load DC divider example (empty schematic)"), &CircuitWorkflow::loadExample);
    cancel_->setEnabled(false);
    connect(menu, &QMenu::aboutToShow, this, [this, net, exportNet, transfer, placer, example] {
        const bool open = projectOpen_();
        net->setEnabled(open); exportNet->setEnabled(open); transfer->setEnabled(open); placer->setEnabled(open);
        autoroute_->setEnabled(open && !autorouter_->isRunning());
        example->setEnabled(open && !running_);
        updateSimulationActions();
    });
    autorouter_ = new FreeroutingRunner(this);
    resolveTimer_ = new QTimer(this);
    resolveTimer_->setSingleShot(true);
    resolveTimer_->setInterval(150);
    connect(resolveTimer_, &QTimer::timeout, this, [this] { if (live_) solve(false); });
    updateSimulationActions();
    connect(schematic_, &DesignCanvas::documentChanged, this, &CircuitWorkflow::schematicChanged);
    connect(board_, &DesignCanvas::documentChanged, this, &CircuitWorkflow::refreshGuidance);
}
CircuitWorkflow::~CircuitWorkflow() {
    cancelDc();
    autorouter_->cancel();
}

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
    html += QStringLiteral("<p>%1</p>").arg(tr("This is a connection guide for tracks, vias and pads; it does not account for copper pours and is not a full ERC/DRC check.").toHtmlEscaped());
    return html;
}
void CircuitWorkflow::showNetlist() {
    if (!projectOpen_()) return;
    report(netlist_, QStringLiteral("hatteda.tool.netlist"), tr("Netlist"))->setHtml(netlistHtml());
}
void CircuitWorkflow::schematicChanged() {
    ++revision_;
    cancelDc();
    if (live_) {
        // Solve again once the edit settles; the finished stale solve restarts it too.
        resolveTimer_->start();
    } else {
        schematic_->setAnnotations({});
        if (simulation_) simulation_->setHtml(QStringLiteral("<p>%1</p>")
            .arg(tr("Circuit changed. Previous results are out of date; run simulation again.").toHtmlEscaped()));
    }
    refreshGuidance();
}
void CircuitWorkflow::updateSimulationActions() {
    const bool open = projectOpen_();
    start_->setEnabled(open && !live_);
    stop_->setEnabled(live_);
    run_->setEnabled(open && !running_ && !live_);
    cancel_->setEnabled(running_ && !live_);
}
void CircuitWorkflow::startSimulation() {
    if (!projectOpen_() || live_) return;
    live_ = true;
    updateSimulationActions();
    emit simulationStateChanged(true);
    emit statusMessage(tr("Simulation running: probe voltages follow your edits."));
    solve(false);
}
void CircuitWorkflow::stopSimulation() {
    if (!live_) return;
    live_ = false;
    resolveTimer_->stop();
    cancelDc();
    schematic_->setAnnotations({});
    updateSimulationActions();
    emit simulationStateChanged(false);
    emit statusMessage(tr("Simulation stopped."));
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

int CircuitWorkflow::autoPlace(double grid, double spacing) {
    if (!projectOpen_()) return 0;
    const auto waiting = unplacedBoardParts(schematic_->document(), board_->document());
    if (!waiting.parts.isEmpty()) {
        const SketchDocument placed = autoPlaceParts(board_->document(), waiting.parts, grid, spacing);
        const int placedCount = placed.size() - board_->document().size();
        if (placedCount > 0)
            board_->applyDocumentEdit(tr("Auto place components"), placed);
        refreshGuidance();
        if (placedCount < waiting.parts.size()) {
            QMessageBox::warning(
                host_, tr("Auto placer"),
                tr("%1 of %2 component(s) fit inside the board outline. Enlarge the board, reduce "
                   "spacing, or place the remaining components manually.")
                    .arg(placedCount)
                    .arg(waiting.parts.size()));
        }
        showBoard_();
        if (placedCount > 0) board_->zoomToFit();
        return placedCount;
    }
    showBoard_();
    return 0;
}

void CircuitWorkflow::showAutoPlacer() {
    if (!projectOpen_()) return;
    const auto waiting = unplacedBoardParts(schematic_->document(), board_->document());
    const LengthUnit unit = board_->lengthUnit();
    QSettings settings;
    QDialog dialog(host_);
    dialog.setObjectName(QStringLiteral("AutoPlacerDialog"));
    dialog.setWindowTitle(tr("Auto placer"));
    auto* form = new QFormLayout(&dialog);
    auto* summary = new QLabel(&dialog);
    summary->setWordWrap(true);
    summary->setText(waiting.parts.isEmpty()
                         ? tr("Every schematic component is already on the board.")
                         : tr("%n component(s) will be placed inside the board outline, or next to the "
                              "design when there is no outline.", nullptr, static_cast<int>(waiting.parts.size())));
    form->addRow(summary);
    auto length = [&](const QString& name, const QString& key, double fallback) {
        auto* field = new QDoubleSpinBox(&dialog);
        field->setObjectName(name);
        field->setRange(0.0, toDisplayUnit(50.0, unit));
        field->setDecimals(unitDecimals(unit));
        field->setSuffix(QLatin1Char(' ') + unitSymbol(unit));
        field->setValue(toDisplayUnit(settings.value(key, fallback).toDouble(), unit));
        return field;
    };
    auto* grid = length(QStringLiteral("AutoPlacerGrid"), QStringLiteral("pcb/autoPlacer/grid"), 1.27);
    auto* spacing = length(QStringLiteral("AutoPlacerSpacing"), QStringLiteral("pcb/autoPlacer/spacing"), 2.54);
    form->addRow(tr("Placement grid"), grid);
    form->addRow(tr("Spacing between components"), spacing);
    if (!waiting.problems.isEmpty()) {
        auto* problems = new QLabel(waiting.problems.join(QLatin1Char('\n')), &dialog);
        problems->setWordWrap(true);
        form->addRow(tr("Not placed"), problems);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Place"));
    buttons->button(QDialogButtonBox::Ok)->setEnabled(!waiting.parts.isEmpty());
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    const double gridMm = fromDisplayUnit(grid->value(), unit);
    const double spacingMm = fromDisplayUnit(spacing->value(), unit);
    settings.setValue(QStringLiteral("pcb/autoPlacer/grid"), gridMm);
    settings.setValue(QStringLiteral("pcb/autoPlacer/spacing"), spacingMm);
    autoPlace(gridMm, spacingMm);
}

void CircuitWorkflow::showAutorouter() {
    if (!projectOpen_() || autorouter_->isRunning()) return;
    // Issue #49: routing settings live in the Design Rules dialog's Autorouter tab, not a separate
    // dialog here. The host opens that dialog (Autorouter tab active); its Route Board button applies
    // any rule edits and then calls runAutorouter() below.
    configureAutorouter_();
}

void CircuitWorkflow::runAutorouter() {
    if (!projectOpen_() || autorouter_->isRunning()) return;

    const DesignRules rules = designRules_();
    QSettings settings;

    const SpecctraDsnResult exported =
        exportSpecctraDsn(schematic_->document(), board_->document(), rules, QStringLiteral("HattEDA"));
    if (!exported.errors.isEmpty()) {
        QMessageBox::warning(host_, tr("Auto Router"), exported.errors.join(QLatin1Char('\n')));
        return;
    }

    const QString bundledRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/autorouter");
    const QString bundledJar = bundledRoot + QStringLiteral("/freerouting.jar");
#ifdef Q_OS_WIN
    const QString bundledJava = bundledRoot + QStringLiteral("/runtime/bin/javaw.exe");
#else
    const QString bundledJava = bundledRoot + QStringLiteral("/runtime/bin/java");
#endif
    const bool bundled = QFileInfo::exists(bundledJar) && QFileInfo::exists(bundledJava);
    QString jarPath = bundled ? bundledJar
                              : settings.value(QStringLiteral("pcb/freerouting/jar")).toString();
    if (!bundled && (jarPath.isEmpty() || !QFileInfo::exists(jarPath))) {
        jarPath = QFileDialog::getOpenFileName(host_, tr("Select Freerouting"), QString(),
                                               tr("Freerouting JAR (*.jar);;All files (*.*)"));
        if (jarPath.isEmpty()) return;
        settings.setValue(QStringLiteral("pcb/freerouting/jar"), jarPath);
    }

    autorouteProgress_ = new QProgressDialog(tr("Auto Router is routing the PCB..."), tr("Cancel"), 0, 0, host_);
    autorouteProgress_->setObjectName(QStringLiteral("AutorouterProgress"));
    autorouteProgress_->setWindowTitle(tr("Auto Router"));
    autorouteProgress_->setWindowModality(Qt::ApplicationModal);
    autorouteProgress_->setMinimumDuration(0);
    autorouteProgress_->setAutoClose(false);
    autorouteProgress_->setAutoReset(false);
    connect(autorouteProgress_, &QProgressDialog::canceled, autorouter_, &FreeroutingRunner::cancel);

    connect(autorouter_, &FreeroutingRunner::finished, this,
            [this, rules](const FreeroutingResult& routed) {
                if (autorouteProgress_) {
                    autorouteProgress_->close();
                    autorouteProgress_->deleteLater();
                    autorouteProgress_.clear();
                }
                autoroute_->setEnabled(projectOpen_());
                if (!routed.error.isEmpty()) {
                    auto* box = new QMessageBox(QMessageBox::Critical, tr("Auto Router"), routed.error,
                                                QMessageBox::Ok, host_);
                    if (!routed.diagnostics.trimmed().isEmpty()) box->setDetailedText(routed.diagnostics);
                    box->exec();
                    delete box;
                    return;
                }

                const SpecctraSesResult imported = importSpecctraSes(routed.ses);
                if (!imported.errors.isEmpty()) {
                    QMessageBox::warning(host_, tr("Auto Router"), imported.errors.join(QLatin1Char('\n')));
                    return;
                }
                const SketchDocument routing =
                    snapSpecctraRoutingToPads(imported.routing, board_->document());
                SketchDocument candidate = board_->document();
                candidate += routing;
                const CheckReport check = runDesignRuleCheck(schematic_->document(), candidate, rules);
                QStringList errors;
                QStringList warnings;
                for (const auto& violation : check.violations) {
                    const bool blocking = violation.severity == CheckSeverity::Error ||
                                          violation.rule == QStringLiteral("drc.unrouted");
                    (blocking ? errors : warnings) << violation.message;
                }
                if (!errors.isEmpty()) {
                    auto* box = new QMessageBox(
                        QMessageBox::Warning, tr("Auto Router"),
                        tr("The proposed routing was not applied because HattEDA found %n blocking DRC issue(s).",
                           nullptr, errors.size()),
                        QMessageBox::Ok, host_);
                    box->setDetailedText(errors.join(QLatin1Char('\n')));
                    box->exec();
                    delete box;
                    return;
                }

                board_->applyDocumentEdit(tr("Auto Router"), candidate);
                refreshGuidance();
                showBoard_();
                board_->zoomToFit();
                const int trackCount = std::count_if(routing.cbegin(), routing.cend(),
                                                     [](const SketchItem& item) {
                                                         return item.kind == SketchItem::Kind::Wire;
                                                     });
                const int viaCount = routing.size() - trackCount;
                QString message = tr("Autorouting applied: %1 track(s), %2 via(s).")
                                      .arg(trackCount)
                                      .arg(viaCount);
                if (!warnings.isEmpty()) {
                    message += QLatin1Char('\n') +
                               tr("HattEDA also reported %n warning(s). Run DRC to review them.",
                                  nullptr, warnings.size());
                }
                QMessageBox::information(host_, tr("Auto Router"), message);
                emit statusMessage(message);
            },
            Qt::SingleShotConnection);

    autoroute_->setEnabled(false);
    autorouteProgress_->show();
    FreeroutingRequest request;
    if (bundled) request.javaExecutable = bundledJava;
    request.jarPath = jarPath;
    request.dsn = exported.data;
    request.maxPasses = settings.value(QStringLiteral("pcb/freerouting/maxPasses"), 100).toInt();
    request.threadCount = settings.value(QStringLiteral("pcb/freerouting/threads"), 0).toInt();
    request.updateStrategy =
        settings.value(QStringLiteral("pcb/freerouting/updateStrategy"), QStringLiteral("greedy")).toString();
    request.selectionStrategy =
        settings.value(QStringLiteral("pcb/freerouting/selectionStrategy"), QStringLiteral("prioritized")).toString();
    request.timeoutMs = settings.value(QStringLiteral("pcb/freerouting/timeoutMs"), 5 * 60 * 1000).toInt();
    autorouter_->start(request);
}

void CircuitWorkflow::exportNetlist() {
    if (!projectOpen_()) return;
    QStringList errors;
    const QString text = netlistText(schematic_->document(), &errors);
    if (!errors.isEmpty()) {
        QMessageBox::warning(host_, tr("Export netlist"), errors.join(QLatin1Char('\n')));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(host_, tr("Export netlist"), QString(),
                                                      tr("Netlist (*.net);;All files (*.*)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || file.write(text.toUtf8()) < 0 || !file.commit()) {
        QMessageBox::warning(host_, tr("Export netlist"), tr("Cannot write %1: %2").arg(path, file.errorString()));
    }
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
void CircuitWorkflow::runDc() { solve(true); }
void CircuitWorkflow::solve(bool reveal) {
    if (!projectOpen_() || running_) return;
    const auto snapshot = analyzeSchematic(schematic_->document());
    const QString heading = QStringLiteral("<h2>%1</h2><p>%2</p>").arg(tr("DC operating point").toHtmlEscaped(),
        tr("Supported: resistors, independent DC voltage sources, capacitors (open) and inductors (short). Values use SI/SPICE suffixes (1k, 5, 1meg); unit labels are omitted.").toHtmlEscaped());
    if (!snapshot.errors.isEmpty() || !snapshot.simulationErrors.isEmpty()) {
        schematic_->setAnnotations({});
        report(simulation_, QStringLiteral("hatteda.tool.dc-results"), tr("DC operating point"))
            ->setHtml(heading + messages(snapshot.errors + snapshot.simulationErrors));
        if (live_) stopSimulation();
        return;
    }
    QTextEdit* output = reveal ? report(simulation_, QStringLiteral("hatteda.tool.dc-results"), tr("DC operating point"))
                               : simulation_.data();
    running_ = true;
    updateSimulationActions();
    if (output) output->setHtml(heading + QStringLiteral("<p>%1</p>").arg(tr("Running…").toHtmlEscaped()));
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
        running_ = false;
        updateSimulationActions();
        if (revision != revision_) {
            // The schematic changed while solving: a live simulation solves the new circuit.
            if (live_ && !resolveTimer_->isActive()) resolveTimer_->start();
            return;
        }
        if (!flag->load() && result->success) {
            QVector<CanvasAnnotation> readings;
            for (const auto& probe : snapshot.probes) {
                const int dcNet = snapshot.dcNets.indexOf(probe.net);
                readings.append({probe.position, dcNet >= 0 ? QStringLiteral("%1 V").arg(result->voltages[dcNet], 0, 'g', 4)
                                                            : tr("not connected")});
            }
            schematic_->setAnnotations(readings);
        } else {
            schematic_->setAnnotations({});
            if (live_) {
                report(simulation_, QStringLiteral("hatteda.tool.dc-results"), tr("DC operating point"))
                    ->setHtml(heading + messages({flag->load() ? tr("Simulation cancelled.")
                                                               : QString::fromStdString(result->error)}));
                stopSimulation();
                return;
            }
        }
        if (!simulation_) return;
        if (flag->load()) { simulation_->setHtml(heading + tr("Simulation cancelled.").toHtmlEscaped()); return; }
        if (!result->success) { simulation_->setHtml(heading + messages({QString::fromStdString(result->error)})); return; }
        QString html = heading + QStringLiteral("<table border='1' cellpadding='6'><tr><th>%1</th><th>V</th></tr>").arg(tr("Net").toHtmlEscaped());
        for (int n = 0; n < static_cast<int>(result->voltages.size()); ++n)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>")
                        .arg(QString::fromStdString(snapshot.connectivity.nets[snapshot.dcNets[n]].name).toHtmlEscaped())
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
