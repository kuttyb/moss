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
- await/domain contract;
- the materialized semantic identity.

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

For a typed callable that awaits through a domain parameter, the contract uses
`target=parameter[N]` plus the handler/domain. Final linking substitutes the
caller's exact declared instance (for example `app::worker1`) before running
the same await DFS. Internal module-owned instances remain qualified and
opaque.
