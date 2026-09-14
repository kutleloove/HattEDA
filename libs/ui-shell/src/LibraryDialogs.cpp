#include "hatt/ui/LibraryDialogs.hpp"

#include "hatt/ui/ComponentLibrary.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace hatt::ui {

// Footprint drawing for the editor: silkscreen shapes, copper pads with holes and pad numbers.
class FootprintPreview final : public QWidget {
public:
    explicit FootprintPreview(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("FootprintPreview"));
        setMinimumSize(300, 300);
    }
    void setSymbol(const SymbolDefinition& symbol) {
        symbol_ = symbol;
        update();
    }
    [[nodiscard]] const SymbolDefinition& symbol() const { return symbol_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool dark = palette().color(QPalette::Window).lightness() < 128;
        const QColor ground = dark ? QColor(QStringLiteral("#080b0f")) : QColor(QStringLiteral("#f4f1ea"));
        const QColor copper = dark ? QColor(QStringLiteral("#c8963e")) : QColor(QStringLiteral("#b3731f"));
        const QColor silk = dark ? QColor(QStringLiteral("#d9e1e8")) : QColor(QStringLiteral("#2b3640"));
        painter.fillRect(rect(), ground);
        QRectF bounds;
        auto include = [&](const QRectF& r) { bounds = bounds.isNull() ? r : bounds.united(r); };
        for (const auto& shape : symbol_.shapes) {
            for (const QPointF& p : shape.points) include(QRectF(p, QSizeF(0.001, 0.001)));
        }
        for (int i = 0; i < symbol_.pins.size() && i < symbol_.pads.size(); ++i) {
            const auto& pad = symbol_.pads[i];
            include(QRectF(symbol_.pins[i] - QPointF(pad.width / 2, pad.height / 2), QSizeF(pad.width, pad.height)));
        }
        if (bounds.isNull()) return;
        const QRectF area = QRectF(rect()).adjusted(16, 16, -16, -16);
        const double scale = std::min(area.width() / std::max(bounds.width(), 0.1),
                                      area.height() / std::max(bounds.height(), 0.1));
        auto map = [&](QPointF p) { return area.center() + (p - bounds.center()) * scale; };
        for (int i = 0; i < symbol_.pins.size() && i < symbol_.pads.size(); ++i) {
            const auto& pad = symbol_.pads[i];
            const QRectF r(map(symbol_.pins[i]) - QPointF(pad.width, pad.height) * scale / 2,
                           QSizeF(pad.width, pad.height) * scale);
            painter.setPen(Qt::NoPen);
            painter.setBrush(copper);
            if (pad.shape == PadShape::Rect) painter.drawRect(r);
            else if (pad.shape == PadShape::Round) painter.drawEllipse(r);
            else painter.drawRoundedRect(r, std::min(r.width(), r.height()) / 2, std::min(r.width(), r.height()) / 2);
            if (pad.drillDiameter > 0) {
                painter.setBrush(ground);
                const double radius = pad.drillDiameter * scale / 2;
                painter.drawEllipse(r.center(), radius, radius);
            }
            if (std::min(r.width(), r.height()) >= 12) {
                QFont font = painter.font();
                font.setPixelSize(std::clamp(static_cast<int>(std::min(r.width(), r.height()) * 0.45), 8, 14));
                painter.setFont(font);
                painter.setPen(dark ? QColor(QStringLiteral("#10151b")) : QColor(QStringLiteral("#ffffff")));
                painter.drawText(r, Qt::AlignCenter, QString::number(pad.number));
            }
        }
        painter.setBrush(Qt::NoBrush);
        for (const auto& shape : symbol_.shapes) {
            QPolygonF polygon;
            for (const QPointF& p : shape.points) polygon << map(p);
            painter.setPen(QPen(silk, 1.5));
            painter.setBrush(shape.filled ? QBrush(silk) : QBrush(Qt::NoBrush));
            shape.closed ? painter.drawPolygon(polygon) : painter.drawPolyline(polygon);
        }
    }

private:
    SymbolDefinition symbol_;
};

