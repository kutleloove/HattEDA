# ADR-0018: Nonlinear DC Operating-Point Models

**Status:** Accepted
**Date:** 2026-09-18
**Extends:** ADR-0003 (electrical snapshot and bounded DC slice)
**Issue:** #62 (epic #60)

## Context

ADR-0003's DC solver (`libs/electrical`, `solveDc`) only modelled resistors, independent DC
sources, open capacitors and shorted inductors — a plain linear MNA system solved once. Issue #62
asked for diode, LED, zener, BJT, MOSFET, op-amp and "basic logic gates and a D/JK flip-flop"
operating-point models, so the built-in library's semiconductor and IC parts can actually be
simulated (`hatteda.action.simulation-start`, F12).

This was delivered as three PRs, kept intentionally narrow (`libs/electrical` and its own tests
only — no `ComponentCatalog`/`SketchCircuit` wiring, which follows once #61(b)'s catalog
restructuring lands, to avoid a merge conflict there):

- **PR #70**: Newton-Raphson infrastructure, diode, zener, LED.
- **PR #71**: BJT (Ebers-Moll) and MOSFET (level-1).
- **PR #73** (this one): op-amp, logic gates, and a warm-start capability. Closes #62.

## Decision

### Newton-Raphson infrastructure (PR #70)

Every nonlinear device is linearized at a per-element guess (one or two scalars, kind-dependent)
into a Norton-equivalent companion model — a conductance plus an equivalent current source — and
stamped into the same MNA matrix the linear elements use, following the sign convention already
established for `DcKind::CurrentSource` (a value stamped as `a[p][n] -= value; a[m][n] += value;`
represents current flowing from `p` to `m` through the branch). One outer Newton iteration per
`solveDc` call: re-stamp at the current guess, solve, extract the new guess from the solution,
apply a per-kind step limit, repeat until the guess stops moving (`voltageTolerance`, 1e-9 V) or
`maxIterations` (300) is exhausted. If plain damped Newton fails, two continuation fallbacks are
tried in order, each reusing the previous stage's converged guess: **source stepping** (ramp
independent sources from 0.1% to 100%) and **gmin stepping** (anneal an extra parallel conductance
at every node from 1e-1 down to 0). If none of that converges, `solveDc` returns a clear failure
(a message, `voltages`/`currents` empty) — it never crashes or hangs.

A 2-terminal element with a nonzero series resistance gets a solver-internal auxiliary node
between the resistor and the ideal junction, appended after the caller's own nets; callers never
see this index. The pre-#62 linear-only path (`circuit.nonlinear` empty) is untouched: a single
`gaussianSolve` call, byte-for-byte the same behaviour as before this epic.

### Diode, Zener, LED (PR #70)

Shockley equation, `Is·(exp(V/(n·Vt)) - 1)`, `Vt` fixed at 0.025852 V (kT/q, ~300 K). Parameters
`{Is, n, Rs}`. Zener adds a linear breakdown branch below `-Vz` (`{Is, n, Rs, Vz, Rz}`), continuous
with the forward/leakage branch at the knee. LED reuses the diode equation exactly (same physics,
different named use) with a `ratedCurrent` parameter (`{Is, n, Rs, ratedCurrent}`) purely for the
brightness output below — there is no separate "LED equation".

### BJT, MOSFET (PR #71)

BJT: classic Ebers-Moll transport model, junction ideality fixed at 1, `{Is, BetaF, BetaR}`. PNP
reuses the NPN equations evaluated at the mirrored junction voltages (`Veb`, `Vcb` instead of
`Vbe`, `Vbc`); only the reported currents and the linearization constants flip sign — the four
companion-model conductances are identical in either frame, derived and cross-checked
independently for both signs (this is the easiest place to get a sign wrong; two independent PNP
derivations that agreed with each other, not a "negate the NPN formula" shortcut, is what actually
shipped).

