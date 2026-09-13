# ADR-0005: Continuous integration with GitHub Actions

Status: Accepted; refs #15.

The repository had no CI, so pull requests could merge without being built or tested. The local toolchain is pinned by absolute `C:\Qt\...` paths in `CMakePresets.json`, which hosted runners do not have.

## Decision

- `.github/workflows/ci.yml` runs one `windows-latest` job on pushes to `main`, `feature/**` and `work/**`, on pull requests to `main`, and on manual dispatch.
- The toolchain matches the local setup: Qt 6.11.2 `win64_mingw`, Qt's MinGW 13.1 (`tools_mingw1310`) and Ninja (`tools_ninja`), installed with `jurplel/install-qt-action` and cached. These are CI tools only and add no application or build dependency. aqtinstall is installed from a pinned git commit because its latest release (3.3.0) cannot resolve the Qt 6.11 repository layout; switch back to a release once one ships with Qt 6.11 support.
- Third-party actions are pinned to commit SHAs with the version in a comment.
- Additional presets `ci-mingw-debug` and `ci-mingw-release` (hidden base `ci-mingw-base`, build presets `ci-debug`/`ci-release`, test preset `ci-debug`) read `QT_ROOT_DIR` and `HATTEDA_MINGW_DIR` from the environment and find `ninja` on PATH. The existing `windows-mingw-*` presets stay unchanged for local use.
- The option `HATTEDA_WERROR` (default OFF) sets `CMAKE_COMPILE_WARNING_AS_ERROR`. CI enables it for Debug and Release.
- Steps: configure and build Debug with tests, `ctest --preset ci-debug` with `QT_QPA_PLATFORM=offscreen`, then configure and build Release with `BUILD_TESTING=OFF`.
- QtTest output does not reach CTest on Windows, so QtTest executables are registered with `hatt_add_qt_test`, which writes `<build>/test-logs/<test>.txt`. On failure CI prints these logs and uploads them, together with CTest's log and JUnit file, as an artifact.

## Consequences

- Every PR to `main` is built in Debug and Release and tested. New warnings fail CI, so code must build warning-free with MinGW.
- A Qt version bump must update `QT_VERSION` in the workflow along with the local presets.
- clang-format checking, MSVC or Linux jobs, and packaging are not part of this decision.
