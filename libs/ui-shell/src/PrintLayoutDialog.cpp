#include "hatt/ui/PrintLayoutDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace hatt::ui {

// Paper sheet preview: the page scaled into the widget with its margin frame and artworks.
class PrintPagePreview final : public QWidget {
public:
    explicit PrintPagePreview(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("PrintPreview"));
        setMinimumSize(260, 340);
    }
    void setPage(const CamOutput* output, const PrintSettings& settings) {
        output_ = output;
        settings_ = settings;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Dark));
        if (output_ == nullptr) return;
        const QSizeF page = pageSize(settings_);
        const double scale = std::min((width() - 24.0) / page.width(), (height() - 24.0) / page.height());
        const QSizeF shown = page * scale;
        painter.translate((width() - shown.width()) / 2.0, (height() - shown.height()) / 2.0);
        paintPrintPage(painter, *output_, settings_, scale, true);
    }

private:
    const CamOutput* output_ = nullptr;
    PrintSettings settings_;
};

namespace {

constexpr PaperPreset Presets[] = {PaperPreset::A3, PaperPreset::A4, PaperPreset::A5, PaperPreset::Letter,
                                   PaperPreset::Custom};
constexpr double Scales[] = {0.5, 1.0, 1.5, 2.0, 4.0};

QDoubleSpinBox* millimetres(QWidget* parent, const char* name, double minimum, double maximum, double step) {
    auto* field = new QDoubleSpinBox(parent);
    field->setObjectName(QLatin1String(name));
    field->setRange(minimum, maximum);
    field->setDecimals(1);
    field->setSingleStep(step);
    field->setSuffix(QStringLiteral(" mm"));
    return field;
}

} // namespace

