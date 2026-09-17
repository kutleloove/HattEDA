#include "hatt/ui/PadStyles.hpp"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QUuid>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace hatt::ui {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("hatt::ui::PadStyles", text); }

const QString CustomPadStylesKey = QStringLiteral("editor/board/customPadStyles");

QString padSizeProblem(const PadStyleEntry& style);

QString shapeToken(PadShape shape) {
    switch (shape) {
    case PadShape::Round: return QStringLiteral("round");
    case PadShape::Rect: return QStringLiteral("rect");
    case PadShape::Oval: return QStringLiteral("oval");
    }
    return QStringLiteral("round");
}

PadShape shapeFromToken(const QString& token) {
    if (token == QLatin1String("rect")) return PadShape::Rect;
    if (token == QLatin1String("oval")) return PadShape::Oval;
    return PadShape::Round;
}

// Through-hole pads conduct on both copper layers; SMD pads go to the active copper layer when
// they are placed, so they are stored on top copper.
void normaliseLayers(PadDefinition& pad) {
    pad.layers = pad.drillDiameter > 0.0 ? CopperLayerMask : layerBit(BoardLayer::TopCopper);
    if (pad.shape == PadShape::Round) pad.height = pad.width;
}

} // namespace

QVector<PadStyleEntry> customPadStyles() {
    QVector<PadStyleEntry> result;
    for (const QVariant& entry : QSettings().value(CustomPadStylesKey).toList()) {
        const QVariantMap map = entry.toMap();
        PadStyleEntry style;
        style.id = map.value(QStringLiteral("id")).toString();
        style.name = map.value(QStringLiteral("name")).toString().trimmed();
        style.pad.shape = shapeFromToken(map.value(QStringLiteral("shape")).toString());
        style.pad.width = map.value(QStringLiteral("width")).toDouble();
        style.pad.height = map.value(QStringLiteral("height")).toDouble();
        style.pad.drillDiameter = map.value(QStringLiteral("drill")).toDouble();
        normaliseLayers(style.pad);
        if (style.id.startsWith(UserPadStylePrefix) && padSizeProblem(style).isEmpty()) result.append(style);
    }
    return result;
}

void setCustomPadStyles(const QVector<PadStyleEntry>& styles) {
    QVariantList list;
    for (const auto& style : styles) {
        QVariantMap map;
        map.insert(QStringLiteral("id"), style.id);
        map.insert(QStringLiteral("name"), style.name);
        map.insert(QStringLiteral("shape"), shapeToken(style.pad.shape));
        map.insert(QStringLiteral("width"), style.pad.width);
        map.insert(QStringLiteral("height"), style.pad.height);
        map.insert(QStringLiteral("drill"), style.pad.drillDiameter);
        list.append(map);
    }
    QSettings().setValue(CustomPadStylesKey, list);
}

QVector<PadStyleEntry> padStyleEntries() {
    QVector<PadStyleEntry> result;
    for (const auto& style : padStyles()) {
        result.append({QLatin1String(style.id), padStyleDisplayName(style), style.pad, true});
    }
    result.append(customPadStyles());
    return result;
}

std::optional<PadStyleEntry> findPadStyleEntry(const QString& id) {
    if (const auto* style = findPadStyle(id)) {
        return PadStyleEntry{QLatin1String(style->id), padStyleDisplayName(*style), style->pad, true};
    }
    if (!id.startsWith(UserPadStylePrefix)) return std::nullopt;
    for (const auto& style : customPadStyles()) {
        if (style.id == id) return style;
    }
    return std::nullopt;
}

namespace {

// Shape and size checks only; customPadStyles uses it without looking at other styles.
QString padSizeProblem(const PadStyleEntry& style) {
    if (style.name.trimmed().isEmpty()) return tr("Enter a style name.");
    const PadDefinition& pad = style.pad;
    const double height = pad.shape == PadShape::Round ? pad.width : pad.height;
    if (!(pad.width > 0.0) || !(height > 0.0) || !std::isfinite(pad.width) || !std::isfinite(height)) {
        return tr("The pad size must be greater than zero.");
    }
    if (pad.drillDiameter < 0.0 || !std::isfinite(pad.drillDiameter)) return tr("The drill cannot be negative.");
    if (pad.drillDiameter > 0.0 && pad.drillDiameter >= std::min(pad.width, height)) {
        return tr("The drill must be smaller than the pad.");
    }
    return {};
}

} // namespace

