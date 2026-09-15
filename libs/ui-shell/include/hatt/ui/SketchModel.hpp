#pragma once

#include <QLineF>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

namespace hatt::ui {

// Interim editor model for the UI shell. It is intentionally simple (floating point millimetres,
// snapshot based undo) and will be replaced by the fixed-point domain model and command
// transactions planned in HATT-003 and HATT-004.

enum class Workspace { Schematic, Board };

enum class SymbolCategory { Component, Terminal, Probe };

// PCB layer identifiers (data model v2, ADR-0006). Named after Proteus conventions.
// Stable file tokens — never reuse or rename an entry.
enum class BoardLayer {
    TopCopper,
    BottomCopper,
    TopSilk,
    BottomSilk,
    TopResist,
    BottomResist,
    TopPaste,
    BottomPaste,
    BoardEdge,
};

inline constexpr int BoardLayerCount = 9;

[[nodiscard]] constexpr int layerBit(BoardLayer layer) noexcept {
    return 1 << static_cast<int>(layer);
}
inline constexpr int CopperLayerMask =
    layerBit(BoardLayer::TopCopper) | layerBit(BoardLayer::BottomCopper);
inline constexpr int AllLayersMask = (1 << BoardLayerCount) - 1;

[[nodiscard]] constexpr bool isCopperLayer(BoardLayer layer) noexcept {
    return layer == BoardLayer::TopCopper || layer == BoardLayer::BottomCopper;
}
[[nodiscard]] constexpr bool isBottomLayer(BoardLayer layer) noexcept {
    return layer == BoardLayer::BottomCopper || layer == BoardLayer::BottomSilk ||
           layer == BoardLayer::BottomResist || layer == BoardLayer::BottomPaste;
}
// The same layer on the other board side (top <-> bottom); the board edge stays.
[[nodiscard]] BoardLayer oppositeSideLayer(BoardLayer layer) noexcept;
// Layer mask with every top layer swapped for its bottom counterpart and vice versa.
[[nodiscard]] int mirroredLayerMask(int mask) noexcept;
[[nodiscard]] QString boardLayerName(BoardLayer layer);

// Pad shape for footprint pads and via annular rings.
enum class PadShape { Round, Rect, Oval };

// Describes one pad in a footprint definition or a placed Pad/Via item.
// `layers` is a bitmask of BoardLayer values represented as (1 << int(layer)).
struct PadDefinition {
    int number = 1;              // Pad number (1-based, matches SymbolDefinition::pins index)
    PadShape shape = PadShape::Rect;
    double width = 1.0;          // mm
    double height = 1.0;         // mm (== width for Round)
    double drillDiameter = 0.0;  // mm, 0 = SMD (no hole)
    int layers = (1 << static_cast<int>(BoardLayer::TopCopper)); // bitmask

    friend bool operator==(const PadDefinition&, const PadDefinition&) = default;
};

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
    // Board footprints: pads[i] sits at pins[i]. Pads are drawn from this list; `shapes` only
    // carries the silkscreen outline.
    QVector<PadDefinition> pads;
    // Schematic components: value and footprint given to a newly placed part. The footprint has
    // the same pin count, so pins map to pads one to one.
    QString defaultValue;
    QString defaultFootprint;
    // Pad number of each pin on defaultFootprint; empty means pin N goes to pad N.
    QVector<int> defaultPinPadMap;
    // Stable simulation model id. Empty means that no simulation model is assigned. The model
    // registry and analysis support live in ComponentCatalog; snapshots copy only supported
    // models into the Qt-free electrical domain.
    QString simulationModel;
    // Untranslated name of project-defined symbols; built-in symbols use `name`.
    QString displayName;
};

struct SketchItem {
    enum class Kind { Symbol, Wire, Line, Polyline, Rectangle, Circle, Arc, Text, Pad, Via };

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
    // v2 fields (ADR-0006)
    BoardLayer layer = BoardLayer::TopCopper;  // Active layer for board items
    bool onBottom = false;                     // Footprint placed on bottom side
    bool excludeFromBoard = false;             // Schematic component excluded from PCB transfer
    PadDefinition pad;                         // For Kind::Pad items
    double drillDiameter = 0.0;               // For Kind::Via items (mm)
    // Board tracks: copper width; vias: outer diameter (mm). 0 = the default for the kind.
    double width = 0.0;
    // Copper zones: name of the net the zone is poured for (ADR-0009); empty = not poured.
    QString net;
};

using SketchDocument = QVector<SketchItem>;

inline constexpr double DefaultTrackWidth = 0.3048; // T12
inline constexpr double DefaultViaDiameter = 0.8;
inline constexpr double DefaultViaDrill = 0.4;

// Proteus ARES style track widths; the name is the width in thou.
struct TrackStyle {
    const char* name;
    double width; // mm
};
[[nodiscard]] const QVector<TrackStyle>& trackStyles();

struct ViaStyle {
    const char* name;
    double diameter; // mm
    double drill;    // mm
};
[[nodiscard]] const QVector<ViaStyle>& viaStyles();

// Pads offered by the board pad tools. `pad` is the pad placed by the tool (number 1).
struct PadStyle {
    const char* id; // e.g. "pad.round", stable tool variant
    const char* name;
    PadDefinition pad;
};
[[nodiscard]] const QVector<PadStyle>& padStyles();
[[nodiscard]] const PadStyle* findPadStyle(const QString& id);
[[nodiscard]] QString padStyleDisplayName(const PadStyle& style);