PrintLayoutDialog::PrintLayoutDialog(const CamOutput& output, QString documentName, QWidget* parent)
    : QDialog(parent), output_(output), documentName_(std::move(documentName)) {
    setObjectName(QStringLiteral("PrintLayoutDialog"));
    setWindowTitle(tr("Print layout"));
    auto* root = new QVBoxLayout(this);
    auto* columns = new QHBoxLayout;
    root->addLayout(columns, 1);

    // Layers.
    auto* layersBox = new QGroupBox(tr("Layers / artwork"), this);
    auto* layersLayout = new QVBoxLayout(layersBox);
    for (int kind = 0; kind <= static_cast<int>(CamLayerKind::Outline); ++kind) {
        auto* box = new QCheckBox(camLayerName(static_cast<CamLayerKind>(kind)), layersBox);
        box->setObjectName(QStringLiteral("PrintLayer.%1").arg(kind));
        layerBoxes_.append(box);
        layersLayout->addWidget(box);
    }
    drills_ = new QCheckBox(tr("Drill holes"), layersBox);
    drills_->setObjectName(QStringLiteral("PrintDrills"));
    drills_->setToolTip(tr("Leave the holes open so they can guide the drill"));
    layersLayout->addWidget(drills_);
    grouping_ = new QComboBox(layersBox);
    grouping_->setObjectName(QStringLiteral("PrintGrouping"));
    grouping_->addItem(tr("Overlay the layers"), static_cast<int>(PrintGrouping::Overlay));
    grouping_->addItem(tr("Each layer side by side"), static_cast<int>(PrintGrouping::SideBySide));
    grouping_->setToolTip(tr("Side by side puts, for example, top and bottom copper on the same sheet"));
    layersLayout->addWidget(grouping_);
    layersLayout->addStretch();
    columns->addWidget(layersBox);

    // Page, scale and copies.
    auto* settingsColumn = new QVBoxLayout;
    auto* pageBox = new QGroupBox(tr("Paper"), this);
    auto* pageForm = new QFormLayout(pageBox);
    paper_ = new QComboBox(pageBox);
    paper_->setObjectName(QStringLiteral("PrintPaper"));
    for (PaperPreset preset : Presets) paper_->addItem(paperName(preset), static_cast<int>(preset));
    pageForm->addRow(tr("Size"), paper_);
    paperWidth_ = millimetres(pageBox, "PrintPaperWidth", 20.0, 2000.0, 1.0);
    paperHeight_ = millimetres(pageBox, "PrintPaperHeight", 20.0, 2000.0, 1.0);
    pageForm->addRow(tr("Width"), paperWidth_);
    pageForm->addRow(tr("Height"), paperHeight_);
    landscape_ = new QCheckBox(tr("Landscape"), pageBox);
    landscape_->setObjectName(QStringLiteral("PrintLandscape"));
    pageForm->addRow(QString(), landscape_);
    margin_ = millimetres(pageBox, "PrintMargin", 0.0, 100.0, 1.0);
    pageForm->addRow(tr("Margin"), margin_);
    settingsColumn->addWidget(pageBox);

    auto* artBox = new QGroupBox(tr("Artwork"), this);
    auto* artForm = new QFormLayout(artBox);
    scale_ = new QComboBox(artBox);
    scale_->setObjectName(QStringLiteral("PrintScale"));
    for (double scale : Scales) scale_->addItem(QStringLiteral("%1%").arg(scale * 100.0), scale);
    artForm->addRow(tr("Scale"), scale_);
    compensationX_ = new QDoubleSpinBox(artBox);
    compensationX_->setObjectName(QStringLiteral("PrintCompensationX"));
    compensationY_ = new QDoubleSpinBox(artBox);
    compensationY_->setObjectName(QStringLiteral("PrintCompensationY"));
    for (auto* field : {compensationX_, compensationY_}) {
        field->setRange(0.9, 1.1);
        field->setDecimals(4);
        field->setSingleStep(0.001);
        field->setToolTip(tr("Printer correction: 1.005 when 100 mm prints as 99.5 mm"));
    }
    artForm->addRow(tr("Compensation X"), compensationX_);
    artForm->addRow(tr("Compensation Y"), compensationY_);
    colours_ = new QComboBox(artBox);
    colours_->setObjectName(QStringLiteral("PrintColours"));
    colours_->addItem(tr("Monochrome (black copper)"), static_cast<int>(PrintColours::Monochrome));
    colours_->addItem(tr("Negative (white copper on black)"), static_cast<int>(PrintColours::Negative));
    colours_->addItem(tr("Board colours"), static_cast<int>(PrintColours::Board));
    artForm->addRow(tr("Colours"), colours_);
    mirror_ = new QCheckBox(tr("Mirror (toner transfer)"), artBox);
    mirror_->setObjectName(QStringLiteral("PrintMirror"));
    artForm->addRow(QString(), mirror_);
    rotate_ = new QCheckBox(tr("Rotate 90°"), artBox);
    rotate_->setObjectName(QStringLiteral("PrintRotate"));
    artForm->addRow(QString(), rotate_);
    settingsColumn->addWidget(artBox);

    auto* copiesBox = new QGroupBox(tr("Copies on the page"), this);
    auto* copiesForm = new QFormLayout(copiesBox);
    columns_ = new QSpinBox(copiesBox);
    columns_->setObjectName(QStringLiteral("PrintColumns"));
    rows_ = new QSpinBox(copiesBox);
    rows_->setObjectName(QStringLiteral("PrintRows"));
    for (auto* field : {columns_, rows_}) field->setRange(1, 200);
    copiesForm->addRow(tr("Across"), columns_);
    copiesForm->addRow(tr("Down"), rows_);
    spacing_ = millimetres(copiesBox, "PrintSpacing", 0.0, 100.0, 0.5);
    copiesForm->addRow(tr("Spacing"), spacing_);
    auto* fit = new QPushButton(tr("Fit as many as possible"), copiesBox);
    fit->setObjectName(QStringLiteral("PrintFit"));
    fit->setToolTip(tr("Fill the page with copies, turning the board when more fit that way"));
    connect(fit, &QPushButton::clicked, this, &PrintLayoutDialog::fitToPage);
    copiesForm->addRow(fit);
    summary_ = new QLabel(copiesBox);
    summary_->setObjectName(QStringLiteral("PrintSummary"));
    summary_->setWordWrap(true);
    copiesForm->addRow(summary_);
    settingsColumn->addWidget(copiesBox);
    settingsColumn->addStretch();
    columns->addLayout(settingsColumn);

    preview_ = new PrintPagePreview(this);
    columns->addWidget(preview_, 1);

    auto* buttons = new QDialogButtonBox(this);
    auto* pdf = buttons->addButton(tr("Save PDF..."), QDialogButtonBox::ActionRole);
    pdf->setObjectName(QStringLiteral("PrintSavePdf"));
    connect(pdf, &QPushButton::clicked, this, &PrintLayoutDialog::choosePdf);
    auto* print = buttons->addButton(tr("Print..."), QDialogButtonBox::AcceptRole);
    print->setObjectName(QStringLiteral("PrintToPrinter"));
    connect(print, &QPushButton::clicked, this, &PrintLayoutDialog::printPage);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    // Restore the last settings.
    QSettings stored;
    PrintSettings initial;
    initial.paper = stored.value(QStringLiteral("print/paper"), initial.paper).toSizeF();
    initial.landscape = stored.value(QStringLiteral("print/landscape"), initial.landscape).toBool();
    initial.margin = stored.value(QStringLiteral("print/margin"), initial.margin).toDouble();
    initial.spacing = stored.value(QStringLiteral("print/spacing"), initial.spacing).toDouble();
    initial.layers = stored.value(QStringLiteral("print/layers"), initial.layers).toInt();
    initial.drills = stored.value(QStringLiteral("print/drills"), initial.drills).toBool();
    initial.grouping = static_cast<PrintGrouping>(stored.value(QStringLiteral("print/grouping"), 0).toInt());
    initial.colours = static_cast<PrintColours>(stored.value(QStringLiteral("print/colours"), 0).toInt());
    initial.mirror = stored.value(QStringLiteral("print/mirror"), initial.mirror).toBool();
    initial.scale = stored.value(QStringLiteral("print/scale"), initial.scale).toDouble();
    initial.compensationX = stored.value(QStringLiteral("print/compensationX"), 1.0).toDouble();
    initial.compensationY = stored.value(QStringLiteral("print/compensationY"), 1.0).toDouble();
    setSettings(initial);
    fitToPage();

    for (auto* box : layerBoxes_) connect(box, &QCheckBox::toggled, this, &PrintLayoutDialog::refresh);
    for (auto* box : {drills_, landscape_, mirror_, rotate_}) connect(box, &QCheckBox::toggled, this, &PrintLayoutDialog::refresh);
    for (auto* combo : {grouping_, scale_, colours_}) {
        connect(combo, &QComboBox::currentIndexChanged, this, &PrintLayoutDialog::refresh);
    }
    connect(paper_, &QComboBox::currentIndexChanged, this, [this] {
        const auto preset = static_cast<PaperPreset>(paper_->currentData().toInt());
        if (!updating_ && preset != PaperPreset::Custom) {
            const QSizeF size = paperSize(preset);
            updating_ = true;
            paperWidth_->setValue(size.width());
            paperHeight_->setValue(size.height());
            updating_ = false;
        }
        refresh();
    });
    for (auto* field : {paperWidth_, paperHeight_}) {
        connect(field, &QDoubleSpinBox::valueChanged, this, [this] {
            if (updating_) return;
            // Typing a size that matches no preset switches to Custom.
            const QSizeF size(paperWidth_->value(), paperHeight_->value());
            int match = paper_->findData(static_cast<int>(PaperPreset::Custom));
            for (PaperPreset preset : Presets) {
                const QSizeF known = paperSize(preset);
                if (preset != PaperPreset::Custom && std::abs(known.width() - size.width()) < 0.05 &&
                    std::abs(known.height() - size.height()) < 0.05) {
                    match = paper_->findData(static_cast<int>(preset));
                }
            }
            updating_ = true;
            paper_->setCurrentIndex(match);
            updating_ = false;
            refresh();
        });
    }
    for (auto* field : {margin_, spacing_, compensationX_, compensationY_}) {
        connect(field, &QDoubleSpinBox::valueChanged, this, &PrintLayoutDialog::refresh);
    }
    for (auto* field : {columns_, rows_}) connect(field, &QSpinBox::valueChanged, this, &PrintLayoutDialog::refresh);
    refresh();
}

