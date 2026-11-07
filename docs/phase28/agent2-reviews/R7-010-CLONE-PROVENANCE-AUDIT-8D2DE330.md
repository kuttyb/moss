# Agent 2 Read-Only Audit: R7-010 Clone Provenance

## Scope and pinned evidence

- Exact source/compiler commit: `8d2de3301e5f2dafef079439b0143a7b6675213e`.
- Compiler binary: `build/compiler/moss-dev`, SHA-256
  `26f157f25dc619184ae1d3fcaf52065d5e32720e2727276904e1544c0be9243d`.
- Oracle artifacts: `tmp/clone-audit-8d2de330/`.
- No compiler source, tests, registry, allowlist, or tracked files were
  modified. I added only disposable audit outputs under that `tmp/` directory
  and this separate report.
- Exact source blob SHA-256s: `src/executor_runtime.cpp`
  `3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690`,
  `src/file_chunk_codegen.inc`
  `30b29209b4f2974cc147647d0edfb5fd056145d7aa93305252761e2b40fed92a`,
  `src/fileio_runtime.cpp`
  `d46fc2b25db17fd62eb7951a826ef201e94efc2804e210c1570f07c2ef918a6`.

I recursively checked 84 generated Rust files and 84 sidecars. Every sidecar
`rust_sha256` matches its Rust file. The sidecar set has 258 occurrence rows:
252 `unclassified`, 4 `root_ingress_domain_capture`, and 2 `explicit_copy`.
Thus 252 is the exact row count across artifacts, not a count of 252 distinct
Moss source copies. The 252 rows map to three direct runtime template
`.clone()` sites repeated across 84 artifacts. Two explicit-copy rows are
Debug/Optimized observations of one exact checker `TypedCopyFact`; four
Root-ingress rows are Debug/Optimized observations of two checked route
captures, each with
`compiler_generated_capture_only;permits_moss_value_copy=false`.
These are syntactic occurrences in generated Rust, not dynamic execution
counts; runtime helper bodies are emitted in every artifact even when a
fixture does not call that helper.

Independent scanner rerun over the actual generated Rust files:

| Evidence | Result |
|---|---:|
| Artifacts / candidates / site groups | 84 / 1,876 / 36 |
| Direct `.clone()` observations | 258 |
| Qualified `Arc::clone` observations | 588 |
| `.copied()` / `.to_string()` | 6 / 756 |
| `mem::replace` / `mem::take` | 252 / 14 |
| `String::from` or `Vec::from` | 2 |
| Generated Moss `Clone` derives | 408 capabilities, not calls |

Artifacts are pinned by `oracle-artifact-manifest.sha256` digest
`bb3d841bbef60275fd8bef5fce9ff91b0162a33677b678cc0b2e474192e916f7`;
`results.json` SHA-256 is
`101050a884eb78af0804451a0221b3a09f1842abf8502d434ddfded37e1b669e`; the
inventory JSON SHA-256 is
`1db6bb25840bc2c522001bb31361bd88c54d97fc368ce2e6cfd68a9717cd40ed`.
The 588 observations are exactly seven executor-runtime callsites repeated
over 84 artifacts. They are observations, not 588 distinct source calls.

## Direct `.clone()` rows and capture/copy rows

The three unclassified runtime callsites in `src/executor_runtime.cpp` are:

| Line / generated Rust | Type at the callsite | What source evidence supports | Remaining issue |
|---|---|---|---|
| 497 `active_executor.clone()` | `Option<Arc<MossGlobalExec>>` | Returns an owned runtime reference from process state so it can outlive the lock guard. It duplicates an Arc count, not executor fields or queued Moss closures. | Runtime lifetime/locking invariants still need independent proof and authority. |
| 857 `self.affinity.clone()` | `Option<Vec<usize>>` | Public `MossExecutor::config(&self)` creates an owned config snapshot. `MossExecutor::affinity` accepts `Vec<usize>`, which native lowering can construct from a Moss `Vector[Int]`. The built-in generated `ExecutorStartPlan` uses `.start()` and the move-based `into_config`, and the oracle files contain no call to `MossExecutor::config()`. | Moss-path reachability is not demonstrated. The method is public in the runtime ABI; an external Rust caller could request the snapshot. The cloned contents are scheduler configuration, but their Moss-origin possibility means “not a Moss value” cannot be assumed solely from the Rust type. This is the least-supported proposed runtime-only classification. |
| 1407 `s.clone().into_raw_owned()` | `MossBranchScope`, wrapping `Arc<MossBranchScopeState>` | FFI wrapper turns a live borrowed opaque handle into a new owned Arc count; it does not clone the state or queued work. | The generated Rust corpus contains the exported function definition but no call to it. External caller/frequency and full FFI ownership protocol are not evidenced by these artifacts. |

