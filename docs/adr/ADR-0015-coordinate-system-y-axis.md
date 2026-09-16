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
  pairs, item property dialogs' Position X/Y fields) use Y-down**, matching Qt's native widget
  coordinate system. This is unchanged, existing behaviour, not a new choice — this ADR records it
  so it is not re-litigated or accidentally inverted by later work (HATT-003 in particular).
- **Only the status bar coordinate readout displays Y-up**, by negating the value at render time
  (`-world.y()`). No other reader of `SketchItem::points` performs this negation; item property
  dialogs (e.g. `ItemPositionY` in `MainWindow.cpp`) show the stored Y-down value directly, not the
  status bar's flipped one.
- **The origin is wherever the first item was placed** — there is no fixed board/sheet origin
  today; `DesignCanvas` does not special-case `(0, 0)`.
- HATT-003's fixed-point geometry types must adopt this same Y-down storage convention so that
  existing `.hatt` files, and any future format migration, do not require an axis flip.

## Consequences

- The Position X/Y fields in the item properties dialog do not match the sign of the status bar's Y
  readout while editing the same item. This is confusing but pre-existing; fixing it (e.g. by
  showing Y-up everywhere data is presented to the user) is separate follow-up work, not part of
  this decision.
- No code or `.hatt` format change accompanies this ADR; it documents the status quo so future work
  (HATT-003, any coordinate-input UI, import/export of external formats) has a single point of
  reference instead of reverse-engineering the convention from `DesignCanvas.cpp`.
