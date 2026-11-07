# Team C: exact-SHA R7-010 clone provenance audit

**Source SHA:** `8d2de3301e5f2dafef079439b0143a7b6675213e`
**Compiler binary SHA-256:** `26f157f25dc619184ae1d3fcaf52065d5e32720e2727276904e1544c0be9243d`
**Scope:** Read-only independent verification of the frozen ownership oracle and clone source inventory. No source, registry, or allowlist changes were made.

## Oracle result

- The frozen v2 oracle checked 60 fixtures. All **60/60 checker expectations**
  passed, and native Debug and Optimized agreed on completed cases.
- **19/60** passed every gate. **40** failed only clone provenance. ORACLE-45
  failed provenance and has a separate Fast Debug crash because its Root-ingress
  primitive argument lacks an exact closed TypeRef.
- The generated artifact set contains **84 Rust files and 84 sidecars**; all
  sidecar Rust hashes match the corresponding Rust files.
- The manifest covers 1,728 files and has digest
  `bb3d841bbef60275fd8bef5fce9ff91b0162a33677b678cc0b2e474192e916f7`.
  Results JSON SHA-256:
  `101050a884eb78af0804451a0221b3a09f1842abf8502d434ddfded37e1b669e`.
  Clone inventory JSON SHA-256:
  `1db6bb25840bc2c522001bb31361bd88c54d97fc368ce2e6cfd68a9717cd40ed`.

## Reconciled clone counts

| Inventory unit | Count | Interpretation |
|---|---:|---|
| Direct `.clone()` observations | 258 | 252 unclassified runtime rows, 4 capture-only rows, 2 explicit-copy rows. |
| Direct runtime `.clone()` sites | 3 | Same generated runtime snippets repeat in all 84 artifacts, yielding the 252 rows. |
| Root-ingress capture rows | 4 | Two Worker route-handle captures in Debug and Optimized output; facts set `permits_moss_value_copy=false`. |
| Explicit Moss copy rows | 2 | Debug and Optimized observations of ORACLE-10A `copy(v[0])`, linked to exact `TypedCopyFact`/READ authority. |
| Qualified `Arc::clone` observations | 588 | Seven executor-runtime sites repeated in 84 artifacts. These are runtime-handle candidates, not 588 unique source calls. |
| Qualified `Arc::clone` source sites total | 9 | Two FileIO sites are not present in these 84 artifacts. |

The three direct runtime sites are in `src/executor_runtime.cpp`:

- Line 497: owned `Option<Arc<MossGlobalExec>>` snapshot out of process state.
- Line 857: builder `Option<Vec<usize>>` affinity copied into an owned executor
  configuration snapshot. Whether this API is reachable from Moss-generated
  code is not established by these artifacts.
- Line 1407: new owned FFI `MossBranchScope` count from a borrowed opaque handle.

The seven represented Arc sites are executor-runtime lines 58, 152, 438, 733,
919, 1089, and 1116: fair-lock node and waiter handles, ready-queue scope,
worker closure executor, process-global active executor, reply slot, and
ingress executor snapshot. Source shows runtime state/reference-count handles;
the reply slot Arc duplicates the slot handle, not its contained `R` payload.
This source-level rationale is evidence for investigation, not formal approval.

The other Arc sites are `src/file_chunk_codegen.inc:238` (generated file-chunk
branch result slot) and `src/fileio_runtime.cpp:1293` (shared FileIO inner read
handle). Neither pattern occurs in this oracle artifact set.

## Root cause and disposition

The oracle sidecar provenance scanner records method-form `.clone()` calls but
does not attach per-site authority to the three runtime snippets. The runtime
callsite registry is empty. The separate inventory detects qualified
`Arc::clone`, but only lexically; it does not supply type/lifetime authority.
Markers locate source snippets and repeated generated contexts but do not
authorize them. Therefore all 252 runtime direct rows remain unclassified and
unauthorized, and the 588 Arc observations are not approved either.

Team C found a defensible runtime ownership purpose in source for each of the
ten oracle-represented sites and no site that is plainly removable without
changing current ownership/snapshot behavior. This does not prove every call is
necessary in every execution or authorize a runtime exception. Site 02 has the
least settled semantic boundary because the copied native affinity vector's
Moss reachability is unknown. The frozen runtime allowlist has zero entries;
**this audit approves no exception**. The artifacts do not demonstrate an
implicit Moss value copy beyond those unclassified runtime rows, but they do
not establish complete coverage of every duplication mechanism.

## Recommended next work

1. Independently verify lifetimes at the lock/queue release points, branch-scope
   enqueue/remove/join, worker and Root closure lifetime, reply-slot ownership,
   FFI scope transfer, and config snapshot API. Resolve whether line 857 is
   reachable from Moss-generated code.
2. For rejected sites, remove or restructure only after reviewing the affected
   lifetime and ownership boundary. For accepted runtime-only sites, add exact
   pinned callsite provenance records and mutation tests; do not add blanket
   `Arc` or runtime-marker exemptions.
3. Generate separate native artifacts for the two FileIO Arc sites before
   including them in provenance acceptance.
4. Rerun the exact-SHA oracle with compiler hash and complete sidecars, registry
   and marker mutation checks, and Default plus EST-shadow suites.

Raw artifacts are in `tmp/clone-audit-8d2de330/`: `results.json`, `ledger.md`,
`clone-inventory.json`, `clone-inventory.md`, and
`oracle-artifact-manifest.sha256`.
