# Phase 25 Team G G0 fixtures

`check_rust_feasibility.py` is a bounded Rust-only architecture prototype. It
compiles one provider-owned generic nominal record, two independent consumer
crates containing specialized free functions, and a final executable that uses
both consumers. It also proves the selected accessor visibility boundary and
the Rust orphan-rule failure that a Moss Map key adapter must avoid.

Run it from the repository root:

```sh
python3 tests/phase25/g0/check_rust_feasibility.py
```

`conformance/*.moss` are forward Phase 25 Option B inputs. They are deliberately
not in `tests/run.sh` at G0: the pinned ABI-v9 baseline rejects open-result
functions at their declarations. G-B/G-D should promote these cases into their
permanent checker, native, Fast Debug, and source-free ABI-v10 matrices as the
subsystem lands. The expected outcomes are specified in
`docs/phase25/TEAM_G_G0_FEASIBILITY.md`.
