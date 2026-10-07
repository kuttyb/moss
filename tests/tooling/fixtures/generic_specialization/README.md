# Concrete static specialization regressions

Run `python3 tests/tooling/check_generic_specialization.py ./moss` from the
repository root. `tests/run.sh` runs this general suite; the Phase 20 reviewer
suite also has focused String, builtin callable and domain WRITE guards.
Existing reviewer chunk-callable probes cover both expression and explicit
return forms without duplicating their FileIO setup here.

The manifest fixes native stdout, Fast Debug stdout and relevant concrete
parameter effects. The suite checks, lowers, compiles with `rustc -O -D warnings`,
runs, and queries type/effects/ownership/calls/inspect for every source.
Negative probes require the exact structured diagnostic code. FileIO runs
natively; the Executor/for stress source runs natively because Fast Debug does
not support for iteration. The ordinary message WRITE probe has full parity.
The typed and generic 2000-invocation stress probes must produce identical
semantic synchronization plans, including `items` WRITE and EXCLUSIVE locking.

`--baseline` records failures without failing the runner, for comparison with
an unchanged reference compiler. All scratch output stays under
`tmp/generic-specialization/`. These fixtures preserve the minimal A–D sources,
both tuple call orders, ownership transfer/use-after-consume, builtin READ and
WRITE receivers, nested helpers, local binding types across tuples, indexed-result snapshots, and the
existing typed/String/Float/user-object/Range/FileIO behavior.
