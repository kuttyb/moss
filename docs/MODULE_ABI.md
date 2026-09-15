# Moss module interface and ABI

Moss owns semantic contracts; Rust owns materialized code and native linkage.
The build emits normal Rust artifacts for the final program and a versioned
`<module>.mossi` semantic interface for explicit modules. `.mossi` is metadata,
not a Moss object-file format and does not serialize rustc MIR.

The interface records the project-qualified `ModuleId`, compiler compatibility,
backend/toolchain fingerprint, imports, and public declarations.

For concrete exports it records:

- the closed signature;
- parameter READ/WRITE/CONSUME modes;
- observable effects;
- await/domain contract;
- the materialized semantic identity.

For generic exports it records the semantic body hash, open parameter positions,
inferred structural requirements, and dependencies needed for specialization.
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
