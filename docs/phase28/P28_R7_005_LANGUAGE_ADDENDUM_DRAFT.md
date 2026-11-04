# P28-R7-005 Language Addendum — Draft for L7 Approval

**Status: PROPOSED. Not frozen.** The approved L7 decisions below are the requested
oracle contract; this addendum and its expectation table remain pending explicit
L7 approval. The oracle owner cannot approve or freeze them.

## Collection and error semantics

`copy` is a reserved source word. A user declaration named `copy`, including
`fn copy(...)`, is rejected by the parser/checker; this spelling is reserved for
the language's explicit duplication operation.

For `Map[K, V]`, one-argument `map.get(key)` returns a borrowed `V`. If the key
is absent, it raises `CollectionError.MissingKey`. It does not return
`option[V]`. Map operations have these distinct contracts:

| Operation | Present key | Missing key |
| --- | --- | --- |
| `map[key]` read | Borrowed value | Raises `CollectionError.MissingKey` |
| Write through `map[key].field` | Writes the existing value | Raises `CollectionError.MissingKey` |
| `map[key] = value` | Replaces the value | Inserts the value; key absence is not an error |
| `map.get(key)` | Borrows the value | Raises `CollectionError.MissingKey` |
| `map.remove(key)` | Transfers the removed value | Raises `CollectionError.MissingKey` |

For Vector indexing, `vector[index]` outside the valid range panics and is not
recoverable. `vector.remove(index)` and `vector.swap_remove(index)` raise
`CollectionError.InvalidIndex` for an invalid index. Empty Vector and Queue
`pop()` raise `CollectionError.Empty`.

These typed collection failures participate in inferred raise sets. The effect
summary is preserved through imported `.mossi` interfaces, `try`/`recover`
ownership analysis, and Root totality checks. A Root must declare a handler for
each propagated `CollectionError` variant that can escape its checked entry
path. A Root lacking the required handler is rejected. Panics remain outside
typed recovery and do not create a `recover` edge.

## Clone provenance contract for native emission

The oracle audits the complete generated Rust artifact for both debug and
optimized builds, including user helpers emitted before `main`. It does not
estimate hidden clones by subtracting an aggregate count.

The emitter must write `<generated-rust-path>.mossclone.json` with format
`moss-clone-provenance`, version `1`, the `mode` (`debug` or `optimized`),
SHA-256 digests for the generated Rust and originating Moss source, and one row
for every `.clone()` call occurrence in the generated artifact. Each row has:
Source and Rust coordinates are 1-based, with end positions exclusive.

```json
{
  "rust_span": {
    "start": {"line": 7, "column": 18},
    "end": {"line": 7, "column": 26}
  },
  "origin_kind": "explicit_copy",
  "source_span": {
    "start": {"line": 3, "column": 8},
    "end": {"line": 3, "column": 24}
  }
}
```

`origin_kind` is one of `explicit_copy`, `compiler_runtime_handle`, or
`runtime_support`. `explicit_copy` rows must point to the exact complete
`copy(expression)` call occurrence in Moss source. The oracle independently
lexes Moss source (excluding comments and literals), enumerates those calls, and
compares their source spans with the manifest. The manifest must cover every
generated Rust clone occurrence exactly once, and each source explicit-copy
occurrence must be represented. Any missing, duplicate, stale, extra, or
unclassified occurrence fails the fixture. The same checks run against the
debug and optimized Rust outputs independently.

Current `.mossmap` construct and line mappings are not an occurrence-level
clone provenance interface. Until the emitter supplies this artifact, fixtures
with generated clone occurrences fail closed as `PROVENANCE_MISSING` or
`INVALID_PROVENANCE`; this oracle task does not modify compiler source.

## Oracle data ownership

`tests/phase28/oracle/expectations.json` is the complete, independent expected
behavior table. `diagnostic_mapping.json` is the oracle-owned normalization
from stable defect/fixture IDs to accepted checker diagnostic codes. Engine
results are stored separately for every fixture and engine. Defect IDs are
permanent and fixture-derived; report ordering never allocates identifiers.