// A pad in world coordinates: footprint pads (rotated, mirrored for the bottom side), placed pads
// and vias. `layers` is a BoardLayer mask.
struct PlacedPad {
    QPointF center;
    PadShape shape = PadShape::Round;
    double width = 0.0;  // along X after rotation
    double height = 0.0; // along Y after rotation
    double drill = 0.0;
    int layers = 0;
    int number = 0;
};
[[nodiscard]] QVector<PlacedPad> itemPads(const SketchItem& item);
// Outline of a pad (closed polygon, world millimetres).
[[nodiscard]] QVector<QPointF> padOutline(const PlacedPad& pad);
// Copper layers an item conducts on (0 for non-copper items); schematic items are not considered.
[[nodiscard]] int itemCopperLayers(const SketchItem& item);
// Layers an item is drawn on, used for board layer visibility. Footprints return their silk layer
// plus their pads' layers.
[[nodiscard]] int itemLayerMask(const SketchItem& item);
[[nodiscard]] double trackWidth(const SketchItem& item);
[[nodiscard]] double viaDiameter(const SketchItem& item);
[[nodiscard]] double viaDrill(const SketchItem& item);

inline const QString BoardOutlineVariant = QStringLiteral("board-outline");
inline const QString CopperZoneVariant = QStringLiteral("copper-zone");
inline constexpr double TextHeightMm = 2.0;

// Pad arrangement of a generated footprint (ComponentLibrary.hpp, ADR-0007).
// Stable file tokens: none, two-terminal, single-row, dual-row, quad-row.
enum class PackageStyle { None, TwoTerminal, SingleRow, DualRow, QuadRow };

// Datasheet data of a device. Every field is optional: empty text or 0 means unknown.
struct DeviceSpec {
    QString manufacturer;
    QString partNumber;
    QString datasheet;
    PackageStyle package = PackageStyle::None;
    bool throughHole = false;
    double pitch = 0.0;      // mm, centre distance between neighbouring pins in a row
    double rowSpacing = 0.0; // mm, centre distance between opposite pad rows or the two terminals
    double bodyWidth = 0.0;  // mm, X
    double bodyLength = 0.0; // mm, Y
    double leadWidth = 0.0;  // mm, lead or terminal width
    double leadLength = 0.0; // mm, SMD foot length
    double pinCurrent = 0.0; // A, maximum continuous current per pin
};

// Parameters of a generated footprint, in millimetres, origin at the footprint centre.
struct FootprintParams {
    PackageStyle style = PackageStyle::SingleRow;
    int padCount = 2;
    double pitch = 2.54;
    double rowSpacing = 7.62;
    PadShape shape = PadShape::Round;
    double padWidth = 1.6;  // across the row (X for left/right rows)
    double padLength = 1.6; // along the row
    double drill = 0.8;     // 0 = SMD
    double bodyWidth = 0.0; // silkscreen body outline; 0 derives it from the pads
    double bodyLength = 0.0;
};

// A footprint created in the project; `id` starts with CustomFootprintPrefix. It is either
// generated from `params` or, when `pads` is not empty, explicit geometry drawn by the user
// (Make Package): silkscreen `shapes`, pad centres in `pins` and `pads` parallel to `pins`, in mm
// relative to the footprint origin as seen from the top.
struct FootprintDefinition {
    QString id;
    QString name;
    FootprintParams params;
    QVector<SymbolShape> shapes;
    QVector<QPointF> pins;
    QVector<PadDefinition> pads;

    [[nodiscard]] bool isExplicit() const { return !pads.isEmpty(); }
    [[nodiscard]] int padCount() const { return isExplicit() ? pads.size() : params.padCount; }
};

// A schematic device created in the project; `id` starts with CustomDevicePrefix. Its symbol is a
// generated box with `pinCount` pins, and `footprint` (optional) must have the same pad count.
struct DeviceDefinition {
    QString id;
    QString name;
    QString prefix = QStringLiteral("U");
    QString defaultValue;
    QString footprint;
    int pinCount = 2;
    QStringList pinNames;
    // Pad number (1-based) on `footprint` for each pin; empty means pin N goes to pad N. When set
    // it has pinCount entries and uses every pad number once.
    QVector<int> pinPadMap;
    QString simulationModel;
    DeviceSpec spec;
};

// Project library (v2, ADR-0006/0007). `devices` is the Proteus style pick list of schematic
// component ids offered by component mode; custom devices and footprints are created in the
// project and registered with registerSymbols when the project is loaded or edited.
struct ProjectLibrary {
    QStringList devices;
    QVector<DeviceDefinition> customDevices;
    QVector<FootprintDefinition> customFootprints;
};

// Schematic component symbol ids placed in `schematic`, in first-use order.
[[nodiscard]] QStringList placedDevices(const SketchDocument& schematic);
// The project's device list: picked devices followed by any device the schematic uses that was
// not picked (e.g. after undoing a delete), without duplicates.
[[nodiscard]] QStringList projectDeviceList(const ProjectLibrary& library,
                                            const SketchDocument& schematic);
// Built-in schematic component ids that can be picked into a project.
[[nodiscard]] bool isPickableDevice(const QString& id);

[[nodiscard]] const QVector<SymbolDefinition>& symbolLibrary();
// Built-in symbols first, then symbols added with registerSymbols.
[[nodiscard]] const SymbolDefinition* findSymbol(const QString& id);
// Makes project-defined symbols findable. A symbol registered again with the same id replaces the
// earlier definition; pointers returned by findSymbol before stay valid for the process lifetime.
void registerSymbols(const QVector<SymbolDefinition>& symbols);
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
