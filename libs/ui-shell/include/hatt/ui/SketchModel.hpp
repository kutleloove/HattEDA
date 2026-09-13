#pragma once

#include <QLineF>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QUuid>
#include <QVector>

namespace hatt::ui {

// Interim editor model for the UI shell. It is intentionally simple (floating point millimetres,
// snapshot based undo) and will be replaced by the fixed-point domain model and command
// transactions planned in HATT-003 and HATT-004.

enum class Workspace { Schematic, Board };

enum class SymbolCategory { Component, Terminal, Probe };

struct SymbolShape {
    QVector<QPointF> points;
    bool closed = false;
    bool filled = false;
    bool copper = false;
    bool hole = false;
};

struct SymbolDefinition {
    QString id;
    const char* name = "";
    Workspace workspace = Workspace::Schematic;
    SymbolCategory category = SymbolCategory::Component;
    QString prefix;
    QString defaultLabel;
    QVector<SymbolShape> shapes;
    QVector<QPointF> pins;
};

struct SketchItem {
    enum class Kind { Symbol, Wire, Line, Polyline, Rectangle, Circle, Arc, Text };

    Kind kind = Kind::Line;
    QVector<QPointF> points;
    QString variant;
    QString label;
    int quarterTurns = 0;
    bool closed = false;
    // Session identities and electrical metadata; persistence is tracked in #6.
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString value;
    QString footprint;
    QVector<int> pinPadMap;
    QString sourceId;
};

using SketchDocument = QVector<SketchItem>;

inline const QString BoardOutlineVariant = QStringLiteral("board-outline");
inline const QString CopperZoneVariant = QStringLiteral("copper-zone");
inline constexpr double TextHeightMm = 2.0;

[[nodiscard]] const QVector<SymbolDefinition>& symbolLibrary();
[[nodiscard]] const SymbolDefinition* findSymbol(const QString& id);
[[nodiscard]] QList<const SymbolDefinition*> symbolsFor(Workspace workspace,
                                                        SymbolCategory category);
[[nodiscard]] QString symbolDisplayName(const SymbolDefinition& symbol);

[[nodiscard]] QPointF symbolToWorld(const SketchItem& item, QPointF local);
[[nodiscard]] QVector<QLineF> itemSegments(const SketchItem& item);
[[nodiscard]] QVector<QPointF> itemAnchors(const SketchItem& item);
[[nodiscard]] QRectF itemBounds(const SketchItem& item);
[[nodiscard]] QVector<QPointF> arcSamples(QPointF start, QPointF through, QPointF end,
                                          int segments = 32);
[[nodiscard]] double distanceToSegment(QPointF point, const QLineF& segment,
                                       QPointF* nearest = nullptr);
[[nodiscard]] QString nextDesignator(const SketchDocument& document, const QString& prefix);

void translateItem(SketchItem& item, QPointF delta);
void rotateItemQuarterTurn(SketchItem& item, QPointF pivot);

} // namespace hatt::ui
