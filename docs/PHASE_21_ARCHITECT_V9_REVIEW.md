# Phase 21 ABI-v9 independent architectural review

**Exact candidate:** `96eef9e0d554e26139b3e86268faf598fc03b38e` on `phase-21-integration`.

**Architect L6 verdict for this SHA: NO-GO.** A valid synchronous Root message from `main`, whose handler has an exhaustive `on_fail`, is rejected when the message is nested in a binary expression, vector literal or ordinary function argument. The same Root checks as a direct or parenthesized `echo message`. This is a new concrete accepted-syntax failure at the absolute Root boundary. L7 independently reproduced it and is repairing another worktree; that future repair has not been reviewed here.

I checked out the exact candidate in an isolated repository-local worktree, read the two Moss repository skills, built its compiler with `make moss -j2`, and ran `./moss agent bootstrap --json` (`moss-0.1`). The original external peer ZIP and original A–F ZIP were not available for independent byte comparison. The A–F files committed under `tests/tooling/fixtures/p21_open_at_7775049/` are attributed to L7's transcription; their SHA-256 values are pinned and validated by the gate. No implementation or test source was edited in this review.

## New blocker: Root `on_fail` is lost in nested expression analysis

Minimal probe, at `tmp/architect-v9/probes/root_nested_expression.moss` in the isolated worktree:

```moss
enum E:
  Bad
domain Worker:
  fn Fetch() -> Int:
    raise E.Bad
  on_fail E.Bad:
    reply 7
fn main():
  worker = Worker()
  echo 1 + message worker.Fetch()
```

`./moss check ... --json` exits 1 with `MOSS_COMPILE_ERROR: main may not let typed errors escape; handle every raised variant with try/recover`. Replacing the last line with `echo message worker.Fetch()` or `echo (message worker.Fetch())` checks. The identical nested-expression syntax with a nonraising `Fetch` checks, generates Rust, and prints `8` natively; thus nested messages are otherwise supported. With the exhaustive trailer restored, `echo [message worker.Fetch()]` and `echo identity(message worker.Fetch())` also falsely reject. A source-removed ABI-v9 provider `provider.Leaf.Fail()` with exhaustive trailer reproduces the binary form's rejection. Conversely, removing the trailer and placing the nested call inside a `main` `try` correctly reports `ROOT_UNHANDLED_RAISE`; I found no unsafe admission in this probe family.

As a control, wrapping the already total binary expression in `main`'s redundant `try`/`recover E.Bad` makes checking pass; native and Fast Debug both print `8`, never entering that catch arm. The unwrapped source should have the same accepted Root result. This control does not change the §4 rule against an unhandled Root escaping into `main`.

The implementation route is visible in `src/moss.cpp`: `phase21_raise_set_block` passes `statement.message_root_ingress` to `observable_expression_effects`. Its direct `message` case excludes a handler's typed raises when a Root trailer handles them. The binary, vector and ordinary-call recursive descents omit that `root_ingress` argument and use the default `false`; they therefore re-add the pre-trailer raise to `main`'s effect set. Grouping and field projection propagate it, explaining the split result. This is a semantic checker defect, not a request to allow Root raises into lexical `main` recovery. The v4.5 §4 absolute Root contract remains correct.

## ABI-v9 authenticity and cache assessment

I inspected the actual `src/moss.cpp` loader, artifact-selection, archive-marker and build-cache paths, and the `docs/MOSS_SOURCE_FREE_ABI_V9.md` contract. On this SHA the loader hashes canonical `.mossi` bytes excluding the digest record, then scans the selected `.rlib` archive for exactly one module/digest marker in a native-object member rather than accepting a `.rmeta` copy. The explicit-module build re-resolves external imports before warm-cache return, fingerprints selected interface and artifact bytes, and passes the same selected `.rlib` through `--extern`. This binds an untrusted metadata file to a trusted provider build under the stated threat model; it does not authenticate a publisher or protect a simultaneously replaced provider object and marker.

I reran `check_phase21_source_free_abi.py`, `check_phase21_imported_effects.py`, `check_phase21_source_free_cache_binding.py`, `check_phase21_abi_v9_external_cache.py`, `check_phase21_abi_v9_migration.py`, `repro_phase21_mossi_pattern_forgery.py`, and `check_phase21_sourcefree_noop_warning.py`; all passed. These exercise source removal, exact method effects and warnings, recomputed-digest pattern forgery, missing/alternate selected rlibs, direct/transitive warm caches and ABI-v8 migration. The historical ABI-v8 pattern-swap abort and missing warning are **not** reproduced under the ABI-v9 contract on this exact source. A marker/digest check assumes a trusted provider build and stable artifact during checking/linking; the gate does not establish publisher identity or a cryptographic signature.

