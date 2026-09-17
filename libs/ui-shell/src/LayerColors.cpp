#include "hatt/ui/LayerColors.hpp"

#include <QColorDialog>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>

#include <map>

namespace hatt::ui {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("hatt::ui::LayerColors", text); }

QString key(BoardLayer layer, bool dark) {
    return QStringLiteral("appearance/layerColors/%1/%2")
        .arg(dark ? QStringLiteral("dark") : QStringLiteral("light"))
        .arg(static_cast<int>(layer));
}

QIcon swatch(const QColor& color) {
    QPixmap pixmap(40, 20);
    pixmap.fill(color);
    return QIcon(pixmap);
}

} // namespace

QColor defaultLayerColor(BoardLayer layer, bool dark) {
    switch (layer) {
    case BoardLayer::TopCopper: return QColor(dark ? "#ff4d4d" : "#d11f1f");
    case BoardLayer::BottomCopper: return QColor(dark ? "#4d8dff" : "#1f4fd1");
    case BoardLayer::TopSilk: return QColor(dark ? "#f2f2f2" : "#3a3a3a");
    case BoardLayer::BottomSilk: return QColor(dark ? "#c9a3e6" : "#7a48a3");
    case BoardLayer::TopResist: return QColor(dark ? "#4cc38a" : "#1d8a5c");
    case BoardLayer::BottomResist: return QColor(dark ? "#5cc8c8" : "#1f8080");
    case BoardLayer::TopPaste: return QColor(dark ? "#a9b4be" : "#66727d");
    case BoardLayer::BottomPaste: return QColor(dark ? "#8f99cc" : "#4d5891");
    case BoardLayer::BoardEdge: return QColor(dark ? "#e9c46a" : "#b8860b");
    }
    return {};
}

QColor boardLayerColor(BoardLayer layer, bool dark) {
    const QColor stored(QSettings().value(key(layer, dark)).toString());
    return stored.isValid() ? stored : defaultLayerColor(layer, dark);
}

QColor throughHoleColor(bool dark) {
    const QColor stored(QSettings()
                            .value(QStringLiteral("appearance/layerColors/%1/through-hole")
                                       .arg(dark ? QStringLiteral("dark") : QStringLiteral("light")))
                            .toString());
    return stored.isValid() ? stored : QColor(dark ? "#b45cff" : "#7b2fbf");
}

void setLayerColorOverride(BoardLayer layer, bool dark, const QColor& color) {
    if (color.isValid() && color != defaultLayerColor(layer, dark)) {
        QSettings().setValue(key(layer, dark), color.name());
    } else {
        QSettings().remove(key(layer, dark));
    }
}

void clearLayerColorOverrides(bool dark) {
    for (int index = 0; index < BoardLayerCount; ++index) {
        QSettings().remove(key(static_cast<BoardLayer>(index), dark));
    }
}

bool editLayerColorsDialog(QWidget* parent, bool dark) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("LayerColorsDialog"));
    dialog.setWindowTitle(dark ? tr("Layer colours (dark theme)") : tr("Layer colours (light theme)"));
    auto* form = new QFormLayout(&dialog);
    std::map<int, QColor> chosen;
    QList<QPushButton*> buttons;
    for (int index = 0; index < BoardLayerCount; ++index) {
        const auto layer = static_cast<BoardLayer>(index);
        chosen[index] = boardLayerColor(layer, dark);
        auto* button = new QPushButton(&dialog);
        button->setObjectName(QStringLiteral("LayerColor.%1").arg(index));
        button->setIcon(swatch(chosen[index]));
        button->setIconSize(QSize(40, 20));
        button->setText(chosen[index].name());
        QObject::connect(button, &QPushButton::clicked, &dialog, [&, index, button, layer] {
            const QColor color = QColorDialog::getColor(chosen[index], &dialog, boardLayerName(layer));
            if (!color.isValid()) return;
            chosen[index] = color;
            button->setIcon(swatch(color));
            button->setText(color.name());
        });
        buttons.append(button);
        form->addRow(boardLayerName(layer), button);
    }
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto* reset = box->addButton(tr("Restore defaults"), QDialogButtonBox::ResetRole);
    reset->setObjectName(QStringLiteral("LayerColorsReset"));
    QObject::connect(reset, &QPushButton::clicked, &dialog, [&] {
        for (int index = 0; index < BoardLayerCount; ++index) {
            chosen[index] = defaultLayerColor(static_cast<BoardLayer>(index), dark);
            buttons[index]->setIcon(swatch(chosen[index]));
            buttons[index]->setText(chosen[index].name());
        }
    });
    QObject::connect(box, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(new QLabel(tr("Top copper red and bottom copper blue follow the Proteus and KiCad convention."),
                            &dialog));
    form->addRow(box);
    if (dialog.exec() != QDialog::Accepted) return false;
    for (const auto& [index, color] : chosen) setLayerColorOverride(static_cast<BoardLayer>(index), dark, color);
    return true;
}

} // namespace hatt::ui