namespace {

QDoubleSpinBox* lengthField(QWidget* parent, const QString& name, double maximum = 100.0) {
    auto* field = new QDoubleSpinBox(parent);
    field->setObjectName(name);
    field->setRange(0.0, maximum);
    field->setDecimals(2);
    field->setSingleStep(0.05);
    field->setSuffix(QStringLiteral(" mm"));
    return field;
}

// 0 means "not in the datasheet".
QDoubleSpinBox* optionalField(QWidget* parent, const QString& name, const QString& suffix = QStringLiteral(" mm")) {
    auto* field = lengthField(parent, name);
    field->setSuffix(suffix);
    field->setSpecialValueText(DeviceEditorDialog::tr("unknown"));
    return field;
}

void fillStyles(QComboBox* combo, bool withUnknown) {
    if (withUnknown) combo->addItem(FootprintEditorDialog::tr("Unknown"), static_cast<int>(PackageStyle::None));
    combo->addItem(FootprintEditorDialog::tr("Two terminals (chip)"), static_cast<int>(PackageStyle::TwoTerminal));
    combo->addItem(FootprintEditorDialog::tr("Single row (header, SIP)"), static_cast<int>(PackageStyle::SingleRow));
    combo->addItem(FootprintEditorDialog::tr("Dual row (DIP, SOIC)"), static_cast<int>(PackageStyle::DualRow));
    combo->addItem(FootprintEditorDialog::tr("Four sides (QFP)"), static_cast<int>(PackageStyle::QuadRow));
}

QString millimetres(double value) {
    return QStringLiteral("%1 mm").arg(QString::number(value, 'g', 4));
}

} // namespace

FootprintEditorDialog::FootprintEditorDialog(std::optional<DeviceDefinition> device, QWidget* parent)
    : QDialog(parent), device_(std::move(device)), id_(newCustomFootprintId()) {
    setObjectName(QStringLiteral("FootprintEditorDialog"));
    setWindowTitle(device_ ? tr("Create footprint for %1").arg(device_->name) : tr("New footprint"));
    auto* layout = new QVBoxLayout(this);
    auto* columns = new QHBoxLayout;
    layout->addLayout(columns, 1);

    auto* form = new QFormLayout;
    name_ = new QLineEdit(device_ ? tr("%1 footprint").arg(device_->name) : tr("Footprint"), this);
    name_->setObjectName(QStringLiteral("FootprintName"));
    form->addRow(tr("Name"), name_);
    style_ = new QComboBox(this);
    style_->setObjectName(QStringLiteral("FootprintStyle"));
    fillStyles(style_, false);
    form->addRow(tr("Pad arrangement"), style_);
    padCount_ = new QSpinBox(this);
    padCount_->setObjectName(QStringLiteral("FootprintPadCount"));
    padCount_->setRange(1, MaxGeneratedPads);
    form->addRow(tr("Pads"), padCount_);
    pitch_ = lengthField(this, QStringLiteral("FootprintPitch"));
    form->addRow(tr("Pitch"), pitch_);
    rowSpacing_ = lengthField(this, QStringLiteral("FootprintRowSpacing"));
    form->addRow(tr("Row spacing (pad centres)"), rowSpacing_);
    shape_ = new QComboBox(this);
    shape_->setObjectName(QStringLiteral("FootprintPadShape"));
    shape_->addItem(tr("Round"), static_cast<int>(PadShape::Round));
    shape_->addItem(tr("Rectangle"), static_cast<int>(PadShape::Rect));
    shape_->addItem(tr("Oval"), static_cast<int>(PadShape::Oval));
    form->addRow(tr("Pad shape"), shape_);
    padWidth_ = lengthField(this, QStringLiteral("FootprintPadWidth"));
    form->addRow(tr("Pad width (across the row)"), padWidth_);
    padLength_ = lengthField(this, QStringLiteral("FootprintPadLength"));
    form->addRow(tr("Pad length (along the row)"), padLength_);
    throughHole_ = new QCheckBox(tr("Through-hole"), this);
    throughHole_->setObjectName(QStringLiteral("FootprintThroughHole"));
    form->addRow(QString(), throughHole_);
    drill_ = lengthField(this, QStringLiteral("FootprintDrill"));
    form->addRow(tr("Drill"), drill_);
    bodyWidth_ = lengthField(this, QStringLiteral("FootprintBodyWidth"));
    bodyWidth_->setSpecialValueText(tr("from pads"));
    form->addRow(tr("Body width (silkscreen)"), bodyWidth_);
    bodyLength_ = lengthField(this, QStringLiteral("FootprintBodyLength"));
    bodyLength_->setSpecialValueText(tr("from pads"));
    form->addRow(tr("Body length (silkscreen)"), bodyLength_);
    columns->addLayout(form);

    auto* previewColumn = new QVBoxLayout;
    preview_ = new FootprintPreview(this);
    previewColumn->addWidget(preview_, 1);
    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("FootprintSummary"));
    summary_->setWordWrap(true);
    previewColumn->addWidget(summary_);
    validation_ = new QLabel(this);
    validation_->setObjectName(QStringLiteral("FootprintValidation"));
    validation_->setWordWrap(true);
    previewColumn->addWidget(validation_);
    columns->addLayout(previewColumn, 1);
    if (device_) columns->addWidget(createInfoBox());

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("Create footprint"));
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons_);

    FootprintParams initial;
    if (device_) {
        initial = suggestFootprint(*device_).params;
        padCount_->setEnabled(false);
        padCount_->setToolTip(tr("Fixed to the %n pin(s) of the device", nullptr, device_->pinCount));
    }
    setParams(initial);
    for (auto* combo : {style_, shape_}) connect(combo, &QComboBox::currentIndexChanged, this, &FootprintEditorDialog::refresh);
    connect(padCount_, &QSpinBox::valueChanged, this, &FootprintEditorDialog::refresh);
    connect(name_, &QLineEdit::textChanged, this, &FootprintEditorDialog::refresh);
    for (auto* field : {pitch_, rowSpacing_, padWidth_, padLength_, drill_, bodyWidth_, bodyLength_}) {
        connect(field, &QDoubleSpinBox::valueChanged, this, &FootprintEditorDialog::refresh);
    }
    connect(throughHole_, &QCheckBox::toggled, this, [this](bool on) {
        if (on && drill_->value() <= 0) drill_->setValue(std::max(0.3, std::round(std::min(padWidth_->value(), padLength_->value()) * 5.0) / 10.0));
        refresh();
    });
}

