# ADR-0015: Coordinate System, Y-Axis Direction and Status Bar Display

**Status:** Accepted
**Date:** 2026-09-16
**Issue:** #16 (split from #2)

## Context

`DesignCanvas` stores every `SketchItem` point in floating-point millimetres (`SketchModel.hpp`),
and `.hatt` persists those coordinates unchanged (`ProjectFile.cpp`, `pointsToJson`/`pointFromJson`
write the raw `x`/`y` pair). The canvas maps that "world" space to pixels with a plain scale and
offset — `worldToScreen(world) = world * scale_ + offset_`, `screenToWorld` its inverse
(`DesignCanvas.cpp`) — with no axis flip. Since Qt widget/painter space has Y increasing downward,
world Y also increases downward: moving an item toward the bottom of the canvas increases its
stored Y.

Most schematic/PCB tools (Proteus among them) show the user Y increasing upward, matching
conventional Cartesian/engineering drawings. `MainWindow`'s status bar coordinate readout already
does this by negating the value it displays, without changing what is stored:

```cpp
coordinateLabel_->setText(tr("X %1   Y %2 %3")
                              .arg(formatCoordinate(world.x(), unit),
                                   formatCoordinate(-world.y(), unit),
                                   unitSymbol(unit)));
```

Issue #16 asked whether this display/storage split is the intended permanent rule, since HATT-003
(fixed-point units and geometry primitives) must not silently pick a different axis convention than
what shipped in the interim model and in `.hatt`.

## Decision

- **Storage and internal APIs (`SketchItem::points`, `DesignCanvas` world space, `.hatt` `x`/`y`
  pairs) use Y-down**, matching Qt's native widget
  coordinate system. This is unchanged, existing behaviour, not a new choice — this ADR records it
  so it is not re-litigated or accidentally inverted by later work (HATT-003 in particular).
- **User-facing coordinates use Y-up**: both the status bar and item property dialogs negate
  stored Y for display. Property input is negated when converted back to millimetres; internal
  storage remains Y-down. Follow-up issue #58 applies this to symbols, pads, vias and text in all
  display units.
- **The origin is wherever the first item was placed** — there is no fixed board/sheet origin
  today; `DesignCanvas` does not special-case `(0, 0)`.
- HATT-003's fixed-point geometry types must adopt this same Y-down storage convention so that
  existing `.hatt` files, and any future format migration, do not require an axis flip.

## Consequences

- Status bar and property coordinates have the same sign. Accepting unchanged properties never
  mirrors the item; editing Y still creates one snapshot undo step.
- Existing `.hatt` points, canvas coordinates and external format conversions remain unchanged.
- Issue #58 is covered by property-dialog tests for both signs, mil/mm/inch, cancel, undo/redo
  and project serialization.
