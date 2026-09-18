#include "hatt/ui/ComponentCatalog.hpp"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>

namespace hatt::ui {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("hatt::ui", text); }

// componentCatalog()'s description strings live in builtin.json and are translated at the point
// of use (catalogDescription()), not wrapped in tr() here; lupdate needs a separate source
// inventory to discover them (matching LibraryModel.cpp's BuiltInLibraryTranslationSources).
[[maybe_unused]] const char* CatalogDescriptionTranslationSources[] = {
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Battery represented by an ideal DC source"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Common real component"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Full-wave diode bridge"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "General inductor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "General non-polarized capacitor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "General resistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "General silicon diode"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "General-purpose 555 timer"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Generic SPDT relay"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Generic digital logic function"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Generic pin header"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Ideal voltage operational amplifier"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Independent DC current source"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Independent DC voltage source"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Independent pulse source (transient required)"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Independent sine source (transient required)"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Light-dependent resistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Light-emitting diode"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Light-sensitive diode"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Low-forward-voltage Schottky diode"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Momentary normally-open push button"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Negative-temperature-coefficient thermistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Polarized electrolytic capacitor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Positive-temperature-coefficient thermistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Quartz crystal"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Replaceable over-current fuse"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Screw terminal block"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Single-pole switch"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Three-terminal adjustable regulator"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Three-terminal fixed regulator"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Three-terminal transistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Three-terminal variable resistor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Two-terminal DC motor"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Two-terminal buzzer"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Two-winding transformer"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Voltage comparator"),
    QT_TRANSLATE_NOOP("hatt::ui::SymbolLibrary", "Voltage-reference Zener diode"),
};

QString normalized(QString text) {
    text = text.normalized(QString::NormalizationForm_D).toCaseFolded();
    text.remove(QRegularExpression(QStringLiteral("[\\p{Mn}\\s_\\-/]+")));
    return text;
}

QString translated(const QString& sourceText) {
    if (sourceText.isEmpty()) return {};
    return QCoreApplication::translate("hatt::ui::SymbolLibrary", sourceText.toUtf8().constData());
}

} // namespace

const QVector<LibraryDevice>& componentCatalog() {
    static const QVector<LibraryDevice> entries = [] {
        QVector<LibraryDevice> result;
        for (const auto& device : builtInLibraryData().devices) {
            if (device.category == SymbolCategory::Component) result.append(device);
        }
        return result;
    }();
    return entries;
}

const QVector<SimulationModelDefinition>& simulationModelCatalog() {
    static const QVector<SimulationModelDefinition> entries = {
        {QStringLiteral("none"), tr("No simulation model"), AnalysisSupport::None, {}, tr("DC simulation does not support this component because it has no model.")},
        {QStringLiteral("dc.resistor"), tr("Linear resistor"), AnalysisSupport::DcOperatingPoint, {}, {}},
        {QStringLiteral("dc.capacitor"), tr("Capacitor (open at DC)"), AnalysisSupport::DcOperatingPoint, {}, {}},
        {QStringLiteral("dc.inductor"), tr("Inductor (short at DC)"), AnalysisSupport::DcOperatingPoint, {}, {}},
        {QStringLiteral("dc.voltage-source"), tr("Independent DC voltage source"), AnalysisSupport::DcOperatingPoint, {}, {}},
        {QStringLiteral("dc.current-source"), tr("Independent DC current source"), AnalysisSupport::DcOperatingPoint, {}, {}},
        {QStringLiteral("dc.switch-open"), tr("Open switch (DC)"), AnalysisSupport::DcOperatingPoint, {{QStringLiteral("resistance"), 1e12}}, {}},
        {QStringLiteral("dc.switch-closed"), tr("Closed switch (DC)"), AnalysisSupport::DcOperatingPoint, {{QStringLiteral("resistance"), 1e-3}}, {}},
        {QStringLiteral("nonlinear.diode"), tr("Diode"), AnalysisSupport::None, {{QStringLiteral("is"), 1e-12}, {QStringLiteral("n"), 1.0}}, tr("Nonlinear DC analysis is not implemented yet.")},
        {QStringLiteral("nonlinear.zener"), tr("Zener diode"), AnalysisSupport::None, {{QStringLiteral("breakdown"), 5.1}}, tr("Nonlinear DC analysis is not implemented yet.")},
        {QStringLiteral("nonlinear.led"), tr("LED"), AnalysisSupport::None, {{QStringLiteral("is"), 1e-18}, {QStringLiteral("n"), 2.0}}, tr("Nonlinear DC analysis is not implemented yet.")},
        {QStringLiteral("nonlinear.photodiode"), tr("Photodiode"), AnalysisSupport::None, {}, tr("Nonlinear DC analysis is not implemented yet.")},
        {QStringLiteral("nonlinear.bjt-npn"), tr("NPN BJT"), AnalysisSupport::None, {{QStringLiteral("beta"), 100.0}}, tr("BJT operating-point analysis is not implemented yet.")},
        {QStringLiteral("nonlinear.bjt-pnp"), tr("PNP BJT"), AnalysisSupport::None, {{QStringLiteral("beta"), 100.0}}, tr("BJT operating-point analysis is not implemented yet.")},
        {QStringLiteral("nonlinear.nmos"), tr("N-channel MOSFET"), AnalysisSupport::None, {{QStringLiteral("vto"), 2.0}}, tr("MOSFET operating-point analysis is not implemented yet.")},
        {QStringLiteral("nonlinear.pmos"), tr("P-channel MOSFET"), AnalysisSupport::None, {{QStringLiteral("vto"), -2.0}}, tr("MOSFET operating-point analysis is not implemented yet.")},
        {QStringLiteral("nonlinear.jfet"), tr("JFET"), AnalysisSupport::None, {}, tr("JFET operating-point analysis is not implemented yet.")},
        {QStringLiteral("nonlinear.opamp-ideal"), tr("Ideal op-amp"), AnalysisSupport::None, {}, tr("Controlled sources are not implemented yet.")},
        {QStringLiteral("nonlinear.opamp"), tr("General op-amp"), AnalysisSupport::None, {}, tr("Macromodel simulation is not implemented yet.")},
        {QStringLiteral("nonlinear.comparator"), tr("Comparator"), AnalysisSupport::None, {}, tr("Comparator simulation is not implemented yet.")},
        {QStringLiteral("transient.sine-source"), tr("Sine source"), AnalysisSupport::None, {}, tr("Transient analysis is not implemented yet.")},
        {QStringLiteral("transient.pulse-source"), tr("Pulse source"), AnalysisSupport::None, {}, tr("Transient analysis is not implemented yet.")},
    };
    return entries;
}

