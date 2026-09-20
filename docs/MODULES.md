# Moss modules

Phase 8 modules are logical compilation units. A module is declared with
`module name`; any number of `.moss` files may declare the same module. Source
file order is not semantic.

```moss
module pricing

export fn notional(value: Int) -> Int:
  value * 2
```

An importing file names the module and uses qualified symbols:

```moss
module app
import pricing

fn main():
  value = pricing.notional(21)
```

Declarations are private by default. `export fn`, `export type`, `export
trait`, and `export domain` form the public surface. Wildcard imports, aliases,
namespace merging, and multiple providers for one module are intentionally not
part of Phase 8.

Typed exports are materialized module ABI: their arguments and result have a
closed concrete Moss signature, ownership modes, effects, and await contract.
They are checked and lowered by the exporting module.

An export with unresolved parameters is a compile-time Moss generic. Its
semantic body, structural requirements, and semantic hash are recorded in the
versioned `.mossi` interface and specialized at concrete call sites. No
runtime generic dispatch is generated. Generic exported functions may not take
domain-typed parameters.

Imports form an acyclic dependency graph. Module-qualified nominal types remain
distinct (`pricing::Quote` is not `market::Quote`), and exported domains remain
opaque: callers see their public handlers and contracts, not state or backend
details.

Projects without `module` declarations continue to use the implicit project
module used by earlier Moss phases.

Domain helper scoping remains an ordinary language/module concern: common handler
logic should live in ordinary helpers with normal static call resolution. This
document does not add implicit same-domain handler chaining or self-send rules.
Concrete domain topology and any domain rank are whole-program composition/link
facts, not declarations inferred from a directory or a local handler.

## Separate compilation

An explicit module is compiled in import-DAG order to its own generated Rust
crate and conventional `lib<module>.rlib`. A consumer crate receives the
dependency with rustc `--extern`; dependency declarations are not regenerated
in the consumer. The final executable is compiled from the root module and
links those rlibs normally.

The build also writes `<module>.mossi`. A typed export is consumable from that
interface plus the provider rlib without provider source. Generic exports carry
versioned structured Moss semantic IR and their private semantic dependency
closure; they are specialized once in the final-build
`libmoss_specializations.rlib`. Rustc MIR/rmeta is never Moss's interchange
format.

When a provider is source-independent, place its `.mossi` and rlib in the
consumer build/dependency path (or `MOSS_MODULE_PATH`). Interface and provider
artifact hashes participate in downstream invalidation.
