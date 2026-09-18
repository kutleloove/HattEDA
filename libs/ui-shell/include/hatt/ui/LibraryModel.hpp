#pragma once

#include "hatt/ui/SketchModel.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace hatt::ui {

// Library data model v2 (ADR-0017, issue #61): a data-driven built-in library that replaces the
// C++-literal symbolLibrary(). A LibraryDevice is a schematic part with one or more visual
// LibrarySymbolVariants (same pins, different shapes/layout) and one or more LibraryFootprint
// options. This is the *authoring/storage* format; at load time it is converted into the existing
// SymbolDefinition and fed to the unchanged registerSymbols()/findSymbol() runtime registry, so
// DesignCanvas, connectivity and export code do not change.

// Stable, UI-independent catalog vocabulary (moved from ComponentCatalog.hpp in #61 PR (b), when
// ComponentCatalogEntry folded into LibraryDevice). Display strings are translated by the
// accessors in ComponentCatalog.hpp, which still owns catalogCategoryName()/pinElectricalTypeName().
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

// Id namespace unchanged by #61 (shared with #62's nonlinear solver work).
struct SimulationModelDefinition {
    QString id;                 // stable token, e.g. "dc.resistor"
    QString name;               // translated display name
    AnalysisSupport support = AnalysisSupport::None;
    QMap<QString, double> parameters; // editable SI defaults for later property editors
    QString limitation;         // translated, empty only when the declared support really works
};

struct LibraryPin {
    QString name;
    int number = 1; // 1-based, stable across every variant of the owning device
    // Catalog metadata (#61 PR (b)); replaces ComponentCatalogEntry::pinTypes, which was a
    // separate QVector<PinElectricalType> parallel to pins and could desync from them.
    PinElectricalType type = PinElectricalType::Passive;

    friend bool operator==(const LibraryPin&, const LibraryPin&) = default;
};

// One visual form of a device's schematic symbol. `pinPositions` is parallel to the device's
// `pins` (same order/count). `animationToken` is a hook for #64 (simulation-driven animation,
// e.g. "led-glow"); empty means static. No animation behavior exists yet, only the field.
struct LibrarySymbolVariant {
    QString id = QStringLiteral("standard");
    QVector<SymbolShape> shapes;
    QVector<QPointF> pinPositions;
    QString animationToken;

    friend bool operator==(const LibrarySymbolVariant&, const LibrarySymbolVariant&) = default;
};

// A footprint option offered to a device. `pinPadMap` is empty (pin N -> pad N) or has one entry
// per device pin, a permutation of the footprint's pad numbers.
struct LibraryFootprintOption {
    QString footprintId;
    QVector<int> pinPadMap;

    friend bool operator==(const LibraryFootprintOption&, const LibraryFootprintOption&) = default;
};

struct LibraryDevice {
    QString id; // lib.<family>.<part>
    SymbolCategory category = SymbolCategory::Component;
    QString prefix;
    QString defaultValue;
    // Empty = explicitly "cannot simulate" (reported at simulation start). Ids come from
    // ComponentCatalog's simulationModelCatalog(); this field only ever stores/passes one through.
    QString simulationModel;
    QVector<LibraryPin> pins;
    QVector<LibrarySymbolVariant> variants; // never empty; variants.first() is the default
    QVector<LibraryFootprintOption> footprints; // schematic-only devices (terminals/probes): empty
    QString displayNameKey; // English source text, translated via context "hatt::ui::SymbolLibrary"

    // Catalog metadata (#61 PR (b), folded in from ComponentCatalogEntry so componentCatalog(),
    // searchComponentCatalog() and the device picker read this one model). Only meaningful for
    // `category == SymbolCategory::Component`; terminals/probes leave these at their defaults.
    // `footprintOptions` and `symbolTemplate` from ComponentCatalogEntry were dropped: the former
    // duplicated `footprints[i].footprintId`, the latter was only ever a build-time geometry
    // source, baked into `variants` once at authoring time.
    CatalogCategory catalogCategory = CatalogCategory::Passive;
    QString description;
    QStringList keywords; // English and Turkish aliases, normalized by search
    QString manufacturer;
    QString partNumber;

    [[nodiscard]] const LibrarySymbolVariant* variant(const QString& id) const;
};

struct LibraryFootprint {
    QString id; // lib.footprint.<package>
    QString displayNameKey;
    // Usually Component; Terminal for board items placed like a terminal (via, test point,
    // mounting hole) so they still list in Kayra's Terminal mode, matching symbolsFor().
    SymbolCategory category = SymbolCategory::Component;
    QString prefix;
    QVector<SymbolShape> shapes;
    QVector<QPointF> pins;
    QVector<PadDefinition> pads; // pads[i] at pins[i]
    QString source; // the standard/datasheet the dimensions came from; empty until #63
};

// One legacy id (schematic.*, board.*, catalog.device.*, catalog.footprint.*) mapped to its
// replacement. Read-direction only: newly-saved projects always write `newId`.
struct LibraryAlias {
    QString oldId;
    QString newId;
};

struct LibraryData {
    QVector<LibraryDevice> devices;
    QVector<LibraryFootprint> footprints;
    QVector<LibraryAlias> aliases;
};

[[nodiscard]] QJsonObject libraryDataToJson(const LibraryData& data);
// Empty QStringList `errors` on success.
[[nodiscard]] LibraryData libraryDataFromJson(const QJsonObject& root, QStringList& errors);

// Converts one device + one of its variants into the existing SymbolDefinition the rendering,
// connectivity and export code already reads. `workspace` is Schematic; board footprints convert
// with footprintToSymbolDefinition instead.
[[nodiscard]] SymbolDefinition deviceVariantToSymbolDefinition(const LibraryDevice& device,
                                                                const LibrarySymbolVariant& variant);
[[nodiscard]] SymbolDefinition footprintToSymbolDefinition(const LibraryFootprint& footprint);

// Loads the embedded built-in library resource (:/library/builtin.json) once, converts every
// device's default variant and every footprint into SymbolDefinition, and returns them together
// with the alias table. Logs to qWarning and returns what parsed on a malformed resource (should
// not happen for the shipped file; guards against a corrupted build).
struct BuiltInLibrary {
    QVector<SymbolDefinition> symbols;
    QHash<QString, QString> aliases; // oldId -> newId, every legacy prefix
};
[[nodiscard]] const BuiltInLibrary& builtInLibrary();

// The same embedded resource, parsed but not converted to SymbolDefinition: ComponentCatalog.cpp
// reads this for LibraryDevice's catalog metadata (catalogCategory, description, keywords, ...),
// which builtInLibrary()'s SymbolDefinition list does not carry. Loaded/cached once, same file.
[[nodiscard]] const LibraryData& builtInLibraryData();

} // namespace hatt::ui
