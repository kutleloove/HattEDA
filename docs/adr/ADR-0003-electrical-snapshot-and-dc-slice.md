# ADR-0003: Transient electrical snapshots and a bounded DC slice

Status: Accepted for the interim editor implementation; refs #7, #9, #10, #11, #20, #21, #22.

The current editor stores geometric sketches. Connectivity and simulation must not grow into UI event handlers or depend on Qt Widgets. The permanent document/identity/transaction contracts remain separate work.

## Decision

- Add `HattEDA::Electrical`, a C++20 library with no Qt or third-party dependency. The UI adapter supplies immutable pin/wire/junction/name snapshots. Coordinates are transient floating-point millimetres with a 1e-6 mm comparison tolerance, not a new file format or the final HATT-003 fixed-point representation.
- Each sketch item receives a session UUID. Undo retains it; duplication generates a new one. PCB footprints link to schematic UUIDs, never vector indexes or editable references. These fields, values, footprint assignments and explicit pin-to-pad permutations were in-memory only until #6; ADR-0004 now persists them in the `.hatt` file. HATT-002/004 will replace the provisional identity/snapshot transaction mechanics.
- Schematic wire endpoints join other wires (including T joins); collinear overlaps conduct. Internal crossings need a pin, junction or named-node anchor. Equal names join globally, conflicting names are diagnosed; `0` is ground. Board tracks currently share one copper layer, so crossing tracks conduct. Clearance, pad areas, vias and multilayer connectivity are not full DRC and must not be advertised as such.
- PCB update is a single undoable edit, preserves existing placement and tracks, and rejects changed footprint/mapping or obsolete/duplicate source links pending explicit review. Ratsnest is a shortest-link guide between disconnected copper groups for each schematic net; copper joins across distinct schematic nets are reported as shorts.
- Use a small in-process modified nodal analysis solver for resistors and independent DC voltage sources. There is no downloaded engine or licensing/distribution dependency. This is deliberately not SPICE compatibility or a general simulator. AC/transient/nonlinear components require later decisions and issues. Unsupported elements are errors.
- Update (#22): the DC operating point also models capacitors as open circuits and inductors as 0 V sources whose current is reported. Nets reached from ground only through capacitors get a 1e-12 S tie to ground (SPICE GMIN); nets with no path at all are still rejected. The adapter leaves nets that no element touches (lone probes, unused ports) out of the solve and maps results back (`CircuitSnapshot::dcNets`). Diodes, transistors, op-amps and ICs remain unsupported errors.
- Interactive simulation (#22): `hatteda.action.simulation-start` (F12) / `simulation-stop` (Shift+F12), shown as code-drawn play/stop command bar buttons, keep a live mode in which every schematic edit cancels the current solve and re-solves after a 150 ms debounce. Results are shown as read-only `DesignCanvas` annotations at voltage probe pins, not stored in the document; errors open the results workspace and stop the live mode. One-shot "Run DC operating point" keeps the out-of-date behaviour below.
- Solve on a `QThread` from copied data, maximum 256 unknowns. Atomic cancellation is checked during work; a 10-second timer requests cancellation. Closing the UI leaves no worker references to Qt widgets. Every schematic edit cancels an active solve and invalidates displayed results; completion is checked against the schematic revision.
- `MainWindow` owns menus and report workspaces. `CircuitWorkflow` coordinates host-owned actions, adapter calls and results. Canvas emits context-menu intent; the host constructs the menu. Right-click cancellation replaces the old right-click path-commit behavior.

## Validation and remaining work

Connectivity tests cover disconnected pins, T joins, unmarked/marked crossings, overlap, names and invalid data. DC tests use analytic divider/current results, parser edge cases, invalid/floating/singular circuits and cancellation. Adapter/workflow tests cover actual rotated symbol pins, PCB idempotence/undo, preserved routing, changed mappings, UUID duplication and asynchronous results.

External netlist formats, persistent project data, general SPICE model mappings, automatic routing, comprehensive ERC/DRC and large-design indexing remain unimplemented. Revisit the adapter when HATT-002/003/004 land; the electrical library must remain independent of the UI shell.
