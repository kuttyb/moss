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
