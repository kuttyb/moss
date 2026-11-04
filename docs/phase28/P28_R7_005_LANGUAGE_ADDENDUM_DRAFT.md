# P28-R7-005 Language Addendum — Certified Frozen

**Language semantics: APPROVED by L7. Oracle: CERTIFIED FROZEN.** Independent
Agent 2 returned GO for candidate `d37ea08fa853c994b69c16bd90671ffcf34c72ea`;
see `tests/phase28/oracle/certification.json` for approval and holdout details.

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

## Clone provenance

See [P28-R7-005 Clone Provenance Contract](P28_R7_005_CLONE_PROVENANCE_CONTRACT.md)
for the revised v2 sidecar schema, source-copy-to-emission relation, independently
checkable evidence, scanner limits and fail-closed controls. This oracle task
does not modify compiler source; missing provenance remains an unresolved gate.

## Oracle data ownership

`tests/phase28/oracle/expectations.json` is the complete, independent expected
behavior table. `diagnostic_mapping.json` is the oracle-owned normalization
from stable defect/fixture IDs to accepted checker diagnostic codes. Engine
results are stored separately for every fixture and engine. Defect IDs are
permanent and fixture-derived; report ordering never allocates identifiers.

