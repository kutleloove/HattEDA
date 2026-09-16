# Repository Guidelines

## Project Structure & Module Organization

HattEDA is a C++20/Qt 6 desktop application built with CMake. Executables live in `apps/`: `hatteda-desktop` is the main UI and `hatteda-embedded-agent` demonstrates the agent connection. Reusable code is split into `libs/ui-shell`, `libs/electrical`, and `libs/agentic-mcp`; public headers belong under each library's `include/hatt/...` tree and implementations under `src/`. Unit and workflow tests are in `tests/unit`. Keep Turkish UI translations in `translations/hatteda_tr.ts`, architecture decisions in `docs/adr`, and broader design notes in `docs/architecture`.

## Build, Test, and Development Commands

CMake is installed with Qt and is not assumed to be on `PATH`:

```powershell
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --preset windows-mingw-debug
& 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build --preset debug
& 'C:\Qt\Tools\CMake_64\bin\ctest.exe' --preset debug
```

The first command configures a Debug build, the second compiles all targets, and the third runs the complete test suite offscreen. To run the application:

```powershell
$env:PATH = "C:\Qt\6.11.2\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;$env:PATH"
& '.\build\windows-mingw-debug\apps\hatteda-desktop\hatteda.exe'
```

## Coding Style & Naming Conventions

Follow `.clang-format`: four-space indentation, 100-column guidance, and left-aligned pointers. Use `PascalCase` for types, `camelCase` for functions and locals, and trailing underscores for private members. Keep public APIs in the `hatt::ui`, `hatt::electrical`, or relevant library namespace. Route user-facing strings through `tr()` and add Turkish translations. Preserve snapshot-based document edits so each user operation remains one undo step.

## Testing Guidelines

Tests use QtTest, except explicitly documented plain C++ tests. Name test files after the subject (`DesignCanvasTests.cpp`) and test functions after behavior (`movingSelectionIsOneUndoStep`). Register new QtTest executables with `hatt_add_qt_test` in `tests/unit/CMakeLists.txt`. Test reports are written to `build/windows-mingw-debug/test-logs`; inspect them when CTest output is sparse. Add focused unit coverage for every behavior change, then run the full preset.

## Commit & Pull Request Guidelines

Recent history uses concise, imperative subjects such as `Add net class clearance` or `Fix stray ADR references`. Keep commits scoped and avoid mixing formatting with behavior. Pull requests should explain the user-visible result, identify affected modules, link the issue or ADR, and report build/test results. Include screenshots for UI changes and document architectural boundaries with an ADR when the decision affects persistence, dependencies, or subsystem ownership.

## Safety & Repository Hygiene

Do not edit generated files under `build/`. Preserve unrelated working-tree changes, avoid destructive Git commands, and keep external tools optional behind documented interchange boundaries.