QWidget* FootprintEditorDialog::createInfoBox() {
    const DeviceDefinition& device = *device_;
    const DeviceSpec& s = device.spec;
    const FootprintSuggestion suggestion = suggestFootprint(device);
    auto* box = new QFrame(this);
    box->setObjectName(QStringLiteral("DeviceInfoBox"));
    box->setFrameShape(QFrame::StyledPanel);
    box->setMinimumWidth(240);
    box->setMaximumWidth(300);
    auto* layout = new QVBoxLayout(box);
    auto* title = new QLabel(tr("DEVICE"), box);
    title->setObjectName(QStringLiteral("SectionLabel"));
    layout->addWidget(title);

    QStringList rows;
    auto row = [&rows](const QString& label, const QString& value) {
        rows << QStringLiteral("<tr><td>%1</td><td><b>%2</b></td></tr>").arg(label.toHtmlEscaped(), value.toHtmlEscaped());
    };
    row(tr("Name"), device.name);
    if (!s.manufacturer.isEmpty()) row(tr("Manufacturer"), s.manufacturer);
    if (!s.partNumber.isEmpty()) row(tr("Part number"), s.partNumber);
    row(tr("Pins"), QString::number(device.pinCount));
    if (s.package != PackageStyle::None) {
        const int index = style_->findData(static_cast<int>(s.package));
        row(tr("Package"), (index >= 0 ? style_->itemText(index) : QString()) +
                               (s.throughHole ? tr(", through-hole") : tr(", surface mount")));
    }
    if (s.pitch > 0) row(tr("Pitch"), millimetres(s.pitch));
    if (s.rowSpacing > 0) row(tr("Row spacing"), millimetres(s.rowSpacing));
    if (s.bodyWidth > 0 || s.bodyLength > 0)
        row(tr("Body"), QStringLiteral("%1 × %2").arg(millimetres(s.bodyWidth), millimetres(s.bodyLength)));
    if (s.leadWidth > 0 || s.leadLength > 0)
        row(tr("Leads"), QStringLiteral("%1 × %2").arg(millimetres(s.leadWidth), millimetres(s.leadLength)));
    if (s.pinCurrent > 0) {
        row(tr("Current per pin"), QStringLiteral("%1 A").arg(s.pinCurrent));
        row(tr("Min. copper width"), millimetres(std::round(recommendedCopperWidth(s.pinCurrent) * 100) / 100));
    }
    QString html = QStringLiteral("<table cellspacing='0' cellpadding='3'>%1</table>").arg(rows.join(QString()));
    if (!s.datasheet.isEmpty()) {
        html += QStringLiteral("<p><a href='%1'>%2</a></p>").arg(s.datasheet.toHtmlEscaped(), tr("Open datasheet"));
    }
    const bool onlyPins = !suggestion.fromDatasheet && s.pinCurrent <= 0;
    if (onlyPins) {
        html += QStringLiteral("<p>%1</p>").arg(
            tr("Only the pin count is known, so the footprint gets exactly %1 pads. The starting geometry is a "
               "generic 2.54 mm through-hole part; enter the datasheet pitch and pad sizes.")
                .arg(device.pinCount)
                .toHtmlEscaped());
    } else if (suggestion.fromDatasheet) {
        html += QStringLiteral("<p>%1</p>").arg(tr("Pitch, row spacing and pad sizes were generated from the datasheet values.").toHtmlEscaped());
    }
    for (const auto& note : suggestion.notes) html += QStringLiteral("<p>%1</p>").arg(note.toHtmlEscaped());
    auto* details = new QLabel(html, box);
    details->setObjectName(QStringLiteral("DeviceInfoDetails"));
    details->setWordWrap(true);
    details->setOpenExternalLinks(true);
    details->setTextFormat(Qt::RichText);
    layout->addWidget(details);
    auto* reset = new QPushButton(tr("Use datasheet values"), box);
    reset->setObjectName(QStringLiteral("FootprintUseDatasheet"));
    reset->setToolTip(tr("Regenerate pitch, row spacing and pads from the device data"));
    connect(reset, &QPushButton::clicked, this, [this] { setParams(suggestFootprint(*device_).params); });
    layout->addWidget(reset);
    layout->addStretch();
    return box;
}

