#include "hatt/ui/MainWindow.hpp"
#include "hatt/ui/CircuitWorkflow.hpp"

#include "hatt/ui/DesignCanvas.hpp"
#include "hatt/ui/Theme.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QUndoGroup>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace hatt::ui {
namespace {

constexpr int ToolRole = Qt::UserRole;
constexpr int VariantRole = Qt::UserRole + 1;
constexpr int IconRole = Qt::UserRole + 2;

QColor iconColor(const QPalette& palette) {
    return palette.color(QPalette::Window).lightness() < 128 ? QColor(QStringLiteral("#b4bfca"))
                                                             : QColor(QStringLiteral("#34404b"));
}

QIcon makeIcon(const QString& kind, const QColor& color) {
    QPixmap pixmap(48, 48);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    const QColor accent(QStringLiteral("#18b6a4"));

    auto line = [&](double x1, double y1, double x2, double y2) {
        painter.drawLine(QLineF(x1, y1, x2, y2));
    };
    auto poly = [&](std::initializer_list<QPointF> points, bool closed = false) {
        const QPolygonF polygon(points);
        closed ? painter.drawPolygon(polygon) : painter.drawPolyline(polygon);
    };
    auto rect = [&](double x, double y, double w, double h) { painter.drawRect(QRectF(x, y, w, h)); };

    if (kind == QLatin1String("select")) {
        poly({{7, 3}, {7, 19}, {11, 15.5}, {14, 21}, {16.5, 20}, {13.5, 14.5}, {18.5, 14.5}}, true);
    } else if (kind == QLatin1String("component")) {
        rect(7, 6, 10, 12);
        for (double y : {9.0, 15.0}) {
            line(3, y, 7, y);
            line(17, y, 21, y);
        }
    } else if (kind == QLatin1String("wire")) {
        poly({{3, 18}, {10, 18}, {10, 6}, {21, 6}});
        painter.setBrush(accent);
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(3, 18), 2, 2);
        painter.drawEllipse(QPointF(21, 6), 2, 2);
    } else if (kind == QLatin1String("terminal")) {
        poly({{3, 8}, {13, 8}, {17, 12}, {13, 16}, {3, 16}}, true);
        line(17, 12, 21, 12);
    } else if (kind == QLatin1String("probe")) {
        painter.drawEllipse(QPointF(15, 9), 5, 5);
        line(11.5, 12.5, 4, 20);
        poly({{13, 11}, {15, 6.5}, {17, 11}});
    } else if (kind == QLatin1String("draw")) {
        poly({{5, 19}, {6, 15}, {16, 5}, {19, 8}, {9, 18}}, true);
        line(14, 7, 17, 10);
    } else if (kind == QLatin1String("measure")) {
        line(3, 16, 21, 16);
        line(3, 11, 3, 20);
        line(21, 11, 21, 20);
        for (double x : {7.5, 12.0, 16.5}) line(x, 13, x, 16);
        poly({{6, 7}, {18, 7}});
        poly({{8, 5}, {6, 7}, {8, 9}});
        poly({{16, 5}, {18, 7}, {16, 9}});
    } else if (kind == QLatin1String("undo") || kind == QLatin1String("redo")) {
        painter.save();
        if (kind == QLatin1String("redo")) {
            painter.translate(24, 0);
            painter.scale(-1, 1);
        }
        QPainterPath path;
        path.moveTo(6, 9);
        path.lineTo(14, 9);
        path.cubicTo(21, 9, 21, 19, 14, 19);
        path.lineTo(9, 19);
        painter.drawPath(path);
        poly({{10, 5}, {6, 9}, {10, 13}});
        painter.restore();
    } else if (kind == QLatin1String("zoom-in") || kind == QLatin1String("zoom-out")) {
        painter.drawEllipse(QPointF(10, 10), 6.5, 6.5);
        line(15, 15, 21, 21);
        line(7, 10, 13, 10);
        if (kind == QLatin1String("zoom-in")) line(10, 7, 10, 13);
    } else if (kind == QLatin1String("fit")) {
        poly({{4, 9}, {4, 4}, {9, 4}});
        poly({{15, 4}, {20, 4}, {20, 9}});
        poly({{20, 15}, {20, 20}, {15, 20}});
        poly({{9, 20}, {4, 20}, {4, 15}});
        rect(9, 9, 6, 6);
    } else if (kind == QLatin1String("rotate")) {
        QPainterPath path;
        path.arcMoveTo(QRectF(5, 5, 14, 14), 90);
        path.arcTo(QRectF(5, 5, 14, 14), 90, 270);
        painter.drawPath(path);
        poly({{15.5, 9.5}, {19, 12}, {21.5, 8.5}});
    } else if (kind == QLatin1String("duplicate")) {
        rect(4, 8, 11, 12);
        rect(9, 4, 11, 12);
    } else if (kind == QLatin1String("delete")) {
        line(5, 7, 19, 7);
        poly({{10, 7}, {10, 4}, {14, 4}, {14, 7}});
        poly({{7, 7}, {8, 20}, {16, 20}, {17, 7}});
        line(10.5, 10, 10.5, 17);
        line(13.5, 10, 13.5, 17);
    } else if (kind == QLatin1String("check")) {
        poly({{5, 12.5}, {10, 17.5}, {19, 7}});
    } else if (kind == QLatin1String("line")) {
        line(5, 19, 19, 5);
        rect(3, 17, 4, 4);
        rect(17, 3, 4, 4);
    } else if (kind == QLatin1String("polyline")) {
        poly({{3, 19}, {8, 7}, {14, 15}, {21, 5}});
    } else if (kind == QLatin1String("rectangle")) {
        rect(4, 6, 16, 12);
    } else if (kind == QLatin1String("circle")) {
        painter.drawEllipse(QPointF(12, 12), 8, 8);
    } else if (kind == QLatin1String("arc")) {
        QPainterPath path;
        path.arcMoveTo(QRectF(4, 7, 16, 16), 180);
        path.arcTo(QRectF(4, 7, 16, 16), 180, -180);
        painter.drawPath(path);
    } else if (kind == QLatin1String("text")) {
        line(6, 5, 18, 5);
        line(12, 5, 12, 19);
        line(9.5, 19, 14.5, 19);
    } else if (kind == QLatin1String("outline")) {
        poly({{4, 5}, {15, 5}, {20, 10}, {20, 19}, {4, 19}}, true);
    } else if (kind == QLatin1String("zone")) {
        QColor fill = color;
        fill.setAlpha(90);
        painter.setBrush(fill);
        poly({{4, 6}, {20, 4}, {18, 19}, {6, 18}}, true);
    } else if (kind.startsWith(QLatin1String("align-")) ||
               kind.startsWith(QLatin1String("distribute-"))) {
        painter.save();
        const bool vertical = kind == QLatin1String("align-top") ||
                              kind == QLatin1String("align-vcenter") ||
                              kind == QLatin1String("align-bottom") ||
                              kind == QLatin1String("distribute-v");
        if (vertical) {
            painter.translate(24, 0);
            painter.rotate(90);
        }
        if (kind.startsWith(QLatin1String("distribute-"))) {
            line(3, 4, 3, 20);
            line(21, 4, 21, 20);
            rect(6, 8, 3, 8);
            rect(10.5, 6, 3, 12);
            rect(15, 8, 3, 8);
        } else {
            // Drawn as a left/centre/right alignment; the vertical variants are rotated.
            const bool start = kind == QLatin1String("align-left") || kind == QLatin1String("align-top");
            const bool middle = kind.endsWith(QLatin1String("center"));
            const double axis = middle ? 12 : start ? 4 : 20;
            line(axis, 3, axis, 21);
            rect(6, 6, 12, 4);
            rect(middle ? 8 : start ? 6 : 10, 14, 8, 4);
        }
        painter.restore();
    } else {
        painter.drawEllipse(QPointF(12, 12), 7, 7);
        line(12, 3, 12, 21);
        line(3, 12, 21, 12);
    }
    painter.end();
    return QIcon(pixmap);
}

