# Expression parser/evaluator surface exercise

This standalone Moss package evaluates a fixed, already-parsed expression plan.
An `Expression` carries two operands, an opcode, enablement, and priority. The
demo evaluates four binary calculations, projects them through a `map → filter →
map → sum` pipeline, indexes the resulting vector, and uses a `Map` for named
result lookup.

From this directory:

```sh
../../../../margo run
../../../../margo test --json
../../../../margo debug --trace
```

The intended output is:

```text
accepted expression values 26 28
expression total 54
projection and map lookup 80
```

See [REPORT.md](REPORT.md) for compiler-led iteration notes, preserved failed
attempts, validation, and swarm-ledger overlap.
