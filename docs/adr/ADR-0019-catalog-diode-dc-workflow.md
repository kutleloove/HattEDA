# ADR-0019: Catalog diode models in the desktop DC workflow

**Status:** Accepted
**Date:** 2026-10-10
**Related:** #22, #62; ADR-0003, ADR-0017. PR #74 remains a separate solver extension.

## Context

The electrical core already solves diode, LED and Zener models, but the catalog declared them
unsupported and SketchCircuit only emitted linear elements. Enabling a catalog flag alone would
incorrectly fall through to a resistor and would omit nonlinear terminal nets from the compact
snapshot. Packaged diodes can also have an NC terminal: BAT54 has A/NC/K, not two adjacent pins.

## Decision

- Enable only `nonlinear.diode`, `nonlinear.led` and `nonlinear.zener` in this slice. BJT, MOSFET,
  JFET, photodiodes, op-amps and transient models keep their explicit unsupported diagnostics.
- The UI adapter constructs immutable `electrical::NonlinearElement` DTOs. Core remains Qt-free;
  the async worker, cancellation flag and revision checks remain the existing workflow.
- Resolve A/K from built-in catalog pin metadata. Legacy unnamed two-pin symbols and project
  two-pin diode models use pin 1 = anode, pin 2 = cathode. Additional catalog terminals must be
  declared NC and must not be wired. Keep original pin numbers in diagnostics.
- Include both linear and nonlinear terminals in reachability checks and DC net compaction;
  leave unrelated nets and unused NC terminals outside the solve. Prefer the first voltage
  source's negative net for implicit ground, then a linear element, then a diode's cathode.
- Report linear currents separately from nonlinear current **into the anode**; reverse Zener
  current is negative. LED `nonlinearAux` remains available for future animation (#64).

## Model defaults and limits

Parameters come from the model catalog, not from the component label or part-number string:

| Model | Ordered core parameters (SI) |
| --- | --- |
| Diode | Is = 1e-12 A, n = 1, Rs = 0 ohm |
| LED | Is = 1e-18 A, n = 2, Rs = 0 ohm, rated current = 0.02 A |
| Zener | Is = 1e-12 A, n = 1, Rs = 0 ohm, breakdown = 5.1 V, slope = 5 ohm |

These are generic educational defaults, not manufacturer-specific datasheet models. A BAT54 or
1N4148 label does not imply calibrated device parameters; changing a Zener label does not change
its 5.1 V breakdown. The result panel states this limitation. This slice adds no persistent fields
and no parameter editor. Per-device parameter overrides, LED colors/animations, temperature and
AC/transient remain separate work. No part datasheets or third-party model files are bundled.

## Validation

Schematic adapter tests solve forward diode/LED circuits against an independent scalar Shockley
plus resistor reference and reverse Zener against the breakdown/slope formula (within 2%). They
cover aliases, BAT54 NC mapping, compact nets, project serialization, a nonlinear-only circuit,
floating sections, and actual pin-number diagnostics. A real asynchronous CircuitWorkflow test
checks nonlinear current in the report and invalidation after undo. Existing unsupported models
still report their limitation rather than silently being omitted.
