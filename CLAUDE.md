# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

HattEDA is a Qt 6 Widgets (C++20) desktop EDA application: schematic & simulation, PCB/CAD, CAM, and manufacturing automation. The README and much of the project documentation are in Turkish. The authoritative architecture contract is `docs/HattEDA_Teknik_Gereksinimler_ve_Mimari_Spesifikasyon_v0.2.docx` (HATT-SPEC-0001).

The repo is currently a bootstrap vertical slice that only proves the UI shell architecture. Schematic editing, PCB editing, file formats, and real plugin loading are **not implemented yet**. Planned next tickets: `HATT-002` (IDs, `Result/Error`, logging), `HATT-003` (fixed-point units, geometry primitives), `HATT-004` (command/undo transactions), `HATT-032` (`.hattplug` manifest + compatibility preflight), `HATT-033` (Plugin API + `ContributionRegistry`), `HATT-034` (native Qt plugin loader + sample plugin).

## Build, run, test (Windows, PowerShell, from repo root)

The toolchain is pinned by absolute paths in `CMakePresets.json` (Qt 6.11.2 MinGW 64-bit at `C:\Qt\6.11.2\mingw_64`, MinGW 13.1, Ninja and CMake under `C:\Qt\Tools`). CMake is not assumed to be on PATH.

```powershell
# Configure + build (Debug, tests enabled)
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --preset windows-mingw-debug
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset debug

# Run (Qt and MinGW DLLs must be on PATH)
$env:PATH = "C:\Qt\6.11.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
& '.\build\windows-mingw-debug\apps\hatteda-desktop\hatteda.exe'

# All tests (test preset sets QT_QPA_PLATFORM=offscreen and Qt PATH)
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --preset debug

# A single QtTest function: run the test exe directly with the function name
$env:PATH = "C:\Qt\6.11.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"; $env:QT_QPA_PLATFORM = "offscreen"
& '.\build\windows-mingw-debug\tests\unit\hatt-ui-shell-tests.exe' toolWorkspaceIsOpenedOnce

# Release (BUILD_TESTING=OFF)
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --preset windows-mingw-release
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset release
```

Formatting follows `.clang-format` (LLVM base, 4-space indent, 100 columns, left pointer alignment). Targets compile with `-Wall -Wextra -Wpedantic` (`/W4 /permissive-` on MSVC).

## Layout and architecture

- `apps/hatteda-desktop/` — composition root only (`main.cpp`): sets app/org names, loads translation from `QSettings` key `appearance/language`, applies theme from `appearance/lightTheme`, shows `MainWindow`. Translations (`translations/hatteda_tr.ts`) are compiled via `qt_add_translations` into resources under `:/i18n/`.
- `libs/ui-shell/` — static target `hatt-ui-shell` (alias `HattEDA::UiShell`), namespace `hatt::ui`, public headers under `include/hatt/ui/`. Contains `MainWindow` (all shell chrome) and `Theme` (dark/light `QPalette` + stylesheet).
- `tests/unit/` — QtTest executable `hatt-ui-shell-tests`, registered with CTest. New test files need to be added to `tests/unit/CMakeLists.txt`.
- `docs/adr/` — numbered architecture decision records; `docs/architecture/` — implementation notes; `docs/design-system.md` — color tokens and UI patterns.

### Shell model (`MainWindow`)

- `ApplicationPages` stacked widget: index 0 is `WelcomePage` (no project chrome, docks hidden); a project activation switches to the editor.
- Two **persistent primary workspaces** in `PrimaryWorkspaceHost`: Mergen (schematic & simulation) and Kayra (PCB/CAD/CAM). **Tool workspaces** (Gerber viewer, simulation reports, future plugin tools) are closable tabs in `ToolWorkspaceHost`, opened via `openToolWorkspace(stableId, ...)`, which dedupes by stable id (e.g. `hatteda.tool.simulation-diagnostics`) set as the content widget's `objectName`.
- Tests locate widgets by `objectName` and by dynamic properties (`rail`, `command`, `snap`, `documentTab`, `quiet`), and `Theme` styles them via the same property/object-name selectors. Treat these names as a contract: renaming them breaks tests, styling, and future layout persistence.

## Architecture rules (from README, ADR-0001, architecture notes)

- Mergen/Kayra are marketing names; code uses technical names (`SchematicWorkspace`, `BoardWorkspace`, `ToolWorkspaceHost`) and must not turn marketing names into domain class names.
- UI event handlers carry no domain business logic. Domain libraries must not depend on `hatt-ui-shell` or on Qt UI types.
- Only the host creates docks, toolbars, and menus, with stable object names. Feature/plugin code must not call `QMainWindow::addDockWidget`, `addToolBar`, or mutate menus directly; contributions will go through `ContributionRegistry`.
- Plugins never get mutable domain pointers: reads via snapshots/DTOs, writes via command/transaction services. Plugin UI inherits the app palette and must not restyle the shell.
- `.hatt`, `.hattc`, `.hattplug` are separate, versioned formats.
- AI and cloud features must never be required for the app to run.
- No new dependency, persistent format field, or cross-layer dependency without an ADR and tests.
- QML is not a dependency of the v1 shell or plugin SDK.
- UI: user-facing strings go through `tr()` (add Turkish entries to `translations/hatteda_tr.ts`); dark and light themes are both first-class; selection state must pair color with a structural cue (left rail / top border); use text labels for commands rather than ad-hoc Unicode symbols as icons; alignment/distribution commands live only in the collapsible bottom alignment strip.
