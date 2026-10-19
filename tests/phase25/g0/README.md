# Phase 25 Team G G0 fixtures

`check_rust_feasibility.py` is a bounded Rust-only architecture prototype. It
compiles a compiler-owned shared Map runtime, one provider-owned generic nominal
record with mechanical public backend fields, two independent consumer crates
containing consumer-generated free functions accessing those fields directly,
and a final executable that passes the same record and Map values through both
consumers. The Map exercises custom static `isequal`/`hash` semantics, insertion,
lookup, overwrite, deletion, and an eager owned keys snapshot. It also proves the
Rust orphan-rule failure that a direct foreign key implementation must avoid, and
compiles the preserved accessor/private-field experiment as an evaluated alternative
verifying `E0616`.

Run it from the repository root:

```sh
python3 tests/phase25/g0/check_rust_feasibility.py
```

`conformance/*.moss` are forward Phase 25 Option B inputs and negative visibility
probes (including `private_field_visibility_negative.moss`). They are deliberately
not in `tests/run.sh` at G0: the pinned ABI-v9 baseline rejects open-result
functions at their declarations and does not yet have Phase 25 visibility rules.
G-B/G-D should promote these cases into their permanent checker, native, Fast
Debug, and source-free ABI-v10 matrices as the compiler milestones land. The
expected outcomes are specified in `docs/phase25/TEAM_G_G0_FEASIBILITY.md`.
