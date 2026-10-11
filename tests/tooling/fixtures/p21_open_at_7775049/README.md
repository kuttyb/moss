# Phase 21 — still failing at `7775049`

Exact programs from the external review that still fail, plus two chunk-pipeline
cases. "Native" = `moss F -o out.rs && rustc out.rs -o out && ./out`; "FD" =
`moss run --interp F`. D and E need data files: `printf 'ABABC' > p4.bin`,
`printf 'ABCD' > p6.bin`.

| File | Register ID | Spec says | Observed at 7775049 |
|---|---|---|---|
| A_trailing_method_raise_lost | EXT-03 (marked FIXED) | `main` rejected: `via_local` raises `E.A` | `moss effects via_local` → `raise_set []`; check ok; codegen "checked raising method call lacks a Phase 21 outcome context"; FD terminates. `return b.poke()` instead of a trailing `b.poke()` is detected correctly. |
| B_negated_float_echo | EXT-12 (marked FIXED) | native = FD = `-2.0 -1.0 -2.0 -2.0 -1.0 -0.0 -1e16` (§13.2) | native `-2 -1 -2.0 -2 -1.0 -0 -10000000000000000` |
| C_grouped_bare_reraise | EXT-17 (marked NOT-REPRODUCIBLE) | ok: only `E.A` can reach `recover E.A, E.B: raise` | `main` totality error demanding `E.B` |
| D_chunk_placeholder_mapper_raise | (part of original pipeline finding) | prints `-1` (typed `on_fail`) | check ok; rustc E0277 at O0 and `-Oshared-memory`. `map(checked)` without `_` works. |
| E_chunk_reduce_seed_raise | (part of original pipeline finding) | prints `-1` | check ok; rustc E0277 |
| F_sqrt_float_local | pre-existing, Float parity | prints `4.0 4.0 1.4142135623730951` | rustc E0689 (`let a = 2.0` has no f64 type; `(a).sqrt()` is ambiguous) |