void FootprintEditorDialog::setParams(const FootprintParams& p) {
    {
        const QSignalBlocker blockers[] = {QSignalBlocker(style_), QSignalBlocker(padCount_), QSignalBlocker(pitch_),
                                           QSignalBlocker(rowSpacing_), QSignalBlocker(shape_), QSignalBlocker(padWidth_),
                                           QSignalBlocker(padLength_), QSignalBlocker(throughHole_), QSignalBlocker(drill_),
                                           QSignalBlocker(bodyWidth_), QSignalBlocker(bodyLength_)};
        style_->setCurrentIndex(std::max(0, style_->findData(static_cast<int>(p.style))));
        padCount_->setValue(device_ ? device_->pinCount : p.padCount);
        pitch_->setValue(p.pitch);
        rowSpacing_->setValue(p.rowSpacing);
        shape_->setCurrentIndex(std::max(0, shape_->findData(static_cast<int>(p.shape))));
        padWidth_->setValue(p.padWidth);
        padLength_->setValue(p.padLength);
        throughHole_->setChecked(p.drill > 0);
        drill_->setValue(p.drill);
        bodyWidth_->setValue(p.bodyWidth);
        bodyLength_->setValue(p.bodyLength);
    }
    refresh();
}

FootprintParams FootprintEditorDialog::params() const {
    FootprintParams p;
    p.style = static_cast<PackageStyle>(style_->currentData().toInt());
    p.padCount = padCount_->value();
    p.pitch = pitch_->value();
    p.rowSpacing = rowSpacing_->value();
    p.shape = static_cast<PadShape>(shape_->currentData().toInt());
    p.padWidth = padWidth_->value();
    p.padLength = p.shape == PadShape::Round ? padWidth_->value() : padLength_->value();
    p.drill = throughHole_->isChecked() ? drill_->value() : 0.0;
    p.bodyWidth = bodyWidth_->value();
    p.bodyLength = bodyLength_->value();
    return p;
}