QIcon symbolIcon(const QString& symbolId, const QPalette& palette) {
    QPixmap pixmap(64, 64);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    DesignCanvas::paintSymbolPreview(painter, QRectF(2, 2, 28, 28), symbolId, palette);
    painter.end();
    return QIcon(pixmap);
}

class ObjectPreview final : public QWidget {
public:
    explicit ObjectPreview(QWidget* parent) : QWidget(parent) { setMinimumHeight(124); }

    void setContent(const QString& symbolId, const QString& iconKind, const QString& caption) {
        symbolId_ = symbolId;
        iconKind_ = iconKind;
        caption_ = caption;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRectF area = QRectF(rect()).adjusted(18, 12, -18, -30);
        if (!symbolId_.isEmpty()) {
            DesignCanvas::paintSymbolPreview(painter, area, symbolId_, palette());
        } else if (!iconKind_.isEmpty()) {
            const QRect iconRect(QPoint(0, 0), QSize(56, 56));
            makeIcon(iconKind_, iconColor(palette()))
                .paint(&painter, iconRect.translated(area.center().toPoint() - iconRect.center()));
        }
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(QRectF(rect()).adjusted(8, 0, -8, -8), Qt::AlignHCenter | Qt::AlignBottom,
                         caption_);
    }

private:
    QString symbolId_;
    QString iconKind_;
    QString caption_;
};

QLabel* label(const QString& text, const QString& objectName, QWidget* parent = nullptr) {
    auto* result = new QLabel(text, parent);
    result->setObjectName(objectName);
    return result;
}

QToolButton* commandButton(QAction* action, QWidget* parent) {
    auto* button = new QToolButton(parent);
    button->setDefaultAction(action);
    button->setProperty("command", true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setIconSize(QSize(20, 20));
    return button;
}

QFrame* divider(QWidget* parent) {
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setObjectName(QStringLiteral("CommandDivider"));
    return line;
}

QString withShortcut(const QString& text, const QKeySequence& shortcut) {
    return shortcut.isEmpty() ? text
                              : QStringLiteral("%1 (%2)").arg(text, shortcut.toString(QKeySequence::NativeText));
}

int rememberKey(int mode, Workspace workspace) {
    return mode * 10 + static_cast<int>(workspace);
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("HattEDA.MainWindow"));
    setWindowTitle(QStringLiteral("HattEDA"));
    resize(1440, 900);

    undoGroup_ = new QUndoGroup(this);
    for (const Workspace workspace : {Workspace::Schematic, Workspace::Board}) {
        auto* canvas = new DesignCanvas(workspace);
        canvases_.append(canvas);
        undoGroup_->addStack(canvas->undoStack());
    }

    createActions();
    auto* editor = createEditor();
    createMenus();

    shellPages_ = new QStackedWidget(this);
    shellPages_->setObjectName(QStringLiteral("ApplicationPages"));
    shellPages_->addWidget(createWelcomePage());
    shellPages_->addWidget(editor);
    shellPages_->setCurrentIndex(0);
    setCentralWidget(shellPages_);

    coordinateLabel_ = label(QString(), QStringLiteral("CoordinateReadout"));
    coordinateLabel_->setMinimumWidth(190);
    zoomLabel_ = label(QString(), QStringLiteral("ZoomReadout"));
    zoomLabel_->setMinimumWidth(80);
    statusBar()->addPermanentWidget(coordinateLabel_);
    statusBar()->addPermanentWidget(zoomLabel_);
    coordinateLabel_->hide();
    zoomLabel_->hide();

    for (auto* canvas : canvases_) {
        connect(canvas, &DesignCanvas::selectionChanged, this, &MainWindow::updateEditActions);
        connect(canvas->undoStack(), &QUndoStack::indexChanged, this, &MainWindow::updateEditActions);
        connect(canvas, &DesignCanvas::cursorMoved, this, [this, canvas](QPointF world) {
            if (canvas == activeCanvas()) {
                coordinateLabel_->setText(tr("X %1   Y %2 mm")
                                              .arg(world.x(), 0, 'f', 3)
                                              .arg(-world.y(), 0, 'f', 3));
            }
        });
        connect(canvas, &DesignCanvas::statusMessage, this, [this, canvas](const QString& message) {
            if (canvas == activeCanvas()) {
                statusBar()->showMessage(message);
            }
        });
        connect(canvas, &DesignCanvas::zoomChanged, this, [this, canvas](int percent) {
            if (canvas == activeCanvas()) {
                zoomLabel_->setText(tr("Zoom %1%").arg(percent));
            }
        });
        connect(canvas, &DesignCanvas::selectToolRequested, this,
                [this] { activateToolMode(ToolMode::Select); });
        connect(canvas, &DesignCanvas::contextMenuRequested, this,
                [this, canvas](QPoint position, int index) {
                    if (canvas == editingCanvas()) showCanvasContextMenu(canvas, position, index);
                });
    }

    auto* circuitMenu = menuBar()->addMenu(tr("Circuit"));
    circuitMenu->setObjectName(QStringLiteral("CircuitMenu"));
    new CircuitWorkflow(this, circuitMenu, canvases_[0], canvases_[1],
        [this](const QString& id, const QString& title, QWidget* content) { openToolWorkspace(id, title, content); },
        [this] { return shellPages_ && shellPages_->currentIndex() == 1; },
        [this] { showKayraWorkspace(); });
    applySnapSettings();
    refreshIcons();
    workspaceChanged();
    statusBar()->showMessage(tr("Start by creating or opening a project"));
}

