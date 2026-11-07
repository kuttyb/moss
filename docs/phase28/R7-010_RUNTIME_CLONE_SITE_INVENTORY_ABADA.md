# R7-010 Runtime Clone Site Inventory (abada candidate)

## Status

This report records observations only. Every runtime site remains `source_site_only_unapproved`; `runtime_callsite_allowlist.json` remains empty. The observations do not authorize Rust `Clone`, `Arc::clone`, or a Moss value copy.

Candidate worktree: `tmp/phase28-r7-010-runtime-clone-provenance-abada`
Base: `abada4f03ac2b13f59ac5b97ce14ce2c29c28ed9`
Candidate SHA: pending local freeze.

The source-site blobs for the ten executor sites were unchanged from the pinned 9cad matrix. FileChunk and FileIO received marker comments only. Inventory rows bind source blob SHA-256, marker byte span and line/column, operator kind, expected expression, and a reviewed generated-type assertion. The v3 sidecar records the exact Rust operator/context span and context hash.

## Site coverage and focused evidence

| Sites | Lifecycle evidence | Focused result |
|---|---|---|
| executor-01, 08, 09 | Exact active-executor and two-worker Arc count; weak reference reaches zero after `join()` | `54-worker-arc-lifetime-through-join` PASS |
| executor-02 | Mutate builder affinity after `config()` snapshot; prior `Vec` remains unchanged | `53-affinity-config-snapshot-independent` PASS |
| executor-03, 06 | Existing direct-count and queued-scope controls | Tests 39, 42, 46, 47 PASS |
| executor-04 | Uncontended and queued fair-lock node references return to zero | `56-fair-mutex-node-arc-drop` PASS |
| executor-05 | Condvar queue holds one extra waiter reference; notify and reacquisition release it | `57-fair-condvar-waiter-arc-drop` PASS |
| executor-10 | Sixteen concurrent result values are each dropped exactly once | `55-runtime-invoke-reply-drop-once` PASS |
| executor-11 | Host ingress clone held through DRAINING admission retry, then released at shutdown | `58-root-ingress-arc-released-after-draining-retry` PASS, 20 repetitions |
| FileChunk / FileIO | Two eligible FileChunk expansions map to one exact source site with generated occurrence indices 0/1; both rows are unapproved. Artifact was built and executed against a three-byte file. | Focused provenance gate PASS; output `3` |

The runtime-02 Moss callability probe found that `Executor.start().config()` is currently accepted by `moss --check` and emitted as `executor.config()` in Rust. This candidate preserves that observed language surface and does not describe the config snapshot as generic non-Moss data.

## Fail-closed evidence checks

`check_phase28_r7_010_runtime_clone_evidence.py` now verifies source blob and marker spans, exact operator/type fields, sidecar schema and coverage, generated operator/context hashes, and mutation rejection for missing, duplicate, wrong-hash, perturbed-span, wrong-type, wrong-operator, dropped FileChunk expansion, and relocated FileChunk marker. It validates both the executor artifact and a FileChunk/FileIO artifact, which includes multiple legitimate expansions of one generator source site.

## Validation

Passed:

- `make --jobs="$(nproc)" moss-dev`
- Focused runtime clone provenance gate in Default and `MOSS_EST=shadow`
- Focused Phase 20C probes 39, 42, 46, 47, and new 53–58 in Default
- New Phase 20C probes 53–58 in `MOSS_EST=shadow`
- `python3 -m py_compile` for the changed Python gates
- `python3 tests/tooling/check_agent_feedback.py`
- `git diff --check`

Known focused gate limits:

- Full `check_phase20_executor_fileio.py` stops before its chunk cases at the base-known `MOSS_INTERNAL_INVARIANT return-value requirement/fact missing or ambiguous` on `tests/phase20_executor_untyped_parameter.moss`.
- `check_phase21c_branch_errors.py` now passes its earlier FileChunk duplicate-expansion/source-free consumer stages, then stops at `phase21_large_read.moss` with the known missing checked-copy/consume authority invariant.
- No full suite or frozen oracle result is claimed for this candidate yet.

## Review requested

Fresh Team C and Agent 2 reviews should check:

1. Exact source marker SHA/span integrity and generated Rust operator/context binding, especially FileChunk `out_slot` allocation and matching `branch_publish(&scope, move || ...)` context.
2. FileChunk multi-expansion coverage and mutation rejection when one observation is dropped or relocated.
3. Whether the lifecycle tests are sufficient for each executor site, including the deterministic Root-ingress DRAINING race.
4. The runtime-02 `config()` callability observation and whether any separate language/API question should be tracked.
5. That no observation in this candidate is treated as clone authorization.
