#include "hatt/ui/ComponentCatalog.hpp"

#include "hatt/ui/ComponentLibrary.hpp"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace hatt::ui {
namespace {

QString tr(const char* text) { return QCoreApplication::translate("hatt::ui", text); }

// device()/two() keep the catalog compact, so their string arguments are not visible to lupdate.
// This inventory makes every user-facing catalog source discoverable without duplicating data at
// runtime.
[[maybe_unused]] const char* CatalogTranslationSources[] = {
    QT_TRANSLATE_NOOP("hatt::ui", "Resistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "General resistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "Potentiometer"),
    QT_TRANSLATE_NOOP("hatt::ui", "Three-terminal variable resistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "LDR / photoresistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "Light-dependent resistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "NTC thermistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "Negative-temperature-coefficient thermistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "PTC thermistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "Positive-temperature-coefficient thermistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "Capacitor, non-polarized"),
    QT_TRANSLATE_NOOP("hatt::ui", "General non-polarized capacitor"),
    QT_TRANSLATE_NOOP("hatt::ui", "Capacitor, polarized"),
    QT_TRANSLATE_NOOP("hatt::ui", "Polarized electrolytic capacitor"),
    QT_TRANSLATE_NOOP("hatt::ui", "Inductor"),
    QT_TRANSLATE_NOOP("hatt::ui", "General inductor"),
    QT_TRANSLATE_NOOP("hatt::ui", "Transformer"),
    QT_TRANSLATE_NOOP("hatt::ui", "Two-winding transformer"),
    QT_TRANSLATE_NOOP("hatt::ui", "Standard diode"),
    QT_TRANSLATE_NOOP("hatt::ui", "General silicon diode"),
    QT_TRANSLATE_NOOP("hatt::ui", "Schottky diode"),
    QT_TRANSLATE_NOOP("hatt::ui", "Low-forward-voltage Schottky diode"),
    QT_TRANSLATE_NOOP("hatt::ui", "Zener diode"),
    QT_TRANSLATE_NOOP("hatt::ui", "Voltage-reference Zener diode"),
    QT_TRANSLATE_NOOP("hatt::ui", "LED"),
    QT_TRANSLATE_NOOP("hatt::ui", "Light-emitting diode"),
    QT_TRANSLATE_NOOP("hatt::ui", "Photodiode"),
    QT_TRANSLATE_NOOP("hatt::ui", "Light-sensitive diode"),
    QT_TRANSLATE_NOOP("hatt::ui", "Bridge rectifier"),
    QT_TRANSLATE_NOOP("hatt::ui", "Full-wave diode bridge"),
    QT_TRANSLATE_NOOP("hatt::ui", "NPN transistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "PNP transistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "N-channel MOSFET"),
    QT_TRANSLATE_NOOP("hatt::ui", "P-channel MOSFET"),
    QT_TRANSLATE_NOOP("hatt::ui", "N-channel JFET"),
    QT_TRANSLATE_NOOP("hatt::ui", "Three-terminal transistor"),
    QT_TRANSLATE_NOOP("hatt::ui", "Ideal operational amplifier"),
    QT_TRANSLATE_NOOP("hatt::ui", "Ideal voltage operational amplifier"),
    QT_TRANSLATE_NOOP("hatt::ui", "Comparator"),
    QT_TRANSLATE_NOOP("hatt::ui", "Voltage comparator"),
    QT_TRANSLATE_NOOP("hatt::ui", "555 timer"),
    QT_TRANSLATE_NOOP("hatt::ui", "General-purpose 555 timer"),
    QT_TRANSLATE_NOOP("hatt::ui", "Fixed linear regulator"),
    QT_TRANSLATE_NOOP("hatt::ui", "Three-terminal fixed regulator"),
    QT_TRANSLATE_NOOP("hatt::ui", "Adjustable linear regulator"),
    QT_TRANSLATE_NOOP("hatt::ui", "Three-terminal adjustable regulator"),
    QT_TRANSLATE_NOOP("hatt::ui", "AND gate"), QT_TRANSLATE_NOOP("hatt::ui", "OR gate"),
    QT_TRANSLATE_NOOP("hatt::ui", "NOT gate"), QT_TRANSLATE_NOOP("hatt::ui", "NAND gate"),
    QT_TRANSLATE_NOOP("hatt::ui", "NOR gate"), QT_TRANSLATE_NOOP("hatt::ui", "XOR gate"),
    QT_TRANSLATE_NOOP("hatt::ui", "Buffer"), QT_TRANSLATE_NOOP("hatt::ui", "Tri-state buffer"),
    QT_TRANSLATE_NOOP("hatt::ui", "D flip-flop"), QT_TRANSLATE_NOOP("hatt::ui", "Digital clock"),
    QT_TRANSLATE_NOOP("hatt::ui", "Generic digital logic function"),
    QT_TRANSLATE_NOOP("hatt::ui", "DC voltage source"),
    QT_TRANSLATE_NOOP("hatt::ui", "Independent DC voltage source"),
    QT_TRANSLATE_NOOP("hatt::ui", "DC current source"),
    QT_TRANSLATE_NOOP("hatt::ui", "Independent DC current source"),
    QT_TRANSLATE_NOOP("hatt::ui", "Sine voltage source"),
    QT_TRANSLATE_NOOP("hatt::ui", "Independent sine source (transient required)"),
    QT_TRANSLATE_NOOP("hatt::ui", "Pulse / clock source"),
    QT_TRANSLATE_NOOP("hatt::ui", "Independent pulse source (transient required)"),
    QT_TRANSLATE_NOOP("hatt::ui", "Push button"),
    QT_TRANSLATE_NOOP("hatt::ui", "Momentary normally-open push button"),
    QT_TRANSLATE_NOOP("hatt::ui", "SPST switch"), QT_TRANSLATE_NOOP("hatt::ui", "Single-pole switch"),
    QT_TRANSLATE_NOOP("hatt::ui", "Relay"), QT_TRANSLATE_NOOP("hatt::ui", "Generic SPDT relay"),
    QT_TRANSLATE_NOOP("hatt::ui", "Fuse"), QT_TRANSLATE_NOOP("hatt::ui", "Replaceable over-current fuse"),
    QT_TRANSLATE_NOOP("hatt::ui", "Crystal"), QT_TRANSLATE_NOOP("hatt::ui", "Quartz crystal"),
    QT_TRANSLATE_NOOP("hatt::ui", "Buzzer"), QT_TRANSLATE_NOOP("hatt::ui", "Two-terminal buzzer"),
    QT_TRANSLATE_NOOP("hatt::ui", "DC motor"), QT_TRANSLATE_NOOP("hatt::ui", "Two-terminal DC motor"),
    QT_TRANSLATE_NOOP("hatt::ui", "Battery"),
    QT_TRANSLATE_NOOP("hatt::ui", "Battery represented by an ideal DC source"),
    QT_TRANSLATE_NOOP("hatt::ui", "Pin header"), QT_TRANSLATE_NOOP("hatt::ui", "Generic pin header"),
    QT_TRANSLATE_NOOP("hatt::ui", "Terminal block"), QT_TRANSLATE_NOOP("hatt::ui", "Screw terminal block"),
    QT_TRANSLATE_NOOP("hatt::ui", "Common real component"),
};

FootprintDefinition footprint(QString id, QString name, PackageStyle style, int pads, double pitch,
                              double rows, double padWidth, double padLength, double drill,
                              double bodyWidth, double bodyLength, PadShape shape = PadShape::Rect) {
    FootprintDefinition result;
    result.id = QStringLiteral("catalog.footprint.") + id;
    result.name = std::move(name);
    result.params = {style, pads, pitch, rows, shape, padWidth, padLength, drill, bodyWidth, bodyLength};
    return result;
}

QVector<FootprintDefinition> buildFootprints() {
    QVector<FootprintDefinition> out;
    auto smd2 = [&](const char* code, double spacing, double pw, double pl, double bw, double bl) {
        out << footprint(QString::fromLatin1("passive.") + QLatin1String(code),
                         tr("SMD passive %1").arg(QLatin1String(code)), PackageStyle::TwoTerminal,
                         2, 0, spacing, pw, pl, 0, bw, bl);
    };
    smd2("0201", 0.50, 0.30, 0.30, 0.60, 0.30);
    smd2("0402", 0.90, 0.55, 0.60, 1.10, 0.60);
    smd2("0603", 1.60, 0.80, 0.95, 1.70, 0.90);
    smd2("0805", 1.90, 1.00, 1.30, 2.10, 1.30);
    smd2("1206", 3.20, 1.20, 1.60, 3.30, 1.70);
    smd2("1210", 3.20, 1.20, 2.80, 3.30, 2.60);

    auto smdDiode = [&](const char* id, const char* name, double rows, double pw, double pl,
                        double bw, double bl) {
        out << footprint(QLatin1String(id), tr(name), PackageStyle::TwoTerminal, 2, 0, rows, pw,
                         pl, 0, bw, bl);
    };
    smdDiode("sod123", "SOD-123", 3.70, 1.20, 1.20, 3.70, 1.80);
    smdDiode("sod323", "SOD-323", 2.30, 0.80, 0.80, 2.00, 1.25);
    smdDiode("sma", "SMA / DO-214AC", 4.50, 1.80, 2.30, 4.60, 2.80);
    smdDiode("smb", "SMB / DO-214AA", 5.30, 2.30, 2.60, 5.40, 3.60);
    out << footprint(QStringLiteral("do35"), tr("Axial DO-35, 10.16 mm"),
                     PackageStyle::TwoTerminal, 2, 0, 10.16, 1.70, 1.70, 0.80, 4.00, 2.00,
                     PadShape::Round);
    out << footprint(QStringLiteral("do41"), tr("Axial DO-41, 12.70 mm"),
                     PackageStyle::TwoTerminal, 2, 0, 12.70, 2.00, 2.00, 1.00, 5.20, 2.70,
                     PadShape::Round);

    auto dual = [&](const char* id, const char* name, int pins, double pitch, double rows,
                    double pw, double pl, double bw, double bl, bool th = false) {
        out << footprint(QLatin1String(id), tr(name), PackageStyle::DualRow, pins, pitch, rows, pw,
                         pl, th ? 0.8 : 0.0, bw, bl, th ? PadShape::Round : PadShape::Rect);
    };
    dual("sot23", "SOT-23", 3, 0.95, 2.20, 0.85, 0.75, 2.90, 1.30);
    // SOT-23 is physically two-plus-one; the existing parametric model uses a single row for odd
    // counts. Replace the above invalid dual-row record with a validated explicit symbol below.
    out.removeLast();
    out << footprint(QStringLiteral("sot23"), tr("SOT-23"), PackageStyle::SingleRow, 3, 0.95,
                     0, 0.85, 0.75, 0, 2.90, 1.30);
    dual("sot223", "SOT-223", 4, 2.30, 6.20, 2.00, 1.50, 6.50, 3.50);
    out << footprint(QStringLiteral("to92"), tr("TO-92 inline"), PackageStyle::SingleRow, 3,
                     2.54, 0, 1.70, 1.70, 0.80, 5.00, 4.00, PadShape::Round);
    out << footprint(QStringLiteral("to220"), tr("TO-220-3 vertical"), PackageStyle::SingleRow,
                     3, 2.54, 0, 2.00, 2.00, 1.10, 10.20, 4.60, PadShape::Round);
    out << footprint(QStringLiteral("dpak"), tr("TO-252 / DPAK"), PackageStyle::SingleRow, 3,
                     2.28, 0, 2.00, 1.30, 0, 6.50, 6.10);

    for (int pins : {6, 8, 14, 16, 20, 28, 40}) {
        dual(QString("dip%1").arg(pins).toLatin1().constData(), "DIP", pins, 2.54,
             pins >= 28 ? 15.24 : 7.62, 1.70, 1.70, 0, 0, true);
    }
    for (int pins : {8, 14, 16, 20, 28}) {
        out << footprint(QString("soic%1").arg(pins), tr("SOIC-%1").arg(pins),
                         PackageStyle::DualRow, pins, 1.27, 5.40, 1.55, 0.60, 0.0,
                         3.90, std::max(5.0, pins / 2.0 * 1.27));
    }
    for (int pins : {8, 14, 16, 20, 24, 28}) {
        out << footprint(QString("tssop%1").arg(pins), tr("TSSOP-%1").arg(pins),
                         PackageStyle::DualRow, pins, 0.65, 5.60, 1.30, 0.35, 0.0,
                         4.40, std::max(3.0, pins / 2.0 * 0.65));
        out << footprint(QString("ssop%1").arg(pins), tr("SSOP-%1").arg(pins),
                         PackageStyle::DualRow, pins, 0.65, 7.20, 1.70, 0.40, 0.0,
                         5.30, std::max(3.5, pins / 2.0 * 0.65));
    }
    for (int pins : {32, 44, 48, 64, 100}) {
        out << footprint(QString("tqfp%1").arg(pins), tr("TQFP-%1").arg(pins),
                         PackageStyle::QuadRow, pins, 0.50,
                         pins == 100 ? 15.0 : pins >= 64 ? 12.0 : 9.0, 1.40, 0.30,
                         0.0, pins >= 64 ? 10.0 : 7.0, pins >= 64 ? 10.0 : 7.0);
    }
    for (int pins : {16, 20, 24, 32, 48}) {
        out << footprint(QString("qfn%1").arg(pins), tr("QFN-%1 (perimeter pads)").arg(pins),
                         PackageStyle::QuadRow, pins, 0.50,
                         pins == 48 ? 7.0 : pins >= 32 ? 6.0 : 4.0, 0.80, 0.28,
                         0.0, pins >= 32 ? 5.0 : 3.0, pins >= 32 ? 5.0 : 3.0);
    }

    for (int pins = 1; pins <= 20; ++pins) {
        out << footprint(QString("header1x%1").arg(pins), tr("Pin header 1x%1, 2.54 mm").arg(pins),
                         PackageStyle::SingleRow, pins, 2.54, 0, 1.70, 1.70, 1.00,
                         2.54, pins * 2.54, PadShape::Round);
        if (pins >= 2 && pins % 2 == 0) {
            out << footprint(QString("header2x%1").arg(pins / 2),
                             tr("Pin header 2x%1, 2.54 mm").arg(pins / 2),
                             PackageStyle::DualRow, pins, 2.54, 2.54, 1.70, 1.70, 1.00,
                             5.08, pins / 2 * 2.54, PadShape::Round);
        }
    }
    for (int pins : {2, 3, 4}) {
        out << footprint(QString("terminal%1").arg(pins), tr("Terminal block %1 pin, 5.08 mm").arg(pins),
                         PackageStyle::SingleRow, pins, 5.08, 0, 2.80, 2.80, 1.30,
                         8.0, pins * 5.08, PadShape::Round);
    }
    for (double pitch : {2.0, 2.5, 5.0, 7.5}) {
        const QString token = QString::number(pitch, 'f', 1).replace(QLatin1Char('.'), QLatin1Char('_'));
        out << footprint(QStringLiteral("radial") + token, tr("Radial capacitor, %1 mm pitch").arg(pitch),
                         PackageStyle::TwoTerminal, 2, 0, pitch, 1.80, 1.80, 0.80,
                         pitch + 2.0, pitch + 2.0, PadShape::Round);
    }
    for (int diameter : {3, 5}) {
        out << footprint(QString("led%1mm").arg(diameter), tr("LED %1 mm, 2.54 mm pitch").arg(diameter),
                         PackageStyle::TwoTerminal, 2, 0, 2.54, 1.80, 1.80, 0.80,
                         diameter, diameter, PadShape::Round);
    }
    out << footprint(QStringLiteral("trimmer3"), tr("Trimmer potentiometer, 3 pin"),
                     PackageStyle::SingleRow, 3, 2.54, 0, 1.80, 1.80, 0.80, 7.0, 7.0,
                     PadShape::Round)
        << footprint(QStringLiteral("relay5"), tr("Relay, generic 5 pin"),
                     PackageStyle::SingleRow, 5, 5.08, 0, 2.00, 2.00, 1.00, 20.0, 10.0,
                     PadShape::Round)
        << footprint(QStringLiteral("bridge4"), tr("Bridge rectifier, 4 pin"),
                     PackageStyle::SingleRow, 4, 5.08, 0, 2.00, 2.00, 1.00, 15.0, 15.0,
                     PadShape::Round);
    return out;
}

ComponentCatalogEntry device(const char* id, const char* name, CatalogCategory category,
                             const char* prefix, const char* value, QStringList pins,
                             QVector<PinElectricalType> types, const char* footprintId,
                             const char* model, const char* description, QStringList keywords,
                             const char* symbolTemplate = "") {
    ComponentCatalogEntry result;
    result.device.id = QStringLiteral("catalog.device.") + QLatin1String(id);
    result.device.name = tr(name);
    result.device.prefix = QLatin1String(prefix);
    result.device.defaultValue = QLatin1String(value);
    result.device.pinCount = pins.size();
    result.device.pinNames = std::move(pins);
    result.device.footprint = QLatin1String(footprintId);
    result.device.simulationModel = QLatin1String(model);
    result.category = category;
    result.description = tr(description);
    result.keywords = std::move(keywords);
    result.pinTypes = std::move(types);
    result.footprintOptions << result.device.footprint;
    result.symbolTemplate = QLatin1String(symbolTemplate);
    return result;
}

QVector<ComponentCatalogEntry> buildComponents() {
    using C = CatalogCategory;
    using P = PinElectricalType;
    QVector<ComponentCatalogEntry> out;
    auto two = [&](const char* id, const char* name, C category, const char* prefix,
                   const char* value, const char* fp, const char* model, const char* description,
                   QStringList keys, const char* symbol = "") {
        out << device(id, name, category, prefix, value, {tr("1"), tr("2")},
                      {P::Passive, P::Passive}, fp, model, description, std::move(keys), symbol);
    };
    two("resistor", "Resistor", C::Passive, "R", "1k", "catalog.footprint.passive.0603",
        "dc.resistor", "General resistor", {"resistor", "direnç", "ohm"}, "schematic.resistor");
    out << device("potentiometer", "Potentiometer", C::Passive, "RV", "10k",
                  {tr("A"), tr("Wiper"), tr("B")}, {P::Passive, P::Passive, P::Passive},
                  "catalog.footprint.trimmer3", "none", "Three-terminal variable resistor",
                  {"potentiometer", "potansiyometre", "trimmer"});
    two("ldr", "LDR / photoresistor", C::Passive, "R", "10k", "catalog.footprint.radial5_0",
        "none", "Light-dependent resistor", {"ldr", "photoresistor", "ışık", "foto direnç"});
    two("ntc", "NTC thermistor", C::Passive, "TH", "10k", "catalog.footprint.radial5_0",
        "none", "Negative-temperature-coefficient thermistor", {"ntc", "thermistor", "termistör"});
    two("ptc", "PTC thermistor", C::Passive, "TH", "1k", "catalog.footprint.radial5_0",
        "none", "Positive-temperature-coefficient thermistor", {"ptc", "thermistor", "termistör"});
    two("capacitor", "Capacitor, non-polarized", C::Passive, "C", "100n",
        "catalog.footprint.passive.0805", "dc.capacitor", "General non-polarized capacitor",
        {"capacitor", "kondansatör", "kapasitör"}, "schematic.capacitor");
    two("capacitor-polarized", "Capacitor, polarized", C::Passive, "C", "10u",
        "catalog.footprint.radial2_5", "dc.capacitor", "Polarized electrolytic capacitor",
        {"electrolytic", "polarized", "kutuplu", "kondansatör"}, "schematic.capacitor");
    two("inductor", "Inductor", C::Passive, "L", "10u", "catalog.footprint.passive.0805",
        "dc.inductor", "General inductor", {"inductor", "bobin", "coil"}, "schematic.inductor");
    out << device("transformer", "Transformer", C::Passive, "T", "1:1",
                  {tr("P1"), tr("P2"), tr("S1"), tr("S2")},
                  {P::Passive, P::Passive, P::Passive, P::Passive},
                  "catalog.footprint.header1x4", "none", "Two-winding transformer",
                  {"transformer", "transformatör", "trafo"});

    two("diode", "Standard diode", C::Diode, "D", "1N4148", "catalog.footprint.do35",
        "nonlinear.diode", "General silicon diode", {"diode", "diyot"}, "schematic.diode");
    two("schottky", "Schottky diode", C::Diode, "D", "BAT54", "catalog.footprint.sod123",
        "nonlinear.diode", "Low-forward-voltage Schottky diode", {"schottky", "diyot"}, "schematic.diode");
    two("zener", "Zener diode", C::Diode, "D", "5V1", "catalog.footprint.do35",
        "nonlinear.zener", "Voltage-reference Zener diode", {"zener", "zenner", "diyot"}, "schematic.diode");
    two("led", "LED", C::Diode, "D", "red", "catalog.footprint.led5mm", "nonlinear.led",
        "Light-emitting diode", {"led", "ışık yayan diyot"}, "schematic.led");
    two("photodiode", "Photodiode", C::Diode, "D", "", "catalog.footprint.led3mm",
        "nonlinear.photodiode", "Light-sensitive diode", {"photodiode", "fotodiyot"}, "schematic.diode");
    out << device("bridge", "Bridge rectifier", C::Diode, "BR", "",
                  {tr("AC1"), tr("AC2"), tr("+"), tr("-")},
                  {P::Passive, P::Passive, P::Output, P::Output}, "catalog.footprint.bridge4",
                  "none", "Full-wave diode bridge", {"bridge", "rectifier", "köprü", "doğrultucu"});

    auto transistor = [&](const char* id, const char* name, const char* value, const char* model,
                          QStringList pins, const char* fp, QStringList keys, const char* templ) {
        out << device(id, name, C::Transistor, "Q", value, std::move(pins),
                      {P::Passive, P::Input, P::Passive}, fp, model, "Three-terminal transistor",
                      std::move(keys), templ);
    };
    transistor("npn", "NPN transistor", "BC547", "nonlinear.bjt-npn", {"C", "B", "E"},
               "catalog.footprint.to92", {"npn", "transistor", "transistör"}, "schematic.npn");
    transistor("pnp", "PNP transistor", "BC557", "nonlinear.bjt-pnp", {"C", "B", "E"},
               "catalog.footprint.to92", {"pnp", "transistor", "transistör"}, "schematic.npn");
    transistor("nmos", "N-channel MOSFET", "2N7000", "nonlinear.nmos", {"D", "G", "S"},
               "catalog.footprint.to92", {"nmos", "mosfet", "n kanal"}, "schematic.npn");
    transistor("pmos", "P-channel MOSFET", "", "nonlinear.pmos", {"D", "G", "S"},
               "catalog.footprint.sot23", {"pmos", "mosfet", "p kanal"}, "schematic.npn");
    transistor("jfet", "N-channel JFET", "J201", "nonlinear.jfet", {"D", "G", "S"},
               "catalog.footprint.to92", {"jfet", "fet"}, "schematic.npn");

    out << device("opamp-ideal", "Ideal operational amplifier", C::Analog, "U", "ideal",
                  {"IN+", "IN-", "OUT"}, {P::Input, P::Input, P::Output},
                  "catalog.footprint.sot23", "nonlinear.opamp-ideal", "Ideal voltage operational amplifier",
                  {"opamp", "op-amp", "işlemsel yükselteç"}, "schematic.opamp");
    out << device("comparator", "Comparator", C::Analog, "U", "",
                  {"IN+", "IN-", "OUT"}, {P::Input, P::Input, P::OpenCollector},
                  "catalog.footprint.sot23", "nonlinear.comparator", "Voltage comparator",
                  {"comparator", "karşılaştırıcı"}, "schematic.opamp");
    out << device("timer555", "555 timer", C::Analog, "U", "NE555",
                  {"GND", "TRIG", "OUT", "RESET", "CTRL", "THRESH", "DISCH", "VCC"},
                  {P::PowerInput, P::Input, P::Output, P::Input, P::Input, P::Input, P::OpenCollector, P::PowerInput},
                  "catalog.footprint.dip8", "none", "General-purpose 555 timer",
                  {"555", "timer", "zamanlayıcı", "ne555"});
    out << device("regulator-fixed", "Fixed linear regulator", C::Analog, "U", "5V",
                  {"IN", "GND", "OUT"}, {P::PowerInput, P::PowerInput, P::PowerOutput},
                  "catalog.footprint.to220", "none", "Three-terminal fixed regulator",
                  {"regulator", "regülatör", "7805"});
    out << device("regulator-adjustable", "Adjustable linear regulator", C::Analog, "U", "LM317",
                  {"ADJ", "OUT", "IN"}, {P::Input, P::PowerOutput, P::PowerInput},
                  "catalog.footprint.to220", "none", "Three-terminal adjustable regulator",
                  {"regulator", "regülatör", "lm317"});

    auto logic = [&](const char* id, const char* name, QStringList pins, QStringList keys) {
        QVector<P> types(pins.size(), P::Input); types.last() = P::Output;
        const QByteArray footprintId = QStringLiteral("catalog.footprint.header1x%1")
                                           .arg(pins.size())
                                           .toLatin1();
        out << device(id, name, C::Digital, "U", "", std::move(pins), std::move(types),
                      footprintId.constData(), "none", "Generic digital logic function",
                      std::move(keys));
    };
    logic("and", "AND gate", {"A", "B", "Y"}, {"and", "ve kapısı", "logic"});
    logic("or", "OR gate", {"A", "B", "Y"}, {"or", "veya kapısı", "logic"});
    logic("not", "NOT gate", {"A", "Y"}, {"not", "inverter", "değil kapısı"});
    logic("nand", "NAND gate", {"A", "B", "Y"}, {"nand", "ve değil"});
    logic("nor", "NOR gate", {"A", "B", "Y"}, {"nor", "veya değil"});
    logic("xor", "XOR gate", {"A", "B", "Y"}, {"xor", "özel veya"});
    logic("buffer", "Buffer", {"A", "Y"}, {"buffer", "tampon"});
    logic("tristate", "Tri-state buffer", {"A", "EN", "Y"}, {"tristate", "three state", "üç durum"});
    logic("dff", "D flip-flop", {"D", "CLK", "Q", "Q/"}, {"d flip-flop", "dff", "flip flop"});
    logic("clock", "Digital clock", {"OUT"}, {"clock", "saat", "pulse"});

    two("vdc", "DC voltage source", C::Source, "V", "5", "catalog.footprint.header1x2",
        "dc.voltage-source", "Independent DC voltage source", {"voltage", "gerilim", "dc source"}, "schematic.vdc");
    two("idc", "DC current source", C::Source, "I", "1m", "catalog.footprint.header1x2",
        "dc.current-source", "Independent DC current source", {"current", "akım", "dc source"});
    two("sine", "Sine voltage source", C::Source, "V", "1", "catalog.footprint.header1x2",
        "transient.sine-source", "Independent sine source (transient required)", {"sine", "sinüs", "ac source"});
    two("pulse", "Pulse / clock source", C::Source, "V", "5", "catalog.footprint.header1x2",
        "transient.pulse-source", "Independent pulse source (transient required)", {"pulse", "clock", "darbe", "saat"});

    two("pushbutton", "Push button", C::Electromechanical, "SW", "", "catalog.footprint.header1x2",
        "dc.switch-open", "Momentary normally-open push button", {"button", "buton", "pushbutton"});
    two("switch", "SPST switch", C::Electromechanical, "SW", "", "catalog.footprint.header1x2",
        "dc.switch-open", "Single-pole switch", {"switch", "anahtar", "spst"});
    out << device("relay", "Relay", C::Electromechanical, "K", "5V",
                  {"COIL+", "COIL-", "COM", "NO", "NC"},
                  {P::Passive, P::Passive, P::Passive, P::Passive, P::Passive},
                  "catalog.footprint.relay5", "none", "Generic SPDT relay", {"relay", "röle"});
    two("fuse", "Fuse", C::Electromechanical, "F", "1A", "catalog.footprint.do41",
        "none", "Replaceable over-current fuse", {"fuse", "sigorta"});
    two("crystal", "Crystal", C::Electromechanical, "Y", "16M", "catalog.footprint.header1x2",
        "none", "Quartz crystal", {"crystal", "kristal", "quartz"});
    two("buzzer", "Buzzer", C::Electromechanical, "BZ", "5V", "catalog.footprint.radial7_5",
        "none", "Two-terminal buzzer", {"buzzer", "zil"});
    two("motor", "DC motor", C::Electromechanical, "M", "6V", "catalog.footprint.terminal2",
        "none", "Two-terminal DC motor", {"motor", "dc motor"});
    two("battery", "Battery", C::Electromechanical, "BT", "9V", "catalog.footprint.header1x2",
        "dc.voltage-source", "Battery represented by an ideal DC source", {"battery", "batarya", "pil"});
    out << device("header", "Pin header", C::Connector, "J", "1x4",
                  {"1", "2", "3", "4"}, {P::Passive, P::Passive, P::Passive, P::Passive},
                  "catalog.footprint.header1x4", "none", "Generic pin header", {"header", "pin", "konnektör"});
    out << device("terminal-block", "Terminal block", C::Connector, "J", "3P",
                  {"1", "2", "3"}, {P::Passive, P::Passive, P::Passive},
                  "catalog.footprint.terminal3", "none", "Screw terminal block", {"terminal", "klemens", "connector"});

    auto real = [&](const char* id, const char* name, C category, const char* prefix, const char* value,
                    QStringList pins, QVector<P> types, const char* fp, const char* model,
                    QStringList keys, const char* templ = "") -> ComponentCatalogEntry& {
        out << device(id, name, category, prefix, value, std::move(pins), std::move(types), fp, model,
                      "Common real component", std::move(keys), templ);
        out.last().device.spec.manufacturer = QStringLiteral("Multi-source");
        out.last().device.spec.partNumber = QLatin1String(name);
        return out.last();
    };
    real("1n4148", "1N4148", C::Diode, "D", "1N4148", {"A", "K"}, {P::Passive, P::Passive},
         "catalog.footprint.do35", "nonlinear.diode", {"1n4148", "switching diode", "diyot"}, "schematic.diode");
    real("1n4007", "1N4007", C::Diode, "D", "1N4007", {"A", "K"}, {P::Passive, P::Passive},
         "catalog.footprint.do41", "nonlinear.diode", {"1n4007", "rectifier", "doğrultucu"}, "schematic.diode");
    real("bat54", "BAT54", C::Diode, "D", "BAT54", {"A", "NC", "K"},
         {P::Passive, P::NoConnect, P::Passive}, "catalog.footprint.sot23", "nonlinear.diode",
         {"bat54", "schottky"}, "schematic.npn");
    real("zener-5v1", "5V1 Zener", C::Diode, "D", "5V1", {"A", "K"}, {P::Passive, P::Passive},
         "catalog.footprint.do35", "nonlinear.zener", {"5v1", "zener", "zenner"}, "schematic.diode");
    real("bc547", "BC547", C::Transistor, "Q", "BC547", {"C", "B", "E"}, {P::Passive, P::Input, P::Passive},
         "catalog.footprint.to92", "nonlinear.bjt-npn", {"bc547", "npn"}, "schematic.npn");
    real("bc557", "BC557", C::Transistor, "Q", "BC557", {"C", "B", "E"}, {P::Passive, P::Input, P::Passive},
         "catalog.footprint.to92", "nonlinear.bjt-pnp", {"bc557", "pnp"}, "schematic.npn");
    real("2n2222", "2N2222 / PN2222A", C::Transistor, "Q", "2N2222", {"E", "B", "C"},
         {P::Passive, P::Input, P::Passive}, "catalog.footprint.to92", "nonlinear.bjt-npn", {"2n2222", "pn2222", "npn"}, "schematic.npn");
    real("2n7000", "2N7000", C::Transistor, "Q", "2N7000", {"S", "G", "D"},
         {P::Passive, P::Input, P::Passive}, "catalog.footprint.to92", "nonlinear.nmos", {"2n7000", "nmos"}, "schematic.npn");
    real("irlz44n", "IRLZ44N", C::Transistor, "Q", "IRLZ44N", {"G", "D", "S"},
         {P::Input, P::Passive, P::Passive}, "catalog.footprint.to220", "nonlinear.nmos", {"irlz44n", "nmos", "logic level"}, "schematic.npn");
    real("lm358", "LM358", C::Analog, "U", "LM358",
         {"OUT1", "IN1-", "IN1+", "V-", "IN2+", "IN2-", "OUT2", "V+"},
         {P::Output, P::Input, P::Input, P::PowerInput, P::Input, P::Input, P::Output, P::PowerInput},
         "catalog.footprint.dip8", "nonlinear.opamp", {"lm358", "opamp", "op-amp"});
    real("lm393", "LM393", C::Analog, "U", "LM393",
         {"OUT1", "IN1-", "IN1+", "GND", "IN2+", "IN2-", "OUT2", "VCC"},
         {P::OpenCollector, P::Input, P::Input, P::PowerInput, P::Input, P::Input, P::OpenCollector, P::PowerInput},
         "catalog.footprint.dip8", "nonlinear.comparator", {"lm393", "comparator", "karşılaştırıcı"});
    real("ne555", "NE555", C::Analog, "U", "NE555",
         {"GND", "TRIG", "OUT", "RESET", "CTRL", "THRESH", "DISCH", "VCC"},
         {P::PowerInput, P::Input, P::Output, P::Input, P::Input, P::Input, P::OpenCollector, P::PowerInput},
         "catalog.footprint.dip8", "none", {"ne555", "555", "timer"});
    real("7805", "7805", C::Analog, "U", "7805", {"IN", "GND", "OUT"},
         {P::PowerInput, P::PowerInput, P::PowerOutput}, "catalog.footprint.to220", "none", {"7805", "regulator", "regülatör"});
    real("lm317", "LM317", C::Analog, "U", "LM317", {"ADJ", "OUT", "IN"},
         {P::Input, P::PowerOutput, P::PowerInput}, "catalog.footprint.to220", "none", {"lm317", "adjustable", "regulator"});
    for (auto& entry : out) {
        const int pins = entry.device.pinCount;
        auto option = [&](const QString& id) {
            if (!entry.footprintOptions.contains(id)) entry.footprintOptions << id;
        };
        if (pins == 2 && entry.category == C::Passive) {
            for (const char* size : {"0402", "0603", "0805", "1206"})
                option(QStringLiteral("catalog.footprint.passive.") + QLatin1String(size));
            option(QStringLiteral("catalog.footprint.radial5_0"));
        } else if (pins == 2 && entry.category == C::Diode) {
            for (const char* family : {"sod323", "sod123", "sma", "do35", "do41"})
                option(QStringLiteral("catalog.footprint.") + QLatin1String(family));
        } else if (pins == 3 && entry.category == C::Transistor) {
            for (const char* family : {"sot23", "to92", "to220", "dpak"})
                option(QStringLiteral("catalog.footprint.") + QLatin1String(family));
        } else if (pins >= 6 && entry.category == C::Analog) {
            option(QStringLiteral("catalog.footprint.dip%1").arg(pins));
            option(QStringLiteral("catalog.footprint.soic%1").arg(pins));
        }
    }
    return out;
}

QString normalized(QString text) {
    text = text.normalized(QString::NormalizationForm_D).toCaseFolded();
    text.remove(QRegularExpression(QStringLiteral("[\\p{Mn}\\s_\\-/]+")));
    return text;
}

} // namespace

const QVector<ComponentCatalogEntry>& componentCatalog() {
    static const QVector<ComponentCatalogEntry> entries = buildComponents();
    return entries;
}

const QVector<FootprintDefinition>& footprintCatalog() {
    static const QVector<FootprintDefinition> entries = buildFootprints();
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

const ComponentCatalogEntry* findCatalogComponent(const QString& id) {
    const auto& entries = componentCatalog();
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.device.id == id; });
    return it == entries.end() ? nullptr : &*it;
}

