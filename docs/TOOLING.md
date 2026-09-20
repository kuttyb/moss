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
- deterministic source/provenance identities derived from source context and
  location;
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

The JSON field remains named `semantic_identity`, but its Phase 5 contract is
deliberately narrower than a durable entity ID: it is deterministic for an
unchanged source layout and across that build's lowering modes. Moving a
declaration or functional stage can change its line-derived identity. Phase 6
may add durable semantic entity identities that survive ordinary source edits;
debuggers and editors must not assume the Phase 5 value already has that
property.

Many-to-one entries are intentional. When `map`, `filter`, and `sum` become one
loop, each semantic node points to the same generated range and each entry lists
the full contributing provenance set.

## Debug builds

`--debug` selects the `-O0` eager functional lowering and plan-driven domain runtime, adds stable
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

The retired `--cluster` option is removed. Optimized compilations still emit valid
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

Install `dape` with the Emacs package manager of your choice; the only mode-side
requirement is that this succeeds before debugging:

```elisp
(require 'dape)
```

`moss-mode` loads it lazily when `M-x moss-debug` is invoked.

On Debian, the adapter is commonly installed under a versioned name such as
`/usr/bin/lldb-dap-19`; `moss-mode` discovers that form automatically. The
`moss-lldb-dap-command` customization remains available for other layouts.
For Debian 13 (trixie), the tested setup is LLDB 19.1.7 with `lldb` and
`lldb-dap-19` from the `lldb-19` package:

```sh
sudo apt install lldb-19
command -v lldb
command -v lldb-dap-19
```

An unversioned `lldb-dap` on `PATH` takes priority. Otherwise Moss selects the
highest numeric `/usr/bin/lldb-dap-N` deterministically. It does not fall back
to `lldb`, GDB, or another debug adapter. Customize
`moss-lldb-dap-command` when the intended compatible adapter lives elsewhere.

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
the first generated file/line in deterministic order; LLDB may resolve that
generated line to several honest machine locations. Breakpoints require an
exact `.mossmap` line mapping. A blank, comment-only, or range-only location is
rejected with `Moss breakpoint has no exact generated mapping` rather than
being moved silently to a nearby statement.

Scalar locals and ordinary structs use LLDB's DWARF variable support. The live
Debian regression inspects an integer, boolean, user type, `String`, and
`Vector`. This LLDB build has no Rust language presentation plugin, so `String`
and `Vector` are inspectable as raw Rust storage (including their lengths) but
are not rendered as polished Moss values. No custom pretty-printer is promised
in Phase 5.

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

The end-to-end DAP regression verifies `next` from one ordinary Moss assignment
to the following exact Moss assignment in `--debug` mode. A step may also land
on generated code with no exact Moss origin; in that case Emacs deliberately
leaves the generated source visible. Phase 5 does not promise statement-level
stepping through optimized or fused code.

Startup failures are reported at the Moss tooling boundary: missing debug
executable, missing `.mossmap`, missing/non-executable `lldb-dap`, missing LLDB
Python helper, a non-exact breakpoint, or a synchronous DAP launch failure.
The DAP console remains available for lower-level LLDB details when useful.

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
- a live LLDB command-bridge breakpoint/stack test when the `lldb` CLI is
  installed;
- a separate real DAP source-breakpoint, stepping, stack, and local-inspection
  test whenever `lldb-dap` and Python 3 are installed. The DAP test does not
  depend on the separate `lldb` CLI executable.

The real DAP test performs `initialize`, launches with the same
`initCommands`/`preRunCommands` as `moss-debug`, confirms helper/map loading and
native breakpoint resolution, exercises function/method/handler stops and
exact step-over, inspects locals, correlates user and runtime stack frames, and
lets the debuggee exit with status zero before disconnecting.

Missing optional tooling prints a skip message and does not make the compiler
suite fail. The live debugger test also reports a capability skip when an OS or
container explicitly denies process tracing; installed tooling that can launch
the fixture but fails the Moss integration remains a test failure.
