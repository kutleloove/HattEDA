# ADR-0014: External autorouter through Specctra DSN/SES

Status: Accepted for incremental implementation.

HattEDA needs whole-board automatic routing, but routing search must not become UI event-handler
logic or replace HattEDA's netlist, connectivity and design-rule authority. Freerouting is a
GPL-3.0 external engine with a documented Specctra DSN input and SES output boundary. HattEDA is
also GPLv3, but bundling and attribution remain separate release concerns.

## Decision

- Define a replaceable external autorouter boundary. The first backend targets local Freerouting;
  the project document never stores Freerouting objects or settings.
- Export a transient Specctra DSN snapshot and import a transient SES result. Temporary interchange
  files are not `.hatt` project data and add no persistent-format fields.
- The router proposes tracks and vias. HattEDA remains authoritative for schematic net membership,
  pin-to-pad mapping, layer support, connectivity and DRC.
- Reject unsupported geometry instead of silently omitting it. Initial export accepts one closed
  outline and an unrouted board; existing tracks, vias, standalone pads and zones are added in
  later tested slices.
- Treat every closed Board Edge shape as one physical PCB region. Multi-board routing runs one
  isolated job per region and combines validated results; it never creates copper across the gap.
  A footprint outside every region or intersecting more than one region blocks the operation.
  Shared schematic nets remain project-level connections through explicit headers/connectors.
- Run the engine out of process with cancellation, a time limit and captured diagnostics. Import
  into a candidate document, validate it, show the result, and commit accepted routing as one undo
  operation. A failed or cancelled run does not change the project.
- Keep the backend optional and local. HattEDA continues to build and run without Java,
  Freerouting, network access or a downloaded service.

## License boundary: external process, not linking

Freerouting is GPL-3.0. HattEDA is also GPL-3.0 ([LICENSE](../../LICENSE)), so even a combined
work would be license-compatible, but the integration is deliberately designed to stay outside
that question entirely:

- `hatt::ui::FreeroutingRunner` (`libs/ui-shell/src/FreeroutingRunner.cpp`) starts Freerouting's
  `.jar` as an independent OS process (`QProcess`, `java -jar ...`). HattEDA and Freerouting run
  as two separate programs communicating only through temporary Specctra `.dsn`/`.ses` files on
  disk — no Freerouting header, class or object file is compiled into, statically linked with, or
  otherwise combined into the `hatteda` binary or this repository's source.
- No Freerouting source, `.jar`, or Java runtime is committed to this git repository. The optional
  release-packaging bundle (`HATTEDA_FREEROUTING_JAR` / `HATTEDA_JAVA_RUNTIME` CMake cache
  variables in `apps/hatteda-desktop/CMakeLists.txt`) copies packager-supplied paths into the
  *build output* directory only, and both default to empty — a plain development build has no
  Freerouting dependency at all, matching "HattEDA continues to build and run without Java,
  Freerouting, network access or a downloaded service" below.
- Tests that need a real Freerouting installation (`FreeroutingRoundTripTests.cpp`) read its path
  from `HATTEDA_FREEROUTING_JAR`/`HATTEDA_FREEROUTING_JAVA` environment variables and `QSKIP` when
  unset, so CI (which has neither) never depends on it; `FreeroutingRunnerTests.cpp` instead uses
  an in-repo fake executable (`FreeroutingFake.cpp`) that speaks the same CLI shape.
- Attribution and the GPL-3.0 text pointer live in [`THIRD_PARTY_LICENSES.md`](../../THIRD_PARTY_LICENSES.md)
  (repo root) and the README's "Üçüncü taraf araçlar" section. Whoever performs an official release
  bundle (filling in the two cache variables above) is responsible for shipping Freerouting's own
  `LICENSE` and a source offer alongside that specific bundle, per GPL-3.0 §6 — that packaging step
  happens outside this repository and outside this ADR's scope.

## Validation and next slices

Golden DSN fixtures cover outline coordinates, layers, padstacks, placement, nets and rules. A
real-engine smoke test exercises export, Freerouting 2.4.1 on its bundled Java 25 runtime, SES
import and final HattEDA DRC when the test environment paths are set. The local process runner uses
a private temporary directory, headless/offline arguments, cancellation, a time limit and captured
diagnostics. **Circuit → Auto Router...** presents HattEDA's routing settings, then uses the
packaged engine without showing its UI. It runs
behind a cancellable progress dialog, rejects malformed output, incomplete nets or blocking DRC
issues, and applies valid tracks/vias as one undo operation. A local JAR picker is available only
as a developer fallback when the package is absent. Next slices add existing copper, keepout and
zone export plus configurable routing passes and timeout in the UI. Multi-board export will
partition footprints by containment and run each physical board as a separate routing job.

Windows release builds provide `HATTEDA_FREEROUTING_JAR` and `HATTEDA_JAVA_RUNTIME` at configure
time. CMake copies them beside HattEDA under `autorouter/`; runtime discovery prefers this bundle,
so users install neither Java nor Freerouting and never see the external router UI. Manual JAR
selection remains only as a developer fallback.
