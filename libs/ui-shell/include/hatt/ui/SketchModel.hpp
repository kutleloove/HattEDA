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

// Connection-preserving edits. Wires (schematic wires and board tracks) whose vertices sit on a
// moved pin or wire vertex follow it; horizontal/vertical runs stay orthogonal by moving a free
// neighbouring corner or inserting a dogleg whose bend is rounded to `grid` (0 disables rounding).
// Item count and order never change, so selections stay valid.
[[nodiscard]] SketchDocument moveItemsKeepingConnections(const SketchDocument& document,
                                                         const QList<int>& items, QPointF delta,
                                                         double grid = 0.0);
// Moves one wire segment. Axis-aligned segments only move perpendicular to themselves; segment
// ends attached to pins or other wires keep their position and gain a connecting stub.
[[nodiscard]] SketchDocument dragWireSegment(const SketchDocument& document, int wire, int segment,
                                             QPointF delta, double grid = 0.0);
// Moves one wire vertex; other wires joined at that vertex (not at a pin) follow it.
[[nodiscard]] SketchDocument dragWireVertex(const SketchDocument& document, int wire, int vertex,
                                            QPointF delta, double grid = 0.0);

// Corners of a right-angled route from `from` to `to` (end points excluded). `leaving` is the
// direction the route should leave `from` in (e.g. a pin's outward direction or the previous wire
// segment), `entering` the outward direction of a pin at `to`; either may be null. Two parallel
// preferences give a dogleg whose bend is rounded to `grid`.
[[nodiscard]] QVector<QPointF> orthogonalRoute(QPointF from, QPointF to, QPointF leaving,
                                               QPointF entering, double grid = 0.0);
// Outward axis direction (unit, horizontal or vertical) of a symbol pin located at `point`, or a
// null point when no pin is there.
[[nodiscard]] QPointF pinDirectionAt(const SketchDocument& document, QPointF point);
// Removes zero-length segments and straight-through corners from a path.
void simplifyPath(QVector<QPointF>& points);
// Splits a new wire path at its internal vertices that lie on an existing wire, so each vertex
// placed on a wire becomes a wire end and forms a T join. Pieces keep path order and each has at
// least two points.
[[nodiscard]] QVector<QVector<QPointF>> splitPathAtWires(const SketchDocument& document,
                                                        const QVector<QPointF>& path);

} // namespace hatt::ui
