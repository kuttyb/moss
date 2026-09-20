# Moss module interface and ABI

Moss owns semantic contracts; Rust owns materialized code and native linkage.
The build emits normal Rust artifacts for the final program and a versioned
`<module>.mossi` semantic interface for explicit modules. `.mossi` is metadata,
not a Moss object-file format and does not serialize rustc MIR.

The interface records the project-qualified `ModuleId`, compiler compatibility,
backend/toolchain fingerprint, imports, and public declarations.

The materialized artifact is an ordinary Rust `rlib` generated for that one
Moss module. Typed exports resolve to the provider's materialized Rust symbols;
the importing compiler needs no implementation source. Generic semantic IR is
the durable Moss representation for deferred compilation. It is a versioned
structured representation of Moss declarations/statements and dependency
closure, not source text and not rustc-private MIR.

For concrete exports it records:

- the closed signature;
- parameter READ/WRITE/CONSUME modes;
- observable effects;
- synchronous message/reply domain contract (historical await metadata is legacy);
- the materialized semantic identity.

Exported domain handlers also retain the message-payload contract: every
incoming payload parameter has READ-only capability. It may be inspected or
forwarded through another explicit message, but cannot be mutated, consumed,
reassigned, stored by move, or returned as the original snapshot, regardless of
whether its concrete type is `Copy`. This contract
is preserved when a module is specialized and is independent of whether the
final backend uses a mailbox or a synchronous shared-memory reference.

For generic exports it records the semantic body hash, open parameter positions,
inferred structural requirements, and dependencies needed for specialization.
The current IR records parameter identities, constraints, statement/expression
fields, result expressions, and private helper dependency records. A final
build owns one deterministic specialization crate, keyed by module, generic
entity, generic semantic hash, and concrete type tuple.
The canonical specialization identity is the module identity, generic entity
identity, generic semantic hash, and concrete type tuple. A specialization is
materialized once per final build regardless of how many modules request it.

Optimization level and backend flags belong to the native artifact/cache key,
not to the semantic `ModuleId`. A changed Rust compiler/toolchain fingerprint
rebuilds materialized artifacts; Moss does not promise a stable binary ABI
across arbitrary rustc versions.

Await metadata uses exact statically declared domain-instance identities. Two
bindings created by `spawn Worker()` are separate nodes. The final composed
program unions those edges and runs the existing DFS; domain type names are
never used as instance identity.

For a typed callable that invokes a domain through a parameter, the contract uses
`target=parameter[N]` plus the handler/domain. Final linking substitutes the
caller's exact declared instance (for example `app::worker1`) before running
the same legacy dependency machinery. Internal module-owned instances remain qualified and
opaque.