PrintSettings PrintLayoutDialog::settings() const {
    PrintSettings result;
    result.paper = QSizeF(paperWidth_->value(), paperHeight_->value());
    result.landscape = landscape_->isChecked();
    result.margin = margin_->value();
    result.spacing = spacing_->value();
    result.layers = 0;
    for (int kind = 0; kind < layerBoxes_.size(); ++kind) {
        if (layerBoxes_[kind]->isChecked()) result.layers |= 1 << kind;
    }
    result.drills = drills_->isChecked();
    result.grouping = static_cast<PrintGrouping>(grouping_->currentData().toInt());
    result.colours = static_cast<PrintColours>(colours_->currentData().toInt());
    result.mirror = mirror_->isChecked();
    result.rotate = rotate_->isChecked();
    result.columns = columns_->value();
    result.rows = rows_->value();
    result.scale = scale_->currentData().toDouble();
    result.compensationX = compensationX_->value();
    result.compensationY = compensationY_->value();
    return result;
}

void PrintLayoutDialog::setSettings(const PrintSettings& settings) {
    updating_ = true;
    // Paper size stored portrait; orientation separately.
    QSizeF paper = settings.paper;
    if (paper.width() > paper.height()) paper = QSizeF(paper.height(), paper.width());
    paperWidth_->setValue(paper.width());
    paperHeight_->setValue(paper.height());
    int preset = paper_->findData(static_cast<int>(PaperPreset::Custom));
    for (PaperPreset candidate : Presets) {
        const QSizeF known = paperSize(candidate);
        if (candidate != PaperPreset::Custom && std::abs(known.width() - paper.width()) < 0.05 &&
            std::abs(known.height() - paper.height()) < 0.05) {
            preset = paper_->findData(static_cast<int>(candidate));
        }
    }
    paper_->setCurrentIndex(preset);
    landscape_->setChecked(settings.landscape);
    margin_->setValue(settings.margin);
    spacing_->setValue(settings.spacing);
    for (int kind = 0; kind < layerBoxes_.size(); ++kind) layerBoxes_[kind]->setChecked(settings.layers & (1 << kind));
    drills_->setChecked(settings.drills);
    grouping_->setCurrentIndex(qMax(0, grouping_->findData(static_cast<int>(settings.grouping))));
    colours_->setCurrentIndex(qMax(0, colours_->findData(static_cast<int>(settings.colours))));
    mirror_->setChecked(settings.mirror);
    rotate_->setChecked(settings.rotate);
    columns_->setValue(settings.columns);
    rows_->setValue(settings.rows);
    int scale = scale_->findData(settings.scale);
    scale_->setCurrentIndex(scale >= 0 ? scale : scale_->findData(1.0));
    compensationX_->setValue(settings.compensationX);
    compensationY_->setValue(settings.compensationY);
    updating_ = false;
    refresh();
}

