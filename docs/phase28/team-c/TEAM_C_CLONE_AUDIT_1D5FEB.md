# Team C clone provenance diagnosis — exact SHA `1d5feb1e`

**Scope:** read-only diagnosis of the frozen v2 oracle's generated `.clone()` observations at exact source SHA `1d5feb1ea237e1d57297d38af0734cb65722c348`. No source, tests, registries, allowlists, or existing worktrees were modified. This report classifies evidence and remaining ambiguity; it does not authorize any runtime clone or certify Phase 28.

## Evidence inputs

- Fresh exact-SHA worktree and compiler: `tmp/team-c-oracle-1d5feb-review`; compiler binary `build/compiler/moss-dev` SHA-256 `143f1e8807ebd24f39be867f640d174b208decbe0c21c8b8960de124b44504e6`.
- Oracle JSON from the independent run: `oracle-results.json` SHA-256 `a5d97ef0d1396ec5c6a0e74acedfd9c43c29fd68b5fb05926e34f9017c29849e`.
- Recursive sidecar manifest: 84 `.mossclone.json` files; SHA-256 over sorted `relative-path:file-sha256` rows `8e033da83c04810ece45fc548c7f2b5886258174ec1e2b3939bb27c6a7c68134`.
- Frozen runtime callsite allowlist is `CERTIFIED_FROZEN`, has zero entries, digest `1b4cc2f59ddaec51149f84468b098c53772815c3ca64e858febd976e53a6e19d`.

## Reconciliation of the 258 observations

The sidecars contain exactly 258 occurrence rows, partitioned as follows:

| Sidecar origin | Count | Source/output evidence | Interpretation |
|---|---:|---|---|
| `unclassified` | 252 | Three identical Rust context hashes, each in all 84 sidecars | Generated shared-runtime clone snippets; likely mechanical runtime clones, but not provenance-authorized or semantically classified by the current sidecar/registry. |
| `root_ingress_domain_capture` | 4 | Two context hashes, each emitted in debug and optimized output for ORACLE-43 and ORACLE-45 | Compiler-generated domain-handle closure captures tied to checker Root-ingress capture facts. These facts explicitly say `compiler_generated_capture_only;permits_moss_value_copy=false`; they do not grant Moss payload-copy authority. |
| `explicit_copy` | 2 | One context hash in debug and optimized output for ORACLE-10A; `source_copy_id=copy@7:16` | Semantic Moss `copy(v[0])`, with checked fact `typed-copy-fact-v1:ee11376c3161735d1fbf37483026388ca88954a0e2af04afc374c6b5fbbea2a4`, type key for `Record`, and READ ownership. These are the two sidecar-authorized semantic copies, not runtime-registry exceptions. |

The JSON aggregate fields reconcile: `unauthorized=252`, `unclassified=252`, `root_ingress_captures=4`, `authorized=2`; raw observations sum to 258. Thus the 252 called “unverified” by the oracle summary are precisely the unclassified generated-runtime observations. There are no sidecar rows that suggest an implicit Moss-level copy among those 252, but their status remains unresolved for acceptance because no exact provenance classification is carried into the sidecar or certified runtime registry.

### The 252 shared-runtime observations

Each of the three context hashes occurs 84 times (once in every debug/optimized generated Rust artifact, including the four provider/consumer package artifacts for ORACLE-54):

1. Rust context SHA `63523e55f7885e103e2d1e60bd23537d1f3fff72e504137390f27800c501a62e`, generated line 2014, is tagged `MOSS_RUNTIME_CLONE_SITE:r7-010-runtime-01`. Source is [executor_runtime.cpp](/home/owner/daji/code/moss/tmp/team-c-oracle-1d5feb-review/src/executor_runtime.cpp:497): `Option<Arc<MossGlobalExec>>::clone()` in `MossProcessRuntime::active_executor`. This increments/copies the runtime executor handle under the process-runtime lock.
2. Rust context SHA `82e616fec2606e1eb32df9053f8c22d1a51bfdbdf80b66ab9fd986f7cfbc027b`, generated line 2374, is tagged `r7-010-runtime-02`. Source is [executor_runtime.cpp](/home/owner/daji/code/moss/tmp/team-c-oracle-1d5feb-review/src/executor_runtime.cpp:857): clone of the runtime executor builder's optional affinity `Vec<usize>` while resolving `MossExecutorConfig`.
3. Rust context SHA `b5d689175ca67ce681be9c9083defbc5ca5c043a5efc9be896db6a972e4ff0ff`, generated line 2924, is tagged `r7-010-runtime-03`. Source is [executor_runtime.cpp](/home/owner/daji/code/moss/tmp/team-c-oracle-1d5feb-review/src/executor_runtime.cpp:1407): `MossBranchScope` clone through the runtime FFI bridge, which creates another owned runtime scope count from a live borrowed count.

