# First and Second modules

This project demonstrates the current explicit-module workflow:

```text
phase10_modules/
  moss.toml
  src/
    first.moss   # module First
    main.moss    # module Second, imports First
```

`Second` imports the exported `First.answer` and `First.add_bonus` functions,
then calls them from its `main`. From this directory, run:

```text
moss build
moss debug .
```

The debug command interprets the complete reachable Moss closure (`Second →
First`) and prints `42` without invoking `rustc`. The native build produces the
same observable result through the Rust backend.
