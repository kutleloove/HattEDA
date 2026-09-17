#include "hatt/ui/RoutingStyles.hpp"

#include "hatt/ui/SketchModel.hpp"

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
#include <QVariantMap>

namespace hatt::ui {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("hatt::ui::RoutingStyles", text); }

QString settingsKey(RoutingStyleKind kind) {
    return kind == RoutingStyleKind::Track ? QStringLiteral("editor/board/customTrackStyles")
                                           : QStringLiteral("editor/board/customViaStyles");
}

constexpr double MilPerMm = 1000.0 / 25.4;

} // namespace

QVector<RoutingStyle> customRoutingStyles(RoutingStyleKind kind) {
    QVector<RoutingStyle> result;
    for (const QVariant& entry : QSettings().value(settingsKey(kind)).toList()) {
        const QVariantMap map = entry.toMap();
        RoutingStyle style;
        style.name = map.value(QStringLiteral("name")).toString().trimmed();
        style.width = map.value(QStringLiteral("width")).toDouble();
        style.drill = map.value(QStringLiteral("drill")).toDouble();
        const bool valid = !style.name.isEmpty() && style.width > 0.0 &&
                           (kind == RoutingStyleKind::Track || (style.drill > 0.0 && style.drill < style.width));
        if (valid) result.append(style);
    }
    return result;
}

void setCustomRoutingStyles(RoutingStyleKind kind, const QVector<RoutingStyle>& styles) {
    QVariantList list;
    for (const auto& style : styles) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), style.name);
        map.insert(QStringLiteral("width"), style.width);
        if (kind == RoutingStyleKind::Via) map.insert(QStringLiteral("drill"), style.drill);
        list.append(map);
    }
    QSettings().setValue(settingsKey(kind), list);
}

QVector<RoutingStyle> routingStyles(RoutingStyleKind kind) {
    QVector<RoutingStyle> result;
    if (kind == RoutingStyleKind::Track) {
        for (const auto& style : trackStyles()) {
            result.append({QLatin1String(style.name), style.width, 0.0, true});
        }
    } else {
        for (const auto& style : viaStyles()) {
            result.append({QLatin1String(style.name), style.diameter, style.drill, true});
        }
    }
    result.append(customRoutingStyles(kind));
    return result;
}

RoutingStyle findRoutingStyle(RoutingStyleKind kind, const QString& name) {
    for (const auto& style : routingStyles(kind)) {
        if (style.name == name) return style;
    }
    return {};
}

QString routingStyleProblem(RoutingStyleKind kind, const RoutingStyle& style, const QString& previousName) {
    const QString name = style.name.trimmed();
    if (name.isEmpty()) return tr("Enter a style name.");
    if (name.compare(previousName, Qt::CaseInsensitive) != 0) {
        for (const auto& existing : routingStyles(kind)) {
            if (existing.name.compare(name, Qt::CaseInsensitive) == 0) {
                return tr("A style named %1 already exists.").arg(existing.name);
            }
        }
    }
    if (style.width <= 0.0) return tr("The width must be greater than zero.");
    if (kind == RoutingStyleKind::Via && (style.drill <= 0.0 || style.drill >= style.width)) {
        return tr("The via drill must be smaller than the via diameter.");
    }
    return {};
}

bool editRoutingStyleDialog(QWidget* parent, RoutingStyleKind kind, RoutingStyle& style) {
    const bool track = kind == RoutingStyleKind::Track;
    const QString previousName = style.name;
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("RoutingStyleDialog"));
    dialog.setWindowTitle(previousName.isEmpty() ? (track ? tr("New track style") : tr("New via style"))
                                                 : (track ? tr("Edit track style") : tr("Edit via style")));
    auto* form = new QFormLayout(&dialog);

    auto* name = new QLineEdit(style.name, &dialog);
    name->setObjectName(QStringLiteral("RoutingStyleName"));
    name->setPlaceholderText(track ? QStringLiteral("T16") : QStringLiteral("V36"));
    form->addRow(tr("Name"), name);

    auto* unit = new QComboBox(&dialog);
    unit->setObjectName(QStringLiteral("RoutingStyleUnit"));
    unit->addItem(QStringLiteral("mm"));
    unit->addItem(tr("th (mil)"));
    form->addRow(tr("Unit"), unit);

    auto makeField = [&](const char* objectName) {
        auto* field = new QDoubleSpinBox(&dialog);
        field->setObjectName(QLatin1String(objectName));
        field->setDecimals(3);
        field->setRange(0.0, 10.0);
        field->setSingleStep(0.05);
        return field;
    };
    auto* width = makeField("RoutingStyleWidth");
    width->setValue(style.width > 0.0 ? style.width : (track ? DefaultTrackWidth : DefaultViaDiameter));
    form->addRow(track ? tr("Width") : tr("Diameter"), width);
    QDoubleSpinBox* drill = nullptr;
    if (!track) {
        drill = makeField("RoutingStyleDrill");
        drill->setValue(style.drill > 0.0 ? style.drill : DefaultViaDrill);
        form->addRow(tr("Drill"), drill);
    }

    // Switching units converts the values in place; the style is always stored in mm.
    QObject::connect(unit, &QComboBox::currentIndexChanged, &dialog, [=](int index) {
        const bool mil = index == 1;
        for (auto* field : {width, drill}) {
            if (field == nullptr) continue;
            const double value = field->value();
            field->setDecimals(mil ? 1 : 3);
            field->setRange(0.0, mil ? 400.0 : 10.0);
            field->setSingleStep(mil ? 1.0 : 0.05);
            field->setValue(mil ? value * MilPerMm : value / MilPerMm);
        }
    });

    auto* validation = new QLabel(&dialog);
    validation->setObjectName(QStringLiteral("RoutingStyleValidation"));
    validation->setWordWrap(true);
    form->addRow(validation);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);

    auto current = [=] {
        const double scale = unit->currentIndex() == 1 ? 1.0 / MilPerMm : 1.0;
        RoutingStyle result;
        result.name = name->text().trimmed();
        result.width = width->value() * scale;
        result.drill = drill != nullptr ? drill->value() * scale : 0.0;
        return result;
    };
    auto validate = [=] {
        const QString problem = routingStyleProblem(kind, current(), previousName);
        validation->setText(problem);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
    };
    QObject::connect(name, &QLineEdit::textChanged, &dialog, validate);
    QObject::connect(width, &QDoubleSpinBox::valueChanged, &dialog, validate);
    if (drill != nullptr) QObject::connect(drill, &QDoubleSpinBox::valueChanged, &dialog, validate);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    validate();

    if (dialog.exec() != QDialog::Accepted) return false;
    style = current();
    return true;
}

} // namespace hatt::ui
