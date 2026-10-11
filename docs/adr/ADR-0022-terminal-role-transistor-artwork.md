# ADR-0022: Terminal-role transistor artwork

- Status: Accepted
- Date: 2026-10-11
- Related: #60, #64, ADR-0020

## Context

The built-in transistor entries reused one NPN drawing, including PNP and MOSFET
parts. Packaged parts have different terminal orders, so a drawing's base or
emitter could disagree with its named electrical terminal. The DC adapter already
uses terminal names; the picture must agree with those names.

## Decision

Generate each drawing from its C/B/E or D/G/S roles while retaining the existing
pin numbers, coordinates, device IDs and footprint pad mappings. Generic NPN
pins receive B/C/E names in their existing 1/2/3 order, matching the existing
adapter's collector/base/emitter mapping of 2/1/3.

BJT emitter arrows distinguish NPN and PNP. Enhancement MOSFET drawings have an
insulated gate, a broken channel and a source-connected body arrow that
distinguishes NMOS and PMOS. No body diode is drawn: the current generic solver
does not implement that additional device behavior.

The JSON resource remains the runtime source. `scripts/Update-TransistorArtwork.py`
is a standard-library-only authoring helper, with no application or build-time
Python dependency. This change requires no project migration or persistence
change. Existing wires and board pad mappings retain their endpoints.

Manufacturer pin-role references include [BC547](https://www.onsemi.com/pdf/datasheet/bc546-d.pdf)
and [2N7000](https://www.onsemi.com/download/data-sheet/pdf/nds7002a-d.pdf).
These references do not calibrate the generic solver or validate footprint dimensions.

## Validation

Nine device cases check fixed pin coordinates, the control terminal, arrow
polarity and MOSFET gate isolation. Existing schematic-to-solver cases check all
nine terminal mappings, current signs and project round trips. A contact sheet
supports visual review of all nine generated drawings.