void MainWindow::showCanvasContextMenu(DesignCanvas* canvas, QPoint position, int index) {
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("CanvasContextMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const bool hasItem = index >= 0 && index < canvas->document().size();
    if (hasItem) {
        canvas->selectItem(index);
        auto* properties = menu->addAction(tr("Edit properties"));
        properties->setObjectName(QStringLiteral("hatteda.context.properties"));
        connect(properties, &QAction::triggered, this,
                [this, canvas, index] { editItemProperties(canvas, index); });
        menu->addAction(actions_.value(QStringLiteral("hatteda.action.rotate")));
        menu->addAction(actions_.value(QStringLiteral("hatteda.action.duplicate")));
        menu->addAction(actions_.value(QStringLiteral("hatteda.action.delete")));
    } else {
        menu->addAction(actions_.value(QStringLiteral("hatteda.action.undo")));
        menu->addAction(actions_.value(QStringLiteral("hatteda.action.redo")));
        menu->addSeparator();
        menu->addAction(actions_.value(QStringLiteral("hatteda.action.select-all")));
        menu->addAction(actions_.value(QStringLiteral("hatteda.action.fit")));
    }
    menu->popup(position);
}

void MainWindow::editItemProperties(DesignCanvas* canvas, int index) {
    if (index < 0 || index >= canvas->document().size()) return;
    const auto item = canvas->document().at(index);
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("ItemPropertiesDialog"));
    dialog.setWindowTitle(tr("Edit properties"));
    auto* form = new QFormLayout(&dialog);
    auto* label = new QLineEdit(item.label, &dialog);
    label->setObjectName(QStringLiteral("ItemLabel"));
    const bool hasLabel = item.kind == SketchItem::Kind::Symbol || item.kind == SketchItem::Kind::Text;
    if (hasLabel) form->addRow(tr("Label / text"), label);
    else label->hide();
    QLineEdit* value = nullptr;
    QComboBox* footprint = nullptr;
    QLineEdit* mapping = nullptr;
    const auto* symbol = findSymbol(item.variant);
    if (item.kind == SketchItem::Kind::Symbol && symbol &&
        canvas->workspace() == Workspace::Schematic && symbol->category == SymbolCategory::Component) {
        value = new QLineEdit(item.value, &dialog);
        value->setObjectName(QStringLiteral("ItemValue"));
        form->addRow(tr("Value (SI / SPICE)"), value);
        footprint = new QComboBox(&dialog);
        footprint->setObjectName(QStringLiteral("ItemFootprint"));
        footprint->addItem(tr("Unassigned"), QString());
        for (const auto& candidate : symbolLibrary()) {
            if (candidate.workspace == Workspace::Board &&
                candidate.pins.size() == symbol->pins.size()) {
                footprint->addItem(symbolDisplayName(candidate), candidate.id);
            }
        }
        footprint->setCurrentIndex(qMax(0, footprint->findData(item.footprint)));
        form->addRow(tr("Footprint"), footprint);
        QStringList pads;
        for (int pad : item.pinPadMap) pads.append(QString::number(pad));
        mapping = new QLineEdit(pads.join(QStringLiteral(",")), &dialog);
        mapping->setObjectName(QStringLiteral("ItemPinPadMap"));
        form->addRow(tr("Pin to pad (1-based, comma-separated)"), mapping);
    }
    auto coordinate = [&](const QString& name, double value) {
        auto* field = new QDoubleSpinBox(&dialog);
        field->setObjectName(name);
        field->setRange(-1e9, 1e9);
        field->setDecimals(6);
        field->setSuffix(tr(" mm"));
        field->setValue(value);
        return field;
    };
    auto* x = coordinate(QStringLiteral("ItemPositionX"), item.points.value(0).x());
    auto* y = coordinate(QStringLiteral("ItemPositionY"), item.points.value(0).y());
    form->addRow(tr("Anchor X"), x);
    form->addRow(tr("Anchor Y (positive down)"), y);
    auto* rotation = new QComboBox(&dialog);
    rotation->setObjectName(QStringLiteral("ItemRotation"));
    rotation->addItems({tr("0 degrees"), tr("90 degrees"), tr("180 degrees"), tr("270 degrees")});
    rotation->setCurrentIndex((item.quarterTurns % 4 + 4) % 4);
    if (item.kind != SketchItem::Kind::Text) form->addRow(tr("Rotation"), rotation);
    else rotation->hide();
    auto* validation = new QLabel(&dialog);
    validation->setObjectName(QStringLiteral("ItemPropertiesValidation"));
    validation->setWordWrap(true);
    form->addRow(validation);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    QVector<int> pinPadMap = item.pinPadMap;
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (footprint) {
            pinPadMap.clear();
            if (!footprint->currentData().toString().isEmpty()) {
                const auto fields = mapping->text().split(QLatin1Char(','));
                QSet<int> unique;
                bool valid = fields.size() == symbol->pins.size();
                for (const auto& field : fields) {
                    bool ok = false;
                    const int pad = field.trimmed().toInt(&ok);
                    valid = valid && ok && pad > 0 && pad <= symbol->pins.size() &&
                            !unique.contains(pad);
                    unique.insert(pad);
                    pinPadMap.append(pad);
                }
                if (!valid) {
                    validation->setText(tr("Enter one unique pad number per pin, from 1 to %1.")
                                            .arg(symbol->pins.size()));
                    return;
                }
            }
        }
        dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() == QDialog::Accepted) {
        auto properties = item;
        properties.label = label->text();
        properties.points[0] = {x->value(), y->value()};
        properties.quarterTurns = rotation->currentIndex();
        if (value) properties.value = value->text();
        if (footprint) properties.footprint = footprint->currentData().toString();
        properties.pinPadMap = pinPadMap;
        canvas->editItemProperties(index, properties);
    }
}

MainWindow::~MainWindow() {
    // Child widgets outlive this destructor body; their teardown signals (e.g. QUndoStack::clear
    // emitting indexChanged) must not reach the partially destroyed window.
    for (auto* canvas : canvases_) {
        disconnect(canvas, nullptr, this, nullptr);
        disconnect(canvas->undoStack(), nullptr, this, nullptr);
    }
    disconnect(objectSelector_, nullptr, this, nullptr);
    disconnect(toolWorkspaces_, nullptr, this, nullptr);
}

int MainWindow::primaryWorkspaceCount() const noexcept { return primaryWorkspaces_->count(); }
int MainWindow::toolWorkspaceCount() const noexcept { return toolWorkspaces_->count(); }

DesignCanvas* MainWindow::activeCanvas() const {
    const int index = primaryWorkspaces_ != nullptr ? primaryWorkspaces_->currentIndex() : 0;
    return canvases_.value(index, nullptr);
}

DesignCanvas* MainWindow::editingCanvas() const {
    if (shellPages_ == nullptr || shellPages_->currentIndex() != 1 ||
        editorSurfaces_->currentWidget() != primaryWorkspaces_) {
        return nullptr;
    }
    return activeCanvas();
}

QAction* MainWindow::makeAction(const QString& objectName, const QString& text,
                                const QString& iconKind) {
    auto* action = new QAction(text, this);
    action->setObjectName(objectName);
    action->setProperty("iconKind", iconKind);
    actions_.insert(objectName, action);
    return action;
}