void PrintLayoutDialog::fitToPage() {
    const PrintSettings current = settings();
    const QRectF artwork = artworkBounds(output_, current.layers);
    const PrintFit fit = fitCopies(current, artwork.size(), true);
    if (fit.columns == 0) {
        refresh();
        return;
    }
    updating_ = true;
    rotate_->setChecked(fit.rotate);
    columns_->setValue(fit.columns);
    rows_->setValue(fit.rows);
    updating_ = false;
    refresh();
}

void PrintLayoutDialog::refresh() {
    if (updating_) return;
    const PrintSettings current = settings();
    const bool custom = static_cast<PaperPreset>(paper_->currentData().toInt()) == PaperPreset::Custom;
    paperWidth_->setEnabled(custom);
    paperHeight_->setEnabled(custom);
    const QRectF artwork = artworkBounds(output_, current.layers);
    int dropped = 0;
    const auto tiles = layoutTiles(current, artwork.size(), &dropped);
    if (artwork.isNull()) {
        summary_->setText(tr("Nothing to print: choose layers that have artwork."));
    } else {
        QString text = tr("Board %1 × %2 mm; %n artwork(s) on the page", nullptr, static_cast<int>(tiles.size()))
                           .arg(artwork.width(), 0, 'f', 1)
                           .arg(artwork.height(), 0, 'f', 1);
        if (dropped > 0) text += QLatin1Char('\n') + tr("%n artwork(s) do not fit and are left out", nullptr, dropped);
        summary_->setText(text);
    }
    preview_->setPage(&output_, current);
    storeSettings();
}