## Root, effects, ownership, locking and original cases

The following exact-SHA gates passed with the worktree compiler:

| Area | Commands (each invoked with `./moss` unless the gate takes only an output path) | What they establish |
| --- | --- | --- |
| Absolute Root and recovery | `check_phase21_absolute_root_boundary.py`, `check_phase21_root_totality.py`, `check_phase21_peer_recovery.py`, `check_phase21_recovery_semantics.py`, `check_phase21_recovery_native.py` | Direct synchronous/Executor and source-free Root cases, trailer coverage, variant and native/Fast Debug dispatch; they miss the nested expression above. |
| O1 and original peer A/C | `check_phase21_peer_effects.py`, `check_phase21_addendum_a_c.py`, `check_phase21_raising_iterators.py`, `check_phase21_unary_not_effects.py`, `check_phase21_field_projection_effects.py`, `check_phase21_unary_not_speculation.py`, `check_phase21_source_free_standard_write_recovery.py`, `check_phase21_standard_write_order.py` | Reconstructed helper, pipeline, implicit iterator fail-closed, imported WRITE and exact typed variant forms pass. The nested Root failure is an O1/main projection omission, not evidence that the checked handler effects themselves are empty. |
| O4/O8 and early placement | `check_phase21_expression_ownership_edges.py`, `check_phase21_recover_reinit.py`, `check_phase21_on_fail_capture.py`, `check_phase21_source_free_proof_edges.py`, `check_phase21_early_condition_raise.py`, `check_phase21_early_placement_proof.py`, `check_phase21_shared_abi.py`, `check_phase21_borrowed_view_write.py` | Selected exceptional joins, owned failure captures, source-free captured method call, ranked bounded leading-condition split and untouched-guard cancellation pass in tested paths. |
| Synchronization, FileIO and O9 | `check_phase21_handler_failure_native.py`, `check_phase21_lock_fileio_adversarial.py`, `check_phase21_fileio_errors.py`, `check_phase21_fileio_close_full.py`, `check_phase21_fileio_root_cleanup.py`, `check_phase21_chunk_fileio_raise.py`, `check_phase21c_branch_errors.py` | Typed fault/cleanup, old-to-fresh Root guard ordering, chunk raise propagation and concurrent Branch ordering pass in the tested paths. |
| Warnings and standard errors | `check_phase21_peer_warnings.py`, `check_phase21_method_noop_warning.py`, `check_phase21_builtin_error_contract.py`, `check_phase21_sourcefree_noop_warning.py` | Local/method/source-free warning provenance and standard-error shape/identity cases pass. |
| Numeric and original A–F | `check_phase21_float_ieee.py`, `check_phase21_numeric_parse.py`, `check_phase21_original_af.py` | Current Rust/GCC Float oracle and numeric cases pass; attributed original A–F files meet their pinned expected diagnostics/outputs in native O0/O3 and Fast Debug where supported. |
| Phase 20 restriction | `check_phase20_cross_crate_range_abi.py` | Concrete cross-crate `Range` continues to fail closed at Moss checking; this does not constitute native interoperability. |

These tests show the implementation's repaired concrete paths, including the actual ABI-v9 source-free contract. I did not rerun the full `make check`, `make examples`, strict Clang build or four-pair Float matrix in this isolated review. L7's engineering evidence for those gates applies to the same compiler source (`c1ef3eb`), but is a separate provenance from this review. The isolated compiler was built from `96eef9e`, whose later commits add evidence and the attributed A–F files without changing compiler source.

The bounded early-placement implementation cancels only guards absent from the selected path's entire checked footprint; a touched guard remains held to handler completion. The focused three-class/raising-condition and native guard-order witnesses passed. This is a conditional invariant over complete O1 footprints, selected exceptional edges and static closed routing. It is not a proof of a general optimization that releases touched guards mid-execution. Likewise, ABI exceptional-consumer bits plus focused O4/O8 cases do not themselves certify all future grammar shapes; the reviewed engineering transfer argument must remain explicit. The O2 normative wording must continue to allow an `on_fail` signature column to repartition classes and increase normal-path exclusive lock **count** without adding a normal-path per-leaf WRITE.

**Disposition:** The new nested-expression Root rejection blocks acceptance of `96eef9e`. ABI-v9 authenticity, source-free method effects and warning provenance passed their independently rerun adversarial gates under the documented trust boundary. Revalidate the corrected Root expression forms and all affected direct/source-free cases at the exact next implementation SHA before a GO vote; none is granted here.
