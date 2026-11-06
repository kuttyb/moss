# Team C verification: local specialized ReturnValue authority

## Exact source checkpoint

- Implementation candidate: `c4a48668b7a1adee0528efa8e8ff1a7770231e54`
- Parent: `c043ea07eb2e284f69302ea08ebd4a96fa586b35`
- Status-only descendant: `ea8cddeba7f344c27509aadcde11cfbef50ece87` (implementation tree remains the requested `c4a48668` tree).
- Fresh detached verification worktree: `tmp/team-c-verify-c4a4866`; clean at setup, no tracked files edited.
- Scoped review only; this does not establish full Phase 28 GO.

## Build and local ReturnValue gate

Built with isolated compiler/cache paths:

```sh
make --jobs="$(nproc)" COMPILER_DIR=tmp/team-c-4c4866-build moss moss-opt
```

Build succeeded. Artifact SHA-256:

- `moss-dev`: `c0e224fa589ab057bdc21b1ecf30a9d0deeea5f7e6de149d5354081aae9c7663`
- `moss-opt`: `ec763046b15938ddbcaf3720acf7e1f37f892de144727b5e8f0589aa38b555f4`

Ran:

```sh
env -u MOSS_EST python3 tests/tooling/check_phase28_local_return_authority.py "$PWD/tmp/team-c-4c4866-build/moss-dev"
MOSS_EST=shadow python3 tests/tooling/check_phase28_local_return_authority.py "$PWD/tmp/team-c-4c4866-build/moss-dev"
```

Both report `Phase 28 local ReturnValue EST authority: 44 scenarios PASS`. The checked local specialized function has exactly one `PHASE28_RETURN_FACT` for `context=fn:size<string>`, `est_context=fn:size<string>`, `type=int`, and a `specialization-v2:` identity. Generated Debug and `-Oshared-memory` native code compile with `rustc --edition=2021 -D warnings` and run with `3\n`; Fast Debug also returns `3\n`. The gate checks the specialized body contains neither `.clone()` nor `.to_owned()`.

Fail-closed cases were exercised in both modes:

- Checker producer: drop, contradict-existing, and perturb of the local specialized ReturnValue EST fact all fail closed.
- Native and Fast Debug consumers: drop/duplicate requirement, drop/duplicate fact, perturb fact, wrong context, and wrong specialization all fail closed. Failed native generation leaves no Rust artifact.

## Package `.mossi` source-context behavior

Ran:

```sh
python3 tests/tooling/check_phase27_est_mossi.py "$PWD/tmp/team-c-4c4866-build/moss-dev"
```

Result:

```text
phase27 .mossi EST passed (raw default/shadow hashes={'library': '4992b4eb9d1f9e6241336c15da586c92e74fb64ab24d62521cc57e7bd9d2b035', 'app': 'ce6573487a381590f7dfebc911f90b19056ec72f72de8817ebdc9fe1626c0935'}, generic=1, Section2=2 imported dumps)
```

The test independently builds package provider and consumer `.mossi` artifacts in Default and Shadow and asserts raw byte equality. It imports the provider artifact and verifies generic and Section 2 EST dumps, then injects `MOSS_TEST_DROP_RETURN_VALUE_SOURCE_EST_FACT=1`; the package consumer check rejects with `specialized return fact does not reference its checked EST occurrence`. Thus a missing imported source-context fact remains fail-closed.

`git diff --check c043ea07eb2e284f69302ea08ebd4a96fa586b35 c4a48668b7a1adee0528efa8e8ff1a7770231e54` passed.

## Team C scoped verdict

**PASS for the local specialized ReturnValue authority slice at exact source SHA `c4a48668`.** Default/Shadow producer and consumer corruption gates, package `.mossi` source-context-drop, and no-implicit-copy checks pass. This is scoped evidence only and is not a whole-phase GO.