void FootprintEditorDialog::refresh() {
    const FootprintParams p = params();
    const bool rows = p.style != PackageStyle::TwoTerminal;
    pitch_->setEnabled(rows);
    rowSpacing_->setEnabled(p.style != PackageStyle::SingleRow);
    padLength_->setEnabled(p.shape != PadShape::Round);
    drill_->setEnabled(throughHole_->isChecked());
    const QString problem = validateFootprintParams(p);
    validation_->setText(problem);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty() && !name_->text().trimmed().isEmpty());
    preview_->setSymbol(problem.isEmpty() ? footprintSymbol(footprint()) : SymbolDefinition{});
    QStringList parts{tr("%n pad(s)", nullptr, p.padCount)};
    if (rows && p.padCount > 1) parts << tr("pitch %1").arg(millimetres(p.pitch));
    if (p.style != PackageStyle::SingleRow) parts << tr("row spacing %1").arg(millimetres(p.rowSpacing));
    parts << (p.drill > 0 ? tr("through-hole, drill %1").arg(millimetres(p.drill)) : tr("surface mount"));
    summary_->setText(parts.join(QStringLiteral("  ·  ")));
}

FootprintDefinition FootprintEditorDialog::footprint() const {
    FootprintDefinition footprint;
    footprint.id = id_;
    footprint.name = name_->text().trimmed();
    footprint.params = params();
    return footprint;
}

