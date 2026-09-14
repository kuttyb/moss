# Moss tooling and source provenance

Phase 5 uses one compiler-emitted provenance artifact for editor navigation,
debugging, and native-code inspection. Emacs, LLDB, and objdump do not infer
Moss structure by parsing generated Rust.

```text
Moss source
    |
    +-- generated Rust + DWARF + native executable
    |
    `-- program.mossmap
             |
             +-- moss-mode navigation
             +-- LLDB/lldb-dap source breakpoints
             `-- symbol-targeted objdump views
```

All editor and debugger integrations are optional. The compiler has no runtime
dependency on Emacs, LLDB, Python, `lldb-dap`, or objdump.

## The `.mossmap` contract

Every normal compilation emits a JSON map beside its Rust output. For example:

```sh
./moss examples/functional_dataflow.moss -o build/dataflow.rs
```

creates `build/dataflow.rs` and `build/dataflow.mossmap`. Override the map path
and record the intended executable with:

```sh
./moss examples/functional_dataflow.moss \
  -o build/dataflow.rs \
  --emit-debug-map build/dataflow.mossmap \
  --native-output build/dataflow
```

The version-1 map records:

- absolute Moss and generated-Rust paths;
- one-based Moss source spans and generated Rust ranges;
- stable semantic identities derived from source context and location;
- construct kinds for functions, methods, handlers, domains, types, `main`,
  functional pipelines, functional nodes, and shared dataflow groups;
- deterministic generated and native symbol names where a concrete native
  function exists;
- per-line Moss-to-Rust mappings;
- all source origins contributing to a fused generated region.

Dense numeric pipeline/node IDs are compiler-local handles and never appear in
the map. Repeating an identical build at the same paths produces byte-identical
metadata. A generic implementation without a concrete specialization may not
have a native symbol; its generated symbol and source mapping remain available.

Many-to-one entries are intentional. When `map`, `filter`, and `sum` become one
loop, each semantic node points to the same generated range and each entry lists
the full contributing provenance set.

## Debug builds

`--debug` selects the `-O0` eager/mailbox reference lowering, adds stable
function boundaries, and emits deterministic native symbols. It produces Rust;
the supplied wrapper also invokes `rustc` with DWARF, frame pointers, no
stripping, and warnings denied:

```sh
tools/moss-build-debug examples/functional_dataflow.moss \
  -o build/debug/dataflow
```

The result is:

```text
build/debug/dataflow
build/debug/dataflow.rs
build/debug/dataflow.mossmap
```

`--debug` cannot be combined with `--cluster`, because its contract is the
unoptimized source-fidelity baseline. Optimized compilations still emit valid
maps and retain fused provenance, but several Moss constructs may share one
machine-code location.

Use `--diagnostic-paths` when invoking the compiler from an editor. Diagnostics
then have the conventional clickable form:

```text
/path/to/program.moss:12: error: ...
```

The default command-line diagnostic prefix remains `moss:12` for compatibility.

## Emacs `moss-mode`

Add the repository directory to `load-path`:

```elisp
(add-to-list 'load-path "/path/to/moss/editors/emacs")
(require 'moss-mode)
```

Opening a `.moss` file then enables:

- `#` comments and string-aware syntax;
- two-space, tab-free block indentation with `else` dedenting;
- font lock for declarations, control flow, concurrency, builtin types, and
  functional operations;
- Imenu categories for functions, types/traits, domains, methods, and handlers;
- basic beginning/end-of-definition navigation.

The primary commands are:

| Command | Purpose |
| --- | --- |
| `M-x moss-check-buffer` | statically check the current source |
| `M-x moss-compile-buffer` | compile through Rust to a native executable |
| `M-x moss-run-buffer` | compile and run |
| `M-x moss-build-debug-buffer` | produce the `-O0` DWARF debug build |
| `M-x moss-show-generated-rust` | open generated Rust read-only |
| `M-x moss-show-debug-map` | open the shared JSON map read-only |
| `M-x moss-goto-generated-rust` | jump from Moss to its generated location |
| `M-x moss-jump-to-moss-source` | jump from generated Rust to Moss |