const SimulationModelDefinition* findSimulationModel(const QString& id) {
    const auto& entries = simulationModelCatalog();
    const auto it = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) { return entry.id == id; });
    return it == entries.end() ? nullptr : &*it;
}

QList<const ComponentCatalogEntry*> searchComponentCatalog(const QString& query, CatalogCategory* category) {
    QList<const ComponentCatalogEntry*> result;
    const QString needle = normalized(query);
    for (const auto& entry : componentCatalog()) {
        if (category != nullptr && entry.category != *category) continue;
        QStringList haystack{entry.device.name, entry.device.id, entry.device.defaultValue,
                             entry.device.spec.manufacturer, entry.device.spec.partNumber,
                             entry.description};
        haystack += entry.keywords;
        haystack += entry.device.pinNames;
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

void registerBuiltInCatalog() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    QVector<SymbolDefinition> symbols;
    for (const auto& fp : footprintCatalog()) {
        if (validateFootprintParams(fp.params).isEmpty()) symbols << footprintSymbol(fp);
    }
    for (const auto& entry : componentCatalog()) {
        SymbolDefinition symbol = deviceSymbol(entry.device);
        if (!entry.symbolTemplate.isEmpty()) {
            if (const auto* source = findSymbol(entry.symbolTemplate);
                source != nullptr && source->pins.size() == symbol.pins.size()) {
                symbol.shapes = source->shapes;
                symbol.pins = source->pins;
            }
        }
        symbols << symbol;
    }
    registerSymbols(symbols);
}

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
