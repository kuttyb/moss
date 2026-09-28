# Phase 22.1 teaching-diagnostic mapping

This is the small implementation map required by Phase 22.1A. The frozen
evidence is `benchmarks/agent/baselines/pre-22.1/`; its files are not inputs to
compiler behavior and remain unchanged.

| Baseline tasks | Current code | Checker site | Compiler fact already available | Phase 22.1 diagnostic and guidance | Focused regressions |
| --- | --- | --- | --- | --- | --- |
| AB008, AB009, AB023 | `MOSS_COMPILE_ERROR` | `Checker::check_expression`, `check_statements`, and `check_static_message_receiver` | receiver is `self`, enclosing domain type, requested handler, and enclosing handler identity | `DOMAIN_SELF_MESSAGE`; rule `domains.no-self-message`; `local-helper` guidance | expression/statement self-send, legal helper rewrite, and non-self cross-domain negative case |
| same-domain alias control | `MOSS_COMPILE_ERROR` | same message sites | receiver and `self` have the same checked domain type | `DOMAIN_SAME_INSTANCE_MESSAGE`; rule `domains.no-same-instance-handler-chain`; `local-helper` guidance | aliased same-domain instance plus a distinct-domain negative case |
| AB021 | `MOSS_COMPILE_ERROR` | ordinary member-call checks in `check_expression` / `check_statements` | receiver has a checked domain type and the handler name is parsed | `DOMAIN_HANDLER_REQUIRES_MESSAGE`; rule `domains.cross-domain-message`; `use-message` guidance | naked handler call, legal synchronous message, and ordinary object-method negative case |
| AB010 | `TYPE_INFERENCE_FAILED` | reply inference reaches an unresolved `message ledger.Read()` before message-target validation | enclosing domain has no `ledger` route; route declarations and concrete bindings are checked facts | `DOMAIN_ROUTE_NOT_DECLARED`; rule `domains.static-routes`; `declare-domain-route` guidance | missing route, declared-and-bound rewrite, and non-route unresolved-expression negative case |
| AB012, AB022 | `OWNERSHIP_CONFLICTING_ACCESS` | `Checker::check_conflicting_call_accesses` | call target, original actual expressions, argument indexes, storage paths, and inferred READ/WRITE/CONSUME modes | retain `OWNERSHIP_CONFLICTING_ACCESS`; rule `ownership.overlapping-access`; cause `overlapping-actual-arguments`; `separate-conflicting-access` guidance | WRITE+READ alias facts/locations, distinct-storage rewrite, READ+READ negative case, and no copy suggestion |
| AB018 | `FUNCTIONAL_SEMANTIC_ERROR` | `Checker::check_functional_callable` | operation, callable source form, input types, and absence of `_`/named target | `FUNCTIONAL_PLACEHOLDER_REQUIRED`; rule `functional.supported-callable-form`; `supported-pipeline-placeholder` guidance | unsupported `it` form, `_` rewrite, and valid named callable negative case |
| AB019 | `FUNCTIONAL_SEMANTIC_ERROR` | `Checker::check_functional_callable` | parsed invocation names an existing function and its arity and statically known input types match the stage callable shape | `FUNCTIONAL_CALLABLE_INVOCATION_UNSUPPORTED`; same rule; `named-pipeline-callable` guidance | `map(double())`, `map(double)` rewrite, wrong-arity/type negatives, and unrelated unknown callable negative case |
| AB020 | `FUNCTIONAL_SEMANTIC_ERROR` | `Checker::check_functional_callable` capture/method-effect check | pipeline operation, callback expression, captured receiver, resolved method, and non-READ receiver effect | `FUNCTIONAL_CAPTURE_MUTATION`; rule `functional.pure-callback`; `pure-pipeline-callback` guidance | mutable capture, pure named-stage rewrite, and READ-only captured method negative case |
| AB015 | `QUERY_TARGET_NOT_FOUND` | `write_semantic_query_json` after `semantic_target_facts` | complete checked target catalog with kind, canonical name, identity, and source | retain `QUERY_TARGET_NOT_FOUND`; rule `queries.exact-target`; cause `unqualified-query-target`; deterministic candidates; `qualify-query-target` guidance | unique bare handler, multiple same-suffix candidates, exact qualified success, and unrelated missing-name negative case |
| AB010, AB017 | `TYPE_INFERENCE_FAILED` | reply/return/pipeline inference sites | unresolved expression plus enclosing callable and expected result or collection-stage context | retain `TYPE_INFERENCE_FAILED`; rule/cause chosen by the concrete site; only legal context-specific guidance | missing-route case is removed from generic inference; reduce seed/callable probes, legal rewrite, and unsupported-operator no-annotation negative case |

AB017 was a write task and the baseline retained only its final file and the
diagnostic code, not the transient first-draft source or diagnostic message.
Phase 22.1 therefore covers the concrete reduce seed/callable inference sites
without claiming a source-level reconstruction that the evidence cannot prove.

The structured contract is additive to `moss-agent-1`: existing error fields
remain, while compiler-authored diagnostics may add `source`, `rule`, `cause`,
`related`, and `guidance`. Human and JSON forms consume the same facts.
`cause.entities[].semantic_identity` is the actual compiler semantic identity for a
resolved entity or `null`; canonical selector text remains in `name` and durable
`entity-v1` identifiers are not overloaded into this field. Focused inference
regressions exercise reduce seeds, functional callable results, function returns,
handler replies, legal controls, and unsupported-operator negative guidance.