void MainWindow::createActions() {
    toolActions_ = new QActionGroup(this);
    toolActions_->setExclusive(true);
    struct ToolSpec {
        ToolMode mode;
        const char* id;
        QString text;
        const char* icon;
        const char* shortcut;
    };
    const QList<ToolSpec> tools = {
        {ToolMode::Select, "hatteda.tool.select", tr("Selection mode"), "select", "V"},
        {ToolMode::Component, "hatteda.tool.component", tr("Component mode"), "component", "A"},
        {ToolMode::Connect, "hatteda.tool.connect", tr("Wire and track mode"), "wire", "W"},
        {ToolMode::Terminal, "hatteda.tool.terminal", tr("Terminal and port mode"), "terminal", "R"},
        {ToolMode::Probe, "hatteda.tool.probe", tr("Probe mode"), "probe", "P"},
        {ToolMode::Draw, "hatteda.tool.draw", tr("2D graphics mode"), "draw", "D"},
        {ToolMode::Measure, "hatteda.tool.measure", tr("Measure mode"), "measure", "M"},
    };
    for (const auto& spec : tools) {
        auto* action = makeAction(QString::fromLatin1(spec.id), spec.text, QString::fromLatin1(spec.icon));
        const QKeySequence shortcut(QString::fromLatin1(spec.shortcut));
        action->setCheckable(true);
        action->setShortcut(shortcut);
        action->setToolTip(withShortcut(spec.text, shortcut));
        action->setData(static_cast<int>(spec.mode));
        toolActions_->addAction(action);
        const ToolMode mode = spec.mode;
        connect(action, &QAction::triggered, this, [this, mode] { activateToolMode(mode); });
    }
    actions_.value(QStringLiteral("hatteda.tool.select"))->setChecked(true);

    auto* undo = undoGroup_->createUndoAction(this, tr("Undo"));
    undo->setObjectName(QStringLiteral("hatteda.action.undo"));
    undo->setProperty("iconKind", QStringLiteral("undo"));
    undo->setShortcuts(QKeySequence::Undo);
    actions_.insert(undo->objectName(), undo);

    auto* redo = undoGroup_->createRedoAction(this, tr("Redo"));
    redo->setObjectName(QStringLiteral("hatteda.action.redo"));
    redo->setProperty("iconKind", QStringLiteral("redo"));
    QList<QKeySequence> redoKeys = QKeySequence::keyBindings(QKeySequence::Redo);
    if (!redoKeys.contains(QKeySequence(QStringLiteral("Ctrl+Y")))) {
        redoKeys.append(QKeySequence(QStringLiteral("Ctrl+Y")));
    }
    redo->setShortcuts(redoKeys);
    actions_.insert(redo->objectName(), redo);

    auto canvasAction = [this](const char* id, const QString& text, const char* icon,
                               const QList<QKeySequence>& shortcuts,
                               std::function<void(DesignCanvas*)> run) {
        auto* action = makeAction(QString::fromLatin1(id), text, QString::fromLatin1(icon));
        action->setShortcuts(shortcuts);
        action->setToolTip(withShortcut(text, shortcuts.value(0)));
        connect(action, &QAction::triggered, this, [this, run = std::move(run)] {
            if (auto* canvas = editingCanvas()) {
                run(canvas);
            }
        });
        return action;
    };
    canvasAction("hatteda.action.delete", tr("Delete"), "delete", {QKeySequence::Delete},
                 [](DesignCanvas* canvas) { canvas->deleteSelection(); });
    canvasAction("hatteda.action.duplicate", tr("Duplicate"), "duplicate",
                 {QKeySequence(QStringLiteral("Ctrl+D"))},
                 [](DesignCanvas* canvas) { canvas->duplicateSelection(); });
    canvasAction("hatteda.action.rotate", tr("Rotate 90°"), "rotate",
                 {QKeySequence(QStringLiteral("Ctrl+R"))},
                 [](DesignCanvas* canvas) { canvas->rotateSelection(); });
    canvasAction("hatteda.action.select-all", tr("Select all"), "", {QKeySequence::SelectAll},
                 [](DesignCanvas* canvas) { canvas->selectAll(); });
    canvasAction("hatteda.action.zoom-in", tr("Zoom in"), "zoom-in",
                 {QKeySequence::ZoomIn, QKeySequence(QStringLiteral("Ctrl+="))},
                 [](DesignCanvas* canvas) { canvas->zoomIn(); });
    canvasAction("hatteda.action.zoom-out", tr("Zoom out"), "zoom-out", {QKeySequence::ZoomOut},
                 [](DesignCanvas* canvas) { canvas->zoomOut(); });
    canvasAction("hatteda.action.fit", tr("Fit to design"), "fit",
                 {QKeySequence(QStringLiteral("Home")), QKeySequence(QStringLiteral("Ctrl+0"))},
                 [](DesignCanvas* canvas) { canvas->zoomToFit(); });

    struct AlignSpec {
        const char* id;
        QString text;
        const char* icon;
        AlignOperation operation;
    };
    const QList<AlignSpec> alignments = {
        {"hatteda.align.left", tr("Align left edges"), "align-left", AlignOperation::Left},
        {"hatteda.align.hcenter", tr("Align horizontal centres"), "align-hcenter",
         AlignOperation::HorizontalCenter},
        {"hatteda.align.right", tr("Align right edges"), "align-right", AlignOperation::Right},
        {"hatteda.align.top", tr("Align top edges"), "align-top", AlignOperation::Top},
        {"hatteda.align.vcenter", tr("Align vertical centres"), "align-vcenter",
         AlignOperation::VerticalCenter},
        {"hatteda.align.bottom", tr("Align bottom edges"), "align-bottom", AlignOperation::Bottom},
        {"hatteda.align.distribute-h", tr("Distribute horizontally"), "distribute-h",
         AlignOperation::DistributeHorizontally},
        {"hatteda.align.distribute-v", tr("Distribute vertically"), "distribute-v",
         AlignOperation::DistributeVertically},
    };
    for (const auto& spec : alignments) {
        const AlignOperation operation = spec.operation;
        canvasAction(spec.id, spec.text, spec.icon, {},
                     [operation](DesignCanvas* canvas) { canvas->align(operation); });
    }

    auto* save = makeAction(QStringLiteral("hatteda.action.save"), tr("Save"), QString());
    save->setShortcut(QKeySequence::Save);
    save->setEnabled(false);
    save->setToolTip(tr("Project files are not available yet"));

    auto* checks = makeAction(QStringLiteral("hatteda.action.run-checks"), tr("Run design checks"),
                              QStringLiteral("check"));
    checks->setEnabled(false);
    checks->setToolTip(tr("Electrical and design rule checks are not available yet"));
}

