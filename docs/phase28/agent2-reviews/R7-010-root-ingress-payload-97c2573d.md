# Independent review: Root-ingress fresh payload authority

**Verdict: scoped GO for the checker-issued fresh Root-ingress payload authority at `97c2573d8e11e1e5ac4d7a58cd28242bccf66c8d`.** This approves only direct main-body Root message payload transfer for fresh nonprimitive constructors and literals. It does not approve broader Phase 28 completion or any merge to `phase28-work` / `main`.

## Reviewed state and authority

Reviewed exact candidate SHA `97c2573d8e11e1e5ac4d7a58cd28242bccf66c8d` in `phase28-r7-010-root-ingress-payload-authority`. Candidate tracked state was clean before this report; independent build/test artifacts are in `tmp/review-root-ingress-*`. The implementation diff and `git diff --check HEAD^..HEAD` were reviewed.

The checker collects payload authority only after resolving a Root-ingress message to a unique checked handler and exact synchronous message call edge. It retains the statement occurrence, arena, message expression, argument index and EST occurrence, message source fingerprint, selected handler identity, checker context, and the checked argument TypeRef. It only emits a payload fact for a direct `Stmt::Message` argument whose unparenthesized EST node is a `Constructor` or `Literal`, whose closed checked type matches the selected handler parameter, and which has no overlapping `copy` or ReturnValue fact. It explicitly rejects primitive payloads. Identifiers, locals, pipelines, projections, nested messages, and forwarding do not receive this authority.

The typed API requires exact fact/requirement identity and full-field equality, including TypeRef, handler, EST occurrences, context, action, access, fresh-rvalue proof, and `permits_moss_value_copy=false`. Native lowering independently resolves the same exact handler/argument/EST identity, compares the retained EST TypeRef to both fact and handler TypeRef, and permits only `TransferOwnedFreshRvalue`. With no matching requirement, the existing message payload lowering fails closed unless another checker authority (explicit `copy` or exact consume) applies. The consumer does not derive payload types or freshness.

The generated thunk moves the constructor/literal owner into its argument local. The observed `.clone()` on the same generated line is for the domain handle and carries the existing capture-only `permits_moss_value_copy=false` identity; the payload expression itself has no clone. No nonprimitive Moss copy is authorized.

## Independent evidence

- Exact candidate build: `make --jobs="$(nproc)" COMPILER_DIR=tmp/review-root-ingress-build moss-dev moss-opt` — passed.
- 138-scenario gate: `python3 tests/tooling/check_phase28_root_ingress_capture_authority.py tmp/review-root-ingress-build/moss-dev` — passed (`138 scenarios PASS`). It covers Default/Shadow fact identity, checker drop/duplicate/owner/argument/type/action/context/occurrence perturbations, consumer drop/duplicate/perturbations, Fast Debug output, and unsupported identifier/materialized-local payloads failing closed in native generation.
- Constructor/string native compile and runtime, Default: `tmp/review-root-ingress-build/moss-dev tests/fresh_message_payload.moss -o tmp/review-root-ingress-rust/default/fresh.rs` and the corresponding `tests/string_payload_snapshot.moss -o tmp/review-root-ingress-rust/default/string.rs`; `rustc -D warnings` compiled each generated file. Both executables ran with exact outputs `fresh: 7` and `fresh`.
- Constructor/string native compile and runtime, EST-Shadow: repeated both compiler invocations with `MOSS_EST=shadow`, writing to `tmp/review-root-ingress-rust/shadow/{fresh,string}.rs`; `rustc -D warnings` compiled each generated file. Both executables ran with exact outputs `fresh: 7` and `fresh`.
- Generated Rust payload lines were exactly `Payload { value: 7_i64 }` and `MossStr::from("fresh")` assigned to `__moss_root_arg_*`, with no `.clone()` in the payload expression. Clone provenance sidecars identified the neighboring domain-handle clone as capture-only with `permits_moss_value_copy=false`; no payload copy fact was present.
- Adversarial extra-argument nested Root message probe returned the ordinary `TYPE_MISMATCH` (“message Worker.Hit expects 0 arguments, got 1”) rather than accepting payload authority or crashing.

## Scope boundary

The candidate status correctly leaves source-free `.mossi` transport, full suites, frozen ownership oracle, and runtime clone provenance outside this slice. This scoped GO does not authorize any runtime clone exception and is not a whole-phase GO.
