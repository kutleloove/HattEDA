# ADR-0004: Interim `.hatt` project file (format version 1)

Status: Accepted for the interim editor implementation; refs #6, #9, #10, #11. Supersedes the "in-memory only" note for identities and electrical metadata in ADR-0003.

Projects could not be saved: New project created no file, Open only changed the title, and Save was disabled. The permanent document model (HATT-002 identities, HATT-003 fixed-point geometry, HATT-004 transactions) is not ready, but losing every design on exit makes the editor unusable. We need persistence now without pretending the interim model is final.

## Decision

- A `.hatt` file is UTF-8 JSON written with Qt Core's `QJsonDocument`. This adds no new dependency. The root holds `format: "hatteda-project"`, an integer `formatVersion` (currently `1`), the project `name`, and `schematic` and `board` sections. Each section holds `items`, a list of `SketchItem`s.
- Each item stores `id` (UUID), `kind` (`symbol`, `wire`, `line`, `polyline`, `rectangle`, `circle`, `arc` or `text`) and `points` as `[x, y]` pairs. The optional fields `variant`, `label`, `quarterTurns`, `closed`, `value`, `footprint`, `pinPadMap` and `sourceId` are written only when they differ from their defaults. Kind names are part of the format and are never renamed or reused.
- Coordinates are floating-point millimetres, exactly as the interim model holds them. Doubles use Qt's shortest round-trip representation, so saving an unchanged project reproduces the file byte for byte. Keys are sorted.
- Readers:
  - They reject a missing or wrong `format`, a missing or non-integer version, and any version newer than they support. The error tells the user to update HattEDA.
  - They also reject malformed sections, invalid or duplicate ids, unknown kinds, invalid or too few points, fields of the wrong type, and symbols unknown to the running library or placed in the wrong workspace.
  - A rejected file loads nothing. Unknown extra fields and sections are ignored, so additive changes do not need a version bump.
  - Anything that changes the meaning of existing data (units, the fixed-point migration, renamed fields) increments `formatVersion`. Newer readers keep a converter for older versions.
- Writes go through `QSaveFile`, so a failed or interrupted save never truncates the previous file.
- Host behaviour:
  - New project writes an empty project immediately. It suggests a free name and asks before replacing an existing file.
  - Open and Recent load the file.
  - Save (Ctrl+S) and Save as (Ctrl+Shift+S) write both workspaces and mark both undo stacks clean.
  - The window is marked modified while either stack is not clean. New, Open, Recent and closing the window ask Save / Discard / Cancel.
  - The file name is the project name.
  - The last folder used for new projects is stored in `QSettings` as `projects/location`.
- The code lives in `ProjectFile.hpp/.cpp` in `hatt-ui-shell`, next to the interim `SketchModel`. It uses only Qt Core and has no widget dependency, so it can move with the model when HATT-003 lands. `.hattc` and `.hattplug` remain separate formats.

## Consequences and remaining work

- Designs survive restarts, and UUID links between schematic and PCB (`sourceId`) persist.
- HATT-003 will introduce `formatVersion: 2` with fixed-point units and a v1 converter. Until then, files carry floating-point rounding exactly as the editor does.
- The symbol library is compiled in. A file that references a symbol removed from a later build is rejected rather than silently dropping parts. Library versioning is future work.
- Autosave, crash recovery, the `.bak` backup and the project lock file are in ADR-0005. They do not change the format.
- Later additive sections: format version 2 layers/pads and the `library` object (ADR-0006), project devices and footprints in `library.customDevices|customFootprints` (ADR-0007), and design rules in `rules` (ADR-0008). They are read before the documents where documents depend on them.
- Not covered yet: embedded schematic symbol drawings, and project-level editor settings (units, grid) inside the file.

## Validation

`hatt-project-file-tests` covers:
- a byte-identical round trip of every item kind and field, including non-ASCII text and awkward doubles;
- that default fields are omitted;
- that unknown fields are accepted;
- rejection of each invalid-file case above, plus the newer-version message;
- atomic save leaving no temporary files, and missing files and folders.

`MainWindowTests::projectSaveOpenAndUnsavedChanges` covers:
- New project creating the file;
- the modified marker;
- save and reopen of both workspaces with identical ids, and the recent list;
- Cancel and Discard on close;
- a corrupted file leaving the open project untouched.
