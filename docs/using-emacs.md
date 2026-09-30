Assuming Phase 22.5 lands as designed, the best way to use Moss in Emacs is to treat Emacs as a thin semantic frontend to the Moss compiler, not as a text editor doing guesses.

Open any `.moss` file and `moss-mode` should activate automatically. Your normal editing loop becomes: write code, let semantic completion guide you, use compiler-backed navigation to understand unfamiliar code, and run `moss-check-buffer` frequently.

The small set of commands worth memorizing is:

- `M-.` — go to the declaration/definition of the symbol at point.
- `M-?` — find all semantic references to the symbol at point.
- `M-x xref-find-apropos` — search functions, types, domains, handlers, methods, etc. across the Moss project.
- `M-x completion-at-point` — semantic completion. Usually bind/use whatever completion UI you already like; stock Emacs works.
- `M-x moss-callers` / `moss-callees` / `moss-call-tree` — understand control flow.
- `C-c C-c` — check the current Moss program.
- `C-c C-b` — native build.
- `C-c C-r` — build and run.
- `C-c C-d` — toggle a Moss breakpoint.
- `M-x moss-debug` — source-level native debugging through LLDB/DAP.
- `C-c C-g` — jump from Moss to generated Rust.
- `M-x moss-disassemble-at-point` — inspect native code for the function/handler at point.

### Writing ordinary Moss

Use completion heavily rather than remembering every API. For example:

```moss
fn total_positive(values: Vector[Int]) -> Int:
  values
    .filter(fn(x): x > 0)
    .sum()
```

After typing:

```moss
values.
```

completion should offer only members meaningful for `Vector[Int]`.

On `filter`, `sum`, or `total_positive`, `M-.` takes you to the corresponding semantic declaration. `M-?` finds actual uses rather than same-name text matches.

That becomes particularly useful once a project has multiple modules.

### Navigating a Moss program

Suppose you encounter:

```moss
let result = normalize(parse(input))
```

Put point on `parse` and use `M-.`.

Then:

```text
M-x moss-callers
```

shows who calls `parse`, while:

```text
M-x moss-call-tree
```

lets you inspect the whole static call structure.

Because Moss is statically resolved and deliberately avoids dynamic dispatch in this model, the call hierarchy should be especially useful: it represents what the compiler actually resolved rather than an IDE approximation.

### Domains

This is where semantic editor support becomes unusually valuable.

For example:

```moss
domain Counter:
  var value: Int = 0

  on Add(n: Int):
    value = value + n
    reply value
```

Elsewhere, when you have a legal route to `Counter`, completion around the message target should show the handlers you can actually invoke.

That means you don't need to remember every route or handler name. The editor can use the compiler's closed domain topology to tell you what is legal.

On a handler invocation:

```text
M-.
```

takes you directly to the handler.

`M-?` tells you every place the handler is messaged.

And the call tree should distinguish:

```text
ordinary call
```

from:

```text
synchronous message → handler
```

which makes understanding concurrent Moss programs much easier.

### Use the compiler as your documentation

Moss infers a lot: ownership, effects, specialization, synchronization and domain behavior. You should not have to reconstruct those facts mentally from syntax.

For normal editing, Emacs exposes the common pieces through completion/xref.

When you need deeper information, drop to Moss's semantic commands from a terminal:

```sh
./moss inspect <symbol> --source src/main.moss --json
./moss effects <symbol> --source src/main.moss --json
./moss ownership <symbol> --source src/main.moss --json
./moss calls <symbol> --source src/main.moss --json
./moss why <symbol> --source src/main.moss --json
```

So the mental model is:

**Emacs for navigation and programming; Moss semantic queries when you want to understand why the compiler reached a conclusion.**

### Functional pipelines

For the functional side of Moss, write naturally:

```moss
let result =
  values
    .filter(fn(x): x > threshold)
    .map(fn(x): x * x)
    .sum()
```

Completion helps with available pipeline operations, while Moss remains free to optimize the pipeline underneath you.

If you're curious what it became:

```text
C-c C-b
C-c C-g
```

shows the generated Rust.

For the actual machine code, put point inside the function and run:

```text
M-x moss-disassemble-at-point
```

This is a nice Moss workflow because you can move through three abstraction levels without leaving Emacs:

```text
Moss source
   ↓
generated Rust
   ↓
native assembly
```

while the `.mossmap` preserves the relationship between them.

### Debugging

For ordinary bugs, first use:

```text
C-c C-c
```

because many Moss mistakes—ownership, invalid message topology, illegal mutation, effects—should be caught statically.

For runtime logic bugs:

```text
M-x moss-build-debug-buffer
```

place a breakpoint with:

```text
C-c C-d
```

then:

```text
M-x moss-debug
```

You should land back in Moss source even though LLDB is physically debugging generated Rust.

For harder failures, Phase 22 also gives you structured Fast Debug queries such as control-flow slices and write history. Those are particularly useful when you don't want to single-step a large execution.

### The overall workflow

The effective Moss/Emacs loop is therefore:

**complete → navigate → check → inspect call structure → run → debug → inspect lowering only when needed.**

The key shift is that you should rarely need to grep for symbols or manually trace relationships. `M-.`, `M-?`, semantic search, and the call tree should make the **compiler's understanding of the program directly navigable**.

That fits Moss especially well because so much of the language's power—ownership, static dispatch, domains, synchronization, specialization, and functional optimization—is compiler-derived rather than written explicitly in the source.
