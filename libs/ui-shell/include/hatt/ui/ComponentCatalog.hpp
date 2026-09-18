#pragma once

#include "hatt/ui/LibraryModel.hpp"
#include "hatt/ui/SketchModel.hpp"

#include <QMap>

namespace hatt::ui {

// CatalogCategory, PinElectricalType, AnalysisSupport and SimulationModelDefinition live in
// LibraryModel.hpp (moved there in #61 PR (b) when ComponentCatalogEntry folded into
// LibraryDevice); still visible here as hatt::ui::* through the include above.

// The built-in, pickable schematic components: every LibraryDevice with
// `category == SymbolCategory::Component`, read straight out of builtInLibrary() so this, the
// symbol/footprint registry and the device picker all read one model (#61 PR (b) closes the old
// split between symbolLibrary()'s literal devices and ComponentCatalog's generated
// ComponentCatalogEntry list).
[[nodiscard]] const QVector<LibraryDevice>& componentCatalog();
[[nodiscard]] const QVector<SimulationModelDefinition>& simulationModelCatalog();
// Resolves legacy catalog.device.* ids via the alias table before searching, same as findSymbol().
[[nodiscard]] const LibraryDevice* findCatalogComponent(const QString& id);
[[nodiscard]] const SimulationModelDefinition* findSimulationModel(const QString& id);
[[nodiscard]] QList<const LibraryDevice*> searchComponentCatalog(const QString& query,
                                                                   CatalogCategory* category = nullptr);
[[nodiscard]] QString catalogCategoryName(CatalogCategory category);
[[nodiscard]] QString pinElectricalTypeName(PinElectricalType type);
// LibraryDevice::description translated at the point of use (like displayNameKey); empty in -> empty out.
[[nodiscard]] QString catalogDescription(const QString& description);

// A conservative creator suggestion: only unambiguous source/passive choices are inferred.
[[nodiscard]] QString suggestedSimulationModel(const QString& typeToken, int pinCount);

} // namespace hatt::ui