QWidget* MainWindow::createEditor() {
    auto* editor = new QWidget(this);
    auto* editorLayout = new QVBoxLayout(editor);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(0);

    auto* documentBar = new QFrame(editor);
    documentBar->setObjectName(QStringLiteral("DocumentBar"));
    documentBar->setFixedHeight(38);
    auto* documentLayout = new QHBoxLayout(documentBar);
    documentLayout->setContentsMargins(8, 0, 10, 0);
    documentLayout->setSpacing(2);
    mergenTab_ = new QPushButton(tr("Mergen  ·  Schematic"), documentBar);
    kayraTab_ = new QPushButton(tr("Kayra  ·  PCB Layout"), documentBar);
    auto* workspaceTabs = new QButtonGroup(documentBar);
    workspaceTabs->setExclusive(true);
    for (auto* tab : {mergenTab_, kayraTab_}) {
        tab->setProperty("documentTab", true);
        tab->setCheckable(true);
        workspaceTabs->addButton(tab);
        documentLayout->addWidget(tab);
    }
    mergenTab_->setChecked(true);
    connect(mergenTab_, &QPushButton::clicked, this, &MainWindow::showMergenWorkspace);
    connect(kayraTab_, &QPushButton::clicked, this, &MainWindow::showKayraWorkspace);
    documentLayout->addStretch();
    projectTitle_ = label(tr("Project"), QStringLiteral("CompactProjectTitle"), documentBar);
    documentLayout->addWidget(projectTitle_);
    editorLayout->addWidget(documentBar);

    auto* commandBar = new QFrame(editor);
    commandBar->setObjectName(QStringLiteral("CommandBar"));
    commandBar->setFixedHeight(44);
    auto* commands = new QHBoxLayout(commandBar);
    commands->setContentsMargins(8, 4, 8, 4);
    commands->setSpacing(2);
    const QList<QList<const char*>> commandGroups = {
        {"hatteda.action.undo", "hatteda.action.redo"},
        {"hatteda.action.zoom-in", "hatteda.action.zoom-out", "hatteda.action.fit"},
        {"hatteda.action.rotate", "hatteda.action.duplicate", "hatteda.action.delete"},
    };
    for (const auto& group : commandGroups) {
        if (&group != &commandGroups.first()) {
            commands->addWidget(divider(commandBar));
        }
        for (const char* id : group) {
            commands->addWidget(commandButton(actions_.value(QString::fromLatin1(id)), commandBar));
        }
    }
    commands->addStretch();
    auto* checks = commandButton(actions_.value(QStringLiteral("hatteda.action.run-checks")), commandBar);
    checks->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    commands->addWidget(checks);
    editorLayout->addWidget(commandBar);

    auto* body = new QWidget(editor);
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    auto* toolRail = new QFrame(body);
    toolRail->setObjectName(QStringLiteral("ToolRail"));
    toolRail->setFixedWidth(48);
    auto* railLayout = new QVBoxLayout(toolRail);
    railLayout->setContentsMargins(5, 7, 5, 7);
    railLayout->setSpacing(4);
    for (auto* action : toolActions_->actions()) {
        auto* button = commandButton(action, toolRail);
        button->setProperty("rail", true);
        button->setIconSize(QSize(22, 22));
        railLayout->addWidget(button);
    }
    railLayout->addStretch();
    bodyLayout->addWidget(toolRail);

    auto* contextPanel = new QFrame(body);
    contextPanel->setObjectName(QStringLiteral("ContextPanel"));
    contextPanel->setFixedWidth(252);
    auto* contextLayout = new QVBoxLayout(contextPanel);
    contextLayout->setContentsMargins(10, 10, 10, 10);
    contextLayout->setSpacing(8);
    activeModeLabel_ = label(QString(), QStringLiteral("ActiveModeLabel"), contextPanel);
    contextLayout->addWidget(activeModeLabel_);
    auto* previewFrame = new QFrame(contextPanel);
    previewFrame->setObjectName(QStringLiteral("ToolPreview"));
    auto* previewLayout = new QVBoxLayout(previewFrame);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    objectPreview_ = new ObjectPreview(previewFrame);
    previewLayout->addWidget(objectPreview_);
    contextLayout->addWidget(previewFrame);
    contextHint_ = label(QString(), QStringLiteral("ContextHint"), contextPanel);
    contextHint_->setWordWrap(true);
    contextLayout->addWidget(contextHint_);
    objectsLabel_ = label(tr("OBJECTS"), QStringLiteral("SectionLabel"), contextPanel);
    contextLayout->addWidget(objectsLabel_);
    objectSelector_ = new QListWidget(contextPanel);
    objectSelector_->setObjectName(QStringLiteral("ObjectSelector"));
    objectSelector_->setIconSize(QSize(32, 32));
    connect(objectSelector_, &QListWidget::currentRowChanged, this,
            [this] { applyObjectSelection(); });
    contextLayout->addWidget(objectSelector_, 1);
    contextLayout->addStretch();
    bodyLayout->addWidget(contextPanel);

    primaryWorkspaces_ = new QStackedWidget(body);
    primaryWorkspaces_->setObjectName(QStringLiteral("PrimaryWorkspaceHost"));
    for (auto* canvas : canvases_) {
        primaryWorkspaces_->addWidget(canvas);
    }

    toolWorkspaces_ = new QTabWidget(body);
    toolWorkspaces_->setObjectName(QStringLiteral("ToolWorkspaceHost"));
    toolWorkspaces_->setTabsClosable(true);
    toolWorkspaces_->setDocumentMode(true);
    connect(toolWorkspaces_, &QTabWidget::tabCloseRequested, this, [this](int index) {
        if (auto* page = toolWorkspaces_->widget(index)) {
            toolWorkspaces_->removeTab(index);
            page->deleteLater();
        }
        if (toolWorkspaces_->count() == 0) {
            editorSurfaces_->setCurrentWidget(primaryWorkspaces_);
            workspaceChanged();
        }
    });

    editorSurfaces_ = new QStackedWidget(body);
    editorSurfaces_->setObjectName(QStringLiteral("EditorSurfaces"));
    editorSurfaces_->addWidget(primaryWorkspaces_);
    editorSurfaces_->addWidget(toolWorkspaces_);
    bodyLayout->addWidget(editorSurfaces_, 1);
    editorLayout->addWidget(body, 1);

    auto* alignmentBar = new QFrame(editor);
    alignmentBar->setObjectName(QStringLiteral("AlignmentBar"));
    alignmentBar->setFixedHeight(36);
    auto* alignmentLayout = new QHBoxLayout(alignmentBar);
    alignmentLayout->setContentsMargins(8, 2, 8, 2);
    alignmentLayout->setSpacing(3);
    alignmentLayout->addWidget(label(tr("SNAP"), QStringLiteral("SectionLabel"), alignmentBar));

    struct SnapSpec {
        const char* key;
        QString text;
        QString tooltip;
        bool enabledByDefault;
    };
    const QList<SnapSpec> snaps = {
        {"grid", tr("Grid"), tr("Snap points to the grid"), true},
        {"objects", tr("Objects"), tr("Snap to pins, pads, vertices and corners"), true},
        {"edges", tr("Edges"), tr("Snap to the nearest point on object edges"), false},
        {"centers", tr("Centres"), tr("Snap to object centres"), false},
        {"diagonal", tr("45°"), tr("Constrain wires and lines to 45° steps"), true},
        {"orthogonal", tr("Orthogonal"), tr("Constrain wires and lines to horizontal and vertical"), false},
    };
    QSettings settings;
    for (const auto& spec : snaps) {
        const QString key = QString::fromLatin1(spec.key);
        auto* toggle = new QPushButton(spec.text, alignmentBar);
        toggle->setObjectName(QStringLiteral("hatteda.snap.") + key);
        toggle->setProperty("snap", true);
        toggle->setProperty("snapKey", key);
        toggle->setCheckable(true);
        toggle->setToolTip(spec.tooltip);
        toggle->setChecked(settings.value(QStringLiteral("editor/snap/") + key, spec.enabledByDefault).toBool());
        snapToggles_.append(toggle);
        alignmentLayout->addWidget(toggle);
        connect(toggle, &QPushButton::toggled, this, [this, key](bool checked) {
            if (checked && (key == QLatin1String("diagonal") || key == QLatin1String("orthogonal"))) {
                const QString other = key == QLatin1String("diagonal") ? QStringLiteral("orthogonal")
                                                                       : QStringLiteral("diagonal");
                for (auto* button : snapToggles_) {
                    if (button->property("snapKey").toString() == other) {
                        const QSignalBlocker blocker(button);
                        button->setChecked(false);
                        QSettings().setValue(QStringLiteral("editor/snap/") + other, false);
                    }
                }
            }
            QSettings().setValue(QStringLiteral("editor/snap/") + key, checked);
            applySnapSettings();
        });
    }
    if (snapToggles_[4]->isChecked() && snapToggles_[5]->isChecked()) {
        snapToggles_[4]->setChecked(false);
    }
    alignmentLayout->addStretch();
    alignmentLayout->addWidget(label(tr("ALIGN"), QStringLiteral("SectionLabel"), alignmentBar));
    for (const char* id : {"hatteda.align.left", "hatteda.align.hcenter", "hatteda.align.right",
                           "hatteda.align.top", "hatteda.align.vcenter", "hatteda.align.bottom",
                           "hatteda.align.distribute-h", "hatteda.align.distribute-v"}) {
        auto* button = commandButton(actions_.value(QString::fromLatin1(id)), alignmentBar);
        button->setIconSize(QSize(18, 18));
        alignmentLayout->addWidget(button);
    }
    auto* collapseAlignment = new QPushButton(tr("Hide"), alignmentBar);
    collapseAlignment->setProperty("snap", true);
    collapseAlignment->setCheckable(false);
    alignmentLayout->addWidget(collapseAlignment);
    connect(collapseAlignment, &QPushButton::clicked, alignmentBar, [alignmentBar, collapseAlignment] {
        const bool collapse = collapseAlignment->property("collapsed").toBool() == false;
        for (auto* child : alignmentBar->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) {
            if (child != collapseAlignment) {
                child->setVisible(!collapse);
            }
        }
        collapseAlignment->setProperty("collapsed", collapse);
        collapseAlignment->setText(collapse ? MainWindow::tr("Snapping and alignment")
                                            : MainWindow::tr("Hide"));
    });
    editorLayout->addWidget(alignmentBar);

    return editor;
}

