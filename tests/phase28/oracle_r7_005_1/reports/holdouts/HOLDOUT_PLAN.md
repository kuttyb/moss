# Reviewer-controlled P28-R7-005.1 holdouts

Compiler target: frozen R7-008 source SHA `e9d3f844bae715809f6b78f4d0d0f8ba908db6bc`, built clean in this detached worktree. These source files are new, temporary probes and are not copied from the 60 checked-in oracle fixtures.

## Expected outcomes fixed before execution

1. `01_primitive_owner.moss`: ACCEPT. A consuming `Int -> Int` helper may receive a primitive Vector element; a later indexed read and a read-only loop must still observe the Vector. Expected stdout `8\n8\n8\n9` in Fast Debug, native debug, and native -O. Rationale: the R7-009 requested primitive-element contract treats Copy scalar elements as values while preserving the Vector owner; the nonprimitive collection-element consume restriction remains in force.
2. `01b_nonprimitive_control.moss`: REJECT with `OWNERSHIP_COLLECTION_ELEMENT_CONSUME`. A nonprimitive collection element is passed to a consuming helper and may not be implicitly transferred from an indexed borrow. Rationale: Phase 28 proposal §5.3/§12 and frozen ORACLE-03.
3. `02_map_get_write_through.moss`: ACCEPT. `Map.get(key)` yields a writable borrow of the stored value; after changing its String field, reading the alias and looking up the entry both produce `new`. Expected stdout `new\nnew` in Fast Debug, native debug, and native -O. Rationale: certified R7-005 language addendum's Map.get borrowed-V contract and map write-through behavior.
4. `03_specialization_collision.moss`: ACCEPT. One untyped generic relay is called at distinct `LeftCell` and `RightCell` record specializations; each result retains its own type/value. Expected stdout `41\nready` in Fast Debug, native debug, and native -O. Rationale: checked semantic facts are isolated by concrete specialization identity; no callsite may reuse another specialization's answer.
5. `04_shadowed_function_name.moss`: ACCEPT. A loop-local and a one-branch local named `offset` shadow the sibling function only within their scopes; after those scopes the function call still resolves. Expected stdout `99\n7\n7\n7` in Fast Debug, native debug, and native -O. Rationale: established Moss lexical binding identity and Phase 15 shadowing acceptance contract.

Only after recording these expectations will the runner invoke the compiler. Negative control is checker-only because accepted-to-rustc/runtime parity does not apply to a source expected to be rejected.
