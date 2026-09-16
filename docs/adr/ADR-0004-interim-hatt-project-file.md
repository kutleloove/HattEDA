# ADR-0004: Interim `.hatt` project file (format version 4)

Status: Accepted for the interim editor implementation; refs #6, #9, #10, #11. Supersedes the "in-memory only" note for identities and electrical metadata in ADR-0003.

Projects could not be saved: New project created no file, Open only changed the title, and Save was disabled. The permanent document model (HATT-002 identities, HATT-003 fixed-point geometry, HATT-004 transactions) is not ready, but losing every design on exit makes the editor unusable. We need persistence now without pretending the interim model is final.

## Decision

- A `.hatt` file is UTF-8 JSON written with Qt Core's `QJsonDocument`. This adds no new dependency. The root holds `format: "hatteda-project"`, an integer `formatVersion` (currently `4` for projects that use version 4 zone features and `3` otherwise, see `requiredFormatVersion`), the project `name`, and `schematic` and `board` sections. Each section holds `items`, a list of `SketchItem`s.
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
- Versions 1 and 2 remain readable. Version 2 introduced layers/pads and project library data; version 3 adds the optional stable `simulationModel` id to custom device records. Missing ids keep the explicit “no model assigned” behavior. Version 4 (ADR-0012) adds keepout and area zones (`variant` `keepout-zone` / `area-zone`) and the item field `zoneFill` (`hatched` or `empty`, omitted for `solid`). Version 3 readers would read them as copper or solid pours, so a project is written as version 4 only when it uses one of them; all other projects keep writing version 3 and still open in older builds. The future fixed-point migration will use a later version and converter.
- The symbol library is compiled in. A file that references a symbol removed from a later build is rejected rather than silently dropping parts. Library versioning is future work.
- Autosave, crash recovery, the `.bak` backup and the project lock file are in ADR-0005. They do not change the format.
- Later sections: format version 2 layers/pads and the `library` object (ADR-0006), project devices and footprints in `library.customDevices|customFootprints` (ADR-0007), design rules in `rules` (ADR-0008; Design Rule Manager parts in ADR-0010, including the optional, additive net class `clearance` from issue #39 that needs no version bump), version 3 custom-device simulation model ids, the copper zone `net` (ADR-0009), and version 4 zone kinds and fill styles (ADR-0012). Built-in catalog definitions are referenced by stable id and are not copied into the file.
- `Kind::Text` items gained an optional `fontFamily` string (#36): the QFont family used for schematic text, empty meaning the application default; board text ignores it and always renders with StrokeFont. Purely additive (omitted when empty, defaults to empty when absent), so no `formatVersion` bump.
- `Kind::Symbol` items gained optional `mirroredX`/`mirroredY` booleans (#8): schematic component mirroring (`hatteda.action.mirror-x|y`), independent of the existing board-side `onBottom` flip. Purely additive (omitted when `false`, defaults to `false` when absent), so no `formatVersion` bump.
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
