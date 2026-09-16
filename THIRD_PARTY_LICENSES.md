# Third-party notices

HattEDA is licensed under the [GNU General Public License v3.0](LICENSE). It optionally invokes
the following third-party program as a separate, external process (never linked into or
distributed as part of the `hatteda` binary's source):

## Freerouting

- **Project:** [Freerouting](https://www.freerouting.org) — an open-source automatic PCB router
  ([https://github.com/freerouting/freerouting](https://github.com/freerouting/freerouting)).
- **License:** GNU General Public License v3.0 (GPL-3.0).
- **How HattEDA uses it:** `hatt::ui::FreeroutingRunner` (`libs/ui-shell`) starts Freerouting's
  packaged `.jar` as an independent OS process via `java -jar` (see ADR-0014,
  "License boundary: external process, not linking"). HattEDA writes a temporary Specctra `.dsn`
  file, Freerouting reads it and writes a temporary Specctra `.ses` file, and HattEDA reads that
  back. No Freerouting source or object code is compiled into, statically linked with, or
  distributed inside the `hatteda` source repository or its git history — the `.jar` is supplied
  separately (a developer-provided path during development, or a release packaging step outside
  this repository), never vendored here.
- **If a HattEDA distribution bundles the Freerouting `.jar`:** that packaging step must include
  Freerouting's own `LICENSE` file (GPL-3.0) and a written offer of its corresponding source (or
  the source itself), alongside the bundle, per GPL-3.0 §6. This repository does not perform that
  packaging; see `apps/hatteda-desktop/CMakeLists.txt`'s optional `HATTEDA_FREEROUTING_JAR` /
  `HATTEDA_JAVA_RUNTIME` cache variables, which are empty by default and are the packager's
  responsibility to fill in and accompany with the required notices.

No other third-party source is vendored in this repository at this time.