void MainWindow::createMenus() {
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* newProject = fileMenu->addAction(tr("New project"));
    newProject->setShortcut(QKeySequence::New);
    connect(newProject, &QAction::triggered, this, &MainWindow::createNewProject);
    auto* open = fileMenu->addAction(tr("Open project…"));
    open->setShortcut(QKeySequence::Open);
    connect(open, &QAction::triggered, this, &MainWindow::openProject);
    fileMenu->addSeparator();
    fileMenu->addAction(actions_.value(QStringLiteral("hatteda.action.save")));

    auto* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(actions_.value(QStringLiteral("hatteda.action.undo")));
    editMenu->addAction(actions_.value(QStringLiteral("hatteda.action.redo")));
    editMenu->addSeparator();
    editMenu->addAction(actions_.value(QStringLiteral("hatteda.action.rotate")));
    editMenu->addAction(actions_.value(QStringLiteral("hatteda.action.duplicate")));
    editMenu->addAction(actions_.value(QStringLiteral("hatteda.action.delete")));
    editMenu->addSeparator();
    editMenu->addAction(actions_.value(QStringLiteral("hatteda.action.select-all")));

    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(actions_.value(QStringLiteral("hatteda.action.zoom-in")));
    viewMenu->addAction(actions_.value(QStringLiteral("hatteda.action.zoom-out")));
    viewMenu->addAction(actions_.value(QStringLiteral("hatteda.action.fit")));
    viewMenu->addSeparator();
    auto* diagnostics = viewMenu->addAction(tr("Simulation diagnostics"));
    connect(diagnostics, &QAction::triggered, this, &MainWindow::openDiagnosticsWorkspace);
    viewMenu->addSeparator();
    auto* themeMenu = viewMenu->addMenu(tr("Theme"));
    auto* themeGroup = new QActionGroup(themeMenu);
    auto* darkTheme = themeMenu->addAction(tr("Dark"));
    auto* lightTheme = themeMenu->addAction(tr("Light"));
    const bool light = QSettings().value(QStringLiteral("appearance/lightTheme"), false).toBool();
    for (auto* action : {darkTheme, lightTheme}) {
        action->setCheckable(true);
        themeGroup->addAction(action);
    }
    (light ? lightTheme : darkTheme)->setChecked(true);
    connect(darkTheme, &QAction::triggered, this, [] {
        QSettings().setValue(QStringLiteral("appearance/lightTheme"), false);
        Theme::apply(*qApp, ThemeMode::Dark);
    });
    connect(lightTheme, &QAction::triggered, this, [] {
        QSettings().setValue(QStringLiteral("appearance/lightTheme"), true);
        Theme::apply(*qApp, ThemeMode::Light);
    });
    auto* languageMenu = viewMenu->addMenu(tr("Language"));
    auto setLanguage = [this](const QString& code) {
        QSettings().setValue(QStringLiteral("appearance/language"), code);
        QMessageBox::information(this, tr("Language"),
                                 tr("The language will be applied after restarting HattEDA."));
    };
    connect(languageMenu->addAction(QStringLiteral("English")), &QAction::triggered, this,
            [setLanguage] { setLanguage(QStringLiteral("en")); });
    connect(languageMenu->addAction(QStringLiteral("Türkçe")), &QAction::triggered, this,
            [setLanguage] { setLanguage(QStringLiteral("tr")); });

    auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
    toolsMenu->addActions(toolActions_->actions());

    auto* designMenu = menuBar()->addMenu(tr("&Design"));
    auto* alignMenu = designMenu->addMenu(tr("Align and distribute"));
    for (const char* id : {"hatteda.align.left", "hatteda.align.hcenter", "hatteda.align.right",
                           "hatteda.align.top", "hatteda.align.vcenter", "hatteda.align.bottom",
                           "hatteda.align.distribute-h", "hatteda.align.distribute-v"}) {
        alignMenu->addAction(actions_.value(QString::fromLatin1(id)));
    }
    designMenu->addSeparator();
    designMenu->addAction(actions_.value(QStringLiteral("hatteda.action.run-checks")));

    auto* helpMenu = menuBar()->addMenu(tr("&Help"));
    connect(helpMenu->addAction(tr("About HattEDA")), &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About HattEDA"),
                           tr("HattEDA %1\nSchematic, simulation, PCB and CAM design suite.")
                               .arg(QCoreApplication::applicationVersion()));
    });
}

void MainWindow::activateToolMode(ToolMode mode) {
    toolMode_ = mode;
    for (auto* action : toolActions_->actions()) {
        if (action->data().toInt() == static_cast<int>(mode)) {
            if (!action->isChecked()) {
                action->setChecked(true);
            }
            activeModeLabel_->setText(action->text().toUpper());
        }
    }
    rebuildObjectSelector();
}

