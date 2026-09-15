# ADR-0011: Print Layout and the Qt PrintSupport Dependency

**Status:** Accepted
**Date:** 2026-09-14
**Related:** ADR-0001 (Qt desktop shell), ADR-0009 (zone pour), Gerber export (`GerberExport.hpp`)

## Context

Many HattEDA users etch boards at home (toner transfer, photo resist). They print the copper
artwork at true scale on coated paper or film, and to save material they repeat a small board as
many times as fits on one sheet (Proteus ARES users do this with copy/replicate before printing).
The fabrication export only wrote Gerber and drill files; there was no way to print or to produce a
PDF, and repeating a board meant editing the design and starting over after every fix.

## Decision

- **Print layout** (`PrintLayout.hpp`, Output › Print layout..., Ctrl+P) paints the fabrication output
  (`buildCamOutput`, with poured zones) on a paper page, so a print matches the Gerber files.
  Settings: paper preset (A3, A4, A5, Letter) or custom size, orientation, margin, spacing, selected
  layers (overlaid or each layer side by side), drill holes left open, colours (monochrome, negative,
  board colours), mirror, 90° turn, scale and X/Y printer compensation, and copies across × down.
  `fitCopies` fills the printable area with as many copies as fit, trying both orientations. The
  board is repeated only on the page; the design is never modified.
- **Rendering** builds one filled `QPainterPath` per layer (strokes widened, clear-polarity pour holes
  subtracted), so PDF and printer output are vector graphics at the device resolution.
- **Output**: `QPdfWriter` (Qt Gui) writes PDF; printing uses `QPrinter`/`QPrintDialog` from
  **Qt PrintSupport**. PrintSupport is part of Qt Base, installed with every Qt kit the project already
  requires (local presets and CI `install-qt-action`), and is linked privately by `hatt-ui-shell`.
- **Persistence**: print settings are an application preference (`QSettings print/*`), not part of
  the `.hatt` project format.

## Consequences

- New module dependency `Qt6::PrintSupport`; deployments must ship `Qt6PrintSupport.dll` (windeployqt
  includes it automatically when the executable links it).
- Only board artwork is printed; schematic printing and multi-page output are future work.
- Printer margins are the layout's own margin measured from the paper edge (`QPrinter::setFullPage`);
  printers that cannot print to the edge clip content inside their hardware margin, so the default
  margin is 10 mm.
