# Agent 2 review: method-parameter ReturnValue identity

Date: 2026-11-06
Exact candidate: `602a14404af92c64f22b20391ed1579e03570098`
Worktree/branch: `tmp/phase28-r7-010-runtime-clone-provenance-715946` / `phase28-r7-010-runtime-clone-provenance-715946`
Scope: method parameter binding/definition seeding for ReturnValue ownership facts, including overloaded method identity and Section 2 `.mossi` projection. No runtime clone authorization.

## Verdict

**SCOPED GO** for the method-parameter ReturnValue identity repair. The parameter’s binding and definition identities derive from its exact method context plus parameter ordinal. For overloads, the context uses an unambiguous length-encoded canonical parameter signature; I verified two overloads produce distinct ReturnValue contexts and binding identities. The existing ReturnValue checker and `.mossi` artifact paths preserve and validate these fields, and the newly registered gate’s corruption/drop controls fail closed.

The gate’s source-free imported execution check covers a single method, not overloaded methods. My overload `.mossi` artifact probe confirms distinct serialized identities and source-free checking accepts both overload calls. Native compilation of the overload probe fails because Rust emission creates duplicate `Echo` definitions; I do not attribute that unrelated overload-lowering limitation to this identity patch or claim overloaded native runtime parity. Whole Phase 28 remains NO-GO.

## Exact candidate verification

The candidate worktree was clean at the exact SHA above. I forced a fresh `moss-dev` build and ran:

```sh
make -B --jobs="$(nproc)" moss-dev
env -u MOSS_EST python3 tests/tooling/check_phase28_method_parameter_return.py ./build/compiler/moss-dev
MOSS_EST=shadow python3 tests/tooling/check_phase28_method_parameter_return.py ./build/compiler/moss-dev
```

Both focused gate runs passed. They exercise checker ReturnValue fact drop/duplicate/perturb, native consumer requirement/fact drop/duplicate/perturb, direct Native/Fast Debug return parity, artifact emission with exact method/parameter binding and definition identities, and source-free import checking. The forced dev compiler SHA-256 is `0aa8a615fc474eb1860e94c4a1a221cdf85ef1486edc9a1fc83f4c1514aca307`.

## Overload adversarial probe

I checked this untracked scratch fixture under `tmp/agent2-review-602a1440/overload-probe/overload.moss` in both Default and EST-shadow. It declares `Carrier.Echo(PayloadA) -> PayloadA` and `Carrier.Echo(PayloadB) -> PayloadB`, each returning its parameter. Both modes accept it and emit two facts. The observed identities are:

```text
method-return-v1:7:Carrier4:Echo12:1;8:PayloadA:parameter:0
method-return-v1:7:Carrier4:Echo12:1;8:PayloadB:parameter:0
```

The corresponding `est_fact_context` values are distinct and contain the same exact signature distinction. Both facts have action `TransferOwnedValue`; no copy authority is introduced. The code constructs each parameter identity as `method_identity + ":parameter:" + ordinal`, and its definition identity as that binding plus `":definition"`.

For transport, I built a scratch Margo provider containing those two overloads. Margo emitted `build/debug/provider.mossi` before Rust compilation failed with E0592 for duplicate generated Rust methods named `Echo`. The two serialized `return_value_fact_v1` records have distinct execution-body hashes, EST contexts, source binding identities and source definition identities, while sharing the expected stable callable identity `method:provider__Carrier.Echo`. A source-free `moss check` of a consumer importing the emitted `.mossi` accepted calls to both overloads. I did not claim source-free runtime parity: the existing Native overload lowering limitation prevents producing a Rust executable for this probe.

## Identity and fail-closed review

`check_method_ownership()` now initializes each method parameter in the ownership environment with its type plus a stable binding and definition identity. For a non-overloaded method it uses `functional_method_context(object, method)`; for an overloaded name, `overloaded_method_return_context()` builds a length-prefixed owner/name/signature identity from canonical, closed parameter TypeRefs and rejects duplicate canonical signatures or an absent selected declaration. The signature includes arity and parameter types, which distinguishes the `PayloadA` and `PayloadB` cases without relying on parameter spelling or declaration line order.

The ReturnValue fact builder reads the binding and definition identities from the ownership environment for a simple returned identifier. It issues `TransferOwnedValue` with `Consume` for the nonprimitive parameter. The ReturnValue fact’s authority predicate requires both source identities for a binding return; the requirement is copied from that checker fact and the exact lookup requires one matching authoritative fact. No consumer reconstructs an overload signature or chooses the return source.

This composes with the earlier `3dc803a2` overload-context repair: the exact same `overloaded_method_return_context()` identity used to separate `Choice.next(Int)` and `Choice.next(String)` ReturnValue EST facts is now also the prefix of each method parameter binding/definition identity. The new identities therefore keep the established declaration distinction rather than reverting to a method-name-only key.

The existing artifact writer copies both source identity fields into the v1 ReturnValue artifact. The `.mossi` encoding serializes both fields; import validates the artifact body hash, exact valued-return ordinal/slot, source location and callable identity, then projects the artifact fields into an in-memory checker fact and requirement. The overloaded probe’s two records confirmed the parameter-signature identities survive encoding. The added single-method gate confirms its artifacts retain `method:provider__Carrier.Echo:parameter:0` and the matching `:definition` identity.

The gate’s checker-side hooks exercise missing/duplicate/perturbed ReturnValue facts and expect the checker to reject. Native consumer hooks remove or duplicate the exact requirement/fact and perturb the fact, with every case required to fail the build. These are active test-build hooks and the gate is registered in Default and Shadow. The test currently runs the corruption controls against a source-backed provider; it does not repeat them after source-free artifact import. The artifact/import verification covers the unchanged serialization projection path, and the custom overload probe verified serialized identities plus source-free checking, but not source-free corruption injection or execution. That is a coverage limitation, not a defect found in this checker identity repair.

## Scope limits

No full suites or oracle were run, as requested. The `.mossi` scratch artifact and probes are disposable validation artifacts; no candidate source or test file was edited. No runtime clone was authorized by this review.
