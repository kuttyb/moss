# Team I architectural blocker: empty-vector diagnostic precedence

**Status:** Resolved by L7 on 2026-10-18, selecting option 1. Implementation
and combined validation are in progress.

## Minimal reproducer

```moss
fn main():
  echo []
```

The exact source is also
`tests/phase25/g2/conformance/unpinned_vector.moss` on the H/T intake.

## Conflicting frozen requirements

- `TYPE_IR_CONTRACT.md` section 4.2 says every reachable unbound value at body
  closure produces `TYPE_AMBIGUOUS_OPEN_RESULT` at its binding/use. The G
  conformance suite requires that exact code for the reproducer.
- `FAST_DEBUG.md` requires bare `echo []` and `echo [[]]` with unpinned element
  types to produce `ECHO_DISPLAY_UNSUPPORTED`. The accepted Team S Architect
  review records ARCH-025 closed with that exact diagnostic in every engine.

Live results:

- H/T exact head `5cfdc1630d67506146e4adcc52628e508ffdc7c5`:
  `TYPE_AMBIGUOUS_OPEN_RESULT` at line 2.
- S-preserving reconciliation scratch:
  `ECHO_DISPLAY_UNSUPPORTED` at line 2.

Both implementations reject the program and preserve the semantic rule that an
unbound collection type is never defaulted. The conflict is diagnostic
precedence and its exact machine-readable code. There is no source/context
distinction available: both frozen tests use the identical standalone program.

## Decision requested

Choose the authoritative diagnostic for an unbound collection directly at an
`echo` boundary:

1. `ECHO_DISPLAY_UNSUPPORTED` at `echo`, while
   `TYPE_AMBIGUOUS_OPEN_RESULT` remains authoritative for other unresolved
   values at closure. This requires a narrow Type IR contract/conformance
   amendment.
2. `TYPE_AMBIGUOUS_OPEN_RESULT` for all unbound values including `echo`. This
   requires amending Team S ARCH-025, its display contract, and exact-code tests.

L7 selected option 1: retain `TYPE_AMBIGUOUS_OPEN_RESULT` for every unresolved
value except an unresolved collection immediately presented to `echo`; that
boundary reports `ECHO_DISPLAY_UNSUPPORTED`. This keeps the no-default/no-`Any`
type rule while applying the operation-specific diagnostic at the display
boundary.

## Integration state

The S scratch branch is being updated to implement the accepted decision. H #5,
H2, G, strict-evidence, source-free harness, and coordinator recovery branches
remain pushed at the SHAs recorded in `.codex/CURRENT_STATUS.md`. Nothing has
been merged to `phase25-integration` or `main` during this decision point.
