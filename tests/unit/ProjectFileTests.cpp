#include "hatt/ui/ProjectFile.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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
        QCOMPARE(root[QStringLiteral("formatVersion")].toInt(), ProjectFormatVersion);
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
};

QTEST_GUILESS_MAIN(ProjectFileTests)
#include "ProjectFileTests.moc"
