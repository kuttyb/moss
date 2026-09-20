# Moss module interface and ABI

Domain route declarations are structural metadata. Domain handles are not ABI
payload values: they cannot be ordinary or handler parameters, reply results,
state data, or aggregate elements. `.mossi` retains declared route slots and
nominal target domains; composition binds concrete instances and their exact
specializations in the final application. Final `domain_rank` and
synchronization-class facts are not module ABI properties.

The interface now includes `construction_state` records (member name, type,
default initializer) and `handler_param` records (handler, parameter name, type),
alongside existing `route` declarations. These support the same structural
checks after provider source is removed. Rebuild older provider interfaces when
these records are needed; missing metadata is not permission for dynamic routing.
Exported handlers have a synchronous generated bridge that keeps crate-private
legacy transport types inside the provider. This is backend compatibility
plumbing, not a new Moss calling convention for passable handles.
With provider source available, module projection retains the application's
authoritative per-instance specialization records. Different inferred private
layouts are materialized in their owning provider crate; application routes use
the generated nominal adapter without redefining source type or instance identity.
The source-free regression covers concrete exported domains. This does not add
a new generic-domain semantic serialization format.

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
reassigned, or stored by move. A reply is a new semantic value boundary, so
replying the incoming value by value is legal regardless of whether its
concrete type is `Copy`. This contract
is preserved when a module is specialized and is independent of whether the
final backend uses a mailbox or a synchronous shared-memory reference.

Exported domains may also carry structural `route name: Domain` declarations
from `domainroutes(...)`. These describe route edges for final application
composition but never carry a whole-program `domain_rank`; ranks are assigned
only after `main` constructs the concrete graph.

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

Legacy await metadata uses exact statically declared domain-instance identities.
Two bindings created by the retired `spawn Worker()` spelling were separate
nodes. The final composed
program unions those edges and runs the existing DFS; domain type names are
never used as instance identity.

For a typed callable that invokes a domain through a parameter, the contract uses
`target=parameter[N]` plus the handler/domain. Final linking substitutes the
caller's exact declared instance (for example `app::worker1`) before running
the same legacy dependency machinery. Internal module-owned instances remain qualified and
opaque.

## Phase 10.6C handler state effects

Exported domains include `handler_state_effects` semantic records with counted
READ, WRITE, and CONSUME leaf lists, including explicit empty lists. These records
participate in semantic interface hashing and let source-free `.mossi` consumers
derive the same graph-relative synchronization plan. Missing records fail closed
with a request to rebuild the provider. They do not export synchronization classes,
class ranks, or lock-layout ABI. See [SynchronizationPlan](SYNCHRONIZATION_PLAN.md).

Concrete exported helpers similarly carry `parameter_leaf_effects` records.
They preserve formal READ/WRITE/CONSUME paths for substitution onto a caller's
state even when only the helper's compiled interface remains. Missing helper
records also require rebuilding the provider; an absent body is not proof of
an empty effect. These records participate in the semantic interface hash.

Phase 10.6D uses native ABI version 2 for inferred primitive WRITE references and
plan-driven domain constructors. Old providers must be rebuilt. This native
compatibility marker carries no synchronization-class ABI: the final application
supplies its compiler-derived descriptor at construction, including for source-free
providers. `.mossi` continues to export semantic handler/formal leaf effects only;
class IDs, ranks, ClassSets, and lock storage are not serialized as provider ABI.