void PrintLayoutDialog::storeSettings() const {
    const PrintSettings current = settings();
    QSettings stored;
    stored.setValue(QStringLiteral("print/paper"), current.paper);
    stored.setValue(QStringLiteral("print/landscape"), current.landscape);
    stored.setValue(QStringLiteral("print/margin"), current.margin);
    stored.setValue(QStringLiteral("print/spacing"), current.spacing);
    stored.setValue(QStringLiteral("print/layers"), current.layers);
    stored.setValue(QStringLiteral("print/drills"), current.drills);
    stored.setValue(QStringLiteral("print/grouping"), static_cast<int>(current.grouping));
    stored.setValue(QStringLiteral("print/colours"), static_cast<int>(current.colours));
    stored.setValue(QStringLiteral("print/mirror"), current.mirror);
    stored.setValue(QStringLiteral("print/scale"), current.scale);
    stored.setValue(QStringLiteral("print/compensationX"), current.compensationX);
    stored.setValue(QStringLiteral("print/compensationY"), current.compensationY);
}

QString PrintLayoutDialog::savePdf(const QString& path) const {
    const PrintSettings current = settings();
    QPdfWriter writer(path);
    writer.setCreator(QStringLiteral("HattEDA"));
    writer.setTitle(documentName_);
    writer.setResolution(1200);
    const QSizeF page = pageSize(current);
    writer.setPageLayout(QPageLayout(QPageSize(page, QPageSize::Millimeter), QPageLayout::Portrait, QMarginsF()));
    QPainter painter;
    if (!painter.begin(&writer)) return tr("Cannot write %1.").arg(path);
    paintPrintPage(painter, output_, current, writer.resolution() / 25.4, false);
    painter.end();
    return {};
}

void PrintLayoutDialog::choosePdf() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Save print layout as PDF"), documentName_ + QStringLiteral(".pdf"),
                                                      tr("PDF files (*.pdf)"));
    if (path.isEmpty()) return;
    const QString error = savePdf(path);
    if (!error.isEmpty()) QMessageBox::warning(this, tr("Print layout"), error);
}

void PrintLayoutDialog::printPage() {
    const PrintSettings current = settings();
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(documentName_);
    printer.setFullPage(true); // the layout's own margin is measured from the paper edge
    const QSizeF page = pageSize(current);
    printer.setPageLayout(QPageLayout(QPageSize(current.paper, QPageSize::Millimeter),
                                      page.width() > page.height() ? QPageLayout::Landscape : QPageLayout::Portrait,
                                      QMarginsF()));
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() != QDialog::Accepted) return;
    QPainter painter;
    if (!painter.begin(&printer)) {
        QMessageBox::warning(this, tr("Print layout"), tr("The printer could not be started."));
        return;
    }
    paintPrintPage(painter, output_, current, printer.resolution() / 25.4, false);
    painter.end();
    accept();
}

} // namespace hatt::ui
