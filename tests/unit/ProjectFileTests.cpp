#include "hatt/ui/ProjectFile.hpp"

#include "hatt/ui/ComponentLibrary.hpp"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace hatt::ui;

namespace {

SketchItem item(SketchItem::Kind kind, QVector<QPointF> points, QString variant = {}) {
    SketchItem result;
    result.kind = kind;
    result.points = std::move(points);
    result.variant = std::move(variant);
    return result;
}

ProjectData sampleProject() {
    ProjectData project;
    project.name = QStringLiteral("Divider");
    SketchItem resistor = item(SketchItem::Kind::Symbol, {{20.32, 20.32}}, QStringLiteral("schematic.resistor"));
    resistor.label = QStringLiteral("R1");
    resistor.value = QStringLiteral("4.7k");
    resistor.quarterTurns = 3;
    resistor.footprint = QStringLiteral("board.r0603");
    resistor.pinPadMap = {2, 1};
    project.schematic = {
        resistor,
        item(SketchItem::Kind::Wire, {{25.4, 20.32}, {40.64, 20.32}, {40.64, 0.1 + 0.2}}),
        item(SketchItem::Kind::Line, {{0, 0}, {1, 1}}),
        item(SketchItem::Kind::Rectangle, {{0, 0}, {5, 5}}),
        item(SketchItem::Kind::Circle, {{0, 0}, {2, 0}}),
        item(SketchItem::Kind::Arc, {{0, 0}, {2, 0}, {1, 1}}),
        item(SketchItem::Kind::Text, {{3, 3}}),
    };
    project.schematic.last().label = QStringLiteral("Güç girişi");
    SketchItem footprint = item(SketchItem::Kind::Symbol, {{10.16, 10.16}}, QStringLiteral("board.r0603"));
    footprint.sourceId = resistor.id;
    SketchItem outline =
        item(SketchItem::Kind::Polyline, {{0, 0}, {50, 0}, {50, 30}}, BoardOutlineVariant);
    outline.closed = true;
    project.board = {footprint, outline, item(SketchItem::Kind::Wire, {{-1.5e-7, 2}, {3, 2}})};
    return project;
}

void compareDocuments(const SketchDocument& actual, const SketchDocument& expected) {
    QCOMPARE(actual.size(), expected.size());
    for (qsizetype i = 0; i < actual.size(); ++i) {
        const SketchItem& a = actual[i];
        const SketchItem& e = expected[i];
        QCOMPARE(a.id, e.id);
        QCOMPARE(a.kind, e.kind);
        QCOMPARE(a.points.size(), e.points.size());
        for (qsizetype p = 0; p < a.points.size(); ++p) {
            // Exact: the file must round-trip every double bit for bit.
            QVERIFY(a.points[p].x() == e.points[p].x() && a.points[p].y() == e.points[p].y());
        }
        QCOMPARE(a.variant, e.variant);
        QCOMPARE(a.label, e.label);
        QCOMPARE(a.quarterTurns, e.quarterTurns);
        QCOMPARE(a.closed, e.closed);
        QCOMPARE(a.value, e.value);
        QCOMPARE(a.footprint, e.footprint);
        QCOMPARE(a.pinPadMap, e.pinPadMap);
        QCOMPARE(a.sourceId, e.sourceId);
        // v2 fields
        QCOMPARE(a.layer, e.layer);
        QCOMPARE(a.onBottom, e.onBottom);
        QCOMPARE(a.excludeFromBoard, e.excludeFromBoard);
        if (e.kind == SketchItem::Kind::Pad) {
            QCOMPARE(a.pad.number, e.pad.number);
            QCOMPARE(a.pad.shape, e.pad.shape);
            QVERIFY(a.pad.width == e.pad.width);
            QVERIFY(a.pad.height == e.pad.height);
            QVERIFY(a.pad.drillDiameter == e.pad.drillDiameter);
            QCOMPARE(a.pad.layers, e.pad.layers);
        }
        if (e.kind == SketchItem::Kind::Via)
            QVERIFY(a.drillDiameter == e.drillDiameter);
    }
}

QJsonObject sampleJson() { return QJsonDocument::fromJson(serializeProject(sampleProject())).object(); }

QString errorFor(const QJsonObject& root) {
    return parseProject(QJsonDocument(root).toJson()).error;
}

QJsonObject withFirstSchematicItem(QJsonObject root, const std::function<void(QJsonObject&)>& edit) {
    QJsonObject schematic = root[QStringLiteral("schematic")].toObject();
    QJsonArray items = schematic[QStringLiteral("items")].toArray();
    QJsonObject first = items[0].toObject();
    edit(first);
    items[0] = first;
    schematic[QStringLiteral("items")] = items;
    root[QStringLiteral("schematic")] = schematic;
    return root;
}

} // namespace