These exact source/context matches support “mechanical runtime clone” as the likely classification: the first is a runtime executor handle, the second runtime configuration, and the third runtime branch-scope ownership. They are not observations of Moss source `copy(...)` and do not themselves establish a Moss nonprimitive value duplication. However, their sidecar rows remain `origin_kind=unclassified`, with semantic type, ownership fact, and identity all `unclassified`. The marker comments are source breadcrumbs, not a certified mapping. Therefore the audit cannot mark them permitted or dismiss them as spurious for acceptance solely from this evidence.

### Four Root-ingress captures

The four tagged rows are two distinct capture facts across debug/optimized generated output:

- ORACLE-43 context `b1ae2b3563c1644895a3dd067614493b27ba6927ff1b5be5cbe5ba3bc544f4bb`, source identity `root-ingress-domain-capture-fact-v1:d2516fdeeabf77233436715a1f2b3884eed17bda61362b8fb3e9d51c440b0647`.
- ORACLE-45 context `4a2a494c2d8a7b1adffe0b9da9ea74d73bcaf1a8365d007eb201d8346d95111a`, source identity `root-ingress-domain-capture-fact-v1:e4ab2d9ea2c30f9e1c9f28d584747b4e0b3782fbeef1982ea8850a008b09f1b9`.

Both generated snippets clone a `WorkerRef` domain handle into a Root-ingress closure. Their checker-side capture facts identify type `Worker`, are marked capture-only, and explicitly disallow Moss value copies. This is distinct from payload-copy authority. The capture is checker-identified but still has no entry in the frozen runtime clone allowlist.

### Two semantic Moss copies

ORACLE-10A source line 7 is `var copied = copy(v[0])`. Its debug and optimized Rust each contain the same explicit clone operation inside the generated `__moss_copy_value_*` helper and carry `copy@7:16` plus the exact checked typed-copy fact above. These are genuine explicit Moss copies and are not implicit copies. They are the only two occurrences counted as authorized in the sidecar totals.

## Separate source inventory: qualified `Arc::clone`

At the same source SHA, textual inventory over `src/**/*.{cpp,hpp,inc}` finds **9 qualified `Arc::clone` call sites**:

- `src/executor_runtime.cpp`: 7 sites at lines 58, 152, 438, 733, 919, 1089, and 1116. These cover fair-lock nodes/waiters, branch-scope ready-queue references, worker executor handles, active-executor storage, reply slots, and active-executor ingress snapshots.
- `src/file_chunk_codegen.inc`: 1 emitted site at line 238, duplicating an `Arc<Mutex<Option<_>>>` slot for generated file-chunk branches.
- `src/fileio_runtime.cpp`: 1 site at line 1293, acquiring a runtime `Arc` to FileIO internals for a branch read borrow.

This is source-site inventory, not a runtime call count. Qualified `Arc::clone(&x)` does not match the oracle sidecar's `.clone()` occurrence spelling, so these nine sites are a separate coverage boundary and are not part of the 258 `.clone()` rows. Seven executor-runtime Arc sites have runtime marker IDs (`04`–`06`, `08`–`11`); marker `07` is absent at this SHA. The file-chunk and FileIO sites are not part of the frozen v2 `.clone()` accounting.

For context, a literal text scan finds 47 source lines containing `.clone()` under `src/**/*.{cpp,hpp,inc}`. This includes comments, scanner/search strings, and C++ generator templates, so it is not a semantic source-level clone count.

## Diagnosis and remaining evidence

Current evidence separates the rows into two checker-evidenced semantic copies (explicit `copy`), four checker-evidenced Root domain-handle captures that explicitly do not authorize Moss value copies, and 252 mechanically generated shared-runtime snippets whose source appears runtime-only but whose sidecars lack exact semantic/runtime classification. No observed row has been shown to be an implicit Moss nonprimitive copy. Equally, no runtime clone exception is authorized: the runtime registry is empty, and the 252 remain unauthorized/unclassified in the oracle result.

To decide whether any of those 252 are truly spurious relative to Moss semantics, evidence still needed is a reviewable per-site provenance mapping from the three sidecar context hashes to their exact runtime source identities and concrete Rust types, with lifetime/ownership rationale for each clone, confirmed against emitted Rust at both optimization levels. For the separate nine `Arc::clone` sites, the current oracle does not enumerate them as `.clone()` observations; they need independent source-site classification/evidence if runtime clone provenance acceptance is meant to cover qualified associated-function clones as well. This is an evidence gap description, not a code-change or authorization recommendation.
