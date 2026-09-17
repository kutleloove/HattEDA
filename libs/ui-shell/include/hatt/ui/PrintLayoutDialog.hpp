#pragma once

#include "hatt/ui/PrintLayout.hpp"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace hatt::ui {

class PrintPagePreview;

// Proteus ARES style "Print Layout" dialog (objectName "PrintLayoutDialog"). Controls:
// "PrintLayer.<CamLayerKind>" check boxes, "PrintDrills", "PrintGrouping", "PrintPaper",
// "PrintPaperWidth", "PrintPaperHeight", "PrintLandscape", "PrintMargin", "PrintSpacing",
// "PrintScale", "PrintCompensationX", "PrintCompensationY", "PrintColours", "PrintMirror",
// "PrintRotate", "PrintColumns", "PrintRows", "PrintFit" (as many copies as fit), "PrintSummary",
// "PrintPreview", and buttons "PrintSavePdf" and "PrintToPrinter". Settings are remembered as the
// application preference `print/*`.
class PrintLayoutDialog final : public QDialog {
    Q_OBJECT

public:
    PrintLayoutDialog(const CamOutput& output, QString documentName, QWidget* parent = nullptr);

    [[nodiscard]] PrintSettings settings() const;
    void setSettings(const PrintSettings& settings);
    // Fills the page with as many copies as fit, in whichever orientation holds more.
    void fitToPage();
    // Writes the page as a vector PDF; returns a translated error or an empty string.
    [[nodiscard]] QString savePdf(const QString& path) const;

private:
    void refresh();
    void storeSettings() const;
    void choosePdf();
    void printPage();

    CamOutput output_;
    QString documentName_;
    bool updating_ = false;
    QList<QCheckBox*> layerBoxes_;
    QCheckBox* drills_ = nullptr;
    QComboBox* grouping_ = nullptr;
    QComboBox* paper_ = nullptr;
    QDoubleSpinBox* paperWidth_ = nullptr;
    QDoubleSpinBox* paperHeight_ = nullptr;
    QCheckBox* landscape_ = nullptr;
    QDoubleSpinBox* margin_ = nullptr;
    QDoubleSpinBox* spacing_ = nullptr;
    QComboBox* scale_ = nullptr;
    QDoubleSpinBox* compensationX_ = nullptr;
    QDoubleSpinBox* compensationY_ = nullptr;
    QComboBox* colours_ = nullptr;
    QCheckBox* mirror_ = nullptr;
    QCheckBox* rotate_ = nullptr;
    QSpinBox* columns_ = nullptr;
    QSpinBox* rows_ = nullptr;
    QLabel* summary_ = nullptr;
    PrintPagePreview* preview_ = nullptr;
};

} // namespace hatt::ui
