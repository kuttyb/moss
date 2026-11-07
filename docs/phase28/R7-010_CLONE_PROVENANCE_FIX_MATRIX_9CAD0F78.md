# R7-010 clone/provenance root-cause matrix — exact staging 9cad0f78

Read-only audit of 9cad0f78bea157ffee1a3838fdd41373f95dfa5d on
phase28-r7-010-vertical-integration. No compiler, test, registry, or tracked
files were changed. Earlier 8d2de330 counts are historical; this report uses
a fresh exact-9cad oracle and generated-artifact inventory.

## Pinned current-SHA evidence

- Compiler build/compiler/moss-dev SHA-256:
  f7693e80c97396a482a5f38ca705ceb1bc41e3470a6bedb5ad5b8e07cdd1a959.
- Frozen oracle: 60 fixtures; 19 pass all gates; 41 fail at least one.
  Checker expectations 60/60; 59/60 semantic/execution pass. ORACLE-45 has the
  Root-ingress Fast Debug missing-TypeRef crash and also fails provenance.
- Fresh Rust inventory: 84 Rust artifacts and 84 sidecars; 1,876 lexical
  duplication candidates in 36 site groups. Direct .clone(): 258 rows,
  comprising 252 unclassified runtime rows, 4 Root-ingress capture rows, and
  2 explicit-copy rows. Qualified Arc::clone: 588 rows. The two copy rows are
  one source operation emitted Debug and Optimized; four captures are two
  source message edges emitted in both modes. Each of the three runtime
  dot-clone groups and seven runtime qualified-clone groups occurs once in
  every artifact (84 rows/site).
- The v2 runtime registry remains empty. Frozen expectations SHA-256:
  7ca0d550e38f33c47105f57e22f5993d23449bf09c1cd5da043de924657f23a2.
- Run outputs: tmp/clone-audit-9cad0f78/. The 1,728-file oracle-work tree
  manifest SHA-256 is
  6c8bb2f21ea079ddf30571b66ad6ab3e120896808c0664fd0da350adcaf504e0.
  Oracle JSON SHA-256:
  9697d6306707dfa65559e6ad3cc9321d57bce3a8806c778b24675139ee438d37.
  Inventory JSON SHA-256:
  a32cbd992ede033cdc5e8bf4e3e6ff569790889351433e0a5e36a6c48cd935db.
- Source hashes: executor_runtime.cpp
  3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690;
  file_chunk_codegen.inc
  30b29209b4f2974cc147647d0edfb5fd056145d7aa93305252761e2b40fed92a;
  fileio_runtime.cpp
  d46fc2b25db17fd62ebd7951a826ef201e94efc2804e210c1570f07c2ef918a6.

## Exact source-operation spans

Columns below are one-based byte columns in the exact current source files.
Direct clone token spans: executor_runtime.cpp:497:90-98, :857:99-107,
:1407:109-117. Qualified call spans: executor_runtime.cpp:58:92-120,
:152:103-133, :438:93-121, :733:63-88, :919:86-112, :1089:65-99,
:1116:74-177. The FileIO call is fileio_runtime.cpp:1293:21-44.
file_chunk_codegen.inc:238 emits the Arc::clone call through split C++ stream
fragments, so its generated Rust span must be captured from a focused fixture;
the C++ template line is its exact source locator.

## Prioritized site/fix matrix