void MainWindow::rebuildObjectSelector() {
    const Workspace workspace = activeCanvas()->workspace();
    const QColor color = iconColor(palette());
    {
        const QSignalBlocker blocker(objectSelector_);
        objectSelector_->clear();
        auto addSymbols = [&](SymbolCategory category) {
            for (const auto* symbol : symbolsFor(workspace, category)) {
                auto* item = new QListWidgetItem(symbolIcon(symbol->id, palette()),
                                                 symbolDisplayName(*symbol), objectSelector_);
                item->setData(ToolRole, static_cast<int>(CanvasTool::Symbol));
                item->setData(VariantRole, symbol->id);
            }
        };
        auto addTool = [&](CanvasTool tool, const QString& text, const QString& icon,
                           const QString& variant = QString()) {
            auto* item = new QListWidgetItem(makeIcon(icon, color), text, objectSelector_);
            item->setData(ToolRole, static_cast<int>(tool));
            item->setData(VariantRole, variant);
            item->setData(IconRole, icon);
        };
        switch (toolMode_) {
        case ToolMode::Component:
            addSymbols(SymbolCategory::Component);
            break;
        case ToolMode::Terminal:
            addSymbols(SymbolCategory::Terminal);
            break;
        case ToolMode::Probe:
            addSymbols(SymbolCategory::Probe);
            break;
        case ToolMode::Draw:
            addTool(CanvasTool::Line, tr("Line"), QStringLiteral("line"));
            addTool(CanvasTool::Polyline, tr("Polyline"), QStringLiteral("polyline"));
            addTool(CanvasTool::Rectangle, tr("Rectangle"), QStringLiteral("rectangle"));
            addTool(CanvasTool::Circle, tr("Circle"), QStringLiteral("circle"));
            addTool(CanvasTool::Arc, tr("Arc"), QStringLiteral("arc"));
            addTool(CanvasTool::Text, tr("Text"), QStringLiteral("text"));
            if (workspace == Workspace::Board) {
                addTool(CanvasTool::Polyline, tr("Board outline"), QStringLiteral("outline"),
                        BoardOutlineVariant);
                addTool(CanvasTool::Polyline, tr("Copper zone"), QStringLiteral("zone"),
                        CopperZoneVariant);
            }
            break;
        case ToolMode::Select:
        case ToolMode::Connect:
        case ToolMode::Measure:
            break;
        }
        const bool hasObjects = objectSelector_->count() > 0;
        objectSelector_->setVisible(hasObjects);
        objectsLabel_->setVisible(hasObjects);
        if (hasObjects) {
            const int row = rememberedObjectRows_.value(rememberKey(static_cast<int>(toolMode_), workspace), 0);
            objectSelector_->setCurrentRow(std::clamp(row, 0, objectSelector_->count() - 1));
        }
    }
    applyObjectSelection();
}

void MainWindow::applyObjectSelection() {
    auto* canvas = activeCanvas();
    const Workspace workspace = canvas->workspace();
    CanvasTool tool = CanvasTool::Select;
    QString variant;
    QString symbolId;
    QString iconKind;
    QString caption;
    switch (toolMode_) {
    case ToolMode::Select:
        iconKind = QStringLiteral("select");
        caption = tr("Select, move and edit objects");
        break;
    case ToolMode::Connect:
        tool = CanvasTool::Wire;
        iconKind = QStringLiteral("wire");
        caption = workspace == Workspace::Board ? tr("Track") : tr("Wire");
        break;
    case ToolMode::Measure:
        tool = CanvasTool::Measure;
        iconKind = QStringLiteral("measure");
        caption = tr("Measure distance");
        break;
    case ToolMode::Component:
    case ToolMode::Terminal:
    case ToolMode::Probe:
    case ToolMode::Draw:
        if (auto* item = objectSelector_->currentItem()) {
            tool = static_cast<CanvasTool>(item->data(ToolRole).toInt());
            variant = item->data(VariantRole).toString();
            iconKind = item->data(IconRole).toString();
            caption = item->text();
            if (tool == CanvasTool::Symbol) {
                symbolId = variant;
            }
            rememberedObjectRows_.insert(rememberKey(static_cast<int>(toolMode_), workspace),
                                         objectSelector_->currentRow());
        }
        break;
    }
    canvas->setTool(tool, variant);
    static_cast<ObjectPreview*>(objectPreview_)->setContent(symbolId, iconKind, caption);
    contextHint_->setText(canvas->toolHint());
    updateEditActions();
}

void MainWindow::workspaceChanged() {
    auto* canvas = activeCanvas();
    const bool board = canvas->workspace() == Workspace::Board;
    undoGroup_->setActiveStack(canvas->undoStack());
    mergenTab_->setChecked(!board);
    kayraTab_->setChecked(board);
    for (auto* other : canvases_) {
        if (other != canvas) {
            other->cancelOperation();
        }
    }
    auto* probe = actions_.value(QStringLiteral("hatteda.tool.probe"));
    probe->setEnabled(!board);
    probe->setToolTip(board ? tr("Probes are only available in Mergen")
                            : withShortcut(probe->text(), probe->shortcut()));
    activateToolMode(board && toolMode_ == ToolMode::Probe ? ToolMode::Select : toolMode_);
    zoomLabel_->setText(tr("Zoom %1%").arg(canvas->zoomPercent()));
    if (editingCanvas() != nullptr) {
        canvas->setFocus();
    }
}

void MainWindow::applySnapSettings() {
    SnapSettings settings;
    for (auto* toggle : snapToggles_) {
        const QString key = toggle->property("snapKey").toString();
        const bool on = toggle->isChecked();
        if (key == QLatin1String("grid")) settings.grid = on;
        else if (key == QLatin1String("objects")) settings.objects = on;
        else if (key == QLatin1String("edges")) settings.edges = on;
        else if (key == QLatin1String("centers")) settings.centers = on;
        else if (key == QLatin1String("diagonal")) settings.diagonal = on;
        else if (key == QLatin1String("orthogonal")) settings.orthogonal = on;
    }
    for (auto* canvas : canvases_) {
        canvas->setSnapSettings(settings);
    }
}

void MainWindow::updateEditActions() {
    if (actions_.isEmpty()) {
        return;
    }
    const auto* canvas = editingCanvas();
    const int selected = canvas != nullptr ? static_cast<int>(canvas->selection().size()) : 0;
    auto enable = [this](const char* id, bool enabled) {
        if (auto* action = actions_.value(QString::fromLatin1(id))) {
            action->setEnabled(enabled);
        }
    };
    enable("hatteda.action.delete", selected > 0);
    enable("hatteda.action.duplicate", selected > 0);
    enable("hatteda.action.rotate", selected > 0 || (canvas != nullptr && canvas->tool() == CanvasTool::Symbol));
    enable("hatteda.action.select-all", canvas != nullptr && !canvas->document().isEmpty());
    for (const char* id : {"hatteda.action.zoom-in", "hatteda.action.zoom-out", "hatteda.action.fit"}) {
        enable(id, canvas != nullptr);
    }
    for (const char* id : {"hatteda.align.left", "hatteda.align.hcenter", "hatteda.align.right",
                           "hatteda.align.top", "hatteda.align.vcenter", "hatteda.align.bottom"}) {
        enable(id, selected >= 2);
    }
    enable("hatteda.align.distribute-h", selected >= 3);
    enable("hatteda.align.distribute-v", selected >= 3);
}

