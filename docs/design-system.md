# HattEDA desktop design system

## Direction

HattEDA uses a modern engineering-workstation visual language. The interface prioritizes long-session comfort, canvas contrast, information density, and predictable plugin surfaces over decorative dashboard patterns.

## Tokens

- Canvas: `#0b1016`
- Application surface: `#11161d`
- Elevated surface: `#18212a`
- Border: `#26313d`
- Primary text: `#e8edf2`
- Secondary text: `#7f8d9a`
- Brand and selection: `#18b6a4`
- Copper geometry: `#f4a261`
- Secondary layer geometry: `#62b6ff`
- Corner radius: 4 px for dense controls and 8 px for canvas surfaces
- Base spacing: 4 px, composed primarily as 8, 12, 16, 24 px

## Patterns

Primary workspaces use document-style tabs above the canvas. Commands occupy a compact horizontal strip. The left edge combines a narrow tool-mode rail with one contextual browser; HattEDA does not surround the canvas with permanently open Project, Inspector, and Output docks. Full workflow tools replace the central canvas through Tool Workspace Host instead of splitting it. Commands use text labels until a coherent icon set is introduced; arbitrary Unicode symbols must not become the permanent icon system.

Selection always combines color with a structural cue such as a left rail or top tab border. This keeps workspace state distinguishable when color perception is limited.

Plugin-provided UI must inherit the application palette and typography. Plugins provide content widgets and descriptors; they do not restyle the application shell.

The schematic and PCB canvases derive their surface and grid contrast from the active application palette. Dark and light themes are first-class settings. User-facing strings must use Qt translation contexts; compiled translations are loaded from the application resources according to the saved language preference.

Placement and drawing share a collapsible bottom alignment strip. Grid, object, edge, center, 45-degree, and orthogonal snapping are independent states. Alignment and distribution commands appear only in this strip so they do not permanently consume the primary command bar.
