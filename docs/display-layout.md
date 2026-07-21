# EHAM display layout notes

## Runways modeled

Only the six Schiphol physical runways are modeled (`include/data/eham_runways.h`):

| Index | Physical runway | Name | End A | End B |
|---:|---|---|---|---|
| 0 | 18R/36L | Polderbaan | 18R | 36L |
| 1 | 18C/36C | Zwanenburgbaan | 18C | 36C |
| 2 | 09/27 | Oostbaan | 09 | 27 |
| 3 | 18L/36R | Aalsmeerbaan | 18L | 36R |
| 4 | 06/24 | Kaagbaan | 06 | 24 |
| 5 | 04/22 | Buitenveldertbaan | 04 | 22 |

Geometry is stored as normalized percentage coordinates (`x1_pct`, `y1_pct`,
`x2_pct`, `y2_pct`) and must be transformed into the map viewport at render
time, not hard-coded to screen pixels. Source geometry: MIT-licensed
`archofthings/ha-schiphol-runway-card`.

## Identifier normalization

Before any lookup, a raw heading from an external source is normalized
(`normalizeHeading`):

1. Trim whitespace.
2. Uppercase.
3. Leading zeroes are preserved (`04`, `06`, `09`).
4. Bare `18` is treated as `18C`, and bare `36` as `36C` — Zwanenburgbaan
   aliases used by the reference integration.
5. Anything not matching one of the twelve known ends is rejected by
   `findRunwayEnd` without mutating state.

## Critical movement-direction rule

A runway identifier describes the direction of travel, not just which
physical strip is active:

- Activity on `18R` moves **from the `18R` end toward the `36L` end**.
- Activity on `36L` moves **from the `36L` end toward the `18R` end**.
- The same rule applies to every one of the six physical runway pairs.

Landing and departure differ by color and marker style only — never by
reversing this direction rule. The active end's arrow/chevron must always
point away from the named end and toward its opposite end.

## Independent landing/departure flags

`RunwayEndUse` stores `landing` and `departure` as independent booleans, not a
single enum choice. The same physical runway end can be both a landing end
and a departure end at the same time (e.g. `18C` landing and `18C` departure
simultaneously), and one physical runway can have landing traffic on one end
while the opposite end has departure traffic. Domain-model code must always
set these flags independently and must never use `if / else if` when mapping
`landingRunways` / `departingRunways` arrays onto runway ends.

A parser failure must not partially mutate the currently displayed state:
build a temporary `EhamOperationalState`, validate it, then replace the live
state atomically.