MOSFET: level-1 square law, no channel-length modulation, `{Vto, K}` (`K = kp·(W/L)/2`). PMOS
reuses the NMOS equations with source and drain swapped and `Vto`'s sign flipped. The gate carries
no current at all (ideal, no leakage) in either device — it must reach ground through some other
element, or the circuit reports it as a floating net, exactly like any other undriven net.

### Op-amp (PR #73)

Ideal, ADR-0003-style bounded model: infinite input impedance, a large but finite open-loop gain,
and a smooth `tanh` saturation between two supply rails. Deliberately **not** a fifth and sixth
terminal wired to the schematic (`{gain, outputConductance, Vpos, Vneg}` — the rails are fixed
parameters, not circuit nets). This keeps the model at the same 2-guess-scalar complexity as every
other kind here instead of requiring a new class of solver-tracked supply-rail unknowns for a
single device kind; it matches how many SPICE behavioural op-amp macromodels also take `VCC`/`VEE`
as subcircuit parameters rather than pins. **Wiring the rails to real supply nets is future work**
if a circuit ever needs them to vary (e.g. a rail derived from another part of the same schematic).

Modelled as a Norton-equivalent VCVS rather than a true ideal voltage source: a large output
conductance (`outputConductance`, e.g. 1000 S ≈ 1 mΩ) pulls the output toward
`Vmid + Vswing·tanh(gain·diff/Vswing)` (`diff = Vplus - Vminus`, `Vmid`/`Vswing` from the rails).
Because the algebra reduces so that the linearized current source's value depends only on `diff`,
not on the output voltage itself, this needs no auxiliary MNA unknown — it reuses the same 2-scalar
per-element guess machinery as every other kind. Reported current is 0 (ideal, no current into
either input).

### Logic gates (PR #73)

One `NonlinearKind::LogicGate` with a `LogicFunction` parameter (Not/And/Or/Nand/Nor/Xor) instead
of six enum values, matching how `DcKind::Resistor` takes a value rather than the model gaining a
new kind per resistance. Parameters: `{function, Vth, steepness, Vol, Voh, outputConductance}`.
Each input is turned into a smooth "confidence" `s = tanh((Vin - Vth)·steepness)`; the six
functions combine one or two such confidences (`min`/`max`/products, matching And/Or/Xor) into a
target output level via the same Norton-equivalent technique as the op-amp — again no auxiliary
unknown needed, and again both terminals carry no current (ideal). `nonlinearAux` reports the
output voltage normalized to `[0, 1]` between `Vol` and `Voh` — a "digital level" #64's animated
symbols can read directly, the same slot LED brightness uses.

**Numerical note:** a very steep transition (large `steepness`) narrows the region where the
tanh's derivative is non-negligible to `~1/steepness` volts. The per-iteration step limit for
these two kinds is sized off that width (`10/steepness` for gates, `Vswing/gain` region reasoning
generalized to `Vswing/10` for the op-amp, since its gain is high enough that the derivative-width
approach would need thousands of iterations to cross into saturation) — too large a step jumps
clean over the sensitive region and oscillates instead of converging; too small a step needs
excessive iterations to reach a saturated operating point.

**Symmetric-circuit nudge:** two identically-parameterized gates (or op-amps) in a symmetric
feedback loop — a latch is exactly this — linearized exactly at their unstable symmetric point
produce a genuinely singular Jacobian, not a solver bug. Both kinds' output row gets a fixed,
element-index-alternating ±1 nanosiemens conductance to ground (~1 gigaohm) folded in. This is
negligible next to any real load or either kind's own `outputConductance` (never measurably
changes a non-degenerate circuit's answer, confirmed by the existing op-amp/gate tests' tight
tolerances) while reliably breaking exact ties between identically-parameterized elements. It is
the same idea as the existing per-node floating-net `gmin` (1e-12 S, ADR-0003) — an intentionally
tiny stabilizing conductance a real circuit never notices — just applied per nonlinear element
instead of per node, since the degeneracy here is between two *elements*, not an undriven node.
1 nS was chosen simply because it sits many orders of magnitude below any realistic
`outputConductance` (≥ 1e-3 S in every model/test here) while staying comfortably above the
solver's own floating-point noise floor; it is not calibrated to any physical quantity.

