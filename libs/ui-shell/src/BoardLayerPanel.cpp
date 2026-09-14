#include "hatt/ui/BoardLayerPanel.hpp"

#include "hatt/ui/DesignCanvas.hpp"

#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace hatt::ui {
namespace {

constexpr int LayerRole = Qt::UserRole;

const QString ActiveLayerKey = QStringLiteral("editor/board/activeLayer");
const QString VisibleLayersKey = QStringLiteral("editor/board/visibleLayers");

QIcon swatch(const QColor& color, const QPalette& palette) {
    QPixmap pixmap(28, 28);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette.color(QPalette::Mid), 1.0));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(1.5, 1.5, 11, 11), 2, 2);
    return QIcon(pixmap);
}

QLabel* sectionLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("SectionLabel"));
    return label;
}

} // namespace

BoardLayerPanel::BoardLayerPanel(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("BoardLayerPanel"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    QSettings settings;
    layout->addWidget(sectionLabel(tr("VISIBLE LAYERS"), this));
    visibility_ = new QListWidget(this);
    visibility_->setObjectName(QStringLiteral("LayerVisibility"));
    visibility_->setToolTip(tr("Shown layers; hidden layers cannot be selected"));
    visibility_->setIconSize(QSize(14, 14));
    const int visible = settings.value(VisibleLayersKey, AllLayersMask).toInt();
    for (int index = 0; index < BoardLayerCount; ++index) {
        auto* item = new QListWidgetItem(boardLayerName(static_cast<BoardLayer>(index)), visibility_);
        item->setData(LayerRole, index);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState((visible & (1 << index)) ? Qt::Checked : Qt::Unchecked);
    }
    visibility_->setFixedHeight(visibility_->sizeHintForRow(0) * 5 + 2 * visibility_->frameWidth());
    connect(visibility_, &QListWidget::itemChanged, this, [this] {
        storeVisibility();
        emit visibleLayersChanged(visibleLayers());
    });
    layout->addWidget(visibility_);

    layout->addWidget(sectionLabel(tr("ACTIVE LAYER"), this));
    activeLayer_ = new QComboBox(this);
    activeLayer_->setObjectName(QStringLiteral("ActiveLayer"));
    activeLayer_->setToolTip(tr("Layer for new tracks, pads and 2D graphics (Space swaps top and "
                                "bottom copper, Page Up / Page Down select them)"));
    activeLayer_->setIconSize(QSize(14, 14));
    for (int index = 0; index < BoardLayerCount; ++index) {
        activeLayer_->addItem(boardLayerName(static_cast<BoardLayer>(index)), index);
    }
    activeLayer_->setCurrentIndex(
        std::clamp(settings.value(ActiveLayerKey, 0).toInt(), 0, BoardLayerCount - 1));
    connect(activeLayer_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue(ActiveLayerKey, activeLayer_->currentIndex());
        emit activeLayerChanged(activeLayer());
    });
    layout->addWidget(activeLayer_);
    refreshColors();
}

BoardLayer BoardLayerPanel::activeLayer() const {
    return static_cast<BoardLayer>(std::clamp(activeLayer_->currentIndex(), 0, BoardLayerCount - 1));
}

int BoardLayerPanel::visibleLayers() const {
    int mask = 0;
    for (int row = 0; row < visibility_->count(); ++row) {
        const auto* item = visibility_->item(row);
        if (item->checkState() == Qt::Checked) mask |= 1 << item->data(LayerRole).toInt();
    }
    return mask;
}

void BoardLayerPanel::setActiveLayer(BoardLayer layer) {
    const QSignalBlocker blocker(activeLayer_);
    activeLayer_->setCurrentIndex(static_cast<int>(layer));
    QSettings().setValue(ActiveLayerKey, static_cast<int>(layer));
}

void BoardLayerPanel::setVisibleLayers(int mask) {
    {
        const QSignalBlocker blocker(visibility_);
        for (int row = 0; row < visibility_->count(); ++row) {
            auto* item = visibility_->item(row);
            item->setCheckState((mask & (1 << item->data(LayerRole).toInt())) ? Qt::Checked
                                                                              : Qt::Unchecked);
        }
    }
    storeVisibility();
}

void BoardLayerPanel::changeEvent(QEvent* event) {
    if (event->type() == QEvent::PaletteChange) refreshColors();
    QWidget::changeEvent(event);
}

void BoardLayerPanel::refreshColors() {
    const QSignalBlocker comboBlocker(activeLayer_);
    const QSignalBlocker listBlocker(visibility_);
    for (int index = 0; index < BoardLayerCount; ++index) {
        const QIcon icon = swatch(DesignCanvas::layerColor(static_cast<BoardLayer>(index), palette()), palette());
        activeLayer_->setItemIcon(index, icon);
        visibility_->item(index)->setIcon(icon);
    }
}

void BoardLayerPanel::storeVisibility() { QSettings().setValue(VisibleLayersKey, visibleLayers()); }

} // namespace hatt::ui
