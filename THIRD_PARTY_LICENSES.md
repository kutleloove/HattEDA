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
- **Bundled binary releases:** when the optional `HATTEDA_FREEROUTING_JAR` and
  `HATTEDA_JAVA_RUNTIME` paths are provided, `cmake --install` places the JAR, runtime and an
  explicitly named `FREEROUTING-LICENSE.txt` below `autorouter/`. The HattEDA GitHub release must
  also publish the matching Freerouting source archive beside the Windows bundle. Version
  `0.1.0-alpha.1` uses Freerouting 2.4.1, source tag
  [v2.4.1](https://github.com/freerouting/freerouting/tree/v2.4.1).

## Eclipse Temurin

- **Project:** [Eclipse Temurin](https://adoptium.net/temurin/) Java runtime.
- **Version in `0.1.0-alpha.1`:** Temurin JRE 25.0.4.1+1 LTS, HotSpot, Windows x64.
- **License:** GPL-2.0 with Classpath Exception and third-party notices. The unmodified runtime's
  `NOTICE`, `release`, and per-module `legal/` directories are shipped inside
  `autorouter/runtime/`.
- **How HattEDA uses it:** solely to execute the separate Freerouting JAR; HattEDA itself is not
  linked to the Java runtime.

No third-party binary is committed to this source repository; release packaging supplies the
pinned files at configure time.
