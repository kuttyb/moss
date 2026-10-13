# `moss.io` feasibility status

`moss.io` is not published by this feasibility branch. Moss v0.1 supports
useful FileIO algorithms inside one native compilation unit, and a Margo
provider can open a path internally when its public signature contains only
ordinary values. A public function that accepts FileIO itself does not compile
across the package boundary because each generated crate owns a distinct Rust
FileIO type. Team H therefore ships executable boundary probes and an
architecture contract instead of prematurely freezing a partial package API.

The checked same-unit surface includes fixed 4096-byte reads, a hard-bounded
65536-byte collect-to-EOF loop, borrowed Range checksums, and explicit-offset
Range writes. A second probe verifies that a path-only provider imports and
runs, and a negative probe preserves the FileIO capability type mismatch. See
[`docs/PHASE_24_HOST_IO_SERVICES_FEASIBILITY.md`](../../../docs/PHASE_24_HOST_IO_SERVICES_FEASIBILITY.md)
for the exact provider/runtime blocker and the smallest proposed repair.

Run the permanent feasibility gate from the repository root:

```sh
python3 sdk/host/io/check_feasibility.py
```

FileIO is native-only. Fast Debug intentionally reports that it has no FileIO
backend; this branch does not claim interpreted I/O parity.