### Warm start (PR #73)

`DcCircuit::initialVoltages`: an optional, `netCount`-sized copy of a previous `DcResult::voltages`.
When present, every nonlinear element seeds its Newton-Raphson guess from it instead of 0 V.
Ignored (falls back to 0 V, unchanged pre-#62 behaviour) unless the size matches exactly. This is
plumbing only — no new solve algorithm — but it is what makes a **level-sensitive latch built from
cross-coupled gates** (an SR latch, a gated D latch) actually hold its state: `hatteda`'s live
simulation mode (F12) already re-solves on every schematic edit, so passing the previous solve's
voltages back in on each re-solve keeps a bistable circuit on whichever side of its symmetric point
it last settled on, instead of always landing on the same one from a fresh 0 V guess. Verified by
a full set → warm-started hold → reset sequence on a two-NAND SR latch.

Without a warm start, a *perfectly* symmetric hold condition (as in a test, not as floating-point
noise from a real solve will usually produce) may fail to converge within the iteration budget
instead of settling on either stable state — an accepted, tested "never crashes" outcome (a clear
error, not a crash), not a defect: finding a specific side of an unstable equilibrium's basin
boundary from an exactly-symmetric starting guess is a textbook hard case for Newton-Raphson.

### Edge-triggered D/JK flip-flops: explicitly deferred

**Not implemented.** A DC operating-point solve is, by construction, a single algebraic snapshot —
it has no notion of "the previous value of Q" or "a clock edge just occurred." Warm start (above)
lets a solve remember voltages *across separate `solveDc` calls*, which is exactly what a
level-sensitive latch needs (it has no clock; its state is just whichever the feedback loop
settled on). An edge-triggered flip-flop's defining behaviour — Q changes *only* at a clock
transition, not merely because D changed while the clock is high — requires representing time and
detecting an edge within *one* solve, which a bounded DC operating point cannot do without
becoming a transient solver. Modelling one as if it worked via warm start alone would silently
promise edge-triggered behaviour the solver cannot deliver (it would behave like a transparent
latch on the clock's active level instead). This is deferred pending a real transient/timestepping
decision — out of scope for this DC-only epic; issue #62 itself offered this ADR as the place to
record that choice, rather than a half-implemented device.

## Consequences

- `#61(b)`'s catalog changes land first; a follow-up "wiring" PR maps
  `simulationModelCatalog()` entries (`dc.diode`, `dc.led`, `dc.npn`, `dc.opamp`, `dc.gate.*`, ...)
  onto these `NonlinearElement` kinds in `SketchCircuit.cpp`, reading device parameters (`Is`,
  `BetaF`, `Vth`, ...) from the placed part. Until then these models exist in `libs/electrical`
  but are not reachable from the app.
- `#66`'s "basic circuits" templates can include an SR latch or a gated D latch (warm start makes
  them actually simulate); a template built around an edge-triggered flip-flop cannot yet.
- `#64`'s animated symbols can read `nonlinearCurrents`/`nonlinearAux` (LED brightness, logic
  level) once the wiring PR exists.
- The op-amp's fixed-parameter supply rails mean a circuit cannot yet model an op-amp whose supply
  itself sags or is generated elsewhere in the same schematic; revisit if that need appears.
- Tests: `hatt-dc-solver-tests` (`DcSolverTests.cpp`) covers every device against an independent
  reference (bisection or closed-form, never the solver's own Newton-Raphson), parameter/topology
  validation for every kind, several "never crashes" cases, and the SR latch set/hold/reset
  sequence with and without a warm start.
