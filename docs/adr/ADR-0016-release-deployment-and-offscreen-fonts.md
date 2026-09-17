# ADR-0016: Release Deployment Script and Offscreen Test Font Directory

**Status:** Accepted
**Date:** 2026-09-16
**Issue:** #5

## Context

`windows-mingw-release` built a `hatteda.exe` that only ran on a machine with the exact Qt/MinGW
kit on `PATH` — nothing bundled the Qt DLLs, MinGW runtime, or Qt plugins (platform, image format,
TLS, etc.) it needs. Release builds were configured and compiled by CI, but never verified to
actually run standalone; issue #5 asked for that gap to be closed without adding a new dependency.

Separately, every offscreen test run (`QT_QPA_PLATFORM=offscreen`, used by `tests/unit` and by the
release smoke test below) printed:

```
QFontDatabase: Cannot find font directory C:/Qt/6.11.2/mingw_64/lib/fonts.
Note that Qt no longer ships fonts. Deploy some (from https://dejavu-fonts.github.io/ for example)
or switch to fontconfig.
```

The `offscreen` platform plugin uses Qt's basic/FreeType font database, which — unlike the native
`windows` platform plugin — needs real font files at a path it can enumerate; Qt 6 stopped bundling
any. Tests still pass (it is a `QWARN`, not a failure), but the noise obscures real warnings.

## Decision

### Deployment: `qt_generate_deploy_app_script`

`apps/hatteda-desktop/CMakeLists.txt` now calls Qt's own `qt_generate_deploy_app_script` (Qt 6.3+,
no new dependency — it ships with the Qt CMake package already required) and installs the
generated script:

```cmake
install(CODE "set(QT_DEPLOY_BIN_DIR \".\")")
qt_generate_deploy_app_script(
    TARGET hatteda
    OUTPUT_SCRIPT hatteda_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${hatteda_deploy_script})
```

This runs `windeployqt` (via Qt's `qt6_deploy_runtime_dependencies`) as part of `cmake --install`,
copying the Qt/MinGW DLLs and the plugins `hatteda` actually uses into the install prefix.

- **`QT_DEPLOY_BIN_DIR` is forced to `"."`, not its `"bin"` default.** The deploy script runs as
  its own `cmake -P` at install time, so this must be set with `install(CODE ...)`, not a
  configure-time variable — a plain `set(QT_DEPLOY_BIN_DIR ".")` before the call has no effect.
  Without this, DLLs land in `<prefix>/bin/` while `hatteda.exe` (via the existing
  `install(TARGETS hatteda RUNTIME DESTINATION .)`) sits in `<prefix>/`, and Windows' default
  same-directory DLL search never finds them — confirmed by running the deployed exe with a
  minimal `PATH`, which exited immediately until this was fixed.
- The deployed folder ships only the `windows` platform plugin: that is what an end user needs.
  `offscreen` is added only for CI's own smoke test (see below), copied in after deployment, not
  part of the shipped bundle.
- `hatteda-embedded-agent` deploys alongside `hatteda` (`qt_generate_deploy_app_script` runs once
  per target that needs it; both console tools share the install prefix already).

### CI: deploy, smoke test, upload

`ci.yml`'s Release job now runs `cmake --install` after the Release build, then launches the
deployed `hatteda.exe` with `PATH` reduced to `C:\WINDOWS\system32;C:\WINDOWS` (no Qt, no MinGW) to
prove it is self-contained: if the process exits within 5 seconds, the job fails. The deployed
folder is uploaded as a build artifact (`hatteda-windows-mingw-release`, 14-day retention) either
way, so a red smoke test still leaves the broken deploy available to inspect.

### Offscreen tests: `QT_QPA_FONTDIR`

Both the `debug` and `ci-debug` `ctest` presets in `CMakePresets.json` now set
`QT_QPA_FONTDIR=$penv{SystemRoot}/Fonts` (`C:\Windows\Fonts` in practice) alongside the existing
`QT_QPA_PLATFORM=offscreen`. This points Qt's basic font database at Windows' own installed fonts
— no font files are added to the repository or shipped with `hatteda`; this only affects the
offscreen *test* environment, matching what issue #5 asked to "consider" for CI. Verified: the
`QFontDatabase: Cannot find font directory` warning no longer appears in any `tests/unit` log.

## Consequences

- A real Windows install of HattEDA still only needs the `windows` platform plugin; `offscreen`
  never ships to end users.
- If `hatteda` starts using more Qt modules (e.g. Qml, Multimedia), `qt_generate_deploy_app_script`
  picks up their plugins automatically the next time it runs — no manual plugin list to maintain.
- `QT_DEPLOY_BIN_DIR` must stay `"."` as long as `install(TARGETS hatteda RUNTIME DESTINATION .)`
  does; if that destination ever changes, this must change with it or the deploy breaks silently
  (the install step still "succeeds" — it just produces an exe that cannot find its DLLs).
- The CI smoke test only proves the process survives 5 seconds under `offscreen` with no window
  shown; it is not a substitute for issue #2's still-pending manual desktop QA pass.
