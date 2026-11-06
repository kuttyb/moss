# R7-010 Clone Provenance Root-Cause Audit

## Scope and exact input

Read-only diagnostic of source/compiler commit
`1d5feb1ea237e1d57297d38af0734cb65722c348` (parent
`badad52fe4b8c7c770ceefee7bfbe5fcf9372ee3`), checked out in
`tmp/team-c-oracle-1d5feb-review`. No implementation, test, registry,
allowlist, or existing worktree was changed. This report is a separate output
from candidate review reports.

## Findings

- The exact-SHA sidecars record **252 unclassified direct `.clone()` rows**
  across repeated generated artifacts. These are observation rows, not 252
  distinct semantic Moss copies; they collapse to three repeated
  runtime-template source sites.
- The exact `1d5feb1` `src/executor_runtime.cpp` scan has **3 direct
  `.clone()` sites and 7 qualified `Arc::clone` sites** (10 marked runtime
  operations in that file). Across the compiler/runtime source set, there are
  **9 qualified `Arc::clone` sites**: executor runtime lines 58, 152, 438,
  733, 919, 1089 and 1116; `src/file_chunk_codegen.inc:238`; and
  `src/fileio_runtime.cpp:1293`. These are separate from the direct sites.
  Exact-commit blob SHA-256 values are `executor_runtime.cpp`
  `3af71438b396ad0303eda7a1844902d2802c27bff8a3cdd18e8d6e82f5d22690`,
  `file_chunk_codegen.inc`
  `30b29209b4f2974cc147647d0edfb5fd056145d7aa93305252761e2b40fed92a`,
  and `fileio_runtime.cpp`
  `d46fc2b25db17fd62eb7951a826ef201e94efc2804e210c1570f07c2ef918a6`.
  The direct sites are:
  `active_executor.clone()` returning `Option<Arc<MossGlobalExec>>` (line
  497), `self.affinity.clone()` returning `Option<Vec<usize>>` (line 857),
  and `s.clone().into_raw_owned()` for a `MossBranchScope` Arc wrapper (line
  1407). These observed receiver/result types and runtime callsites support a
  mechanical runtime-handle/configuration explanation; they do not show a
  Moss semantic payload being cloned. They remain unapproved by the frozen
  provenance contract. In particular, source purpose/type inspection is not
  the independent authorization required by that contract.
- These nine qualified Arc sites appear to duplicate runtime-owned handles or
  synchronization/reply cells, rather than Moss payload values: the node and
  waiter are fair-lock queue state; the scope is a ready-queue entry; executor
  Arcs span worker, process-registry and ingress lifetimes; the chunk-codegen
  Arc shares a chunk outcome cell with a closure; and file I/O retains the
  shared inner handle for a borrow wrapper. At `executor_runtime.cpp:1089`,
  the cloned Arc owns a reply cell; its generic Moss result `R` is written
  once, not cloned at that call. These interpretations are supported by
  surrounding types and code flow, but do not discharge a full safety/lifetime
  audit or grant clone authority.
- A recursive scan of the exact Team C output at
  `tmp/team-c-oracle-1d5feb-review/oracle-work/` finds **84 sidecars and 258
  occurrence rows: 252 unclassified, 4 Root-ingress capture-only, and 2
  explicit-copy**. The 252 unclassified rows repeat the three runtime
  template sites across artifacts; they are not 252 distinct Moss copies.
  The 4/2 rows are separately supported by checker facts as described below.
  One representative artifact, ORACLE-10A Debug, has Rust digest
  `66f72d3ce6974d17b126c2e428599065893de9f52181d047fab8549c2e8fc99a` and
  one explicit-copy plus three unclassified rows. Thus the retained 252
  count is directly present in this exact-SHA output. The earlier count of
  only 80 came from a nonrecursive directory scan and omitted four nested
  ORACLE-54 provider/consumer sidecars. Those four each contain three
  unclassified occurrences: consumer Debug Rust SHA-256
  `c4c7c5b9ab1331312d4f91643b35b328b2adaf90842684534d859e400bbbabad`,
  consumer release `4a6db5c4d87669a17a5c6bf2f4730d252178c37e1a41e0809abac24958813132`,
  provider Debug `dc0d2d14c4ecf810eb8c287ff376f2f8da56eb1221704d9b024b9055916bc010`,
  provider release `29e6fa0a1ba4bdc3f7ab0f5968d5418a5732aed130b2900885bc140a9368e2ca`.
  Their respective Moss source digests are
  `5274280c3ce69b495d419e7690fcd17d92ebb7cdcca82c2cb60d6a1c8c35f36c`
  (consumer) and
  `39ec7084eb723a2da6eb350ec95c320ca3439186c4bf07f02efa57663e68f13d`
  (provider). The sorted path/SHA manifest for all 84 sidecars is pinned by
  SHA-256 `8e033da83c04810ece45fc548c7f2b5886258174ec1e2b3939bb27c6a7c68134`.
- The four Root-ingress rows are two Moss source occurrences emitted in both
  Debug and optimized modes: `w.clone()` captures a `Worker` route handle into
  the `runtime_invoke(move || ...)` closure for the `Process_shared` and
  `Increment_shared` calls. Their checker facts say
  `compiler_generated_capture_only;permits_moss_value_copy=false`. The
  emitted operation therefore appears to duplicate a routing handle for
  closure lifetime, not a Moss payload; it explicitly carries no Moss-value
  copy permission.
- The two explicit-copy observations are Debug and optimized output for the
  same ORACLE-10A source `copy(values[0])`; both point at `copy@7:16` and an
  exact checked `TypedCopyFact` (`READ`). Their Rust occurrences are supported
  by semantic copy authority, unlike the runtime rows.
- The 2 checker-linked explicit-copy rows and 4 Root-ingress capture-only
  rows are separately present in the exact-SHA sidecars, apart from the 252
  unclassified rows. Root-ingress facts explicitly have
  `permits_moss_value_copy=false`; they identify routing-handle capture and
  do not authorize Moss-value copies. The two explicit-copy rows are linked
  to checked `copy(...)` facts.

## Limits and remaining evidence

The 252-row claim is pinned to the exact-SHA sidecar set and the reported
path/SHA manifest above. A compiler binary digest and original run command
are not recorded in this audit worktree. To classify every occurrence, preserve
the generated Rust span and its template source span for all artifacts, then
bind each to source/callable/type evidence. For runtime sites, independent
review must establish the runtime ownership/lifetime invariant and a reviewed
provenance record; the marker and Rust type alone are locators/evidence, not
authority. For the 4 Root-ingress rows, retain exact checker capture facts and
Rust spans while keeping the copy-permission field false. For the 2 explicit
copies, retain exact checker facts and their one-to-one helper/Rust spans.

The qualified Arc census is not complete proof that no other duplication
mechanism exists. The retained contract explicitly excludes `.to_string()`,
`mem::replace`, `mem::take`, `.copied()`, constructors, `Copy` loads, macros
and other operators from the `.clone()` scanner's coverage. They require a
separate source/type/authority audit and must not be folded into either the
252 `.clone()` count or the qualified Arc count.

## Disposition

Diagnostic only. Evidence supports that the 252 rows repeat a small set of
runtime template operations and does **not** establish 252 semantic Moss
copies. It also does not authorize the runtime clones or close the broader
clone-provenance gate. The compiler binary/run provenance details and
independent runtime-site authorization remain outstanding.