class ProjectFileTests final : public QObject {
    Q_OBJECT

private slots:
    void roundTripsEveryKindAndField() {
        const ProjectData project = sampleProject();
        const QByteArray bytes = serializeProject(project);
        const ProjectLoad load = parseProject(bytes);
        QVERIFY2(load.ok(), qPrintable(load.error));
        QCOMPARE(load.project.name, project.name);
        compareDocuments(load.project.schematic, project.schematic);
        compareDocuments(load.project.board, project.board);
        // Writing is deterministic, so saving an unchanged project gives identical bytes.
        QCOMPARE(serializeProject(load.project), bytes);
    }

    void writesVersionedHeaderAndOmitsDefaults() {
        const QJsonObject root = sampleJson();
        QCOMPARE(root[QStringLiteral("format")].toString(), QStringLiteral("hatteda-project"));
        // No version 4 feature (ADR-0012) is used, so older builds can still open the file.
        QCOMPARE(root[QStringLiteral("formatVersion")].toInt(), ProjectBaseFormatVersion);
        const QJsonObject line = root[QStringLiteral("schematic")][QStringLiteral("items")][2].toObject();
        QCOMPARE(line[QStringLiteral("kind")].toString(), QStringLiteral("line"));
        QVERIFY(!line.contains(QStringLiteral("label")));
        QVERIFY(!line.contains(QStringLiteral("quarterTurns")));
    }

    void emptyProjectAndUnknownFieldsAreAccepted() {
        ProjectData empty;
        QVERIFY(parseProject(serializeProject(empty)).ok());
        QJsonObject root = withFirstSchematicItem(sampleJson(), [](QJsonObject& first) {
            first[QStringLiteral("futureField")] = 42;
        });
        root[QStringLiteral("futureSection")] = QJsonObject{};
        QVERIFY(errorFor(root).isEmpty());
    }

    void rejectsInvalidFiles_data() {
        QTest::addColumn<QByteArray>("bytes");
        auto add = [](const char* name, const QJsonObject& root) {
            QTest::newRow(name) << QJsonDocument(root).toJson();
        };
        QTest::newRow("not json") << QByteArray("{ nope");
        QTest::newRow("array root") << QByteArray("[]");
        QJsonObject root = sampleJson();
        root[QStringLiteral("format")] = QStringLiteral("other");
        add("wrong format", root);
        root = sampleJson();
        root[QStringLiteral("formatVersion")] = ProjectFormatVersion + 1;
        add("newer version", root);
        root = sampleJson();
        root.remove(QStringLiteral("formatVersion"));
        add("missing version", root);
        root = sampleJson();
        root.remove(QStringLiteral("board"));
        add("missing board", root);
        const QJsonObject base = sampleJson();
        add("unknown kind", withFirstSchematicItem(base, [](QJsonObject& o) { o[QStringLiteral("kind")] = QStringLiteral("blob"); }));
        add("bad id", withFirstSchematicItem(base, [](QJsonObject& o) { o[QStringLiteral("id")] = QStringLiteral("x"); }));
        add("bad point", withFirstSchematicItem(base, [](QJsonObject& o) { o[QStringLiteral("points")] = QJsonArray{QJsonArray{1}}; }));
        add("string point", withFirstSchematicItem(base, [](QJsonObject& o) { o[QStringLiteral("points")] = QJsonArray{QJsonArray{QStringLiteral("1"), 2}}; }));
        add("too few points", withFirstSchematicItem(base, [](QJsonObject& o) { o[QStringLiteral("points")] = QJsonArray{}; }));
        add("unknown symbol", withFirstSchematicItem(base, [](QJsonObject& o) { o[QStringLiteral("variant")] = QStringLiteral("board.r0603"); }));
        add("wrong type", withFirstSchematicItem(base, [](QJsonObject& o) { o[QStringLiteral("label")] = 5; }));
        add("fractional rotation", withFirstSchematicItem(base, [](QJsonObject& o) { o[QStringLiteral("quarterTurns")] = 1.5; }));
        add("bad map", withFirstSchematicItem(base, [](QJsonObject& o) { o[QStringLiteral("pinPadMap")] = QJsonArray{QStringLiteral("a")}; }));
        root = sampleJson();
        QJsonObject schematic = root[QStringLiteral("schematic")].toObject();
        QJsonArray items = schematic[QStringLiteral("items")].toArray();
        items.append(items[0]);
        schematic[QStringLiteral("items")] = items;
        root[QStringLiteral("schematic")] = schematic;
        add("duplicate id", root);
        root = sampleJson();
        root[QStringLiteral("library")] = QJsonObject{{QStringLiteral("devices"), QJsonArray{QStringLiteral("board.r0603")}}};
        add("unknown device", root);
        root[QStringLiteral("library")] = QJsonObject{{QStringLiteral("devices"), QStringLiteral("schematic.resistor")}};
        add("devices not a list", root);
        root[QStringLiteral("library")] = QJsonArray{};
        add("library not an object", root);
    }