The four capture rows are two `Worker` route handles cloned into
`runtime_invoke(move || ...)`, each emitted in Debug and Optimized output. The
checker identity and false-permission fact are attached. This supports a
closure-lifetime route-handle explanation; it expressly grants no Moss-value
copy permission.

The two explicit-copy rows are one `copy(v[0])` occurrence in ORACLE-10A,
Debug and Optimized. Both rows name `copy@7:16`, `READ`, the same exact
`typed-copy-fact-v1:` hash, and a fact-specific helper. These two rows have
semantic copy authority and are separate from all runtime rows.

## Qualified Arc observations and source paths

The 588 qualified `Arc::clone` observations cover these seven source sites,
each present in every oracle artifact:

| `executor_runtime.cpp` site | Referent | Source-level operation | What remains unproved |
|---|---|---|---|
| 58 | `MossFairNode` | Adds Arc count transferred into fair-lock atomic tail. | Raw-pointer tail/drop safety and all enqueue/dequeue interleavings need independent lifetime review. |
| 152 | `MossFairWaiter` | Queue owns waiter while the waiting thread observes notification. | Queue/notification teardown races need independent proof. |
| 438 | `MossBranchScopeState` | Ready queue owns scope while Branch work is pending. | Join/remove/stale-entry races and exactly-once release need independent proof. |
| 733 | `MossGlobalExec` | Worker closure owns executor Arc for physical worker lifetime. | Worker panic/exit and guard ordering need independent proof. |
| 919 | `MossGlobalExec` | Process runtime retains active executor ownership. | Activation/drain/clear state transitions need independent proof. |
| 1089 | `Arc<Mutex<Option<R>>, Condvar>` | Shares reply-cell control storage between caller and work closure; `R` is written once, not cloned here. | Wakeup, panic, and result-take protocol need independent proof; this is a runtime cell clone, not a clone of `R`. |
| 1116 | `MossGlobalExec` | Local Arc survives dropping ingress lock before admission. | Admission/drain race and executor lifetime after unlock need independent proof. |

The source types and code flow support “runtime handle/control-cell
duplication” as a purpose for these seven operations. They do not prove that
every Arc increment is necessary in every state, prove all synchronization
invariants, or authorize an exception.

Two additional qualified Arc source sites are not represented in the 84
oracle Rust files:

- `src/file_chunk_codegen.inc:238` clones an `Arc<Mutex<Option<outcome>>>`
  slot so the parent slot vector and asynchronous Branch closure share the
  result cell. The mapped value is placed in the `Option<outcome>` once; this
  callsite clones the cell handle.
- `src/fileio_runtime.cpp:1293` clones `Arc<MossFairMutex<Option<FileIOInner>>>`
  into `MossFileIOReadBorrow`. The object is a read-only runtime borrow token,
  distinct from a second `FileIO` value.

Their corresponding `moss_fileio_branch_read_borrow` path is used by the
eligible FileIO chunks lowering, but no matching invocation occurs in this
oracle artifact set. Therefore the oracle supports 588 observations for seven
sites; it does not cover all nine source-level qualified Arc sites. The two
FileIO sites need a generated FileIO-chunks artifact and an independent
source/type/lifetime review before any provenance classification.

## Markers, registries, and sidecar authority

`check_phase28_r7_010_runtime_clone_evidence.py` expects marker IDs and exact
neighbor expressions, checks marker/operator one-to-one placement, and checks
that the current ORACLE-10A output has four direct clones (three runtime plus
one checked copy) and seven Arc clones. Those checks make the comments useful
locators. The runtime callsite registry at this SHA has an empty `callsites`
array. The sidecar audit independently returns `UNAUTHORIZED`, with
`authorized=1`, `unauthorized=3`, `unclassified=3`, and zero Root-ingress
captures for ORACLE-10A. Markers and inventory site groups authorize nothing.