const LibraryDevice* findCatalogComponent(const QString& id) {
    QString resolved = id;
    const auto alias = builtInLibrary().aliases.constFind(id);
    if (alias != builtInLibrary().aliases.constEnd()) resolved = alias.value();
    const auto& entries = componentCatalog();
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.id == resolved; });
    return it == entries.end() ? nullptr : &*it;
}

const SimulationModelDefinition* findSimulationModel(const QString& id) {
    const auto& entries = simulationModelCatalog();
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.id == id; });
    return it == entries.end() ? nullptr : &*it;
}

QList<const LibraryDevice*> searchComponentCatalog(const QString& query, CatalogCategory* category) {
    QList<const LibraryDevice*> result;
    const QString needle = normalized(query);
    for (const auto& entry : componentCatalog()) {
        if (category != nullptr && entry.catalogCategory != *category) continue;
        QStringList haystack{translated(entry.displayNameKey), entry.id, entry.defaultValue,
                             entry.manufacturer, entry.partNumber, translated(entry.description)};
        haystack += entry.keywords;
        for (const auto& pin : entry.pins) haystack << pin.name;
        if (needle.isEmpty() || std::any_of(haystack.begin(), haystack.end(), [&](const QString& text) {
                return normalized(text).contains(needle);
            })) result.append(&entry);
    }
    return result;
}

QString catalogCategoryName(CatalogCategory category) {
    switch (category) {
    case CatalogCategory::Passive: return tr("Passives");
    case CatalogCategory::Diode: return tr("Diodes");
    case CatalogCategory::Transistor: return tr("Transistors");
    case CatalogCategory::Analog: return tr("Analog");
    case CatalogCategory::Digital: return tr("Digital");
    case CatalogCategory::Source: return tr("Sources and simulation");
    case CatalogCategory::Electromechanical: return tr("Electromechanical");
    case CatalogCategory::Connector: return tr("Connectors");
    }
    return {};
}

QString pinElectricalTypeName(PinElectricalType type) {
    switch (type) {
    case PinElectricalType::Passive: return tr("Passive");
    case PinElectricalType::Input: return tr("Input");
    case PinElectricalType::Output: return tr("Output");
    case PinElectricalType::PowerInput: return tr("Power input");
    case PinElectricalType::PowerOutput: return tr("Power output");
    case PinElectricalType::OpenCollector: return tr("Open collector");
    case PinElectricalType::NoConnect: return tr("Not connected");
    }
    return {};
}

QString catalogDescription(const QString& description) { return translated(description); }

QString suggestedSimulationModel(const QString& typeToken, int pinCount) {
    const QString token = normalized(typeToken);
    if (pinCount == 2 && token.contains(QStringLiteral("resistor"))) return QStringLiteral("dc.resistor");
    if (pinCount == 2 && token.contains(QStringLiteral("direnc"))) return QStringLiteral("dc.resistor");
    if (pinCount == 2 && (token.contains(QStringLiteral("capacitor")) || token.contains(QStringLiteral("kondansator"))))
        return QStringLiteral("dc.capacitor");
    if (pinCount == 2 && (token.contains(QStringLiteral("inductor")) || token.contains(QStringLiteral("bobin"))))
        return QStringLiteral("dc.inductor");
    if (pinCount == 2 && token.contains(QStringLiteral("voltage"))) return QStringLiteral("dc.voltage-source");
    if (pinCount == 2 && token.contains(QStringLiteral("current"))) return QStringLiteral("dc.current-source");
    return QStringLiteral("none");
}

} // namespace hatt::ui