DeviceEditorDialog::DeviceEditorDialog(const ProjectLibrary& library, QWidget* parent)
    : QDialog(parent), library_(library), id_(newCustomDeviceId()) {
    setObjectName(QStringLiteral("DeviceEditorDialog"));
    setWindowTitle(tr("New device"));
    auto* layout = new QVBoxLayout(this);
    auto* columns = new QHBoxLayout;
    layout->addLayout(columns);

    auto* general = new QGroupBox(tr("Device"), this);
    auto* form = new QFormLayout(general);
    name_ = new QLineEdit(this);
    name_->setObjectName(QStringLiteral("DeviceName"));
    name_->setPlaceholderText(tr("e.g. LM358 dual op-amp"));
    form->addRow(tr("Name"), name_);
    prefix_ = new QLineEdit(QStringLiteral("U"), this);
    prefix_->setObjectName(QStringLiteral("DevicePrefix"));
    form->addRow(tr("Designator prefix"), prefix_);
    value_ = new QLineEdit(this);
    value_->setObjectName(QStringLiteral("DeviceValue"));
    form->addRow(tr("Default value"), value_);
    pinCount_ = new QSpinBox(this);
    pinCount_->setObjectName(QStringLiteral("DevicePinCount"));
    pinCount_->setRange(1, MaxGeneratedPads);
    pinCount_->setValue(2);
    form->addRow(tr("Pins"), pinCount_);
    pinNames_ = new QLineEdit(this);
    pinNames_->setObjectName(QStringLiteral("DevicePinNames"));
    pinNames_->setPlaceholderText(tr("optional, comma-separated: OUT1, IN1-, IN1+, GND"));
    form->addRow(tr("Pin names"), pinNames_);
    footprint_ = new QComboBox(this);
    footprint_->setObjectName(QStringLiteral("DeviceFootprint"));
    form->addRow(tr("Footprint"), footprint_);
    auto* create = new QPushButton(tr("Create footprint from device..."), this);
    create->setObjectName(QStringLiteral("DeviceCreateFootprint"));
    form->addRow(QString(), create);
    pinMap_ = new QTableWidget(0, 3, this);
    pinMap_->setObjectName(QStringLiteral("DevicePinMap"));
    pinMap_->setHorizontalHeaderLabels({tr("Pin"), tr("Name"), tr("Pad")});
    pinMap_->verticalHeader()->hide();
    pinMap_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    pinMap_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pinMap_->setSelectionMode(QAbstractItemView::NoSelection);
    pinMap_->setMinimumHeight(140);
    form->addRow(tr("Pin to pad"), pinMap_);
    columns->addWidget(general, 1);

    auto* datasheet = new QGroupBox(tr("Datasheet data (optional)"), this);
    auto* sheet = new QFormLayout(datasheet);
    manufacturer_ = new QLineEdit(this);
    manufacturer_->setObjectName(QStringLiteral("DeviceManufacturer"));
    sheet->addRow(tr("Manufacturer"), manufacturer_);
    partNumber_ = new QLineEdit(this);
    partNumber_->setObjectName(QStringLiteral("DevicePartNumber"));
    sheet->addRow(tr("Part number"), partNumber_);
    datasheet_ = new QLineEdit(this);
    datasheet_->setObjectName(QStringLiteral("DeviceDatasheet"));
    datasheet_->setPlaceholderText(QStringLiteral("https://"));
    sheet->addRow(tr("Datasheet link"), datasheet_);
    package_ = new QComboBox(this);
    package_->setObjectName(QStringLiteral("DevicePackage"));
    fillStyles(package_, true);
    sheet->addRow(tr("Package"), package_);
    throughHole_ = new QCheckBox(tr("Through-hole leads"), this);
    throughHole_->setObjectName(QStringLiteral("DeviceThroughHole"));
    sheet->addRow(QString(), throughHole_);
    pitch_ = optionalField(this, QStringLiteral("DevicePitch"));
    sheet->addRow(tr("Pitch"), pitch_);
    rowSpacing_ = optionalField(this, QStringLiteral("DeviceRowSpacing"));
    sheet->addRow(tr("Row spacing (pad centres)"), rowSpacing_);
    bodyWidth_ = optionalField(this, QStringLiteral("DeviceBodyWidth"));
    sheet->addRow(tr("Body width"), bodyWidth_);
    bodyLength_ = optionalField(this, QStringLiteral("DeviceBodyLength"));
    sheet->addRow(tr("Body length"), bodyLength_);
    leadWidth_ = optionalField(this, QStringLiteral("DeviceLeadWidth"));
    sheet->addRow(tr("Lead width"), leadWidth_);
    leadLength_ = optionalField(this, QStringLiteral("DeviceLeadLength"));
    sheet->addRow(tr("Lead (foot) length"), leadLength_);
    pinCurrent_ = optionalField(this, QStringLiteral("DevicePinCurrent"), QStringLiteral(" A"));
    sheet->addRow(tr("Max. current per pin"), pinCurrent_);
    columns->addWidget(datasheet, 1);

    validation_ = new QLabel(this);
    validation_->setObjectName(QStringLiteral("DeviceValidation"));
    validation_->setWordWrap(true);
    layout->addWidget(validation_);
    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("Create device"));
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons_);

    connect(create, &QPushButton::clicked, this, &DeviceEditorDialog::createFootprint);
    connect(pinCount_, &QSpinBox::valueChanged, this, &DeviceEditorDialog::refreshFootprints);
    connect(footprint_, &QComboBox::currentIndexChanged, this, &DeviceEditorDialog::refreshPinMap);
    connect(pinNames_, &QLineEdit::textChanged, this, &DeviceEditorDialog::refreshPinMap);
    for (auto* field : {name_, prefix_, pinNames_}) connect(field, &QLineEdit::textChanged, this, &DeviceEditorDialog::validate);
    refreshFootprints();
}

void DeviceEditorDialog::refreshPinMap() {
    const int pins = pinCount_->value();
    const bool assigned = !footprint_->currentData().toString().isEmpty();
    QVector<int> previous;
    for (int row = 0; row < pinMap_->rowCount(); ++row) {
        const auto* pad = qobject_cast<QSpinBox*>(pinMap_->cellWidget(row, 2));
        previous.append(pad != nullptr ? pad->value() : row + 1);
    }
    const QStringList names = pinNames_->text().split(QLatin1Char(','));
    pinMap_->setRowCount(assigned ? pins : 0);
    for (int row = 0; assigned && row < pins; ++row) {
        auto* number = new QTableWidgetItem(QString::number(row + 1));
        pinMap_->setItem(row, 0, number);
        pinMap_->setItem(row, 1, new QTableWidgetItem(row < names.size() ? names[row].trimmed() : QString()));
        auto* pad = qobject_cast<QSpinBox*>(pinMap_->cellWidget(row, 2));
        if (pad == nullptr) {
            pad = new QSpinBox(pinMap_);
            pad->setObjectName(QStringLiteral("DevicePinPad%1").arg(row + 1));
            connect(pad, &QSpinBox::valueChanged, this, &DeviceEditorDialog::validate);
            pinMap_->setCellWidget(row, 2, pad);
        }
        const QSignalBlocker blocker(pad);
        pad->setRange(1, pins);
        pad->setValue(row < previous.size() && previous[row] <= pins ? previous[row] : row + 1);
    }
    pinMap_->setEnabled(assigned);
    validate();
}