| Priority / exact source site | Type, lifetime and Moss-value risk | Missing proof / root cause | Minimal remediation; required check |
|---|---|---|---|
| P0 — executor_runtime.cpp:497, marker runtime-01: self.lock_inner().active_executor.clone() | Option<Arc<MossGlobalExec>>; called by branch_publish (:1282), runtime test access (:1549), and other runtime paths. Arc count lets the executor outlive the process mutex guard; this expression does not clone a Moss payload. | Sidecar has unclassified type/ownership/identity. Marker locates the site but registry has no reviewed row. Still unapproved. | Prefer exact runtime-site provenance only after independent lock/lifetime review; otherwise redesign without holding process lock across executor work. Test concurrent start/stop/ingress, lock release before admission, and Arc drop/liveness. Missing/duplicate/moved marker, wrong source digest, or missing registry row must fail closed. |
| P0 — executor_runtime.cpp:857, runtime-02: self.affinity.clone() | Option<Vec<usize>>; MossExecutorBuilder::config(&self) returns an owned MossExecutorConfig snapshot. Duplicates runtime CPU indices, not a Moss value. | Same unclassified sidecar/empty registry. Need to establish snapshot independence and prove no Moss value enters config. | Keep the owned snapshot and authorize only this exact reviewed site, or redesign config storage/API to share immutable runtime settings. Test builder mutation after snapshot and exact config equality; corrupt exact-site registry identity. No global Vec::clone rule. |
| P0 — executor_runtime.cpp:1407, runtime-03: s.clone().into_raw_owned() | MossBranchScope wraps Arc<MossBranchScopeState>; called through MossBranchScope::clone (:1696–1700) and __moss_branch_scope_clone (:1405). Produces a new FFI-owned count from a borrowed handle; does not clone queued work. | Runtime row is unclassified; borrowed lifetime, raw-count transfer and exactly-once drop need independent proof. | Review or remove the runtime Clone path if no caller needs it; otherwise exact-site authority after raw-handle ownership review. Test clone/drop permutations, borrowed lifetime, double-drop/use-after-free, and marker/registry mutations. |
| P1 — executor_runtime.cpp:58, :152, :438, :733, :919, :1089, :1116; markers runtime-04/05/06/08/09/10/11; 588 generated rows | Respectively Arc<MossFairNode> tail ownership; Arc<MossFairWaiter> queue ownership; Arc<MossBranchScopeState> ready queue; Arc<MossGlobalExec> worker closure; process active-executor slot; Arc<(Mutex<Option<R>>, Condvar)> reply slot; active-executor snapshot before releasing ingress lock. These calls clone Arc handles; at :1089 R is stored once and the outer Arc clone does not clone R. No Moss-value duplication shown. | Oracle v3 scanner records only method-form .clone() (oracle_harness.py:710–713; builder searches the method token .clone(. Qualified Arc::clone is absent from sidecars. Independent inventory finds it, but markers are locators, not approval; registry has no rows. | Add qualified-call occurrence support (separately versioned sidecar if needed) and bind each marker to exact source digest/span, Rust type, lifetime rationale, independent review. No global Arc::clone whitelist. Test marker omission/duplication/relocation, comment/string decoys, wrong registry identity/type, sidecar row drop/extra, plus fair-lock, queue, worker, ingress and reply lifecycle. |
| P1, absent from oracle — file_chunk_codegen.inc:238: generated Arc::clone(&slot) | Shares Arc<Mutex<Option<Outcome>>> between caller and chunk closure; shares the slot, not an Outcome clone. | No matching site in the 84 oracle artifacts; no current-SHA observation row or lifecycle proof from this run. | Add focused native file-chunk test pinning output marker/span/type and one publish/one consume, cancellation and drop. Include in qualified-clone provenance review. Absence from ownership fixtures is not approval. |
| P1, absent from oracle — fileio_runtime.cpp:1293: Arc::clone(&file.inner) | Creates MossFileIOReadBorrow retaining shared FileIO internals for a branch read; clones the Arc handle, not file contents. | No matching oracle artifact; current oracle cannot establish borrow lifetime, lock, or close behavior. | Add FileIO native test for concurrent branch-read borrow and close/drop ordering, then exact-site provenance review. Keep separate from seven oracle runtime Arc sites. |
| P2 — Root captures: ORACLE-43 line 8 message w.Process(42); ORACLE-45 line 10 message w.Increment(10) | Four Worker route-handle clones total: two source call sites × Debug/Optimized. Checker fact says compiler_generated_capture_only=true and permits_moss_value_copy=false. No Moss payload duplicated. | Already classified capture rows, separate from the 252 unclassified rows. Risk is treating capture as generic copy authority. | No runtime clone registry entry. Preserve exact capture fact and false permission; keep corruption/drop/duplicate/edge-identity tests. Repair ORACLE-45 Fast Debug TypeRef separately. |
| P2 — ORACLE-10A line 7 copy(v[0]) | One explicit Moss Record duplication, two Rust observations. Exact TypedCopyFact, READ, source span and helper link. | No provenance gap observed. | No repair. Retain exact checker fact and helper-link validation plus copy-fact drop/perturb/wrong-owner tests. This row cannot justify implicit copy. |

## Dependency and order

1. Independent L6 review the ten oracle runtime sites (3 direct, 7 qualified)
   against actual lock, FFI, queue, thread and destruction paths. Type/purpose
   evidence is not approval.
2. For each reviewed-safe site, add only exact reviewed runtime provenance.
   For rejected/unneeded sites, remove/restructure and add lifetime regressions.
   Do not change the frozen registry simply to clear current oracle failures.
3. Add FileIO/FileChunk-focused artifacts for the two sites absent from the
   ownership oracle; current 84-artifact counts cannot close them.
4. Validate with the frozen oracle, exact marker gate
   tests/tooling/check_phase28_r7_010_runtime_clone_evidence.py, qualified-clone
   registry/sidecar mutation tests, then Default and EST-shadow full suites.

Current oracle evidence shows no implicit Moss-value clone among its 258 direct
.clone() rows. It grants no runtime-clone authority. Qualified Arc rows are
runtime handle operations by source/type inspection but remain unapproved
because no exact reviewed callsite authority exists. The broader 1,876
candidates include .to_string, mem::replace, mem::take, .copied and constructors;
these are not interchangeable clone counts and need contract-specific review.