Check, compile, build, and run commands use Emacs `compilation-mode`. Moss
diagnostics are clickable. Artifacts are kept under
`build/emacs/<source-name>/`; users do not need to derive their filenames.

Useful default keys are:

```text
C-c C-c  check
C-c C-b  compile
C-c C-r  run
C-c C-g  generated Rust
C-c C-d  toggle pending Moss breakpoint
```

The mode is dependency-light. `dape` is loaded only when debugging is requested.
Projects need not live inside the Moss checkout: customize
`moss-compiler-command` when the compiler is not on `PATH`, and
`moss-lldb-script` only when the LLDB bridge was installed separately from the
major mode.

## LLDB and DAP debugging

Install LLDB with `lldb-dap` and the optional Emacs
[`dape`](https://github.com/svaante/dape) package. The compiler does not require
either tool. The supported transport is:

```text
Emacs moss-mode -> dape -> lldb-dap -> native Moss executable
```

From a Moss buffer:

1. Run `M-x moss-build-debug-buffer` and wait for compilation to finish.
2. Place one or more breakpoints with `M-x moss-toggle-breakpoint`.
3. Run `M-x moss-debug`.

`moss-debug` gives `dape` an ordinary `lldb-dap` launch configuration. LLDB
loads `tools/moss_lldb.py` and the program's `.mossmap`; pending Moss lines are
translated to generated-Rust breakpoint locations after the native target is
created. On each stop, `moss-mode` uses dape's public source-display hook to
show the exact mapped Moss line. It leaves range-only or ambiguous optimized
locations in generated source rather than presenting them as precise Moss
steps. No editor-specific mapping is embedded in the compiler.

When one Moss line maps to several native locations, the integration selects
the first location in deterministic generated-file/line order and reports that
choice. Scalar locals and ordinary structs use LLDB's DWARF variable support;
collection presentation is whatever the installed Rust/LLDB toolchain exposes.

The LLDB bridge can also be used outside Emacs:

```text
(lldb) command script import /path/to/moss/tools/moss_lldb.py
(lldb) target create build/debug/dataflow
(lldb) moss-map-load build/debug/dataflow.mossmap
(lldb) moss-break /absolute/path/functional_dataflow.moss:12
(lldb) run
(lldb) moss-where
(lldb) moss-stack
```

`moss-stack` hides unmapped runtime frames by default; `moss-stack --all` shows
them too. Native LLDB commands such as `frame variable` remain available.

Precise Moss stepping is best in `--debug` builds. DWARF describes generated
Rust locations, and the map translates meaningful stops back to Moss. Fused
nodes or several source operations on one generated statement cannot honestly
offer distinct machine-code steps; the tooling reports their shared provenance
instead of inventing fake stops.

## Disassembly and navigation

Build the program, place point inside a Moss function, method, or handler, then
use any of:

```text
M-x moss-disassemble-function
M-x moss-disassemble-at-point
M-x moss-show-assembly
```

The commands resolve the semantic construct and exact native symbol through the
`.mossmap`, prefer `llvm-objdump`, fall back to GNU `objdump`, and display the
result in a read-only `asm-mode` buffer. The buffer header includes semantic
identity and every provenance origin. With debug information available, objdump
also interleaves generated-source lines.

For a fused optimized pipeline, compile with `M-x moss-compile-buffer` while
`moss-compile-optimization` is `"-O"`. The resulting symbol contains the fused
loop, while its assembly buffer lists the originating map/filter/reduction
nodes. This is Moss-level provenance even though the physical loop is one native
region.

Reverse assembly-to-Moss navigation is currently approximate: symbol-level
provenance is shown in the assembly header, while exact address-to-Moss line
selection remains constrained by the DWARF locations emitted by `rustc`.

## Capability-gated tests

`make check` always validates map determinism and structure. It additionally
runs:

- ERT tests when Emacs is installed;
- map/LLDB helper tests when Python 3 is installed;
- native symbol disassembly when llvm-objdump or GNU objdump is installed;
- a live source-breakpoint/local inspection test when both LLDB and `lldb-dap`
  are installed.

Missing optional tooling prints a skip message and does not make the compiler
suite fail.