    void rejectsInvalidFiles() {
        QFETCH(QByteArray, bytes);
        const ProjectLoad load = parseProject(bytes);
        QVERIFY(!load.ok());
        QVERIFY(load.project.schematic.isEmpty());
        QVERIFY(load.project.board.isEmpty());
    }

    void newerVersionExplainsUpdate() {
        QJsonObject root = sampleJson();
        root[QStringLiteral("formatVersion")] = 7;
        QVERIFY(errorFor(root).contains(QStringLiteral("newer")));
    }

    void savesAtomicallyAndLoads() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("board.hatt"));
        const ProjectData project = sampleProject();
        QVERIFY(saveProjectFile(path, project).isEmpty());
        const ProjectLoad load = loadProjectFile(path);
        QVERIFY2(load.ok(), qPrintable(load.error));
        compareDocuments(load.project.board, project.board);
        QCOMPARE(QDir(directory.path()).entryList(QDir::Files), QStringList{QStringLiteral("board.hatt")});

        QVERIFY(!loadProjectFile(directory.filePath(QStringLiteral("missing.hatt"))).ok());
        QVERIFY(!saveProjectFile(directory.filePath(QStringLiteral("no/such/dir/x.hatt")), {}).isEmpty());
    }

    // v2 tests

    void v2RoundTripsNewFields() {
        // Build a project with v2-specific fields set.
        ProjectData project;
        project.name = QStringLiteral("V2Test");
        SketchItem comp = item(SketchItem::Kind::Symbol, {{0, 0}}, QStringLiteral("schematic.resistor"));
        comp.excludeFromBoard = true;
        SketchItem board = item(SketchItem::Kind::Symbol, {{10, 10}}, QStringLiteral("board.r0603"));
        board.onBottom = true;
        board.layer = BoardLayer::BottomCopper;
        project.schematic = {comp};
        project.board = {board};

        const ProjectLoad load = parseProject(serializeProject(project));
        QVERIFY2(load.ok(), qPrintable(load.error));
        compareDocuments(load.project.schematic, project.schematic);
        compareDocuments(load.project.board, project.board);
        // Deterministic re-serialise
        QCOMPARE(serializeProject(load.project), serializeProject(project));
    }

    void v1FileIsUpgradedToV2() {
        // Craft a minimal v1 file (formatVersion=1, no v2 fields).
        QJsonObject root;
        root[QStringLiteral("format")] = QStringLiteral("hatteda-project");
        root[QStringLiteral("formatVersion")] = 1;
        root[QStringLiteral("name")] = QStringLiteral("OldProject");
        QJsonArray items;
        QJsonObject wire;
        wire[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        wire[QStringLiteral("kind")] = QStringLiteral("wire");
        wire[QStringLiteral("points")] = QJsonArray{QJsonArray{0.0, 0.0}, QJsonArray{1.0, 0.0}};
        items.append(wire);
        QJsonObject schematicSection;
        schematicSection[QStringLiteral("items")] = items;
        root[QStringLiteral("schematic")] = schematicSection;
        root[QStringLiteral("board")] = QJsonObject{{QStringLiteral("items"), QJsonArray{}}};

        const ProjectLoad load = parseProject(QJsonDocument(root).toJson());
        QVERIFY2(load.ok(), qPrintable(load.error));
        QCOMPARE(load.project.schematic.size(), 1);
        // v2 defaults applied
        QCOMPARE(load.project.schematic[0].layer, BoardLayer::TopCopper);
        QVERIFY(!load.project.schematic[0].onBottom);
        QVERIFY(!load.project.schematic[0].excludeFromBoard);
    }

    void padItemRoundTrip() {
        ProjectData project;
        project.name = QStringLiteral("PadTest");
        SketchItem padItem;
        padItem.kind = SketchItem::Kind::Pad;
        padItem.points = {{5.0, 3.0}};
        padItem.pad.number = 2;
        padItem.pad.shape = PadShape::Round;
        padItem.pad.width = 1.2;
        padItem.pad.height = 1.2;
        padItem.pad.drillDiameter = 0.6;
        padItem.pad.layers = (1 << static_cast<int>(BoardLayer::TopCopper))
                           | (1 << static_cast<int>(BoardLayer::BottomCopper));
        padItem.layer = BoardLayer::TopCopper;
        SketchItem viaItem;
        viaItem.kind = SketchItem::Kind::Via;
        viaItem.points = {{8.0, 4.0}};
        viaItem.drillDiameter = 0.4;
        project.board = {padItem, viaItem};

        const ProjectLoad load = parseProject(serializeProject(project));
        QVERIFY2(load.ok(), qPrintable(load.error));
        compareDocuments(load.project.board, project.board);
    }

    void libraryStubIsWritten() {
        const QJsonObject root = sampleJson();
        // v2 format must write the library key (even if empty).
        QVERIFY(root.contains(QStringLiteral("library")));
        QVERIFY(root[QStringLiteral("library")].isObject());
    }

    void libraryDevicesRoundTrip() {
        ProjectData project;
        project.library.devices = {QStringLiteral("schematic.capacitor"), QStringLiteral("schematic.resistor")};
        const QByteArray bytes = serializeProject(project);
        const ProjectLoad load = parseProject(bytes);
        QVERIFY2(load.ok(), qPrintable(load.error));
        QCOMPARE(load.project.library.devices, project.library.devices);
        QCOMPARE(serializeProject(load.project), bytes);

        // Files written before the device list (empty library object, or none) still open.
        QJsonObject root = sampleJson();
        root[QStringLiteral("library")] = QJsonObject{};
        QVERIFY(errorFor(root).isEmpty());
        root.remove(QStringLiteral("library"));
        QVERIFY(errorFor(root).isEmpty());
    }

    void designRulesRoundTrip() {
        ProjectData project = sampleProject();
        project.rules.clearance = 0.254;
        project.rules.minTrackWidth = 0.127;
        project.rules.minDrill = 0.35;
        project.rules.minAnnularRing = 0.1;
        project.rules.boardEdgeClearance = 0.5;
        const QByteArray bytes = serializeProject(project);
        const ProjectLoad load = parseProject(bytes);
        QVERIFY2(load.ok(), qPrintable(load.error));
        QVERIFY(load.project.rules == project.rules);
        QCOMPARE(serializeProject(load.project), bytes);

        // Files without rules (or with some keys missing) use the defaults.
        QJsonObject root = sampleJson();
        root.remove(QStringLiteral("rules"));
        ProjectLoad old = parseProject(QJsonDocument(root).toJson());
        QVERIFY2(old.ok(), qPrintable(old.error));
        QVERIFY(old.project.rules == DesignRules{});
        root[QStringLiteral("rules")] = QJsonObject{{QStringLiteral("clearance"), 0.3}};
        old = parseProject(QJsonDocument(root).toJson());
        QVERIFY(old.ok());
        QCOMPARE(old.project.rules.clearance, 0.3);
        QCOMPARE(old.project.rules.minDrill, DesignRules{}.minDrill);

        // Wrong types, negative values and a zero clearance are rejected.
        for (const QJsonValue& rules : {QJsonValue(QStringLiteral("tight")),
                                        QJsonValue(QJsonObject{{QStringLiteral("minDrill"), -0.1}}),
                                        QJsonValue(QJsonObject{{QStringLiteral("clearance"), QStringLiteral("0.2")}}),
                                        QJsonValue(QJsonObject{{QStringLiteral("clearance"), 0}})}) {
            root[QStringLiteral("rules")] = rules;
            QVERIFY(!errorFor(root).isEmpty());
        }
    }

    void customLibraryRoundTrips() {
        ProjectData project;
        FootprintDefinition generated;
        generated.id = newCustomFootprintId();
        generated.name = QStringLiteral("SOIC-8 wide");
        generated.params.style = PackageStyle::DualRow;
        generated.params.padCount = 8;
        generated.params.pitch = 1.27;
        generated.params.rowSpacing = 5.4;
        generated.params.shape = PadShape::Rect;
        generated.params.padWidth = 1.5;
        generated.params.padLength = 0.6;
        generated.params.drill = 0.0;
        generated.params.bodyWidth = 3.9;
        FootprintDefinition drawn;
        drawn.id = newCustomFootprintId();
        drawn.name = QStringLiteral("Drawn jack");
        PadDefinition tip;
        tip.number = 2;
        tip.shape = PadShape::Oval;
        tip.width = 1.8;
        tip.height = 2.4;
        tip.drillDiameter = 1.1;
        tip.layers = 3;
        PadDefinition sleeve;
        drawn.pads = {tip, sleeve};
        drawn.pins = {{3.5, -0.25}, {-3.5, 0.125}};
        SymbolShape outline;
        outline.points = {{-5, -3}, {5, -3}, {5, 3}};
        outline.closed = true;
        SymbolShape mark;
        mark.points = {{0.5, 0.5}};
        mark.filled = true;
        drawn.shapes = {outline, mark};
        DeviceDefinition device;
        device.id = newCustomDeviceId();
        device.name = QStringLiteral("LM358");
        device.prefix = QStringLiteral("IC");
        device.defaultValue = QStringLiteral("LM358");
        device.pinCount = 8;
        device.footprint = generated.id;
        device.pinNames = {QStringLiteral("OUT1"), QStringLiteral("IN1-")};
        device.simulationModel = QStringLiteral("none");
        device.pinPadMap = {8, 7, 6, 5, 4, 3, 2, 1};
        device.spec.manufacturer = QStringLiteral("Texas Instruments");
        device.spec.package = PackageStyle::DualRow;
        device.spec.pitch = 1.27;
        device.spec.pinCurrent = 0.04;
        DeviceDefinition jack;
        jack.id = newCustomDeviceId();
        jack.name = QStringLiteral("Jack");
        jack.prefix = QStringLiteral("J");
        jack.footprint = drawn.id;
        project.library.customFootprints = {generated, drawn};
        project.library.customDevices = {device, jack};
        project.library.devices = {device.id};
        SketchItem part = item(SketchItem::Kind::Symbol, {{0, 0}}, device.id);
        part.label = QStringLiteral("IC1");
        project.schematic = {part};
        project.board = {item(SketchItem::Kind::Symbol, {{0, 0}}, drawn.id)};

        const QByteArray bytes = serializeProject(project);
        const ProjectLoad load = parseProject(bytes);
        QVERIFY2(load.ok(), qPrintable(load.error));
        const ProjectLibrary& read = load.project.library;
        QCOMPARE(read.devices, project.library.devices);
        QCOMPARE(read.customFootprints.size(), 2);
        QCOMPARE(read.customDevices.size(), 2);
        QCOMPARE(read.customFootprints[0].params.padCount, 8);
        QVERIFY(!read.customFootprints[0].isExplicit());
        QCOMPARE(read.customFootprints[0].params.shape, PadShape::Rect);
        QVERIFY(read.customFootprints[0].params.bodyWidth == 3.9);
        const FootprintDefinition& readDrawn = read.customFootprints[1];
        QVERIFY(readDrawn.isExplicit());
        QCOMPARE(readDrawn.pads.size(), 2);
        QCOMPARE(readDrawn.pads[0].number, 2);
        QCOMPARE(readDrawn.pads[0].shape, PadShape::Oval);
        QVERIFY(readDrawn.pads[0].drillDiameter == 1.1);
        QCOMPARE(readDrawn.pads[0].layers, 3);
        QVERIFY(readDrawn.pins[1] == QPointF(-3.5, 0.125));
        QCOMPARE(readDrawn.shapes.size(), 2);
        QVERIFY(readDrawn.shapes[0].closed && readDrawn.shapes[1].filled);
        QCOMPARE(read.customDevices[0].pinPadMap, device.pinPadMap);
        QCOMPARE(read.customDevices[0].pinNames, device.pinNames);
        QCOMPARE(read.customDevices[0].simulationModel, device.simulationModel);
        QCOMPARE(read.customDevices[0].spec.manufacturer, device.spec.manufacturer);
        QCOMPARE(read.customDevices[0].spec.package, PackageStyle::DualRow);
        QVERIFY(read.customDevices[0].spec.pinCurrent == 0.04);
        QCOMPARE(load.project.schematic.size(), 1);
        QCOMPARE(serializeProject(load.project), bytes);
        const auto* symbol = findSymbol(device.id);
        QVERIFY(symbol != nullptr);
        QCOMPARE(symbol->defaultPinPadMap, device.pinPadMap);
    }

    void invalidCustomLibraryRowsAreRejected() {
        auto libraryWith = [](const QJsonObject& footprint, const QJsonObject& device) {
            QJsonObject root = sampleJson();
            QJsonObject library;
            if (!footprint.isEmpty()) library[QStringLiteral("customFootprints")] = QJsonArray{footprint};
            if (!device.isEmpty()) library[QStringLiteral("customDevices")] = QJsonArray{device};
            root[QStringLiteral("library")] = library;
            return root;
        };
        auto footprint = [](int padCount) {
            return QJsonObject{{QStringLiteral("id"), newCustomFootprintId()},
                               {QStringLiteral("name"), QStringLiteral("F")},
                               {QStringLiteral("style"), QStringLiteral("dual-row")},
                               {QStringLiteral("padCount"), padCount}};
        };
        auto device = [](int pinCount) {
            return QJsonObject{{QStringLiteral("id"), newCustomDeviceId()},
                               {QStringLiteral("name"), QStringLiteral("D")},
                               {QStringLiteral("prefix"), QStringLiteral("U")},
                               {QStringLiteral("pinCount"), pinCount}};
        };
        QVERIFY(errorFor(libraryWith(footprint(8), device(3))).isEmpty());
        // Odd pad count for a dual row, unknown style, foreign id prefix.
        QVERIFY(!errorFor(libraryWith(footprint(7), {})).isEmpty());
        QJsonObject style = footprint(8);
        style[QStringLiteral("style")] = QStringLiteral("bga");
        QVERIFY(!errorFor(libraryWith(style, {})).isEmpty());
        QJsonObject foreign = footprint(8);
        foreign[QStringLiteral("id")] = QStringLiteral("board.soic8");
        QVERIFY(!errorFor(libraryWith(foreign, {})).isEmpty());
        // Explicit pads without a position, or with a repeated number.
        QJsonObject pad{{QStringLiteral("number"), 1}, {QStringLiteral("shape"), QStringLiteral("rect")},
                        {QStringLiteral("width"), 1.0}, {QStringLiteral("height"), 1.0},
                        {QStringLiteral("layers"), 1}, {QStringLiteral("at"), QJsonArray{0.0, 0.0}}};
        QJsonObject drawn = footprint(2);
        drawn[QStringLiteral("pads")] = QJsonArray{pad};
        QVERIFY(errorFor(libraryWith(drawn, {})).isEmpty());
        QJsonObject unplaced = pad;
        unplaced.remove(QStringLiteral("at"));
        drawn[QStringLiteral("id")] = newCustomFootprintId();
        drawn[QStringLiteral("pads")] = QJsonArray{unplaced};
        QVERIFY(!errorFor(libraryWith(drawn, {})).isEmpty());
        drawn[QStringLiteral("id")] = newCustomFootprintId();
        drawn[QStringLiteral("pads")] = QJsonArray{pad, pad};
        QVERIFY(!errorFor(libraryWith(drawn, {})).isEmpty());
        // Pin count out of range, footprint with another pad count, bad pin to pad map.
        QVERIFY(!errorFor(libraryWith({}, device(0))).isEmpty());
        QJsonObject withFootprint = device(3);
        withFootprint[QStringLiteral("footprint")] = QStringLiteral("board.soic8");
        QVERIFY(!errorFor(libraryWith({}, withFootprint)).isEmpty());
        QJsonObject mapped = device(3);
        mapped[QStringLiteral("pinPadMap")] = QJsonArray{1, 1, 2};
        QVERIFY(!errorFor(libraryWith({}, mapped)).isEmpty());
        mapped[QStringLiteral("pinPadMap")] = QJsonArray{3, 1, 2};
        QVERIFY(errorFor(libraryWith({}, mapped)).isEmpty());
        // A document using an unregistered custom device does not open.
        QJsonObject root = withFirstSchematicItem(sampleJson(), [](QJsonObject& first) {
            first[QStringLiteral("variant")] = newCustomDeviceId();
        });
        QVERIFY(!errorFor(root).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ProjectFileTests)
#include "ProjectFileTests.moc"