The locator script itself does **not pass** on this exact compiler/output.
Running it with the pinned compiler and ORACLE-10A fixture fails its
marker-stripped Rust digest assertion: it expects
`113847626e46a1ec7e92107fa9c45d207f35664b27eccafcb6c5315f844e45e6`, while
the retained artifact and a fresh exact-SHA generation are byte-identical at
raw SHA-256 `34d793a423ee485f9d89a4f3da69e828dc53d13281b5c7022e76c1516a11a129`
and marker-stripped SHA-256
`6aec8bb33275ead81fe27623709c87a0a9ade0420c9672b1f4991fefe6d22948`.
I compared the exact retained artifact with the fresh output: they are
byte-identical. Against the preceding `e86f02de` artifact that produced the
pinned expected digest, the only Rust-line difference is the exact typed-copy
helper symbol and full `TypedCopyFact` identity for `copy(v[0])`. Replacing
that one helper/fact identity with its prior value reproduces the hard-coded
`113847...` digest exactly. Thus this is a stale snapshot digest caused by a
legitimate exact-fact identity change, not evidence of a runtime marker or
callsite mismatch. Calling the marker-placement checker independently passes
its ten expected runtime marker/operator bindings; the separate sidecar audit
remains `UNAUTHORIZED` as above. Refresh the digest against the exact
reviewed generated artifact (or explicitly scope the hash to the runtime
template) before citing the registered locator gate as passing.

## Scanner blind spots

The lexical scanner masks comments and strings and recognizes a useful set of
candidate spellings: method `.clone`, `.clone_from`, `.cloned`, `.copied`,
`.to_owned`, `.to_string`, `.to_vec`; `Clone::clone`, `Arc::clone`,
`Rc::clone`; `mem::replace`, `mem::take`, `ptr::read`; and `String::from` /
`Vec::from`. The 1,876 count is only this operator inventory. It does not
establish whether any candidate copies a Moss value.

Concrete false-negative classes remain:

- Rust `Copy` loads/assignments (including `*borrowed_value`), Copy-derived
  types and compiler-generated copies; the scanner does not inspect MIR or
  trait resolution.
- Macro expansions and duplication hidden behind macros such as `format!` or
  custom macros; only pre-expansion text is scanned.
- Unlisted duplication/conversion APIs: `Arc::make_mut` / `Rc::make_mut`,
  `Cow::to_mut` or `into_owned`, `clone_into`, `ToOwned::to_owned` in UFCS
  form, `ptr::copy` / `copy_nonoverlapping`, `mem::swap`,
  `MaybeUninit::assume_init_read`, and constructors/collection operations
  such as `collect`, `extend_from_slice`, or non-String/Vec `From` impls.
- Custom helper functions in external crates or runtime code outside the
  generated artifact can duplicate their inputs internally without a visible
  operator in the scanned file.

The 408 generated `Clone` derives are capabilities, not invocations. They
need call/use analysis; the inventory's two lexically associated helper clone
calls are the two authorized explicit-copy observations, but that association
alone is not authority.

## Minimum evidence needed and disposition

Before classifying any runtime row, resolve its API boundary and ownership
reason at the exact source span/type, bind a reviewed record to the exact
whole-file digest and callsite, and test stale/missing/duplicate/relocated or
forged evidence fail-closed. For site 857, first establish whether only
host-side Rust can invoke `config()` or whether a supported Moss path can; then
prove the `Vec<usize>` snapshot is outside semantic Moss-copy authority or
remove the snapshot clone. For site 1407, document all FFI callers and count
transfer/release behavior. For FileIO, add a focused generated chunks artifact
for Debug and Optimized output and trace both cell/borrow handles through
`branch_join`. If any site cannot meet the policy, remove or restructure its
clone instead of relying on a broad `Arc` or marker exemption.

**Disposition: NO-GO for per-site runtime authorization and clone provenance.**
The code supports plausible runtime-handle purposes for the represented
operations but not reviewed exceptions. The empty runtime registry, 252
unclassified rows, 588 unapproved Arc observations, uncovered FileIO sites,
site-857 reachability ambiguity, scanner blind spots, and stale locator digest
assertion leave material risks unresolved. No exception is approved by this
audit.
