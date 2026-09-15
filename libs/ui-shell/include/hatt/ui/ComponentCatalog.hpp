#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QMap>

namespace hatt::ui {

// Stable, UI-independent catalog vocabulary. Display strings are translated by the accessors.
enum class CatalogCategory {
    Passive,
    Diode,
    Transistor,
    Analog,
    Digital,
    Source,
    Electromechanical,
    Connector,
};

enum class PinElectricalType { Passive, Input, Output, PowerInput, PowerOutput, OpenCollector, NoConnect };

enum class AnalysisSupport { None, DcOperatingPoint };

struct SimulationModelDefinition {
    QString id;                 // stable token, e.g. "dc.resistor"
    QString name;               // translated display name
    AnalysisSupport support = AnalysisSupport::None;
    QMap<QString, double> parameters; // editable SI defaults for later property editors
    QString limitation;         // translated, empty only when the declared support really works
};

struct ComponentCatalogEntry {
    DeviceDefinition device;
    CatalogCategory category = CatalogCategory::Passive;
    QString description;
    QStringList keywords;       // English and Turkish aliases, normalized by search
    QVector<PinElectricalType> pinTypes;
    QStringList footprintOptions;
    QString symbolTemplate;     // optional existing symbol whose geometry is reused
};

// Generated once from compact family tables. Definitions are built-in and are never copied into
// ProjectLibrary::customDevices/customFootprints; projects store only picked ids.
[[nodiscard]] const QVector<ComponentCatalogEntry>& componentCatalog();
[[nodiscard]] const QVector<FootprintDefinition>& footprintCatalog();
[[nodiscard]] const QVector<SimulationModelDefinition>& simulationModelCatalog();
[[nodiscard]] const ComponentCatalogEntry* findCatalogComponent(const QString& id);
[[nodiscard]] const SimulationModelDefinition* findSimulationModel(const QString& id);
[[nodiscard]] QList<const ComponentCatalogEntry*> searchComponentCatalog(const QString& query,
                                                                          CatalogCategory* category = nullptr);
[[nodiscard]] QString catalogCategoryName(CatalogCategory category);
[[nodiscard]] QString pinElectricalTypeName(PinElectricalType type);

// Makes generated catalog symbols and footprints visible through the existing symbol registry.
// Safe to call repeatedly.
void registerBuiltInCatalog();

// A conservative creator suggestion: only unambiguous source/passive choices are inferred.
[[nodiscard]] QString suggestedSimulationModel(const QString& typeToken, int pinCount);

} // namespace hatt::ui
