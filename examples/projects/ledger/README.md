# Ledger dogfooding seed

Four modules model a checkout service, ledger, journal, and priced orders.
`Checkout` has static routes to two domain instances, forwards an independent
Order value, and reports balances synchronously. `Ledger` uses an ordinary
primitive WRITE helper. The model declares a structural `Priced` predicate;
`Order` has its required method without an implementation declaration. The
standalone `examples/traits.moss` seed exercises trait-constrained specialization.

From this directory, with the repository compiler on PATH:

```sh
moss build --release --json
./build/release/ledger
moss debug app
moss run --interp --trace src/main.moss
```

Both engines print:

```text
136
150
150 2 14
```

The replies expose final ledger balance and journal state. Change prices, add
requests, or extend the journal during Phase 15. Related seeds are
`examples/dogfood_index.moss` and `examples/dogfood_pipeline.moss`; the latter
uses functional analytics in production. Fast Debug does not yet execute pipelines.

The v0.1 exercise found real module friction, recorded rather than redesigned:

- Use one application main in a project for the normal build workflow; a second
  analytics main in the source tree produces duplicate-main diagnostics.
- Use exported factories for provider-owned objects; direct constructor lowering
  can reach Rust's private-field diagnostics.
- Cross-module and source-free package generic structural dispatch is statically
  specialized into the artifact containing the concrete call. The structural
  trait itself does not imply a runtime object or dynamic dispatch.
- Keep mutation helpers local to their module; some qualified statement calls
  are not handled by the current rewrite. Qualified expression calls are used
  for the model's concrete value-returning helpers.

A transitive native-link failure discovered here was a backend artifact-name
issue: rustc's dependency search needs crate-name rlib aliases. Phase 10.6F fixes
that without changing Moss module semantics. Tests build a clean copy and also
consume the model as a source-free provider; Fast Debug still requires its source.