QString padStyleProblem(const PadStyleEntry& style) {
    for (const auto& existing : padStyleEntries()) {
        if (existing.id != style.id && existing.name.compare(style.name.trimmed(), Qt::CaseInsensitive) == 0) {
            return tr("A pad style named %1 already exists.").arg(existing.name);
        }
    }
    return padSizeProblem(style);
}

bool editPadStyleDialog(QWidget* parent, PadStyleEntry& style) {
    const bool creating = style.id.isEmpty();
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("PadStyleDialog"));
    dialog.setWindowTitle(creating ? tr("New pad style") : tr("Edit pad style"));
    auto* form = new QFormLayout(&dialog);

    auto* name = new QLineEdit(style.name, &dialog);
    name->setObjectName(QStringLiteral("PadStyleName"));
    name->setPlaceholderText(tr("e.g. C-70-30 or S-60x120"));
    form->addRow(tr("Name"), name);
    auto* shape = new QComboBox(&dialog);
    shape->setObjectName(QStringLiteral("PadStyleShape"));
    shape->addItem(tr("Round"), static_cast<int>(PadShape::Round));
    shape->addItem(tr("Rectangular"), static_cast<int>(PadShape::Rect));
    shape->addItem(tr("Oval"), static_cast<int>(PadShape::Oval));
    shape->setCurrentIndex(std::max(0, shape->findData(static_cast<int>(style.pad.shape))));
    form->addRow(tr("Shape"), shape);
    auto field = [&](const char* objectName, double value) {
        auto* spin = new QDoubleSpinBox(&dialog);
        spin->setObjectName(QLatin1String(objectName));
        spin->setDecimals(3);
        spin->setRange(0.0, 50.0);
        spin->setSingleStep(0.05);
        spin->setSuffix(QStringLiteral(" mm"));
        spin->setValue(value);
        return spin;
    };
    auto* width = field("PadStyleWidth", style.pad.width > 0.0 ? style.pad.width : 1.6);
    form->addRow(tr("Width (X)"), width);
    auto* height = field("PadStyleHeight", style.pad.height > 0.0 ? style.pad.height : 1.6);
    form->addRow(tr("Height (Y)"), height);
    auto* drill = field("PadStyleDrill", style.pad.drillDiameter);
    drill->setToolTip(tr("0 makes a surface mount pad on the active copper layer"));
    form->addRow(tr("Drill (0 = SMD)"), drill);
    auto* validation = new QLabel(&dialog);
    validation->setObjectName(QStringLiteral("PadStyleValidation"));
    validation->setWordWrap(true);
    form->addRow(validation);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);

    auto current = [&] {
        PadStyleEntry result = style;
        result.name = name->text().trimmed();
        result.pad.shape = static_cast<PadShape>(shape->currentData().toInt());
        result.pad.width = width->value();
        result.pad.height = height->value();
        result.pad.drillDiameter = drill->value();
        normaliseLayers(result.pad);
        return result;
    };
    auto validate = [&] {
        height->setEnabled(shape->currentData().toInt() != static_cast<int>(PadShape::Round));
        const QString problem = padStyleProblem(current());
        validation->setText(problem);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
    };
    QObject::connect(name, &QLineEdit::textChanged, &dialog, validate);
    QObject::connect(shape, &QComboBox::currentIndexChanged, &dialog, validate);
    for (auto* spin : {width, height, drill}) QObject::connect(spin, &QDoubleSpinBox::valueChanged, &dialog, validate);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    validate();
    if (dialog.exec() != QDialog::Accepted) return false;
    style = current();
    style.builtIn = false;
    if (creating) style.id = UserPadStylePrefix + QUuid::createUuid().toString(QUuid::WithoutBraces);
    return true;
}

} // namespace hatt::ui