void MainWindow::refreshIcons() {
    const QColor color = iconColor(palette());
    for (auto* action : findChildren<QAction*>()) {
        const QString kind = action->property("iconKind").toString();
        if (!kind.isEmpty()) {
            action->setIcon(makeIcon(kind, color));
        }
    }
    if (objectSelector_ != nullptr) {
        for (int row = 0; row < objectSelector_->count(); ++row) {
            auto* item = objectSelector_->item(row);
            const QString icon = item->data(IconRole).toString();
            item->setIcon(icon.isEmpty() ? symbolIcon(item->data(VariantRole).toString(), palette())
                                         : makeIcon(icon, color));
        }
    }
    if (objectPreview_ != nullptr) {
        objectPreview_->update();
    }
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange) {
        refreshIcons();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::showMergenWorkspace() {
    editorSurfaces_->setCurrentWidget(primaryWorkspaces_);
    primaryWorkspaces_->setCurrentIndex(0);
    workspaceChanged();
}

void MainWindow::showKayraWorkspace() {
    editorSurfaces_->setCurrentWidget(primaryWorkspaces_);
    primaryWorkspaces_->setCurrentIndex(1);
    workspaceChanged();
}

void MainWindow::openDiagnosticsWorkspace() {
    auto* report = new QTextEdit;
    report->setReadOnly(true);
    report->setHtml(tr("<h3>Simulation diagnostics</h3><p>No simulation has been run for this project.</p>"));
    openToolWorkspace(QStringLiteral("hatteda.tool.simulation-diagnostics"),
                      tr("Simulation Diagnostics"), report);
}

QWidget* MainWindow::createWelcomePage() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("WelcomePage"));
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(72, 54, 72, 54);
    outer->addWidget(label(QStringLiteral("HATTEDA"), QStringLiteral("BrandMark"), page), 0,
                     Qt::AlignLeft);
    outer->addSpacing(40);
    outer->addWidget(label(tr("Start your next hardware project"), QStringLiteral("WelcomeTitle"), page));
    outer->addWidget(label(tr("Create a new HattEDA project or continue from an existing design."),
                           QStringLiteral("WelcomeSubtitle"), page));
    outer->addSpacing(34);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(44);
    auto* actionsColumn = new QVBoxLayout;
    actionsColumn->setSpacing(12);
    auto* newButton = new QPushButton(tr("New project"), page);
    newButton->setObjectName(QStringLiteral("PrimaryAction"));
    newButton->setMinimumSize(250, 48);
    connect(newButton, &QPushButton::clicked, this, &MainWindow::createNewProject);
    actionsColumn->addWidget(newButton);
    auto* openButton = new QPushButton(tr("Open project"), page);
    openButton->setProperty("quiet", true);
    openButton->setMinimumSize(250, 46);
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openProject);
    actionsColumn->addWidget(openButton);
    actionsColumn->addStretch();
    columns->addLayout(actionsColumn);

    auto* recentColumn = new QVBoxLayout;
    recentColumn->addWidget(label(tr("RECENT PROJECTS"), QStringLiteral("SectionLabel"), page));
    recentProjects_ = new QListWidget(page);
    recentProjects_->setObjectName(QStringLiteral("RecentProjects"));
    recentProjects_->setMinimumWidth(480);
    connect(recentProjects_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) {
            activateProject(QFileInfo(path).completeBaseName(), path);
        }
    });
    recentColumn->addWidget(recentProjects_, 1);
    columns->addLayout(recentColumn, 1);
    outer->addLayout(columns, 1);
    refreshRecentProjects();
    return page;
}

void MainWindow::createNewProject() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("New project"));
    dialog.setMinimumWidth(500);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(label(tr("Create a HattEDA project"), QStringLiteral("WorkspaceTitle"), &dialog));
    auto* form = new QFormLayout;
    auto* name = new QLineEdit(tr("My Project"), &dialog);
    name->selectAll();
    auto* location = new QLineEdit(QDir::homePath() + QStringLiteral("/Documents"), &dialog);
    form->addRow(tr("Project name"), name);
    form->addRow(tr("Location"), location);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create project"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) {
        return;
    }
    const QString projectName = name->text().trimmed();
    activateProject(projectName,
                    QDir(location->text().trimmed()).filePath(projectName + QStringLiteral(".hatt")));
}

void MainWindow::openProject() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open HattEDA project"), QString(),
                                                      tr("HattEDA projects (*.hatt);;All files (*.*)"));
    if (path.isEmpty()) {
        return;
    }
    QSettings settings;
    QStringList recent = settings.value(QStringLiteral("recentProjects")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    settings.setValue(QStringLiteral("recentProjects"), recent.mid(0, 10));
    refreshRecentProjects();
    activateProject(QFileInfo(path).completeBaseName(), path);
}

void MainWindow::activateProject(const QString& projectName, const QString& projectPath) {
    projectTitle_->setText(projectName);
    projectTitle_->setToolTip(QDir::toNativeSeparators(projectPath));
    setWindowTitle(QStringLiteral("HattEDA - %1").arg(projectName));
    for (auto* canvas : canvases_) {
        canvas->restore({}, {});
        canvas->undoStack()->clear();
    }
    shellPages_->setCurrentIndex(1);
    coordinateLabel_->show();
    zoomLabel_->show();
    showMergenWorkspace();
}

void MainWindow::refreshRecentProjects() {
    if (recentProjects_ == nullptr) {
        return;
    }
    recentProjects_->clear();
    const QStringList recent = QSettings().value(QStringLiteral("recentProjects")).toStringList();
    for (const QString& path : recent) {
        auto* item = new QListWidgetItem(QFileInfo(path).completeBaseName(), recentProjects_);
        item->setData(Qt::UserRole, path);
        item->setToolTip(QDir::toNativeSeparators(path));
    }
    if (recentProjects_->count() == 0) {
        auto* empty = new QListWidgetItem(tr("No recent projects yet"), recentProjects_);
        empty->setFlags(Qt::NoItemFlags);
    }
}

void MainWindow::openToolWorkspace(const QString& stableId, const QString& title, QWidget* content) {
    for (int index = 0; index < toolWorkspaces_->count(); ++index) {
        if (toolWorkspaces_->widget(index)->objectName() == stableId) {
            toolWorkspaces_->setCurrentIndex(index);
            editorSurfaces_->setCurrentWidget(toolWorkspaces_);
            undoGroup_->setActiveStack(nullptr);
            updateEditActions();
            if (content != toolWorkspaces_->widget(index)) delete content;
            return;
        }
    }
    content->setObjectName(stableId);
    toolWorkspaces_->setCurrentIndex(toolWorkspaces_->addTab(content, title));
    editorSurfaces_->setCurrentWidget(toolWorkspaces_);
    for (auto* canvas : canvases_) {
        canvas->cancelOperation();
    }
    undoGroup_->setActiveStack(nullptr);
    updateEditActions();
}

} // namespace hatt::ui
