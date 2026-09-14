#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QDialog>
#include <optional>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace hatt::ui {

class FootprintPreview;

// Creates a parametric footprint (ADR-0007). Opened from a device, the pad count is fixed to the
// device's pin count, the starting geometry comes from suggestFootprint and `DeviceInfoBox` on the
// right shows the device's datasheet data (current rating, pitch, body) or, without data, only
// its pin count. Lengths are entered in millimetres, as in datasheets.
class FootprintEditorDialog final : public QDialog {
    Q_OBJECT

public:
    explicit FootprintEditorDialog(std::optional<DeviceDefinition> device, QWidget* parent = nullptr);

    [[nodiscard]] FootprintDefinition footprint() const;
    void setParams(const FootprintParams& params);

private:
    [[nodiscard]] FootprintParams params() const;
    void refresh();
    QWidget* createInfoBox();

    std::optional<DeviceDefinition> device_;
    QString id_;
    QLineEdit* name_ = nullptr;
    QComboBox* style_ = nullptr;
    QSpinBox* padCount_ = nullptr;
    QDoubleSpinBox* pitch_ = nullptr;
    QDoubleSpinBox* rowSpacing_ = nullptr;
    QComboBox* shape_ = nullptr;
    QDoubleSpinBox* padWidth_ = nullptr;
    QDoubleSpinBox* padLength_ = nullptr;
    QCheckBox* throughHole_ = nullptr;
    QDoubleSpinBox* drill_ = nullptr;
    QDoubleSpinBox* bodyWidth_ = nullptr;
    QDoubleSpinBox* bodyLength_ = nullptr;
    QLabel* validation_ = nullptr;
    QLabel* summary_ = nullptr;
    FootprintPreview* preview_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

// Creates a schematic device with a pin count, optional datasheet data and a footprint, which can be
// an existing one with the same pad count or one created from the device.
class DeviceEditorDialog final : public QDialog {
    Q_OBJECT

public:
    explicit DeviceEditorDialog(const ProjectLibrary& library, QWidget* parent = nullptr);

    [[nodiscard]] DeviceDefinition device() const;
    // Footprints created from this dialog; they are already registered with registerSymbols.
    [[nodiscard]] QVector<FootprintDefinition> createdFootprints() const { return created_; }

private:
    void refreshFootprints();
    void refreshPinMap();
    [[nodiscard]] QVector<int> pinPadMap() const;
    void validate();
    void createFootprint();

    ProjectLibrary library_;
    QString id_;
    QVector<FootprintDefinition> created_;
    QLineEdit* name_ = nullptr;
    QLineEdit* prefix_ = nullptr;
    QLineEdit* value_ = nullptr;
    QSpinBox* pinCount_ = nullptr;
    QLineEdit* pinNames_ = nullptr;
    QLineEdit* manufacturer_ = nullptr;
    QLineEdit* partNumber_ = nullptr;
    QLineEdit* datasheet_ = nullptr;
    QComboBox* package_ = nullptr;
    QCheckBox* throughHole_ = nullptr;
    QDoubleSpinBox* pitch_ = nullptr;
    QDoubleSpinBox* rowSpacing_ = nullptr;
    QDoubleSpinBox* bodyWidth_ = nullptr;
    QDoubleSpinBox* bodyLength_ = nullptr;
    QDoubleSpinBox* leadWidth_ = nullptr;
    QDoubleSpinBox* leadLength_ = nullptr;
    QDoubleSpinBox* pinCurrent_ = nullptr;
    QComboBox* footprint_ = nullptr;
    // `DevicePinMap`: one row per pin (number, name, pad spin box), shown with a footprint.
    QTableWidget* pinMap_ = nullptr;
    QLabel* validation_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

} // namespace hatt::ui
