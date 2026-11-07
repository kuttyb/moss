# Team C scoped review: method-parameter ReturnValue identity

## Verdict

**Scoped PASS** for exact candidate `602a14404af92c64f22b20391ed1579e03570098` (`phase28-r7-010-runtime-clone-provenance-715946`). The registered method-parameter ReturnValue gate passed independently in Default and EST-shadow. The exact method parameter binding and definition identities survive provider `.mossi` serialization and source-free import checking; native and Fast Debug agree for the tested unoverloaded method. Checker and native consumer corruption hooks fail closed.

A separate adversarial overload probe found distinct checker identities and correct Fast Debug execution, but generated Rust fails to compile because same-named overloaded methods lower to duplicate Rust methods. The candidate does not change native emission; this is outside the scoped identity-seeding change and must remain visible as a Native limitation. This review is not full Phase 28 GO and authorizes no runtime clone site.

## Exact source and artifacts

- Candidate SHA: `602a14404af92c64f22b20391ed1579e03570098`.
- Parent SHA: `715946e0ef3eed3c963100374f8516b864444282`.
- Original candidate worktree: `tmp/phase28-r7-010-runtime-clone-provenance-715946`; its existing ignored artifacts under `tmp/phase28_method_parameter_return/` were left untouched.
- Fresh detached verification worktree: `tmp/teamc-602a1440/review-worktree`, at the exact candidate SHA.
- Isolated compiler cache: `tmp/teamc-602a1440/build/compiler`.
- Fresh `moss-dev` SHA-256: `0aa8a615fc474eb1860e94c4a1a221cdf85ef1486edc9a1fc83f4c1514aca307`.
- Fresh `moss-opt` SHA-256: `c2f64be1699d88f72668bab7f3f9491a0867da82073d8b93f73bb1d2a4e9f7ca`.
- `src/semantic_checker.cpp` SHA-256: `80cdb4856c55e93fcf2ec3807ba0e58d15d763f1f711c3231d3e1ff31f6f54ca`.
- `tests/tooling/check_phase28_method_parameter_return.py` SHA-256: `a9f4c7aec46a980c8c97a1e402dc73e686450d318c05ac55d72a837a2dfd919e`.
- Rebuilt provider `.mossi` SHA-256: `fc93cf60bd3b3429d9e62d856520fb689c4b0326cce90c1062608245f87ea8a2`.
- Fresh verification worktree tracked status is clean; probe and build artifacts are under `tmp/` only.

## Reproduction

From the fresh verification worktree:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/teamc-602a1440/build/compiler \
  tmp/teamc-602a1440/build/compiler/moss-dev \
  tmp/teamc-602a1440/build/compiler/moss-opt

env -u MOSS_EST python3 tests/tooling/check_phase28_method_parameter_return.py \
  tmp/teamc-602a1440/build/compiler/moss-dev
MOSS_EST=shadow python3 tests/tooling/check_phase28_method_parameter_return.py \
  tmp/teamc-602a1440/build/compiler/moss-dev
```

Each gate run printed:

```text
Phase 28 method parameter ReturnValue authority: PASS
```

The registered gate checks a provider method returning its nonprimitive parameter, checks the emitted `.mossi` identity strings, builds/runs Native and Fast Debug (both output `9`), checks a source-free consumer against that `.mossi`, and exercises three checker hooks (`DROP`, `DUPLICATE`, `PERTURB_RETURN_VALUE_FACT`) plus five Native consumer hooks (`DROP`, `DUPLICATE` requirement/fact, and `PERTURB_RETURN_VALUE_FACT`). Each hook must make the corresponding checker/build result fail.

The focused script recreates its fixed `tmp/phase28_method_parameter_return` directory. I ran it only in the fresh detached worktree, not in the original candidate worktree, which already had ignored files at that path. After the gate removed build output while testing consumer hooks, I independently rebuilt the transport project and inspected the `.mossi` row. It contained:

```text
"method:provider__Carrier.Echo"
"method:provider__Carrier.Echo:parameter:0"
"method:provider__Carrier.Echo:parameter:0:definition"
```

The fresh `.mossi` row records `provider__Payload`, `CONSUME`, and those exact identities. The source-free consumer check passed using `MOSS_MODULE_PATH` pointed at the emitted provider interface. The gate does not execute the source-free consumer and does not mutate/corrupt the `.mossi` artifact itself; those are limits of this focused test.

## Identity and collision review

In `check_method_ownership`, every parameter receives a binding identity based on the method identity plus `:parameter:<index>`; its definition identity adds `:definition`. For a non-overloaded method the existing `functional_method_context` is `method:<owner>.<name>`, matching the serialized method context expected by the gate.

For overloaded methods the existing checker helper constructs `method-return-v1:` followed by length-prefixed owner, method name, and a signature containing arity plus length-prefixed canonical parameter types. It rejects open parameter types and duplicate canonical signatures. The new seeding code uses this helper, so overload parameter identities are separated by full canonical signature before the parameter index suffix. I found no delimiter ambiguity in this encoding; duplicate signatures fail closed.

Adversarial probe source: `tmp/teamc-602a1440/overloaded_return.moss` defines `Carrier.Echo(PayloadA) -> PayloadA` and `Carrier.Echo(PayloadB) -> PayloadB`, each returning its nonprimitive parameter. Exact checker fact rows were distinct:

```text
context=method-return-v1:7:Carrier4:Echo12:1;8:PayloadA
binding=method-return-v1:7:Carrier4:Echo12:1;8:PayloadA:parameter:0
...definition identity...:PayloadA:parameter:0:definition

context=method-return-v1:7:Carrier4:Echo12:1;8:PayloadB
binding=method-return-v1:7:Carrier4:Echo12:1;8:PayloadB:parameter:0
...definition identity...:PayloadB:parameter:0:definition
```

`moss-dev check` accepted the probe, and `moss-dev run --interp` printed `9` then `10`. Native emission followed by `rustc --edition=2021 -Dwarnings -C opt-level=0` failed: the generated Rust has duplicate `Carrier::Echo` definitions and ambiguous calls. No emitter file changed in this candidate, so this is not evidence of identity collision or a regression introduced by this patch. It means this candidate does not establish Native support for overloaded nonprimitive ReturnValue methods; the focused gate’s Native assertion covers the unoverloaded method only.

## Scope limits

No full suites, frozen oracle, clone inventory, or clone authorization were performed. Build logs contained existing `src/type_ir.hpp` missing-field-initializer warnings; both fresh compiler binaries linked successfully. The focused method gate is the independent evidence for this slice.