QVector<int> DeviceEditorDialog::pinPadMap() const {
    QVector<int> map;
    bool identity = true;
    for (int row = 0; row < pinMap_->rowCount(); ++row) {
        const auto* pad = qobject_cast<QSpinBox*>(pinMap_->cellWidget(row, 2));
        map.append(pad != nullptr ? pad->value() : row + 1);
        identity = identity && map.last() == row + 1;
    }
    // Pin N to pad N is stored as an empty map.
    return identity ? QVector<int>{} : map;
}

void DeviceEditorDialog::refreshFootprints() {
    const QString current = footprint_->currentData().toString();
    ProjectLibrary library = library_;
    library.customFootprints += created_;
    const QSignalBlocker blocker(footprint_);
    footprint_->clear();
    footprint_->addItem(tr("Unassigned"), QString());
    for (const auto* symbol : footprintsWithPads(library, pinCount_->value())) {
        footprint_->addItem(symbolDisplayName(*symbol), symbol->id);
    }
    footprint_->setCurrentIndex(std::max(0, footprint_->findData(current)));
    refreshPinMap();
}

void DeviceEditorDialog::validate() {
    QString problem;
    const QStringList names = pinNames_->text().trimmed().isEmpty() ? QStringList{} : pinNames_->text().split(QLatin1Char(','));
    if (name_->text().trimmed().isEmpty()) problem = tr("Enter a device name.");
    else if (!QRegularExpression(QStringLiteral("^[A-Za-z]{1,8}$")).match(prefix_->text().trimmed()).hasMatch())
        problem = tr("The designator prefix must be 1 to 8 letters, e.g. U or IC.");
    else if (names.size() > pinCount_->value())
        problem = tr("%1 pin names were entered for %2 pins.").arg(names.size()).arg(pinCount_->value());
    else problem = validatePinPadMap(pinPadMap(), pinCount_->value());
    validation_->setText(problem);
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

void DeviceEditorDialog::createFootprint() {
    DeviceDefinition current = device();
    if (current.name.isEmpty()) current.name = tr("New device");
    FootprintEditorDialog editor(current, this);
    if (editor.exec() != QDialog::Accepted) return;
    const FootprintDefinition footprint = editor.footprint();
    created_.append(footprint);
    registerSymbols({footprintSymbol(footprint)});
    refreshFootprints();
    footprint_->setCurrentIndex(std::max(0, footprint_->findData(footprint.id)));
}

DeviceDefinition DeviceEditorDialog::device() const {
    DeviceDefinition device;
    device.id = id_;
    device.name = name_->text().trimmed();
    device.prefix = prefix_->text().trimmed();
    device.defaultValue = value_->text().trimmed();
    device.footprint = footprint_->currentData().toString();
    device.pinCount = pinCount_->value();
    if (!device.footprint.isEmpty()) device.pinPadMap = pinPadMap();
    if (!pinNames_->text().trimmed().isEmpty()) {
        for (const auto& name : pinNames_->text().split(QLatin1Char(','))) device.pinNames << name.trimmed();
    }
    DeviceSpec& s = device.spec;
    s.manufacturer = manufacturer_->text().trimmed();
    s.partNumber = partNumber_->text().trimmed();
    s.datasheet = datasheet_->text().trimmed();
    s.package = static_cast<PackageStyle>(package_->currentData().toInt());
    s.throughHole = throughHole_->isChecked();
    s.pitch = pitch_->value();
    s.rowSpacing = rowSpacing_->value();
    s.bodyWidth = bodyWidth_->value();
    s.bodyLength = bodyLength_->value();
    s.leadWidth = leadWidth_->value();
    s.leadLength = leadLength_->value();
    s.pinCurrent = pinCurrent_->value();
    return device;
}

} // namespace hatt::ui
